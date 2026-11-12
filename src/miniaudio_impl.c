// Implémentation de miniaudio, limitée au strict nécessaire (WASAPI, lecture/capture, décodage des MP3).
#define MINIAUDIO_IMPLEMENTATION
#define MA_ENABLE_ONLY_SPECIFIC_BACKENDS
#define MA_ENABLE_WASAPI
#define MA_NO_FLAC  // décodage : WAV et MP3 seulement (sons de Myinstants)
#define MA_NO_ENCODING
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE
#define MA_NO_GENERATION
#include "miniaudio/miniaudio.h"
