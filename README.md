# Path Finder (OpenGL)

A modern C++ application for visualizing and experimenting with pathfinding algorithms in real time, using OpenGL for rendering and ImGui for a user-friendly interface. Built for education, experimentation, and demonstration.

---

## ✨ Features

- **Interactive Visualization:** Step through pathfinding algorithms (A*, Dijkstra, and more).
- **Intuitive GUI:** Built with ImGui and GLFW for easy interaction.
- **Customizable Grid:** Set grid size, obstacles, and start/end points visually.
- **Real-Time Controls:** Pause, resume, step, and reset the algorithm at any time.
- **Cross-Platform Ready:** Designed for Windows, easily portable to other platforms.

---

## 🗂️ Project Structure

```
include/   # All header files (GLFW, GLAD, ImGui, GLM, stb_image, etc.)
lib/       # Precompiled libraries (e.g., glfw3dll)
src/       # Source files (main.cpp, ImGui backends, glad.c, etc.)
OpenGL.exe # Compiled executable (Windows)
glfw3.dll  # GLFW runtime DLL
imgui.ini  # ImGui configuration
```

---

## 🚀 Getting Started

### Prerequisites
- C++17 or later
- OpenGL-compatible GPU and drivers
- All dependencies (GLFW, GLAD, ImGui, GLM, stb_image) are included in `include/` and `lib/`

### Build & Run

**Recommended (VS Code):**
1. Open the project in VS Code.
2. Use the build task: `C/C++: gcc.exe build OpenGL` (Ctrl+Shift+B).
3. Run the app with the `Run OpenGL` task or double-click `OpenGL.exe`.

**Manual (MSYS2/MinGW Example):**
```sh
C:\msys64\ucrt64\bin\g++.exe -fdiagnostics-color=always -g src/main.cpp src/glad.c src/imgui.cpp src/imgui_draw.cpp src/imgui_tables.cpp src/imgui_widgets.cpp src/imgui_demo.cpp src/imgui_impl_glfw.cpp src/imgui_impl_opengl3.cpp -o OpenGL.exe -Iinclude -Iinclude/imgui -Llib -lglfw3dll -lopengl32
```

---

## 🕹️ Controls
- Use the GUI to set start/end points, place obstacles, and select algorithms.
- Step, pause, or reset the visualization as needed.

---

## 🙏 Credits
- [ImGui](https://github.com/ocornut/imgui)
- [GLFW](https://www.glfw.org/)
- [GLAD](https://glad.dav1d.de/)
- [GLM](https://github.com/g-truc/glm)
- [stb_image](https://github.com/nothings/stb)

---

## 📄 License

This project is licensed under the MIT License. See [LICENSE](LICENSE) for details.

---

## 📬 Contact
For questions, suggestions, or contributions, feel free to reach out:

**Muneeb Anjum**  
muneeb.anjum0@gmail.com
