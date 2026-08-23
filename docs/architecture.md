# Arsitektur Project

NarutoSenki-V2 adalah game 2D yang dibangun di atas **Cocos2d-x 2.2.6 yang dikustomisasi**, bukan project Unity atau Godot. Engine, Lua runtime, extensions, audio layer, dan beberapa dependency disimpan di repository; kode game sendiri berada di `projects/NarutoSenki`.[1]

## Layer arsitektur

| Layer | Lokasi | Tanggung jawab |
|---|---|---|
| Platform | `projects/NarutoSenki/proj.android-studio`, `proj.linux`, `proj.win32`, `proj.mac`, `proj.ios` | Entry point platform, window/GL view, packaging, Gradle/NDK/Xcode/Visual Studio. |
| Engine | `cocos2dx` | Director, Scene, Node, Sprite, Action, input, renderer, resource resolver, tilemap, dan platform abstraction. |
| Scripting | `scripting/lua`, `projects/NarutoSenki/lua` | LuaJIT, binding C++–Lua, bootstrap, scene/menu UI, utility, audio, dan helper. |
| Game runtime | `projects/NarutoSenki/Classes` | Battle simulation, character, skill, projectile, tower, flog, map, game mode, HUD, save, dan scene. |
| LAN networking | `projects/NarutoSenki/Classes/Network` | Protocol/frame validation, UDP transport, broadcast discovery, host/client session, lobby state, command/snapshot bridge, dan shared runtime. |
| Data/resource | `projects/NarutoSenki/Resources` | Atlas, sprite frame, audio `.ogg`, map, font, metadata, UI, dan konfigurasi. |
| Third-party | `external`, `extensions`, `CocosDenshion` | fmt, GLFW, SQLite3, TOML, GUI/CocoStudio, dan audio implementation. |

## Modul C++ game

`Classes/CharacterBase.*` adalah basis unit yang menyimpan state, HP/CKR, target, buff, collision, animation, damage, visibility, group, role, dan lifecycle. `Core/Hero.hpp` menambahkan perilaku player/AI, normal attack, skill, Ougi, gear, reborn, level, dan resource combat. Implementasi karakter per hero berada di `Core/Shinobi`; summon berada di `Core/Kuchiyose`, guardian di `Core/Guardian`, clone di `Core/Bunshin`, tower di `Core/Tower`, dan unit wave di `Core/Warrior`.

`Core/Provider.hpp` adalah factory string-to-class untuk membuat hero, clone, guardian, dan summon. `GameMode` menentukan aturan match, roster, map, team, tower, flog, randomisasi, gear, dan lifecycle. `Systems` menyediakan `CommandSystem`, `BattleRuntimeSystem`, `SpawnSystem`, `SessionState`, serta initializer. `GameLayer` menggabungkan world scene, unit array, mode handler, physics/collision sederhana, input, dan HUD.

### LAN networking boundary

`Classes/Network` terdiri atas empat tanggung jawab. `LanProtocol` melakukan serialisasi/deserialisasi frame, batas payload, validasi message type, protocol version, sequence, dan data match. `LanTransport` memiliki worker native UDP yang hanya menerima/mengirim data dan memasukkan event ke queue; worker tidak boleh memanggil Cocos2d-x. `LanDiscovery` menjalankan broadcast room advertisement. `LanSession` menjadi state machine host/client untuk handshake, lobby, ready/loading barrier, input, snapshot, heartbeat, timeout, dan cleanup.

`LanNetworkRuntime::sharedLanSession()` menyediakan satu session yang melewati transisi lobby ke battle. Session tersebut bersifat **opt-in**: constructor dan mode offline tidak membuat socket; transport/discovery baru dimulai oleh `host()`, `join()`, atau `startScan()`. `NetworkLobbyLayer` dan `GameLayer` memanggil `poll()` hanya di main thread, sedangkan `GameLayer` tetap menjadi pemilik simulation dan presentation.

| Modul | Kontrak penting |
|---|---|
| `GameLayer` | Orchestrator battle; menerima command dari HUD/input dan menggerakkan simulation. |
| `HudLayer` | View/controller input battle; meneruskan attack, gear, pause, dan joystick ke `GameLayer`. |
| `CharacterBase` | State dan perilaku umum semua unit. |
| `Hero` | Perilaku hero, skill, gear, AI/player control, dan resource. |
| `IGameModeHandler` | Interface mode: init, roster, game start, game over, map, tower, flog, dan aturan khusus. |
| `Provider` | Registrasi/factory karakter berdasarkan string. |
| `Parser` | Membaca metadata unit TOML menjadi `UnitMetadata` dan action data. |
| `KTools` | SQLite save/record, coin, win count, dan best time. |
| `Cocos2dxHelper` | Sprite helper, format compatibility, dan bridge callback Lua. |
| `Classes/Network` | UDP transport/discovery, protocol, session state machine, dan bridge command/snapshot. |

## Layer Lua

`lua/main.lua` adalah bootstrap script. Ia memuat konfigurasi, framework, core, utility, dan UI. `lua/ui/GameScene.lua` mengatur scene awal lalu memulai `StartMenu`. `lua/ui/StartMenu.lua` menyediakan callback global untuk masuk character selection dan kembali dari game over. `lua/ui/SelectLayer.lua` membangun character selection; `SelectButton.lua` menangani tap dan lock; `SkillLayer.lua` menampilkan skill/rank/transform. Lua dipakai terutama untuk bootstrap dan UI yang tidak perlu berada di C++.

Komunikasi C++–Lua dipusatkan pada callback string di `Classes/Constants/UiFlowKeys.hpp` dan helper di `Classes/Utils/Cocos2dxHelper.hpp`. Callback utama adalah `enterSelectLayer`, `onGameOver`, `backToStartMenu`, dan callback credits. Jangan menulis string callback baru tersebar di banyak file.

## Ownership dan lifecycle

Node dan sprite mengikuti ownership/ref-counting Cocos2d-x: objek yang dibuat dengan `create()` biasanya autorelease dan dimiliki scene/node parent. Beberapa system internal menggunakan `std::unique_ptr`, tetapi itu tidak berarti semua node Cocos2d-x boleh dikelola smart pointer. Scene transition memakai `Director::replaceScene(...)`, sedangkan overlay Skill, Pause, dan Gear memakai `pushScene`/`popScene`.

Perubahan lifecycle harus diuji pada tiga titik: masuk scene, keluar scene, dan kembali ke menu setelah battle. Pastikan observer notification, touch delegate, schedule, audio, dan pointer ke `GameLayer` dilepas atau dipulihkan.

## Dependency boundary

Engine/vendor sebaiknya dianggap read-only untuk perubahan fitur game. Jika API engine kurang, tambahkan adapter kecil di `Classes/Utils` atau layer game. Perubahan pada `cocos2dx`, LuaJIT, extensions, atau prebuilt library harus memiliki alasan lintas platform dan diverifikasi pada target yang terdampak.

Untuk LAN, dependency boundary lebih ketat: kode worker transport harus bebas dari `Director`, `Node`, `Sprite`, scheduler, dan callback UI. Event jaringan diproses melalui `LanSession::poll()` pada callback `update(dt)` main thread. Mode offline tidak boleh memanggil polling LAN atau membuat socket.

## Referensi

[1]: ../Doc/README.md "NarutoSenki-V2 legacy documentation"
[2]: ../projects/NarutoSenki/Classes "Game C++ source"
[3]: ../projects/NarutoSenki/lua "Game Lua source"
[4]: ../projects/NarutoSenki/Classes/Systems "Runtime systems"
[5]: ../projects/NarutoSenki/Classes/GameMode "Game mode handlers"
