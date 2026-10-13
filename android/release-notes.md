Versão de testes do **Project Alice para Android**: o motor de código aberto para jogar Victoria 2, rodando nativamente no celular.

> ⚠️ **Versão de testes.** Já roda num aparelho real (Moto One Hyper, Android 11, Adreno 612), mas espere problemas. Relatos são bem-vindos.

## Requisitos

- Android 11 ou mais novo, celular ou tablet **ARM 64 bits**
- GPU com OpenGL ES 3.2
- Os arquivos do **Victoria 2 com as expansões**, copiados para o aparelho. O conteúdo do jogo original não está incluído.

## Como instalar

1. Baixe o arquivo `.apk` abaixo, em **Assets**.
2. Copie a pasta do Victoria 2 para o aparelho, por exemplo para `Jogos/Victoria 2`. A pasta certa é a que contém `common`, `map` e `gfx`. Evite a pasta `Download` e a raiz do armazenamento, que o seletor do Android não deixa escolher.
3. Instale o APK. É preciso permitir a instalação de fontes desconhecidas.
4. Abra o app, toque em **Escolher pasta**, ative **Acesso a todos os arquivos**, volte ao app e escolha a pasta do jogo.
5. Na primeira vez o app monta o cenário, o que pode levar vários minutos.

**Vindo da v0.4 ou anterior?** Esta versão não instala por cima delas: desinstale a anterior antes. Desinstalar apaga os saves e as configurações; para guardar os saves, copie antes a pasta `Android/data/org.projectalice.mobile/files/.local/share/Alice/` pelo cabo USB e devolva-a depois de abrir a v0.5 uma vez. **É a última vez:** a partir da v0.5 o APK é assinado sempre com a mesma chave, e as próximas versões instalam por cima, mantendo saves e configurações.

## Controles

| Gesto | Equivale a |
|---|---|
| Toque rápido | Clique esquerdo |
| Segurar parado por ~0,5 s | Clique direito |
| Arrastar sobre a interface | Arrastar com o botão esquerdo |
| Arrastar sobre o mapa | Mover o mapa |
| Pinça com dois dedos | Zoom |
| Toque com três dedos | Mostra ou esconde o contador de desempenho (FPS) |
| Voltar | ESC (menu) |

Mouse e teclado físicos também funcionam.

## Novidades desta versão

- **Atualizações sem desinstalar:** o APK passa a ser assinado sempre com a mesma chave. Depois de instalar a v0.5, as próximas versões instalam por cima dela.
- **Resposta mais rápida ao toque com o jogo rodando:** a parte do app que lê o toque e desenha a tela ganhou prioridade sobre a simulação, que antes disputava os mesmos núcleos do processador.
- **Contador de desempenho:** toque com três dedos para ver FPS, tempo dos quadros, velocidade do jogo e dias simulados por segundo. Toque de novo para esconder.
- Continua com tudo da v0.4, incluindo a sincronização com o Project Alice original.

## Limitações conhecidas

- Sem seleção de mods; só o jogo base.
- Não é possível digitar texto (nome de save, chat).
- Seleção em caixa de várias unidades com o dedo ainda não existe.
- Entrar na partida leva alguns segundos num celular intermediário.

## Problemas?

Abra uma issue com a descrição. Se puder, anexe a saída de `adb logcat -s Alice`.
