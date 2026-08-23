# Automated APK Release

Workflow [`../.github/workflows/release-apk.yml`](../.github/workflows/release-apk.yml) membangun `assembleRelease`, mengunggah APK dan SHA-256 sebagai Actions artifact, lalu—jika diminta—menerbitkan keduanya sebagai asset GitHub Release. Pada branch `feature/lan-hotspot-multiplayer`, workflow ini adalah jalur build APK yang canonical karena project memakai Gradle/NDK legacy.

## Aturan branch dan tag

Fitur LAN dan hotfix-nya harus dibangun dari branch `feature/lan-hotspot-multiplayer`; jangan membuat commit release ke `main` atau `master`. Tag release harus unik dan menunjuk ke commit branch yang sudah diverifikasi.

Release yang sudah dibuat untuk fitur LAN adalah:

| Release | Commit dasar | Isi |
|---|---|---|
| [`v2.1.0-lan-mvp`](https://github.com/sansaks-jpg/NarutoSenki-V2/releases/tag/v2.1.0-lan-mvp) | `2241721` | LAN MVP dan perbaikan Android NDK `LOCAL_PATH`. |
| [`v2.1.1-lan-hotfix`](https://github.com/sansaks-jpg/NarutoSenki-V2/releases/tag/v2.1.1-lan-hotfix) | `7a68897` | Perbaikan Back Network, GameOver, dan beban idle worker. |
| [`v2.1.2-lan-optin`](https://github.com/sansaks-jpg/NarutoSenki-V2/releases/tag/v2.1.2-lan-optin) | `f5117d7` | LAN hanya aktif setelah Host/Join; offline tidak melakukan polling LAN. |

## Menjalankan workflow secara manual

Dari repository yang sudah memiliki workflow dan branch fitur terbaru, jalankan:

```bash
export TERM=dumb
gh workflow run release-apk.yml \
  --repo sansaks-jpg/NarutoSenki-V2 \
  --ref feature/lan-hotspot-multiplayer \
  -f release_tag=v2.1.3-lan-next \
  -f create_release=true
```

Input `release_tag` menentukan nama tag/release, sedangkan `create_release=true` membuat release published setelah build berhasil. Jika `create_release=false`, APK hanya tersedia sebagai Actions artifact dengan retention terbatas. Jangan memakai tag yang sudah ada.

Status workflow dapat diperiksa dengan:

```bash
gh run list --repo sansaks-jpg/NarutoSenki-V2 \
  --workflow release-apk.yml \
  --branch feature/lan-hotspot-multiplayer

gh run view <RUN_ID> --repo sansaks-jpg/NarutoSenki-V2
```

Jika build gagal, ambil log langkah yang gagal sebelum mengulang. Build Android sebelumnya membutuhkan `LOCAL_PATH := $(call my-dir)` sebagai baris pertama `app/jni/Android.mk`; perubahan build seperti ini harus tetap berada di branch fitur dan diuji ulang melalui workflow.

## Verifikasi asset release

Release yang berhasil harus berstatus published, bukan draft, dan memiliki dua asset: APK serta file `.sha256`. Download asset lalu hitung hash APK secara lokal. File checksum workflow dapat memuat absolute path milik runner GitHub, sehingga validasi portable sebaiknya membandingkan hash pertama pada file dengan hash hasil lokal:

```bash
mkdir -p /tmp/nsv2-release
cd /tmp/nsv2-release
gh release download v2.1.2-lan-optin \
  --repo sansaks-jpg/NarutoSenki-V2 \
  --dir .

apk='NarutoSenki-v2.1.2-lan-optin.apk'
expected=$(awk '{print $1}' "$apk.sha256")
actual=$(sha256sum "$apk" | awk '{print $1}')
test "$expected" = "$actual"
printf 'SHA-256: %s\n' "$actual"
```

Setelah checksum cocok, APK tetap harus diuji pada emulator atau device Android. Pemeriksaan penting untuk release LAN adalah: offline/Training tetap ringan, Network Home dapat dibuka dan di-Back, Host/Join mengaktifkan jaringan, dua device hotspot dapat menemukan atau menghubungi room manual, battle 1v1 berjalan, GameOver tidak force close, dan worker/socket berhenti setelah keluar.

## Toolchain legacy

Workflow menggunakan Java 8 dan NDK r17c karena project memakai Android Gradle Plugin 3.3.3, Gradle 5.6.4, `compileSdkVersion 31`, dan native build legacy. Clang dari NDK r17c membutuhkan `libtinfo5` pada runner Ubuntu; workflow memasang dependency tersebut sebelum `ndk-build` dijalankan. `build.gradle` memakai `NDK_TOOLCHAIN_VERSION=clang`.

## Signing dan keamanan

Jangan menaruh password, token, private key, atau keystore baru di YAML. Repository lama pernah melacak signing configuration; untuk distribusi nyata, pindahkan signing ke GitHub Actions Secrets dan rotasi key yang pernah terekspos. APK release harus diuji pada emulator/device test sebelum dibagikan.

## Checklist

| Item | Pemeriksaan |
|---|---|
| Branch | Workflow berjalan dari `feature/lan-hotspot-multiplayer`, bukan `main`/`master`. |
| Commit | Commit source dan Android.mk sudah diverifikasi serta pushed. |
| Tag | Tag baru, unik, dan menunjuk ke commit yang dimaksud. |
| Build | Job `Build release APK` berhasil. |
| Asset | APK dan `.sha256` muncul pada Release published. |
| Package | Application id, version, dan ABI sesuai target. |
| LAN | Offline tidak membuka socket/polling; Host/Join tetap mengaktifkan LAN. |
| Security | Secret tidak muncul pada log atau commit. |
| Integrity | SHA-256 dicatat dan diverifikasi setelah download. |
| Device | APK diuji pada Android nyata/emulator untuk alur Back, battle, dan GameOver. |

## Referensi

[1]: ../.github/workflows/release-apk.yml "GitHub Actions APK release workflow"
[2]: ../projects/NarutoSenki/proj.android-studio/app/build.gradle "Android native build configuration"
[3]: ../projects/NarutoSenki/proj.android-studio/app/jni/Android.mk "Android native source registration"
[4]: ../projects/NarutoSenki/proj.android-studio/gradle/wrapper/gradle-wrapper.properties "Gradle wrapper configuration"
[5]: ../projects/NarutoSenki/proj.android-studio/gradle.properties "Android project properties"
[6]: lan-multiplayer.md "LAN behavior and device test procedure"
