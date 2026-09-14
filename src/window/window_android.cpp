// window_android.cpp
//
// Implementacao Android da interface declarada em window.hpp -- sem GLFW.
// A janela/superficie real (ANativeWindow + EGL) e criada a partir dos
// callbacks do android_native_app_glue (APP_CMD_INIT_WINDOW etc.), entao a
// maior parte das funcoes abaixo so pode ser preenchida de verdade na Fase 4
// (NativeActivity/GameActivity), quando existir um android_app* pra consultar.
// Por enquanto isso so precisa COMPILAR e dar pra Fase 1.4 rodar o primeiro
// build; as funcoes marcadas com TODO nao fazem nada util ainda.

#include "window.hpp"
#include "system_state.hpp"

#include <android/log.h>

namespace window {

int32_t cursor_blink_ms() {
	return 1000; // igual ao valor fixo usado em window_nix.cpp
}
int32_t double_click_ms() {
	return 500; // igual ao valor fixo usado em window_nix.cpp
}

bool is_key_depressed(sys::state const& game_state, sys::virtual_key key) {
	// TODO (Fase 4): mapear eventos de toque/teclado do android_app pra sys::virtual_key.
	// A maioria das teclas nem se aplica em touchscreen -- ver Fase 4 no plano.
	return false;
}

void get_window_size(sys::state const& game_state, int& width, int& height) {
	// TODO (Fase 4): ler de ANativeWindow_getWidth/Height do win_ptr->native_window.
	width = game_state.win_ptr ? game_state.win_ptr->creation_x_size : 0;
	height = game_state.win_ptr ? game_state.win_ptr->creation_y_size : 0;
}

bool is_in_fullscreen(sys::state const& game_state) {
	return true; // no Android o jogo sempre ocupa a tela toda
}

void set_borderless_full_screen(sys::state& game_state, bool fullscreen) {
	// no-op: no Android nao existe "janela" pra alternar -- sempre fullscreen
}

void close_window(sys::state& game_state) {
	// TODO (Fase 4): sinalizar pro android_main encerrar o loop principal
	// (equivalente ao glfwSetWindowShouldClose da versao nix).
}

void create_window(sys::state& game_state, creation_parameters const& params) {
	// TODO (Fase 4): so pode ser implementado de verdade dentro do android_main,
	// depois de receber APP_CMD_INIT_WINDOW com o ANativeWindow valido, e apos
	// criar o contexto EGL (ver opengl_wrapper_android.cpp).
	// Preenchera game_state.win_ptr->native_window / egl_display / egl_surface / egl_context.
	__android_log_print(ANDROID_LOG_ERROR, "Alice",
			"create_window ainda nao implementado (Fase 4)");
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
