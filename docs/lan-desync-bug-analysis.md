# Analisis Bug Desync — LAN Multiplayer 1v1

Laporan analisis akar masalah untuk gejala: **posisi tidak sama, damage tidak sama, minion tidak sama, death tidak kehitung** antara dua device.

> **STATUS: SELURUH BUG DI LAPORAN INI SUDAH DIPERBAIKI** (protocol v2, host-simulasi penuh).
> Rincian implementasi ada di [`lan-multiplayer.md`](lan-multiplayer.md) § "Authority model".
> Verifikasi: `tests/lan_protocol_test.cpp` + `tests/lan_session_test.cpp` (loopback end-to-end) lolos; seluruh TU tersentuh lolos `-fsyntax-only` (g++ C++20).

## Ringkasan eksekutif

Arsitektur LAN saat ini adalah **hybrid yang tidak konsisten**: kedua device menjalankan simulasi tempur penuh secara independen (collision, damage, knockback, AI minion), tetapi hanya menyinkronkan sebagian kecil state (posisi + HP/CKR 2 hero, dikirim host 15 Hz). Tidak ada lockstep, tidak ada otoritas damage tunggal, dan sebagian data yang sudah dikirim bahkan tidak pernah diterapkan. Desync bukan satu bug, melainkan konsekuensi deterministik dari 13 masalah berikut.

```
DEVICE A (Host)                          DEVICE B (Client)
┌─────────────────────────┐              ┌─────────────────────────┐
│ Simulasi PENUH:         │   Input      │ Simulasi PENUH:         │
│ - hero A (lokal)        │ ───────────► │ - hero B (lokal)        │
│ - hero B (via input)    │   (30Hz)     │ - hero A (via input +   │
│ - flog AI lokal         │  Snapshot    │    TELEPORT snapshot)   │
│ - tower/guardian/bullet │ ◄─────────── │ - flog AI lokal         │
│                         │   (15Hz)     │ - tower/guardian/bullet │
│ rand() seed = time(A)   │              │ rand() seed = time(B)   │
└─────────────────────────┘              └─────────────────────────┘
Damage dihitung DUA kali oleh DUA simulasi yang berbeda → tidak mungkin sinkron.
```

---

## BUG-1 (KRITIS) — Damage dihitung ganda oleh dua simulasi independen

**Lokasi:** `GameLayer.cpp:746-830` (`applyNetworkCommand`, `applyNetworkSnapshot`)

Kedua device menjalankan alur serangan penuh: `attack()` → deteksi collision → `setDamage()` → HP berkurang. Sinkronisasi hanya menimpa HP via snapshot:

- Hero lokal client: menerima damage dari **simulasi sendiri** (lawan menyerang versi lokalnya) DAN dari **snapshot host** (`GameLayer.cpp:806-818`). Dua sumber menulis nilai HP yang sama dengan hasil berbeda.
- Host menghitung damage ke hero client berdasarkan posisi hasil rekonstruksi input (yang lag/kehilangan paket), bukan posisi asli client.

**Dampak:** angka damage berbeda, HP melompat-lompat, hit yang terlihat miss tetapi kena (atau sebaliknya). Inilah "damage tidak sama".

## BUG-2 (KRITIS) — Byte state pada snapshot tidak pernah diterapkan

**Lokasi:** `GameLayer.cpp:719-736` (state dikirim), `GameLayer.cpp:825-829` (tidak dipakai)

Snapshot mengirim `static_cast<uint8_t>(character->getState())`, tetapi cabang remote di `applyNetworkSnapshot` hanya memanggil:

```cpp
character->setPosition(...);
character->setFlipX(state.flipped);
character->setHP(state.hp);
character->setCKR(state.ckr);
// state.state HILANG — tidak ada setState()
```

**Dampak:**
- `State::DEAD` / `KNOCKDOWN` / `HURT` / `FLOAT` lawan tidak pernah tampil di device lain.
- `CharacterBase::dead()` (`CharacterBase.cpp:4366`, counter `_deadNum++` dan `deadLabel` di `4467-4473`) hanya jalan di device tempat HP habis secara lokal → **kill/death count berbeda antar device** ("death tidak kehitung").
- Timer reborn lawan tidak pernah muncul; lawan tampak berdiri setelah "mati" di sisi lain.
- Animasi NATTACK/SATTACK/HURT lawan tidak dirender dari snapshot.

## BUG-3 (KRITIS) — Model posisi asimetris tanpa koreksi & interpolasi

**Lokasi:** `GameLayer.cpp:801-829`

- **Hero lokal tidak pernah dikoreksi posisinya oleh host** — cabang `slot == _networkLocalSlot` hanya sync HP/CKR. Posisi hero client di host murni rekonstruksi input.
- Rekonstruksi input rusak oleh:
  - Rate limit joystick 33 Hz (`GameLayer.cpp:852-856`) vs tick 30 Hz;
  - UDP loss tanpa retransmit;
  - **Gerak lokal berhenti saat HURT/KNOCKDOWN/collision map, tapi host terus menerapkan arah `walk()` terakhir** sampai paket release berikutnya → divergensi permanen (knockback displacement juga tak pernah dikirim).
- **Hero lawan dirender dengan teleport `setPosition` 15 Hz tanpa interpolasi**, plus animasi walk tidak sinkron (BUG-2) → gerakan patah-patah dan terlihat "salah posisi".

## BUG-4 (KRITIS) — Minion/flog sepenuhnya independen per device

**Lokasi:** `GameLayer.cpp:28-33` (`BattleRuntimeSystem::onGameStart`), `GameLayer.cpp:409-448` (`addFlog`/`initFlogs`), `Core/Warrior/Flog.hpp:145-146`

- `schedule(addFlog, flogSpawnDuration)` + `initFlogs()` jalan **di kedua device** tanpa koordinasi waktu maupun isi.
- Posisi spawn memakai `rand()`: `_randomPosY = rand() % 8 + 4; _randomPosX = rand() % 32 + 20;`
- Flog menjalankan `doAI()` penuh secara lokal — target, gerak, attack semua divergen.
- Tipe flog bergantung **jumlah tower LOKAL** (`GameLayer.cpp:607-631`: PainFlog/ObitoFlog/IzumoFlog/KakashiFlog) — karena tower juga divergen (BUG-5), dua device bisa spawn jenis minion berbeda.
- Tidak ada satu pun field flog dalam `StateSnapshot`.

**Dampak:** jumlah, posisi, HP, dan perilaku minion berbeda total. Ini juga sumber damage liar yang memperparah BUG-1/BUG-5.

## BUG-5 (KRITIS) — Tower & win condition divergen; match bisa berakhir beda

**Lokasi:** `GameLayer.cpp:603-667` (cek game over), `initTower()` ~line 470-507

- Tower diserang oleh hero + flog + guardian versi lokal masing-masing device; HP tower tidak pernah disinkron.
- `checkWinCondition` menghitung `_TowerArray` **lokal**: device yang tower-nya hancur duluan memicu `onGameOver(...)`, device lain masih lanjut bertarung. Pemenang bisa terbalik.

## BUG-6 (TINGGI) — RNG tidak deterministik; `MatchConfig.seed` dikirim tapi tidak pernah dipakai

**Lokasi:** `LanSession.cpp:75,276` (seed dibuat & dikirim); pemakaian nyata: `GameLayer.cpp:386` (`playGameOpeningAnimation` → `setRand()` = `srand(time(0))`), `GameLayer.cpp:227-228` (guardian), `CharacterBase.cpp:1283-1285` (crit), `Flog.hpp:145-146`.

- `srand(time(0))` dipanggil di momen wall-clock berbeda per device → stream `rand()` berbeda total.
- Konsekuensi: critical damage beda untuk hit "yang sama", guardian acak beda (BUG-7), posisi flog beda.
- Ironisnya protokol sudah membawa `config.seed` yang deterministik — tetapi **nol referensi pemakaiannya** di gameplay (diverifikasi: hanya encode/decode di `LanProtocol.cpp:244,270`).

## BUG-7 (TINGGI) — Guardian muncul independen dan bisa berbeda antar device

**Lokasi:** `HPBar.cpp:46` (trigger ≤80% HP tower lokal), `GameLayer.cpp:226-258` (spawn)

- Trigger berbasis HP bar tower **lokal**; nama guardian `random(2)` → Roshi vs Han bisa berbeda di dua device.
- Guardian masuk `_CharacterArray` index ≥2; loop snapshot dibatasi `i < maxPlayers(2)` (`GameLayer.cpp:725`) → tidak pernah disinkron. Entitas tempur LV6 yang berbeda ini menyerang tower/hero → akselerator desync besar begitu tower pertama tersentuh.

## BUG-8 (TINGGI) — Transport UDP polos: input hilang permanen, tanpa reorder

**Lokasi:** `Network/LanTransport.cpp` (sendto/recvfrom polos, tanpa ack/retry), `LanSession.cpp:493-505`

- Kehilangan paket `Input` = aksi hilang selamanya. Burst 3x di `JoyStickRelease` (`GameLayer.cpp:826-840`) hanya mitigasi untuk stop, bukan untuk walk/attack/skill.
- Validasi `command.sequence <= _lastRemoteInputSequence` **membuang** paket out-of-order tanpa buffer reorder → jika paket "idle" datang lebih dulu lalu dibuang karena urutan, hero lawan jalan terus ke arah salah sampai paket Move berikutnya.
- Tidak ada watchdog idle-timeout untuk hero remote.

## BUG-9 (SEDANG-TINGGI) — Snapshot diterapkan tanpa cek urutan tick

**Lokasi:** `LanSession.cpp:526-531` (push FIFO apa adanya), `GameLayer.cpp:728-743` (apply semua), `snapshot.tick` diabaikan.

UDP boleh reorder: snapshot lama yang datang telat **menimpa state yang lebih baru** → posisi loncat mundur sesaat. Perbaikan murah: buang snapshot dengan `tick <= lastAppliedTick`.

## BUG-10 (SEDANG) — Tick tidak pernah align; accumulator membuang waktu

**Lokasi:** `GameLayer.cpp:709-745`

- `_networkTick` dimulai pada momen wall-clock berbeda per device (host masuk Battle saat Ack terkirim, client saat Ack diterima).
- `MIN(dt, 0.25f)` membuang akumulasi saat frame hitch → counter tick permanen bergeser.
- `command.tick` diisi pengirim namun tak pernah dipakai penerima. Karena tidak ada lockstep maupun epoch bersama, tick saat ini tidak bermakna lintas device.

## BUG-11 (SEDANG) — Transisi mati/reborn hero lokal konflik dengan override HP host

**Lokasi:** `GameLayer.cpp:806-818`

- Guard hanya `DEAD`/`KNOCKDOWN`; saat `HURT`/`FLOAT` (sedang di-combo) HP tetap ditimpa host di tengah simulasi combo lokal → nilai loncat.
- `setHP(0)` dari host **tidak memicu** alur `dead()` (yang hanya jalan lewat alur damage lokal) → hero bisa menampilkan HP 0 tanpa mati, atau mati di detik berbeda dari host.
- `setCKR` override tanpa syarat bentrok dengan pengeluaran chakra skill lokal.

## BUG-12 (RENDAH-MENENGAH) — Bullet, summon, clone, gear, buff tidak disinkron sama sekali

Hanya 2 hero dalam snapshot. Projectile (`Projectile/Bullet`), summon/Kuchiyose, Bunshin, buff, item effect dibuat oleh event animasi **lokal** dan hidup dalam array lokal — di device lawan semuanya adalah hasil rekonstruksi berbeda.

## BUG-13 (RENDAH) — Timer match & kosmetik tidak sinkron

`updateGameTime` jalan per-device (1 detik sendiri-sendiri), audio battle start dipilih `random(2)` per-device (`GameLayer.cpp:386-388`). Kosmetik, tapi menandakan tidak ada "jam pertandingan" bersama.

---

## Kesimpulan akar masalah

1. **Model sinkronisasi salah bentuk.** Bukan lockstep deterministik, bukan server-authoritative penuh — melainkan "kedua device simulasikan semuanya, lalu timpa sebagian angka". Setiap sistem yang tidak masuk snapshot (flog, tower, guardian, bullet, state animasi) pasti desync.
2. **Data yang sudah dikirim tidak dimanfaatkan** (byte `state`, field `tick`, field `seed`) — tiga perbaikan termurah justru tertinggal.
3. **Non-determinisme aktif**: `srand(time(0))`, `rand()` untuk crit/spawn/guardian, UDP loss tanpa reliabilitas.

## Rekomendasi perbaikan

### Tahap 1 — Patch gejala (DITERAPKAN ✓)

| # | Perbaikan | Menutup | Status |
|---|---|---|---|
| 1 | Terapkan byte `state` di snapshot; transisi DEAD→`deadLabel++`/fade, revive ala `Hero::reborn` | BUG-2, BUG-11 | ✅ `applyCharacterSnapshot()` |
| 2 | Buang snapshot stale (`tick <= _netLastAppliedTick`) | BUG-9 | ✅ `updateNetworkBattle()` |
| 3 | Flog + guardian: hanya host yang spawn/`doAI`; client mirror dari unit snapshot (id, variant nama/group) | BUG-4, BUG-7 | ✅ `applyNetworkUnits()`, gate di `onGameStart`/`initGard` |
| 4 | Host satu-satunya penghitung outcome: guard `acceptAttack`/`setDamage`/`HPBar::loseHP` no-op di client; HP/CKR murni snapshot | BUG-1 | ✅ |
| 5 | Interpolasi posisi hero remote & unit (lerp 120 ms) + posisi hero client dilaporkan via `ClientState` (slot tervalidasi) | BUG-3 | ✅ |
| 6 | Game over diputuskan host saja + broadcast `MatchEnd(winnerGroup)` | BUG-5 | ✅ `checkTower()` |
| 7 | PRNG xorshift ber-seed `MatchConfig.seed` untuk guardian & keputusan lintas device (bukan `srand(time)`) | BUG-6 | ✅ `netRandom()` |
| 8 | Reliable mini-channel: aksi diskrit diretransmit ~120 ms sampai di-ack (ack dipiggyback di header semua pesan); input out-of-order diterima via sorted-insert per slot | BUG-8 | ✅ `LanSession` |

Tambahan yang ikut dibereskan: header protokol v2 (+field `ack`), pesan `ClientState`/`MatchEnd`, `elapsedSeconds` pada snapshot (jam match sinkron), `unitId` uint16, burst 3x joystick dihapus (tidak dibutuhkan), `Move` tidak lagi direlay.

### Tahap 2 — Perbaikan struktural (SESUAI PILIHAN DESAIN ✓)

Diimplementasikan sebagai **host-simulasi penuh** (opsi 2): client hanya mengirim input + state hero miliknya; host mensimulasikan SELURUH dunia (hero, flog, tower, guardian) dan mengirim world-state lengkap; client merender dengan prediksi ringan untuk hero sendiri — sesuai prinsip AGENTS.md *"jangan menjadikan posisi client sebagai sumber kebenaran"*. Opsi lockstep deterministik tidak diambil karena float drift dan jejak `rand()` di seluruh codebase terlalu luas untuk disinkronkan aman.

### Catatan verifikasi

Verifikasi otomatis yang sudah dijalankan:
- `tests/lan_protocol_test.cpp` — roundtrip header+ack, snapshot units (tower/guardian/flog/variant), payload terpotong, kind invalid, version mismatch, payload oversize.
- `tests/lan_session_test.cpp` — loopback dua session: handshake→lobby→match→battle, relay input diskrit+move, reliable queue, snapshot units ke client, ClientState validasi slot host-side, MatchEnd broadcast.

Smoke test manual yang wajib dilanjutkan pada device sungguhan (sesuai `AGENTS.md`): host+client hotspot, catat role; uji damage saling serang (angka HP identik), kill counter sama saat hero mati, minion muncul identik di kedua layar, tower hancur → kedua device keluar bersamaan dengan pemenang sama, dan disconnect salah satu device mengakhiri match.
