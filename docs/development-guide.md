# Development Guide

## Sebelum mengubah kode

Mulai dari root repository dengan memeriksa branch, remote, working tree, dan file generated. Pahami apakah perubahan berada di source C++, Lua, resource, platform project, atau vendored engine. Baca dokumen yang sesuai dari [docs/README.md](README.md) sebelum mengedit.

```bash
git status --short
git branch --show-current
git remote -v
git diff --stat
```

Buat branch feature untuk perubahan nontrivial. Jangan ikut men-stage perubahan lama yang tidak terkait, terutama file font yang dapat tampak berubah karena line ending.

## Extension point karakter

Karakter baru biasanya dibuat di `Classes/Core/Shinobi`, sementara summon, guardian, clone, tower, dan flog memiliki folder khusus. Ikuti pola kelas yang sudah ada, gunakan tipe/state/role/group yang tersedia, dan daftarkan karakter di `Core/Provider.hpp`. Tambahkan asset selection, half/full portrait, skill frame, animation, audio, dan metadata yang dibutuhkan.

Checklist karakter meliputi factory name dan alias, `setID`, role/group, spawn/flip direction, HP/CKR/defense, normal attack, skill 1–5, Ougi, cooldown, damage/collision, buff, summon/master/controller, dead/reborn, AI, audio, atlas, dan save record. Uji karakter sebagai player dan COM pada lebih dari satu mode.

## Extension point mode

Mode baru memerlukan enum, handler, entry pada registry, label/deskripsi, tombol/resource, roster logic, map, team, tower/flog, gear/reborn rule, win condition, cleanup, dan dukungan SelectLayer. Jangan mengubah `Classic` untuk eksperimen online; buat handler/variant terpisah sehingga local mode tetap stabil.

Untuk LAN branch ini, mode yang sudah di-wire adalah 1v1 dengan roster terbatas dan `MatchConfig` deterministik. Perubahan LAN harus menjaga mode offline tetap tidak membuat socket atau memanggil polling. Detail kontrak ada di [lan-multiplayer.md](lan-multiplayer.md).

## Extension point UI

Menu dan scene C++ memakai `MenuItemSprite`, `Menu`, dan touch delegate; UI Lua memakai `ui.newImageMenuItem`, `Button`, dan `TouchGroup`. Callback lintas bahasa harus memakai key terpusat di `UiFlowKeys.hpp` serta helper `Cocos2dxHelper.hpp`.

Saat menambah tombol, tentukan owner state, hitbox, selected/disabled visual, callback, sound, lock condition, scene transition, dan cleanup. Elemen display-only seperti label status dan minimap tidak boleh diberi side effect tanpa kebutuhan.

## Perubahan battle

`HudLayer` hanya meneruskan intent input; simulation tetap di `GameLayer`, `CharacterBase`, `Hero`, projectile, system, dan mode handler. Untuk attack/skill baru, tambahkan tipe command/ABType jika dibutuhkan, buat button/asset, hubungkan delegate, lalu implementasikan validation resource, cooldown, collision, state transition, dan release behavior.

## Debugging

Gunakan `CCLOG`/`LOG` dengan context yang jelas. Debug minimal harus mencatat mode, hero, group, state, command, cooldown, dan scene saat masalah terjadi. Untuk bug random, catat seed atau kondisi awal. Saat melacak callback Lua, pastikan Lua engine siap, global function ditemukan, jumlah argument benar, dan error stack dibersihkan.

## Testing manual

Tidak ada test suite unit/integration lengkap di repository. Smoke test minimum adalah launch menu, scroll menu, buka Training, pilih mode, masuk select, ganti page, pilih hero dengan tap dua kali, buka/tutup Skill, mulai battle, gerak, normal attack, skill, Ougi, gear, item, pause, toggle setting, resume, surrender, GameOver, kembali menu, dan mulai match kedua.

Untuk perubahan LAN, tambahkan pemeriksaan berikut secara berurutan: jalankan game offline dan pastikan tidak ada socket/worker LAN; buka Network Home tanpa memilih Host/Join dan pastikan tidak ada polling LAN; pilih Host pada perangkat pertama; pilih Join pada perangkat kedua; verifikasi discovery atau manual IP; uji handshake, hero, ready, loaded barrier, input, snapshot, leave, timeout, hardware Back, dan kembali ke StartMenu. Setelah match LAN selesai, pastikan GameOver tidak ganda, aplikasi tidak force close, dan session/worker berhenti.

| Area perubahan | Verifikasi minimum |
|---|---|
| Menu/scene | Semua tombol, back, fade/push/pop, audio, dan cleanup. |
| Character | Player/COM, semua skill, animation, damage, dead/reborn, resource. |
| Mode | Roster, map, group, tower/flog, random, gear, win condition. |
| Resource | Clean build, atlas registration, case path, audio, Android asset copy. |
| Save | Coin, win count, best time, restart aplikasi, schema lama. |
| Platform | Target yang terdampak; desktop build tidak membuktikan Android. |
| Multiplayer LAN | Protocol round-trip, payload >64 KiB, message type/version invalid, sequence out-of-order, offline tanpa socket, host/client loopback, discovery/manual IP, timeout, Back, cleanup GameOver, dan worker tanpa akses Cocos2d-x. |

## Pre-commit checklist

```bash
git diff --check
git diff --stat
git status --short
```

Pastikan hanya file yang dimaksud yang masuk commit, tidak ada credential/API key, generated asset tidak ikut commit, dan dokumentasi diperbarui bila arsitektur berubah. Jika push ke remote diminta, catat branch dan commit hash.

## Referensi

[1]: ../AGENTS.md "Root agent guide"
[2]: ../projects/NarutoSenki/Classes/Core/Provider.hpp "Character factory"
[3]: ../projects/NarutoSenki/Classes/Utils/Cocos2dxHelper.hpp "Lua bridge helpers"
[4]: ../projects/NarutoSenki/Classes/Systems/CommandSystem.hpp "Command system"
[5]: ../Doc/README.md "Legacy development notes"
