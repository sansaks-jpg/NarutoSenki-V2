# Workflow UI NarutoSenki-V2

## Ringkasan alur utama

```text
GameScene.lua
  -> StartMenu C++
      -> Training / menu02
          -> GameModeLayer C++
              -> pilih mode (klik pertama)
              -> klik mode yang sama lagi
                  -> SelectLayer.lua
                      -> pilih page karakter
                      -> pilih hero / COM
                      -> SkillLayer.lua (opsional)
                      -> Ranking (saat ini ComingSoon)
                      -> Start
                          -> LoadLayer
                              -> GameLayer battle
                                  -> Pause / Gear / Action HUD
                                      -> GameOver
                                          -> StartMenu.lua
      -> Network / menu01
          -> NetworkLobbyLayer C++
              -> Role Home (HOST / JOIN)
                  -> HOST -> Lobby 1v1 (POV Mirroring)
                  -> JOIN -> Discovery scanner & manual IP input
              -> CHANGE HERO -> SelectLayer Grid (35 hero avatars)
                  -> 1x tap -> preview half-portrait & Kanji logo
                  -> 2x tap (double-click) / tap OK -> commit & return to Lobby
              -> READY -> START MATCH (Host only when both ready)
              -> LoadLayer -> GameLayer LAN Battle (Full Sync)
              -> GameOver / Leave / Disconnect -> StartMenu
      -> Credits
          -> CreditsLayer
              -> Return
      -> Exit
      -> News (handler saat ini kosong)
      -> Login (menampilkan ServerMainten)
```

## 1. Startup dan menu utama

`lua/ui/GameScene.lua` membuat scene awal, kemudian callback/init C++ membangun `StartMenu`. `StartMenu.cpp` menampilkan background, title, notice berjalan, versi, avatar animasi, dan menu vertikal. Menu vertikal dapat di-scroll dengan drag vertikal; item yang berada di posisi teratas menjadi item aktif dan label deskripsinya berubah.

| UI | File/handler | Perilaku |
|---|---|---|
| Training / `menu02.png` | `MenuButtonType::Training` -> `StartMenu::onTrainingCallBack()` | Membuka `GameModeLayer`. Ini adalah jalur masuk utama untuk memulai match. |
| Network / `menu01.png` | `MenuButtonType::Custom` -> `NetworkLobbyLayer` | Membuka Network Home. Belum ada socket atau polling ketika hanya halaman Home dibuka; pemain harus memilih Host atau Join untuk mengaktifkan LAN. |
| Credits / `menu04.png` | `StartMenu::onCreditsCallBack()` | Membuka `CreditsLayer`. Di source item ini dibuat `visible=false`, tetapi dapat muncul melalui scroll state bila logika menu mengaktifkannya. |
| Exit / `menu03.png` | `MenuButtonType::Exit` -> `onExitCallBack()` | Desktop memanggil `Director::end()` dan `exit(0)`. Android memakai jalur back yang saat ini sebagian besar dikomentari. |
| News | `StartMenu::onNewsBtn()` | Tombol ada di pojok atas; implementasi Android/webview dikomentari, sehingga secara praktis tidak melakukan aksi. |
| Login | `StartMenu::onLoginBtn()` | Tidak membuka login; menampilkan tips `ServerMainten`. |
| Notice | `StartMenu::setNotice()` | Teks berjalan dari `Config/strings.xml`; informatif, bukan tombol. |

Menu utama memiliki dialog konfirmasi Hardcore yang tersedia lewat `onHardLayerCallBack()`: tombol Yes menghapus dialog dan tombol No membatalkan dialog. Pada implementasi menu saat ini, item Hardcore tidak dibuat sebagai item utama yang aktif dalam daftar `menuArray`; mode Hardcore lebih jelas tersedia di `GameModeLayer`.

## 3. Pemilihan mode

`StartMenu::onTrainingCallBack()` membuat scene baru berisi `GameModeLayer`. `GameModeLayer::init()` membuat tombol mode dari `GameMode/1.png` sampai `GameMode/8.png`, memberi setiap tombol enum `GameMode`, lalu mengunci mode yang memiliki `isLocked=true` dengan mask rantai.

Mode yang terdaftar adalah:

| Enum | Label | Status dari source |
|---|---|---|
| `OneVsOne` | `1 VS 1` | Tersedia. |
| `Classic` | `3 VS 3` / Classic Mode | Tersedia. |
| `FourVsFour` | `4 VS 4` | Tersedia. |
| `HardCore_4Vs4` | `HardCore (4 VS 4)` | Terdaftar; aturan gear dinonaktifkan. |
| `Boss` | `Boss (3 VS 3)` | Terkunci / in development pada `initModeData()`. |
| `Clone` | `Clone (3 VS 3)` | Tersedia jika tidak dikunci oleh build/config. |
| `Deathmatch` | `Deathmatch (3 VS 3)` | Terkunci / in development dan memakai mask khusus. |
| `RandomDeathmatch` | `Random Deathmatch (3 VS 3)` | Terdaftar; status bergantung konfigurasi mode. |

Perilaku `ModeMenuButton` penting: klik pertama pada mode memanggil `selectMode(mode)`, menampilkan judul/deskripsi dan menandai pilihan. Mode yang sama perlu diklik lagi setelah `setSelect()` mengembalikan status terpilih agar transisi ke select layer terjadi. Setelah mode terkonfirmasi, C++ memanggil callback Lua `StartMenu.enterSelectLayer(gameMode, enableCustomSelect)`.

Tombol return `UI/return_btn.png` selalu tersedia di layar mode dan memanggil `GameModeLayer::backToMenu()`, yang membuat ulang `StartMenu`.

## 4. Character selection

`lua/ui/StartMenu.lua::enterSelectLayer()` memuat sprite atlas Select/UI/Report/Ougis/Map/Gears, menyimpan `_G.mode` dan `_G.enableCustomSelect`, lalu membuat `SelectLayer`. `SelectLayer.lua` membangun tiga page karakter. Daftar karakter berasal dari `ns.CharactersLayout`; portrait disusun dalam grid tujuh kolom dan tiga row per page.

| UI | Perilaku |
|---|---|
| Page 1, 2, 3 | Klik tab page menampilkan page terkait melalui `onPageButtonClick(index)` dan menyembunyikan page lain. |
| Portrait hero | Klik pertama menampilkan half image, nama, suara intro, dan marker `1P`. Klik kedua mengunci pilihan hero; portrait digrayscale dan hero disimpan sebagai player selection. Mekanisme ini berada di `SelectButton.lua`. |
| Karakter unsupported/limited | Menampilkan tip `LimitedChar`; tidak mengunci pilihan. `None2` juga dianggap limited. |
| COM selector 1/2/3 | Hanya muncul jika `enableCustomSelect` aktif. Untuk mode 3v3 tersedia dua slot COM; untuk 4v4 tersedia tiga slot COM. Setiap slot dipilih dengan pola klik hero dua kali. |
| Ranking | Memanggil `onRankingButtonClick()`, tetapi handler saat ini hanya menampilkan `ComingSoon`. |
| Skill | Membuka `SkillLayer` sebagai scene yang di-push di atas select layer. |
| Start | Memanggil `SelectLayer:onGameStart()`, mengunci input, menyiapkan hero melalui `gameModeHandler:onInitHeros()`, preload audio, lalu membuka `LoadLayer`. |
| Return | Pada desktop terdapat tombol return langsung; tombol/back Android memanggil Lua `backToStartMenu()`. |

Mode `Clone` dan `OneVsOne` memaksa `enableCustomSelect=false`. Mode `Classic` dan `RandomDeathmatch` diperlakukan sebagai 3v3; `FourVsFour` dan `HardCore_4Vs4` diperlakukan sebagai 4v4. Karena itu, jumlah slot COM dan kapan selection dikunci tergantung mode.

## 5. Skill screen

`SkillLayer.lua` dibuka dari tombol Skill di character selection. Layar ini menampilkan hero besar, rank berdasarkan win/bond record, coin, bond progress, best time, dan lima slot skill.

| UI | Perilaku |
|---|---|
| Tombol Return | `SkillLayer:onCancel()` menghapus layer dan melakukan `director.popScene()`, kembali ke character selection. |
| Skill 1–3 | Klik menampilkan panel penjelasan skill hero. |
| Ougi 1–2 / slot 4–5 | Klik menampilkan penjelasan atau label unlock terkait. Status unlock/beli ditentukan oleh field `SelectButton.Type`. |
| Change/evolution | Jika hero memiliki transform mapping, tombol berubah dapat muncul dan mengganti tampilan/skill ke bentuk lanjutan, misalnya Naruto -> SageNaruto -> RikudoNaruto, Jiraiya -> SageJiraiya, Sasuke -> ImmortalSasuke, dan Pain -> Nagato. |
| Rank/best time/coin | Informasi display; tidak menjadi tombol navigasi utama. |

`SkillLayer` tidak memulai battle. Setelah kembali, tombol Start tetap berada di `SelectLayer`.

## 6. Load dan masuk battle

Klik Start di `SelectLayer` menjalankan `onGameStart()`. Fungsi ini memanggil handler mode untuk membuat roster, memuat/preload audio, lalu mengganti scene ke `LoadLayer`. Setelah loading selesai, `GameLayer` dibangun. `GameLayer` membuat `HudLayer`, unit, tower, flog, map, dan battle runtime sesuai handler mode.

## 7. Battle HUD yang dapat diklik

`HudLayer::initHeroInterface()` membuat kontrol berbeda untuk mobile dan desktop. Elemen status seperti HP, CKR/EXP, coin, kill/death, skor group, dan game clock hanya display.

| UI battle | Handler | Fungsi |
|---|---|---|
| Joystick | `JoyStick` touch delegate | Mengubah arah gerak player. Input diabaikan ketika semua tombol sedang terkunci. |
| Normal attack | `ActionButton` tipe `NAttack` | Memanggil `HudLayer::attackButtonClick()` -> `GameLayer::attackButtonClick(NAttack)`. Pada desktop normal attack disembunyikan dari posisi utama dan input keyboard dapat mengambil alih. |
| Skill 1–3 | `ActionButton` tipe `SKILL1`–`SKILL3` | Mengirim command skill ke GameLayer; cooldown/lock dan resource diperiksa oleh ActionButton/GameLayer. |
| Ougi 1 | `skill4Button`, tipe `OUGIS1` | Serangan khusus pertama; memiliki mark dan syarat CKR. |
| Ougi 2 | `skill5Button`, tipe `OUGIS2` | Serangan khusus kedua; memiliki mark dan syarat CKR2. |
| Item 1 | `item1Button`, tipe `Item1` | Item ramen/efek pemulihan sesuai gameplay. Memiliki cooldown dan cost. |
| Item 2–4 | `item2Button`–`item4Button`, tipe `GearItem` | Awalnya tersembunyi/terkunci. Dibuka oleh gear tertentu melalui `updateGears()`. |
| Gear/avatar | `gearMenuSprite` -> `HudLayer::gearButtonClick(Ref*)` | Membuka shop equipment `GearLayer`. |
| Gear slot 1–3 | `gear1Button`–`gear3Button`, tipe `GearBtn` | Mengaktifkan gear yang sudah dibeli, tunduk pada cooldown/lock. |
| Ikon pause/minimap | Sprite `minimap_bg.png` -> `pauseButtonClick()` | Secara visual berada di area minimap, tetapi kliknya membuka `PauseLayer`. |
| Ikon unit/tower di minimap | `MiniIcon` | Display-only. Ikon memperbarui posisi/visibility melalui notifikasi `updateMap`; tidak memiliki touch handler. |

`ActionButton` menerapkan hitbox touch, lock global, cooldown, double-click/ougi handling, freeze mask, progress mark, dan dispatch ke delegate `HudLayer`. Untuk menambah kontrol baru, jangan langsung mengubah `GameLayer` dari UI; tambahkan tipe command/ABType, buat button, hubungkan delegate, lalu implementasikan perilakunya di GameLayer.

## 8. Gear/shop overlay

Klik avatar/gear membuka `GearLayer` sebagai scene overlay dengan screenshot battle di belakang dan blend gelap.

| UI | Perilaku |
|---|---|
| Daftar sembilan gear | Scroll melalui `ScrewLayer`; gear yang sudah dibeli diberi `gear_so.png` dan tidak dapat dipilih kembali sebagai pembelian. |
| Gear detail/icon besar | Klik gear yang belum dibeli mengubah `currentGear` dan menampilkan detail/icon. |
| Buy | Memanggil `currentPlayer->setGear(currentGear)`. Jika berhasil, daftar gear dan gear slot HUD diperbarui. |
| Gear yang dimiliki | Ditampilkan pada row gear milik player; kliknya hanya mengubah detail, bukan menjual, karena handler menolak item yang sudah memiliki `soIcon`. |
| Close | `GearLayer::onResume()` memperbarui HUD lalu `popScene()`. |

Gear tertentu membuka item HUD tambahan: `Gear06` membuka item2, `Gear00` membuka item3, dan `Gear03` membuka item4.

## 9. Pause overlay

Klik ikon pause/minimap membuat `PauseLayer`. Battle di belakang dirender sebagai screenshot; effect dan background music dipause.

| UI | Perilaku |
|---|---|
| Resume | Resume BGM/effect sesuai setting, `popScene()`, dan mengubah `_isPause=false`. |
| Back to menu | Membuka dialog konfirmasi menyerah. |
| BGM toggle | Mengubah `UserDefault::isBGM` dan tampilan on/off. |
| Voice toggle | Mengubah `UserDefault::isVoice` dan tampilan on/off. |
| Preload toggle | Mengubah `UserDefault::isPreload` dan tampilan on/off. |
| Yes pada dialog surrender | Menandai `_isSurrender=true`, menutup pause scene, dan melanjutkan flow game over. |
| No pada dialog surrender | Menutup dialog dan menampilkan kembali menu pause. |

## 10. Game over dan kembali ke menu

`GameOver` menampilkan report match, slain/death, hasil, reward/record, dan tombol close. Tombol upload masih dikomentari/tidak aktif. Tombol close memanggil `onBackToMenu()`, menjalankan cleanup mode dan callback Lua `onGameOver()`. `lua/ui/StartMenu.lua::onGameOver()` membuat ulang scene `StartMenu`.

## 11. Titik modifikasi UI yang paling penting

Untuk mengubah label, sprite, atau susunan menu, mulai dari `projects/NarutoSenki/lua/ui` dan `Resources/UI`/atlas terkait. Untuk mengubah scene transition, periksa `StartMenu.cpp`, `GameModeLayer.cpp`, `SelectLayer.lua`, dan `lua/ui/StartMenu.lua`. Untuk menambah pilihan mode, ubah enum/data, implementasi handler, wiring `GameModeLayer`, serta resource `GameMode/*.png`. Untuk menambah tombol gameplay, periksa `HudLayer.cpp`, `ActionButton.cpp`, `HudLayer.h`, dan dispatch GameLayer.

Untuk memperluas mode LAN, mulai dari `NetworkLobbyLayer`, `LanSession`, `LanProtocol`, dan bridge `GameLayer`. UI hanya mengirim intent Host/Join/ready/start; state authoritative tetap berada pada session dan GameLayer. Jangan mengaktifkan jaringan dari Training atau mode offline.

## Referensi

[1]: ../projects/NarutoSenki/Classes/StartMenu.cpp "Start menu and menu button callbacks"
[2]: ../projects/NarutoSenki/Classes/UI/GameModeLayer.cpp "Game mode selection"
[3]: ../projects/NarutoSenki/lua/ui/StartMenu.lua "Lua scene transition callbacks"
[4]: ../projects/NarutoSenki/lua/ui/SelectLayer.lua "Character selection workflow"
[5]: ../projects/NarutoSenki/lua/ui/SelectButton.lua "Character selection tap behavior"
[6]: ../projects/NarutoSenki/lua/ui/SkillLayer.lua "Skill screen"
[7]: ../projects/NarutoSenki/Classes/HudLayer.cpp "Battle HUD and minimap"
[8]: ../projects/NarutoSenki/Classes/PauseLayer.cpp "Pause overlay"
[9]: ../projects/NarutoSenki/Classes/GearLayer.cpp "Gear shop overlay"
[10]: ../projects/NarutoSenki/Classes/GameOver.cpp "Game over flow"

Fakta workflow pada dokumen ini diringkas dari [StartMenu.cpp][1], [GameModeLayer.cpp][2], [StartMenu.lua][3], [SelectLayer.lua][4], [SelectButton.lua][5], [SkillLayer.lua][6], [HudLayer.cpp][7], [PauseLayer.cpp][8], [GearLayer.cpp][9], dan [GameOver.cpp][10].
