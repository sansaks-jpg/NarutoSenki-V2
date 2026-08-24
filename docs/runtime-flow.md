# Runtime Flow dan Scene Lifecycle

## Alur startup

```text
Platform main
  -> AppDelegate::applicationDidFinishLaunching
      -> GLView + Director
      -> search path Resources/Lua
      -> LuaEngine / ScriptEngineManager
      -> Internal::initAllSystems
      -> execute lua/main.lua
          -> GameScene.lua
              -> StartMenu C++
```

`AppDelegate.cpp` adalah entry point lintas platform. Ia menginisialisasi director, view, search path, Lua engine, audio/system, lalu mengeksekusi `main.lua`. `main.lua` memuat framework dan scene; `GameScene.lua` membuat scene awal dan menghubungkan callback global yang dipanggil C++.[1]

## Scene graph tingkat tinggi

| Scene/layer | Dibuat oleh | Masuk dari | Keluar ke |
|---|---|---|---|
| `GameScene` | `lua/main.lua` | AppDelegate | `StartMenu` |
| `StartMenu` | `GameScene` atau `onGameOver()` | Startup/game over/Back dari Network | `GameModeLayer`, `NetworkLobbyLayer`, `CreditsLayer`, exit, atau no-op untuk News/Login |
| `GameModeLayer` | `StartMenu::onTrainingCallBack()` | Training | `SelectLayer` melalui callback Lua atau StartMenu melalui Return |
| `NetworkLobbyLayer` | `StartMenu::onCustomCallBack()` | Network pada StartMenu | Network Home, Host/Join lobby, atau kembali ke StartMenu |
| `SelectLayer` | `enterSelectLayer()` Lua | Mode terpilih | `SkillLayer`, `LoadLayer`, atau StartMenu |
| `SkillLayer` | `SelectLayer:onSkillMenuButtonClick()` | Skill | `SelectLayer` melalui `popScene()` |
| `LoadLayer` | `SelectLayer::onGameStart()` | Start | `GameLayer` setelah preload/init |
| `GameLayer` | Load/runtime | Loading | `PauseLayer`, `GearLayer`, `GameOver` |
| `PauseLayer` | `GameLayer::onPause()` | Pause icon | GameLayer melalui Resume atau surrender/game over |
| `GearLayer` | `GameLayer::onGear()` | Avatar/gear | GameLayer melalui Close |
| `GameOver` | Battle runtime | Match selesai/surrender | StartMenu melalui callback Lua |
| `CreditsLayer` | `StartMenu` | Credits | StartMenu melalui callback Lua |

## Mode selection sequence

1. Pemain menekan Training pada StartMenu.
2. `StartMenu::onTrainingCallBack()` membuat scene baru dan menambahkan `GameModeLayer`.
3. `GameModeLayer::initModeData()` mengisi label dan status lock mode.
4. `ModeMenuButton::ccTouchEnded()` memanggil `GameModeLayer::selectMode(mode)`.
5. Klik pertama menandai mode dan memperbarui label; mode yang sama perlu dikonfirmasi lagi agar `setSelect()` mengembalikan true.
6. C++ memanggil global Lua `enterSelectLayer(mode, enableCustomSelect)`.
7. Lua memuat atlas select/battle, menyimpan `_G.mode`, membuat `SelectLayer`, dan melakukan fade transition.

Jalur Network tidak melewati pemilihan mode offline. `StartMenu::onCustomCallBack()` membuka `NetworkLobbyLayer` secara langsung. Network Home hanya merender Host/Join/Back; socket, worker UDP, discovery, dan polling belum aktif sampai pemain memilih Host atau Join.

## Character selection sequence

`SelectLayer:init()` menentukan apakah mode 1v1/Clone, 3v3, atau 4v4. Ia memuat tiga page portrait, membuat `SelectButton` untuk setiap karakter pada `ns.CharactersLayout`, dan menampilkan hero pertama. `SelectButton:click()` memakai dua tahap: tap pertama preview/voice, tap kedua mengunci karakter. Pada mode custom, tahap berikutnya mengisi slot COM satu per satu.

Tombol Skill mendorong `SkillLayer` di atas select scene. Tombol Return melakukan `popScene()`. Tombol Start memanggil `SelectLayer:onGameStart()`, menonaktifkan input, menjalankan `gameModeHandler:onInitHeros()`, melakukan preload audio, dan mengganti scene ke `LoadLayer`.

## Network lobby dan battle initialization

`NetworkLobbyLayer` memiliki tiga halaman: Network Home, Host, dan Join. Host mengaktifkan advertising discovery serta transport gameplay; Join mengaktifkan scanner discovery, sedangkan manual `IP:port` menjadi fallback jika broadcast hotspot tidak tersedia. Setelah handshake, lobby, hero selection, ready, dan loaded barrier selesai, `LanSession` memasuki state Battle dan layer berpindah ke `LoadLayer` dengan konfigurasi 1v1 deterministik.

## Battle initialization

`LoadLayer` memuat resource dan audio. Setelah siap, `GameLayer` membuat `BattleRuntimeSystem`, mode handler, unit/tower/flog, map, dan `HudLayer`. `HudLayer::initHeroInterface()` membuat joystick pada mobile, action button, item, gear, pause, minimap, dan status display. Pada battle LAN, `GameLayer::updateNetworkBattle()` menjalankan fixed tick, mengirim input command, dan menerapkan snapshot host. Pada mode offline, `_networkBattle` false sehingga jalur LAN dilewati seluruhnya.

`GameLayer::onGameStart(float)` kemudian menjalankan battle runtime serta callback `handler->onGameStart()`. `LanSession::poll()` hanya dijalankan oleh main thread melalui callback update ketika transport/discovery aktif.

## Input dan dispatch

```text
Touch/keyboard
  -> JoyStick / ActionButton / MenuItem
      -> HudLayer
          -> GameLayer::attackButtonClick(type)
          -> GameLayer::gearButtonClick(type)
          -> GameLayer::onGear()
          -> GameLayer::onPause()
          -> movement delegate
              -> CommandSystem / CharacterBase / Hero
```

`ActionButton` menolak input saat `_isAllButtonLocked`, cooldown belum selesai, atau syarat skill/Ougi belum terpenuhi. Input release juga diteruskan melalui `attackButtonRelease()`. MiniIcon bukan input control; ia menerima notifikasi `updateMap` untuk mengubah posisi/visibility.

## Pause, gear, dan cleanup

Pause dan Gear adalah overlay scene dengan screenshot battle sebagai background. Pause menghentikan audio/effect, sedangkan Gear menampilkan shop dan mengubah state gear player. Resume/Close melakukan `popScene()` dan mengembalikan flag pause/gear. Surrender menandai `_isSurrender`; runtime kemudian mengarahkan ke GameOver.

## Game over

GameOver menghitung report, reward, coin, record, dan win/death. Pada battle LAN, session dihentikan saat GameOver pertama kali ditampilkan agar worker/socket tidak terbawa ke scene berikutnya. Guard idempotensi mencegah GameOver ganda dan `popScene()` ganda. Tombol close memanggil mode handler cleanup lalu callback Lua `onGameOver()`. Lua membuat ulang `StartMenu`, mendaftarkan init handler, dan melakukan fade transition. Semua perubahan pada pointer `selectLayer`, observer, schedule, dan audio harus diuji saat alur ini dijalankan berulang kali.

## Referensi

[1]: ../projects/NarutoSenki/Classes/AppDelegate.cpp "Application entry point"
[2]: ../projects/NarutoSenki/lua/main.lua "Lua bootstrap"
[3]: ../projects/NarutoSenki/lua/ui/GameScene.lua "Initial Lua scene"
[4]: ../projects/NarutoSenki/lua/ui/StartMenu.lua "Lua scene callbacks"
[5]: ../projects/NarutoSenki/Classes/Systems/BattleRuntimeSystem.hpp "Battle runtime"
