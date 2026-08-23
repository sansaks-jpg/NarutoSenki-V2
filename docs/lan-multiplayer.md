# LAN Multiplayer MVP

## Status dan ruang lingkup

Branch `feature/lan-hotspot-multiplayer` menyediakan multiplayer **LAN/hotspot 1v1 host-authoritative**. Tombol `Network` pada StartMenu membuka `NetworkLobbyLayer`, lalu pemain dapat memilih `HOST ROOM` atau `JOIN ROOM`. Fitur ini ditujukan untuk dua perangkat pada jaringan lokal atau hotspot yang sama; fitur ini bukan internet matchmaking dan tidak memakai backend server.

Mode offline dan Training tetap terpisah dari LAN. Session, UDP socket, worker, discovery, dan polling baru diaktifkan setelah pemain memilih Host atau Join. Membuka Network Home saja tidak mengaktifkan jaringan. Setelah Leave, Back, GameOver, timeout, atau scene keluar, session harus dihentikan dan socket/worker harus dilepas.

## Alur pengguna

| Tahap | Host | Client |
|---|---|---|
| 1. Masuk Network | Membuka Network Home. | Membuka Network Home. |
| 2. Aktifkan LAN | Memilih `HOST ROOM`; advertising discovery dan gameplay UDP dimulai. | Memilih `JOIN ROOM`; scanner discovery dimulai. |
| 3. Temukan peer | Menunggu client pada room. | Memilih room hasil discovery atau memasukkan `IP:port` manual. |
| 4. Lobby | Melihat slot, menerima hero client, menekan `READY`, lalu `START MATCH`. | Memilih/ganti hero, menekan `READY`, lalu menunggu host. |
| 5. Loading | Menunggu kedua perangkat mengirim loaded. | Mengirim loaded setelah resource siap. |
| 6. Battle | Menjalankan simulation authoritative dan mengirim snapshot. | Mengirim input command dan menerapkan snapshot host. |
| 7. Keluar | Leave/Back/GameOver menghentikan session. | Leave/Back/GameOver/timeout menghentikan session. |

Hardware Back Android dan tombol Back memakai cleanup yang sama. Dari halaman Host/Join, Back kembali ke Network Home; dari Network Home, Back kembali ke StartMenu.

## Konfigurasi koneksi

MVP mendukung satu host dan satu client. Host memakai UDP gameplay port `28765`; discovery memakai UDP port `28766`. Discovery hanya membantu menemukan room dan tidak menjadi syarat koneksi. Jika broadcast diblokir oleh AP isolation atau konfigurasi hotspot, gunakan alamat manual, misalnya `192.168.43.1:28765`.

Konfigurasi match yang dikirim host menetapkan mode `GameMode::OneVsOne`, map `1`, tick rate default `30`, dua slot, seed, match id, gear/reborn, nama player, hero, dan ready state. Host memvalidasi pilihan client sebelum memasukkannya ke `MatchConfig`.

## Authority model dan lifecycle thread

Client tidak mengirim posisi final, HP, damage, hasil serangan, atau status menang. Client mengirim intent `InputCommand`; host memvalidasi command, memasukkannya ke simulation queue, dan mengirim `StateSnapshot`. Input lokal host dan input remote client menggunakan tipe command yang sama.

```text
UI/HUD input
  -> GameLayer / LanSession
      -> LanProtocol validation
          -> LanTransport UDP queue
              -> host-authoritative session
                  -> StateSnapshot
                      -> client GameLayer presentation
```

`LanTransport` memiliki worker native yang hanya menangani socket, encode/decode frame, dan queue event. Worker tidak boleh memanggil `Director`, `Node`, `Sprite`, scheduler, `GameLayer`, atau UI. `LanSession::poll()` menguras queue pada main thread melalui callback update. `LanNetworkRuntime::sharedLanSession()` mempertahankan session saat transisi lobby ke loading/battle.

Guard `networkActive()` memastikan `LanSession::poll()` dan `getRooms()` langsung dilewati ketika transport/discovery tidak aktif. Pada battle offline, `_networkBattle` false sehingga `GameLayer` tidak masuk ke update jaringan.

## Protocol dan validasi

Frame terdiri atas magic `NSV2`, protocol version, message type, payload length, sequence, tick, dan payload. Payload dibatasi maksimum `64 KiB`, menggunakan integer little-endian, dan memakai string length-prefixed dengan batas panjang.

| Pesan | Arah | Tujuan |
|---|---|---|
| `Hello` | Client -> Host | Handshake awal dan nama client. |
| `JoinAccept` | Host -> Client | Menerima client dan mengirim konfigurasi awal. |
| `LobbyUpdate` | Host -> Client | Menyebarkan roster, hero, ready, map, seed, dan tick rate. |
| `SelectHero` | Client -> Host | Pilihan hero client. |
| `Ready` | Client -> Host | Ready/unready client. |
| `MatchStart` | Host -> Client | Mengunci match dan memulai loading. |
| `Loaded`/`Ack` | Dua arah | Loaded barrier sebelum battle. |
| `Input` | Client -> Host | Intent dengan tick, slot, action, dan sequence. |
| `Snapshot` | Host -> Client | State authoritative entity yang tersedia. |
| `Heartbeat` | Dua arah | Memantau peer aktif. |
| `Leave`/`Disconnect` | Dua arah | Cleanup dan akhir session. |

Payload melebihi `64 KiB`, message type tidak dikenal, protocol version mismatch, match id tidak cocok, player slot invalid, action invalid, dan input dengan sequence tidak meningkat harus ditolak sebelum masuk simulation. Duplicate atau input out-of-order tidak boleh merusak state host.

## Battle bridge

`GameLayer::updateNetworkBattle()` menggunakan accumulator fixed-timestep di atas callback `update(dt)`. Render mengikuti frame rate perangkat, sedangkan tick network mengikuti `MatchConfig.tickRate`. Host memproses command dan mengirim snapshot posisi, HP, CKR, state, serta facing untuk entity yang tersedia. Client menerapkan snapshot host pada entity remote dan dapat melakukan respons input lokal melalui queue command.

Bridge ini belum menjadikan seluruh battle sebagai simulation deterministik penuh. Projectile, summon, AI kompleks, damage event detail, tower/flog, gear interaction, reborn, result replication, snapshot interpolation, dan resync penuh masih perlu diperluas serta diuji per hero.

## Android, desktop, dan permission

Project Android menggunakan `compileSdkVersion 31`, `targetSdkVersion 31`, `minSdkVersion 21`, Gradle legacy, dan NDK r17c. Source Network didaftarkan pada `app/jni/Android.mk`; file tersebut harus diawali `LOCAL_PATH := $(call my-dir)`. Linux dan Windows juga mendaftarkan source network serta library UDP native masing-masing.

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

Test protocol harus mencakup round-trip, payload >64 KiB, message type tidak dikenal, protocol version mismatch, dan invalid frame. Test session harus mencakup offline tanpa socket, host/client loopback, handshake, lobby, ready, loaded barrier, input, snapshot, input out-of-order, disconnect, timeout, dan cleanup.

Test perangkat harus dilakukan pada dua device di hotspot yang sama. Verifikasi urutannya adalah: offline tetap ringan; Network Home dapat dibuka dan di-Back; Host membuat room; Join menemukan room atau memakai manual IP; handshake; pilihan hero; ready; start; loaded barrier; movement/normal attack sebagai command; snapshot; selesai battle; GameOver tanpa force close; leave/disconnect; dan tidak ada worker/socket yang tertinggal.

## Release branch fitur

APK LAN dibuat melalui workflow GitHub Actions `release-apk.yml` pada branch fitur. Release terakhir yang memuat lifecycle opt-in adalah [`v2.1.2-lan-optin`](https://github.com/sansaks-jpg/NarutoSenki-V2/releases/tag/v2.1.2-lan-optin). Asset release terdiri atas APK dan file `.sha256`; hash harus diverifikasi setelah download. Detail command workflow dan verifikasi tersedia di [release-build.md](release-build.md).

## Known limitations

MVP belum menjanjikan semua hero, semua mode, 3v3/4v4, reconnect otomatis, internet matchmaking, spectator, replay, anti-cheat production-grade, atau snapshot interpolation tingkat lanjut. Koneksi putus selama battle berakhir dengan state `Finished`; timeout handshake dan timeout battle saat ini lima detik. Discovery menggunakan UDP broadcast, sehingga AP isolation dapat mengharuskan manual IP.

## Referensi

[1]: ../projects/NarutoSenki/Classes/Network/LanProtocol.hpp "LAN protocol types and limits"
[2]: ../projects/NarutoSenki/Classes/Network/LanTransport.cpp "Native UDP transport worker"
[3]: ../projects/NarutoSenki/Classes/Network/LanDiscovery.cpp "UDP discovery"
[4]: ../projects/NarutoSenki/Classes/Network/LanSession.cpp "Host/client session lifecycle"
[5]: ../projects/NarutoSenki/Classes/Network/NetworkLobbyLayer.cpp "LAN lobby UI and transitions"
[6]: ../projects/NarutoSenki/Classes/GameLayer.cpp "Battle command and snapshot bridge"
[7]: ../tests/lan_protocol_test.cpp "Protocol tests"
[8]: ../tests/lan_session_test.cpp "Session loopback and offline tests"
[9]: ../.github/workflows/release-apk.yml "APK release workflow"
