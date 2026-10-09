// entry_point_android.cpp
//
// Equivalente Android do entry_point_nix.cpp. No lugar de main(), a
// NativeActivity (via android_native_app_glue) chama android_main numa thread
// propria; o ciclo de vida (janela, pausa, foco, destruicao) e o loop de render
// ficam em window::run_android_main_loop (window_android.cpp).
//
// TODO (Fase 5): o que o entry_point_nix.cpp faz antes de abrir a janela --
// achar a pasta do jogo (add_root), selecionar/montar o arquivo de cenario e
// carrega-lo. No Android isso vira uma tela propria (sem argv/terminal) e os
// arquivos do Victoria 2 precisam estar acessiveis no armazenamento.

#include <android_native_app_glue.h>
#include <android/log.h>

#include "system_state.hpp"
#include "parsers_declarations.hpp"

static sys::state game_state;

void android_main(struct android_app* app) {
	__android_log_print(ANDROID_LOG_INFO, "Alice", "android_main iniciado");
	window::run_android_main_loop(game_state, app);
}
