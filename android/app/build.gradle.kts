// App Android do Project Alice.
//
// O codigo nativo NAO e compilado pelo Gradle: a libAlice.so sai do build CMake
// (preset android-arm64, ver CMakePresets.json) e e copiada para
// app/src/main/jniLibs/<abi>/ pelo script android/collect-native-libs.sh.
// O Gradle so empacota e assina o APK.
//
// Os arquivos do proprio Alice (pasta assets/ do repositorio: shaders, fontes,
// sons, localizacao) vao dentro do APK em assets/assets/... junto com uma lista
// (alice_assets_manifest.txt); o C++ extrai tudo para o armazenamento interno
// na primeira execucao e quando a lista muda (entry_point_android.cpp).

import java.util.zip.CRC32

plugins {
    id("com.android.application")
}

val aliceAssetsSource = rootProject.file("../assets")
val aliceAssetsOut = layout.buildDirectory.dir("generated/aliceAssets")

val prepareAliceAssets by tasks.registering {
    inputs.dir(aliceAssetsSource)
    outputs.dir(aliceAssetsOut)
    doLast {
        val out = aliceAssetsOut.get().asFile
        out.deleteRecursively()
        val dest = File(out, "assets")
        aliceAssetsSource.copyRecursively(dest, overwrite = true)
        // lista "caminho<TAB>tamanho<TAB>crc32" -- o NDK nao consegue listar
        // subpastas do APK, e o crc faz a extracao se repetir se algo mudar
        val manifest = dest.walkTopDown()
            .filter { it.isFile }
            .map {
                val crc = CRC32()
                crc.update(it.readBytes())
                it.relativeTo(out).invariantSeparatorsPath + "\t" + it.length() + "\t" + crc.value.toString(16)
            }
            .sorted()
            .joinToString("\n")
        File(out, "alice_assets_manifest.txt").writeText(manifest + "\n")
    }
}

android {
    namespace = "org.projectalice.mobile"
    compileSdk = 35

    defaultConfig {
        applicationId = "org.projectalice.mobile"
        minSdk = 31 // a solucao de ICU usa a libicu do sistema (API 31+)
        targetSdk = 35
        versionCode = 1
        versionName = "0.2-fase5"

        ndk {
            abiFilters += listOf("arm64-v8a")
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            // ainda sem chave de release propria -- assina com a chave de debug
            signingConfig = signingConfigs.getByName("debug")
        }
    }

    sourceSets {
        getByName("main") {
            assets.srcDir(aliceAssetsOut)
        }
    }

    androidResources {
        // o padrao ignora arquivos comecando com "." e pastas com "_"
        ignoreAssetsPattern = "!.svn:!.git:!.ds_store:!*.scc:!CVS:!thumbs.db:!picasa.ini:!*~"
    }

    packaging {
        jniLibs {
            // as .so ja chegam sem simbolos (strip no collect-native-libs.sh)
            keepDebugSymbols += listOf("**/*.so")
        }
    }
}

tasks.named("preBuild") {
    dependsOn(prepareAliceAssets)
}
