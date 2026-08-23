# LAN Multiplayer MVP

## Status

Branch ini menambahkan fondasi multiplayer LAN 1v1 host-authoritative. Tombol `Custom/Network` pada start menu membuka screen Network yang memiliki alur `Host Room`, `Join Room`, discovery room berbasis UDP broadcast, manual `IP:port`, lobby, pilihan hero, ready, loaded barrier, dan transisi ke pipeline battle.

Mode offline existing tidak membuka socket karena seluruh lifecycle LAN hanya dibuat dari `NetworkLobbyLayer` ketika pengguna memilih Network.

## Supported MVP

MVP mendukung satu host dan satu client pada jaringan lokal atau hotspot yang sama. Host menggunakan UDP gameplay port `28765`; discovery menggunakan UDP port `28766`. Discovery hanya membantu menemukan room. Jika broadcast/multicast tidak tersedia karena konfigurasi hotspot atau AP isolation, client dapat memasukkan alamat host secara manual, misalnya `192.168.43.1:28765`.

Mode yang dipakai adalah 1v1 (`GameMode::OneVsOne`), map `1`, tick rate default `30`, dan dua slot. Host menggunakan hero `Naruto` sebagai pilihan awal, sedangkan client menggunakan `Sasuke` sebagai pilihan awal dan dapat menggantinya dari tombol `CHANGE HERO`. Host memvalidasi pilihan client sebelum pilihan tersebut dikunci ke `MatchConfig`.

## Authority model

Client tidak mengirim posisi final, HP, damage, hasil serangan, atau status menang. Client mengirim intent `InputCommand`; host memvalidasi command, memasukkannya ke simulation queue, lalu mengirim `StateSnapshot`. Input host lokal dan input client remote menggunakan tipe command yang sama; perbedaannya hanya sumbernya.

`GameLayer::updateNetworkBattle` menjalankan fixed-timestep accumulator di atas callback `update(dt)`. Render tetap mengikuti frame rate perangkat, sedangkan tick network memakai `MatchConfig.tickRate`. Host mengirim snapshot posisi, HP, CKR, state, dan facing untuk entity yang tersedia. Client menerapkan snapshot hanya pada entity remote; input lokal dapat diterapkan lebih cepat melalui queue command, lalu dikoreksi oleh snapshot host.

Semua event hasil polling socket masuk ke queue internal transport. Hanya main thread yang memanggil `LanSession::poll()` dan menyentuh `GameLayer`, `Hero`, atau node Cocos2d-x.

## Protocol

Frame memiliki magic `NSV2`, protocol version, message type, payload length dengan batas `64 KiB`, sequence, tick, dan payload. Payload menggunakan integer little-endian serta string length-prefixed dengan batas panjang. Message type yang sudah dicadangkan meliputi handshake, room, lobby, `select_hero`, `ready`, `match_start`, `loaded`, `input`, `snapshot`, `ack`, `heartbeat`, leave/disconnect, result, resync, dan error.

Handshake dan lobby mengirim `MatchConfig`, sedangkan gameplay mengirim `InputCommand` dan `StateSnapshot`. Duplicate input remote ditolak berdasarkan sequence. Payload invalid, slot invalid, match id tidak cocok, action tidak dikenal, dan version mismatch tidak boleh diterapkan ke simulation.

## Android and desktop

Project Android saat ini memiliki `compileSdkVersion 31`, `targetSdkVersion 31`, dan `minSdkVersion 21`. Oleh karena itu branch ini tidak menambahkan `ACCESS_LOCAL_NETWORK`, yang baru diperlukan ketika aplikasi menaikkan target SDK ke Android 17/API 37 atau lebih tinggi. Saat target tersebut dinaikkan, request permission local network wajib ditambahkan sebelum raw UDP atau NSD dipakai; manual IP tetap harus dipertahankan sebagai fallback.

Source network didaftarkan pada Android `Android.mk`, Linux `proj.linux/Makefile`, dan Windows `NarutoSenki.vcxproj`. Windows menambahkan `Ws2_32.lib`. Transport saat ini menggunakan IPv4 UDP native dan tidak membutuhkan library eksternal.

## Test procedure

Unit protocol dapat dijalankan dengan:

```sh
g++ -std=c++20 -Wall -Wextra -Werror \
  -Iprojects/NarutoSenki/Classes \
  projects/NarutoSenki/Classes/Network/LanProtocol.cpp \
  tests/lan_protocol_test.cpp \
  -o /tmp/lan_protocol_test
/tmp/lan_protocol_test
```

Integration loopback dua session dapat dijalankan dengan:

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

Test perangkat harus dilakukan pada dua device yang terhubung ke hotspot yang sama. Jalur yang harus diverifikasi adalah Host membuat room, Join menemukan room atau memakai manual IP, handshake, pilihan hero, ready, start, loaded barrier, movement/normal attack sebagai command, snapshot, serta leave/disconnect dan timeout.

## Known limitations

MVP belum menjanjikan semua hero, semua mode, 3v3/4v4, reconnect otomatis, internet matchmaking, spectator, replay, atau snapshot interpolation visual tingkat lanjut. Saat ini discovery menggunakan UDP broadcast native, bukan Android NSD picker. Battle pipeline sudah memiliki seam command/snapshot dan fixed tick, tetapi efek kompleks seperti projectile, summon, AI, damage event detail, resync penuh, dan win-condition replication perlu diperluas serta diuji per hero sebelum disebut production-ready.

Koneksi yang putus selama battle berakhir dengan state `Finished`; branch ini tidak melakukan reconnect diam-diam. Timeout handshake adalah lima detik. Heartbeat dikirim berkala selama peer tersambung dan timeout battle juga lima detik.
