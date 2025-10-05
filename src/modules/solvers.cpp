#include "../headers/solvers.h"
#include "../headers/logging.h"

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

void pushEvent(int u, int v, bool ok, float wCost)
{
    events.emplace_back(u, v, ok, wCost);
}

void pushSuccess(int u, int v)
{
    float ux = (u % gCols) + 0.5f, uy = (u / gCols) + 0.5f;
    float vx = (v % gCols) + 0.5f, vy = (v / gCols) + 0.5f;
    successVertices.insert(successVertices.end(), {ux, uy, vx, vy});
}

void pushFailure(int u, int v)
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