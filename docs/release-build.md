# Automated APK Release

Workflow [`../.github/workflows/release-apk.yml`](../.github/workflows/release-apk.yml) membangun `assembleRelease`, mengunggah APK dan SHA-256 sebagai Actions artifact, lalu menerbitkan APK sebagai asset GitHub Release.

## Cara membuat release dengan tag

```bash
git tag v2.1.0
git push origin v2.1.0
```

Push tag dengan pola `v*` menjalankan workflow dan membuat Release. Setelah workflow selesai, APK akan tersedia pada halaman **Releases** sebagai file download `NarutoSenki-v2.1.0.apk`, bersama file `.sha256` untuk verifikasi checksum.

## Cara menjalankan manual

Buka tab **Actions**, pilih **Build and Release APK**, lalu tekan **Run workflow**. Isi `release_tag`, misalnya `v2.1.0`, dan set `create_release` menjadi `true` untuk menerbitkan Release. Jika `create_release` false, APK hanya muncul sebagai Actions artifact dengan retention terbatas.

## Toolchain legacy

Workflow menggunakan Java 8 dan NDK r17c karena project memakai Android Gradle Plugin 3.3.3, Gradle 5.6.4, `compileSdkVersion 31`, dan native build legacy. Clang dari NDK r17c membutuhkan `libtinfo5` pada runner Ubuntu; workflow memasang dependency tersebut sebelum `ndk-build` dijalankan. `build.gradle` memakai `NDK_TOOLCHAIN_VERSION=clang`.

## Signing dan keamanan

Jangan menaruh password, token, private key, atau keystore baru di YAML. Repository lama pernah melacak signing configuration; untuk distribusi nyata, pindahkan signing ke GitHub Actions Secrets dan rotasi key yang pernah terekspos. APK release harus diuji pada emulator/device test sebelum dibagikan.

## Checklist

| Item | Pemeriksaan |
|---|---|
| Tag | Menggunakan pola `v*`, misalnya `v2.1.0`. |
| Build | Job `Build release APK` berhasil. |
| Asset | APK dan `.sha256` muncul di Release. |
| Package | Application id, version, dan ABI sesuai target. |
| Security | Secret tidak muncul pada log atau commit. |
| Integrity | SHA-256 dicatat dan diverifikasi setelah download. |

## Referensi

[1]: ../.github/workflows/release-apk.yml "GitHub Actions APK release workflow"
[2]: ../projects/NarutoSenki/proj.android-studio/app/build.gradle "Android native build configuration"
[3]: ../projects/NarutoSenki/proj.android-studio/gradle/wrapper/gradle-wrapper.properties "Gradle wrapper configuration"
[4]: ../projects/NarutoSenki/proj.android-studio/gradle.properties "Android project properties"
