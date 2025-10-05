#pragma once

#include "common.h"

// Pathfinding solver functions
void resetAnimationBuffers();
void pushEvent(int u, int v, bool ok, float wCost = 1.0f);
void pushSuccess(int u, int v);
void pushFailure(int u, int v);
void solveDFS();
void solveBFS();
void solveDijkstra();
void solveAStar();