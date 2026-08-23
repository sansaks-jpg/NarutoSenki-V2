# Build dan Platform

## Prinsip umum

Project memakai build system legacy per platform. Root `Makefile` mengorkestrasi engine, audio, extensions, Lua, dan target game; Android memakai Gradle + `ndk-build`; Windows memakai Visual Studio; macOS/iOS memakai Xcode; Linux memakai Makefile. Toolchain modern dapat memerlukan penyesuaian, jadi dokumentasikan perubahan environment sebelum memperbaiki error.

## Linux

`build.sh` membangun `tools/tolua++`, kemudian menjalankan build debug dengan Makefile. Root Makefile membangun komponen dasar secara berurutan sebelum `projects/NarutoSenki/proj.linux`.

```bash
./build.sh
# atau setelah tool/basis tersedia
make DEBUG=1 -j
```

Dependency yang disebut dokumentasi legacy meliputi compiler C/C++, freetype, GLFW, freealut/OpenAL, pthread, SQLite3, tolua++, dan debugger. Output release/deploy dibuat oleh `build-release.sh` dan `deploy.sh` ke `Build/NarutoSenki`.

## Android

Project Android berada di `projects/NarutoSenki/proj.android-studio`. `app/build.gradle` menggunakan external native build dengan `ndk-build`. `app/jni/Android.mk` mendaftarkan source C++ dan menghubungkan Cocos2d-x, CocosDenshion, Lua, extensions, SQLite, serta library native.

ABI yang dikonfigurasi pada `gradle.properties` adalah `armeabi-v7a` dan `arm64-v8a`. Task Gradle menyalin `../../lua` ke `app/assets/lua` dan `../../Resources` ke `app/assets` sebelum build. Jangan menjadikan folder hasil salinan sebagai source-of-truth.

```bash
cd projects/NarutoSenki/proj.android-studio
./gradlew assembleDebug
```

Sebelum distribusi, verifikasi package/application id, version code/name, ABI, target SDK, permission final, dan signing. Jangan menggunakan signing password yang ter-commit; gunakan keystore lokal/CI secret yang baru.

## Windows

Windows memiliki solution/project di `projects/NarutoSenki/proj.win32`. Root `build.bat`, `build-win32.bat`, dan `build-win32-debug.bat` membantu menjalankan build/copy asset. Toolchain menggunakan Visual Studio dan prebuilt library Win32. Periksa path separator serta command `rm -rf` pada script `.bat`; beberapa script legacy mengandalkan environment shell tertentu.

## macOS

macOS menggunakan `projects/NarutoSenki/proj.mac/hello.xcodeproj`. Dokumentasi legacy menyediakan `tools/patch_mac_xcode.py` dan script build LuaJIT. Alur umum:

```bash
cd scripting/lua/luajit
./build_mac_luajit.sh
cd ../../..
python3 tools/patch_mac_xcode.py
xcodebuild -project projects/NarutoSenki/proj.mac/hello.xcodeproj -scheme hello -configuration Debug
```

Jika project Xcode meminta `external/imgui` atau Chipmunk yang tidak tersedia, ikuti helper strip yang didokumentasikan di `Doc/README.md` dan jangan mengunduh dependency tidak dikenal tanpa verifikasi.

## iOS

iOS project lama berada di `projects/NarutoSenki/proj.ios`. Xcode project masih memiliki referensi lama ke WebSocket/libwebsockets dan dependency platform. Perlakukan iOS sebagai target legacy; perubahan di sini harus diverifikasi pada Xcode yang sesuai dan tidak boleh diasumsikan berdampak sama dengan Android.

## Generated output

| Output | Status | Aturan |
|---|---|---|
| `app/assets` | Generated Android assets | Jangan edit langsung; dihasilkan dari Lua/Resources. |
| `Build/NarutoSenki` | Packaging/deploy | Jangan commit hasil package. |
| `projects/NarutoSenki/__BIN__` | Build output | Di-ignore oleh repository. |
| `Debug.win32`, `Release*`, `obj`, `bin`, `gen` | Build/cache | Jangan commit. |
| `proj.mac/build` | Xcode generated output | Hapus/abaikan bila tidak diperlukan untuk source. |

## Debugging build

Bedakan error dependency engine dari error game. Jika compiler gagal pada header `cocos2d`, Lua, fmt, GLFW, SQLite, atau TOML, periksa include path dan prebuilt library target terlebih dahulu. Jika build berhasil tetapi runtime gagal, periksa resource copy, case-sensitive path, atlas registration, Lua search path, dan ABI.

## Referensi

[1]: ../build.sh "Linux build script"
[2]: ../Makefile "Root build orchestration"
[3]: ../projects/NarutoSenki/proj.android-studio/app/build.gradle "Android Gradle configuration"
[4]: ../projects/NarutoSenki/proj.android-studio/app/jni/Android.mk "Android native build"
[5]: ../Doc/README.md "Legacy build instructions"
