// entry_point_android.cpp
//
// ATENCAO: placeholder. entry_point_nix.cpp (402 linhas) faz bem mais que so
// abrir a janela -- carrega mods, monta/verifica o scenario file, parseia
// argumentos de linha de comando (--mod, porta de multiplayer, etc.) antes de
// finalmente chamar window::create_window(...). Portar tudo isso pro ciclo de
// vida do Android e trabalho de verdade da Fase 4, incluindo:
//   - usar android_native_app_glue em vez de um main() tradicional;
//   - so criar a janela/contexto dentro do callback APP_CMD_INIT_WINDOW;
//   - tratar APP_CMD_PAUSE/RESUME/TERM_WINDOW (o Android pode pausar ou matar
//     o app a qualquer momento -- nao existe isso no fluxo desktop);
//   - decidir de onde vem o "--mod"/selecao de scenario sem terminal
//     (provavelmente uma tela propria no app, nao argv).
//
// Por enquanto isso so faz o suficiente pra existir um android_main() valido
// e a Fase 1.4 conseguir compilar/linkar.

#include <android_native_app_glue.h>
#include <android/log.h>

#include "system_state.hpp"
#include "parsers_declarations.hpp"

static sys::state game_state;

void android_main(struct android_app* app) {
	__android_log_print(ANDROID_LOG_INFO, "Alice", "android_main iniciado (entry point ainda placeholder)");

	// TODO (Fase 4): loop de eventos padrao do android_native_app_glue, tratando
	// app->onAppCmd (APP_CMD_INIT_WINDOW -> window::create_window(...), etc.)
	// e app->onInputEvent, ate ALOOPER_POLL_ONCE indicar destruicao do app.
	int events;
	struct android_poll_source* source;
	while(true) {
		while(ALooper_pollAll(-1, nullptr, &events, (void**)&source) >= 0) {
			if(source != nullptr) {
				source->process(app, source);
			}
			if(app->destroyRequested != 0) {
				return;
			}
		}
	}
}
