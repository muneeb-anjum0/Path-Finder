#include "headers/common.h"
#include "headers/logging.h"
#include "headers/textures.h"
#include "headers/shaders.h"
#include "headers/maze_generation.h"
#include "headers/solvers.h"
#include "headers/rendering.h"

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

    // Set app icon using assets/logo.png
    {
        int iconW = 0, iconH = 0, iconC = 0;
        unsigned char *iconPixels = stbi_load("assets/logo.png", &iconW, &iconH, &iconC, 4);
        if (iconPixels && iconW > 0 && iconH > 0)
        {
            logf("Loaded window icon: %dx%d pixels, %d channels", iconW, iconH, iconC);
            GLFWimage icon;
            icon.width = iconW;
            icon.height = iconH;
            icon.pixels = iconPixels;
            glfwSetWindowIcon(gWindow, 1, &icon);
            stbi_image_free(iconPixels);
            logf("Window icon set successfully");
        }
        else
        {
            logf("Failed to load window icon from assets/logo.png");
            if (iconPixels) stbi_image_free(iconPixels);
        }
    }

    // Initial maze
    regenerateMaze();

    const double baseDelay = 0.005;
    double lastEventTime = 0.0;

    while (!glfwWindowShouldClose(gWindow))
    {
        glfwPollEvents();
        
        // Handle splash screen click
        if (showSplashScreen && !splashScreenAnimating)
        {
            if (glfwGetMouseButton(gWindow, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS ||
                glfwGetMouseButton(gWindow, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS ||
                glfwGetKey(gWindow, GLFW_KEY_SPACE) == GLFW_PRESS ||
                glfwGetKey(gWindow, GLFW_KEY_ENTER) == GLFW_PRESS ||
                glfwGetKey(gWindow, GLFW_KEY_ESCAPE) == GLFW_PRESS)
            {
                splashScreenAnimating = true;
                splashScreenStartTime = glfwGetTime();
                logf("Splash screen animation started");
            }
        }
        
        // Update splash screen animation
        updateSplashScreen();
        
        glClearColor(0.05f, 0.05f, 0.10f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Draw splash screen first if active
        if (showSplashScreen || splashScreenAnimating)
        {
            drawSplashScreen();
        }

        // Only draw game content if splash screen is not showing
        if (!showSplashScreen && !splashScreenAnimating)
        {
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
            if (texSettings && ImGui::ImageButton("settings", (ImTextureID)(intptr_t)texSettings, ImVec2(24, 24)))
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

        if (texRegen && ImGui::ImageButton("regen", (ImTextureID)(intptr_t)texRegen, ImVec2(24, 24)))
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
            if (texPlay && ImGui::ImageButton("play", (ImTextureID)(intptr_t)texPlay, ImVec2(32, 32)))
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

            if (texPause && ImGui::ImageButton("pause", (ImTextureID)(intptr_t)texPause, ImVec2(32, 32)))
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

                if (texStep && ImGui::ImageButton("step", (ImTextureID)(intptr_t)texStep, ImVec2(32, 32)))
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
        } // End of game content conditional block

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