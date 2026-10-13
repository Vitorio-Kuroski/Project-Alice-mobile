# Project Alice mobile

Versão para Android do [Project Alice](https://github.com/schombert/Project-Alice), um motor de código aberto para jogar **Victoria 2**. O código C++ do jogo roda nativamente no celular (ARM64, OpenGL ES 3.2), com controles por toque.

## ⬇️ Download

**[Baixar a última versão (APK)](https://github.com/Vitorio-Kuroski/Project-Alice-mobile/releases/latest/download/ProjectAlice-Android-arm64.apk)**

[Novidades e versões anteriores](https://github.com/Vitorio-Kuroski/Project-Alice-mobile/releases)

> **Versão de testes.** Já roda num aparelho real (Moto One Hyper, Android 11, Adreno 612), mas espere problemas.

## Requisitos

- Android 11 ou mais novo, celular ou tablet **ARM 64 bits**
- GPU com OpenGL ES 3.2
- Os arquivos do **Victoria 2 com as expansões**, copiados para o aparelho. O conteúdo do jogo original não está incluído.

## Como instalar

1. Baixe o APK pelo link acima.
2. Copie a pasta do Victoria 2 para o aparelho, por exemplo para `Jogos/Victoria 2`. A pasta certa é a que contém `common`, `map` e `gfx`. Evite a pasta `Download` e a raiz do armazenamento, que o seletor do Android não deixa escolher.
3. Instale o APK. É preciso permitir a instalação de fontes desconhecidas.
4. Abra o app, toque em **Escolher pasta**, ative **Acesso a todos os arquivos**, volte ao app e escolha a pasta do jogo.
5. Na primeira vez o app monta o cenário, o que pode levar vários minutos.

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

## Mais informações

O [**android/README.md**](android/README.md) tem o resto: onde ficam saves e configurações, desempenho, limitações conhecidas, como compilar e como o port funciona por dentro.

## Problemas?

Abra uma [issue](https://github.com/Vitorio-Kuroski/Project-Alice-mobile/issues) com a descrição. Se puder, anexe a saída de `adb logcat -s Alice`.
