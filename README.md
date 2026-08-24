# FaceAuthTPM

FaceAuthTPM is an experimental Windows x64 Credential Provider for **Microsoft-account Windows users**. It uses a normal RGB webcam to recognize an enrolled user and then submits that user's Microsoft-account credential. The stored Microsoft-account password is encrypted at rest with a per-user AES-256-GCM key, and that AES key is wrapped by a non-exportable machine RSA key in the TPM.

> **Security warning:** FaceAuthTPM is **not Windows Hello** and does not provide Windows Hello's IR/depth anti-spoofing, Enhanced Sign-in Security, or biometric-key architecture. A normal RGB face matcher can be spoofed more easily. Use this project only if that tradeoff fits your threat model, and keep Windows Password/Hello available as recovery methods.

## Highlights

- Windows 10/11 x64 Credential Provider for Microsoft-account users.
- RGB webcam face recognition using OpenCV YuNet + SFace.
- YuNet/SFace models are preloaded by a small service to reduce sign-in latency.
- TPM-backed, machine-bound, non-exportable RSA key; private-key use is restricted to `SYSTEM`.
- Production builds disable FaceAuth for RDP sessions; normal RDP/NLA authentication remains unchanged.
- Does not filter or remove Microsoft's Password/Windows Hello providers.
- Does not implement `CPUS_CREDUI`; Edge/Windows Hello re-auth dialogs do not use FaceAuth.
- No custom Authentication Package is loaded into LSASS, so PPL/LSA Protection, Credential Guard, Secure Boot and VBS do not need to be disabled.

## Download / release files

Each tagged GitHub Release intentionally contains **exactly two user-facing files**:

1. **`FaceAuthTPM-Setup.exe`** — self-contained x64 installer. It installs the binaries/models, initializes the TPM vault, creates the model-preloader service, and leaves the Credential Provider disabled until the user validates enrollment.
2. **`FaceAuthTPM-Configure.ps1`** — standalone command-line configuration and recovery tool used after installation.

The installer also places a copy of `FaceAuthTPM-Configure.ps1` in `C:\Program Files\FaceAuth`.

## Quick start for end users

Run `FaceAuthTPM-Setup.exe` as Administrator. The installer intentionally **does not enable the Credential Provider automatically**.

Then open **Windows PowerShell as Administrator** in the directory containing `FaceAuthTPM-Configure.ps1`:

```powershell
Set-ExecutionPolicy -Scope Process Bypass

.\FaceAuthTPM-Configure.ps1 status
.\FaceAuthTPM-Configure.ps1 enroll
.\FaceAuthTPM-Configure.ps1 test
.\FaceAuthTPM-Configure.ps1 vault-test
.\FaceAuthTPM-Configure.ps1 enable
```

During `enroll`, enter the account's **actual Microsoft-account password**, not a Windows Hello PIN. Passwordless Microsoft accounts are not supported by this design. If the Microsoft-account password changes later, enroll that user again.

For Microsoft accounts, `enroll` stores identity metadata automatically. If the qualified identity is wrong, repair it with:

```powershell
.\FaceAuthTPM-Configure.ps1 identity -Qualified 'MicrosoftAccount\you@example.com'
```

After `enable`, test with **Win+L** before signing out or rebooting. The built-in Password/Windows Hello sign-in options remain available.

### Camera tuning

Benchmark startup without rebooting:

```powershell
.\FaceAuthTPM-Configure.ps1 camera-benchmark -Backend msmf -Repeat 3 -CooldownMs 5000
```

Save a backend and optional media type:

```powershell
.\FaceAuthTPM-Configure.ps1 camera-set -Backend msmf -Width 2560 -Height 1440 -Fps 30
```

Use native/default format instead:

```powershell
.\FaceAuthTPM-Configure.ps1 camera-set -Backend msmf
```

### Recovery / disable

```powershell
.\FaceAuthTPM-Configure.ps1 disable
```

This unregisters only FaceAuthTPM. It does not modify Windows Password or Windows Hello providers.

### Uninstall and data retention

Normal uninstall preserves `C:\ProgramData\FaceAuth` so an accidental uninstall does not destroy enrollment data. To deliberately erase FaceAuth profiles, TPM-encrypted credential blobs and camera configuration before uninstalling:

```powershell
.\FaceAuthTPM-Configure.ps1 purge-data
```

## What is stored

Under `C:\ProgramData\FaceAuth`:

- `profiles\*.fap` — face embeddings/templates; raw face photographs are not required for normal operation.
- `credentials\*.fav` — AES-GCM encrypted Microsoft-account password blobs plus TPM-wrapped AES keys.
- `identities\*.fai` — non-secret SID / qualified-name / display-name metadata.
- `models\*.onnx` — YuNet and SFace inference models.
- `camera-backend.txt` / `camera-format.txt` — optional camera tuning.

The TPM RSA private key is created through the Microsoft Platform Crypto Provider as a machine key, is marked non-exportable, and is restricted to `SYSTEM`. During a successful sign-in, plaintext password material exists briefly in the Credential Provider process while Windows credential serialization is created and is then wiped by the implementation.

## Known limitations

- RGB-only face recognition is weaker against presentation/spoofing attacks than Windows Hello Face.
- Microsoft-account passwords are still passwords: compromise of a sufficiently privileged local process can attack the custom credential path, and compromise of the password may affect the Microsoft account beyond the PC.
- Local Windows accounts are not supported by the current production Credential Provider; enrollment metadata must identify an online/Microsoft account.
- Passwordless Microsoft accounts are not supported because the current architecture deliberately submits a normal Microsoft-account password credential after face recognition.
- The project does not integrate with Windows Hello's `UserConsentVerifier`; therefore Edge password-manager re-authentication and similar Hello dialogs do not invoke FaceAuthTPM.
- Production builds intentionally refuse RDP/Hyper-V Enhanced Session face authentication.
- A physical fallback sign-in method should always remain configured.

## Building from source

### Requirements

- Windows x64.
- Visual Studio 2026 with Desktop development with C++ and a Windows SDK.
- CMake 4.2+.
- vcpkg.

From **Developer PowerShell for Visual Studio 2026**:

```powershell
$env:VCPKG_ROOT = 'C:\Program Files\Microsoft Visual Studio\18\Community\VC\vcpkg'
Set-ExecutionPolicy -Scope Process Bypass

.\scripts\build.ps1
```

Do **not** use `-AllowRemoteForVm` for a production build.

To create a local installer and the two release assets, install Inno Setup 6 and run:

```powershell
.\scripts\package-release.ps1
```

The output is:

```text
dist\FaceAuthTPM-Setup.exe
dist\FaceAuthTPM-Configure.ps1
```

## GitHub Actions release

`.github/workflows/release.yml` builds on the GitHub-hosted `windows-2025-vs2026` image.

- **Manual run:** Actions → **Build Windows release** → **Run workflow**. The workflow artifact contains the two release files.
- **Publish a GitHub Release:** push a tag beginning with `v`, for example:

```powershell
git tag v0.5.3
git push origin v0.5.3
```

The workflow builds production binaries, downloads and verifies the two ONNX models, creates the Inno Setup installer, and publishes exactly:

```text
FaceAuthTPM-Setup.exe
FaceAuthTPM-Configure.ps1
```

### Optional Authenticode signing

Without code signing, Windows SmartScreen may warn users about a newly downloaded installer. The workflow supports optional installer signing when these repository secrets are configured:

- `WINDOWS_CERTIFICATE_BASE64` — Base64-encoded code-signing PFX.
- `WINDOWS_CERTIFICATE_PASSWORD` — PFX password.

If the secrets are absent, the build still succeeds and publishes an unsigned installer.

## Repository layout

```text
.github/workflows/release.yml  GitHub Actions build/release workflow
installer/FaceAuthTPM.iss      Inno Setup definition
release/FaceAuthTPM-Configure.ps1
scripts/                       build/deploy/developer helpers
src/                           Credential Provider, TPM vault, sensor and model service
models/                        downloaded at build time
third_party/                   upstream license files generated/fetched for release
```

## License

FaceAuthTPM's own source code is licensed under the **GNU General Public License v3.0 only (`GPL-3.0-only`)**. See [`LICENSE`](LICENSE).

The bundled OpenCV runtime, YuNet/SFace model files, and Material Icons asset remain under their upstream licenses. See [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).

## Disclaimer

This project changes the Windows sign-in path and handles authentication secrets. Test updates with an alternate recovery method available. The software is provided without warranty under the terms of the GNU GPL v3.0.
