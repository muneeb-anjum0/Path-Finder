#include "../headers/maze_generation.h"
#include "../headers/logging.h"
#include "../headers/solvers.h"
#include "../headers/rendering.h"
#include "../headers/solvers.h"
#include "../headers/rendering.h"

void DSU::reset(int n)
{
    p.resize(n);
    r.assign(n, 0);
    for (int i = 0; i < n; i++)
        p[i] = i;
}

int DSU::find(int x) 
{ 
    return p[x] == x ? x : p[x] = find(p[x]); 
}

bool DSU::unite(int a, int b)
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