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

#include "window.hpp"
#include "system_state.hpp"

#include <android/log.h>
#include <android/native_window.h>
#include <android_native_app_glue.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl32.h>

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

	// TODO (Fase 5): com os arquivos do jogo disponiveis, na primeira janela:
	//   ogl::initialize_opengl(state); sound::initialize_sound_system(state);
	//   state.on_create(); win.game_started = true;
	// (o equivalente ao trecho final do create_window de window_nix.cpp)
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
		// TODO (Fase 5): autosave -- o Android pode matar o app em segundo plano
		break;
	case APP_CMD_DESTROY:
		ALICE_LOGI("APP_CMD_DESTROY");
		break;
	default:
		break;
	}
}

int32_t handle_input(android_app* app, AInputEvent* event) {
	// TODO (entrada): toque -> on_lbutton_down/up/on_mouse_drag, pinca -> zoom.
	// Retornar 0 deixa o sistema tratar (ex.: botao/gesto de voltar).
	return 0;
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
		// sem jogo carregado ainda: so prova que EGL/superficie/vsync funcionam
		glViewport(0, 0, state.x_size, state.y_size);
		glClearColor(0.11f, 0.20f, 0.33f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
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

		if(can_render(win))
			render_frame(game_state);
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
