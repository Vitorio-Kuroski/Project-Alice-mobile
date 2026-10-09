# Project Alice no Android

Port para Android do [Project Alice](https://github.com/schombert/Project-Alice), um motor de código aberto para jogar Victoria 2. O jogo roda nativamente: o mesmo código C++ do desktop é compilado para ARM64 e desenha com OpenGL ES 3.2.

> **Situação:** o app compila e gera um APK instalável. Ainda **não foi testado num aparelho de verdade**, então espere ajustes no primeiro teste. Veja [Limitações conhecidas](#limitações-conhecidas).

## Para jogar

### Requisitos

- Android 12 ou mais novo (API 31+)
- Celular ou tablet ARM 64 bits (arm64-v8a), que é a grande maioria dos aparelhos atuais
- GPU com OpenGL ES 3.2
- Os arquivos do **Victoria 2 com as expansões**, copiados para o aparelho. O Project Alice não inclui o conteúdo do jogo original.
- Espaço livre: cerca de 150 MB para o app e os arquivos dele, além da pasta do Victoria 2

### Instalação

1. Baixe o APK na aba **Actions** do GitHub. Abra a execução mais recente do workflow "Android port - build do APK (arm64-v8a)" e baixe o artefato `ProjectAlice-arm64-v8a-debug-apk`.
2. Copie a pasta do Victoria 2 para o aparelho, por exemplo para `Download/Victoria 2`. A pasta certa é a que contém `common`, `map` e `gfx`.
3. Instale o APK (é preciso permitir a instalação de fontes desconhecidas) ou use `adb install app-debug.apk`.

### Primeira execução

1. O app prepara os arquivos do próprio Alice (shaders, fontes, sons, textos). Isso leva alguns segundos e só se repete depois de uma atualização.
2. Aparece o aviso **"Pasta do Victoria 2"**. Toque em **Escolher pasta**:
   - o Android pede a permissão **"Acesso a todos os arquivos"**; ative-a e volte ao app (o jogo lê os arquivos direto da pasta);
   - no seletor, abra a pasta do Victoria 2 e toque em **Usar esta pasta**.
3. Na primeira vez o app **monta o cenário** a partir dos arquivos do jogo, o que pode levar vários minutos. Enquanto isso o fundo da tela pulsa. Nas próximas vezes o cenário já está pronto e o jogo abre direto.

A pasta escolhida fica salva. Para trocá-la, apague o arquivo `game_folder.txt` (ver [Onde ficam os arquivos](#onde-ficam-os-arquivos)) ou limpe os dados do app.

### Controles

| Gesto | Equivale a |
|---|---|
| Toque rápido | Clique esquerdo (selecionar, botões) |
| Segurar parado por ~0,5 s | Clique direito (ordem de movimento, menu da província) |
| Arrastar sobre a interface | Arrastar com o botão esquerdo (barras de rolagem, janelas) |
| Arrastar sobre o mapa | Mover o mapa |
| Pinça com dois dedos | Zoom (mover os dois dedos também arrasta o mapa) |
| Botão ou gesto de voltar | ESC (menu do jogo) |

Mouse (com os três botões e a roda) e teclado físico, por USB ou Bluetooth, funcionam como no desktop.

### Onde ficam os arquivos

| O quê | Onde |
|---|---|
| Configurações, cenários, saves, `scenario_errors.log` | `Android/data/org.projectalice.mobile/files/.local/share/Alice/` (acessível pelo cabo USB) |
| Pasta do Victoria 2 escolhida | `Android/data/org.projectalice.mobile/files/game_folder.txt` |
| Arquivos do Alice extraídos do APK | armazenamento interno do app (`/data/data/org.projectalice.mobile/files/alice`) |

O app nunca escreve na pasta do Victoria 2.

### Problemas? Mande o log

```bash
adb logcat -s Alice
```

O log mostra cada etapa: extração dos arquivos, pasta escolhida, montagem e carregamento do cenário, versão do OpenGL ES e GPU, e erros de shader ou textura.

## Para desenvolver

### Compilar pelo GitHub Actions

O workflow `.github/workflows/android-port-fase1.yml` roda manualmente. Na aba **Actions**, escolha "Android port - build do APK (arm64-v8a)", clique em **Run workflow** e selecione a branch. Ele:

1. valida os shaders como GLSL ES 3.20 e GLSL 4.60 (`android/validate-shaders.sh`);
2. compila no Linux as ferramentas que geram código durante o build;
3. compila a `libAlice.so` para arm64-v8a com o NDK r29;
4. gera o APK com o Gradle e o publica como artefato, junto com o log do build.

### Compilar localmente

Precisa de: CMake 3.28+, Ninja, clang, **Android NDK r29**, Android SDK (plataforma 35, build-tools 35), JDK 17+ e, no Linux, os pacotes de desenvolvimento de X11/OpenGL/ICU para as ferramentas de host.

```bash
# 1. ferramentas de geração de código (rodam no PC durante o build Android)
cmake --preset x64-debug-linux-clang
cmake --build out/build/x64-debug-linux-clang --target \
  ParserGenerator DCONGENERATOR DCONINTERFACEGEN DataContainer-Lua DataContainer_OOS_Reporter
mkdir -p native-tools
find out/build/x64-debug-linux-clang -type f \( -name ParserGenerator -o -name DCONGENERATOR \
  -o -name DCONINTERFACEGEN -o -name DataContainer-Lua -o -name DataContainer_OOS_Reporter \) \
  -exec cp {} native-tools/ \;

# 2. a biblioteca nativa
export ANDROID_NDK=/caminho/para/android-ndk-r29
cmake --preset android-arm64 -DALICE_NATIVE_TOOLS_DIR=$PWD/native-tools
cmake --build out/build/android-arm64 --target Alice

# 3. o APK
export ANDROID_HOME=/caminho/para/android-sdk
android/collect-native-libs.sh          # copia as .so para o Gradle (sem símbolos de debug)
cd android && ./gradlew assembleDebug   # -> app/build/outputs/apk/debug/app-debug.apk
```

Para validar os shaders depois de mexer neles (precisa do `glslang-tools`):

```bash
android/validate-shaders.sh
```

### Como o port funciona

O código nativo é compilado pelo CMake, não pelo Gradle. O Gradle só empacota a `.so`, a Activity Java e os arquivos do Alice, e assina o APK.

| Parte | Arquivo(s) | O que faz |
|---|---|---|
| Entrada e carregamento | `src/entry_point_android.cpp` | `android_main`; extrai os arquivos do Alice, acha a pasta do Victoria 2, monta ou carrega o cenário numa thread separada e inicia o jogo |
| Janela, EGL e entrada | `src/window/window_android.cpp` | Ciclo de vida da NativeActivity, contexto EGL (GLES 3.2), loop de render, toque, mouse e teclado |
| Contexto OpenGL | `src/graphics/opengl_wrapper_android.cpp` | `eglMakeCurrent`, vsync e saída de debug |
| OpenGL de desktop sobre GLES | `src/graphics/android_gl/glew.h` | Substitui o GLEW: implementa com GLES as funções de desktop que o jogo usa e descomprime texturas S3TC/DXT quando a GPU não suporta |
| Shaders | `assets/shaders/glsl/*`, `ogl::set_shader_prefix` | Os mesmos arquivos valem como GLSL 4.60 e GLSL ES 3.20; no Android o cabeçalho `#version` é trocado na hora |
| SSE/AVX no ARM | `src/x86_compat/`, `dependencies/simde` | Headers de intrínsecas x86 redirecionados para o [SIMDe](https://github.com/simd-everywhere/simde), que usa NEON |
| Activity Java | `android/app/src/main/java/.../AliceActivity.java` | Permissão de arquivos, seletor de pastas e mensagens; o resto é C++ |
| Empacotamento | `android/app/build.gradle.kts`, `android/collect-native-libs.sh` | `.so` em `jniLibs`; `assets/` do repositório dentro do APK com uma lista (com CRC32) para a extração |

Outras decisões que valem saber:

- **LuaJIT** é linkado de forma estática no Android. A `.so` dele tem nome com versão (`libluajit-5.1.so.2`), que o Android não carrega de dentro do APK.
- **ICU** vem do próprio sistema (`libicu.so`, API 31+), por isso o mínimo é Android 12.
- **Sistema de arquivos:** é o mesmo do Linux (`simple_fs_nix.cpp`). `$HOME` aponta para a pasta externa do app, e as raízes do jogo são `[pasta do Victoria 2, pasta interna com assets/]`.
- **Fechar o app** encerra o processo: as threads do jogo e o estado estático não podem ser reaproveitados por uma nova Activity.

### Limitações conhecidas

- **Nunca rodou num aparelho real.** Tudo foi verificado só por compilação, pelo validador de shaders e por testes do decodificador de texturas.
- **Mods:** só o jogo base; falta uma tela de seleção de mods.
- **Digitar texto** (nome de save, chat) não funciona: a API nativa não entrega os caracteres do teclado virtual. Isso precisa de uma ponte JNI.
- **Seleção em caixa** de várias unidades com o dedo ainda não existe; dá para selecionar uma por vez.
- **Sem salvamento automático** ao ir para segundo plano: o Android pode descartar uma partida em andamento se precisar de memória.
- **Valores de toque** (tempo do segurar, tolerância de movimento, velocidade do zoom da pinça) são palpites a ajustar com testes.
- **Só arm64-v8a**, build de debug assinado com a chave de debug.
- **Memória:** texturas S3TC descomprimidas ocupam mais memória de vídeo em GPUs sem suporte a S3TC (Mali, PowerVR).
- **DataContainer** é baixado da branch `master` sem versão fixa, e uma mudança lá pode quebrar o build.
