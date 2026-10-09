// App Android do Project Alice.
//
// O codigo nativo NAO e compilado pelo Gradle: a libAlice.so sai do build CMake
// (preset android-arm64, ver CMakePresets.json) e e copiada para
// app/src/main/jniLibs/<abi>/ pelo script android/collect-native-libs.sh.
// O Gradle so empacota e assina o APK.

plugins {
    id("com.android.application")
}

android {
    namespace = "org.projectalice.mobile"
    compileSdk = 35

    defaultConfig {
        applicationId = "org.projectalice.mobile"
        minSdk = 31 // a solucao de ICU usa a libicu do sistema (API 31+)
        targetSdk = 35
        versionCode = 1
        versionName = "0.1-fase2"

        ndk {
            abiFilters += listOf("arm64-v8a")
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            // ainda sem chave de release propria -- assina com a chave de debug
            signingConfig = signingConfigs.getByName("debug")
        }
    }

    packaging {
        jniLibs {
            // as .so ja chegam sem simbolos (strip no collect-native-libs.sh)
            keepDebugSymbols += listOf("**/*.so")
        }
    }
}
