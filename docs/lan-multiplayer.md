# LAN Multiplayer MVP

## Status dan ruang lingkup

Branch `feature/lan-hotspot-multiplayer` menyediakan multiplayer **LAN/hotspot 1v1 host-authoritative**. Tombol `Network` pada StartMenu membuka `NetworkLobbyLayer`, lalu pemain dapat memilih `HOST ROOM` atau `JOIN ROOM`. Fitur ini ditujukan untuk dua perangkat pada jaringan lokal atau hotspot yang sama; fitur ini bukan internet matchmaking dan tidak memakai backend server.

Mode offline dan Training tetap terpisah dari LAN. Session, UDP socket, worker, discovery, dan polling baru diaktifkan setelah pemain memilih Host atau Join. Membuka Network Home saja tidak mengaktifkan jaringan. Setelah Leave, Back, GameOver, timeout, atau scene keluar, session harus dihentikan dan socket/worker harus dilepas.

## Alur pengguna

| Tahap | Host | Client |
|---|---|---|
| 1. Masuk Network | Membuka Network Home (`SELECT NETWORK ROLE`). | Membuka Network Home (`SELECT NETWORK ROLE`). |
| 2. Aktifkan LAN | Memilih `HOST`; advertising discovery dan gameplay UDP dimulai. | Memilih `JOIN`; scanner discovery dimulai. |
| 3. Temukan peer | Menunggu client pada room. | Memilih room hasil discovery atau memasukkan `IP:port` manual. |
| 4. Lobby (POV Mirroring) | Melihat diri sendiri di sisi kiri (1P Blue), lawan di kanan (2P Red). Tekan `CHANGE HERO` untuk membuka katalog hero, lalu `READY`, dan `START MATCH`. | Melihat diri sendiri di sisi kiri (1P Blue), lawan di kanan (2P Red). Tekan `CHANGE HERO` untuk membuka katalog hero, lalu `READY`, dan menunggu Host. |
| 5. Pilih Hero (`SelectLayer` Grid) | Memilih hero dari kisi 35 avatar ninja, pratinjau half-portrait + logo Kanji, konfirmasi via double-click atau tombol `OK`. | Memilih hero dari kisi 35 avatar ninja, pratinjau half-portrait + logo Kanji, konfirmasi via double-click atau tombol `OK`. |
| 6. Loading | Menunggu kedua perangkat mengirim loaded barrier. | Mengirim loaded barrier setelah resource match siap. |
| 7. Battle (Sync Penuh) | Menjalankan simulasi authoritative, memproses pergerakan analog + jurus (`SKILL1..3`, `OUGIS1..2`, `Item1`), dan menyebarkan snapshot. | Mengirim input joystick dan tombol jurus ke host, menerapkan snapshot host secara mulus. |
| 8. Keluar | Leave/Back/GameOver menghentikan session dan membersihkan worker. | Leave/Back/GameOver/timeout menghentikan session dan membersihkan worker. |

Hardware Back Android dan tombol Back memakai cleanup yang sama. Dari halaman HeroSelect kembali ke Lobby; dari Lobby/Join kembali ke Network Home; dari Network Home kembali ke StartMenu.

## Konfigurasi koneksi

MVP mendukung satu host dan satu client. Host memakai UDP gameplay port `28765`; discovery memakai UDP port `28766`. Discovery hanya membantu menemukan room dan tidak menjadi syarat koneksi. Jika broadcast diblokir oleh AP isolation atau konfigurasi hotspot, gunakan alamat manual, misalnya `192.168.43.1:28765`.

Konfigurasi match yang dikirim host menetapkan mode `GameMode::OneVsOne`, map `1`, tick rate default `30`, dua slot, seed, match id, gear/reborn, nama player, hero, dan ready state. Host memvalidasi pilihan client sebelum memasukkannya ke `MatchConfig`.

## Sudut Pandang Simetris (POV Mirroring) & UI

Lobby 1v1 mengadopsi sistem **POV Mirroring**:
- Di perangkat Host: Host berada di sisi **Kiri (1P Blue)**, Client berada di sisi **Kanan (2P Red)**.
- Di perangkat Client: Client berada di sisi **Kiri (1P Blue)**, Host berada di sisi **Kanan (2P Red)**.
- Tombol **CHANGE HERO** membuka layar pemilihan karakter penuh bergaya offline `SelectLayer` yang menampilkan seluruh 35 avatar ninja (`[Hero]_select.png`), bingkai seleksi emas (`Blink_select.png`), pratinjau potret setengah badan (`_half.png`), logo kaligrafi Kanji (`_font.png`), dan tombol `OK`. Pemain dapat mengonfirmasi pilihan dengan **Double-Click** pada avatar atau menekan tombol **OK**.

## Authority model dan lifecycle thread

Model authority **host-simulasi penuh** (protocol v2): hanya host yang memutuskan outcome gameplay. Client tidak pernah menghitung damage, mendorong state HURT/KNOCKDOWN, membunuh entity, menjalankan AI minion, atau menyimpulkan pemenang — semua itu diterima dari host. Yang disimulasikan client secara lokal hanyalah posisi hero miliknya (prediksi gerak), dan posisi tersebut dilaporkan ke host via `ClientState` sehingga host menyelesaikan combat dengan koordinat yang sama.

```text
Client                          Host (satu-satunya simulator outcome)
  input joystick  ── lokal ──>  posisi hero client
  tombol jurus    ── Input ──>  eksekusi attack authoritative di mirror hero client
  ClientState(10Hz) ─────────>  posisi/facing/state hero client
  Snapshot(15Hz)  <──────────   heroes + towers + guardian + flogs + clock + verdict
```

Aturan penekanan di kode:
- `CharacterBase::acceptAttack()` dan `setDamage()` no-op pada client (`isNetworkBattle() && !isNetworkHost()`).
- `HPBar::loseHP(percent <= 0)` pada client hanya mengosongkan bar; kill reward, spawn guardian, dan `dead()` hanya jalan di host.
- Death/revive kedua hero berasal dari transisi byte `state` pada snapshot; `deadLabel`, fade mati, dan revive mengikuti host sehingga counter identik.
- Flog/guardian: host mensimulasikan (`doAI`), client membuat mirror tanpa `doAI`; entity yang hilang dari snapshot di-`dead()`-kan lokal agar array bersih sama.
- `checkTower()`: client early-return; host mengirim `MatchEnd(winnerGroup)` sebelum `onGameOver`.

`LanTransport` memiliki worker native yang hanya menangani socket, encode/decode frame, dan queue event. Worker tidak boleh memanggil `Director`, `Node`, `Sprite`, scheduler, `GameLayer`, atau UI. `LanSession::poll()` menguras queue pada main thread melalui callback update. `LanNetworkRuntime::sharedLanSession()` mempertahankan session saat transisi lobby ke loading/battle.

Guard `networkActive()` memastikan `LanSession::poll()` dan `getRooms()` langsung dilewati ketika transport/discovery tidak aktif. Pada battle offline, `_networkBattle` false sehingga `GameLayer` tidak masuk ke update jaringan.

## Protocol dan validasi

Frame terdiri atas magic `NSV2`, protocol version (`2`), message type, payload length, sequence, tick, **ack** (piggyback cumulative ack input), dan payload. Payload dibatasi maksimum `64 KiB`, menggunakan integer little-endian, dan memakai string length-prefixed dengan batas panjang.

| Pesan | Arah | Tujuan |
|---|---|---|
| `Hello` | Client -> Host | Handshake awal dan nama client. |
| `JoinAccept` | Host -> Client | Menerima client dan mengirim konfigurasi awal. |
| `LobbyUpdate` | Host -> Client | Menyebarkan roster, hero, ready, map, seed, dan tick rate. |
| `SelectHero` | Client -> Host | Pilihan hero client. |
| `Ready` | Client -> Host | Ready/unready client. |
| `MatchStart` | Host -> Client | Mengunci match dan memulai loading. |
| `Loaded`/`Ack` | Dua arah | Loaded barrier sebelum battle. |
| `Input` | Client -> Host | Aksi diskrit (`NormalAttack`, `Skill1..5`, `Item1`) dengan sequence; diretransmit otomatis (~120 ms) sampai di-ack karena menentukan outcome. `Move` tidak lagi dikirim (posisi mengalir via Snapshot/ClientState). |
| `Snapshot` | Host -> Client | World authoritative: hero (posisi, HP, CKR, state, facing), units (tower/guardian/flog: id, kind, variant, posisi, HP, state), detik pertandingan. |
| `ClientState` | Client -> Host | State hero milik client (posisi/state/facing) ~10 Hz; divalidasi slot == 1. |
| `MatchEnd` | Host -> Client | Verdict pemenang (GroupId) agar kedua device keluar match dengan hasil sama. |
| `Heartbeat` | Dua arah | Memantau peer aktif; header ack tetap dipiggyback. |
| `Leave`/`Disconnect` | Dua arah | Cleanup dan akhir session. |

Payload melebihi `64 KiB`, message type tidak dikenal, protocol version mismatch, match id tidak cocok, player slot invalid, action invalid, unit kind invalid, dan snapshot lebih lama dari yang sudah diterapkan (`tick <= lastApplied`) harus ditolak sebelum mengubah state. Duplicate atau input out-of-order tidak boleh merusak state host; retransmit out-of-order tetap diterima (sorted insert per slot) agar packet yang hilang bisa mengisi ulang celahnya.

## Battle bridge & Sinkronisasi Animasi

`GameLayer::updateNetworkBattle()` menggunakan accumulator fixed-timestep di atas callback `update(dt)`. Render mengikuti frame rate perangkat, sedangkan tick network mengikuti `MatchConfig.tickRate`.

Pembagian kerja per entity:

- **Hero sendiri**: gerak disimulasikan lokal penuh (joystick → `walk`/`idle` langsung); tombol jurus dikirim via `Input` dan animasi jalan lewat echo command; HP/CKR/matinya murni dari snapshot.
- **Hero lawan**: posisi di-interpolasi (`kNetInterpPeriod` 120 ms) menuju posisi snapshot; byte `state` memicu transisi `WALK`/`IDLE`; aksi diskrit direlay untuk animasi; host menyelesaikan damage-nya.
- **Tower**: HP diperbarui dari unit snapshot (charId deterministik 1..N di kedua device).
- **Guardian** (hardcore): hanya host yang spawn (`initGard`, nama via PRNG ber-seed `MatchConfig.seed`); client membuat mirror tanpa AI dari unit `variant` (bit0 nama, bit1 group).
- **Flog/minion**: hanya host yang schedule & `doAI()`; tiap flog diberi id `300+` saat dibuat; client mirror dari snapshot (nama via tabel variant) dan mengeksekusi `dead()` ketika hilang dari snapshot. Tipe flog mengikuti kondisi tower versi host sehingga tidak pernah beda.
- **Jam pertandingan**: `elapsedSeconds` pada snapshot menulis ulang `gameClock`/`totalTime` client.

Seluruh perintah aksi tempur tetap didukung: `NormalAttack`, `Skill1..3`, `Skill4/5 (OUGIS)`, `Item1`. Di sisi host aksi client dieksekusi sebagai attack authoritative terhadap dunia host; di sisi client aksi host hanya visual karena semua penulisan outcome dicegah guard authority.

## Android, desktop, dan permission

Project Android menggunakan `compileSdkVersion 31`, `targetSdkVersion 31`, `minSdkVersion 21`, Gradle legacy, dan NDK r17c + Clang. Source Network didaftarkan pada `app/jni/Android.mk`; file tersebut harus diawali `LOCAL_PATH := $(call my-dir)`. Linux dan Windows juga mendaftarkan source network serta library UDP native masing-masing.

Branch ini memakai IPv4 UDP native dan tidak memakai Android NSD picker. Dengan target SDK saat ini, branch tidak menambahkan permission local-network khusus. Jika target SDK dinaikkan, tinjau kebijakan dan permission local network sebelum raw UDP dipakai. Manual IP harus tetap dipertahankan sebagai fallback discovery.

## Test procedure

Unit protocol dijalankan dengan:

```sh
g++ -std=c++20 -Wall -Wextra -Werror \
  -Iprojects/NarutoSenki/Classes \
  projects/NarutoSenki/Classes/Network/LanProtocol.cpp \
  tests/lan_protocol_test.cpp \
  -o /tmp/lan_protocol_test
/tmp/lan_protocol_test
```

Integration loopback dua session dijalankan dengan:

```sh
g++ -std=c++20 -Wall -Wextra -Werror -pthread \
  -Iprojects/NarutoSenki/Classes \
  projects/NarutoSenki/Classes/Network/LanProtocol.cpp \
  projects/NarutoSenki/Classes/Network/LanTransport.cpp \
  projects/NarutoSenki/Classes/Network/LanDiscovery.cpp \
  projects/NarutoSenki/Classes/Network/LanSession.cpp \
  tests/lan_session_test.cpp \
  -o /tmp/lan_session_test
/tmp/lan_session_test
```

Test protocol mencakup round-trip, payload >64 KiB, message type tidak dikenal, protocol version mismatch, invalid frame, dan verifikasi skill actions. Test session mencakup offline tanpa socket, host/client loopback, handshake, lobby, ready, loaded barrier, input jurus/gerak, snapshot, input out-of-order, disconnect, timeout, dan cleanup.

## Release branch fitur

APK rilis dibuat secara otomatis melalui workflow GitHub Actions `.github/workflows/release-apk.yml` pada branch fitur dan tag rilis. Release terbaru adalah [`v2.2.0-multiplayer`](https://github.com/sansaks-jpg/NarutoSenki-V2/releases/tag/v2.2.0-multiplayer). Asset release terdiri atas file `NarutoSenki-v2.2.0-multiplayer.apk` dan file checksum `.sha256`.

## Known limitations

MVP saat ini difokuskan pada mode 1v1 dua pemain. 3v3/4v4 multiplayer, reconnect otomatis saat terputus di tengah pertempuran, internet matchmaking di luar LAN, spectator mode, dan replay file direncanakan untuk iterasi selanjutnya. Batasan model v2: jika paket `MatchEnd` hilang, client tetap keluar match lewat jalur disconnect (dianggap menang); bullet/efek skill bersifat kosmetik di sisi client (outcome tetap dari host).

## Referensi

[1]: ../projects/NarutoSenki/Classes/Network/LanProtocol.hpp "LAN protocol types and limits"
[2]: ../projects/NarutoSenki/Classes/Network/LanTransport.cpp "Native UDP transport worker"
[3]: ../projects/NarutoSenki/Classes/Network/LanDiscovery.cpp "UDP discovery"
[4]: ../projects/NarutoSenki/Classes/Network/LanSession.cpp "Host/client session lifecycle"
[5]: ../projects/NarutoSenki/Classes/Network/NetworkLobbyLayer.cpp "LAN lobby UI, POV mirroring, and SelectLayer grid"
[6]: ../projects/NarutoSenki/Classes/GameLayer.cpp "Battle command, animation, and snapshot bridge"
[7]: ../tests/lan_protocol_test.cpp "Protocol tests"
[8]: ../tests/lan_session_test.cpp "Session loopback and offline tests"
[9]: ../.github/workflows/release-apk.yml "APK release workflow"
