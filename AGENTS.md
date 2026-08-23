# Panduan Agent — NarutoSenki-V2

Dokumen ini adalah panduan kerja untuk agent dan kontributor yang memodifikasi repository NarutoSenki-V2.

## Documentation index

Dokumentasi lengkap berada di [`docs/README.md`](docs/README.md). Baca dokumen sesuai area kerja:

| Area | Dokumen |
|---|---|
| Struktur dan layer arsitektur | [`docs/architecture.md`](docs/architecture.md) |
| Startup, scene, dan lifecycle | [`docs/runtime-flow.md`](docs/runtime-flow.md) |
| Semua workflow UI dan tombol | [`docs/ui-workflow.md`](docs/ui-workflow.md) |
| Mode dan aturan gameplay | [`docs/gameplay-modes.md`](docs/gameplay-modes.md) |
| Asset, metadata, parser, dan save | [`docs/assets-and-data.md`](docs/assets-and-data.md) |
| Build lintas platform | [`docs/build-and-platforms.md`](docs/build-and-platforms.md) |
| Kontribusi, debugging, dan testing | [`docs/development-guide.md`](docs/development-guide.md) |
| Roadmap multiplayer | [`docs/multiplayer-roadmap.md`](docs/multiplayer-roadmap.md) |
| Keamanan dan supply chain | [`docs/security.md`](docs/security.md) |

`AGENTS.md` berisi aturan kerja dan guardrail singkat; detail perilaku project harus ditulis atau diperbarui di dokumen `docs/` terkait. Snapshot branch `feature/lan-hotspot-multiplayer` juga memiliki fitur LAN MVP 1v1 berbasis UDP hotspot/LAN; fitur ini tidak boleh diasumsikan tersedia pada `main`/`master` sampai branch tersebut digabungkan. Project ini adalah game 2D Android/desktop berbasis **Cocos2d-x lama yang dikustomisasi**, dengan gameplay utama dalam C++, scripting/UI tertentu dalam Lua, dan resource game khusus. Jangan mengasumsikan bahwa project ini adalah Unity, Godot, atau project modern CMake.

## Tujuan dan batasan umum

Pertahankan perilaku game yang sudah berjalan sebelum melakukan refactor besar. Perubahan gameplay harus diuji pada battle scene, bukan hanya pada menu. Perubahan UI harus memperhatikan pembagian tanggung jawab Lua–C++ yang dijelaskan di `Doc/MENU_FLOW_BOUNDARY.md`. Jangan mengedit library engine/vendor hanya untuk menyelesaikan bug di game layer jika masalah dapat diselesaikan di `projects/NarutoSenki`.

Repository publik asal yang menjadi dasar salinan ini adalah `Zx-Akito/NarutoSenki-V2`. Metadata repository asal tidak mencantumkan lisensi, sehingga source dan asset harus diperlakukan sebagai kode yang penggunaannya terbatas. Pertahankan kredit dan jangan mendistribusikan ulang di luar izin yang sesuai.

## Peta repository

| Path | Peran | Catatan perubahan |
|---|---|---|
| `projects/NarutoSenki/Classes` | Source C++ game | Area utama untuk gameplay, scene, UI, mode, unit, utilitas game, dan subsistem LAN di `Classes/Network`. |
| `projects/NarutoSenki/lua` | Bootstrap dan UI Lua | Berisi `main.lua`, konfigurasi, framework Lua, scene/menu, audio, save, dan utility. |
| `projects/NarutoSenki/Resources` | Asset runtime | Audio, map, efek, font, UI, unit, konfigurasi, dan metadata gameplay. |
| `projects/NarutoSenki/proj.android-studio` | Project Android | Gradle + NDK build; menyalin Lua dan Resource ke `app/assets` saat `preBuild`. |
| `projects/NarutoSenki/proj.linux` | Target Linux | Makefile dan entry point desktop Linux. |
| `projects/NarutoSenki/proj.win32` | Target Windows | Visual Studio project dan Windows-specific entry point. |
| `projects/NarutoSenki/proj.mac` | Target macOS | Xcode project, Objective-C++ glue, dan resource bundle handling. |
| `projects/NarutoSenki/proj.ios` | Target iOS lama | Xcode project dan glue iOS; perlakukan sebagai target legacy. |
| `cocos2dx` | Engine vendored | Implementasi Cocos2d-x, renderer, scene graph, action, sprite, input, platform, tilemap, dan Lua support lama. Hindari perubahan tanpa alasan lintas project. |
| `extensions` | Extension Cocos2d-x | CocoStudio dan GUI/control extension. |
| `CocosDenshion` | Audio engine | Wrapper audio lintas platform yang digunakan oleh game. |
| `scripting/lua` | Runtime Lua dan binding | LuaJIT, binding `cocos2dx_support`, tolua, dan xxtea. |
| `external` | Dependensi pihak ketiga | `fmt`, `glfw3`, SQLite3, dan TOML parser/prebuilt library. |
| `tools/tolua++` | Generator binding | Tool untuk menghasilkan binding Lua; jangan mengedit generated output tanpa memahami sumbernya. |
| `Build` | Template/package helper | Artefak packaging dan template distribusi, bukan runtime game. |
| `Doc` | Dokumentasi | Baca sebelum mengubah menu flow, build, atau platform. |

Inventaris lokal menunjukkan sekitar 2.630 file repository termasuk asset/binary; area vendored paling besar adalah `cocos2dx`, sedangkan area game sendiri terdiri dari sekitar 162 file C++/header, 42 file Lua, dan banyak resource audio/tekstur/metadata. Angka tersebut adalah snapshot repository dan tidak boleh dipakai sebagai kontrak API.

## Arsitektur runtime

Urutan startup utama adalah sebagai berikut:

```text
Platform entry point
  -> AppDelegate::applicationDidFinishLaunching()
  -> LuaEngine dan CCScriptEngineManager
  -> Internal::initAllSystems() / CommandSystem::reset()
  -> lua/main.lua
  -> lua/config.lua
  -> GameScene Lua
  -> StartMenu C++
  -> GameModeLayer C++
  -> SelectLayer C++
  -> LoadLayer
  -> GameLayer battle
  -> GameOver dan callback kembali ke menu
```

`AppDelegate.cpp` memasang search path Lua dan resource, membuat `GLView`, memasang `Director`, menginisialisasi system, lalu menjalankan `main.lua`. `lua/main.lua` membuat `GameScene` dan menjalankannya melalui `CCDirector`. `GameScene.lua` mengatur splash/logo, save default, audio awal, dan transisi ke `StartMenu` C++.

Di battle scene, `GameLayer` menyimpan map, player, array hero, flog, tower, serta batch node untuk skill effect, damage effect, bullet, dan shadow. Runtime battle dibantu oleh `BattleRuntimeSystem`, `SpawnSystem`, `SessionState`, dan `CommandSystem`. `CharacterBase` adalah basis sprite unit dengan state seperti `IDLE`, `WALK`, `NATTACK`, `SATTACK`, `OATTACK`, `O2ATTACK`, `HURT`, `KNOCKDOWN`, `FLOAT`, `JUMP`, dan `DEAD`. `Hero` menambahkan perilaku pemain/AI, statistik, skill, gear, buff, dan reborn.

### Pembagian modul gameplay C++

| Modul | Tanggung jawab | Extension point utama |
|---|---|---|
| `Classes/CharacterBase.*` | State unit, HP/CKR, target, damage, animation, buff, collision, dan lifecycle unit. | Perubahan aturan umum unit harus diuji pada banyak karakter. |
| `Classes/Core/Hero.hpp` | Perilaku hero, input/AI, serangan, skill, gear, level, reborn, dan combat state. | Tambahkan perilaku bersama di sini hanya jika benar-benar lintas hero. |
| `Classes/Core/Shinobi` | Implementasi karakter per nama hero. | Buat kelas baru mengikuti pola hero yang ada dan daftarkan di `Provider.hpp`. |
| `Classes/Core/Bunshin` | Clone Naruto dan varian clone. | Perhatikan `Role::Clone` saat factory membuat instance. |
| `Classes/Core/Guardian` | Guardian atau unit khusus. | Jangan menyamakan lifecycle guardian dengan hero biasa tanpa verifikasi. |
| `Classes/Core/Kuchiyose` | Summon/creature seperti Akamaru, Kurama, Saso, dan lainnya. | Pastikan master/controller/target dibersihkan saat summon berakhir. |
| `Classes/Core/Projectile/Bullet.hpp` | Projectile/bullet dan efek serangan jarak jauh. | Untuk networking, projectile harus memiliki identitas dan lifecycle deterministik. |
| `Classes/Core/Tower` dan `Warrior/Flog` | Tower dan unit wave/creep. | Perhatikan spawn timing, target, group, dan win condition. |
| `Classes/Core/Provider.hpp` | Factory string-to-class untuk hero, clone, guardian, dan summon. | Setiap nama resource yang baru harus konsisten dengan factory dan asset. |
| `Classes/GameMode` | Konfigurasi roster, map, tower, flog, reborn, gear, team, dan callback match. | Mode baru harus mengikuti `IGameModeHandler`. |
| `Classes/Systems` | Inisialisasi command, battle runtime, spawn, session, dan helper sistem. | System baru harus memiliki lifecycle jelas dan tidak membuat global state yang tidak perlu. |
| `Classes/UI` | Menu mode, HUD, battle controls, pause, credits, gear, game over, dan select UI. | UI tidak boleh mengubah aturan simulation secara diam-diam. |
| `Classes/MyUtils` | SQLite save/record, shake/stroke/UI helper, dan utility legacy. | SQL query harus tetap valid untuk schema yang sudah ada. |
| `Classes/Utils` | Lua bridge, parser, logging, keyboard, compatibility, sprite/resource helper. | Gunakan helper yang ada sebelum membuat bridge atau formatter baru. |
| `Classes/Data`, `Enums`, `Constants` | Tipe data hero, enum state/role/group, nilai resolusi, z-order, dan callback key. | Tambahkan enum/constant di lokasi sentral, bukan magic number di scene. |

### Mode game

`IGameModeHandler.hpp` adalah abstraksi utama mode game. Enum yang tersedia mencakup `OneVsOne`, `Classic`, `FourVsFour`, `HardCore_4Vs4`, `Boss`, `Clone`, `Deathmatch`, dan `RandomDeathmatch`. Handler mengatur `GameData`, `HeroData`, map, tower, flog, reborn, group pemain, dan daftar hero. Helper `initHeros(...)` memilih roster pemain/COM, melakukan randomisasi group/hero, dan mengisi `heroDataVector` yang kemudian dipakai `GameLayer`.

`GameModeImpl.h` mengagregasikan implementasi mode di `Classes/GameMode/Impl`. `GameModeLayer.cpp` memilih mode dan memanggil callback Lua untuk berpindah ke character selection. Jangan menambahkan mode hanya di UI; mode harus memiliki aturan init, roster, start, game-over, serta callback character yang lengkap.

### Factory karakter

`Provider::create(name, role, group)` adalah registry/factory utama. Nama string seperti `Jiraiya`/`SageJiraiya`, `Pain`/`Nagato`, `Lee`/`RockLee`, serta varian Naruto memiliki alias atau perilaku khusus. Instance baru harus diinisialisasi, diberi `setID(name, role, group)`, dan mengikuti ownership Cocos2d-x (`autorelease`). Jika nama tidak dikenal, factory saat ini jatuh ke `DefaultAI`; jangan menganggap fallback tersebut sebagai error handling yang aman untuk karakter baru.

## Batas Lua dan C++

Lua adalah sumber kebenaran untuk transisi scene/menu. C++ bertanggung jawab atas state widget/input dan payload pemilihan mode. Callback yang saat ini dipusatkan di `Classes/Constants/UiFlowKeys.hpp` adalah:

| Pemanggil C++ | Callback Lua | Tujuan |
|---|---|---|
| `GameModeLayer` | `enterSelectLayer(mode, enableCustomSelect)` | Membuka select layer. |
| `GameLayer` | `onGameOver()` | Kembali setelah match berakhir. |
| `SelectLayer` | `backToStartMenu()` | Kembali dari character selection. |
| `CreditsLayer` | `CreditsLayer_BackToStartMenu()` | Kembali dari credits. |

Saat menambahkan callback, tambahkan key di `UiFlowKeys.hpp`, implementasikan global Lua function di `lua/ui/StartMenu.lua` atau scene terkait, lalu panggil melalui helper di `Utils/Cocos2dxHelper.hpp`. Hindari string callback yang ditulis ulang di banyak file. Helper standar adalah `lua_call_func`, `lua_call_init_func`, `lua_call_handler`, `get_luastack`, dan `lua_getL`.

Lua bootstrap memuat `config.lua`, framework Cocos2d-x, utility, core, UI, dan class module. `lua/core` mengatur audio/mode core; `lua/ui` mengatur scene/menu/UI; `lua/framework/cocos2dx` menyediakan wrapper API engine; `lua/class` dan `lua/utils` menyediakan helper object/class dan utility.

## Resource dan pipeline asset

`projects/NarutoSenki/Resources` berisi subdirektori `Audio`, `Config`, `Effects`, `Fonts`, `GameMode`, `Maps`, `UI`, dan `Unit`. Audio menggunakan `.ogg`. Map dan data game menggunakan format project seperti `.tmx`, `.ns`, `.nsi`, `.nst`, `.nsx`, serta metadata/config text. Nama file resource sering dibentuk dari nama hero, skill, arah, dan nomor animasi; perubahan nama file dapat menyebabkan runtime gagal memuat asset tanpa error yang jelas.

`Core/Utils/Parser.hpp` membaca metadata unit berbasis TOML dan menerjemahkannya menjadi statistik, action, cooldown, combat point, animation frame, event, dan nilai default. `Config.h`, `UnitData.h`, `UnitDefines.h`, serta `Enums` menentukan kontrak data yang dipakai parser dan runtime. Map dibuat/diperiksa menggunakan Tiled sesuai dokumentasi `Doc/README.md`.

Pada Android, `app/build.gradle` menjalankan `copyAssets` sebelum build: seluruh `../../lua` masuk ke `app/assets/lua`, sedangkan `../../Resources` masuk ke `app/assets`. Jangan mengedit hasil `app/assets` sebagai sumber utama karena folder tersebut generated dan di-ignore oleh `.gitignore`; ubah `projects/NarutoSenki/lua` atau `projects/NarutoSenki/Resources`.

## Build dan platform

### Linux

Root `Makefile` mengorkestrasi build berurutan untuk `cocos2dx`, `CocosDenshion`, `extensions`, `scripting/lua`, lalu `projects/NarutoSenki`. Script `build.sh` terlebih dahulu membangun `tools/tolua++`, kemudian menjalankan `make DEBUG=1 -j`.

```bash
./build.sh
# atau
make DEBUG=1 -j
```

Dependency Linux dan detail platform ada di `Doc/README.md`. Jika hanya mengubah game C++, build target game setelah library dasar tersedia agar error lebih mudah diisolasi.

### Android

Android memakai Gradle + `externalNativeBuild` dengan NDK `ndk-build`. `app/jni/Android.mk` mengompilasi entry point, source C++ game utama, SQLite, lalu menggabungkan static library Cocos2d-x, CocosDenshion, Lua, dan extensions. ABI yang dikonfigurasi adalah `armeabi-v7a` dan `arm64-v8a`.

```bash
cd projects/NarutoSenki/proj.android-studio
./gradlew assembleDebug
```

Source tree memiliki beberapa metadata Android yang tampak legacy atau tidak selalu sama dengan APK yang dianalisis, termasuk perbedaan `applicationId` di Gradle dan package di manifest. Jangan mengubah package, target SDK, signing, atau ABI hanya untuk merapikan konfigurasi tanpa menguji instalasi dan launch APK.

### Windows, macOS, dan iOS

Windows dapat dibangun melalui `build.bat` atau solution Visual Studio di `projects/NarutoSenki/proj.win32`. macOS menggunakan Xcode project di `projects/NarutoSenki/proj.mac`; iOS berada di `projects/NarutoSenki/proj.ios` dan bersifat legacy. Platform-specific code berada di engine dan project platform masing-masing; gunakan `#if CC_TARGET_PLATFORM` secara konsisten.

## Dependensi dan ownership

Dependensi utama yang terlihat dari include/build adalah sebagai berikut:

| Dependensi | Fungsi |
|---|---|
| Cocos2d-x vendored | Scene graph, rendering, sprite/action, input, tilemap, file resolver, dan platform layer. |
| LuaJIT + Cocos2d-x Lua support | Scripting dan scene/UI bootstrap. |
| tolua++ | Binding C++ ke Lua. |
| CocosDenshion/OpenAL/OpenSLES | Audio lintas platform. |
| `fmt` | Formatting pada platform yang tidak memakai `std::format`. |
| GLFW3 | Input/window desktop. |
| SQLite3 | Save, game record, coin, dan statistik karakter lokal. |
| TOML parser | Parsing metadata unit/config. |
| xxtea | Infrastruktur legacy untuk script protection; jangan mengandalkan ini sebagai security boundary. |

## Aturan perubahan kode

Gunakan include path dan naming convention yang sudah dipakai project. C++ game umumnya menggunakan `cocos2d`, `std`, pointer/ref-counted object Cocos2d-x, `CREATE_FUNC`, dan callback/action Cocos2d-x. Hindari memperkenalkan smart pointer ke object yang dikelola Cocos2d-x tanpa memahami ownership; `GameLayer` memang memakai `std::unique_ptr` untuk system internal tertentu, tetapi node dan sprite tetap mengikuti lifecycle engine.

Jangan menaruh aturan gameplay baru di layer UI jika aturan tersebut memengaruhi simulation. Pisahkan perubahan visual, input, dan simulation. Hindari global state baru; repository sudah memiliki beberapa global seperti `_gLayer` dan game-mode handler, sehingga perubahan baru harus membatasi coupling.

Untuk karakter baru, tambahkan kelas di `Classes/Core/Shinobi` atau kategori yang sesuai, resource di `Resources/Unit`/`Resources/Audio`/`Resources/UI`, lalu daftarkan nama dan alias di `Provider.hpp`. Pastikan `setID`, group, role, animation/action name, sound name, skill cooldown, dan resource path konsisten.

Untuk mode baru, tambahkan enum/data, implementasi `IGameModeHandler`, wiring di `GameModeImpl.h`/`GameModeLayer`, localization di Lua/resource yang relevan, dan test untuk roster/map/win condition. Jangan mengandalkan random default saat menulis test; seed atau expose deterministic setup bila diperlukan.

Untuk multiplayer LAN, jangan menjadikan posisi client sebagai sumber kebenaran. Pisahkan input command dari state snapshot, tetapkan tick/seed, dan pastikan damage, cooldown, projectile, tower, reborn, dan win condition dapat divalidasi host. `LanTransport`/worker harus bebas Cocos2d-x; hanya main thread melalui `LanSession::poll()` yang boleh menyentuh GameLayer atau node. Lifecycle jaringan bersifat opt-in: offline/Training tidak membuat socket dan tidak melakukan polling; Host/Join baru mengaktifkan transport/discovery. Detail ada di [`docs/lan-multiplayer.md`](docs/lan-multiplayer.md) dan [`docs/multiplayer-roadmap.md`](docs/multiplayer-roadmap.md). `CommandSystem`, `GameLayer`, `CharacterBase`, `Hero`, `Projectile/Bullet`, `SpawnSystem`, dan `IGameModeHandler` adalah area utama yang harus ditinjau; menu/lobby saja tidak cukup.

## Testing dan verifikasi

Repository ini tidak menunjukkan test suite unit/integration yang lengkap. Karena itu, setiap perubahan harus diverifikasi melalui kombinasi build compiler dan smoke test manual. Minimalnya adalah launch ke menu, masuk select layer, mulai satu battle, menguji input gerak/serang/skill, memeriksa audio dan asset, menyelesaikan atau surrender match, lalu kembali ke menu. Untuk perubahan save, periksa record/coin setelah restart. Untuk perubahan platform, uji target yang terdampak; jangan menganggap build Linux membuktikan Android benar.

Untuk perubahan mode atau gameplay, catat setup reproduksi: mode, map, hero, group, gear, seed/random jika ada, dan urutan input. Untuk perubahan LAN, catat role host/client, alamat/port, hotspot/AP isolation, room state, match id, tick, sequence, dan urutan Host/Join/ready/start/leave. Untuk perubahan resource, periksa case sensitivity path karena Android membedakan huruf besar-kecil.

## Keamanan repository

`projects/NarutoSenki/proj.android-studio/app` dan `gradle.properties` memiliki file keystore serta konfigurasi signing yang pernah dilacak repository. Jangan menyalin, mencetak, atau memperluas penggunaan password/key alias yang ada. Perlakukan kredensial tersebut sebagai berpotensi terekspos; untuk build baru gunakan signing lokal yang tidak di-commit, pindahkan secret ke `~/.gradle/gradle.properties` atau secret manager, dan pertimbangkan rotasi key/password sebelum distribusi.

Jangan menjalankan APK atau binary tidak dikenal selama analisis source. Analisis APK sebaiknya statis. Jangan menambahkan token server, API key, private key, atau credential ke source, `gradle.properties`, resource, log, atau commit.

## Git workflow

Sebelum bekerja, periksa `git status`, branch aktif, dan remote. Jangan menghapus remote source. Buat branch feature untuk perubahan nontrivial. Commit hanya file yang memang diubah; file font tertentu dapat tampak modified karena line-ending/`.gitattributes` walaupun byte-nya sama dengan `HEAD`, sehingga verifikasi diff sebelum staging.

Sebelum commit, jalankan pemeriksaan berikut dari root repository:

```bash
git status --short
git diff --check
git diff --stat
```

Jangan commit hasil generated seperti `app/assets`, `build`, `Debug.win32`, `__BIN__`, Gradle output, object file, atau cache. Push ke remote hanya setelah diminta atau setelah perubahan memang disetujui pengguna.

## Checklist sebelum menyelesaikan task

Pastikan perubahan berada di source-of-truth yang benar, callback Lua–C++ memakai key terpusat, nama resource dan case path cocok, ownership object aman, build target yang relevan berhasil, smoke test battle dilakukan, `git diff` tidak berisi perubahan tak disengaja, dan tidak ada secret baru yang masuk ke commit. Untuk perubahan multiplayer, tambahkan catatan protokol, tick/seed, state yang disinkronkan, reconnect/timeout, strategi deteksi desync, serta pastikan lifecycle opt-in tidak membebani mode offline. Jalankan test LAN dan verifikasi bahwa worker tetap bebas akses Cocos2d-x.
