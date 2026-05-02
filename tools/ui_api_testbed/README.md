# UI API Testbed Hot Reload Boundary

Debug builds run `ui_api_testbed.exe` as a stable host and load `ui_api_testbed_module.dll` as the reloadable app module.

Reloadable files watched by the debug host:

- `tools/ui_api_testbed/app.cpp`
- `tools/ui_api_testbed/app.h`
- `tools/assets/ui_api_testbed_texture.png`

Changing one of those files makes the running host execute:

```bat
cmake --build <build-dir> --config Debug --target ui_api_testbed_module --parallel
```

If the build succeeds, the host copies and loads the new DLL, then destroys the previous module runtime.

Files that are intentionally not watched require rebuilding and restarting the host:

- `tools/ui_api_testbed/host.cpp`
- `tools/ui_api_testbed/module_loader.*`
- `tools/ui_api_testbed/shared.h`
- `tools/ui_api_testbed/trace.*`
- `CMakeLists.txt`

The module owns UI state, theme setup, textures, font/cache/draw runtime, and frame command generation. The host owns the process, Win32 window, input, render context/window, presentation loop, tracing, and rebuild/reload control.

Hot reload is compiled only for Windows debug builds (`BASE_DEBUG`). Release builds link the app module code directly and do not load or rebuild DLLs.
