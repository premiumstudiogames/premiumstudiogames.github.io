#include "gl.h"
#include <SDL.h>
#include <cstdio>
#define PGE_GL_DEF(T, n) T pge_##n = nullptr;
PGE_GL_FUNCS(PGE_GL_DEF)
bool pgeLoadGL() {
  bool ok = true;
#define PGE_GL_LOAD(T, n) pge_##n = (T)SDL_GL_GetProcAddress(#n); if (!pge_##n) { std::fprintf(stderr, "missing GL function %s\n", #n); ok = false; }
  PGE_GL_FUNCS(PGE_GL_LOAD)
  return ok;
}
