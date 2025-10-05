#pragma once

#include "common.h"

// Drawing and rendering functions
void framebuffer_size_callback(GLFWwindow *, int width, int height);
void buildProjection();
void computeViewportAndCell(float &xoff, float &yoff, float &cell, int &sz);
void drawTexturedLayer();
void drawWallsAsLines();
void drawPathsAsLines();
void buildWallVertices();
void rebuildBorderVAO();
void drawSplashScreen();
void updateSplashScreen();

// Helper functions
ImTextureID toImguiTex(GLuint id);