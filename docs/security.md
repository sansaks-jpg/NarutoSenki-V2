# Security dan Supply Chain

## Kesimpulan audit statis

Audit source tidak menemukan pemanggilan langsung `system()`, `popen()`, `execve()`, `fork()`, `dlopen()`, `chmod()`, atau `setuid()` pada source game/tooling yang relevan. Tidak terlihat boot persistence, root escalation, atau Android service mencurigakan. Namun kesimpulan ini adalah **audit statis**, bukan jaminan bebas malware.

## Risiko yang ditemukan

| Risiko | Dampak | Mitigasi |
|---|---|---|
| Binary prebuilt | `.exe`, `.dll`, `.so`, `.a`, dan tool tertutup tidak dapat diaudit seperti source. | Rebuild dari source bila memungkinkan; jalankan tool hanya di sandbox/VM. |
| Signing key tracked | `debug.jks`, `release.jks`, dan konfigurasi password pernah dilacak. | Anggap credential terekspos; rotasi key/password dan pindahkan secret ke local/CI secret. |
| URL server legacy | `Classes/Data/Version.h` berisi `https://game.naruto.re`; source lama juga memiliki referensi WebSocket iOS. | Audit semua endpoint sebelum release; gunakan TLS, allowlist, dan jangan kirim credential lokal. |
| Dependency legacy | Cocos2d-x/LuaJIT/prebuilt library tua dapat memiliki vulnerability atau reproducibility rendah. | Pin version/hash, rebuild clean, dan review perubahan dependency. |
| APK pihak ketiga | APK hasil download dapat berbeda dari source repository. | Jangan install di device utama; verifikasi hash/signature dan permission final APK. |
| Tidak ada lisensi metadata | Hak redistribusi source/asset tidak jelas. | Pertahankan kredit dan dapatkan izin sebelum publikasi/distribusi. |

## Binary inventory

Repository melacak tool Windows seperti `tools/7z/7z.exe`, `tools/7z/7z.dll`, `tools/dos2unix.exe`, dan `tools/tolua++.exe`, LuaJIT runtime native, prebuilt graphics/audio/network library, serta Gradle wrapper jar. Keberadaan binary tersebut wajar untuk project legacy, tetapi setiap binary adalah trust boundary. Jangan mengganti binary dengan hasil download acak.

## Build aman

Gunakan clone bersih dan environment terisolasi. Review `git diff`, hash binary, dan command build sebelum menjalankan. Untuk pertama kali, build tanpa akses network setelah dependency tersedia. Jika Gradle harus mengunduh dependency, gunakan cache/repository yang dipercaya dan catat artifact/version. Jalankan hasil binary pada VM/emulator tanpa data pribadi.

Pada Windows, jangan menjalankan `.exe` dari repository di host utama sebelum memverifikasi provenance. Pada Android, gunakan emulator atau device test tanpa akun/data sensitif. Jika menguji APK, periksa permission, endpoint, certificate, native library, dan perilaku network secara terpisah.

## Secrets dan signing

Jangan menaruh password, token, API key, private key, atau keystore baru di repository. `gradle.properties` lokal/CI harus mengambil secret dari environment atau secret manager. Key yang pernah masuk Git harus dianggap compromised walaupun repository sekarang private; private visibility tidak menghapus riwayat clone atau salinan yang mungkin sudah ada.

## Review perubahan berisiko

Perubahan berikut membutuhkan review tambahan: update prebuilt library, perubahan `AndroidManifest.xml`, penambahan permission, perubahan URL/HTTP client, native loading, script loader, Lua encryption/obfuscation, file extraction, process execution, signing config, atau asset yang berasal dari APK pihak ketiga.

## Referensi

[1]: ../projects/NarutoSenki/proj.android-studio/app/AndroidManifest.xml "Android manifest and permissions"
[2]: ../projects/NarutoSenki/proj.android-studio/gradle.properties "Android ABI and signing configuration"
[3]: ../projects/NarutoSenki/Classes/Data/Version.h "Server URL configuration"
[4]: ../Doc/README.md "Legacy note about removed network code"
[5]: ../.gitignore "Generated output and binary ignore rules"
