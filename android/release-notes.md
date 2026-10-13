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

**Já tem a v0.3 instalada?** Esta versão não instala por cima dela: desinstale a anterior antes. Desinstalar apaga os saves e as configurações; para guardar os saves, copie antes a pasta `Android/data/org.projectalice.mobile/files/.local/share/Alice/` pelo cabo USB e devolva-a depois de abrir a v0.4 uma vez.

## Controles

| Gesto | Equivale a |
|---|---|
| Toque rápido | Clique esquerdo |
| Segurar parado por ~0,5 s | Clique direito |
| Arrastar sobre a interface | Arrastar com o botão esquerdo |
| Arrastar sobre o mapa | Mover o mapa |
| Pinça com dois dedos | Zoom |
| Voltar | ESC (menu) |

Mouse e teclado físicos também funcionam.

## Novidades desta versão

- **Sincronizado com o Project Alice original** ([schombert/Project-Alice](https://github.com/schombert/Project-Alice)): traz as 74 mudanças mais recentes do motor, com correções e ajustes de jogo e de interface.
- Texto renderizado com o Harfbuzz 14.4, versão mais nova da biblioteca de fontes.
- Continua com tudo da v0.3: Android 11+, build otimizado para GPUs de celular, escala da interface pela tela, tela cheia, correção dos painéis piscando nas Adreno, janela "Carregando..." e salvamento automático ao sair do app.

## Limitações conhecidas

- Sem seleção de mods; só o jogo base.
- Não é possível digitar texto (nome de save, chat).
- Seleção em caixa de várias unidades com o dedo ainda não existe.
- Entrar na partida leva alguns segundos num celular intermediário.

## Problemas?

Abra uma issue com a descrição. Se puder, anexe a saída de `adb logcat -s Alice`.
