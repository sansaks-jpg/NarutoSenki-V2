# Laporan Debug — Fitur Multiplayer LAN (NarutoSenki-V2)

Tanggal: 2026-08-23
Cakupan review statis: `Classes/Network/*` (LanProtocol, LanTransport, LanSession, LanDiscovery, LanNetworkRuntime, NetworkLobbyLayer) + integrasi (`GameLayer`, `LoadLayer`, `StartMenu`, `GameMode/IGameModeHandler`, `Core/Hero.hpp`, `HPBar`).
Metode: code review statis menyeluruh; belum ada build/smoke test runtime pada saat laporan ini dibuat.

---

## Yang sudah benar (patut dipertahankan)

- Disiplin threading sesuai aturan `AGENTS.md`: worker `LanTransport`/`LanDiscovery` bebas Cocos2d-x; semua konsumsi event via `poll()` di main thread.
- Lifecycle opt-in benar: mode offline/training tidak membuat socket (`networkActive()` guard di `LanSession::poll()`), dan `stop()` dipanggil di `onGameOver`/`onLeft`/destructor lobby.
- Protokol wire-format (`LanProtocol`) rapi: bounds-check lengkap, little-endian konsisten, validasi tipe/action, limit payload.
- Urutan `_CharacterArray` konsisten antar-peer (index 0 = slot host, 1 = slot client di kedua device) karena `initNetworkHeros` mempertahankan urutan `config.slots`.
- Spawn position deterministik dari map object index yang sama di kedua device.

---

## BUG KRITIS (game-breaking)

### C1. Semua skill/OUGI/item tidak berfungsi sama sekali di mode LAN
- **Lokasi**: `LanSession.cpp:333` (`validateInput`)
- Range validasi salah:
  ```cpp
  if (command.action < ActionType::Move || command.action > ActionType::NormalAttack)
  ```
  `NormalAttack = 2`, padahal protokol mendefinisikan `Skill1..Skill5, Item1` (3–8). Akibatnya:
  - **Host**: submitInput sendiri ditolak → skill host tidak pernah masuk `_inputCommands`.
  - **Client**: submitInput ditolak lokal SEBELUM terkirim, dan seandainya terkirim pun `handleMessage` host akan drop.
- Fungsi `GameLayer::applyNetworkCommand` (GameLayer.cpp:753–776) sudah mendukung `Skill1..Item1`, tapi tidak pernah menerima command tersebut.
- **Dampak**: di battle LAN hanya Move + NormalAttack yang jalan. Bertentangan langsung dengan requirement sinkronisasi `SKILL1..SKILL3, OUGIS1, OUGIS2, Item1` di AGENTS.md.
- **Fix**: ganti batas atas ke `ActionType::Item1`.

### C2. Client stuck selamanya di layar Loading bila client selesai load lebih dulu
- **Lokasi**: `LanSession.cpp:296–311` (`markLoaded`), bandingkan dengan handler `Loaded` di `LanSession.cpp:537–553`
- Skenario: client teks tombol load dulu → kirim `Loaded`; host belum menekan tombol (`_localLoaded=false`). Saat host menekan, `markLoaded()` transisi host ke `Battle` tetapi **tidak pernah mengirim `Ack`** ke client. Client menunggu `Ack` di state `Loading` tanpa timeout → deadlock permanen.
- Path sebaliknya (host load dulu) benar karena handler pesan `Loaded` mengirim `Ack`.
- Kasus ini umum terjadi (device host sering lebih lambat / user host telat menekan tombol).
- **Fix**: di branch host `markLoaded`, jika `_remoteLoaded` kirim `Ack` sebelum `setState(Battle)`.

### C3. Sinkronisasi damage satu arah — hasil pertarungan divergen antar device
- **Lokasi**: `GameLayer.cpp:688–732` (`updateNetworkBattle`), `GameLayer.cpp:779–795` (`applyNetworkSnapshot`)
- Arsitektur saat ini:
  - Input **client** direlay ke host → host mensimulasikan karakter client → snapshot slot-0 (host char) & slot-1 (client char) dikirim 15 Hz.
  - `applyNetworkSnapshot` hanya menerapkan posisi/flipped/hp/ckr untuk slot **lawan** (skip slot lokal) — benar sebagai mirror.
  - **Tetapi** serangan/skill **host** tidak pernah direplay di device client (tidak ada trigger animasi/hit di karakter mirror; field `CharacterSnapshot.state` bahkan tidak pernah dibaca). Damage hanya muncul di simulasi tempat penyerang berada.
- Akibat asimetris:
  - Client→host: tersinkron (via relay input + authority host + snapshot slot-0).
  - Host→client: **tidak pernah sampai** ke HP riil client.
  - Host melihat HP client turun (simulasi lokal host), bisa "membunuh" client di simulasinya, sedangkan HP riil client utuh. Win condition tiap device divergen.
- **Arah perbaikan** (butuh keputusan desain): replay aksi lawan di device penerima (kirim event aksi, bukan cuma posisi), atau pindah ke lockstep input dengan seed bersama, atau jadikan snapshot penuh-authoritative termasuk HP semua unit.

### C4. AI aktif pada karakter remote setelah reborn → double control
- **Lokasi**: `Core/Hero.hpp:465–468` (path reborn)
- Guard awal sudah benar (`BattleRuntimeSystem`, GameLayer.cpp:39: `hero->isCom() && !layer->_networkBattle` → skip `doAI()`). Tetapi path reborn memanggil `doAI()` untuk semua `isNotPlayer()`. Karakter remote dibuat dengan `Role::Com` (`IGameModeHandler.hpp:142`), dan reborn default aktif (`enableHeroReborn=true`).
- Setiap kematian pertama, AI lokal mulai menggerakkan karakter remote (schedule `setAI` 0.1s) sementara device juga menerima input remote / snapshot posisi → karakter lawan "hantu" bergerak sendiri, bertarung sendiri, memperparah divergensi C3.
- **Fix**: guard `!_networkBattle` juga di path reborn (atau jangan buat remote char sebagai Com AI-driven).

### C5. Tidak ada polling sesi saat layar LoadLayer + timeout battle 5 detik prematur
- **Lokasi**: `LoadLayer.cpp` (tidak ada `update/poll` sama sekali), `LanSession.cpp:609–614`
- Alur: state menjadi `Battle` saat masih di lobby/loading → scene diganti ke `LoadLayer` → **tidak ada yang memanggil `session.poll()`** → heartbeat berhenti. Timeout battle (`lastReceive >= 5000ms`) dihitung sejak state Battle, yaitu SEBELUM loading selesai.
- Device yang lebih dulu selesai load akan menjalankan `updateNetworkBattle` → poll → tidak ada paket masuk (lawan masih loading) → dalam ≤5 detik `transport.stop()` + state `Finished`. Ketika lawan selesai load, match sudah mati: input gagal dikirim diam-diam (`submitInput` return false diabaikan pemanggil), snapshot berhenti → ghost match.
- Cold-start Android hampir pasti > 5 detik untuk preload asset.
- **Fix minimal**: panggil `session.poll()` di `LoadLayer::update`, naikkan/geser awal hitungan timeout battle ke setelah pertukaran paket pertama pasca-load, atau kirim keepalive eksplisit selama fase Loading.

### C6. Handshake UDP one-shot tanpa retransmisi
- **Lokasi**: `LanSession::join` (Hello sekali), `handleMessage` (JoinAccept/MatchStart/Ack/Loaded semuanya one-shot)
- Satu datagram hilang di LAN (biasa terjadi):
  - `Hello` hilang → client hang 5 detik → error "timeout handshake" (tidak ada retry).
  - `MatchStart` hilang → client selamanya di Lobby, host lanjut Loading (host tidak punya timeout di state Lobby/Loading untuk remote yang hilang).
  - `Ack` hilang → varian dari C2.
- **Fix**: retry dengan interval untuk handshake-critical messages + timeout per state (Lobby/Loading juga).

---

## BUG HIGH

### H1. Disconnect mid-battle tidak pernah ditangani GameLayer → ghost match
- `GameLayer::updateNetworkBattle` memanggil `session.poll()` tetapi **mengabaikan** notices/state. Bila lawan leave (`onGameOver`/`onLeft` → `stop()` → Leave best-effort), player yang tersisa tidak pernah mendapat game over screen; karakter lawan membeku; match berjalan tanpa lawan sampai user keluar manual.
- `MessageType::MatchEnd` didefinisikan di protokol tapi tidak pernah dikirim/ditangani.
- **Fix**: cek `drainNotices`/`state()` tiap frame di `updateNetworkBattle`; trigger `onGameOver` saat `Finished`/`Error`.

### H2. Satu datagram rusak = fatal error session
- `LanSession.cpp:574–577`: semua `TransportEventType::Error` langsung `setState(Error)` walau sedang battle. Worker push Error untuk **setiap datagram yang gagal decode** (`LanTransport.cpp:333–338`) — termasuk noise/broadcast dari aplikasi lain di port yang sama, atau ICMP-derived errors.
- **Fix**: abaikan/drop malformed datagram (log saja); Error event hanya fatal untuk kesalahan socket lokal.

### H3. Lobby/Hosting tanpa receive-timeout → pemain hantu
- `LanSession::poll` hanya mengecek `lastReceiveMs` di state `Battle`. Di `Hosting/Lobby`, jika client crash/tanpa `Leave`, host tetap menampilkan `remoteConnected=true` dan lobby basi tanpa batas waktu.

### H4. Move-release hilang (UDP) → karakter lawan jalan terus
- `JoyStickRelease` (GameLayer.cpp:797–806) mengirim satu packet `Move(0,0)`. Paket hilang = host terus mensimulasikan walk tanpa stop. Tidak ada pengulangan idle/keepalive maupun watchdog tick-based.
- **Fix**: kirim Move periodically saat joystick ditahan DAN saat dilepas kirim idle beberapa kali, atau gunakan timestamp expiry per command.

### H5. State pollution singleton game-mode handler
- `s_ModeHandlers` (StartMenu.cpp:5) adalah singleton statik per proses. Jalur network memanggil `initNetworkHeros` yang me-reset `gd`, tetapi **tidak** mereset `mapId`, `enableKonohaTowers/AkatsukiTowers`, flag flog, `enableHeroReborn`, dan `Mode1v1::init()` tidak pernah dipanggil di jalur network (inkonsisten dengan jalur offline lewat `GameModeLayer`).
- Main mode offline apa pun sebelum LAN dapat membawa setting basi (mis. tower/flog/reborn dari mode lain) ke match LAN.

### H6. Tower disimulasikan independen di tiap device
- Default `enableKonohaTowers/enableAkatsukiTowers = true` dan tidak dinonaktifkan untuk network. Tower menyerang karakter mirror secara lokal → sumber divergence HP tambahan (terkait C3) dan kehancuran tower/win condition bersifat lokal-per-device.

---

## BUG MEDIUM

| # | Bug | Lokasi |
|---|-----|--------|
| M1 | `MatchConfig.enableReborn` diabaikan — `initNetworkHeros` tidak memanggil `setHero(config.enableReborn)` | `IGameModeHandler.hpp:129–151` |
| M2 | Auto-pick "Sasuke" tiap frame untuk client saat hero kosong + race dengan `LobbyUpdate` in-flight yang bisa menghapus `heroName` → churn SelectHero/LobbyUpdate, pick user bisa tertimpa | `NetworkLobbyLayer.cpp:631–637` |
| M3 | Hero default slot host terisi `"Naruto"` otomatis di `initializeConfig` → host bisa ready tanpa memilih, dan blokir duplikat hero menyasar client yang memilih Naruto tanpa feedback | `LanSession.cpp:80` |
| M4 | Penolakan duplicate-hero di host dilakukan diam-diam (`updateRemoteHero` return false, tidak ada pesan balik ke client); sebaliknya host BEBAS mengubah hero ke hero yang sudah dipilih client → duplikat hero tetap mungkin | `LanSession.cpp:189–200` vs `setLocalHero` |
| M5 | `SelectHero`/`Ready` yang dikirim client saat state `Connecting` (belum `JoinAccept`) hilang tanpa buffer/retry | `LanSession.cpp:212, 242` |
| M6 | Iklan room stale: `playerCount` tidak pernah diupdate setelah join (selalu 1/2); room tetap di-advertise selama battle; join ke room sibuk tidak dibalas apa pun (tanpa JoinReject) → client timeout tanpa pesan; daftar hasil scan tidak pernah expire (ghost rooms) | `LanDiscovery.cpp`, `LanSession::host`, `handleMessage` Hello branch |
| M7 | Snapshot 15 Hz tanpa interpolasi; field `state` tidak dipakai → lawan tampak "meluncur" tanpa animasi walk/attack/skill di device lawan | `GameLayer.cpp:707–725, 779–795` |
| M8 | Tick tidak tersinkron antar-peer: `_networkTick` lokal masing-masing; `command.tick` lintas device tidak bermakna; `snapshot.tick` diabaikan penerima | `GameLayer.cpp:695–731` |
| M9 | `JoinReject` tidak pernah ditangani client (payload 'bad hello' dikirim host tapi client tak punya handler) | `LanSession.cpp:415–420` |
| M10 | Pause lokal (`onPause`) tidak dimirror — lawan terus bermain; desain tapi perlu keputusan (auto-forfeit atau pause-lock) | `GameLayer.cpp:886` |
| M11 | Guardian (mode hardcore/gear-off): pilihan Roshi/Han di-roll acak per-device (`setRand()+random(2)`, `GameLayer.cpp:226–231`) → dua device bisa melihat guardian berbeda; `doAI()` tanpa guard network (`GameLayer.cpp:253`) | `GameLayer.cpp:225–258` |
| M12 | SO_REUSEADDR di Windows memungkinkan dua instance bind port sama senyap (port hijack, testing 1 PC membingungkan); broadcast limited `255.255.255.255` tidak tembus beberapa konfigurasi AP/subnet | `LanTransport.cpp:105–112, 268–271` |

---

## BUG LOW

| # | Bug | Lokasi |
|---|-----|--------|
| L1 | `WSAStartup` tanpa `WSACleanup` — leak reference count Win32 tiap start/stop | `LanTransport.cpp:89–96` |
| L2 | Dead-code error handling di `onReady`: variabel `error` tidak pernah diisi karena signature `setLocalReady(bool)` tanpa param error | `NetworkLobbyLayer.cpp:798–804` |
| L3 | Nama player hard-coded "Host"/"Client", tidak ada input nama | `NetworkLobbyLayer.cpp:673, 772, 789` |
| L4 | State `Loading` memerlukan tekan manual tombol start_btn (tanpa label teks) untuk markLoaded — UX membingungkan, rawan deadlock C2 | `NetworkLobbyLayer.cpp:497–504` |
| L5 | Input joystick dikirim per-frame (~60 pkt/s) tanpa coalescing/rate-limit — spam UDP | `GameLayer.cpp:813–830` |
| L6 | `makeMatchId` dari `steady_clock` ms cast uint32 — wrap ~49 hari (teoretis) | `LanSession.cpp:48–51` |
| L7 | Worker transport bisa spin-push Error events tanpa batas bila socket rusak permanen | `LanTransport.cpp:321–326` |
| L8 | `MessageType::Resync`/`MatchEnd`/`Disconnect` didefinisikan tapi tidak dipakai — surface protokol mati | `LanProtocol.hpp:35–37` |

---

## Prioritas perbaikan yang disarankan

1. **C1** (satu baris — unblock seluruh sistem skill).
2. **C2** (tambah kirim Ack di markLoaded host).
3. **C4** (guard doAI reborn saat network).
4. **C5/H1** (poll session di LoadLayer + tangani notice/state di updateNetworkBattle + geser awal timeout battle).
5. **C6/H2/H3** (retry handshake, toleransi datagram rusak, timeout lobby).
6. Keputusan desain untuk **C3/H6/M7/M8** (model sync damage/animasi/tower) — ini menentukan apakah arsitektur snapshot saat ini cukup atau perlu lockstep/event-action.
