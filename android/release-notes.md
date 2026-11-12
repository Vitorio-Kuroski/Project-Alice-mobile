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

**Já tem uma versão anterior instalada?** Esta versão não instala por cima dela: desinstale a anterior antes. Desinstalar apaga os saves e as configurações; para guardar os saves, copie antes a pasta `Android/data/org.projectalice.mobile/files/.local/share/Alice/` pelo cabo USB e devolva-a depois de abrir a v0.9 uma vez.

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

- **Sincronizado com o Project Alice original** ([schombert/Project-Alice](https://github.com/schombert/Project-Alice)): traz as 224 mudanças mais recentes do motor, com melhorias de jogo e de interface (incluindo a interface militar) e correções.
- Continua com tudo da v0.8: mapa em resolução reduzida com interface nítida, ritmo de quadros regular (Swappy), Modo econômico, menos quadros com o jogo pausado, núcleos rápidos, seleção em caixa, digitar texto, vibração, Continuar e mods.

## Limitações conhecidas

- Entrar na partida leva alguns segundos num celular intermediário.

## Problemas?

Abra uma issue com a descrição. Se puder, anexe a saída de `adb logcat -s Alice`.
