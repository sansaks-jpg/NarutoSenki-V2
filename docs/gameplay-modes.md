# Gameplay Modes dan Match Rules

## Sumber kebenaran mode

`GameMode` dan `IGameModeHandler` berada di `projects/NarutoSenki/Classes/GameMode`. `StartMenu` hanya memilih jalur; handler mode yang menginisialisasi roster, map, tower, flog, gear, reborn, dan aturan akhir match. Jangan menambahkan mode hanya dengan menambah tombol gambar di `GameModeLayer`.

## Mode yang terdaftar

| Enum | Handler | Label | Catatan |
|---|---|---|---|
| `OneVsOne` | `Mode1v1` | 1 VS 1 | Tidak memakai custom COM select. |
| `Classic` | `Mode3v3` | 3 VS 3 / Classic | Mode regular dengan roster 3v3. |
| `FourVsFour` | `Mode4v4` | 4 VS 4 | Custom select dapat menyediakan tiga COM. |
| `HardCore_4Vs4` | `ModeHardCore` | Hardcore 4 VS 4 | Gear dinonaktifkan oleh rule mode. |
| `Boss` | `ModeBoss` | Boss | Terkunci/in development pada `GameModeLayer`. |
| `Clone` | `ModeClone` | Clone 3 VS 3 | Custom select dipaksa nonaktif. |
| `Deathmatch` | `ModeDeathmatch` | Deathmatch | Terkunci/in development pada source saat ini. |
| `RandomDeathmatch` | `ModeRandomDeathmatch` | Random Deathmatch | Roster/random deathmatch; status dapat bergantung konfigurasi. |

Enum dan jumlah mode menggunakan `GameMode::__Internal_Max_Length`; jika menambah enum, perbarui semua array/handler yang mengandalkan urutan indeks.

## Lifecycle handler

Handler mengikuti lifecycle yang kira-kira terdiri dari `init`, `onInitHeros`, `onGameStart`, `onGameOver`, dan cleanup. `init` menyiapkan data mode; `onInitHeros` membuat roster player/COM; `onGameStart` mengatur hal khusus saat battle dimulai; `onGameOver` menyimpan/mereset state. Detail method harus dibaca dari interface aktual sebelum implementasi baru.

`GameData` menyimpan konfigurasi match seperti map, group, tower/flog, randomization, enable gear, reborn, dan opsi mode. `HeroData` menyimpan nama hero, role, group, AI/player ownership, spawn, dan pilihan tambahan. `GameLayer` mengonsumsi data itu ketika membuat world.

## Roster dan team selection

`IGameModeHandler::initHeros(...)` mengisi hero data vector. Pemain dapat memilih hero; COM diisi sesuai mode dan flag `enableCustomSelect`. Jika tidak ada pilihan valid, source memiliki fallback random selection. Untuk 1v1 dan Clone custom select dinonaktifkan; untuk mode 3v3/4v4 slot COM ditampilkan sesuai jumlah team member.

Perubahan roster harus memeriksa hal berikut: nama factory `Provider`, resource hero, group Konoha/Akatsuki, role Player/COM/Guardian/Clone, spawn point, master/controller summon, serta aturan dead/reborn. Jangan memakai string bebas yang tidak terdaftar di `Provider`.

## Match runtime

Selama battle, `GameLayer` mengelola hero, guardian, summon, bullet, tower, flog, map, timer, collision, damage, kill/death, coin, CKR, dan win condition. `BattleRuntimeSystem` membantu update game time/viewpoint dan start runtime. `SpawnSystem` dan mode handler menentukan kapan unit masuk arena. `CommandSystem` menerima command dari input/HUD.

## Menambah mode baru

1. Tambahkan enum mode dan pastikan ukuran array internal ikut berubah.
2. Buat handler di `Classes/GameMode/Impl` yang mengimplementasikan seluruh interface.
3. Tambahkan handler ke `s_ModeHandlers` di `StartMenu.cpp` atau registry setara.
4. Tambahkan label/deskripsi dan status lock di `GameModeLayer::initModeData()`.
5. Tambahkan sprite `Resources/GameMode` jika UI memakai atlas/file baru.
6. Tentukan map, roster, team, tower, flog, gear, reborn, random seed, dan win condition.
7. Pastikan SelectLayer memahami jumlah player/COM dan custom select.
8. Jalankan smoke test match dari StartMenu sampai GameOver.

## Mengubah aturan yang ada

Perubahan pada damage, cooldown, spawn, atau win condition sebaiknya tidak diletakkan di UI. Gunakan handler/rule atau system yang memiliki tanggung jawab jelas. Catat mode yang terdampak, karena `CharacterBase` dan `Hero` dipakai lintas mode. Untuk random mode, simpan seed ketika debugging agar hasil dapat direproduksi.

## Extension multiplayer

Mode online sebaiknya dibuat sebagai handler/rule baru atau online variant terpisah, bukan mengubah `Classic` secara diam-diam. State match harus memiliki match id, player id, tick, seed, roster lock, input command, snapshot, dan result. Server harus memvalidasi damage, cooldown, resource, spawn, dan win condition.

## Referensi

[1]: ../projects/NarutoSenki/Classes/GameMode/IGameModeHandler.hpp "Game mode interface"
[2]: ../projects/NarutoSenki/Classes/GameMode/Impl "Mode implementations"
[3]: ../projects/NarutoSenki/Classes/GameLayer.cpp "Battle layer"
[4]: ../projects/NarutoSenki/Classes/StartMenu.cpp "Mode registry and menu"
