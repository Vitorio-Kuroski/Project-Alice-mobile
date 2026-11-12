// window_android.cpp
//
// Implementacao Android da interface declarada em window.hpp -- sem GLFW.
//
// Ciclo de vida (android_native_app_glue -> handle_cmd):
//   APP_CMD_INIT_WINDOW  -> cria display/contexto EGL (so na primeira vez) e a
//                           superficie para a ANativeWindow nova
//   APP_CMD_TERM_WINDOW  -> destroi so a superficie; o contexto (e com ele as
//                           texturas/shaders do jogo) continua vivo
//   APP_CMD_RESUME/PAUSE -> liga/desliga o loop de render
//   APP_CMD_GAINED/LOST_FOCUS -> pausa o som (se mute_on_focus_lost)
//   APP_CMD_WINDOW_RESIZED/CONFIG_CHANGED -> atualiza x_size/y_size
//   APP_CMD_DESTROY      -> app->destroyRequested, o loop sai
//
// O loop so desenha com superficie + app em primeiro plano; fora isso ele
// bloqueia no ALooper sem gastar CPU/bateria.
//
// Toque -> eventos de mouse do jogo (ver a secao "entrada" abaixo):
//   toque rapido                -> clique esquerdo
//   segurar parado (~0,5 s)     -> clique direito ao soltar (ordens de movimento etc.)
//   segurar e depois arrastar   -> caixa de selecao no mapa (arrasto esquerdo)
//   arrastar sobre a interface  -> arrasto com o botao esquerdo (barras, janelas)
//   arrastar sobre o mapa       -> arrasto com o botao do meio (move o mapa)
//   dois dedos                  -> pinca = zoom (roda do mouse), mover = arrasta o mapa
//   tres dedos                  -> mostra/esconde o contador de desempenho
//   botao/gesto de voltar       -> ESC
// Mouse e teclado fisicos (USB/Bluetooth) tambem funcionam.

#include "window.hpp"
#include "system_state.hpp"

#include <android/configuration.h>
#include <android/input.h>
#include <android/log.h>
#include <android/native_window.h>
#include <android_native_app_glue.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl32.h>
#include <dirent.h>
#include <sched.h>
#include <sys/resource.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <mutex>
#include <optional>

#define ALICE_LOGI(...) __android_log_print(ANDROID_LOG_INFO, "Alice", __VA_ARGS__)
#define ALICE_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "Alice", __VA_ARGS__)

namespace window {

int32_t cursor_blink_ms() {
	return 1000; // igual ao valor fixo usado em window_nix.cpp
}
int32_t double_click_ms() {
	return 500; // igual ao valor fixo usado em window_nix.cpp
}

bool is_key_depressed(sys::state const& game_state, sys::virtual_key key) {
	// TODO (entrada): teclado fisico/bluetooth via AInputEvent. Em touchscreen
	// nao ha teclas "seguradas", entao false e o comportamento correto por ora.
	return false;
}

void get_window_size(sys::state const& game_state, int& width, int& height) {
	if(game_state.win_ptr && game_state.win_ptr->native_window && game_state.win_ptr->render_width > 0) {
		width = game_state.win_ptr->render_width;
		height = game_state.win_ptr->render_height;
	} else {
		// sem janela (app em segundo plano): ultimo tamanho conhecido
		width = game_state.x_size;
		height = game_state.y_size;
	}
}

bool is_in_fullscreen(sys::state const& game_state) {
	return true; // no Android o jogo sempre ocupa a tela toda
}

void set_borderless_full_screen(sys::state& game_state, bool fullscreen) {
	// no-op: no Android nao existe "janela" pra alternar -- sempre fullscreen
}

void close_window(sys::state& game_state) {
	// equivalente ao glfwSetWindowShouldClose: pede pro sistema fechar a
	// Activity, que chega de volta como APP_CMD_DESTROY
	if(game_state.win_ptr && game_state.win_ptr->app)
		ANativeActivity_finish(game_state.win_ptr->app->activity);
}

void create_window(sys::state& game_state, creation_parameters const& params) {
	// No Android quem cria a janela e o sistema (APP_CMD_INIT_WINDOW), e o
	// loop principal e o run_android_main_loop chamado pelo android_main.
	emit_error_message("create_window nao e usado no Android -- ver run_android_main_loop", false);
}

namespace {

// pixels desenhados / pixels da tela: converte as coordenadas do toque e do
// mouse (sempre em pixels da tela) para as do jogo
float g_input_scale = 1.f;

// 1 = um quadro por atualizacao da tela (60 FPS); 2 = um a cada duas (30 FPS)
int32_t g_swap_interval = 1;

// Ajustes do menu "Ajustes do Android" (AliceActivity). Chegam pela thread da
// interface do Java e sao aplicados pela thread de desenho, no loop.
std::atomic<int32_t> pending_render_scale{ 0 }; // porcentagem; 0 = nada novo
std::atomic<int32_t> pending_fps_limit{ 0 };    // 0 = nada novo
std::atomic<int32_t> pending_perf_counter{ -1 }; // -1 = nada novo

// ---------------------------------------------------------------------------
// nucleos do processador
// ---------------------------------------------------------------------------
//
// Celulares misturam nucleos rapidos e lentos (no Snapdragon 675: 2 rapidos e
// 6 lentos), e sem orientacao o Android pode deixar a thread que le o toque e
// desenha num nucleo lento enquanto a simulacao ocupa os rapidos. Com o jogo
// na tela:
//  - a thread de desenho fica so nos nucleos rapidos;
//  - todas as outras threads do app (simulacao, workers do TBB, som...) ficam
//    fora de um dos nucleos rapidos, que sobra livre para o desenho.
// Sem saber a frequencia de todos os nucleos, ou com menos de 4, nada muda.
// O Android desfaz a afinidade quando o app troca de grupo (segundo plano e
// volta), e threads novas herdam a de quem as cria: por isso ela e aplicada de
// novo ao voltar ao primeiro plano e a cada poucos segundos.

struct cpu_layout {
	bool detected = false;
	bool pin_render = false;          // desenho preso aos nucleos rapidos
	bool restrict_simulation = false; // demais threads fora de um nucleo
	cpu_set_t render;
	cpu_set_t simulation;
	std::string description;          // para o contador de desempenho
	std::chrono::steady_clock::time_point last_apply;
	bool apply_now = false;
};
cpu_layout cpus;

int64_t cpu_max_freq_khz(int32_t cpu) {
	char path[96];
	snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq", int(cpu));
	FILE* f = fopen(path, "r");
	if(!f)
		return -1;
	long long value = -1;
	if(fscanf(f, "%lld", &value) != 1)
		value = -1;
	fclose(f);
	return value;
}

std::string describe_cpu_set(cpu_set_t const& set, int32_t count) {
	// "0-5,7"
	std::string out;
	for(int32_t i = 0; i < count; ++i) {
		if(!CPU_ISSET(i, &set))
			continue;
		int32_t j = i;
		while(j + 1 < count && CPU_ISSET(j + 1, &set))
			++j;
		if(!out.empty())
			out += ',';
		out += std::to_string(i);
		if(j > i)
			out += '-' + std::to_string(j);
		i = j;
	}
	return out;
}

void detect_cpu_layout() {
	cpus.detected = true;
	int32_t const count = std::min(int32_t(sysconf(_SC_NPROCESSORS_CONF)), int32_t(CPU_SETSIZE));
	std::vector<int64_t> freq(std::max(count, 0));
	for(int32_t i = 0; i < count; ++i) {
		freq[i] = cpu_max_freq_khz(i);
		if(freq[i] <= 0) {
			ALICE_LOGI("nucleos: frequencia do cpu%d desconhecida, afinidade nao muda", int(i));
			return;
		}
	}
	if(count < 4) {
		ALICE_LOGI("nucleos: so %d, afinidade nao muda", int(count));
		return;
	}

	int64_t const top = *std::max_element(freq.begin(), freq.end());
	int64_t second = 0;
	for(auto f : freq) {
		if(f < top)
			second = std::max(second, f);
	}
	int32_t top_count = 0;
	for(auto f : freq)
		top_count += f == top ? 1 : 0;
	// com um unico nucleo "primo" o desenho tambem pode usar o grupo seguinte
	int64_t const fast_min = (top_count == 1 && second > 0) ? second : top;

	CPU_ZERO(&cpus.render);
	CPU_ZERO(&cpus.simulation);
	int32_t reserved = -1;
	int32_t fast_count = 0;
	for(int32_t i = 0; i < count; ++i) {
		CPU_SET(i, &cpus.simulation);
		if(freq[i] >= fast_min) {
			CPU_SET(i, &cpus.render);
			++fast_count;
		}
		if(freq[i] == top)
			reserved = i; // o rapido de numero mais alto
	}
	CPU_CLR(reserved, &cpus.simulation);
	cpus.restrict_simulation = true;
	cpus.pin_render = fast_count < count; // todos iguais: so sobra um livre
	cpus.description = "desenho " + (cpus.pin_render ? describe_cpu_set(cpus.render, count) : std::string("todos"))
		+ ", simulacao " + describe_cpu_set(cpus.simulation, count);
	ALICE_LOGI("nucleos: %s (max %lld kHz, %d rapidos de %d)", cpus.description.c_str(), (long long)top, int(fast_count), int(count));
}

// Chamada na thread de desenho.
void apply_cpu_affinity() {
	cpus.last_apply = std::chrono::steady_clock::now();
	cpus.apply_now = false;
	pid_t const self = gettid();
	if(cpus.restrict_simulation) {
		if(DIR* dir = opendir("/proc/self/task")) {
			while(dirent* entry = readdir(dir)) {
				pid_t const tid = pid_t(atoi(entry->d_name));
				if(tid <= 0 || tid == self)
					continue;
				// ESRCH: a thread terminou; EINVAL: fora do grupo de nucleos do app
				sched_setaffinity(tid, sizeof(cpu_set_t), &cpus.simulation);
			}
			closedir(dir);
		}
	}
	if(cpus.pin_render && sched_setaffinity(0, sizeof(cpu_set_t), &cpus.render) != 0)
		ALICE_LOGE("nucleos: nao foi possivel prender o desenho (errno %d)", errno);
}

void update_cpu_affinity(window_data_impl const& win) {
	if(!cpus.restrict_simulation || !win.game_started)
		return;
	if(cpus.apply_now || std::chrono::steady_clock::now() - cpus.last_apply > std::chrono::seconds(5))
		apply_cpu_affinity();
}

// Define o tamanho dos buffers da janela pela render_scale. O hardware de
// composicao do Android amplia a imagem para a tela inteira.
void apply_render_scale(window_data_impl& win) {
	if(!win.native_window)
		return;
	// (0, 0) volta ao tamanho natural, para medir a tela de verdade
	ANativeWindow_setBuffersGeometry(win.native_window, 0, 0, WINDOW_FORMAT_RGBX_8888);
	win.physical_width = ANativeWindow_getWidth(win.native_window);
	win.physical_height = ANativeWindow_getHeight(win.native_window);
	if(win.physical_width <= 0 || win.physical_height <= 0)
		return;
	float const scale = std::clamp(win.render_scale, 0.25f, 1.f);
	win.render_width = std::max(1, int32_t(std::lround(float(win.physical_width) * scale)));
	win.render_height = std::max(1, int32_t(std::lround(float(win.physical_height) * scale)));
	if(win.render_width != win.physical_width || win.render_height != win.physical_height)
		ANativeWindow_setBuffersGeometry(win.native_window, win.render_width, win.render_height, WINDOW_FORMAT_RGBX_8888);
	g_input_scale = float(win.render_width) / float(win.physical_width);
}

bool init_egl_display(window_data_impl& win) {
	win.egl_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	if(win.egl_display == EGL_NO_DISPLAY || !eglInitialize(win.egl_display, nullptr, nullptr)) {
		ALICE_LOGE("eglInitialize falhou (0x%x)", eglGetError());
		return false;
	}

	// Superficie OPACA (sem canal alfa). Com alfa, os pixels semitransparentes
	// que o jogo deixa no framebuffer (bordas e texturas com transparencia dos
	// paineis) viram pixels translucidos da janela do app, e o compositor do
	// Android os mistura com o que esta atras -- os paineis piscavam na Adreno.
	// No desktop o alfa da janela e ignorado, entao la isso nunca apareceu.
	EGLint const config_attribs[] = {
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_RED_SIZE, 8,
		EGL_GREEN_SIZE, 8,
		EGL_BLUE_SIZE, 8,
		EGL_ALPHA_SIZE, 0,
		EGL_DEPTH_SIZE, 24,
		EGL_STENCIL_SIZE, 8,
		EGL_NONE
	};
	// eglChooseConfig trata os tamanhos como minimos e ordena primeiro os com
	// mais bits de cor (RGBA8 antes de RGB8), entao procura um sem alfa
	EGLConfig configs[64];
	EGLint num_configs = 0;
	if(!eglChooseConfig(win.egl_display, config_attribs, configs, 64, &num_configs) || num_configs == 0) {
		ALICE_LOGE("nenhuma configuracao EGL com GLES 3 + RGB8 + depth24/stencil8 (0x%x)", eglGetError());
		return false;
	}
	win.egl_config = configs[0];
	for(EGLint i = 0; i < num_configs; ++i) {
		EGLint r = 0, g = 0, b = 0, a = 0;
		eglGetConfigAttrib(win.egl_display, configs[i], EGL_RED_SIZE, &r);
		eglGetConfigAttrib(win.egl_display, configs[i], EGL_GREEN_SIZE, &g);
		eglGetConfigAttrib(win.egl_display, configs[i], EGL_BLUE_SIZE, &b);
		eglGetConfigAttrib(win.egl_display, configs[i], EGL_ALPHA_SIZE, &a);
		if(r == 8 && g == 8 && b == 8 && a == 0) {
			win.egl_config = configs[i];
			break;
		}
	}
	EGLint chosen_alpha = 0;
	eglGetConfigAttrib(win.egl_display, win.egl_config, EGL_ALPHA_SIZE, &chosen_alpha);
	ALICE_LOGI("configuracao EGL: %s", chosen_alpha == 0 ? "RGB8 opaca" : "RGBA8 (sem opcao opaca; buffer marcado como RGBX)");
	return true;
}

bool create_egl_context(window_data_impl& win) {
	// o jogo precisa de GLES 3.2 (ver uses-feature no AndroidManifest.xml)
	EGLint context_attribs[] = {
		EGL_CONTEXT_MAJOR_VERSION, 3,
		EGL_CONTEXT_MINOR_VERSION, 2,
#ifndef NDEBUG
		EGL_CONTEXT_OPENGL_DEBUG, EGL_TRUE,
#endif
		EGL_NONE
	};
	win.egl_context = eglCreateContext(win.egl_display, win.egl_config, EGL_NO_CONTEXT, context_attribs);
#ifndef NDEBUG
	if(win.egl_context == EGL_NO_CONTEXT) {
		// alguns drivers recusam contexto de debug -- tenta sem
		context_attribs[4] = EGL_NONE;
		win.egl_context = eglCreateContext(win.egl_display, win.egl_config, EGL_NO_CONTEXT, context_attribs);
	}
#endif
	if(win.egl_context == EGL_NO_CONTEXT) {
		ALICE_LOGE("eglCreateContext (GLES 3.2) falhou (0x%x)", eglGetError());
		return false;
	}
	return true;
}

bool create_egl_surface(window_data_impl& win, ANativeWindow* native_window) {
	win.native_window = native_window;
	apply_render_scale(win);
	win.egl_surface = eglCreateWindowSurface(win.egl_display, win.egl_config, native_window, nullptr);
	if(win.egl_surface == EGL_NO_SURFACE) {
		ALICE_LOGE("eglCreateWindowSurface falhou (0x%x)", eglGetError());
		return false;
	}
	if(!eglMakeCurrent(win.egl_display, win.egl_surface, win.egl_surface, win.egl_context)) {
		ALICE_LOGE("eglMakeCurrent falhou (0x%x)", eglGetError());
		return false;
	}
	eglSwapInterval(win.egl_display, g_swap_interval); // vsync (1, como o glfwSwapInterval(1) do desktop) ou 30 FPS
	return true;
}

void destroy_egl_surface(window_data_impl& win) {
	if(win.egl_display != EGL_NO_DISPLAY) {
		eglMakeCurrent(win.egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
		if(win.egl_surface != EGL_NO_SURFACE)
			eglDestroySurface(win.egl_display, win.egl_surface);
	}
	win.egl_surface = EGL_NO_SURFACE;
	win.native_window = nullptr;
}

void terminate_egl(window_data_impl& win) {
	destroy_egl_surface(win);
	if(win.egl_display != EGL_NO_DISPLAY) {
		if(win.egl_context != EGL_NO_CONTEXT)
			eglDestroyContext(win.egl_display, win.egl_context);
		eglTerminate(win.egl_display);
	}
	win.egl_context = EGL_NO_CONTEXT;
	win.egl_display = EGL_NO_DISPLAY;
}

void update_window_size(sys::state& state) {
	auto& win = *state.win_ptr;
	if(!win.native_window)
		return;
	apply_render_scale(win); // rotacao etc.: mede a tela de novo
	int32_t const width = win.render_width;
	int32_t const height = win.render_height;
	if(width <= 0 || height <= 0 || (width == state.x_size && height == state.y_size))
		return;
	ALICE_LOGI("tela %dx%d, desenhando em %dx%d (escala %.2f)", win.physical_width, win.physical_height, width, height, win.render_scale);
	if(win.game_started)
		state.on_resize(width, height, window_state::maximized);
	state.x_size = width;
	state.y_size = height;
}

void on_window_created(sys::state& state, ANativeWindow* native_window) {
	auto& win = *state.win_ptr;
	if(win.egl_display == EGL_NO_DISPLAY) {
		if(!init_egl_display(win) || !create_egl_context(win)) {
			emit_error_message("Nao foi possivel iniciar o OpenGL ES 3.2 (EGL). O aparelho e compativel?", true);
		}
	}
	if(!create_egl_surface(win, native_window)) {
		emit_error_message("Nao foi possivel criar a superficie de desenho (EGL).", true);
	}

	static bool logged_gl_info = false;
	if(!logged_gl_info) {
		logged_gl_info = true;
		ALICE_LOGI("GL_VERSION: %s", reinterpret_cast<char const*>(glGetString(GL_VERSION)));
		ALICE_LOGI("GL_RENDERER: %s", reinterpret_cast<char const*>(glGetString(GL_RENDERER)));
		ALICE_LOGI("GL_VENDOR: %s", reinterpret_cast<char const*>(glGetString(GL_VENDOR)));
	}

	update_window_size(state);
	// o jogo em si e iniciado pelo android_launcher_update quando o cenario
	// estiver carregado (ver android_start_game)
}

// Ao ir para segundo plano o Android pode matar o processo a qualquer momento
// para liberar memoria, sem aviso. Entao, numa partida single-player:
//  - pausa a simulacao como o botao de pausa faz (guarda a velocidade em
//    held_game_speed), para o tempo do jogo nao correr com o app fora da tela;
//  - pede um save pelo mesmo comando do menu "Salvar", que a thread do jogo
//    executa (nada de gravar o estado daqui, com a simulacao rodando).
// O arquivo e sempre o mesmo (sobrescrito), e no maximo um a cada 30 s.
constexpr char const* background_save_name = "android_autosave.bin";
constexpr auto background_save_interval = std::chrono::seconds(30);

void pause_and_save_on_background(sys::state& state) {
	auto& win = *state.win_ptr;
	if(!win.game_started || !state.current_scene.game_in_progress)
		return;
	if(state.network_mode != sys::network_mode_type::single_player)
		return; // no multiplayer o host decide pausa e saves
	if(!state.local_player_nation)
		return; // ex.: ainda escolhendo o pais

	if(auto const speed = state.actual_game_speed.load(); speed > 0) {
		state.ui_state.held_game_speed = speed;
		state.actual_game_speed = 0;
	}

	auto const now = std::chrono::steady_clock::now();
	if(now - win.last_background_save < background_save_interval)
		return;
	win.last_background_save = now;
	ALICE_LOGI("indo para segundo plano: pausando e salvando em %s", background_save_name);
	command::save_game(state, state.local_player_nation, false, background_save_name);
}

void handle_cmd(android_app* app, int32_t cmd) {
	auto& state = *static_cast<sys::state*>(app->userData);
	auto& win = *state.win_ptr;

	switch(cmd) {
	case APP_CMD_INIT_WINDOW:
		ALICE_LOGI("APP_CMD_INIT_WINDOW");
		if(app->window)
			on_window_created(state, app->window);
		break;
	case APP_CMD_TERM_WINDOW:
		ALICE_LOGI("APP_CMD_TERM_WINDOW");
		destroy_egl_surface(win);
		break;
	case APP_CMD_WINDOW_RESIZED:
	case APP_CMD_CONFIG_CHANGED:
	case APP_CMD_CONTENT_RECT_CHANGED:
		update_window_size(state);
		break;
	case APP_CMD_GAINED_FOCUS:
		win.has_focus = true;
		cpus.apply_now = true;
		if(win.game_started && state.user_settings.mute_on_focus_lost)
			sound::resume_all(state);
		break;
	case APP_CMD_LOST_FOCUS:
		win.has_focus = false;
		if(win.game_started && state.user_settings.mute_on_focus_lost)
			sound::pause_all(state);
		break;
	case APP_CMD_RESUME:
		ALICE_LOGI("APP_CMD_RESUME");
		win.resumed = true;
		cpus.apply_now = true;
		break;
	case APP_CMD_PAUSE:
		ALICE_LOGI("APP_CMD_PAUSE");
		win.resumed = false;
		pause_and_save_on_background(state);
		break;
	case APP_CMD_SAVE_STATE:
		// TODO: autosave -- o Android pode matar o app em segundo plano
		break;
	case APP_CMD_DESTROY:
		ALICE_LOGI("APP_CMD_DESTROY");
		break;
	default:
		break;
	}
}

// ---------------------------------------------------------------------------
// entrada: toque, mouse e teclado
// ---------------------------------------------------------------------------

constexpr auto long_press_time = std::chrono::milliseconds(500);
constexpr float touch_slop_dp = 10.f;	// quanto o dedo pode mexer e ainda ser toque/segurar
constexpr float wheel_clicks_per_doubling = 3.f; // pinca: dobrar a distancia = 3 "cliques" da roda

struct touch_tracker {
	enum class mode : uint8_t {
		none,
		pending,         // dedo encostou; ainda nao se sabe se e toque, segurar ou arrasto
		left_drag,       // arrasto na interface (botao esquerdo)
		map_pan,         // arrasto no mapa (botao do meio)
		held,            // segurou parado: clique direito ao soltar, ou caixa se arrastar
		box_select,      // segurou e arrastou no mapa: caixa de selecao (botao esquerdo)
		pinch,           // dois dedos
		ignore_until_up  // gesto ja consumido; espera todos os dedos sairem
	};
	mode current = mode::none;
	int32_t primary_id = -1;
	float start_x = 0.f, start_y = 0.f;
	float last_x = 0.f, last_y = 0.f;
	float pinch_last_distance = 0.f;
	std::chrono::steady_clock::time_point down_time;

	// so para o feedback visual enquanto o jogo nao esta carregado
	enum class flash : uint8_t { none, tap, long_press };
	flash last_flash = flash::none;
	std::chrono::steady_clock::time_point flash_time;
	float placeholder_zoom = 0.f;
};
touch_tracker touch;

// posicao do ponteiro em pixels do jogo (ver g_input_scale)
float event_x(AInputEvent const* event, size_t pointer) {
	return AMotionEvent_getX(event, pointer) * g_input_scale;
}
float event_y(AInputEvent const* event, size_t pointer) {
	return AMotionEvent_getY(event, pointer) * g_input_scale;
}

// em pixels do jogo
float touch_slop_px(android_app* app) {
	int32_t const density = app->config ? AConfiguration_getDensity(app->config) : ACONFIGURATION_DENSITY_MEDIUM;
	return touch_slop_dp * float(density > 0 ? density : ACONFIGURATION_DENSITY_MEDIUM) / float(ACONFIGURATION_DENSITY_MEDIUM) * g_input_scale;
}

sys::key_modifiers modifiers_from_meta(int32_t meta) {
	uint32_t val = 0;
	if(meta & AMETA_CTRL_ON)
		val |= uint32_t(sys::key_modifiers::modifiers_ctrl);
	if(meta & AMETA_ALT_ON)
		val |= uint32_t(sys::key_modifiers::modifiers_alt);
	if(meta & AMETA_SHIFT_ON)
		val |= uint32_t(sys::key_modifiers::modifiers_shift);
	return sys::key_modifiers(val);
}

// Os eventos so chegam ao jogo depois do on_create; no desktop eles rodam
// dentro do mesmo lock do render (ver o loop de window_nix.cpp).
template<typename F>
void dispatch_to_game(sys::state& state, F&& f) {
	if(!state.win_ptr->game_started)
		return;
	std::shared_lock lock(state.game_state_resetting_lock);
	state.game_state_resetting_cv.wait(lock, [&] { return !state.yield_game_state_resetting_lock; });
	f();
}

void game_mouse_move(sys::state& state, int32_t x, int32_t y, sys::key_modifiers mod) {
	dispatch_to_game(state, [&] {
		state.on_mouse_move(x, y, mod);
		if(state.win_ptr->left_mouse_down)
			state.on_mouse_drag(x, y, mod);
	});
	state.mouse_x_position = x;
	state.mouse_y_position = y;
}
void game_lbutton(sys::state& state, int32_t x, int32_t y, bool down, sys::key_modifiers mod) {
	dispatch_to_game(state, [&] {
		if(down)
			state.on_lbutton_down(x, y, mod);
		else
			state.on_lbutton_up(x, y, mod);
	});
	state.win_ptr->left_mouse_down = down;
	if(!down)
		state.ui_state.selecting_edit_text = ui::edit_selection_mode::none;
	state.mouse_x_position = x;
	state.mouse_y_position = y;
}
void game_rbutton(sys::state& state, int32_t x, int32_t y, bool down, sys::key_modifiers mod) {
	dispatch_to_game(state, [&] {
		if(down)
			state.on_rbutton_down(x, y, mod);
		else
			state.on_rbutton_up(x, y, mod);
	});
	state.mouse_x_position = x;
	state.mouse_y_position = y;
}
void game_mbutton(sys::state& state, int32_t x, int32_t y, bool down, sys::key_modifiers mod) {
	dispatch_to_game(state, [&] {
		if(down)
			state.on_mbutton_down(x, y, mod);
		else
			state.on_mbutton_up(x, y, mod);
	});
	state.mouse_x_position = x;
	state.mouse_y_position = y;
}
void game_wheel(sys::state& state, int32_t x, int32_t y, float amount, sys::key_modifiers mod) {
	dispatch_to_game(state, [&] { sys::on_mouse_wheel(state, x, y, mod, amount); });
	if(!state.win_ptr->game_started)
		touch.placeholder_zoom = std::clamp(touch.placeholder_zoom + amount, -10.f, 10.f);
}
void game_key(sys::state& state, sys::virtual_key key, bool down, sys::key_modifiers mod) {
	dispatch_to_game(state, [&] {
		if(down)
			state.on_key_down(key, mod);
		else
			state.on_key_up(key, mod);
	});
}

// O dedo esta sobre a interface (e nao sobre o mapa)? O jogo calcula
// ui_state.under_mouse a cada frame a partir de mouse_x/y_position, que ja foi
// atualizado no ACTION_DOWN -- entao no primeiro movimento ele ja vale.
bool touch_is_over_ui(sys::state const& state) {
	if(!state.win_ptr->game_started)
		return false;
	return state.ui_state.under_mouse != nullptr || state.iui_state.over_ui;
}

int32_t find_pointer_index(AInputEvent const* event, int32_t id) {
	size_t const count = AMotionEvent_getPointerCount(event);
	for(size_t i = 0; i < count; ++i) {
		if(AMotionEvent_getPointerId(event, i) == id)
			return int32_t(i);
	}
	return -1;
}

void pinch_geometry(AInputEvent const* event, float& mid_x, float& mid_y, float& distance) {
	float const x0 = event_x(event, 0), y0 = event_y(event, 0);
	float const x1 = event_x(event, 1), y1 = event_y(event, 1);
	mid_x = (x0 + x1) * 0.5f;
	mid_y = (y0 + y1) * 0.5f;
	distance = std::max(std::hypot(x1 - x0, y1 - y0), 1.f);
}

// encerra o gesto de um dedo em andamento, sem gerar clique
void end_single_finger_gesture(sys::state& state) {
	auto const x = int32_t(touch.last_x), y = int32_t(touch.last_y);
	auto const mod = sys::key_modifiers::modifiers_none;
	if(touch.current == touch_tracker::mode::left_drag || touch.current == touch_tracker::mode::box_select)
		game_lbutton(state, x, y, false, mod);
	else if(touch.current == touch_tracker::mode::map_pan || touch.current == touch_tracker::mode::pinch)
		game_mbutton(state, x, y, false, mod);
	touch.current = touch_tracker::mode::none;
}

void start_flash(touch_tracker::flash f) {
	touch.last_flash = f;
	touch.flash_time = std::chrono::steady_clock::now();
}

// Contador de desempenho: quadros por segundo, tempo medio e pior quadro, e
// quantos dias do jogo passaram no ultimo segundo. O contador de FPS do proprio
// jogo (comando "fps" do console) depende de um elemento de interface que nao
// existe nos arquivos, entao este e mostrado pela AliceActivity.
struct perf_counter {
	bool visible = false;
	std::chrono::steady_clock::time_point window_start;
	std::chrono::steady_clock::time_point last_frame;
	int32_t frames = 0;
	float worst_ms = 0.f;
	int32_t start_date = 0;
};
perf_counter perf;

void set_perf_counter(bool visible) {
	if(perf.visible == visible)
		return;
	perf.visible = visible;
	perf.frames = 0;
	perf.worst_ms = 0.f;
	perf.window_start = perf.last_frame = std::chrono::steady_clock::now();
	perf.start_date = -1;
	android_set_stats(perf.visible ? "medindo..." : "");
}

void update_perf_counter(sys::state& state) {
	if(!perf.visible)
		return;
	auto const now = std::chrono::steady_clock::now();
	float const frame_ms = std::chrono::duration<float, std::milli>(now - perf.last_frame).count();
	perf.last_frame = now;
	int32_t const date = state.current_date.to_raw_value();
	if(perf.start_date < 0 || frame_ms > 1000.f) {
		// primeira medida, ou o app ficou parado em segundo plano: recomeca
		perf.window_start = now;
		perf.frames = 0;
		perf.worst_ms = 0.f;
		perf.start_date = date;
		return;
	}
	perf.worst_ms = std::max(perf.worst_ms, frame_ms);
	++perf.frames;

	float const elapsed_ms = std::chrono::duration<float, std::milli>(now - perf.window_start).count();
	if(elapsed_ms < 1000.f || perf.frames == 0)
		return;
	int32_t const speed = state.actual_game_speed.load(std::memory_order::relaxed);
	char speed_text[24] = "pausado";
	if(speed > 0)
		snprintf(speed_text, sizeof(speed_text), "velocidade %d", int(speed));
	char text[256];
	snprintf(text, sizeof(text), "FPS %.0f  quadro %.0f ms (pior %.0f)\n%s  dias/s %.1f\nnucleos: %s",
		float(perf.frames) * 1000.f / elapsed_ms, elapsed_ms / float(perf.frames), perf.worst_ms,
		speed_text, float(date - perf.start_date) * 1000.f / elapsed_ms,
		cpus.restrict_simulation ? cpus.description.c_str() : "padrao do Android");
	android_set_stats(text);
	perf.window_start = now;
	perf.frames = 0;
	perf.worst_ms = 0.f;
	perf.start_date = date;
}

// Aplica o que mudou no menu de ajustes. Na thread de desenho (contexto EGL).
void apply_pending_settings(sys::state& state) {
	auto& win = *state.win_ptr;
	if(int32_t const percent = pending_render_scale.exchange(0); percent > 0) {
		win.render_scale = float(std::clamp(percent, 25, 100)) / 100.f;
		update_window_size(state); // mede a tela e chama o on_resize do jogo
	}
	if(int32_t const fps = pending_fps_limit.exchange(0); fps > 0) {
		g_swap_interval = fps <= 30 ? 2 : 1;
		if(win.egl_surface != EGL_NO_SURFACE)
			eglSwapInterval(win.egl_display, g_swap_interval);
		ALICE_LOGI("limite de quadros: %d FPS (swap interval %d)", int(fps), int(g_swap_interval));
	}
	if(int32_t const perf_on = pending_perf_counter.exchange(-1); perf_on >= 0)
		set_perf_counter(perf_on != 0);
}

// ---------------------------------------------------------------------------
// digitar texto
// ---------------------------------------------------------------------------
//
// A NativeActivity nao entrega os caracteres do teclado virtual. Quando um
// campo de texto do jogo ganha foco (ou recebe um toque com o foco ja nele), a
// AliceActivity abre uma janela com o texto atual e o teclado; o texto
// confirmado substitui o do campo, pelo mesmo on_text do teclado no PC.

std::mutex text_result_mutex;
std::optional<std::string> text_result; // texto confirmado, ainda nao aplicado
bool text_result_cancelled = false;
bool text_dialog_open = false;
ui::element_base* text_target = nullptr;     // campo para onde vai o texto
ui::element_base* last_edit_target = nullptr; // para notar o foco mudando

std::string utf16_to_utf8(std::u16string_view in) {
	std::string out;
	for(size_t i = 0; i < in.size(); ++i) {
		uint32_t c = in[i];
		if(c >= 0xD800 && c < 0xDC00 && i + 1 < in.size() && in[i + 1] >= 0xDC00 && in[i + 1] < 0xE000) {
			c = 0x10000 + ((c - 0xD800) << 10) + (uint32_t(in[i + 1]) - 0xDC00);
			++i;
		}
		if(c < 0x80) {
			out += char(c);
		} else if(c < 0x800) {
			out += char(0xC0 | (c >> 6));
			out += char(0x80 | (c & 0x3F));
		} else if(c < 0x10000) {
			out += char(0xE0 | (c >> 12));
			out += char(0x80 | ((c >> 6) & 0x3F));
			out += char(0x80 | (c & 0x3F));
		} else {
			out += char(0xF0 | (c >> 18));
			out += char(0x80 | ((c >> 12) & 0x3F));
			out += char(0x80 | ((c >> 6) & 0x3F));
			out += char(0x80 | (c & 0x3F));
		}
	}
	return out;
}

std::u32string utf8_to_utf32(std::string_view in) {
	std::u32string out;
	for(size_t i = 0; i < in.size();) {
		uint8_t const b = uint8_t(in[i]);
		int32_t extra = b < 0x80 ? 0 : (b >> 5) == 0x6 ? 1 : (b >> 4) == 0xE ? 2 : (b >> 3) == 0x1E ? 3 : -1;
		if(extra < 0 || i + size_t(extra) >= in.size() + (extra == 0 ? 1 : 0)) {
			++i; // byte invalido: pula
			continue;
		}
		uint32_t c = extra == 0 ? b : extra == 1 ? (b & 0x1F) : extra == 2 ? (b & 0x0F) : (b & 0x07);
		for(int32_t k = 1; k <= extra; ++k)
			c = (c << 6) | (uint8_t(in[i + k]) & 0x3F);
		out += char32_t(c);
		i += size_t(extra) + 1;
	}
	return out;
}

void request_text_input(sys::state& state) {
	auto* target = state.ui_state.edit_target_internal;
	if(!target || text_dialog_open)
		return;
	text_dialog_open = true;
	text_target = target;
	android_request_text(utf16_to_utf8(target->text_content()).c_str());
}

// No loop da thread de desenho: abre a janela quando o foco chega a um campo
// e aplica o texto confirmado.
void update_text_input(sys::state& state) {
	if(!state.win_ptr->game_started)
		return;
	auto* const target = state.ui_state.edit_target_internal;
	if(target != last_edit_target) {
		last_edit_target = target;
		if(target)
			request_text_input(state);
	}

	std::optional<std::string> entered;
	{
		std::lock_guard lock(text_result_mutex);
		if(!text_result && !text_result_cancelled)
			return;
		entered.swap(text_result);
		text_result_cancelled = false;
	}
	text_dialog_open = false;
	// so se o campo ainda e o mesmo e continua com o foco
	if(!entered || text_target != state.ui_state.edit_target_internal || !text_target)
		return;
	auto const text = utf8_to_utf32(*entered);
	dispatch_to_game(state, [&] {
		state.pass_edit_command(ui::edit_command::select_all, sys::key_modifiers::modifiers_none);
		if(text.empty())
			state.pass_edit_command(ui::edit_command::delete_selection, sys::key_modifiers::modifiers_none);
		for(char32_t c : text)
			state.on_text(c);
	});
}

int32_t handle_touch(android_app* app, sys::state& state, AInputEvent* event) {
	int32_t const action = AMotionEvent_getAction(event);
	int32_t const masked = action & AMOTION_EVENT_ACTION_MASK;
	auto const mod = sys::key_modifiers::modifiers_none;

	switch(masked) {
	case AMOTION_EVENT_ACTION_DOWN: {
		touch.current = touch_tracker::mode::pending;
		touch.primary_id = AMotionEvent_getPointerId(event, 0);
		touch.start_x = touch.last_x = event_x(event, 0);
		touch.start_y = touch.last_y = event_y(event, 0);
		touch.down_time = std::chrono::steady_clock::now();
		// sem botao apertado: so posiciona o "cursor" para o jogo achar o que esta sob o dedo
		game_mouse_move(state, int32_t(touch.last_x), int32_t(touch.last_y), mod);
		break;
	}
	case AMOTION_EVENT_ACTION_POINTER_DOWN: {
		if(AMotionEvent_getPointerCount(event) == 3 && touch.current != touch_tracker::mode::ignore_until_up) {
			// terceiro dedo: desfaz a pinca que comecou com os dois primeiros
			end_single_finger_gesture(state);
			android_open_settings();
			touch.current = touch_tracker::mode::ignore_until_up;
		} else if(AMotionEvent_getPointerCount(event) == 2 && touch.current != touch_tracker::mode::ignore_until_up) {
			end_single_finger_gesture(state);
			float mid_x, mid_y;
			pinch_geometry(event, mid_x, mid_y, touch.pinch_last_distance);
			touch.last_x = mid_x;
			touch.last_y = mid_y;
			game_mouse_move(state, int32_t(mid_x), int32_t(mid_y), mod);
			game_mbutton(state, int32_t(mid_x), int32_t(mid_y), true, mod); // arrastar o mapa pelo ponto medio
			touch.current = touch_tracker::mode::pinch;
		}
		break;
	}
	case AMOTION_EVENT_ACTION_MOVE: {
		if(touch.current == touch_tracker::mode::pinch) {
			if(AMotionEvent_getPointerCount(event) < 2)
				break;
			float mid_x, mid_y, distance;
			pinch_geometry(event, mid_x, mid_y, distance);
			touch.last_x = mid_x;
			touch.last_y = mid_y;
			game_mouse_move(state, int32_t(mid_x), int32_t(mid_y), mod);
			float const clicks = std::log2(distance / touch.pinch_last_distance) * wheel_clicks_per_doubling;
			if(std::abs(clicks) > 0.01f) {
				game_wheel(state, int32_t(mid_x), int32_t(mid_y), clicks, mod);
				touch.pinch_last_distance = distance;
			}
			break;
		}

		int32_t const index = find_pointer_index(event, touch.primary_id);
		if(index < 0)
			break;
		float const x = event_x(event, index);
		float const y = event_y(event, index);

		if(touch.current == touch_tracker::mode::held) {
			if(std::hypot(x - touch.start_x, y - touch.start_y) < touch_slop_px(app) || touch_is_over_ui(state))
				break; // na interface continua valendo o clique direito ao soltar
			// segurou e arrastou no mapa: caixa de selecao a partir de onde encostou
			game_lbutton(state, int32_t(touch.start_x), int32_t(touch.start_y), true, mod);
			touch.current = touch_tracker::mode::box_select;
		}
		if(touch.current == touch_tracker::mode::pending) {
			if(std::hypot(x - touch.start_x, y - touch.start_y) < touch_slop_px(app))
				break;
			// virou arrasto: comeca no ponto onde o dedo encostou
			auto const sx = int32_t(touch.start_x), sy = int32_t(touch.start_y);
			if(touch_is_over_ui(state)) {
				game_lbutton(state, sx, sy, true, mod);
				touch.current = touch_tracker::mode::left_drag;
			} else {
				game_mbutton(state, sx, sy, true, mod);
				touch.current = touch_tracker::mode::map_pan;
			}
		}
		if(touch.current == touch_tracker::mode::left_drag || touch.current == touch_tracker::mode::map_pan
			|| touch.current == touch_tracker::mode::box_select) {
			touch.last_x = x;
			touch.last_y = y;
			game_mouse_move(state, int32_t(x), int32_t(y), mod);
		}
		break;
	}
	case AMOTION_EVENT_ACTION_POINTER_UP: {
		if(touch.current == touch_tracker::mode::pinch) {
			// saiu um dos dedos: termina a pinca e ignora o dedo restante
			// (senao ele viraria um toque/arrasto acidental)
			end_single_finger_gesture(state);
			touch.current = touch_tracker::mode::ignore_until_up;
		}
		break;
	}
	case AMOTION_EVENT_ACTION_UP: {
		if(touch.current == touch_tracker::mode::pending) {
			// toque rapido = clique esquerdo onde o dedo encostou
			auto const x = int32_t(touch.start_x), y = int32_t(touch.start_y);
			auto* const focused_before = state.ui_state.edit_target_internal;
			game_lbutton(state, x, y, true, mod);
			game_lbutton(state, x, y, false, mod);
			start_flash(touch_tracker::flash::tap);
			// tocar de novo no campo que ja tinha o foco abre o teclado de novo
			if(focused_before && focused_before == state.ui_state.edit_target_internal
				&& state.ui_state.under_mouse == focused_before)
				request_text_input(state);
			touch.current = touch_tracker::mode::none;
		} else if(touch.current == touch_tracker::mode::held) {
			// segurou e soltou sem arrastar no mapa = clique direito onde encostou
			auto const x = int32_t(touch.start_x), y = int32_t(touch.start_y);
			game_rbutton(state, x, y, true, mod);
			game_rbutton(state, x, y, false, mod);
			touch.current = touch_tracker::mode::none;
		} else {
			end_single_finger_gesture(state);
		}
		break;
	}
	case AMOTION_EVENT_ACTION_CANCEL: {
		end_single_finger_gesture(state);
		break;
	}
	default:
		break;
	}
	return 1;
}

// Segurar parado nao gera evento, entao o toque longo e verificado a cada
// frame. Ele so marca o gesto: soltar da o clique direito, arrastar no mapa
// desenha a caixa de selecao.
void update_touch(sys::state& state) {
	if(touch.current != touch_tracker::mode::pending)
		return;
	if(std::chrono::steady_clock::now() - touch.down_time < long_press_time)
		return;
	start_flash(touch_tracker::flash::long_press);
	touch.current = touch_tracker::mode::held;
	android_haptic(); // avisa que o toque longo valeu
}

// mouse USB/Bluetooth: repassa direto como no desktop
int32_t handle_mouse(sys::state& state, AInputEvent* event) {
	int32_t const masked = AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK;
	auto const x = int32_t(event_x(event, 0));
	auto const y = int32_t(event_y(event, 0));
	auto const mod = modifiers_from_meta(AMotionEvent_getMetaState(event));

	switch(masked) {
	case AMOTION_EVENT_ACTION_HOVER_MOVE:
	case AMOTION_EVENT_ACTION_MOVE:
		game_mouse_move(state, x, y, mod);
		return 1;
	case AMOTION_EVENT_ACTION_BUTTON_PRESS:
	case AMOTION_EVENT_ACTION_BUTTON_RELEASE: {
		// AMotionEvent_getActionButton so existe na API 33+; compara com o estado anterior
		static int32_t previous_buttons = 0;
		int32_t const buttons = AMotionEvent_getButtonState(event);
		int32_t const changed = buttons ^ previous_buttons;
		previous_buttons = buttons;
		if(changed & AMOTION_EVENT_BUTTON_PRIMARY)
			game_lbutton(state, x, y, (buttons & AMOTION_EVENT_BUTTON_PRIMARY) != 0, mod);
		if(changed & AMOTION_EVENT_BUTTON_SECONDARY)
			game_rbutton(state, x, y, (buttons & AMOTION_EVENT_BUTTON_SECONDARY) != 0, mod);
		if(changed & AMOTION_EVENT_BUTTON_TERTIARY)
			game_mbutton(state, x, y, (buttons & AMOTION_EVENT_BUTTON_TERTIARY) != 0, mod);
		return 1;
	}
	case AMOTION_EVENT_ACTION_SCROLL:
		game_wheel(state, x, y, AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_VSCROLL, 0), mod);
		return 1;
	case AMOTION_EVENT_ACTION_DOWN:
	case AMOTION_EVENT_ACTION_UP:
		// ja tratados como BUTTON_PRESS/RELEASE
		return 1;
	default:
		return 0;
	}
}

sys::virtual_key android_key_to_virtual_key(int32_t code) {
	if(code >= AKEYCODE_A && code <= AKEYCODE_Z)
		return sys::virtual_key(uint8_t(sys::virtual_key::A) + (code - AKEYCODE_A));
	if(code >= AKEYCODE_0 && code <= AKEYCODE_9)
		return sys::virtual_key(uint8_t(sys::virtual_key::NUM_0) + (code - AKEYCODE_0));
	if(code >= AKEYCODE_F1 && code <= AKEYCODE_F12)
		return sys::virtual_key(uint8_t(sys::virtual_key::F1) + (code - AKEYCODE_F1));
	if(code >= AKEYCODE_NUMPAD_0 && code <= AKEYCODE_NUMPAD_9)
		return sys::virtual_key(uint8_t(sys::virtual_key::NUMPAD0) + (code - AKEYCODE_NUMPAD_0));
	switch(code) {
	case AKEYCODE_BACK: // botao/gesto de voltar
	case AKEYCODE_ESCAPE: return sys::virtual_key::ESCAPE;
	case AKEYCODE_ENTER:
	case AKEYCODE_NUMPAD_ENTER: return sys::virtual_key::RETURN;
	case AKEYCODE_TAB: return sys::virtual_key::TAB;
	case AKEYCODE_DEL: return sys::virtual_key::BACK; // backspace
	case AKEYCODE_FORWARD_DEL: return sys::virtual_key::DELETE_KEY;
	case AKEYCODE_SPACE: return sys::virtual_key::SPACE;
	case AKEYCODE_DPAD_LEFT: return sys::virtual_key::LEFT;
	case AKEYCODE_DPAD_RIGHT: return sys::virtual_key::RIGHT;
	case AKEYCODE_DPAD_UP: return sys::virtual_key::UP;
	case AKEYCODE_DPAD_DOWN: return sys::virtual_key::DOWN;
	case AKEYCODE_PAGE_UP: return sys::virtual_key::PRIOR;
	case AKEYCODE_PAGE_DOWN: return sys::virtual_key::NEXT;
	case AKEYCODE_MOVE_HOME: return sys::virtual_key::HOME;
	case AKEYCODE_MOVE_END: return sys::virtual_key::END;
	case AKEYCODE_INSERT: return sys::virtual_key::INSERT;
	case AKEYCODE_SHIFT_LEFT: return sys::virtual_key::LSHIFT;
	case AKEYCODE_SHIFT_RIGHT: return sys::virtual_key::RSHIFT;
	case AKEYCODE_CTRL_LEFT: return sys::virtual_key::LCONTROL;
	case AKEYCODE_CTRL_RIGHT: return sys::virtual_key::RCONTROL;
	case AKEYCODE_ALT_LEFT: return sys::virtual_key::LMENU;
	case AKEYCODE_ALT_RIGHT: return sys::virtual_key::RMENU;
	case AKEYCODE_COMMA: return sys::virtual_key::COMMA;
	case AKEYCODE_PERIOD: return sys::virtual_key::PERIOD;
	case AKEYCODE_MINUS: return sys::virtual_key::MINUS;
	case AKEYCODE_EQUALS:
	case AKEYCODE_PLUS: return sys::virtual_key::PLUS;
	case AKEYCODE_NUMPAD_ADD: return sys::virtual_key::ADD;
	case AKEYCODE_NUMPAD_SUBTRACT: return sys::virtual_key::SUBTRACT;
	case AKEYCODE_SEMICOLON: return sys::virtual_key::SEMICOLON;
	case AKEYCODE_SLASH: return sys::virtual_key::FORWARD_SLASH;
	case AKEYCODE_BACKSLASH: return sys::virtual_key::BACK_SLASH;
	case AKEYCODE_LEFT_BRACKET: return sys::virtual_key::OPEN_BRACKET;
	case AKEYCODE_RIGHT_BRACKET: return sys::virtual_key::CLOSED_BRACKET;
	case AKEYCODE_APOSTROPHE: return sys::virtual_key::QUOTE;
	case AKEYCODE_GRAVE: return sys::virtual_key::TILDA;
	default: return sys::virtual_key::NONE;
	}
}

int32_t handle_key(sys::state& state, AInputEvent* event) {
	int32_t const code = AKeyEvent_getKeyCode(event);
	auto const key = android_key_to_virtual_key(code);
	if(key == sys::virtual_key::NONE)
		return 0; // volume etc.: deixa o sistema tratar

	auto const mod = modifiers_from_meta(AKeyEvent_getMetaState(event));
	switch(AKeyEvent_getAction(event)) {
	case AKEY_EVENT_ACTION_DOWN:
		// repeticao so para as teclas de edicao, como no window_nix.cpp
		if(AKeyEvent_getRepeatCount(event) > 0) {
			switch(key) {
			case sys::virtual_key::RETURN: case sys::virtual_key::BACK: case sys::virtual_key::DELETE_KEY:
			case sys::virtual_key::LEFT: case sys::virtual_key::RIGHT: case sys::virtual_key::UP: case sys::virtual_key::DOWN:
				break;
			default:
				return 1;
			}
		}
		game_key(state, key, true, mod);
		return 1;
	case AKEY_EVENT_ACTION_UP:
		game_key(state, key, false, mod);
		return 1;
	default:
		return 1;
	}
	// TODO (entrada): caracteres digitados (on_text) precisam do teclado virtual
	// e de KeyEvent.getUnicodeChar via JNI -- a API nativa nao entrega o caractere.
}

int32_t handle_input(android_app* app, AInputEvent* event) {
	auto& state = *static_cast<sys::state*>(app->userData);
	switch(AInputEvent_getType(event)) {
	case AINPUT_EVENT_TYPE_MOTION:
		if(AInputEvent_getSource(event) == AINPUT_SOURCE_MOUSE)
			return handle_mouse(state, event);
		if(AInputEvent_getSource(event) & AINPUT_SOURCE_CLASS_POINTER)
			return handle_touch(app, state, event);
		return 0;
	case AINPUT_EVENT_TYPE_KEY:
		return handle_key(state, event);
	default:
		return 0;
	}
}

// Enquanto o jogo nao esta carregado: mostra os gestos reconhecidos, para
// testar o toque no aparelho. Fundo clareia/escurece com a pinca; quadrado
// branco = dedo arrastando, amarelo = toque (clique), vermelho = segurar (clique direito).
void render_touch_placeholder(sys::state& state) {
	float z = touch.placeholder_zoom / 10.f; // -1..1
	if(state.win_ptr->loading) {
		// carregando: o fundo "respira" para mostrar que o app nao travou
		auto const ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
		z += 0.5f * std::sin(float(ms % 2000) / 2000.f * 6.2831853f);
	}
	glViewport(0, 0, state.x_size, state.y_size);
	glDisable(GL_SCISSOR_TEST);
	glClearColor(0.11f + 0.08f * z, 0.20f + 0.10f * z, 0.33f + 0.12f * z, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

	auto square = [&](float x, float y, float r, float g, float b) {
		int32_t const half = int32_t(touch_slop_px(state.win_ptr->app) * 4.f);
		glEnable(GL_SCISSOR_TEST);
		glScissor(int32_t(x) - half, state.y_size - int32_t(y) - half, half * 2, half * 2); // GL tem y para cima
		glClearColor(r, g, b, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);
		glDisable(GL_SCISSOR_TEST);
	};

	using m = touch_tracker::mode;
	if(touch.current == m::left_drag || touch.current == m::map_pan || touch.current == m::pinch || touch.current == m::pending)
		square(touch.last_x, touch.last_y, 0.95f, 0.95f, 0.95f);

	if(std::chrono::steady_clock::now() - touch.flash_time < std::chrono::milliseconds(300)) {
		if(touch.last_flash == touch_tracker::flash::tap)
			square(touch.start_x, touch.start_y, 0.98f, 0.85f, 0.20f);
		else if(touch.last_flash == touch_tracker::flash::long_press)
			square(touch.start_x, touch.start_y, 0.90f, 0.25f, 0.20f);
	}
}

bool can_render(window_data_impl const& win) {
	return win.resumed && win.egl_surface != EGL_NO_SURFACE;
}

void render_frame(sys::state& state) {
	auto& win = *state.win_ptr;

	if(win.game_started) {
		// Carregar um save trava a tela ate a thread do jogo terminar (e o
		// pedido de cursor "ocupado" dela fica na fila desta mesma tela). A
		// janela de carregamento abre antes de esperar, e recebe os toques.
		bool const must_wait = state.yield_game_state_resetting_lock;
		if(must_wait)
			android_set_loading("Carregando...");
		std::shared_lock lock(state.game_state_resetting_lock);
		state.game_state_resetting_cv.wait(lock, [&] { return !state.yield_game_state_resetting_lock; });
		if(must_wait)
			android_set_loading("");
		state.render();
	} else {
		// sem jogo carregado ainda: prova que EGL/superficie/vsync e o toque funcionam
		render_touch_placeholder(state);
	}

	// GPU de celular desenha em blocos no proprio chip; avisar que depth e
	// stencil do quadro nao serao mais usados evita grava-los na memoria.
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	GLenum const discard[] = { GL_DEPTH, GL_STENCIL };
	glInvalidateFramebuffer(GL_FRAMEBUFFER, 2, discard);

	if(!eglSwapBuffers(win.egl_display, win.egl_surface)) {
		EGLint const err = eglGetError();
		ALICE_LOGE("eglSwapBuffers falhou (0x%x)", err);
		if(err == EGL_BAD_SURFACE || err == EGL_BAD_NATIVE_WINDOW) {
			// a janela sumiu entre o poll e o swap -- recria na mesma janela se ainda existir
			ANativeWindow* native_window = win.native_window;
			destroy_egl_surface(win);
			if(native_window && win.app->window == native_window)
				create_egl_surface(win, native_window);
		} else if(err == EGL_CONTEXT_LOST) {
			// raro (driver reiniciado): todos os recursos GL se perderam
			emit_error_message("O contexto OpenGL ES foi perdido pelo sistema.", true);
		}
	}

	if(win.game_started)
		sound::update_music_track(state);
}

} // namespace

float android_default_ui_scale(int32_t width, int32_t height, int32_t density_dpi) {
	// A interface do Victoria 2 foi feita para pelo menos ~1024x720 pixels:
	// com escala s, a tela "logica" e (width/s) x (height/s). Entao:
	//  - pela densidade: s = dpi/160 (1 dp do Android = 1 pixel da interface),
	//    o que deixa botoes e textos com tamanho fisico confortavel no dedo;
	//  - mas sem passar da maior escala que ainda cabe 1024x720 (num celular em
	//    paisagem e isso que limita: 1080 de altura -> no maximo 1,5).
	int32_t const long_side = std::max(width, height);
	int32_t const short_side = std::min(width, height);
	float const by_density = float(density_dpi > 0 ? density_dpi : 160) / 160.f;
	float const fits = std::min(float(long_side) / 1024.f, float(short_side) / 720.f);
	float const target = std::min(by_density, fits);

	// o maior valor da lista de opcoes que nao passa do alvo (minimo 0,75)
	float chosen = 0.75f;
	for(float v : sys::ui_scales) {
		if(v <= target + 0.001f && v >= chosen)
			chosen = v;
	}
	return chosen;
}

void android_text_result(char const* text) {
	std::lock_guard lock(text_result_mutex);
	if(text)
		text_result = std::string(text);
	else
		text_result_cancelled = true;
}

void android_set_setting(char const* key, int32_t value) {
	std::string_view const k = key ? key : "";
	if(k == "render_scale")
		pending_render_scale.store(value);
	else if(k == "fps_limit")
		pending_fps_limit.store(value);
	else if(k == "perf_counter")
		pending_perf_counter.store(value ? 1 : 0);
	else
		ALICE_LOGE("ajuste desconhecido: %s", key ? key : "(nulo)");
}

void android_start_game(sys::state& state) {
	auto& win = *state.win_ptr;
	assert(win.egl_surface != EGL_NO_SURFACE && !win.game_started);
	ALICE_LOGI("iniciando o jogo (OpenGL, som, on_create)");

	ogl::initialize_opengl(state);

	sound::initialize_sound_system(state);
	sound::start_music(state, state.user_settings.master_volume * state.user_settings.music_volume);

	// equivalente ao on_window_change inicial do window_nix.cpp
	int32_t const width = win.render_width;
	int32_t const height = win.render_height;

	if(win.first_run_defaults) {
		// primeira execucao (sem user_settings.dat); fica salvo e pode ser
		// mudado depois nas opcoes do jogo:
		//  - escala da interface pela tela. A densidade conta em pixels
		//    desenhados: com render_scale 0,75 cada um ocupa mais espaco na tela.
		int32_t const density = win.app->config ? AConfiguration_getDensity(win.app->config) : ACONFIGURATION_DENSITY_MEDIUM;
		int32_t const render_density = int32_t(float(density) * float(width) / float(std::max(1, win.physical_width)));
		state.user_settings.ui_scale = android_default_ui_scale(width, height, render_density);
		//  - sem MSAA: o jogo desenha numa textura multisample separada e resolve
		//    com um shader proprio; numa GPU de celular (tile-based) isso tira as
		//    4 amostras de cada pixel do chip para a memoria a cada quadro.
		state.user_settings.antialias_level = 0;
		state.save_user_settings();
		win.first_run_defaults = false;
		ALICE_LOGI("primeira execucao: escala da interface %.2f (desenho %dx%d, %d dpi efetivos), MSAA desligado", state.user_settings.ui_scale, width, height, render_density);
	}

	state.on_resize(width, height, window_state::maximized);
	state.x_size = width;
	state.y_size = height;

	state.on_create();
	win.loading = false;
	win.game_started = true;

	// Esta thread le o toque e desenha. Com o tempo correndo, a simulacao ocupa
	// todos os nucleos (thread do jogo + workers do TBB) e, com a mesma
	// prioridade, o toque espera a vez. Prioridade de "display urgente", a mesma
	// que o Android usa na thread de desenho dos apps. Fica para o fim: threads
	// herdam a prioridade de quem as cria, e as do jogo ja foram criadas.
	if(setpriority(PRIO_PROCESS, gettid(), -8) != 0)
		ALICE_LOGE("nao foi possivel subir a prioridade da thread de desenho (errno %d)", errno);

	// e, pelo mesmo motivo, os nucleos (ver "nucleos do processador")
	if(!cpus.detected)
		detect_cpu_layout();
	cpus.apply_now = true;
}

void run_android_main_loop(sys::state& game_state, android_app* app) {
	game_state.win_ptr = std::make_unique<window_data_impl>();
	auto& win = *game_state.win_ptr;
	win.app = app;

	app->userData = &game_state;
	app->onAppCmd = handle_cmd;
	app->onInputEvent = handle_input;

	while(!app->destroyRequested) {
		// renderizando: so processa o que ja chegou (timeout 0); parado: espera evento
		int const timeout = can_render(win) ? 0 : -1;
		android_poll_source* source = nullptr;
		int result = ALooper_pollOnce(timeout, nullptr, nullptr, reinterpret_cast<void**>(&source));
		while(result >= 0 || result == ALOOPER_POLL_CALLBACK) {
			if(source)
				source->process(app, source);
			if(app->destroyRequested)
				break;
			source = nullptr;
			result = ALooper_pollOnce(0, nullptr, nullptr, reinterpret_cast<void**>(&source));
		}
		if(app->destroyRequested)
			break;

		android_launcher_update(game_state);
		apply_pending_settings(game_state);

		if(can_render(win)) {
			update_cpu_affinity(win);
			update_text_input(game_state);
			update_touch(game_state);
			render_frame(game_state);
			update_perf_counter(game_state);
		}
	}

	terminate_egl(win);
	ALICE_LOGI("loop principal encerrado");
}

void change_cursor(sys::state& state, cursor_type type) {
	// Nao ha cursor em touchscreen. Mas o jogo marca com o cursor "ocupado" os
	// trabalhos demorados que travam a thread (criar a interface ao entrar na
	// partida, carregar save...): no Android isso vira a janela de carregamento.
	static std::atomic<bool> busy{ false };
	bool const now_busy = type == cursor_type::busy;
	if(busy.exchange(now_busy) != now_busy)
		android_set_loading(now_busy ? "Carregando..." : "");
}

void emit_error_message(std::string const& content, bool fatal) {
	__android_log_print(fatal ? ANDROID_LOG_FATAL : ANDROID_LOG_ERROR, "Alice", "%s", content.c_str());
	if(fatal) {
		std::abort(); // std::exit não é seguro de dentro do processo do app Android
	}
}

// text services (IME) -- mesmos no-ops usados em window_nix.cpp; entrada de texto
// no Android sera tratada via android_app / soft keyboard, nao por essa interface COM do Windows.
win32_text_services::win32_text_services() { }
win32_text_services::~win32_text_services() { }
void win32_text_services::start_text_services() { }
void win32_text_services::end_text_services() { }
void win32_text_services::on_text_change(text_services_object* ts, uint32_t old_start, uint32_t old_end, uint32_t new_end) { }
void win32_text_services::on_selection_change(text_services_object* ts) { }
bool win32_text_services::send_mouse_event_to_tso(text_services_object* ts, int32_t x, int32_t y, uint32_t buttons) {
	return false;
}
void win32_text_services::set_focus(sys::state& win, text_services_object* o) { }
void win32_text_services::suspend_keystroke_handling() { }
void win32_text_services::resume_keystroke_handling() { }
text_services_object* win32_text_services::create_text_service_object(sys::state& win, ui::element_base& ei) {
	return nullptr;
}
void release_text_services_object(text_services_object* ptr) { }

} // namespace window
