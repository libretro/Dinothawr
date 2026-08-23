# Dinothawr for Android

Gradle project producing the standalone Google Play build. The app
bundles the Dinothawr libretro core (`libretro.so`, built from the
`jni/` directory at the repository root) together with the RetroArch
frontend (`libretroarch-activity.so`) built from an external RetroArch
checkout, and reuses the `com.retroarch` Java sources from that
checkout. A small launcher activity extracts the game data from APK
assets to internal storage, generates `retroarch.cfg` plus the core
options file, and starts `RetroActivityFuture` with the `ROM`,
`LIBRETRO`, `CONFIGFILE` and `DATADIR` intent extras. Passing
`CONFIGFILE` makes the frontend skip its storage-permission flow, so
the app requests no storage permissions at all.

## Requirements

- A RetroArch checkout. The default location is `../RetroArch` next to
  this repository; override with `retroarchDir` in `gradle.properties`
  or `-PretroarchDir=/path/to/RetroArch`.
- Android SDK with platform 36 and build-tools 36.0.0.
- NDK 29.0.14206865 (installed automatically by the SDK manager when
  missing).
- Gradle 9.1 or newer (AGP 9.1.0). Either use a local Gradle
  installation or generate a wrapper with `gradle wrapper`.

## Building

    cd android
    gradle :app:bundleRelease     # AAB for Play submission
    gradle :app:assembleRelease   # APK for local testing

Release signing reads the standard `RELEASE_STORE_FILE`,
`RELEASE_STORE_PASSWORD`, `RELEASE_KEY_ALIAS` and
`RELEASE_KEY_PASSWORD` Gradle properties; without them the release
build is debug-signed for local use. Play submissions are signed by
Play App Signing after upload.

The build ships `arm64-v8a`, `armeabi-v7a` and `x86_64`. Native
libraries are 16 KiB page-size aligned (NDK default, with
`PLAY_STORE_BUILD=1` suppressing RetroArch's 4 KiB sideload override)
and extracted on install (`useLegacyPackaging`) because the frontend
dlopens the core by path.
