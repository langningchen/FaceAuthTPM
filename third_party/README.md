# Third-party components

FaceAuthTPM itself is licensed under GNU GPL v3.0 only. Release builds also redistribute third-party components under their own licenses:

- OpenCV runtime libraries: Apache License 2.0. CI copies all vcpkg package `copyright` files into the installer, including OpenCV and transitive runtime dependencies.
- YuNet face detector model: MIT License. `scripts/fetch-models.ps1` downloads the upstream license as `YuNet-LICENSE.txt`.
- SFace face recognition model: Apache License 2.0. `scripts/fetch-models.ps1` downloads the upstream license as `SFace-LICENSE.txt`.
- Microsoft Visual C++ Redistributable may be bundled in the installer and remains subject to Microsoft's redistributable terms.

The third-party licenses do not change the GPL license of FaceAuthTPM's own source code.
