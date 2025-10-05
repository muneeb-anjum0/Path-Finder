#include "../headers/rendering.h"
#include "../headers/logging.h"

void framebuffer_size_callback(GLFWwindow *, int width, int height)
{
    int sz = std::min(width, height);
    int xoff = (width - sz) / 2;
    int yoff = (height - sz) / 2;
    glViewport(xoff, yoff, sz, sz);
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

ImTextureID toImguiTex(GLuint id) { return (ImTextureID)(intptr_t)id; }

void computeViewportAndCell(float &xoff, float &yoff, float &cell, int &sz)
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

void drawSplashScreen()
{
    if (!showSplashScreen && !splashScreenAnimating) return;
    
    ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImDrawList *dl = ImGui::GetBackgroundDrawList();
    
    float windowWidth = viewport->Size.x;
    float windowHeight = viewport->Size.y;
    
    // Calculate splash screen position with swipe animation
    float yOffset = splashScreenOffset;
    
    if (texSplashScreen)
    {
        ImVec2 splashPos = ImVec2(viewport->Pos.x, viewport->Pos.y + yOffset);
        ImVec2 splashEnd = ImVec2(viewport->Pos.x + windowWidth, viewport->Pos.y + windowHeight + yOffset);
        dl->AddImage(toImguiTex(texSplashScreen), splashPos, splashEnd);
    }
    
    // If animation is complete, hide splash screen
    if (splashScreenAnimating && splashScreenOffset <= -windowHeight)
    {
        showSplashScreen = false;
        splashScreenAnimating = false;
    }
}

void updateSplashScreen()
{
    if (!splashScreenAnimating) return;
    
    double currentTime = glfwGetTime();
    double elapsed = currentTime - splashScreenStartTime;
    float animationDuration = 0.8f; // 0.8 seconds for swipe animation
    
    if (elapsed < animationDuration)
    {
        // Smooth easing function (ease-out)
        float progress = elapsed / animationDuration;
        float eased = 1.0f - (1.0f - progress) * (1.0f - progress);
        
        ImGuiViewport *viewport = ImGui::GetMainViewport();
        splashScreenOffset = -eased * viewport->Size.y;
    }
    else
    {
        // Animation complete
        ImGuiViewport *viewport = ImGui::GetMainViewport();
        splashScreenOffset = -viewport->Size.y;
        splashScreenAnimating = false;
        showSplashScreen = false;
    }
}