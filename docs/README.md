# Dokumentasi NarutoSenki-V2

Folder ini berisi dokumentasi terstruktur untuk project NarutoSenki-V2. `AGENTS.md` di root adalah aturan kerja singkat untuk agent dan kontributor; dokumen di sini menyimpan penjelasan arsitektur dan workflow yang lebih lengkap.

> **Status dokumentasi:** dibuat dari inspeksi source tree, konfigurasi build, entry point, UI, mode game, resource, serta sistem runtime pada branch `master`.

## Peta dokumentasi

| Dokumen | Isi utama | Kapan dibaca |
|---|---|---|
| [architecture.md](architecture.md) | Struktur repository, layer engine, C++, Lua, platform, dan dependency. | Sebelum refactor atau menambah subsistem. |
| [runtime-flow.md](runtime-flow.md) | Urutan startup, scene transition, initialization, battle lifecycle, dan game over. | Saat mengubah startup, scene, loading, atau cleanup. |
| [ui-workflow.md](ui-workflow.md) | Seluruh layar dan elemen UI yang dapat diklik dari menu sampai battle. | Saat mengubah menu, selection, HUD, pause, atau gear. |
| [gameplay-modes.md](gameplay-modes.md) | Enum mode, handler, roster, team, map, tower, flog, clone, dan win condition. | Saat menambah/mengubah mode permainan. |
| [assets-and-data.md](assets-and-data.md) | Resource atlas, audio, map, metadata unit, parser TOML, SQLite, dan naming. | Saat menambah karakter, skill, map, audio, atau konfigurasi. |
| [build-and-platforms.md](build-and-platforms.md) | Build Linux, Android, Windows, macOS, iOS, ABI, NDK, Gradle, dan generated assets. | Sebelum build atau memperbaiki error platform. |
| [development-guide.md](development-guide.md) | Workflow kontribusi, titik extension, debugging, testing manual, dan checklist. | Untuk pekerjaan harian dan code review. |
| [multiplayer-roadmap.md](multiplayer-roadmap.md) | Rancangan lobby, protocol, deterministic simulation, server authority, dan tahapan implementasi. | Saat mulai mengembangkan online multiplayer. |
| [security.md](security.md) | Risiko supply chain, binary prebuilt, signing key, permission, dan cara menjalankan build secara aman. | Sebelum menjalankan binary atau mendistribusikan APK. |
| [release-build.md](release-build.md) | GitHub Actions release build, tag, artifact, dan download APK. | Saat menerbitkan APK otomatis. |

## Struktur tingkat tinggi

```text
NarutoSenki-V2/
├── projects/NarutoSenki/
│   ├── Classes/                 # C++ gameplay, scene, UI, systems
│   ├── lua/                    # bootstrap, framework, UI, utility Lua
│   ├── Resources/              # audio, map, atlas, metadata, font
│   └── proj.*                  # Android, Linux, Windows, macOS, iOS
├── cocos2dx/                   # vendored Cocos2d-x engine
├── scripting/lua/              # LuaJIT dan Cocos2d-x Lua bridge
├── extensions/                 # CocoStudio/GUI extension
├── CocosDenshion/              # audio layer
├── external/                   # fmt, GLFW, SQLite3, TOML, dan prebuilt libs
├── tools/                      # tolua++, 7z, dos2unix, helper script
├── Doc/                        # dokumentasi legacy dari source asal
└── docs/                       # dokumentasi terstruktur saat ini
```

## Aturan membaca source

Dokumen ini menjelaskan perilaku berdasarkan source yang ada, bukan kontrak API stabil. Jika source dan dokumentasi berbeda, gunakan source pada commit yang sedang dikerjakan sebagai sumber kebenaran, lalu perbarui dokumen terkait. Untuk alur UI, mulai dari [runtime-flow.md](runtime-flow.md) dan [ui-workflow.md](ui-workflow.md); untuk perubahan gameplay, lanjutkan ke [gameplay-modes.md](gameplay-modes.md) dan [assets-and-data.md](assets-and-data.md).

## Referensi internal

[1]: ../AGENTS.md "Panduan agent di root"
[2]: ../Doc/README.md "Dokumentasi legacy NarutoSenki-V2"
[3]: ../projects/NarutoSenki/Classes "Source C++ game"
[4]: ../projects/NarutoSenki/lua "Source Lua game"
[5]: ../projects/NarutoSenki/Resources "Resource game"

Dokumen ini terutama diringkas dari [AGENTS.md][1], [Doc/README.md][2], [source C++][3], [source Lua][4], dan [resource game][5].
