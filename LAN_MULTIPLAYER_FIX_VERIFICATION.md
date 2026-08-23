# Verifikasi Fix — LAN Multiplayer (commit `8d084cb`)

Tanggal: 2026-08-24
Metode: review diff commit + pembacaan ulang file di HEAD + **build & runtime test** (`tests/lan_session_test.cpp` dengan g++ MinGW 16.1, loopback UDP).

---

## VERDICT SINGKAT

> **Belum semua teratasi.** 12 dari 26 bug lama sudah benar-benar fixed, sisanya belum disentuh,
> dan fix justru **memunculkan 3 bug baru** — termasuk **2 error kompilasi yang membuat build gagal**
> dan **1 out-of-bounds write** yang bisa crash.

---

## A. BUG BARU DITIMBULKAN OLEH FIX (prioritas tinggi)

### N1 — CRITICAL: `_config.slots.resize(1)` menyebabkan out-of-bounds & rejoin rusak
- `LanSession.cpp` Leave/Disconnect handler + lobby-timeout kini memanggil `_config.slots.resize(1)`.
- Tetapi branch Hello host (`LanSession.cpp:430`) tetap menulis `_config.slots[1].playerName = name;`
  **tanpa guard ukuran** → setelah satu client keluar lalu client BARU join, terjadi
  **OOB write pada vector berukuran 1** → UB / crash / korupsi heap.
- Efek lanjutan meski tidak crash: slot 1 tidak pernah dibuat ulang →
  - `startMatch` membaca `_config.slots[1].ready` saat remoteConnected=true → OOB read;
  - `updateRemoteHero` selalu gagal (guard `size < 2`) → pilihan hero client kedua didiamkan.
- **Fix**: jangan resize; reset slot[1] ke nilai kosong (`{1, GroupId::Akatsuki, false, true, "", ""}`),
  atau tambahkan guard + recreate slot sebelum menulis.

### N2 — COMPILE ERROR: `KTools::showNotice()` tidak ada
- `GameLayer.cpp:704`: `KTools::showNotice(noticeText.c_str(), 2.0f);`
- `MyUtils/KTools.h` TIDAK memiliki fungsi `showNotice` (isi: readXMLToArray, prepareFileOGG,
  initTableInDB, saveToSQLite, encode/decode, saveSQLite, readWinNumFromSQL, readCoinFromSQL).
- Satu-satunya kemunculan "showNotice" di seluruh repo adalah call site-nya sendiri.
- **Fix**: ganti dengan mekanisme notifikasi yang ada (atau tambahkan fungsi tersebut).

### N3 — COMPILE ERROR: `onGameOver()` dipanggil tanpa argumen
- `GameLayer.cpp:705`: `onGameOver();`
- Deklarasi satu-satunya: `void onGameOver(bool isWin);` (`GameLayer.h:136`) — tanpa default argument, tanpa overload.
- **Fix**: tentukan win/lose untuk kasus disconnect (mis. `onGameOver(true)` bila pemain lokal masih hidup, atau tampilkan layar "Opponent disconnected" terpisah dari GameOver biasa).

### N4 — HIGH: Test session flaky 50% (5/10 run gagal) di loopback
- Bukti run `tests/lan_session_test.exe` (build sukses): 3/10 pertama gagal, lalu 5/10.
- Dua signature kegagalan dari harness terinstrumentasi (`dbg_flow`, 8 iterasi):
  1. **ITER 7**: `HOST state=Hosting rc=0` permanen sementara `CLIENT Lobby rc=1` — host tampaknya
     menerima Hello lalu kembali Hosting **dalam batch poll yang sama** (transisi tak tertangkap sampler),
     dicurigai datagram asing/stale yang cocok dengan handler Leave (kini pakai pencocokan
     address+port yang bisa false-positive), ATAU paket handshake yang hilang/duplikat.
  2. **ITER 5**: HOST melompat `Loading → Finished rc=0` ~80ms setelah battle mulai tanpa pernah
     tercatat masuk Battle — hanya path battle-timeout yang bisa menghasilkan Finished+rc=0,
     padahal syarat 6000ms mustahil tercapai dalam 80ms → indikasi state machine berjalan
     tidak sesuai model kita / ada jalur tersembunyi. Perlu investigasi dengan packet logging.
- Catatan: popup firewall Windows untuk exe test baru ikut mengganggu pengukuran; tapi traffic
  loopback umumnya tidak difilter firewall, sehingga akar masalah masih terbuka.
- **Tindak lanjut**: log semua `TransportEvent` (type/address/port) di kedua sisi saat test;
  verifikasi apakah ada frame Leave/Disconnect palsu atau kehilangan Ack.

---

## B. BUG LAMA YANG KONFIRM FIXED (12)

| Bug | Status | Bukti |
|-----|--------|-------|
| C1 validateInput blokir skill | ✅ | Batas atas kini `ActionType::Item1`; test aksi Skill1..Item1 lolos saat run sukses |
| C2 markLoaded host tanpa Ack | ✅ | `LanSession.cpp:308–315` kini mengirim Ack sebelum `setState(Battle)` |
| C4 AI reborn di karakter remote | ✅ | `Hero.hpp:465–471` diguard `!_networkBattle` (+ null check) |
| C5 timeout prematur saat loading | ✅ (sebagian) | `LoadLayer::update` kini poll session; `setState` mereset `_lastReceiveMs`; timeout battle 6s |
| C6 handshake one-shot | ✅ | Retransmit Hello tiap 600ms saat Connecting, timeout dinaikkan ke 6s |
| H1 ghost match saat disconnect | ⚠️ | Logika ditambahkan di `updateNetworkBattle`, TAPI mati oleh N2+N3 (compile error) |
| H2 datagram rusak = fatal | ✅ | Error transport kini diabaikan diam-diam |
| H3 lobby ghost (host) | ✅ | Timeout 10s khusus role Host; catatan: sisi CLIENT masih tanpa timeout |
| H4 move-release hilang | ✅ | Burst 3× idle + idle lokal langsung |
| L1 WSACleanup | ✅ (sebagian) | Dipanggil di `stop()`; error-path `openSocket` masih leak |
| M1 enableReborn diabaikan | ✅ | `initNetworkHeros` kini set `gd.enableHeroReborn` + `setHero(config.enableReborn)` |
| L5 spam input joystick | ✅ | Rate-limit 30ms (~33 pkt/s) |

Catatan positif lain: relay input kini **dua arah** (`submitInput` mengirim saat `remoteConnected`
apa pun role-nya; handler Input menerima `expectedSlot` sesuai role). Ini membuat serangan/skill lawan
ter-replay di device penerima (animasi + hit lokal), yang **meredakan sebagian besar C3/M7** —
meski belum sempurna (lihat D).

---

## C. BUG LAMA YANG BELUM DIPERBAIKI (11 + sisa parsial)

| Ref | Bug | Lokasi |
|-----|-----|--------|
| C3 (sisa) | HP karakter lokal kini ditimpa snapshot host tiap 66ms tanpa guard state — konflik dengan death/reborn lokal (`setHP` tidak memicu `setDead`; setelah reborn, snapshot basi bisa "membangkitkan" HP) | `GameLayer.cpp:799–810` |
| H3 (sisa) | Client tidak punya timeout di Lobby (host mati diam → client hang selamanya) | `LanSession::poll` |
| H5 | Pollution singleton mode handler (tower/flog/mapId/reborn tidak direset; `Mode1v1::init()` tak dipanggil di jalur network) | `IGameModeHandler.hpp` |
| H6 | Tower disimulasikan lokal per-device → divergence & win condition lokal | default handler |
| M2 | Auto-pick Sasuke tiap frame + race LobbyUpdate | `NetworkLobbyLayer.cpp:631` |
| M3 | Slot host default hero `"Naruto"` otomatis | `LanSession::initializeConfig` |
| M4 | Duplicate-hero enforcement asimetris (host bebas, client didiam diam) | `updateRemoteHero` vs `setLocalHero` |
| M5 | SelectHero/Ready saat Connecting hilang tanpa buffer | `setLocalHero/setLocalReady` |
| M6 | Iklan room stale (playerCount, advertise saat battle, tak ada JoinReject busy, rooms tak expire) | `LanDiscovery/LanSession` |
| M9 | JoinReject tidak ditangani client | `handleMessage` |
| L2/L3/L4/L6/L8 | Dead-code error var onReady; nama hard-coded; tombol load manual UX; matchId wrap; tipe protokol mati (Resync/MatchEnd/Disconnect dipakai terbatas) | berbagai |

---

## D. CATATAN DESAIN (dampak perubahan dua-arah)

Relay input dua arah adalah peningkatan arsitektur yang tepat, namun sekarang karakter remote
di-control ganda: `applyNetworkCommand` memanggil `walk()/attack()` (simulasi + animasi) DAN
`applyNetworkSnapshot` tetap menimpa posisinya tiap tick genap → rubber-banding visual antar snapshot.
Rekomendasi: untuk karakter remote, pilih SATU sumber gerak — mis. snapshot hanya sebagai koreksi
halus (lerp) atau matikan position-snap ketika command input aktif.

---

## E. LANGKAH YANG DISARANKAN (urutan)

1. Fix N2 + N3 (wajib agar build lanjut).
2. Fix N1 (ganti resize → reset isi slot; guard semua akses `slots[1]`).
3. Investigasi N4 dengan packet logging di kedua sisi (harness sudah siap di Temp).
4. Guard overwrite HP lokal saat state DEAD/KNOCKDOWN/reborn (C3 sisa).
5. Sisanya sesuai tabel C bila ingin fitur LAN benar-benar solid.
