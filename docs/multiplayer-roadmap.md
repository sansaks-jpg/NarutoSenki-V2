# Multiplayer Roadmap

## Kondisi saat ini

Source saat ini adalah local battle dengan AI/COM. Dokumentasi legacy menyebut kode dan dependency WebSocket pernah dihapus, sehingga tombol `Custom/network` di StartMenu belum menjadi lobby online. UI sudah memiliki titik masuk, tetapi belum ada session server, identity pemain, protocol, snapshot, authority, reconnect, atau anti-cheat.[1]

## Prinsip desain

Multiplayer jangan dimulai dari sinkronisasi sprite. Pisahkan **intent**, **simulation state**, dan **presentation**. Client mengirim input command; simulation authoritative menentukan hasil; HUD menampilkan state. Server sebaiknya authoritative untuk damage, cooldown, resource, spawn, death/reborn, gear, objective, dan win condition.

```text
Client UI/Input
  -> Command {matchId, playerId, tick, sequence, action, payload}
      -> transport (WebSocket/TLS)
          -> authoritative server
              -> deterministic battle simulation
                  -> validated event/state snapshot
                      -> clients
                          -> GameLayer / CharacterBase / HudLayer
```

## Entry point UI

Gunakan `MenuButtonType::Custom` pada `menu01.png` sebagai entry point lobby. Handler saat ini masih `TODO`, sehingga dapat diarahkan ke scene `OnlineLobbyLayer` tanpa mengganggu Training/local flow. Lobby minimal membutuhkan create room, join room/code, ready, leave, connection status, dan error/timeout state.

## Protocol minimum

| Message | Dari | Isi minimum |
|---|---|---|
| `hello` | Client -> Server | Protocol version, build id, client nonce. |
| `room_create` | Client -> Server | Requested mode, map, max players. |
| `room_join` | Client -> Server | Room code/token. |
| `ready` | Client -> Server | Player selection lock dan ready state. |
| `match_start` | Server -> Client | Match id, roster, map, seed, tick rate. |
| `input` | Client -> Server | Tick, sequence, action type, direction/target/payload. |
| `snapshot` | Server -> Client | Tick, authoritative entities, state hash, events. |
| `ack` | Server -> Client | Last processed input sequence/tick. |
| `resync` | Client <-> Server | Snapshot penuh ketika hash berbeda. |
| `leave`/`disconnect` | Either | Player state and reason. |
| `match_end` | Server -> Client | Result, rewards, server summary/hash. |

Gunakan schema version dan batas ukuran payload. Jangan serialisasi raw pointer atau class Cocos2d-x. ID entity, enum, dan action harus eksplisit serta stabil.

## Deterministic simulation

Battle saat ini menggabungkan view dan simulation dalam `GameLayer`/`CharacterBase`. Refactor bertahap diperlukan: pertama ekstrak command dari joystick/action button; kedua buat state data yang dapat diserialisasi; ketiga pisahkan update simulation dari rendering; keempat beri tick tetap dan seed RNG; kelima tambahkan state hash dan replay test.

State yang perlu ditentukan secara eksplisit meliputi posisi/velocity, facing, state animation yang relevan untuk gameplay, HP/CKR/CKR2, cooldown, buff/debuff, target, projectile, summon, tower/flog, group, gear/item, death/reborn, timer, coin yang relevan, dan objective. Frame sprite/particle/audio tidak perlu dikirim sebagai state authoritative.

## Tahapan implementasi

| Tahap | Hasil |
|---|---|
| 0. Offline protocol | Command/event interface dan serializer yang diuji tanpa network. |
| 1. Local loopback | Dua client dalam satu process/host; input dipisahkan dari rendering. |
| 2. Lobby prototype | Room create/join/ready dengan satu mode 1v1. |
| 3. Server tick | Server menerima input, menjalankan tick, mengirim snapshot/event. |
| 4. 1v1 playable | Satu map, roster terbatas, timeout, reconnect, result server. |
| 5. Team mode | 3v3/4v4, COM replacement, roster lock, team state. |
| 6. Hardening | TLS, authentication, rate limit, validation, replay, anti-desync, observability. |

## Reconnect dan desync

Server harus menyimpan ring buffer snapshot/event dan menerima reconnect dengan match token. Client mengirim last acknowledged tick/hash; server mengirim delta atau full snapshot. Jika state hash berbeda, jangan memperbaiki dengan posisi client; lakukan resync dan catat log. Timeout, duplicate sequence, out-of-order input, invalid action, dan client clock drift harus memiliki perilaku yang ditentukan.

## Backend dan deployment

Pisahkan server multiplayer dari game client. Server tidak boleh menjalankan Cocos2d-x renderer atau menerima asset client sebagai executable code. Gunakan service kecil dengan storage room/session, logging, metrics, dan health check. Credential server hanya berada di environment/secret manager, bukan Lua, C++, resource, atau APK.

## Risiko teknis

Refactor ini besar karena `CharacterBase`, `Hero`, `GameLayer`, `ActionButton`, `CommandSystem`, `SpawnSystem`, dan mode handler saat ini saling terhubung dengan global state, callback, timer, dan object lifecycle Cocos2d-x. Prototipe realistis dimulai dari 1v1, roster terbatas, fixed map, dan server-authoritative input; jangan langsung menargetkan semua hero/mode.

## Referensi

[1]: ../Doc/README.md "Legacy note that network/WebSocket code was removed"
[2]: ../projects/NarutoSenki/Classes/Systems/CommandSystem.hpp "Existing command abstraction"
[3]: ../projects/NarutoSenki/Classes/GameLayer.cpp "Current battle orchestration"
[4]: ../projects/NarutoSenki/Classes/HudLayer.cpp "Current HUD input dispatch"
[5]: ../projects/NarutoSenki/Classes/GameMode/IGameModeHandler.hpp "Mode lifecycle and roster contract"
