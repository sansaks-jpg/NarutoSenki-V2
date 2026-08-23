# Assets, Data, dan Resource Pipeline

## Sumber asset runtime

Semua asset game utama berada di `projects/NarutoSenki/Resources`. Pada Android, Gradle menyalin `Resources` ke `app/assets`; Lua disalin ke `app/assets/lua`. Folder hasil salinan adalah generated output, sedangkan source-of-truth tetap berada di `projects/NarutoSenki/Resources` dan `projects/NarutoSenki/lua`.

| Direktori | Isi |
|---|---|
| `Audio` | BGM dan voice/effect `.ogg`, termasuk menu, intro hero, skill, hurt, dead, dan battle effect. |
| `Config` | `strings.xml` dan konfigurasi yang dibaca runtime. |
| `Effects` | Particle/effect plist dan sprite frame. |
| `Fonts` | `.fnt` dan texture font untuk label bitmap. |
| `GameMode` | Gambar tombol/label mode. |
| `Maps`/`Tiles` | Map, tile atlas, `.tmx`, dan data posisi/visual map. |
| `UI` | Sprite tombol, frame, HUD, pause, gear, report, selection, dan icon. |
| `Unit` | Sprite, plist, skill atlas, metadata, dan asset hero/guardian/summon/flog/tower. |
| `*.plist` root | Atlas/record/map/gear/result/select/UI metadata. |

## Atlas dan sprite frame

Lua dan C++ memanggil `addSpriteFramesWithFile`/`tools.addSprites` untuk mendaftarkan atlas. Nama frame adalah API runtime tidak tertulis; contoh pola meliputi `Hero_select.png`, `Hero_half.png`, `Hero_full.png`, `Hero_font.png`, `Hero_skill1.png`, `gear_00.png`, `*_btn.png`, serta icon map. Mengganti nama frame tanpa memperbarui call site akan menghasilkan asset missing saat runtime.

Untuk menambah asset UI, perbarui atlas/plist dengan tool asset yang sesuai, pastikan semua frame yang dipanggil tersedia, lalu uji pada Android karena case sensitivity path berbeda dari beberapa filesystem desktop.

## Metadata unit dan Parser

`Classes/Core/Utils/Parser.hpp` membaca metadata TOML menjadi `UnitMetadata`. Field yang digunakan mencakup nama, HP, speed, defense, attack type/value/range, cooldown, combat point, animation frame/fps/loop/return-to-idle, dan event action. Parser menyediakan default untuk sebagian field dan dapat memakai cache.

Alur data unit adalah:

```text
Resources/Unit/<category>/<Hero>/metadata
  -> Parser::fromToml
      -> UnitMetadata / action data
          -> CharacterBase/Hero
              -> animation, attack, cooldown, UI, runtime
```

Kelas hero tetap menjadi sumber perilaku khusus skill. Metadata tidak menggantikan kode C++ untuk efek kompleks, summon, transform, atau interaksi state.

## Audio

Audio memakai `.ogg` melalui CocosDenshion. Nama file dipanggil dari kode hero, `ActionButton`, menu, dan Lua. `UserDefault` menyimpan `isBGM`, `isVoice`, dan `isPreload`. Jika menambah voice/skill, gunakan pola penamaan hero yang sudah ada dan pastikan audio dipreload pada `LoadLayer` bila diperlukan.

## Map dan tile

Map dibuat/diedit dengan Tiled sesuai dokumentasi legacy. Data tile dan map harus menjaga ukuran grid, spawn point, collision/boundary, tower position, dan background. Uji camera/viewpoint serta minimap setelah mengubah map; `MiniIcon::updatePosition()` mengonversi posisi world ke posisi minimap.

## Save dan local data

`MyUtils/KTools.cpp` menggunakan SQLite3 untuk `GameRecord`, `CharRecord`, coin, win count, dan best time. Data save bersifat lokal; tidak ada account/backend yang menjadi sumber kebenaran. Perubahan schema/query harus backward-compatible atau memiliki migration yang jelas. Hindari menyusun query dari input jaringan tanpa binding/validasi bila multiplayer ditambahkan.

## Naming contract

| Jenis | Pola umum |
|---|---|
| Hero atlas | `<Hero>.plist`, `<Hero>_Skill.plist`, dan frame `<Hero>_skillN.png`. |
| Character selection | `<Hero>_select.png`, `<Hero>_small.png`, `<Hero>_half.png`, `<Hero>_full.png`, `<Hero>_font.png`. |
| Skill explanation | `<Hero>_labelN.png`. |
| Gear | `gear_NN.png`, `gearDetail_NN.png`, dan `Gear*.plist`. |
| Button | `<name>_btn.png`, selected state sering memakai suffix `2`. |
| Audio | `Audio/<category>/<Hero>_<event>.ogg`. |
| Font | `.fnt` merujuk texture page dengan path relatif yang harus tetap benar. |

## Asset checklist

Sebelum commit asset, pastikan file benar-benar berada di folder source, plist/atlas memuat frame, call site memakai case yang tepat, file tidak hanya ada di `app/assets`, ukuran dan format sesuai target, serta asset dapat dimuat pada clean build.

## Referensi

[1]: ../projects/NarutoSenki/Resources "Runtime resources"
[2]: ../projects/NarutoSenki/Classes/Core/Utils/Parser.hpp "Unit metadata parser"
[3]: ../projects/NarutoSenki/Classes/MyUtils/KTools.cpp "SQLite save helpers"
[4]: ../projects/NarutoSenki/Classes/HudLayer.cpp "HUD and minimap asset usage"
[5]: ../Doc/README.md "Legacy asset development documentation"
