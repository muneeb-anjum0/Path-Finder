#include "../headers/common.h"

// Global variables definitions
GLFWwindow *gWindow = nullptr;
int gCols = 20;
int gRows = 20;

std::vector<Cell> grid;

std::vector<float> wallVertices;
unsigned int wallVAO = 0, wallVBO = 0, borderVAO = 0, borderVBO = 0;

bool solving = false;
int animState = 0; // 0 running, 1 done
double animStartTime = 0, animEndTime = 0;
int solveAlgo = 0; // 0 DFS, 1 BFS, 2 Dijkstra, 3 A*
int genAlgo = 0;   // 0 Backtracker, 1 Prim, 2 Kruskal

std::vector<std::tuple<int, int, bool, float>> events;
std::vector<std::pair<int, int>> finalPathEdges;
std::vector<float> successVertices; // pairs of (x,y) points in grid space
std::vector<float> failureVertices;
size_t eventIndex = 0;
bool stepMode = false;

unsigned int successVAO = 0, successVBO = 0, failureVAO = 0, failureVBO = 0;
int startCell = 0, endCell = 0;
std::mt19937 rng(std::random_device{}());

unsigned int shader = 0;
glm::mat4 proj;

float speedMultiplier = 1.0f;
float obstacleDensity = 0.15f;

// Wall and UI icon textures
GLuint texWall = 0; // legacy, unused
GLuint texLineHori = 0;
GLuint texLineVerti = 0;
GLuint texPlay = 0, texPause = 0, texRegen = 0, texSettings = 0, texStep = 0;

GLuint texBackground = 0;
GLuint texStart = 0;
GLuint texEnd = 0;
GLuint texObstacle = 0;
GLuint texSplashScreen = 0;

// Splash screen state
bool showSplashScreen = true;
float splashScreenOffset = 0.0f;
double splashScreenStartTime = 0.0;
bool splashScreenAnimating = false;