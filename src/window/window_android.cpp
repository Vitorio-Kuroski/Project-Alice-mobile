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
//   segurar parado (~0,5 s)     -> clique direito (ordens de movimento etc.)
//   arrastar sobre a interface  -> arrasto com o botao esquerdo (barras, janelas)
//   arrastar sobre o mapa       -> arrasto com o botao do meio (move o mapa)
//   dois dedos                  -> pinca = zoom (roda do mouse), mover = arrasta o mapa
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

#include <chrono>
#include <cmath>

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
	if(game_state.win_ptr && game_state.win_ptr->native_window) {
		width = ANativeWindow_getWidth(game_state.win_ptr->native_window);
		height = ANativeWindow_getHeight(game_state.win_ptr->native_window);
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

bool init_egl_display(window_data_impl& win) {
	win.egl_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	if(win.egl_display == EGL_NO_DISPLAY || !eglInitialize(win.egl_display, nullptr, nullptr)) {
		ALICE_LOGE("eglInitialize falhou (0x%x)", eglGetError());
		return false;
	}

	EGLint const config_attribs[] = {
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_RED_SIZE, 8,
		EGL_GREEN_SIZE, 8,
		EGL_BLUE_SIZE, 8,
		EGL_ALPHA_SIZE, 8,
		EGL_DEPTH_SIZE, 24,
		EGL_STENCIL_SIZE, 8,
		EGL_NONE
	};
	EGLint num_configs = 0;
	if(!eglChooseConfig(win.egl_display, config_attribs, &win.egl_config, 1, &num_configs) || num_configs == 0) {
		ALICE_LOGE("nenhuma configuracao EGL com GLES 3 + RGBA8 + depth24/stencil8 (0x%x)", eglGetError());
		return false;
	}
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
	win.egl_surface = eglCreateWindowSurface(win.egl_display, win.egl_config, native_window, nullptr);
	if(win.egl_surface == EGL_NO_SURFACE) {
		ALICE_LOGE("eglCreateWindowSurface falhou (0x%x)", eglGetError());
		return false;
	}
	if(!eglMakeCurrent(win.egl_display, win.egl_surface, win.egl_surface, win.egl_context)) {
		ALICE_LOGE("eglMakeCurrent falhou (0x%x)", eglGetError());
		return false;
	}
	eglSwapInterval(win.egl_display, 1); // vsync, como o glfwSwapInterval(1) do desktop
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
	int32_t const width = ANativeWindow_getWidth(win.native_window);
	int32_t const height = ANativeWindow_getHeight(win.native_window);
	if(width <= 0 || height <= 0 || (width == state.x_size && height == state.y_size))
		return;
	ALICE_LOGI("tamanho da tela: %dx%d", width, height);
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
		break;
	case APP_CMD_PAUSE:
		ALICE_LOGI("APP_CMD_PAUSE");
		win.resumed = false;
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

float touch_slop_px(android_app* app) {
	int32_t const density = app->config ? AConfiguration_getDensity(app->config) : ACONFIGURATION_DENSITY_MEDIUM;
	return touch_slop_dp * float(density > 0 ? density : ACONFIGURATION_DENSITY_MEDIUM) / float(ACONFIGURATION_DENSITY_MEDIUM);
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
	float const x0 = AMotionEvent_getX(event, 0), y0 = AMotionEvent_getY(event, 0);
	float const x1 = AMotionEvent_getX(event, 1), y1 = AMotionEvent_getY(event, 1);
	mid_x = (x0 + x1) * 0.5f;
	mid_y = (y0 + y1) * 0.5f;
	distance = std::max(std::hypot(x1 - x0, y1 - y0), 1.f);
}

// encerra o gesto de um dedo em andamento, sem gerar clique
void end_single_finger_gesture(sys::state& state) {
	auto const x = int32_t(touch.last_x), y = int32_t(touch.last_y);
	auto const mod = sys::key_modifiers::modifiers_none;
	if(touch.current == touch_tracker::mode::left_drag)
		game_lbutton(state, x, y, false, mod);
	else if(touch.current == touch_tracker::mode::map_pan || touch.current == touch_tracker::mode::pinch)
		game_mbutton(state, x, y, false, mod);
	touch.current = touch_tracker::mode::none;
}

void start_flash(touch_tracker::flash f) {
	touch.last_flash = f;
	touch.flash_time = std::chrono::steady_clock::now();
}

int32_t handle_touch(android_app* app, sys::state& state, AInputEvent* event) {
	int32_t const action = AMotionEvent_getAction(event);
	int32_t const masked = action & AMOTION_EVENT_ACTION_MASK;
	auto const mod = sys::key_modifiers::modifiers_none;

	switch(masked) {
	case AMOTION_EVENT_ACTION_DOWN: {
		touch.current = touch_tracker::mode::pending;
		touch.primary_id = AMotionEvent_getPointerId(event, 0);
		touch.start_x = touch.last_x = AMotionEvent_getX(event, 0);
		touch.start_y = touch.last_y = AMotionEvent_getY(event, 0);
		touch.down_time = std::chrono::steady_clock::now();
		// sem botao apertado: so posiciona o "cursor" para o jogo achar o que esta sob o dedo
		game_mouse_move(state, int32_t(touch.last_x), int32_t(touch.last_y), mod);
		break;
	}
	case AMOTION_EVENT_ACTION_POINTER_DOWN: {
		if(AMotionEvent_getPointerCount(event) == 2 && touch.current != touch_tracker::mode::ignore_until_up) {
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
		float const x = AMotionEvent_getX(event, index);
		float const y = AMotionEvent_getY(event, index);

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
		if(touch.current == touch_tracker::mode::left_drag || touch.current == touch_tracker::mode::map_pan) {
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
			game_lbutton(state, x, y, true, mod);
			game_lbutton(state, x, y, false, mod);
			start_flash(touch_tracker::flash::tap);
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

// Segurar parado nao gera evento, entao o clique direito e verificado a cada frame.
void update_touch(sys::state& state) {
	if(touch.current != touch_tracker::mode::pending)
		return;
	if(std::chrono::steady_clock::now() - touch.down_time < long_press_time)
		return;
	auto const x = int32_t(touch.start_x), y = int32_t(touch.start_y);
	game_rbutton(state, x, y, true, sys::key_modifiers::modifiers_none);
	game_rbutton(state, x, y, false, sys::key_modifiers::modifiers_none);
	start_flash(touch_tracker::flash::long_press);
	touch.current = touch_tracker::mode::ignore_until_up;
}

// mouse USB/Bluetooth: repassa direto como no desktop
int32_t handle_mouse(sys::state& state, AInputEvent* event) {
	int32_t const masked = AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK;
	auto const x = int32_t(AMotionEvent_getX(event, 0));
	auto const y = int32_t(AMotionEvent_getY(event, 0));
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
		std::shared_lock lock(state.game_state_resetting_lock);
		state.game_state_resetting_cv.wait(lock, [&] { return !state.yield_game_state_resetting_lock; });
		state.render();
	} else {
		// sem jogo carregado ainda: prova que EGL/superficie/vsync e o toque funcionam
		render_touch_placeholder(state);
	}

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

void android_start_game(sys::state& state) {
	auto& win = *state.win_ptr;
	assert(win.egl_surface != EGL_NO_SURFACE && !win.game_started);
	ALICE_LOGI("iniciando o jogo (OpenGL, som, on_create)");

	ogl::initialize_opengl(state);

	sound::initialize_sound_system(state);
	sound::start_music(state, state.user_settings.master_volume * state.user_settings.music_volume);

	// equivalente ao on_window_change inicial do window_nix.cpp
	int32_t const width = ANativeWindow_getWidth(win.native_window);
	int32_t const height = ANativeWindow_getHeight(win.native_window);
	state.on_resize(width, height, window_state::maximized);
	state.x_size = width;
	state.y_size = height;

	state.on_create();
	win.loading = false;
	win.game_started = true;
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

		if(can_render(win)) {
			update_touch(game_state);
			render_frame(game_state);
		}
	}

	terminate_egl(win);
	ALICE_LOGI("loop principal encerrado");
}

void change_cursor(sys::state& state, cursor_type type) {
	// no-op -- nao ha cursor de mouse em touchscreen
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
