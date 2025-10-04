// main.cpp
// Maze-Runner with texture assets + walls/paths drawn as ImGui lines (on top of background).
// Robust logging and init fallbacks.

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

#include <cstring> // for memset

// ======================= LOGGING =========================
static std::ofstream gLog;
static void open_log()
{
    gLog.open("maze_runner.log", std::ios::out | std::ios::trunc);
    if (!gLog)
    {
        fprintf(stderr, "Failed to open maze_runner.log\n");
    }
    else
    {
        time_t t = time(nullptr);
        gLog << "Maze Runner log started " << ctime(&t) << "\n";
        gLog.flush();
    }
}
static void logf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    if (gLog)
    {
        char buf[4096];
        va_list ap2;
        va_copy(ap2, ap);
        vsnprintf(buf, sizeof(buf), fmt, ap2);
        va_end(ap2);
        gLog << buf << "\n";
        gLog.flush();
    }
    va_end(ap);
}
[[noreturn]] static void fatal(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char buf[8192];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    fprintf(stderr, "FATAL: %s\n", buf);
    if (gLog)
    {
        gLog << "FATAL: " << buf << "\n";
        gLog.flush();
    }
#ifdef _WIN32
    MessageBoxA(nullptr, buf, "Maze Runner fatal error", MB_OK | MB_ICONERROR);
    ShellExecuteA(nullptr, "open", "notepad.exe", "maze_runner.log", nullptr, SW_SHOWNORMAL);
#endif
    std::exit(1);
}

// ======================= GLOBALS =========================
static GLFWwindow *gWindow = nullptr;
static int gCols = 20;
static int gRows = 20;

void framebuffer_size_callback(GLFWwindow *, int width, int height)
{
    int sz = std::min(width, height);
    int xoff = (width - sz) / 2;
    int yoff = (height - sz) / 2;
    glViewport(xoff, yoff, sz, sz);
}

struct Cell
{
    bool visited = false;
    bool walls[4] = {true, true, true, true}; // top, right, bottom, left
    bool blocked = false;
};

static std::vector<Cell> grid;

// still keep these (we won’t render walls with GL anymore – using ImGui lines)
static std::vector<float> wallVertices;
static unsigned int wallVAO = 0, wallVBO = 0, borderVAO = 0, borderVBO = 0;

static bool solving = false;
static int animState = 0; // 0 running, 1 done
static double animStartTime = 0, animEndTime = 0;
static int solveAlgo = 0; // 0 DFS, 1 BFS, 2 Dijkstra, 3 A*
static int genAlgo = 0;   // 0 Backtracker, 1 Prim, 2 Kruskal

static std::vector<std::tuple<int, int, bool, float>> events;
static std::vector<std::pair<int, int>> finalPathEdges;
static std::vector<float> successVertices; // pairs of (x,y) points in grid space
static std::vector<float> failureVertices;
static size_t eventIndex = 0;
static bool stepMode = false;

static unsigned int successVAO = 0, successVBO = 0, failureVAO = 0, failureVBO = 0;
static int startCell = 0, endCell = 0;
static std::mt19937 rng(std::random_device{}());

static unsigned int shader = 0;
static glm::mat4 proj;

static float speedMultiplier = 1.0f;
static float obstacleDensity = 0.15f;
// Removed maxWeight variable

// Wall and UI icon textures
static GLuint texWall = 0; // legacy, unused
static GLuint texLineHori = 0;
static GLuint texLineVerti = 0;
static GLuint texPlay = 0, texPause = 0, texRegen = 0, texSettings = 0, texStep = 0;

// =============== TEXTURES (stb_image) ===============
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

static GLuint texBackground = 0;
static GLuint texStart = 0;
static GLuint texEnd = 0;
static GLuint texObstacle = 0;

static GLuint loadTexture(const char *path)
{
    int w = 0, h = 0, n = 0;
    stbi_uc *data = stbi_load(path, &w, &h, &n, 4);
    if (!data)
    {
        logf("Failed to load texture: %s", path);
        return 0;
    }
    GLuint id;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    stbi_image_free(data);
    logf("Loaded texture %s (%dx%d)", path, w, h);
    return id;
}

static void loadAllTextures()
{
    texBackground = loadTexture("assets/background.png"); // 1024x1024
    texStart = loadTexture("assets/start.png");           // 64x64
    texEnd = loadTexture("assets/end.png");               // 64x64
    texObstacle = loadTexture("assets/obsticle.png");     // 64x64
    texLineHori = loadTexture("assets/lineHori.png");     // horizontal wall
    texLineVerti = loadTexture("assets/lineVerti.png");   // vertical wall
    texWall = 0;                                          // legacy, not used
    texPlay = loadTexture("assets/play.png");
    texPause = loadTexture("assets/pause.png");
    texRegen = loadTexture("assets/regen.png");
    texSettings = loadTexture("assets/setting.png");
    texStep = loadTexture("assets/step.png");
}

static void deleteAllTextures()
{
    GLuint ids[11] = {texBackground, texStart, texEnd, texObstacle, texLineHori, texLineVerti, texPlay, texPause, texRegen, texSettings, texStep};
    glDeleteTextures(11, ids);
    texBackground = texStart = texEnd = texObstacle = texLineHori = texLineVerti = texPlay = texPause = texRegen = texSettings = texStep = 0;
}

// ======================= UTILS =========================
inline int indexXY(int x, int y, int C, int R)
{
    if (x < 0 || y < 0 || x >= C || y >= R)
        return -1;
    return x + y * C;
}
inline int index(int x, int y) { return indexXY(x, y, gCols, gRows); }

std::vector<int> getUnvisitedNeighbors(int x, int y)
{
    static const int dirs[4][3] = {{0, -1, 0}, {1, 0, 1}, {0, 1, 2}, {-1, 0, 3}};
    std::vector<int> nbrs;
    for (auto &d : dirs)
    {
        int ni = index(x + d[0], y + d[1]);
        if (ni != -1 && !grid[ni].visited)
            nbrs.push_back((d[2] << 16) | ni);
    }
    return nbrs;
}
void removeWallsAB(int a, int b, int w)
{
    grid[a].walls[w] = false;
    grid[b].walls[(w + 2) % 4] = false;
}
void clearGridVisited()
{
    for (auto &c : grid)
        c.visited = false;
}

void buildProjection()
{
    proj = glm::ortho(0.0f, (float)gCols, (float)gRows, 0.0f);
    if (shader)
    {
        glUseProgram(shader);
        glUniformMatrix4fv(glGetUniformLocation(shader, "uProjection"), 1, GL_FALSE, &proj[0][0]);
    }
}

// ======================= SHADERS (kept for future GL use) =========================
unsigned int compileShader(unsigned int type, const char *src)
{
    unsigned int id = glCreateShader(type);
    glShaderSource(id, 1, &src, nullptr);
    glCompileShader(id);
    int ok;
    glGetShaderiv(id, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char buf[4096];
        glGetShaderInfoLog(id, sizeof(buf), nullptr, buf);
        logf("Shader compile error:\n%s", buf);
    }
    return id;
}
unsigned int createProgram(const char *vs, const char *fs)
{
    unsigned int prog = glCreateProgram();
    unsigned int v = compileShader(GL_VERTEX_SHADER, vs);
    unsigned int f = compileShader(GL_FRAGMENT_SHADER, fs);
    glAttachShader(prog, v);
    glAttachShader(prog, f);
    glLinkProgram(prog);
    glDeleteShader(v);
    glDeleteShader(f);
    GLint linked = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &linked);
    if (!linked)
    {
        GLint len = 0;
        glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &len);
        std::vector<char> log(len);
        glGetProgramInfoLog(prog, len, nullptr, log.data());
        logf("Program link error:\n%s", log.data());
    }
    return prog;
}

static const char *VS_330 =
    "#version 330 core\n"
    "layout(location=0) in vec2 aPos;\n"
    "uniform mat4 uProjection;\n"
    "void main(){ gl_Position = uProjection * vec4(aPos,0,1); }\n";
static const char *FS_330 =
    "#version 330 core\n"
    "out vec4 FragColor;\n"
    "uniform vec3 uColor;\n"
    "void main(){ FragColor = vec4(uColor,1); }\n";

static const char *VS_150 =
    "#version 150\n"
    "in vec2 aPos;\n"
    "uniform mat4 uProjection;\n"
    "void main(){ gl_Position = uProjection * vec4(aPos,0,1); }\n";
static const char *FS_150 =
    "#version 150\n"
    "out vec4 FragColor;\n"
    "uniform vec3 uColor;\n"
    "void main(){ FragColor = vec4(uColor,1); }\n";

// ======================= MAZE GEN =========================
struct DSU
{
    std::vector<int> p, r;
    DSU(int n = 0) { reset(n); }
    void reset(int n)
    {
        p.resize(n);
        r.assign(n, 0);
        for (int i = 0; i < n; i++)
            p[i] = i;
    }
    int find(int x) { return p[x] == x ? x : p[x] = find(p[x]); }
    bool unite(int a, int b)
    {
        a = find(a);
        b = find(b);
        if (a == b)
            return false;
        if (r[a] < r[b])
            std::swap(a, b);
        p[b] = a;
        if (r[a] == r[b])
            r[a]++;
        return true;
    }
};

// Ensure start/end points have multiple pathways (at least 7 as requested)
void ensureMultiplePathways()
{
    auto createPathwaysAroundCell = [&](int cellIdx, int minPathways) {
        int x = cellIdx % gCols, y = cellIdx / gCols;
        std::vector<std::pair<int, int>> neighbors;
        
        // Get all valid neighbors within 2-cell radius
        for (int dx = -2; dx <= 2; dx++) {
            for (int dy = -2; dy <= 2; dy++) {
                if (dx == 0 && dy == 0) continue;
                int nx = x + dx, ny = y + dy;
                if (nx >= 0 && nx < gCols && ny >= 0 && ny < gRows) {
                    neighbors.push_back({nx, ny});
                }
            }
        }
        
        std::shuffle(neighbors.begin(), neighbors.end(), rng);
        
        int pathwaysCreated = 0;
        for (auto [nx, ny] : neighbors) {
            if (pathwaysCreated >= minPathways) break;
            
            // Create a path by removing walls along the way
            int cx = x, cy = y;
            while ((cx != nx || cy != ny) && pathwaysCreated < minPathways) {
                // Move one step closer to target
                int stepX = (nx > cx) ? 1 : (nx < cx) ? -1 : 0;
                int stepY = (ny > cy) ? 1 : (ny < cy) ? -1 : 0;
                
                if (stepX != 0) {
                    int wallDir = (stepX > 0) ? 1 : 3; // East or West
                    int currentIdx = index(cx, cy);
                    int nextIdx = index(cx + stepX, cy);
                    if (currentIdx >= 0 && nextIdx >= 0) {
                        grid[currentIdx].walls[wallDir] = false;
                        grid[nextIdx].walls[(wallDir + 2) % 4] = false;
                    }
                    cx += stepX;
                }
                
                if (stepY != 0 && cx == nx) {
                    int wallDir = (stepY > 0) ? 2 : 0; // South or North
                    int currentIdx = index(cx, cy);
                    int nextIdx = index(cx, cy + stepY);
                    if (currentIdx >= 0 && nextIdx >= 0) {
                        grid[currentIdx].walls[wallDir] = false;
                        grid[nextIdx].walls[(wallDir + 2) % 4] = false;
                    }
                    cy += stepY;
                }
            }
            pathwaysCreated++;
        }
    };
    
    // Ensure both start and end have at least 7 pathways
    createPathwaysAroundCell(startCell, 7);
    createPathwaysAroundCell(endCell, 7);
}

// Add some strategic complexity while maintaining maze structure
void addMazeComplexity()
{
    // Add a few strategic loops to make the maze more interesting
    int loopCount = std::max(2, (gCols * gRows) / 50);
    std::uniform_int_distribution<int> cellDist(0, gCols * gRows - 1);
    
    for (int i = 0; i < loopCount; i++) {
        int cellIdx = cellDist(rng);
        if (cellIdx == startCell || cellIdx == endCell) continue;
        
        int x = cellIdx % gCols, y = cellIdx / gCols;
        std::vector<int> possibleWalls;
        
        // Check each direction for potential loop creation
        static const int dirs[4][3] = {{0, -1, 0}, {1, 0, 1}, {0, 1, 2}, {-1, 0, 3}};
        for (auto &d : dirs) {
            int nx = x + d[0], ny = y + d[1];
            int nextIdx = index(nx, ny);
            if (nextIdx >= 0 && grid[cellIdx].walls[d[2]]) {
                // Only create opening if it would create an alternative path
                possibleWalls.push_back(d[2]);
            }
        }
        
        if (!possibleWalls.empty()) {
            std::uniform_int_distribution<int> wallChoice(0, possibleWalls.size() - 1);
            int wallToOpen = possibleWalls[wallChoice(rng)];
            
            // Open the wall
            grid[cellIdx].walls[wallToOpen] = false;
            
            // Open corresponding wall in neighbor
            int nx = x + dirs[wallToOpen][0];
            int ny = y + dirs[wallToOpen][1];
            int neighborIdx = index(nx, ny);
            if (neighborIdx >= 0) {
                grid[neighborIdx].walls[(wallToOpen + 2) % 4] = false;
            }
        }
    }
}

void generateBacktracker()
{
    // Initialize grid with all walls up (proper maze start)
    grid.assign(gCols * gRows, Cell());
    
    std::stack<int> st;
    std::uniform_int_distribution<int> startX(1, gCols - 2);
    std::uniform_int_distribution<int> startY(1, gRows - 2);
    int current = index(startX(rng), startY(rng));
    
    grid[current].visited = true;
    int visitedCount = 1;
    int total = gCols * gRows;
    
    while (visitedCount < total)
    {
        int cx = current % gCols, cy = current / gCols;
        auto nbrs = getUnvisitedNeighbors(cx, cy);
        
        if (!nbrs.empty())
        {
            std::uniform_int_distribution<int> di(0, (int)nbrs.size() - 1);
            int packed = nbrs[di(rng)];
            int w = packed >> 16, nxt = packed & 0xFFFF;
            
            st.push(current);
            removeWallsAB(current, nxt, w);
            current = nxt;
            grid[current].visited = true;
            visitedCount++;
        }
        else if (!st.empty())
        {
            current = st.top();
            st.pop();
        }
        else
        {
            // Find any unvisited cell to continue
            for (int i = 0; i < total; i++) {
                if (!grid[i].visited) {
                    current = i;
                    grid[current].visited = true;
                    visitedCount++;
                    break;
                }
            }
        }
    }
    
    clearGridVisited();
    
    // Add strategic complexity and ensure pathways
    addMazeComplexity();
    ensureMultiplePathways();
}

void generatePrim()
{
    // Initialize all walls up
    grid.assign(gCols * gRows, Cell());
    
    std::uniform_int_distribution<int> sx(1, gCols - 2), sy(1, gRows - 2);
    int cx = sx(rng), cy = sy(rng);
    int start = index(cx, cy);
    grid[start].visited = true;

    struct Edge {
        int a, b, w;
    };
    
    std::vector<Edge> frontier;
    auto addFrontier = [&](int x, int y) {
        static const int d[4][3] = {{0, -1, 0}, {1, 0, 1}, {0, 1, 2}, {-1, 0, 3}};
        int a = index(x, y);
        for (auto &dd : d) {
            int nx = x + dd[0], ny = y + dd[1];
            int b = index(nx, ny);
            if (b != -1 && !grid[b].visited) {
                frontier.push_back({a, b, dd[2]});
            }
        }
    };
    
    addFrontier(cx, cy);
    
    while (!frontier.empty()) {
        std::uniform_int_distribution<size_t> pick(0, frontier.size() - 1);
        size_t k = pick(rng);
        Edge e = frontier[k];
        frontier[k] = frontier.back();
        frontier.pop_back();
        
        if (grid[e.b].visited)
            continue;
            
        removeWallsAB(e.a, e.b, e.w);
        grid[e.b].visited = true;
        int bx = e.b % gCols, by = e.b / gCols;
        addFrontier(bx, by);
    }
    
    clearGridVisited();
    
    // Add strategic complexity and ensure pathways
    addMazeComplexity();
    ensureMultiplePathways();
}

void generateKruskal()
{
    // Initialize all walls up
    grid.assign(gCols * gRows, Cell());
    int N = gCols * gRows;
    DSU dsu(N);
    
    struct Edge {
        int a, b, w;
    };
    
    std::vector<Edge> edges;
    static const int d[2][3] = {{1, 0, 1}, {0, 1, 2}};
    
    // Create all possible edges
    for (int y = 0; y < gRows; y++) {
        for (int x = 0; x < gCols; x++) {
            int a = index(x, y);
            for (auto &dd : d) {
                int nx = x + dd[0], ny = y + dd[1];
                int b = index(nx, ny);
                if (b != -1) {
                    edges.push_back({a, b, dd[2]});
                }
            }
        }
    }
    
    // Randomize edge order for variety
    std::shuffle(edges.begin(), edges.end(), rng);
    
    // Build minimum spanning tree (creates perfect maze)
    for (auto &e : edges) {
        if (dsu.unite(e.a, e.b)) {
            removeWallsAB(e.a, e.b, e.w);
        }
    }
    
    clearGridVisited();
    
    // Add strategic complexity and ensure pathways
    addMazeComplexity();
    ensureMultiplePathways();
}

void pickStartEnd()
{
    // Select start and end from corners for maximum distance
    std::vector<int> corners = {
        index(0, 0),
        index(gCols - 1, 0),
        index(0, gRows - 1),
        index(gCols - 1, gRows - 1)
    };
    
    std::uniform_int_distribution<int> dc(0, (int)corners.size() - 1);
    startCell = corners[dc(rng)];
    
    // Pick end cell from remaining corners (ensuring they're different)
    std::vector<int> endCandidates;
    for (int cell : corners) {
        if (cell != startCell) {
            endCandidates.push_back(cell);
        }
    }
    
    std::uniform_int_distribution<int> ec(0, (int)endCandidates.size() - 1);
    endCell = endCandidates[ec(rng)];
    
    // Ensure start and end are not blocked
    if (grid[startCell].blocked) grid[startCell].blocked = false;
    if (grid[endCell].blocked) grid[endCell].blocked = false;
    
    logf("Start: (%d,%d), End: (%d,%d)", 
         startCell % gCols, startCell / gCols,
         endCell % gCols, endCell / gCols);
}

void randomizeObstacles(float density)
{
    // Clear all obstacles first
    for (auto &c : grid)
        c.blocked = false;
    
    // Find ALL possible paths from start to end using BFS with path tracking
    std::vector<std::vector<int>> allPaths;
    std::function<void(int, std::vector<int>&, std::vector<bool>&)> findAllPaths = 
        [&](int current, std::vector<int>& currentPath, std::vector<bool>& visited) 
    {
        if (current == endCell) 
        {
            allPaths.push_back(currentPath);
            return;
        }
        
        int x = current % gCols, y = current / gCols;
        static const int dirs[4][3] = {{0, -1, 0}, {1, 0, 1}, {0, 1, 2}, {-1, 0, 3}};
        
        for (auto &d : dirs) 
        {
            int nx = x + d[0], ny = y + d[1];
            int next = index(nx, ny);
            if (next >= 0 && !grid[current].walls[d[2]] && !visited[next]) 
            {
                visited[next] = true;
                currentPath.push_back(next);
                findAllPaths(next, currentPath, visited);
                currentPath.pop_back();
                visited[next] = false;
            }
        }
    };
    
    std::vector<bool> visited(gCols * gRows, false);
    std::vector<int> currentPath;
    visited[startCell] = true;
    currentPath.push_back(startCell);
    findAllPaths(startCell, currentPath, visited);
    
    if (allPaths.empty()) 
    {
        logf("No paths found between start and end!");
        return;
    }
    
    logf("Found %d different paths from start to end", (int)allPaths.size());
    
    // Strategy: Either block ALL paths or NO paths (user's requirement)
    std::uniform_real_distribution<float> choice(0.0f, 1.0f);
    bool blockAllPaths = choice(rng) < 0.3f; // 30% chance to block all paths
    
    if (blockAllPaths)
    {
        // Block ALL paths - place obstacles on critical choke points
        std::set<int> criticalCells;
        
        // Find cells that appear in ALL paths (choke points)
        if (!allPaths.empty())
        {
            std::map<int, int> cellFrequency;
            for (const auto& path : allPaths)
            {
                for (int cell : path)
                {
                    if (cell != startCell && cell != endCell)
                        cellFrequency[cell]++;
                }
            }
            
            // Cells that appear in many paths are critical
            int pathCount = allPaths.size();
            for (const auto& pair : cellFrequency)
            {
                if (pair.second >= pathCount * 0.7f) // In at least 70% of paths
                {
                    criticalCells.insert(pair.first);
                }
            }
        }
        
        // Place obstacles on some critical cells
        std::vector<int> criticalList(criticalCells.begin(), criticalCells.end());
        if (!criticalList.empty())
        {
            std::shuffle(criticalList.begin(), criticalList.end(), rng);
            int obstacleCount = std::min((int)(criticalList.size() * 0.6f), (int)(criticalList.size()));
            
            for (int i = 0; i < obstacleCount; i++)
            {
                grid[criticalList[i]].blocked = true;
            }
            
            logf("Blocked ALL paths by placing %d strategic obstacles", obstacleCount);
        }
    }
    else
    {
        // Don't block ANY path - place obstacles only in areas that don't affect any path
        std::set<int> pathCells;
        for (const auto& path : allPaths)
        {
            for (int cell : path)
                pathCells.insert(cell);
        }
        
        // Place obstacles randomly on non-path cells
        std::vector<int> safeCells;
        for (int i = 0; i < gCols * gRows; i++)
        {
            if (pathCells.find(i) == pathCells.end() && i != startCell && i != endCell)
            {
                safeCells.push_back(i);
            }
        }
        
        if (!safeCells.empty())
        {
            std::shuffle(safeCells.begin(), safeCells.end(), rng);
            int obstacleCount = std::min((int)(safeCells.size() * density), (int)safeCells.size());
            
            for (int i = 0; i < obstacleCount; i++)
            {
                grid[safeCells[i]].blocked = true;
            }
            
            logf("Preserved all %d paths, placed %d obstacles in safe areas", (int)allPaths.size(), obstacleCount);
        }
    }
}
void clearObstacles()
{
    for (auto &c : grid)
        c.blocked = false;
}

void resetAnimationBuffers()
{
    solving = false;
    events.clear();
    finalPathEdges.clear();
    successVertices.clear();
    failureVertices.clear();
    eventIndex = 0;
    animState = 0;
}

inline void pushEvent(int u, int v, bool ok, float wCost = 1.0f)
{
    events.emplace_back(u, v, ok, wCost);
}

// ======================= DRAW HELPERS =========================
static inline ImTextureID toImguiTex(GLuint id) { return (ImTextureID)(intptr_t)id; }

static void computeViewportAndCell(float &xoff, float &yoff, float &cell, int &sz)
{
    int w, h;
    glfwGetFramebufferSize(gWindow, &w, &h);

    // Calculate responsive sidebar width (same as in UI)
    float baseWidth = 420.0f;
    float windowWidth = w;
    float responsiveWidth = std::min(baseWidth, windowWidth * 0.32f); // Max 32% of window width
    responsiveWidth = std::max(responsiveWidth, 280.0f); // Minimum width 280
    
    // Calculate available space for maze rendering
    int availableWidth = w - (int)responsiveWidth;
    sz = std::min(availableWidth, h);

    // Position maze dynamically based on actual sidebar width
    xoff = responsiveWidth + (float)(availableWidth - sz) * 0.5f;
    yoff = (float)(h - sz) * 0.5f;
    cell = (float)sz / (float)gCols;
}

// background, obstacles, start/end as images
void drawTexturedLayer()
{
    float xoff, yoff, cell;
    int sz;
    computeViewportAndCell(xoff, yoff, cell, sz);
    ImDrawList *dl = ImGui::GetBackgroundDrawList();

    // Background image fills the square area
    if (texBackground)
        dl->AddImage(toImguiTex(texBackground),
                     ImVec2(xoff, yoff),
                     ImVec2(xoff + sz, yoff + sz));

    // obstacles
    if (texObstacle)
    {
        for (int i = 0; i < gCols * gRows; i++)
        {
            if (!grid[i].blocked)
                continue;
            int x = i % gCols, y = i / gCols;
            float x0 = xoff + x * cell, y0 = yoff + y * cell;
            float x1 = x0 + cell, y1 = y0 + cell;
            dl->AddImage(toImguiTex(texObstacle), ImVec2(x0, y0), ImVec2(x1, y1));
        }
    }

    // start
    if (texStart)
    {
        int sx = startCell % gCols, sy = startCell / gCols;
        float x0 = xoff + sx * cell, y0 = yoff + sy * cell;
        dl->AddImage(toImguiTex(texStart), ImVec2(x0, y0), ImVec2(x0 + cell, y0 + cell));
    }
    // end
    if (texEnd)
    {
        int ex = endCell % gCols, ey = endCell / gCols;
        float x0 = xoff + ex * cell, y0 = yoff + ey * cell;
        dl->AddImage(toImguiTex(texEnd), ImVec2(x0, y0), ImVec2(x0 + cell, y0 + cell));
    }
}

// draw maze walls as textured images (line.png)
void drawWallsAsLines()
{
    float xoff, yoff, cell;
    int sz;
    computeViewportAndCell(xoff, yoff, cell, sz);
    ImDrawList *dl = ImGui::GetBackgroundDrawList();

    float thickness = cell * 0.08f; // consistent thickness
    for (int y = 0; y < gRows; ++y)
        for (int x = 0; x < gCols; ++x)
        {
            int i = index(x, y);
            float xf = xoff + x * cell;
            float yf = yoff + y * cell;
            // Top wall (horizontal)
            if (grid[i].walls[0] && texLineHori)
            {
                ImVec2 p0(xf, yf);
                ImVec2 p1(xf + cell, yf + thickness);
                dl->AddImage(toImguiTex(texLineHori), p0, p1);
            }
            // Right wall (vertical)
            if (grid[i].walls[1] && texLineVerti)
            {
                ImVec2 p0(xf + cell - thickness, yf);
                ImVec2 p1(xf + cell, yf + cell);
                dl->AddImage(toImguiTex(texLineVerti), p0, p1);
            }
            // Bottom wall (horizontal)
            if (grid[i].walls[2] && texLineHori)
            {
                ImVec2 p0(xf, yf + cell - thickness);
                ImVec2 p1(xf + cell, yf + cell);
                dl->AddImage(toImguiTex(texLineHori), p0, p1);
            }
            // Left wall (vertical)
            if (grid[i].walls[3] && texLineVerti)
            {
                ImVec2 p0(xf, yf);
                ImVec2 p1(xf + thickness, yf + cell);
                dl->AddImage(toImguiTex(texLineVerti), p0, p1);
            }
        }

    // outer border
    const ImU32 borderCol = IM_COL32(255, 80, 80, 255); // red border
    float x0 = xoff, y0 = yoff, x1 = xoff + sz, y1 = yoff + sz;
    dl->AddRect(ImVec2(x0, y0), ImVec2(x1, y1), borderCol, 0.0f, 0, std::max(2.0f, cell * 0.08f));
}

// draw success (purple) and failure (red) path segments as ImGui lines
void drawPathsAsLines()
{
    float xoff, yoff, cell;
    int sz;
    computeViewportAndCell(xoff, yoff, cell, sz);
    ImDrawList *dl = ImGui::GetBackgroundDrawList();

    auto drawPairs = [&](const std::vector<float> &v, ImU32 c, float thick)
    {
        for (size_t i = 0; i + 3 < v.size(); i += 4)
        {
            float ux = xoff + v[i + 0] * cell;
            float uy = yoff + v[i + 1] * cell;
            float vx = xoff + v[i + 2] * cell;
            float vy = yoff + v[i + 3] * cell;
            dl->AddLine(ImVec2(ux, uy), ImVec2(vx, vy), c, thick);
        }
    };

    float thickSuccess = std::max(2.0f, cell * 0.10f);
    float thickFail = std::max(1.5f, cell * 0.06f);
    drawPairs(successVertices, IM_COL32(180, 80, 255, 255), thickSuccess); // purple
    drawPairs(failureVertices, IM_COL32(255, 153, 153, 255), thickFail);
}

// ======================= DRAW SETUP (GL buffers kept minimal) =========================
void buildWallVertices()
{
    wallVertices.clear();
    for (int y = 0; y < gRows; y++)
        for (int x = 0; x < gCols; x++)
        {
            int i = index(x, y);
            float xf = (float)x, yf = (float)y;
            if (grid[i].walls[0])
                wallVertices.insert(wallVertices.end(), {xf, yf, xf + 1, yf});
            if (grid[i].walls[1])
                wallVertices.insert(wallVertices.end(), {xf + 1, yf, xf + 1, yf + 1});
            if (grid[i].walls[2])
                wallVertices.insert(wallVertices.end(), {xf + 1, yf + 1, xf, yf + 1});
            if (grid[i].walls[3])
                wallVertices.insert(wallVertices.end(), {xf, yf + 1, xf, yf});
        }
}

void rebuildBorderVAO() { /* no-op for ImGui walls */ }

// ======================= SOLVERS =========================
inline void pushSuccess(int u, int v)
{
    float ux = (u % gCols) + 0.5f, uy = (u / gCols) + 0.5f;
    float vx = (v % gCols) + 0.5f, vy = (v / gCols) + 0.5f;
    successVertices.insert(successVertices.end(), {ux, uy, vx, vy});
}
inline void pushFailure(int u, int v)
{
    float ux = (u % gCols) + 0.5f, uy = (u / gCols) + 0.5f;
    float vx = (v % gCols) + 0.5f, vy = (v / gCols) + 0.5f;
    failureVertices.insert(failureVertices.end(), {ux, uy, vx, vy});
}

void solveDFS()
{
    int N = gCols * gRows;
    std::vector<bool> vis(N, false);
    std::function<bool(int)> dfsRec = [&](int u)
    {
        if (u == endCell)
            return true;
        int x = u % gCols, y = u / gCols;
        static const int dirs[4][3] = {{0, -1, 0}, {1, 0, 1}, {0, 1, 2}, {-1, 0, 3}};
        for (auto &d : dirs)
        {
            int v = index(x + d[0], y + d[1]);
            if (v < 0 || grid[u].walls[d[2]] || vis[v] || grid[v].blocked)
                continue;
            vis[v] = true;
            pushEvent(u, v, true);
            if (dfsRec(v))
                return true;
            pushEvent(u, v, false);
        }
        return false;
    };
    vis[startCell] = true;
    dfsRec(startCell);
}

void solveBFS()
{
    int N = gCols * gRows;
    std::vector<bool> vis(N, false);
    std::vector<int> parent(N, -1);
    std::queue<int> q;
    vis[startCell] = true;
    q.push(startCell);
    static const int dirs[4][3] = {{0, -1, 0}, {1, 0, 1}, {0, 1, 2}, {-1, 0, 3}};
    while (!q.empty())
    {
        int u = q.front();
        q.pop();
        if (u == endCell)
            break;
        int x = u % gCols, y = u / gCols;
        for (auto &d : dirs)
        {
            int v = index(x + d[0], y + d[1]);
            if (v < 0 || grid[u].walls[d[2]] || vis[v] || grid[v].blocked)
                continue;
            pushEvent(u, v, false);
            vis[v] = true;
            parent[v] = u;
            q.push(v);
        }
    }
    finalPathEdges.clear();
    int cur = endCell;
    while (cur != -1 && parent[cur] != -1)
    {
        finalPathEdges.emplace_back(parent[cur], cur);
        cur = parent[cur];
    }
    std::reverse(finalPathEdges.begin(), finalPathEdges.end());
    std::set<std::pair<int, int>> pathSet(finalPathEdges.begin(), finalPathEdges.end());
    for (auto &e : events)
    {
        int u, v;
        bool ok;
        float w;
        std::tie(u, v, ok, w) = e;
        if (pathSet.count({u, v}))
            std::get<2>(e) = true;
    }
}

void solveDijkstra()
{
    int N = gCols * gRows;
    const float INF = std::numeric_limits<float>::infinity();
    std::vector<float> dist(N, INF);
    std::vector<int> parent(N, -1);
    using P = std::pair<float, int>;
    std::priority_queue<P, std::vector<P>, std::greater<P>> pq;

    dist[startCell] = 0;
    pq.push({0, startCell});
    static const int dirs[4][3] = {{0, -1, 0}, {1, 0, 1}, {0, 1, 2}, {-1, 0, 3}};

    while (!pq.empty())
    {
        auto [du, u] = pq.top();
        pq.pop();
        if (du != dist[u])
            continue;
        if (u == endCell)
            break;
        int x = u % gCols, y = u / gCols;
        for (auto &d : dirs)
        {
            int v = index(x + d[0], y + d[1]);
            if (v < 0 || grid[u].walls[d[2]] || grid[v].blocked)
                continue;
            float w = 1.0f; // All edges have weight 1
            if (dist[v] > du + w)
            {
                dist[v] = du + w;
                parent[v] = u;
                pushEvent(u, v, false, w);
                pq.push({dist[v], v});
            }
        }
    }
    finalPathEdges.clear();
    int cur = endCell;
    while (cur != -1 && parent[cur] != -1)
    {
        finalPathEdges.emplace_back(parent[cur], cur);
        cur = parent[cur];
    }
    std::reverse(finalPathEdges.begin(), finalPathEdges.end());
    std::set<std::pair<int, int>> pathSet(finalPathEdges.begin(), finalPathEdges.end());
    for (auto &e : events)
    {
        int u, v;
        bool ok;
        float w;
        std::tie(u, v, ok, w) = e;
        if (pathSet.count({u, v}))
            std::get<2>(e) = true;
    }
}

void solveAStar()
{
    auto h = [&](int a)
    {
        int ax = a % gCols, ay = a / gCols;
        int ex = endCell % gCols, ey = endCell / gCols;
        return (float)(abs(ax - ex) + abs(ay - ey));
    };
    int N = gCols * gRows;
    const float INF = std::numeric_limits<float>::infinity();
    std::vector<float> gScore(N, INF), fScore(N, INF);
    std::vector<int> parent(N, -1);
    using P = std::pair<float, int>;
    std::priority_queue<P, std::vector<P>, std::greater<P>> open;

    gScore[startCell] = 0;
    fScore[startCell] = h(startCell);
    open.push({fScore[startCell], startCell});
    static const int dirs[4][3] = {{0, -1, 0}, {1, 0, 1}, {0, 1, 2}, {-1, 0, 3}};

    while (!open.empty())
    {
        auto [f, u] = open.top();
        open.pop();
        if (f != fScore[u])
            continue;
        if (u == endCell)
            break;
        int x = u % gCols, y = u / gCols;
        for (auto &d : dirs)
        {
            int v = index(x + d[0], y + d[1]);
            if (v < 0 || grid[u].walls[d[2]] || grid[v].blocked)
                continue;
            float w = 1.0f; // All edges have weight 1
            float tent = gScore[u] + w;
            if (tent < gScore[v])
            {
                parent[v] = u;
                gScore[v] = tent;
                fScore[v] = tent + h(v);
                pushEvent(u, v, false, w);
                open.push({fScore[v], v});
            }
        }
    }
    finalPathEdges.clear();
    int cur = endCell;
    while (cur != -1 && parent[cur] != -1)
    {
        finalPathEdges.emplace_back(parent[cur], cur);
        cur = parent[cur];
    }
    std::reverse(finalPathEdges.begin(), finalPathEdges.end());
    std::set<std::pair<int, int>> pathSet(finalPathEdges.begin(), finalPathEdges.end());
    for (auto &e : events)
    {
        int u, v;
        bool ok;
        float w;
        std::tie(u, v, ok, w) = e;
        if (pathSet.count({u, v}))
            std::get<2>(e) = true;
    }
}

// ======================= REGEN AND UI =========================
void regenerateMaze()
{
    resetAnimationBuffers();
    if (genAlgo == 0)
        generateBacktracker();
    else if (genAlgo == 1)
        generatePrim();
    else
        generateKruskal();
    pickStartEnd();
    buildWallVertices();
}

int main()
{
#ifdef _WIN32
    if (!GetConsoleWindow())
    {
        AllocConsole();
        FILE *fp;
        freopen_s(&fp, "CONOUT$", "w", stdout);
        freopen_s(&fp, "CONOUT$", "w", stderr);
    }
#endif
    open_log();

    // GLFW + Window
    glfwSetErrorCallback([](int c, const char *d)
                         { logf("GLFW error %d: %s", c, d); });
    if (!glfwInit())
        fatal("glfwInit failed");
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, 4);
    gWindow = glfwCreateWindow(1000, 900, "Maze-Runner", nullptr, nullptr);
    if (!gWindow)
    {
        glfwDefaultWindowHints();
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
        glfwWindowHint(GLFW_SAMPLES, 4);
        gWindow = glfwCreateWindow(1000, 900, "Maze-Runner", nullptr, nullptr);
        if (!gWindow)
            fatal("glfwCreateWindow failed");
    }
    glfwMaximizeWindow(gWindow); // Start maximized, not fullscreen

    // Set app icon using assets/logo.png
    {
        int iconW = 0, iconH = 0, iconC = 0;
        unsigned char *iconPixels = stbi_load("assets/logo.png", &iconW, &iconH, &iconC, 4);
        if (iconPixels && iconW > 0 && iconH > 0)
        {
            GLFWimage icon;
            icon.width = iconW;
            icon.height = iconH;
            icon.pixels = iconPixels;
            glfwSetWindowIcon(gWindow, 1, &icon);
            stbi_image_free(iconPixels);
        }
    }
    glfwMakeContextCurrent(gWindow);
    glfwSwapInterval(1);
    glfwSetFramebufferSizeCallback(gWindow, framebuffer_size_callback);

    // GLAD
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
        fatal("GLAD init failed");

    glEnable(GL_MULTISAMPLE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    
    if (!ImGui_ImplGlfw_InitForOpenGL(gWindow, true))
        fatal("ImGui_ImplGlfw_InitForOpenGL failed");
    if (!ImGui_ImplOpenGL3_Init("#version 330"))
        if (!ImGui_ImplOpenGL3_Init("#version 150"))
            fatal("ImGui_ImplOpenGL3_Init failed");
    // Themed ImGui style
    ImGuiStyle &style = ImGui::GetStyle();
    style.WindowRounding = 12.0f;
    style.FrameRounding = 8.0f;
    style.ButtonTextAlign = ImVec2(0.5f, 0.5f); // Center button text properly
    style.FramePadding = ImVec2(12.0f, 8.0f); // Proper padding for buttons
    style.ItemSpacing = ImVec2(12.0f, 8.0f); // Good spacing between items
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.13f, 0.10f, 0.18f, 0.95f);
    style.Colors[ImGuiCol_TitleBg] = ImVec4(0.22f, 0.16f, 0.32f, 1.0f);
    style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.32f, 0.22f, 0.52f, 1.0f);
    style.Colors[ImGuiCol_Button] = ImVec4(0.32f, 0.22f, 0.52f, 0.85f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.45f, 0.32f, 0.70f, 1.0f);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.60f, 0.40f, 0.80f, 1.0f);
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.18f, 0.13f, 0.25f, 1.0f);
    style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.32f, 0.22f, 0.52f, 1.0f);
    style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.45f, 0.32f, 0.70f, 1.0f);

    // Load custom font with better error handling
    ImGuiIO &io = ImGui::GetIO();
    
    // Try multiple font paths in case of different working directories
    const char* fontPaths[] = {
        "assets/font/DectoneRegular-q2VG6.ttf",
        "./assets/font/DectoneRegular-q2VG6.ttf",
        "C:/Users/MuneebAnjum/Desktop/MazerRunner/assets/font/DectoneRegular-q2VG6.ttf"
    };
    
    ImFont *customFont = nullptr;
    for (const char* fontPath : fontPaths) {
        customFont = io.Fonts->AddFontFromFileTTF(fontPath, 20.0f);
        if (customFont != nullptr) {
            logf("Successfully loaded font from: %s", fontPath);
            break;
        }
        logf("Failed to load font from: %s", fontPath);
    }
    
    if (customFont == nullptr)
    {
        logf("Failed to load custom font from all paths, using default ImGui font");
        // Add default font to ensure we have something to work with
        customFont = io.Fonts->AddFontDefault();
    }
    else
    {
        io.FontDefault = customFont;
        logf("Custom font set as default successfully");
    }

    // (Shaders/VAOs kept, but not required for walls now)
    shader = createProgram(VS_330, FS_330);

    // Textures
    stbi_set_flip_vertically_on_load(false);
    loadAllTextures();

    // Initial maze
    regenerateMaze();

    const double baseDelay = 0.005;
    double lastEventTime = 0.0;

    while (!glfwWindowShouldClose(gWindow))
    {
        glfwPollEvents();
        glClearColor(0.05f, 0.05f, 0.10f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // 1) Background + assets
        drawTexturedLayer();

        // 2) Maze walls (as lines) and dynamic paths – ABOVE background, BELOW UI windows
        drawWallsAsLines();
        drawPathsAsLines();

        // -------- SIDE MENU UI --------
        // Custom white & purple theme for side menu
        ImGuiStyle &s = ImGui::GetStyle();
        ImVec4 origCol[ImGuiCol_COUNT];
        for (int i = 0; i < ImGuiCol_COUNT; ++i)
            origCol[i] = s.Colors[i];
        s.Colors[ImGuiCol_WindowBg] = ImVec4(0.97f, 0.95f, 1.0f, 0.99f);
        s.Colors[ImGuiCol_TitleBg] = ImVec4(0.70f, 0.55f, 0.95f, 1.0f);
        s.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.80f, 0.60f, 1.0f, 1.0f);
        s.Colors[ImGuiCol_Button] = ImVec4(0.80f, 0.60f, 1.0f, 0.85f);
        s.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.90f, 0.80f, 1.0f, 1.0f);
        s.Colors[ImGuiCol_ButtonActive] = ImVec4(0.60f, 0.40f, 0.80f, 1.0f);
        // Modern dark theme colors
        s.Colors[ImGuiCol_WindowBg] = ImVec4(0.08f, 0.08f, 0.12f, 0.98f);       // Dark blue-gray background
        s.Colors[ImGuiCol_FrameBg] = ImVec4(0.15f, 0.15f, 0.20f, 0.90f);        // Darker frame background
        s.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.25f, 0.25f, 0.35f, 0.90f); // Hover state
        s.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.35f, 0.35f, 0.50f, 0.90f);  // Active state
        s.Colors[ImGuiCol_Text] = ImVec4(0.90f, 0.90f, 0.95f, 1.0f);            // Light text
        s.Colors[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.50f, 0.55f, 1.0f);
        s.Colors[ImGuiCol_SliderGrab] = ImVec4(0.40f, 0.60f, 0.90f, 1.0f); // Blue slider
        s.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.50f, 0.70f, 1.0f, 1.0f);
        s.Colors[ImGuiCol_CheckMark] = ImVec4(0.40f, 0.80f, 0.40f, 1.0f);      // Green checkmark
        s.Colors[ImGuiCol_Button] = ImVec4(0.20f, 0.30f, 0.50f, 0.80f);        // Button background
        s.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.30f, 0.40f, 0.70f, 0.90f); // Button hover
        s.Colors[ImGuiCol_ButtonActive] = ImVec4(0.40f, 0.50f, 0.80f, 1.0f);   // Button active
        s.Colors[ImGuiCol_Header] = ImVec4(0.20f, 0.25f, 0.40f, 0.80f);        // Header background
        s.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.30f, 0.35f, 0.50f, 0.90f);
        s.Colors[ImGuiCol_HeaderActive] = ImVec4(0.40f, 0.45f, 0.60f, 1.0f);
        s.Colors[ImGuiCol_Separator] = ImVec4(0.30f, 0.30f, 0.40f, 1.0f); // Separator line
        s.Colors[ImGuiCol_SeparatorHovered] = ImVec4(0.40f, 0.40f, 0.50f, 1.0f);
        s.Colors[ImGuiCol_SeparatorActive] = ImVec4(0.50f, 0.50f, 0.60f, 1.0f);

        // Modern styling
        s.WindowRounding = 8.0f;
        s.FrameRounding = 6.0f;
        s.GrabRounding = 4.0f;
        s.ScrollbarRounding = 6.0f;
        s.WindowPadding = ImVec2(20, 20);
        s.FramePadding = ImVec2(12, 8);
        s.ItemSpacing = ImVec2(12, 8);
        s.ItemInnerSpacing = ImVec2(8, 6);
        s.IndentSpacing = 25.0f;
        s.ScrollbarSize = 16.0f;
        s.GrabMinSize = 12.0f;

    // Calculate responsive sidebar width once for the entire frame
    ImGuiViewport *viewport = ImGui::GetMainViewport();
    float baseWidth = 420.0f;
    float windowWidth = viewport->Size.x;
    float responsiveWidth = std::min(baseWidth, windowWidth * 0.32f); // Max 32% of window width
    responsiveWidth = std::max(responsiveWidth, 280.0f); // Minimum width 280

        // Modern responsive sidebar using flexible positioning
        // Position sidebar on the left with modern styling
        ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(responsiveWidth, viewport->Size.y), ImGuiCond_Always);

        // Modern Control Panel with clean design
        ImGui::Begin("🎮 Maze Controls", nullptr, 
                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | 
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar);

        // Modern gradient background
        ImDrawList *drawList = ImGui::GetWindowDrawList();
        ImVec2 windowPos = ImGui::GetWindowPos();
        ImVec2 windowSize = ImGui::GetWindowSize();

        // Clean gradient background
        ImU32 bgTop = IM_COL32(22, 27, 37, 245);
        ImU32 bgBottom = IM_COL32(12, 17, 27, 245);
        drawList->AddRectFilledMultiColor(
            windowPos,
            ImVec2(windowPos.x + windowSize.x, windowPos.y + windowSize.y),
            bgTop, bgTop, bgBottom, bgBottom);

        // Elegant border
        ImU32 borderColor = IM_COL32(70, 90, 130, 120);
        drawList->AddRect(windowPos, ImVec2(windowPos.x + windowSize.x, windowPos.y + windowSize.y), 
                         borderColor, 10.0f, 0, 2.0f);

        // Modern header removed - start with first section
        ImGui::SetCursorPosY(20);

        // Helper function for modern section headers
        auto drawSectionHeader = [&](const char *icon, const char *title, ImU32 iconColor)
        {
            ImVec2 availWidth = ImGui::GetContentRegionAvail();
            ImVec2 headerPos = ImGui::GetCursorScreenPos();

            // Responsive section background
            ImVec2 sectionBg_start = ImVec2(headerPos.x - 8, headerPos.y - 3);
            ImVec2 sectionBg_end = ImVec2(headerPos.x + availWidth.x - 8, headerPos.y + 30);
            ImU32 sectionBg = IM_COL32(30, 35, 50, 120);
            drawList->AddRectFilled(sectionBg_start, sectionBg_end, sectionBg, 8.0f);

            // Modern accent line
            drawList->AddRectFilled(
                ImVec2(sectionBg_start.x, sectionBg_start.y + 8),
                ImVec2(sectionBg_start.x + 4, sectionBg_end.y - 8),
                iconColor, 2.0f);

            // Icon and title with responsive positioning
            ImGui::SetCursorPosX(20);
            ImGui::PushStyleColor(ImGuiCol_Text, ImColor(iconColor).Value);
            ImGui::SetWindowFontScale(1.2f);
            ImGui::Text("%s", icon);
            ImGui::SetWindowFontScale(1.0f);
            ImGui::PopStyleColor();

            ImGui::SameLine();
            ImGui::SetCursorPosX(45);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.9f, 0.95f, 1.0f));
            ImGui::SetWindowFontScale(1.1f);
            ImGui::Text("%s", title);
            ImGui::SetWindowFontScale(1.0f);
            ImGui::PopStyleColor();

            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 15);
        };

        // Grid controls section
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10);
        drawSectionHeader("⚙️", "GRID CONFIGURATION", IM_COL32(120, 220, 120, 255));

        static int uiCols = gCols, uiRows = gRows;
        ImVec2 availWidth = ImGui::GetContentRegionAvail();
        float sliderWidth = availWidth.x * 0.7f;

        // Custom slider styling
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10.0f);
        ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0.4f, 0.8f, 0.4f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0.5f, 0.9f, 0.5f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.2f, 0.2f, 0.3f, 0.8f));

        ImGui::SetNextItemWidth(sliderWidth);
        ImGui::SliderInt("##Cols", &uiCols, 5, 60);
        ImGui::SameLine();
        ImGui::Text("Cols: %d", uiCols);

        ImGui::SetNextItemWidth(sliderWidth);
        ImGui::SliderInt("##Rows", &uiRows, 5, 60);
        ImGui::SameLine();
        ImGui::Text("Rows: %d", uiRows);

        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar();

        if ((uiCols != gCols) || (uiRows != gRows))
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.8f, 0.3f, 0.9f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.9f, 0.4f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.4f, 1.0f, 0.5f, 1.0f));

            float buttonWidth = availWidth.x * 0.9f;
            if (texSettings && ImGui::ImageButton("settings", toImguiTex(texSettings), ImVec2(24, 24)))
            {
                gCols = uiCols;
                gRows = uiRows;
                regenerateMaze();
                buildProjection();
                logf("Applied size C=%d R=%d", gCols, gRows);
            }
            ImGui::SameLine();
            ImGui::Text("Apply New Size");
            ImGui::PopStyleColor(3);
        }

        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 25);

        // Generation controls section
        drawSectionHeader("🏗️", "MAZE GENERATION", IM_COL32(220, 170, 120, 255));

        const char *genNames[] = {"Backtracker", "Prim's Algorithm", "Kruskal's Algorithm"};
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10.0f);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.2f, 0.2f, 0.3f, 0.8f));
        ImGui::SetNextItemWidth(availWidth.x * 0.9f);
        ImGui::Combo("##GenAlgo", &genAlgo, genNames, IM_ARRAYSIZE(genNames));
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.9f, 0.6f, 0.2f, 0.9f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.7f, 0.3f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 0.8f, 0.4f, 1.0f));

        if (texRegen && ImGui::ImageButton("regen", toImguiTex(texRegen), ImVec2(24, 24)))
        {
            regenerateMaze();
            logf("Regenerated with algo %d", genAlgo);
        }
        ImGui::SameLine();
        ImGui::Text("Generate New Maze");

        if (ImGui::Button("New Start/End Points", ImVec2(availWidth.x * 0.9f, 32)))
        {
            pickStartEnd();
        }
        ImGui::PopStyleColor(3);

        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 25);
        // Obstacles controls section
        drawSectionHeader("🚧", "OBSTACLES", IM_COL32(170, 120, 220, 255));

        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10.0f);
        ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0.7f, 0.5f, 0.9f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0.8f, 0.6f, 1.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.2f, 0.2f, 0.3f, 0.8f));

        ImGui::SetNextItemWidth(sliderWidth);
        ImGui::SliderFloat("##ObstacleDensity", &obstacleDensity, 0.0f, 0.6f, "%.2f");
        ImGui::SameLine();
        ImGui::Text("Density: %.2f", obstacleDensity);

        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.4f, 0.4f, 0.9f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.5f, 0.5f, 1.0f));
        float buttonWidth = (availWidth.x * 0.9f - 10) / 2.0f;
        if (ImGui::Button("Random Obstacles", ImVec2(buttonWidth, 32)))
        {
            randomizeObstacles(obstacleDensity);
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear", ImVec2(buttonWidth, 32)))
        {
            clearObstacles();
        }
        ImGui::PopStyleColor(2);

        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 25);

        // Solving controls section
        drawSectionHeader("🧠", "PATHFINDING SOLVER", IM_COL32(120, 170, 220, 255));

        const char *solveNames[] = {"Depth-First Search", "Breadth-First Search", "Dijkstra's Algorithm", "A* Algorithm"};
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10.0f);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.2f, 0.2f, 0.3f, 0.8f));
        ImGui::SetNextItemWidth(availWidth.x * 0.9f);
        ImGui::Combo("##SolveAlgo", &solveAlgo, solveNames, IM_ARRAYSIZE(solveNames));

        ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0.5f, 0.7f, 1.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0.6f, 0.8f, 1.0f, 1.0f));
        ImGui::SetNextItemWidth(sliderWidth);
        ImGui::SliderFloat("##Speed", &speedMultiplier, 0.1f, 5.0f, "%.1fx");
        ImGui::SameLine();
        ImGui::Text("Speed");
        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar();

        ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(0.5f, 0.9f, 1.0f, 1.0f));
        ImGui::Checkbox("Step Mode", &stepMode);
        ImGui::PopStyleColor();

        if (solving)
        {
            double liveReal = glfwGetTime() - animStartTime;
            double liveScaled = stepMode ? liveReal : (liveReal * speedMultiplier);

            // Modern status display with responsive background
            ImVec2 statusPos = ImGui::GetCursorScreenPos();
            ImVec2 statusEnd = ImVec2(statusPos.x + availWidth.x * 0.9f, statusPos.y + 30);
            ImU32 statusBg = IM_COL32(40, 80, 60, 160);
            drawList->AddRectFilled(statusPos, statusEnd, statusBg, 8.0f);

            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 1.0f, 0.9f, 1.0f));
            ImGui::Text("⏱️ Elapsed: %.3f seconds", liveScaled);
            ImGui::PopStyleColor();
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5);
        }

        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 20);

        if (!solving && animState == 0)
        {
            // Modern play button with responsive design
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.8f, 0.3f, 0.9f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.4f, 0.9f, 0.4f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.5f, 1.0f, 0.5f, 1.0f));

            float playButtonWidth = availWidth.x * 0.9f;
            if (texPlay && ImGui::ImageButton("play", toImguiTex(texPlay), ImVec2(32, 32)))
            {
                resetAnimationBuffers();
                animStartTime = glfwGetTime();
                lastEventTime = animStartTime;
                if (solveAlgo == 0)
                    solveDFS();
                else if (solveAlgo == 1)
                    solveBFS();
                else if (solveAlgo == 2)
                    solveDijkstra();
                else
                    solveAStar();
                solving = true;
                logf("Solve started with algo %d", solveAlgo);
            }
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 1.0f, 0.9f, 1.0f));
            ImGui::Text("▶️ START SOLVING");
            ImGui::PopStyleColor();
            ImGui::PopStyleColor(3);
        }
        else if (solving && animState == 0)
        {
            // Modern pause button
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.5f, 0.3f, 0.9f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.6f, 0.4f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 0.7f, 0.5f, 1.0f));

            if (texPause && ImGui::ImageButton("pause", toImguiTex(texPause), ImVec2(32, 32)))
            {
                solving = false;
                logf("Paused");
            }
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.9f, 0.7f, 1.0f));
            ImGui::Text("⏸️ PAUSE");
            ImGui::PopStyleColor();
            ImGui::PopStyleColor(3);

            if (stepMode)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.5f, 0.8f, 0.9f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.6f, 0.6f, 0.9f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.7f, 0.7f, 1.0f, 1.0f));

                if (texStep && ImGui::ImageButton("step", toImguiTex(texStep), ImVec2(32, 32)))
                {
                    if (eventIndex < events.size())
                    {
                        auto [u, v, ok, wCost] = events[eventIndex++];
                        if (ok)
                            pushSuccess(u, v);
                        else
                        {
                            if ((solveAlgo == 0) && successVertices.size() >= 4)
                                successVertices.erase(successVertices.end() - 4, successVertices.end());
                            pushFailure(u, v);
                        }
                        if (eventIndex >= events.size())
                        {
                            animState = 1;
                            animEndTime = glfwGetTime();
                            solving = false;
                            logf("Solve finished");
                        }
                    }
                }
                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.9f, 1.0f, 1.0f));
                ImGui::Text("👣 STEP");
                ImGui::PopStyleColor();
                ImGui::PopStyleColor(3);
            }
            // animate auto mode
            if (!stepMode)
            {
                double ct = glfwGetTime();
                while (eventIndex < events.size() &&
                       (ct - lastEventTime) >= (baseDelay / speedMultiplier))
                {
                    auto [u, v, ok, wCost] = events[eventIndex++];
                    if (ok)
                        pushSuccess(u, v);
                    else
                    {
                        if ((solveAlgo == 0) && successVertices.size() >= 4)
                            successVertices.erase(successVertices.end() - 4, successVertices.end());
                        pushFailure(u, v);
                    }
                    lastEventTime += (baseDelay / speedMultiplier);
                    ct = glfwGetTime();
                }
                if (eventIndex >= events.size())
                {
                    animState = 1;
                    animEndTime = glfwGetTime();
                    solving = false;
                }
            }
        }
        else if (!solving && animState == 1)
        {
            // Completion status
            ImGui::SetCursorPosX(30);
            double realElapsed = animEndTime - animStartTime;
            double nominalElapsed = realElapsed * speedMultiplier;

            // Success status background
            ImVec2 successPos = ImGui::GetCursorScreenPos();
            ImVec2 successEnd = ImVec2(successPos.x + 280, successPos.y + 50);
            ImU32 successBg = IM_COL32(40, 80, 40, 200);
            drawList->AddRectFilled(successPos, successEnd, successBg, 6.0f);

            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 1.0f, 0.8f, 1.0f));
            ImGui::Text("✅ COMPLETED!");
            ImGui::Text("Time: %.3f seconds", stepMode ? realElapsed : nominalElapsed);
            ImGui::PopStyleColor();

            ImGui::SetCursorPosX(30);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.5f, 0.5f, 0.8f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.6f, 0.6f, 0.6f, 0.9f));
            if (ImGui::Button("Reset Run", ImVec2(280, 32)))
            {
                resetAnimationBuffers();
                logf("Run reset");
            }
            ImGui::PopStyleColor(2);
        }

        if (!solving)
        {
            ImGui::SetCursorPosX(30);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.6f, 0.3f, 0.8f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.7f, 0.7f, 0.4f, 0.9f));
            if (ImGui::Button("Clear Visualization", ImVec2(280, 32)))
            {
                successVertices.clear();
                failureVertices.clear();
            }
            ImGui::PopStyleColor(2);
        }

        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 25);

        // Display controls section
        // Add some bottom padding
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 30);

        // Footer removed for modern clean look
        ImGui::SetCursorPosX(60);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.6f, 0.8f));
        ImGui::SetWindowFontScale(0.8f);
        // Footer removed for cleaner look
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopStyleColor();

        ImGui::End();
        // Restore original style
        for (int i = 0; i < ImGuiCol_COUNT; ++i)
            s.Colors[i] = origCol[i];

        // -------- THEMED OUTER BACKGROUND --------
        // Draw a purple gradient or color on the area outside the maze
        float xoff, yoff, cell;
        int sz;
        computeViewportAndCell(xoff, yoff, cell, sz);
        ImDrawList *bg = ImGui::GetBackgroundDrawList();
        ImVec2 vp0 = ImGui::GetMainViewport()->Pos;
        ImVec2 vp1 = ImVec2(vp0.x + ImGui::GetMainViewport()->Size.x, vp0.y + ImGui::GetMainViewport()->Size.y);
        // Match sidebar color: bgTop = IM_COL32(25, 25, 35, 250), bgBottom = IM_COL32(15, 15, 25, 250)
        ImU32 grad1 = IM_COL32(25, 25, 35, 250); // sidebar top color
        ImU32 grad2 = IM_COL32(15, 15, 25, 250); // sidebar bottom color
        
        // Left of maze (after sidebar) - use the already calculated responsiveWidth
        bg->AddRectFilledMultiColor(
            ImVec2(vp0.x + responsiveWidth, vp0.y), ImVec2(xoff, vp1.y),
            grad1, grad1, grad2, grad2);
        // Right of maze
        bg->AddRectFilledMultiColor(
            ImVec2(xoff + sz, vp0.y), ImVec2(vp1.x, vp1.y),
            grad2, grad2, grad1, grad1);
        // Top of maze
        bg->AddRectFilledMultiColor(
            ImVec2(xoff, vp0.y), ImVec2(xoff + sz, yoff),
            grad1, grad2, grad1, grad2);
        // Bottom of maze
        bg->AddRectFilledMultiColor(
            ImVec2(xoff, yoff + sz), ImVec2(xoff + sz, vp1.y),
            grad2, grad1, grad2, grad1);

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(gWindow);
    }

    // shutdown
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    deleteAllTextures();
    glfwDestroyWindow(gWindow);
    glfwTerminate();
    logf("Exited cleanly");
    return 0;
}
