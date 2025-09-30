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
    if (!gLog) {
        fprintf(stderr, "Failed to open maze_runner.log\n");
    } else {
        time_t t = time(nullptr);
        gLog << "Maze Runner log started " << ctime(&t) << "\n";
        gLog.flush();
    }
}
static void logf(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    if (gLog) {
        char buf[4096];
        va_list ap2; va_copy(ap2, ap);
        vsnprintf(buf, sizeof(buf), fmt, ap2);
        va_end(ap2);
        gLog << buf << "\n";
        gLog.flush();
    }
    va_end(ap);
}
[[noreturn]] static void fatal(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char buf[8192];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    fprintf(stderr, "FATAL: %s\n", buf);
    if (gLog) { gLog << "FATAL: " << buf << "\n"; gLog.flush(); }
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
    int weight = 1;
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
static int maxWeight = 5;
static float checkerAlpha = 0.07f;

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

static GLuint loadTexture(const char* path)
{
    int w=0, h=0, n=0;
    stbi_uc* data = stbi_load(path, &w, &h, &n, 4);
    if (!data) {
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
    texStart      = loadTexture("assets/start.png");      // 64x64
    texEnd        = loadTexture("assets/end.png");        // 64x64
    texObstacle   = loadTexture("assets/obsticle.png");   // 64x64
    texLineHori   = loadTexture("assets/lineHori.png");   // horizontal wall
    texLineVerti  = loadTexture("assets/lineVerti.png");  // vertical wall
    texWall       = 0; // legacy, not used
    texPlay       = loadTexture("assets/play.png");
    texPause      = loadTexture("assets/pause.png");
    texRegen      = loadTexture("assets/regen.png");
    texSettings   = loadTexture("assets/setting.png");
    texStep       = loadTexture("assets/step.png");
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
    for (auto &c : grid) c.visited = false;
}

void buildProjection()
{
    proj = glm::ortho(0.0f, (float)gCols, (float)gRows, 0.0f);
    if (shader) {
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
unsigned int createProgram(const char* vs, const char* fs)
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
    if (!linked){
        GLint len = 0; glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &len);
        std::vector<char> log(len);
        glGetProgramInfoLog(prog, len, nullptr, log.data());
        logf("Program link error:\n%s", log.data());
    }
    return prog;
}

static const char* VS_330 =
    "#version 330 core\n"
    "layout(location=0) in vec2 aPos;\n"
    "uniform mat4 uProjection;\n"
    "void main(){ gl_Position = uProjection * vec4(aPos,0,1); }\n";
static const char* FS_330 =
    "#version 330 core\n"
    "out vec4 FragColor;\n"
    "uniform vec3 uColor;\n"
    "void main(){ FragColor = vec4(uColor,1); }\n";

static const char* VS_150 =
    "#version 150\n"
    "in vec2 aPos;\n"
    "uniform mat4 uProjection;\n"
    "void main(){ gl_Position = uProjection * vec4(aPos,0,1); }\n";
static const char* FS_150 =
    "#version 150\n"
    "out vec4 FragColor;\n"
    "uniform vec3 uColor;\n"
    "void main(){ FragColor = vec4(uColor,1); }\n";

// ======================= MAZE GEN =========================
struct DSU {
    std::vector<int> p, r;
    DSU(int n=0){ reset(n); }
    void reset(int n){
        p.resize(n); r.assign(n,0);
        for(int i=0;i<n;i++) p[i]=i;
    }
    int find(int x){ return p[x]==x?x:p[x]=find(p[x]); }
    bool unite(int a,int b){
        a=find(a); b=find(b);
        if(a==b) return false;
        if(r[a]<r[b]) std::swap(a,b);
        p[b]=a;
        if(r[a]==r[b]) r[a]++;
        return true;
    }
};

void generateBacktracker()
{
    grid.assign(gCols * gRows, Cell());
    std::stack<int> st;
    int visitedCount = 1, total = gCols * gRows, current = 0;
    grid[0].visited = true;
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
            current = st.top(); st.pop();
        }
    }
    clearGridVisited();
}

void generatePrim()
{
    grid.assign(gCols * gRows, Cell());
    std::uniform_int_distribution<int> sx(0, gCols-1), sy(0, gRows-1);
    int cx = sx(rng), cy = sy(rng);
    int start = index(cx, cy);
    grid[start].visited = true;

    struct Edge { int a, b, w; };
    std::vector<Edge> frontier;
    auto addFrontier = [&](int x, int y){
        static const int d[4][3]={{0,-1,0},{1,0,1},{0,1,2},{-1,0,3}};
        int a = index(x,y);
        for(auto &dd:d){
            int nx=x+dd[0], ny=y+dd[1];
            int b = index(nx,ny);
            if(b!=-1 && !grid[b].visited){
                frontier.push_back({a,b,dd[2]});
            }
        }
    };
    addFrontier(cx,cy);
    while(!frontier.empty()){
        std::uniform_int_distribution<size_t> pick(0, frontier.size()-1);
        size_t k = pick(rng);
        Edge e = frontier[k];
        frontier[k] = frontier.back(); frontier.pop_back();
        if(grid[e.b].visited) continue;
        removeWallsAB(e.a, e.b, e.w);
        grid[e.b].visited = true;
        int bx = e.b % gCols, by = e.b / gCols;
        addFrontier(bx,by);
    }
    clearGridVisited();
}

void generateKruskal()
{
    grid.assign(gCols * gRows, Cell());
    int N = gCols * gRows;
    DSU dsu(N);
    struct Edge { int a,b,w; };
    std::vector<Edge> edges;
    static const int d[2][3]={{1,0,1},{0,1,2}};
    for(int y=0;y<gRows;y++){
        for(int x=0;x<gCols;x++){
            int a = index(x,y);
            for(auto &dd:d){
                int nx=x+dd[0], ny=y+dd[1];
                int b = index(nx,ny);
                if(b!=-1) edges.push_back({a,b,dd[2]});
            }
        }
    }
    std::shuffle(edges.begin(), edges.end(), rng);
    for(auto &e: edges){
        if(dsu.unite(e.a, e.b)){
            removeWallsAB(e.a, e.b, e.w);
        }
    }
    clearGridVisited();
}

void pickStartEnd()
{
    std::vector<int> cand = {
        index(0, 0),
        index(gCols - 1, 0),
        index(0, gRows - 1),
        index(gCols - 1, gRows - 1),
        index(gCols / 2, gRows / 2)};
    std::uniform_int_distribution<int> dc(0, (int)cand.size() - 1);
    startCell = cand[dc(rng)];
    do { endCell = cand[dc(rng)]; } while (endCell == startCell || grid[endCell].blocked);
    if (grid[startCell].blocked) grid[startCell].blocked = false;
}

void randomizeObstacles(float density)
{
    // 1. Find a path from start to end (BFS)
    std::vector<int> parent(gCols * gRows, -1);
    std::vector<bool> vis(gCols * gRows, false);
    std::queue<int> q;
    q.push(startCell);
    vis[startCell] = true;
    static const int dirs[4][3] = {{0, -1, 0}, {1, 0, 1}, {0, 1, 2}, {-1, 0, 3}};
    bool found = false;
    while (!q.empty()) {
        int u = q.front(); q.pop();
        if (u == endCell) { found = true; break; }
        int x = u % gCols, y = u / gCols;
        for (auto &d : dirs) {
            int v = index(x + d[0], y + d[1]);
            if (v < 0 || grid[u].walls[d[2]] || vis[v]) continue;
            vis[v] = true;
            parent[v] = u;
            q.push(v);
        }
    }
    // 2. Mark the path as protected
    std::vector<bool> protectedPath(gCols * gRows, false);
    if (found) {
        int cur = endCell;
        while (cur != -1) {
            protectedPath[cur] = true;
            cur = parent[cur];
        }
    } else {
        // fallback: no path, so don't block anything
        for (auto &c : grid) c.blocked = false;
        return;
    }
    // 3. Place obstacles randomly, but never on the protected path, start, or end
    std::bernoulli_distribution b(density);
    int minObstacles = std::max(1, (int)(gCols * gRows * 0.05f)); // at least 5% of grid
    std::vector<int> candidates;
    int placed = 0;
    for (int i = 0; i < gCols * gRows; i++) {
        if (protectedPath[i] || i == startCell || i == endCell) {
            grid[i].blocked = false;
        } else {
            candidates.push_back(i);
            bool block = b(rng);
            grid[i].blocked = block;
            if (block) placed++;
        }
    }
    // If not enough obstacles placed, force some
    if (placed < minObstacles && !candidates.empty()) {
        std::shuffle(candidates.begin(), candidates.end(), rng);
        for (int j = 0; j < minObstacles && j < (int)candidates.size(); ++j) {
            grid[candidates[j]].blocked = true;
        }
    }
}
void clearObstacles(){ for (auto &c : grid) c.blocked = false; }

void randomizeWeights(int maxW)
{
    std::uniform_int_distribution<int> w(1, std::max(1,maxW));
    for (auto &c : grid) c.weight = w(rng);
    grid[startCell].weight = 1;
    grid[endCell].weight = 1;
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

inline void pushEvent(int u, int v, bool ok, float wCost=1.0f)
{
    events.emplace_back(u, v, ok, wCost);
}

// ======================= DRAW HELPERS =========================
static inline ImTextureID toImguiTex(GLuint id){ return (ImTextureID)(intptr_t)id; }

static void computeViewportAndCell(float& xoff, float& yoff, float& cell, int& sz)
{
    int w, h;
    glfwGetFramebufferSize(gWindow, &w, &h);
    sz = std::min(w, h);
    xoff = (float)(w - sz) * 0.5f;
    yoff = (float)(h - sz) * 0.5f;
    cell = (float)sz / (float)gCols;
}

// background, obstacles, start/end as images
void drawTexturedLayer()
{
    float xoff, yoff, cell; int sz;
    computeViewportAndCell(xoff, yoff, cell, sz);
    ImDrawList* dl = ImGui::GetBackgroundDrawList();

    // Background image fills the square area
    if (texBackground)
        dl->AddImage(toImguiTex(texBackground),
                     ImVec2(xoff, yoff),
                     ImVec2(xoff + sz, yoff + sz));

    // light checker to keep grid sense
    for (int y = 0; y < gRows; y++)
    for (int x = 0; x < gCols; x++)
    {
        float x0 = xoff + x * cell;
        float y0 = yoff + y * cell;
        float x1 = x0 + cell;
        float y1 = y0 + cell;
        bool alt = ((x + y) & 1) == 0;
        ImU32 col = alt ? IM_COL32(255, 255, 255, (int)(checkerAlpha * 255))
                        : IM_COL32(255, 255, 255, (int)(checkerAlpha * 180));
        dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), col);
    }

    // obstacles
    if (texObstacle)
    {
        for (int i = 0; i < gCols * gRows; i++)
        {
            if (!grid[i].blocked) continue;
            int x = i % gCols, y = i / gCols;
            float x0 = xoff + x * cell, y0 = yoff + y * cell;
            float x1 = x0 + cell,     y1 = y0 + cell;
            dl->AddImage(toImguiTex(texObstacle), ImVec2(x0,y0), ImVec2(x1,y1));
        }
    }

    // start
    if (texStart)
    {
        int sx = startCell % gCols, sy = startCell / gCols;
        float x0 = xoff + sx * cell, y0 = yoff + sy * cell;
        dl->AddImage(toImguiTex(texStart), ImVec2(x0,y0), ImVec2(x0+cell,y0+cell));
    }
    // end
    if (texEnd)
    {
        int ex = endCell % gCols, ey = endCell / gCols;
        float x0 = xoff + ex * cell, y0 = yoff + ey * cell;
        dl->AddImage(toImguiTex(texEnd), ImVec2(x0,y0), ImVec2(x0+cell,y0+cell));
    }
}

// draw maze walls as textured images (line.png)
void drawWallsAsLines()
{
    float xoff, yoff, cell; int sz;
    computeViewportAndCell(xoff, yoff, cell, sz);
    ImDrawList* dl = ImGui::GetBackgroundDrawList();

    float thickness = cell * 0.08f; // consistent thickness
    for (int y = 0; y < gRows; ++y)
    for (int x = 0; x < gCols; ++x)
    {
        int i = index(x,y);
        float xf = xoff + x * cell;
        float yf = yoff + y * cell;
        // Top wall (horizontal)
        if (grid[i].walls[0] && texLineHori) {
            ImVec2 p0(xf, yf);
            ImVec2 p1(xf + cell, yf + thickness);
            dl->AddImage(toImguiTex(texLineHori), p0, p1);
        }
        // Right wall (vertical)
        if (grid[i].walls[1] && texLineVerti) {
            ImVec2 p0(xf + cell - thickness, yf);
            ImVec2 p1(xf + cell, yf + cell);
            dl->AddImage(toImguiTex(texLineVerti), p0, p1);
        }
        // Bottom wall (horizontal)
        if (grid[i].walls[2] && texLineHori) {
            ImVec2 p0(xf, yf + cell - thickness);
            ImVec2 p1(xf + cell, yf + cell);
            dl->AddImage(toImguiTex(texLineHori), p0, p1);
        }
        // Left wall (vertical)
        if (grid[i].walls[3] && texLineVerti) {
            ImVec2 p0(xf, yf);
            ImVec2 p1(xf + thickness, yf + cell);
            dl->AddImage(toImguiTex(texLineVerti), p0, p1);
        }
    }

    // outer border
    const ImU32 borderCol = IM_COL32(255, 128, 179, 255); // 1.0,0.5,0.7
    float x0 = xoff, y0 = yoff, x1 = xoff + sz, y1 = yoff + sz;
    dl->AddRect(ImVec2(x0,y0), ImVec2(x1,y1), borderCol, 0.0f, 0, std::max(2.0f, cell * 0.08f));
}

// draw success (purple) and failure (red) path segments as ImGui lines
void drawPathsAsLines()
{
    float xoff, yoff, cell; int sz;
    computeViewportAndCell(xoff, yoff, cell, sz);
    ImDrawList* dl = ImGui::GetBackgroundDrawList();

    auto drawPairs = [&](const std::vector<float>& v, ImU32 c, float thick)
    {
        for (size_t i = 0; i + 3 < v.size(); i += 4)
        {
            float ux = xoff + v[i+0] * cell;
            float uy = yoff + v[i+1] * cell;
            float vx = xoff + v[i+2] * cell;
            float vy = yoff + v[i+3] * cell;
            dl->AddLine(ImVec2(ux,uy), ImVec2(vx,vy), c, thick);
        }
    };

    float thickSuccess = std::max(2.0f, cell * 0.10f);
    float thickFail    = std::max(1.5f, cell * 0.06f);
    drawPairs(successVertices, IM_COL32(180, 80, 255, 255), thickSuccess); // purple
    drawPairs(failureVertices, IM_COL32(255,153,153,255), thickFail);
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
        if (grid[i].walls[0]) wallVertices.insert(wallVertices.end(), {xf, yf, xf + 1, yf});
        if (grid[i].walls[1]) wallVertices.insert(wallVertices.end(), {xf + 1, yf, xf + 1, yf + 1});
        if (grid[i].walls[2]) wallVertices.insert(wallVertices.end(), {xf + 1, yf + 1, xf, yf + 1});
        if (grid[i].walls[3]) wallVertices.insert(wallVertices.end(), {xf, yf + 1, xf, yf});
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
        if (u == endCell) return true;
        int x = u % gCols, y = u / gCols;
        static const int dirs[4][3] = {{0, -1, 0}, {1, 0, 1}, {0, 1, 2}, {-1, 0, 3}};
        for (auto &d : dirs)
        {
            int v = index(x + d[0], y + d[1]);
            if (v < 0 || grid[u].walls[d[2]] || vis[v] || grid[v].blocked) continue;
            vis[v] = true;
            pushEvent(u, v, true);
            if (dfsRec(v)) return true;
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
    vis[startCell] = true; q.push(startCell);
    static const int dirs[4][3] = {{0, -1, 0}, {1, 0, 1}, {0, 1, 2}, {-1, 0, 3}};
    while (!q.empty())
    {
        int u = q.front(); q.pop();
        if (u == endCell) break;
        int x = u % gCols, y = u / gCols;
        for (auto &d : dirs)
        {
            int v = index(x + d[0], y + d[1]);
            if (v < 0 || grid[u].walls[d[2]] || vis[v] || grid[v].blocked) continue;
            pushEvent(u, v, false);
            vis[v] = true; parent[v] = u; q.push(v);
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
    std::set<std::pair<int,int>> pathSet(finalPathEdges.begin(), finalPathEdges.end());
    for (auto &e : events)
    {
        int u,v; bool ok; float w;
        std::tie(u,v,ok,w) = e;
        if (pathSet.count({u,v})) std::get<2>(e) = true;
    }
}

void solveDijkstra()
{
    int N = gCols * gRows;
    const float INF = std::numeric_limits<float>::infinity();
    std::vector<float> dist(N, INF);
    std::vector<int> parent(N, -1);
    using P = std::pair<float,int>;
    std::priority_queue<P, std::vector<P>, std::greater<P>> pq;

    dist[startCell] = 0;
    pq.push({0, startCell});
    static const int dirs[4][3] = {{0, -1, 0}, {1, 0, 1}, {0, 1, 2}, {-1, 0, 3}};

    while(!pq.empty())
    {
        auto [du,u] = pq.top(); pq.pop();
        if (du!=dist[u]) continue;
        if (u == endCell) break;
        int x = u % gCols, y = u / gCols;
        for (auto &d : dirs)
        {
            int v = index(x + d[0], y + d[1]);
            if (v < 0 || grid[u].walls[d[2]] || grid[v].blocked) continue;
            float w = (float)grid[v].weight;
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
    std::set<std::pair<int,int>> pathSet(finalPathEdges.begin(), finalPathEdges.end());
    for (auto &e : events)
    {
        int u,v; bool ok; float w;
        std::tie(u,v,ok,w) = e;
        if (pathSet.count({u,v})) std::get<2>(e) = true;
    }
}

void solveAStar()
{
    auto h = [&](int a){
        int ax = a % gCols, ay = a / gCols;
        int ex = endCell % gCols, ey = endCell / gCols;
        return (float)(abs(ax-ex) + abs(ay-ey));
    };
    int N = gCols * gRows;
    const float INF = std::numeric_limits<float>::infinity();
    std::vector<float> gScore(N, INF), fScore(N, INF);
    std::vector<int> parent(N, -1);
    using P = std::pair<float,int>;
    std::priority_queue<P, std::vector<P>, std::greater<P>> open;

    gScore[startCell]=0;
    fScore[startCell]=h(startCell);
    open.push({fScore[startCell], startCell});
    static const int dirs[4][3] = {{0, -1, 0}, {1, 0, 1}, {0, 1, 2}, {-1, 0, 3}};

    while(!open.empty())
    {
        auto [f,u]=open.top(); open.pop();
        if (f!=fScore[u]) continue;
        if (u==endCell) break;
        int x = u % gCols, y = u / gCols;
        for (auto &d : dirs)
        {
            int v = index(x + d[0], y + d[1]);
            if (v < 0 || grid[u].walls[d[2]] || grid[v].blocked) continue;
            float w = (float)grid[v].weight;
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
    std::set<std::pair<int,int>> pathSet(finalPathEdges.begin(), finalPathEdges.end());
    for (auto &e : events)
    {
        int u,v; bool ok; float w;
        std::tie(u,v,ok,w) = e;
        if (pathSet.count({u,v})) std::get<2>(e) = true;
    }
}

// ======================= REGEN AND UI =========================
void regenerateMaze()
{
    resetAnimationBuffers();
    if (genAlgo == 0) generateBacktracker();
    else if (genAlgo == 1) generatePrim();
    else generateKruskal();
    pickStartEnd();
    buildWallVertices();
}

int main()
{
#ifdef _WIN32
    if (!GetConsoleWindow()){
        AllocConsole();
        FILE* fp;
        freopen_s(&fp, "CONOUT$", "w", stdout);
        freopen_s(&fp, "CONOUT$", "w", stderr);
    }
#endif
    open_log();

    // GLFW + Window
    glfwSetErrorCallback([](int c, const char* d){ logf("GLFW error %d: %s", c, d); });
    if (!glfwInit()) fatal("glfwInit failed");
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, 4);
    gWindow = glfwCreateWindow(1000, 900, "Maze-Runner", nullptr, nullptr);
    if (!gWindow){
        glfwDefaultWindowHints();
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
        glfwWindowHint(GLFW_SAMPLES, 4);
        gWindow = glfwCreateWindow(1000, 900, "Maze-Runner", nullptr, nullptr);
        if (!gWindow) fatal("glfwCreateWindow failed");
    }
    glfwMaximizeWindow(gWindow); // Start maximized, not fullscreen

    // Set app icon using assets/logo.png
    {
        int iconW = 0, iconH = 0, iconC = 0;
        unsigned char* iconPixels = stbi_load("assets/logo.png", &iconW, &iconH, &iconC, 4);
        if (iconPixels && iconW > 0 && iconH > 0) {
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
    if (!ImGui_ImplGlfw_InitForOpenGL(gWindow, true)) fatal("ImGui_ImplGlfw_InitForOpenGL failed");
    if (!ImGui_ImplOpenGL3_Init("#version 330"))
        if (!ImGui_ImplOpenGL3_Init("#version 150")) fatal("ImGui_ImplOpenGL3_Init failed");
    // Themed ImGui style
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 12.0f;
    style.FrameRounding = 8.0f;
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.13f, 0.10f, 0.18f, 0.95f);
    style.Colors[ImGuiCol_TitleBg] = ImVec4(0.22f, 0.16f, 0.32f, 1.0f);
    style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.32f, 0.22f, 0.52f, 1.0f);
    style.Colors[ImGuiCol_Button] = ImVec4(0.32f, 0.22f, 0.52f, 0.85f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.45f, 0.32f, 0.70f, 1.0f);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.60f, 0.40f, 0.80f, 1.0f);
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.18f, 0.13f, 0.25f, 1.0f);
    style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.32f, 0.22f, 0.52f, 1.0f);
    style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.45f, 0.32f, 0.70f, 1.0f);

    // (Shaders/VAOs kept, but not required for walls now)
    shader = createProgram(VS_330, FS_330);

    // Textures
    stbi_set_flip_vertically_on_load(false);
    loadAllTextures();

    // Initial maze
    regenerateMaze();
    randomizeWeights(maxWeight);

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
        ImGuiStyle& s = ImGui::GetStyle();
        ImVec4 origCol[ImGuiCol_COUNT];
        for (int i = 0; i < ImGuiCol_COUNT; ++i) origCol[i] = s.Colors[i];
    s.Colors[ImGuiCol_WindowBg] = ImVec4(0.97f, 0.95f, 1.0f, 0.99f);
    s.Colors[ImGuiCol_TitleBg] = ImVec4(0.70f, 0.55f, 0.95f, 1.0f);
    s.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.80f, 0.60f, 1.0f, 1.0f);
    s.Colors[ImGuiCol_Button] = ImVec4(0.80f, 0.60f, 1.0f, 0.85f);
    s.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.90f, 0.80f, 1.0f, 1.0f);
    s.Colors[ImGuiCol_ButtonActive] = ImVec4(0.60f, 0.40f, 0.80f, 1.0f);
    s.Colors[ImGuiCol_FrameBg] = ImVec4(0.93f, 0.90f, 1.0f, 1.0f);
    s.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.80f, 0.60f, 1.0f, 1.0f);
    s.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.70f, 0.55f, 0.95f, 1.0f);
    s.Colors[ImGuiCol_Text] = ImVec4(0.30f, 0.10f, 0.40f, 1.0f);
    s.Colors[ImGuiCol_SliderGrab] = ImVec4(0.80f, 0.60f, 1.0f, 1.0f);
    s.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.60f, 0.40f, 0.80f, 1.0f);
    s.Colors[ImGuiCol_CheckMark] = ImVec4(0.60f, 0.40f, 0.80f, 1.0f);
    s.WindowRounding = 0.0f; // Square edges for sidebar

        // Sidebar: fixed to the left, full height
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        float sidebarWidth = 340.0f;
        ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(sidebarWidth, viewport->Size.y), ImGuiCond_Always);
        ImGui::Begin("Maze Controls", nullptr,
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollbar);

        // Sidebar header
    ImGui::SetCursorPosY(24);
    ImGui::SetCursorPosX(24);
    ImGui::PushFont(ImGui::GetFont());
    ImGui::TextColored(ImVec4(0.60f, 0.40f, 0.80f, 1.0f), "\xef\x9a\x99  Maze Runner");
    ImGui::PopFont();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4);
    ImGui::Separator();
        // Grid controls
        ImGui::Text("Grid");
        static int uiCols = gCols, uiRows = gRows;
        ImGui::SliderInt("Cols", &uiCols, 5, 60);
        ImGui::SliderInt("Rows", &uiRows, 5, 60);
        if ((uiCols != gCols) || (uiRows != gRows)) {
            if (texSettings && ImGui::ImageButton("settings", toImguiTex(texSettings), ImVec2(28,28))) {
                gCols = uiCols; gRows = uiRows;
                regenerateMaze();
                randomizeWeights(maxWeight);
                buildProjection();
                logf("Applied size C=%d R=%d", gCols, gRows);
            }
            ImGui::SameLine();
            ImGui::Text("Apply Size");
        }
        ImGui::Separator();
        // Generation controls
        ImGui::Text("Generation");
        const char* genNames[] = {"Backtracker", "Prim", "Kruskal"};
        ImGui::Combo("Gen Algo", &genAlgo, genNames, IM_ARRAYSIZE(genNames));
        if (texRegen && ImGui::ImageButton("regen", toImguiTex(texRegen), ImVec2(28,28))) {
            regenerateMaze();
            randomizeWeights(maxWeight);
            logf("Regenerated with algo %d", genAlgo);
        }
        ImGui::SameLine();
        ImGui::Text("Regenerate");
        ImGui::SameLine();
        if (ImGui::Button("New Start/End"))
            pickStartEnd();
        ImGui::Separator();
        // Obstacles/Weights controls
        ImGui::Text("Obstacles / Weights");
        ImGui::SliderInt("Max Weight", &maxWeight, 2, 15);
        ImGui::SliderFloat("Obstacle Density", &obstacleDensity, 0.0f, 0.6f, "%.2f");
        if (ImGui::Button("Random Obstacles"))
            randomizeObstacles(obstacleDensity);
        ImGui::SameLine();
        if (ImGui::Button("Clear Obstacles"))
            clearObstacles();
        if (ImGui::Button("Random Weights"))
            randomizeWeights(maxWeight);
        ImGui::SameLine();
        if (ImGui::Button("Clear Weights")) {
            for (auto &c : grid) c.weight = 1;
        }
        ImGui::Separator();
        // Solving controls
        ImGui::Text("Solving");
        const char* solveNames[] = {"Depth-First", "Breadth-First", "Dijkstra", "A*"};
        ImGui::Combo("Solve Algo", &solveAlgo, solveNames, IM_ARRAYSIZE(solveNames));
        ImGui::SliderFloat("Speed", &speedMultiplier, 0.1f, 5.0f, "%.2fx");
        ImGui::Checkbox("Step Mode", &stepMode);
        if (solving) {
            double liveReal = glfwGetTime() - animStartTime;
            double liveScaled = stepMode ? liveReal : (liveReal * speedMultiplier);
            ImGui::Text("Elapsed: %.3f s", liveScaled);
        }
        if (!solving && animState == 0) {
            if (texPlay && ImGui::ImageButton("play", toImguiTex(texPlay), ImVec2(32,32))) {
                resetAnimationBuffers();
                animStartTime = glfwGetTime();
                lastEventTime = animStartTime;
                if (solveAlgo == 0) solveDFS();
                else if (solveAlgo == 1) solveBFS();
                else if (solveAlgo == 2) solveDijkstra();
                else solveAStar();
                solving = true;
                logf("Solve started with algo %d", solveAlgo);
            }
            ImGui::SameLine();
            ImGui::Text("Play");
        } else if (solving && animState == 0) {
            if (texPause && ImGui::ImageButton("pause", toImguiTex(texPause), ImVec2(32,32))) { solving = false; logf("Paused"); }
            ImGui::SameLine();
            ImGui::Text("Pause");
            if (stepMode) {
                if (texStep && ImGui::ImageButton("step", toImguiTex(texStep), ImVec2(32,32))) {
                    if (eventIndex < events.size()) {
                        auto [u, v, ok, wCost] = events[eventIndex++];
                        if (ok) pushSuccess(u,v);
                        else {
                            if ((solveAlgo == 0) && successVertices.size() >= 4)
                                successVertices.erase(successVertices.end()-4, successVertices.end());
                            pushFailure(u,v);
                        }
                        if (eventIndex >= events.size()) {
                            animState = 1;
                            animEndTime = glfwGetTime();
                            solving = false;
                            logf("Solve finished");
                        }
                    }
                }
                ImGui::SameLine();
                ImGui::Text("Step");
            }
            // animate auto mode
            if (!stepMode) {
                double ct = glfwGetTime();
                while (eventIndex < events.size() &&
                       (ct - lastEventTime) >= (baseDelay / speedMultiplier)) {
                    auto [u, v, ok, wCost] = events[eventIndex++];
                    if (ok) pushSuccess(u,v);
                    else {
                        if ((solveAlgo == 0) && successVertices.size() >= 4)
                            successVertices.erase(successVertices.end()-4, successVertices.end());
                        pushFailure(u,v);
                    }
                    lastEventTime += (baseDelay / speedMultiplier);
                    ct = glfwGetTime();
                }
                if (eventIndex >= events.size()) {
                    animState = 1;
                    animEndTime = glfwGetTime();
                    solving = false;
                }
            }
        } else if (!solving && animState == 1) {
            double realElapsed = animEndTime - animStartTime;
            double nominalElapsed = realElapsed * speedMultiplier;
            ImGui::Text("Elapsed: %.3f s", stepMode ? realElapsed : nominalElapsed);
            if (ImGui::Button("Reset Run")) { resetAnimationBuffers(); logf("Run reset"); }
        }
        if (!solving) {
            ImGui::SameLine();
            if (ImGui::Button("Clear Lines")) {
                successVertices.clear();
                failureVertices.clear();
            }
        }
        ImGui::Separator();
        // Display controls
        ImGui::Text("Display");
        ImGui::SliderFloat("Checker Alpha", &checkerAlpha, 0.0f, 0.25f, "%.3f");
    // ...existing code...
        ImGui::End();
        // Restore original style
        for (int i = 0; i < ImGuiCol_COUNT; ++i) s.Colors[i] = origCol[i];

        // -------- THEMED OUTER BACKGROUND --------
        // Draw a purple gradient or color on the area outside the maze
        float xoff, yoff, cell; int sz;
        computeViewportAndCell(xoff, yoff, cell, sz);
        ImDrawList* bg = ImGui::GetBackgroundDrawList();
        ImVec2 vp0 = ImGui::GetMainViewport()->Pos;
    ImVec2 vp1 = ImVec2(vp0.x + ImGui::GetMainViewport()->Size.x, vp0.y + ImGui::GetMainViewport()->Size.y);
        ImU32 grad1 = IM_COL32(220, 200, 255, 255); // light purple
        ImU32 grad2 = IM_COL32(180, 140, 255, 255); // deeper purple
        // Left of maze (after sidebar)
        bg->AddRectFilledMultiColor(
            ImVec2(vp0.x + sidebarWidth, vp0.y), ImVec2(xoff, vp1.y),
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
