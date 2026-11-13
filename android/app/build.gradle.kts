plugins {
    id("com.android.application")
}

android {
    namespace = "com.turok2port.app"
    compileSdk = 34
    ndkVersion = "26.3.11579264"

    defaultConfig {
        applicationId = "com.turok2port.app"
        minSdk = 26
        targetSdk = 34
        versionCode = 1
        versionName = "0.1.0"

        ndk {
            // Solo ARM64: no se generan otros ABIs
            abiFilters += listOf("arm64-v8a")
        }

        externalNativeBuild {
            cmake {
                arguments += listOf("-DANDROID_STL=c++_shared")
                cppFlags += listOf("-std=c++17")
            }
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            // Firma release: configurar vía variables de entorno, nunca en el repo
        }
    }

    androidResources {
        // .kpf (ZIP) y .ogg ya están comprimidos: sin recompresión el motor puede abrirlos
        // por offset con AAssetManager (solo aplica a builds locales con assets/game_data/)
        noCompress += listOf("kpf", "ogg")
    }

    externalNativeBuild {
        cmake {
            path = file("../../src/CMakeLists.txt")
            version = "3.22.1"
        }
    }
}
