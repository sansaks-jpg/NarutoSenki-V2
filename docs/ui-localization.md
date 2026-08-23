# Runtime UI Localization

## Tujuan

UI interaction text sekarang dirender saat runtime melalui `CCLabelBMFont` pada C++ dan helper `ns.text` pada Lua. Perubahan ini menghindari ketergantungan pada glyph teks yang tertanam di PNG/atlas, sehingga copy dapat diganti tanpa mengedit koordinat sprite atlas. Logo, brand mark, portrait, character art, background, decorative frame, dan artwork battle tetap memakai asset PNG asli.

## Arsitektur

| Runtime | Sumber teks | Pemakaian |
| --- | --- | --- |
| C++ | `Classes/Data/UiText.h` | Key/catalog sederhana untuk menu, mode, gear, common labels, dan loading tips. Screen code memanggil `UiText::common`, `UiText::modeTitle`, `UiText::gearName`, atau fungsi katalog terkait. |
| Lua | `lua/utils/localization.lua` | `ns.text.values`, `ns.text.get`, `ns.text.label`, dan `ns.text.keyMenuItem` untuk membuat label/menu item BMFont. File di-load dari `lua/config.lua` sebelum `ui.init`. |
| Font | `Resources/Fonts/1.fnt` | Font bitmap runtime untuk copy Latin, angka, dan tanda baca yang dipakai fase ini. CJK text belum dipindahkan ke BMFont ini. |

Catalog saat ini berisi English copy. Struktur key-based dipertahankan supaya language map dapat dipisah kemudian; jangan menambahkan string UI baru tersebar di screen code jika key yang sesuai dapat ditambahkan ke catalog.

## Layar yang sudah memakai runtime text

| Area | Migrasi fase ini | Asset visual yang dipertahankan |
| --- | --- | --- |
| Intro | `PUSH START` dibuat sebagai `CCMenuItemLabel` Lua. | `logo.png`, `logo2.png`, `zakume.png`, dan intro artwork. |
| Start menu | Menu selection text dan hardcore confirmation dibuat dinamis. | Menu button artwork, title/logo, background, dan decorative bars. |
| Game mode | Mode names, title, dan return dibuat dinamis. | Background, bars, lock chains, posisi/dimensi tombol. Mode PNG text-bearing tidak lagi dirender sebagai tombol. |
| Gear shop | Gear name/description dan `BUY` dibuat dinamis. | Gear icons, shop background, purchase state icon, dan atlas asli `Gears.png`/`Gears.plist`. |
| Pause | Pause controls, confirmation, dan state `ON/OFF` dibuat dinamis. | Snapshot gameplay, overlay, confirmation background, dan menu bars. |
| Game over | Result title, rank/defeat, clock/reward labels, dan exit confirmation dibuat dinamis. | Snapshot, result/list/time/coin frames, hero/gear artwork, dan close button decoration. |
| Loading | Loading title/tips dibuat dinamis. | Loading background dan tip area layout. |
| Character select | Title, hero name, player/team labels, ranking/start/skills/return dibuat dinamis. | Portrait, select frame, page controls, clouds, gold, background, dan decorative UI. |
| Skill record | Record title, return, dan explanation scaffold dibuat dinamis. | Hero portraits, skill frame, clipping mask, scroll UI, dan decorative record UI. |
| Credits | Title, body copy, dan return dibuat dinamis. | Background, clouds, and menu bars. |

## Sengaja belum dihapus

Unused text-bearing PNG/atlas entries tetap disimpan untuk keamanan rollback dan kompatibilitas resource loader. `Record2.plist` masih di-load oleh `SelectLayer` karena skill descriptions asli belum ditranskripsi secara lengkap dan akurat. `SkillLayer` memakai copy runtime sementara untuk menjaga alur interaksi, bukan klaim bahwa seluruh deskripsi karakter sudah diterjemahkan.

Visual battle announcements, killstreak graphics, Ougis/effect artwork, character labels that are part of illustration, and brand/logo images tetap PNG karena teksnya menyatu dengan art atau membutuhkan transkripsi/komposisi terpisah. Area tersebut harus dimigrasikan pada fase berikutnya dengan data per-hero/per-skill yang direview, bukan dengan mengarang copy generik.

## Aturan implementasi

1. Pertahankan posisi, anchor, scale, dan ukuran hit area lama saat mengganti sprite text dengan label.
2. Gunakan `CCLabelBMFont`/`CCMenuItemLabel` untuk copy interaktif C++ dan `ns.text` untuk copy Lua.
3. Jangan mengedit `Gears.png`, plist atlas, logo, portrait, atau artwork hanya untuk migrasi text runtime.
4. Jika string membutuhkan line wrap, set width dan `setLineBreakWithoutSpace(true)` setelah membuat label.
5. Setiap penambahan language map harus mempertahankan key yang sama di C++ dan Lua atau mendokumentasikan perbedaannya.
6. Build Android dan smoke test scene harus dijalankan sebelum PR dianggap siap. Karena project memakai legacy Cocos2d-x/NDK, static checks saja tidak cukup untuk menjamin binding dan layout runtime.

## Status validasi

Validasi lokal terbatas pada diff/whitespace checks dan inspeksi referensi. Environment ini tidak memiliki compiler Android project atau parser Lua command-line yang siap dipakai; build final harus dilakukan oleh GitHub Actions pada branch Pull Request. Hasil Actions wajib dicantumkan pada review PR. Master tidak boleh diubah langsung dan PR ini tidak boleh di-merge otomatis.
