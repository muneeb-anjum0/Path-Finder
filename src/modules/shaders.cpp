#include "../headers/shaders.h"
#include "../headers/logging.h"

const char *VS_330 =
    "#version 330 core\n"
    "layout(location=0) in vec2 aPos;\n"
    "uniform mat4 uProjection;\n"
    "void main(){ gl_Position = uProjection * vec4(aPos,0,1); }\n";

const char *FS_330 =
    "#version 330 core\n"
    "out vec4 FragColor;\n"
    "uniform vec3 uColor;\n"
    "void main(){ FragColor = vec4(uColor,1); }\n";

const char *VS_150 =
    "#version 150\n"
    "in vec2 aPos;\n"
    "uniform mat4 uProjection;\n"
    "void main(){ gl_Position = uProjection * vec4(aPos,0,1); }\n";

const char *FS_150 =
    "#version 150\n"
    "out vec4 FragColor;\n"
    "uniform vec3 uColor;\n"
    "void main(){ FragColor = vec4(uColor,1); }\n";

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

unsigned int createProgram(const char *vs, const char *fs)
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
    if (!linked)
    {
        GLint len = 0;
        glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &len);
        std::vector<char> log(len);
        glGetProgramInfoLog(prog, len, nullptr, log.data());
        logf("Program link error:\n%s", log.data());
    }
    return prog;
}