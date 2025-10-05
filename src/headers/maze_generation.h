#pragma once

#include "common.h"

// DSU (Disjoint Set Union) for Kruskal's algorithm
struct DSU
{
    std::vector<int> p, r;
    DSU(int n = 0) { reset(n); }
    void reset(int n);
    int find(int x);
    bool unite(int a, int b);
};

// Maze generation functions
std::vector<int> getUnvisitedNeighbors(int x, int y);
void removeWallsAB(int a, int b, int w);
void clearGridVisited();
void ensureMultiplePathways();
void addMazeComplexity();
void generateBacktracker();
void generatePrim();
void generateKruskal();
void pickStartEnd();
void randomizeObstacles(float density);
void clearObstacles();
void regenerateMaze();