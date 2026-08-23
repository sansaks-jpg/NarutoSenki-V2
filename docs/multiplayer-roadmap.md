# Multiplayer Roadmap

## Kondisi saat ini

Branch `feature/lan-hotspot-multiplayer` sudah memiliki MVP multiplayer **LAN/hotspot 1v1 host-authoritative**. Entry point-nya adalah `MenuButtonType::Custom` pada StartMenu, yang membuka `NetworkLobbyLayer`. Fitur ini bukan matchmaking internet dan tidak membutuhkan backend server.

MVP menyediakan pembuatan room oleh host, discovery room melalui UDP broadcast, join melalui room yang ditemukan atau alamat manual `IP:port`, lobby dua slot, pilihan hero terbatas, ready/loading barrier, fixed tick, input command, state snapshot, heartbeat, timeout, leave, dan transisi ke battle. Host menjadi authority untuk menerima input remote dan mengirim snapshot. Detail implementasi dan prosedur uji berada di [lan-multiplayer.md](lan-multiplayer.md).

Lifecycle LAN bersifat **opt-in**. Membuka StartMenu, memainkan Training/offline, atau hanya membuka Network Home tidak membuat socket, tidak menjalankan worker UDP, dan tidak melakukan polling LAN. Transport gameplay/discovery baru dimulai ketika pemain memilih `HOST ROOM` atau `JOIN ROOM`, kemudian dihentikan ketika Leave, Back, GameOver, timeout, atau scene keluar.

## Prinsip desain yang sudah diterapkan

Multiplayer dipisahkan menjadi **intent**, **simulation state**, dan **presentation**. Client mengirim `InputCommand`; host memvalidasi command, menjalankan sisi authoritative yang tersedia, lalu mengirim `StateSnapshot`. Client tidak mengirim posisi final, HP, damage, hasil serangan, atau status menang sebagai sumber kebenaran.

```text
NetworkLobbyLayer / HUD input
  -> LanSession
      -> LanProtocol frame validation
          -> LanTransport UDP
              -> host/client session
                  -> GameLayer command/snapshot bridge
                      -> battle presentation
```

`LanTransport` memiliki worker native yang hanya menangani socket, encode/decode frame, dan queue event. Worker tidak boleh mengakses `Director`, `Node`, `Sprite`, scheduler, atau UI. `LanSession::poll()` menguras queue pada main thread melalui callback update. `LanNetworkRuntime::sharedLanSession()` mempertahankan session saat scene berubah dari lobby ke loading/battle.

## Kontrak protocol LAN

| Message/kontrak | Arah | Fungsi MVP |
|---|---|---|
| `Hello` | Client -> Host | Memulai handshake dan mengirim nama player. |
| `JoinAccept` | Host -> Client | Mengirim konfigurasi awal dan menerima client ke lobby. |
| `LobbyUpdate` | Host -> Client | Menyebarkan roster, hero, ready state, map, seed, dan tick rate. |
| `SelectHero` | Client -> Host | Mengirim pilihan hero client. |
| `Ready` | Client -> Host | Mengubah ready state client. |
| `MatchStart` | Host -> Client | Mengunci `MatchConfig` dan memulai loading. |
| `Loaded`/`Ack` | Dua arah | Menyelesaikan loaded barrier sebelum battle. |
| `Input` | Client -> Host | Mengirim intent dengan tick, slot, action, dan sequence. |
| `Snapshot` | Host -> Client | Mengirim state authoritative yang tersedia. |
| `Heartbeat` | Dua arah | Menjaga koneksi dan mendeteksi peer yang berhenti merespons. |
| `Leave`/`Disconnect` | Dua arah | Mengakhiri session dengan cleanup eksplisit. |

Frame memiliki magic, protocol version, message type, payload length dengan batas `64 KiB`, sequence, tick, dan payload. Payload invalid, message type tidak dikenal, version mismatch, sequence out-of-order, slot invalid, action invalid, dan match id yang tidak sesuai tidak boleh masuk ke simulation.

## Deterministic simulation dan batas MVP

`MatchConfig` menetapkan mode 1v1, map, seed, tick rate, jumlah pemain, gear/reborn, dan slot roster. `GameLayer::updateNetworkBattle()` menggunakan accumulator fixed-timestep; render tetap mengikuti frame rate perangkat. Implementasi saat ini adalah seam command/snapshot dan belum membuktikan determinisme penuh untuk semua efek gameplay.

Projectile, summon, AI kompleks, damage event detail, tower/flog, gear interaction, reborn, result replication, snapshot interpolation, dan resync penuh masih membutuhkan perluasan per hero serta pengujian perangkat. MVP tidak menjanjikan 3v3/4v4 LAN, semua hero, reconnect otomatis, spectator, replay, internet matchmaking, atau anti-cheat production-grade.

## Milestone

| Status | Milestone | Hasil |
|---|---|---|
| Selesai | Offline protocol | Serializer/frame validation dengan batas payload dan rejection untuk data invalid. |
| Selesai | Local loopback | Host/client dua session dalam satu process dengan handshake, lobby, ready, loading, input, snapshot, dan cleanup. |
| Selesai | LAN lobby prototype | Host room, UDP broadcast discovery, manual IP fallback, Join, Back/Leave, dan timeout. |
| Selesai terbatas | Playable LAN 1v1 | Fixed tick, roster terbatas, host-authoritative input/snapshot bridge, serta APK release untuk branch fitur. |
| Berikutnya | Simulation hardening | Memisahkan state simulation dari rendering dan memperluas validasi damage, cooldown, projectile, summon, tower, gear, reborn, dan win condition. |
| Berikutnya | Team LAN | Mendukung 3v3/4v4, roster lock, COM replacement, group state, dan bandwidth budget. |
| Berikutnya | Resync/reconnect | Snapshot ring buffer, match token, last acknowledged tick/hash, full resync, dan reconnect yang eksplisit. |
| Masa depan | Internet service | Backend room/session, identity, authentication, TLS, rate limit, metrics, health check, dan deployment terpisah dari client. |

## Reconnect dan desync

MVP saat ini mengakhiri match dengan state `Finished` ketika timeout atau disconnect; tidak ada reconnect diam-diam. Pengembangan berikutnya harus menyimpan ring buffer snapshot/event dan menerima reconnect dengan match token. Client mengirim last acknowledged tick/hash, kemudian host/server mengirim delta atau full snapshot. Posisi client tidak boleh dipakai untuk memperbaiki state authoritative.

State hash, duplicate sequence, invalid action, client clock drift, dan timeout harus memiliki perilaku yang terdokumentasi serta test deterministik. Reconnect tidak boleh memperpanjang lifetime socket ketika session sudah berada pada state `Finished`, `Error`, atau `Idle`.

## Backend dan deployment masa depan

Jika multiplayer internet ditambahkan, backend harus dipisahkan dari renderer Cocos2d-x dan tidak boleh menerima asset client sebagai executable code. Service tersebut memerlukan storage room/session, logging, metrics, health check, authentication, TLS, rate limiting, dan secret management. Credential server tidak boleh berada di Lua, C++, resource, atau APK.

## Risiko teknis

Refactor lanjutan tetap berisiko karena `CharacterBase`, `Hero`, `GameLayer`, `ActionButton`, `CommandSystem`, `SpawnSystem`, dan mode handler saling terhubung dengan global state, callback, timer, dan lifecycle object Cocos2d-x. Setiap perluasan harus mempertahankan jalur offline, menghindari akses Cocos2d-x dari worker, dan memverifikasi cleanup setelah match selesai.

## Referensi

[1]: lan-multiplayer.md "LAN Multiplayer MVP pada branch fitur"
[2]: ../projects/NarutoSenki/Classes/Network/LanProtocol.hpp "LAN protocol types and limits"
[3]: ../projects/NarutoSenki/Classes/Network/LanTransport.cpp "Native UDP transport worker"
[4]: ../projects/NarutoSenki/Classes/Network/LanSession.cpp "Host/client session lifecycle"
[5]: ../projects/NarutoSenki/Classes/GameLayer.cpp "Battle command and snapshot bridge"
[6]: ../projects/NarutoSenki/Classes/GameMode/IGameModeHandler.hpp "Mode lifecycle and roster contract"
[7]: ../.github/workflows/release-apk.yml "APK release workflow"

Fakta pada dokumen ini diringkas dari source LAN dan battle pada branch `feature/lan-hotspot-multiplayer`, bukan dari kontrak backend yang belum diimplementasikan.
