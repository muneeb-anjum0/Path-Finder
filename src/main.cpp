// main.cpp
// Maze-Runner extended with robust logging and init fallbacks.
// Writes logs to maze_runner.log and stderr. Opens Notepad on fatal.

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
#ifdef _WIN32
#include <windows.h>
#endif

// ======================= LOGGING =========================
static std::ofstream gLog;
static void open_log()
{
    gLog.open("maze_runner.log", std::ios::out | std::ios::trunc);
    if (!gLog) {
        // fallback to stdout only
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
        vsnprintf(buf, sizeof(buf), fmt, ap);
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
    // show a message box and open the log
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
static std::vector<float> wallVertices;
static unsigned int wallVAO = 0, wallVBO = 0, borderVAO = 0, borderVBO = 0;

static bool solving = false;
static int animState = 0; // 0 running, 1 done
static double animStartTime = 0, animEndTime = 0;
static int solveAlgo = 0; // 0 DFS, 1 BFS, 2 Dijkstra, 3 A*
static int genAlgo = 0;   // 0 Backtracker, 1 Prim, 2 Kruskal

static std::vector<std::tuple<int, int, bool, float>> events;
static std::vector<std::pair<int, int>> finalPathEdges;
static std::vector<float> successVertices;
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
static bool useWeights = true;
static int maxWeight = 5;
static float checkerAlpha = 0.07f;

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
    glUseProgram(shader);
    glUniformMatrix4fv(glGetUniformLocation(shader, "uProjection"), 1, GL_FALSE, &proj[0][0]);
}

// ======================= SHADERS =========================
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

// Two versions to match 330 and 150
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
    std::bernoulli_distribution b(density);
    for (int i = 0; i < gCols * gRows; i++)
        grid[i].blocked = b(rng);
    if (grid[startCell].blocked) grid[startCell].blocked = false;
    if (grid[endCell].blocked) grid[endCell].blocked = false;
}
void clearObstacles(){ for (auto &c : grid) c.blocked = false; }

void randomizeWeights(bool enable, int maxW)
{
    if (!enable) { for (auto &c : grid) c.weight = 1; return; }
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

// ======================= SOLVERS =========================
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
            float w = useWeights ? (float)grid[v].weight : 1.0f;
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
            float w = useWeights ? (float)grid[v].weight : 1.0f;
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

// ======================= DRAW SETUP =========================
void buildWallVertices()
{
    wallVertices.clear();
    for (int y = 0; y < gRows; y++)
    {
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
    glBindBuffer(GL_ARRAY_BUFFER, wallVBO);
    glBufferData(GL_ARRAY_BUFFER,
                 wallVertices.size() * sizeof(float),
                 wallVertices.data(),
                 GL_STATIC_DRAW);
}

void rebuildBorderVAO()
{
    std::vector<float> borderVertices = {
        0.0f, 0.0f,
        (float)gCols, 0.0f,
        (float)gCols, (float)gRows,
        0.0f, (float)gRows};
    glBindVertexArray(borderVAO);
    glBindBuffer(GL_ARRAY_BUFFER, borderVBO);
    glBufferData(GL_ARRAY_BUFFER,
                 borderVertices.size() * sizeof(float),
                 borderVertices.data(),
                 GL_STATIC_DRAW);
}

void drawBackgroundCells()
{
    int w, h;
    glfwGetFramebufferSize(gWindow, &w, &h);
    int sz = std::min(w, h), xoff = (w - sz) / 2, yoff = (h - sz) / 2;
    float cell = (float)sz / gCols;
    ImDrawList *dl = ImGui::GetBackgroundDrawList();

    for (int y = 0; y < gRows; y++)
    {
        for (int x = 0; x < gCols; x++)
        {
            float x0 = xoff + x * cell;
            float y0 = yoff + y * cell;
            float x1 = xoff + (x + 1) * cell;
            float y1 = yoff + (y + 1) * cell;

            bool alt = ((x + y) & 1) == 0;
            ImU32 col = alt ? IM_COL32(255, 255, 255, (int)(checkerAlpha * 255))
                            : IM_COL32(255, 255, 255, (int)(checkerAlpha * 180));
            dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), col);

            int i = index(x,y);
            if (grid[i].blocked)
                dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), IM_COL32(60, 60, 60, 200));
            else if (useWeights && grid[i].weight > 1)
            {
                float t = std::min(1.0f, (grid[i].weight - 1) / (float)std::max(1,maxWeight));
                ImU32 wt = IM_COL32((int)(255 * t), (int)(120 * t), 0, 70);
                dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), wt);
            }
        }
    }
}

void drawMarkers()
{
    int w, h;
    glfwGetFramebufferSize(gWindow, &w, &h);
    int sz = std::min(w, h), xoff = (w - sz) / 2, yoff = (h - sz) / 2;
    float cellSize = (float)sz / gCols;
    ImDrawList *dl = ImGui::GetBackgroundDrawList();

    int sx = startCell % gCols, sy = startCell / gCols;
    float cx = xoff + (sx + 0.5f) * cellSize;
    float cy = yoff + (sy + 0.5f) * cellSize;
    dl->AddCircleFilled(ImVec2(cx, cy), cellSize * 0.3f, IM_COL32(255, 0, 0, 255));

    int ex = endCell % gCols, ey = endCell / gCols;
    float pad = cellSize * 0.1f;
    float x0 = xoff + ex * cellSize + pad;
    float y0 = yoff + ey * cellSize + pad;
    float x1 = xoff + (ex + 1) * cellSize - pad;
    float y1 = yoff + (ey + 1) * cellSize - pad;
    dl->AddRect(ImVec2(x0, y0), ImVec2(x1, y1), IM_COL32(0, 255, 0, 255), 0.0f, 0, 2.0f);
}

// ======================= INIT HELPERS =========================
static void glfw_error_cb(int code, const char* desc){
    logf("GLFW error %d: %s", code, desc);
}

static bool init_glfw_and_window()
{
    glfwSetErrorCallback(glfw_error_cb);
    if (!glfwInit()){
        logf("glfwInit failed");
        return false;
    }

    // First try 3.3 core
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, 4);

    gWindow = glfwCreateWindow(1000, 900, "Maze-Runner", nullptr, nullptr);
    if (!gWindow){
        logf("glfwCreateWindow failed for 3.3 core. Trying 3.0 compat.");
        // Fallback to 3.0 no profile
        glfwDefaultWindowHints();
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
        glfwWindowHint(GLFW_SAMPLES, 4);
        gWindow = glfwCreateWindow(1000, 900, "Maze-Runner", nullptr, nullptr);
        if (!gWindow){
            logf("glfwCreateWindow failed for 3.0 compat as well");
            glfwTerminate();
            return false;
        }
    }

    glfwMakeContextCurrent(gWindow);
    glfwSwapInterval(1);
    return true;
}

static bool init_glad_and_dump_info()
{
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)){
        logf("gladLoadGLLoader failed");
        return false;
    }
    const GLubyte* vendor   = glGetString(GL_VENDOR);
    const GLubyte* renderer = glGetString(GL_RENDERER);
    const GLubyte* version  = glGetString(GL_VERSION);
    const GLubyte* glsl     = glGetString(GL_SHADING_LANGUAGE_VERSION);
    logf("GL_VENDOR:   %s", vendor ? (const char*)vendor : "null");
    logf("GL_RENDERER: %s", renderer ? (const char*)renderer : "null");
    logf("GL_VERSION:  %s", version ? (const char*)version : "null");
    logf("GLSL:        %s", glsl ? (const char*)glsl : "null");
    return true;
}

static bool init_imgui_with_fallback(const char** used_glsl)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    if (!ImGui_ImplGlfw_InitForOpenGL(gWindow, true)){
        logf("ImGui_ImplGlfw_InitForOpenGL failed");
        return false;
    }
    if (!ImGui_ImplOpenGL3_Init("#version 330")){
        logf("ImGui_ImplOpenGL3_Init 330 failed. Trying 150.");
        if (!ImGui_ImplOpenGL3_Init("#version 150")){
            logf("ImGui_ImplOpenGL3_Init 150 failed");
            return false;
        }
        *used_glsl = "#version 150";
    } else {
        *used_glsl = "#version 330";
    }
    ImGui::StyleColorsDark();
    return true;
}

static unsigned int try_create_shader_program()
{
    // Prefer 330 shaders
    unsigned int prog = createProgram(VS_330, FS_330);
    GLint linked = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &linked);
    if (linked) return prog;

    logf("Link failed for 330 shaders. Trying 150 shaders.");
    glDeleteProgram(prog);
    prog = createProgram(VS_150, FS_150);
    glGetProgramiv(prog, GL_LINK_STATUS, &linked);
    if (!linked){
        logf("Link failed for 150 shaders too");
        return 0;
    }
    return prog;
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
    // Ensure console for double click
    if (!GetConsoleWindow()){
        AllocConsole();
        FILE* fp;
        freopen_s(&fp, "CONOUT$", "w", stdout);
        freopen_s(&fp, "CONOUT$", "w", stderr);
    }
#endif
    open_log();

    if (!init_glfw_and_window())
        fatal("GLFW window creation failed. See maze_runner.log");

    glfwSetFramebufferSizeCallback(gWindow, framebuffer_size_callback);

    if (!init_glad_and_dump_info())
        fatal("GLAD init failed. Update GPU driver or install latest OpenGL runtime");

    // smoothing and blending
    glEnable(GL_MULTISAMPLE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_LINE_SMOOTH);
    glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);

    const char* imgui_glsl = nullptr;
    if (!init_imgui_with_fallback(&imgui_glsl))
        fatal("ImGui backend init failed. See log");

    // VAOs
    glGenVertexArrays(1, &wallVAO);
    glGenBuffers(1, &wallVBO);
    glBindVertexArray(wallVAO);
    glBindBuffer(GL_ARRAY_BUFFER, wallVBO);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(0);

    glGenVertexArrays(1, &borderVAO);
    glGenBuffers(1, &borderVBO);
    glBindVertexArray(borderVAO);
    glBindBuffer(GL_ARRAY_BUFFER, borderVBO);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(0);

    glGenVertexArrays(1, &failureVAO);
    glGenBuffers(1, &failureVBO);
    glBindVertexArray(failureVAO);
    glBindBuffer(GL_ARRAY_BUFFER, failureVBO);
    glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(0);

    glGenVertexArrays(1, &successVAO);
    glGenBuffers(1, &successVBO);
    glBindVertexArray(successVAO);
    glBindBuffer(GL_ARRAY_BUFFER, successVBO);
    glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(0);

    shader = try_create_shader_program();
    if (!shader)
        fatal("Shader program creation failed. See log for compile and link errors");

    buildProjection();

    // initial maze
    regenerateMaze();
    randomizeWeights(useWeights, maxWeight);

    // border data
    rebuildBorderVAO();

    // animation timing
    const double baseDelay = 0.005;
    double lastEventTime = 0.0;

    logf("Init complete. Using ImGui GLSL: %s", imgui_glsl ? imgui_glsl : "unknown");

    while (!glfwWindowShouldClose(gWindow))
    {
        glfwPollEvents();
        glClearColor(0.05f, 0.05f, 0.10f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        // background and markers
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        drawBackgroundCells();
        drawMarkers();

        // walls
        glUseProgram(shader);
        glLineWidth(1.5f);
        glUniform3f(glGetUniformLocation(shader, "uColor"), 0.60f, 0.40f, 0.80f);
        glBindVertexArray(wallVAO);
        glDrawArrays(GL_LINES, 0, (int)wallVertices.size() / 2);

        // border
        glUniform3f(glGetUniformLocation(shader, "uColor"), 1.00f, 0.50f, 0.70f);
        glBindVertexArray(borderVAO);
        glLineWidth(2.0f);
        glDrawArrays(GL_LINE_LOOP, 0, 4);

        // animate solver
        if (solving && animState == 0 && !stepMode)
        {
            double ct = glfwGetTime();
            while (eventIndex < events.size() &&
                   (ct - lastEventTime) >= (baseDelay / speedMultiplier))
            {
                auto [u, v, ok, wCost] = events[eventIndex++];
                float ux = (u % gCols) + 0.5f, uy = (u / gCols) + 0.5f;
                float vx = (v % gCols) + 0.5f, vy = (v / gCols) + 0.5f;

                if (ok)
                    successVertices.insert(successVertices.end(), {ux, uy, vx, vy});
                else {
                    if ((solveAlgo == 0) && successVertices.size() >= 4)
                        successVertices.erase(successVertices.end() - 4, successVertices.end());
                    failureVertices.insert(failureVertices.end(), {ux, uy, vx, vy});
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

        // draw success path
        glLineWidth(2.5f);
        glUniform3f(glGetUniformLocation(shader, "uColor"), 0.50f, 1.00f, 0.50f);
        glBindVertexArray(successVAO);
        glBindBuffer(GL_ARRAY_BUFFER, successVBO);
        glBufferData(GL_ARRAY_BUFFER,
                     successVertices.size() * sizeof(float),
                     successVertices.data(),
                     GL_DYNAMIC_DRAW);
        glDrawArrays(GL_LINES, 0, (int)successVertices.size() / 2);

        // draw failure
        glLineWidth(1.5f);
        glUniform3f(glGetUniformLocation(shader, "uColor"), 1.00f, 0.60f, 0.60f);
        glBindVertexArray(failureVAO);
        glBindBuffer(GL_ARRAY_BUFFER, failureVBO);
        glBufferData(GL_ARRAY_BUFFER,
                     failureVertices.size() * sizeof(float),
                     failureVertices.data(),
                     GL_DYNAMIC_DRAW);
        glDrawArrays(GL_LINES, 0, (int)failureVertices.size() / 2);

        // UI
        ImGui::Begin("Maze Controls");

        static int uiCols = gCols, uiRows = gRows;
        ImGui::Text("Grid");
        ImGui::SliderInt("Cols", &uiCols, 5, 60);
        ImGui::SliderInt("Rows", &uiRows, 5, 60);
        if ((uiCols != gCols) || (uiRows != gRows))
        {
            if (ImGui::Button("Apply Size"))
            {
                gCols = uiCols; gRows = uiRows;
                regenerateMaze();
                randomizeWeights(useWeights, maxWeight);
                buildProjection();
                rebuildBorderVAO();
                buildWallVertices();
                logf("Applied size C=%d R=%d", gCols, gRows);
            }
        }
        ImGui::Separator();

        ImGui::Text("Generation");
        const char* genNames[] = {"Backtracker", "Prim", "Kruskal"};
        ImGui::Combo("Gen Algo", &genAlgo, genNames, IM_ARRAYSIZE(genNames));
        if (ImGui::Button("Regenerate"))
        {
            regenerateMaze();
            randomizeWeights(useWeights, maxWeight);
            buildWallVertices();
            logf("Regenerated with algo %d", genAlgo);
        }
        ImGui::SameLine();
        if (ImGui::Button("New Start/End"))
            pickStartEnd();

        ImGui::Separator();
        ImGui::Text("Obstacles and Weights");
        ImGui::Checkbox("Use Weights", &useWeights);
        ImGui::SliderInt("Max Weight", &maxWeight, 2, 15);
        ImGui::SliderFloat("Obstacle Density", &obstacleDensity, 0.0f, 0.6f, "%.2f");
        if (ImGui::Button("Random Obstacles"))
            randomizeObstacles(obstacleDensity);
        ImGui::SameLine();
        if (ImGui::Button("Clear Obstacles"))
            clearObstacles();
        if (ImGui::Button("Random Weights"))
            randomizeWeights(useWeights, maxWeight);
        ImGui::SameLine();
        if (ImGui::Button("Clear Weights"))
            randomizeWeights(false, 1);

        ImGui::Separator();
        ImGui::Text("Solving");
        const char* solveNames[] = {"Depth-First", "Breadth-First", "Dijkstra", "A*"};
        ImGui::Combo("Solve Algo", &solveAlgo, solveNames, IM_ARRAYSIZE(solveNames));
        ImGui::SliderFloat("Speed", &speedMultiplier, 0.1f, 5.0f, "%.2fx");
        ImGui::Checkbox("Step Mode", &stepMode);

        if (solving)
        {
            double liveReal = glfwGetTime() - animStartTime;
            double liveScaled = stepMode ? liveReal : (liveReal * speedMultiplier);
            ImGui::Text("Elapsed: %.3f s", liveScaled);
        }

        if (!solving && animState == 0)
        {
            if (ImGui::Button("Start"))
            {
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
        }
        else if (solving && animState == 0)
        {
            if (ImGui::Button("Pause")) { solving = false; logf("Paused"); }
            ImGui::SameLine();
            if (stepMode)
            {
                if (ImGui::Button("Step"))
                {
                    if (eventIndex < events.size())
                    {
                        auto [u, v, ok, wCost] = events[eventIndex++];
                        float ux = (u % gCols) + 0.5f, uy = (u / gCols) + 0.5f;
                        float vx = (v % gCols) + 0.5f, vy = (v / gCols) + 0.5f;
                        if (ok) successVertices.insert(successVertices.end(), {ux, uy, vx, vy});
                        else {
                            if ((solveAlgo == 0) && successVertices.size() >= 4)
                                successVertices.erase(successVertices.end()-4, successVertices.end());
                            failureVertices.insert(failureVertices.end(), {ux, uy, vx, vy});
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
            }
        }
        else if (!solving && animState == 1)
        {
            double realElapsed = animEndTime - animStartTime;
            double nominalElapsed = realElapsed * speedMultiplier;
            ImGui::Text("Elapsed: %.3f s", stepMode ? realElapsed : nominalElapsed);
            if (ImGui::Button("Reset Run")) { resetAnimationBuffers(); logf("Run reset"); }
        }

        if (!solving)
        {
            ImGui::SameLine();
            if (ImGui::Button("Clear Lines"))
            {
                successVertices.clear();
                failureVertices.clear();
            }
        }
        ImGui::Separator();
        ImGui::SliderFloat("Checker Alpha", &checkerAlpha, 0.0f, 0.25f, "%.3f");

        ImGui::End();

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(gWindow);
    }

    // shutdown
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(gWindow);
    glfwTerminate();
    logf("Exited cleanly");
    return 0;
}
