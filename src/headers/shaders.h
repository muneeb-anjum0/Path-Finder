#pragma once

#include "common.h"

// Shader functions
unsigned int compileShader(unsigned int type, const char *src);
unsigned int createProgram(const char *vs, const char *fs);

// Shader source constants
extern const char *VS_330;
extern const char *FS_330;
extern const char *VS_150;
extern const char *FS_150;