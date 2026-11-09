# Release checklist

Every binary release must be traceable to tested source.

1. Set `project(pbAudioStats VERSION ...)` in `CMakeLists.txt` and update both README version tables.
2. Record the full source commit with `git rev-parse HEAD` and require a clean `git status --short` for the release build.
3. Record generator, compiler and version, target architecture, build type, CMake version, and all non-default `PB_*` options. Windows release builds use `build_win.bat Release` and `PB_USE_STATIC_MSVC_RUNTIME=ON`.
4. Configure a clean directory, build, and run `ctest --test-dir <build> -C Release --output-on-failure`. Do not publish if required tests or reference checks fail.
5. Package the executable, library, public header, CMake package files, README files, and `THIRD_PARTY_NOTICES.md`.
6. Generate SHA-256 checksums. PowerShell: `Get-FileHash -Algorithm SHA256 <artifact>`. macOS/Linux: `shasum -a 256 <artifact>`.
7. Publish a manifest containing version, full commit, UTC build time, build conditions, test result, artifact filename, byte size, and SHA-256.

Platform configurations not actually executed for the release must be labeled unverified rather than implied by the manifest. GitHub Actions is not used; release checks are run explicitly on each target platform.
