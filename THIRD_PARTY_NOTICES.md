# Third-party notices

FaceAuthTPM uses and redistributes third-party components. Their licenses remain in force independently of the GNU GPL v3.0 license covering FaceAuthTPM's own source code.

| Component | Use | License |
| --- | --- | --- |
| OpenCV | Camera capture and DNN inference runtime | Apache-2.0 |
| YuNet `face_detection_yunet_2023mar.onnx` | Face detection model | MIT |
| SFace `face_recognition_sface_2021dec.onnx` | Face embedding model | Apache-2.0 |
| Material Icons `face_unlock` | Credential Provider sign-in icon | Apache-2.0 |
| Microsoft Visual C++ Redistributable | MSVC runtime, when bundled by the installer | Microsoft redistributable terms |

`FaceAuthTPM-Setup.exe` produced by GitHub Actions includes the model-specific license files downloaded from the upstream OpenCV Zoo repository. It also collects the `copyright` file for **every package** in the build's `vcpkg_installed/x64-windows/share` tree and installs those notices under `C:\Program Files\FaceAuth\licenses\vcpkg`. This intentionally covers transitive runtime dependencies in addition to OpenCV.

The `face_unlock` icon is sourced from Google's Material Design Icons repository:
<https://github.com/google/material-design-icons/tree/master/src/action/face_unlock>. Material
Icons are licensed under the Apache License 2.0.
