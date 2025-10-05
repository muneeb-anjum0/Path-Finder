#include "../headers/textures.h"
#include "../headers/logging.h"

GLuint loadTexture(const char *path)
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

void loadAllTextures()
{
    texSplashScreen = loadTexture("assets/splashScreen.png"); // Splash screen
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

void deleteAllTextures()
{
    GLuint ids[12] = {texSplashScreen, texBackground, texStart, texEnd, texObstacle, texLineHori, texLineVerti, texPlay, texPause, texRegen, texSettings, texStep};
    glDeleteTextures(12, ids);
    texSplashScreen = texBackground = texStart = texEnd = texObstacle = texLineHori = texLineVerti = texPlay = texPause = texRegen = texSettings = texStep = 0;
}