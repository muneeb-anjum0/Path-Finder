#pragma once

#include "glad/glad.h"
#include "GLFW/glfw3.h"
#include "imgui.h"
#include "imgui/backends/imgui_impl_glfw.h"
#include "imgui/backends/imgui_impl_opengl3.h"
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"

#include <vector>
#include <stack>
#include <random>
#include <iostream>
#include <queue>
#include <functional>
#include <tuple>
#include <algorithm>
#include <set>
#include <map>
#include <limits>
#include <unordered_map>
#include <fstream>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <cstdarg>
#ifdef _WIN32
#include <windows.h>
#endif
#include <cstring>

// STB Image
#include "stb_image.h"

// Global variables
extern GLFWwindow *gWindow;
extern int gCols;
extern int gRows;

// Cell structure
struct Cell
{
    bool visited = false;
    bool walls[4] = {true, true, true, true}; // top, right, bottom, left
    bool blocked = false;
};

extern std::vector<Cell> grid;

// Animation and solving state
extern bool solving;
extern int animState;
extern double animStartTime, animEndTime;
extern int solveAlgo;
extern int genAlgo;
extern std::vector<std::tuple<int, int, bool, float>> events;
extern std::vector<std::pair<int, int>> finalPathEdges;
extern std::vector<float> successVertices;
extern std::vector<float> failureVertices;
extern size_t eventIndex;
extern bool stepMode;

// OpenGL buffers and objects
extern std::vector<float> wallVertices;
extern unsigned int wallVAO, wallVBO, borderVAO, borderVBO;
extern unsigned int successVAO, successVBO, failureVAO, failureVBO;
extern int startCell, endCell;
extern std::mt19937 rng;
extern unsigned int shader;
extern glm::mat4 proj;

// Settings
extern float speedMultiplier;
extern float obstacleDensity;

// Textures
extern GLuint texWall;
extern GLuint texLineHori;
extern GLuint texLineVerti;
extern GLuint texPlay, texPause, texRegen, texSettings, texStep;
extern GLuint texBackground;
extern GLuint texStart;
extern GLuint texEnd;
extern GLuint texObstacle;
extern GLuint texSplashScreen;

// Splash screen state
extern bool showSplashScreen;
extern float splashScreenOffset;
extern double splashScreenStartTime;
extern bool splashScreenAnimating;

// Utility functions
inline int indexXY(int x, int y, int C, int R)
{
    if (x < 0 || y < 0 || x >= C || y >= R)
        return -1;
    return x + y * C;
}

inline int index(int x, int y) { return indexXY(x, y, gCols, gRows); }