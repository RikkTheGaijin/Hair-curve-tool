# Hair Tool (MVP)

Standalone interactive hair-guide authoring tool:
- Maya-like viewport navigation (Alt+LMB orbit, Alt+MMB pan, Alt+RMB dolly)
- Import head mesh (`.obj`)
- Click on mesh to spawn a guide curve (root bound to triangle via barycentric coords)
- Drag curve vertices
- Save/Load scene (`.json`)
- Export curves as `.ply` point cloud

## Tech
- C++20, CMake
- OpenGL 3.3+, GLFW + GLAD
- Dear ImGui (Docking)
- Assimp for OBJ import
- JsonCpp for scene serialization

This repo is structured to later plug in **YarnBall**-style GPU physics (CUDA/OpenGL interop). The initial MVP ships with a fast CPU XPBD solver so the app runs without CUDA.

## Build (Windows)

### 1) Install prerequisites
- Visual Studio 2022 (Desktop development with C++)
- CMake 3.24+
- Git
- (Recommended) vcpkg

### 2) vcpkg (manifest mode)
This project uses `vcpkg.json`. You can use vcpkg manifest mode:

```powershell
git clone https://github.com/microsoft/vcpkg.git C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat
```

### 3) Configure + build
From the project root:

```powershell
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
```

Run:
```powershell
.\build\Release\HairTool.exe
```

## Controls
- **Alt + LMB**: Orbit
- **Alt + MMB**: Pan
- **Alt + RMB**: Dolly
- **LMB (no Alt)**: Interact (spawn/select/drag)

## Notes on YarnBall integration
YarnBall is CUDA/OpenGL-interp based and uses vcpkg deps similar to this project. Once you confirm:
- NVIDIA GPU
- CUDA Toolkit version (YarnBall mentions CUDA 12.8)

…we can add an optional `HAIRTOOL_ENABLE_YARNBALL` build flag and wire curves into a YarnBall-based solver module.
