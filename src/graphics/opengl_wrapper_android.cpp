// opengl_wrapper_android.cpp
//
// ATENCAO: isso aqui e so um placeholder, nao uma implementacao real ainda.
// A versao _nix (opengl_wrapper_nix.cpp) so faz glfwMakeContextCurrent +
// glfwSwapInterval porque a CRIACAO do contexto ja aconteceu no glfwCreateWindow
// (dentro de window_nix.cpp). No Android quem cria o EGLContext/EGLSurface e o
// window_android.cpp (a partir do ANativeWindow recebido em APP_CMD_INIT_WINDOW),
// entao esse arquivo so devera existir pra chamar eglMakeCurrent/eglSwapInterval
// usando os handles guardados em win_ptr (egl_display/egl_surface/egl_context).
//
// Alem disso, opengl_wrapper.hpp inclui GLEW (desktop-only, nao funciona com
// GLES) -- resolver isso e trabalho da Fase 3 (portar os shaders GLSL -> GLSL ES
// tambem implica trocar o carregador de extensoes; GLES normalmente nao precisa
// de GLEW, as funcoes vem direto dos headers do sistema). Por isso as funcoes
// abaixo ainda nao tentam usar EGL de verdade -- so evitam quebrar o link.

#include "opengl_wrapper.hpp"
#include "system_state.hpp"

#include <android/log.h>

namespace ogl {

void create_opengl_context(sys::state& state) {
	// TODO (Fase 3/4): eglMakeCurrent(win_ptr->egl_display, win_ptr->egl_surface,
	//                                  win_ptr->egl_surface, win_ptr->egl_context);
	//                  eglSwapInterval(win_ptr->egl_display, 1);
	// Pendente de: (a) window_android.cpp criar o contexto EGL de verdade (Fase 4),
	//              (b) GLEW ser substituido por um caminho GLES (Fase 3).
	__android_log_print(ANDROID_LOG_ERROR, "Alice",
			"create_opengl_context (EGL) ainda nao implementado (Fase 3/4)");
}

void shutdown_opengl(sys::state& state) { }

} // namespace ogl
