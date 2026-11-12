message(STATUS "Fetching Swappy (Android Frame Pacing)...")

# Swappy, a biblioteca de ritmo de quadros (frame pacing) do Android Game
# Development Kit: entrega os quadros em intervalos regulares, alinhados ao
# vsync, o que deixa a imagem lisa e tira quadros enfileirados (atraso entre o
# toque e a tela). Vem do repositorio Maven do Google como AAR (um zip); aqui so
# a versao estatica para arm64 e os headers do formato prefab sao usados.
FetchContent_Declare(
	swappy
	URL https://dl.google.com/android/maven2/androidx/games/games-frame-pacing/2.1.3/games-frame-pacing-2.1.3.aar
	URL_HASH SHA256=a9f83e0cf47d694ed2cea0ae995d5cf7411060d9d4b680610430d03d6b20ea27
	DOWNLOAD_NAME games-frame-pacing-2.1.3.zip
	DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(swappy)

set(SWAPPY_PREFAB "${swappy_SOURCE_DIR}/prefab/modules/swappy_static")
add_library(dependency_swappy STATIC IMPORTED GLOBAL)
set_target_properties(dependency_swappy PROPERTIES
	IMPORTED_LOCATION "${SWAPPY_PREFAB}/libs/android.${ANDROID_ABI}/libswappy_static.a"
	INTERFACE_INCLUDE_DIRECTORIES "${SWAPPY_PREFAB}/include")
