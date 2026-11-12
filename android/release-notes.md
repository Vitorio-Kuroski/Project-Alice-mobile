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

**Já tem uma versão anterior instalada?** Esta versão não instala por cima dela: desinstale a anterior antes. Desinstalar apaga os saves e as configurações; para guardar os saves, copie antes a pasta `Android/data/org.projectalice.mobile/files/.local/share/Alice/` pelo cabo USB e devolva-a depois de abrir a v0.7 uma vez.

## Controles

| Gesto | Equivale a |
|---|---|
| Toque rápido | Clique esquerdo |
| Segurar parado por ~0,5 s e soltar | Clique direito (ordens de movimento etc.) |
| Segurar ~0,5 s e arrastar no mapa | Caixa de seleção (várias tropas) |
| Arrastar sobre a interface | Arrastar com o botão esquerdo |
| Arrastar sobre o mapa | Mover o mapa |
| Pinça com dois dedos | Zoom |
| Toque com três dedos | Abre os **Ajustes do Android**: modo econômico, contador de desempenho, resolução da interface e do mapa, limite de FPS, vibração, mods |
| Voltar | ESC (menu) |

Para digitar (nome de save, chat), toque no campo de texto do jogo: abre o teclado do Android.

Mouse e teclado físicos também funcionam.

## Novidades desta versão

- **Ajustes do Android** (toque com três dedos): contador de desempenho, resolução do desenho (50%, 60%, 75% ou 100%), limite de 30 FPS, vibração e mods.
- **Toque mais rápido:** a parte que lê o toque e desenha a tela fica nos núcleos rápidos do processador, e um deles sobra livre da simulação.
- **Selecionar várias tropas:** segure ~0,5 s e arraste no mapa para desenhar a caixa de seleção. Segurar e soltar continua sendo o clique direito, agora ao soltar o dedo, com uma vibração curta para confirmar.
- **Digitar texto:** tocar num campo de texto do jogo (nome de save, chat) abre o teclado do Android.
- **Continuar:** ao abrir o jogo, o app oferece continuar a partida salva automaticamente quando você saiu.
- **Mods:** escolha os mods da pasta `mod` do Victoria 2 nos Ajustes do Android. Valem na próxima abertura; na primeira vez com uma combinação nova, o cenário é montado de novo.

## Limitações conhecidas

- Entrar na partida leva alguns segundos num celular intermediário.

## Problemas?

Abra uma issue com a descrição. Se puder, anexe a saída de `adb logcat -s Alice`.
