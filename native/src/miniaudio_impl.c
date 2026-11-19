/* miniaudio (sound) with stb_vorbis for .ogg files */
#define STB_VORBIS_HEADER_ONLY
#include <stb_vorbis.c>
#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#include <miniaudio.h>
#undef STB_VORBIS_HEADER_ONLY
#include <stb_vorbis.c>
