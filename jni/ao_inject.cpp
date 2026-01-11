#include <jni.h>
#include <dlfcn.h>
#include <android/log.h>
#include <sys/mman.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "AO_INJECT", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR,"AO_INJECT", __VA_ARGS__)

#define SHADER_MAX 32768

// ===============================
// GTASA v2.00 OFFSETS (ARMv7)
// ===============================
#define OFF_RQShaderBuildSource   0x1F3A6C
#define OFF_rqSelectShader        0x1F4120
#define OFF_InitES2Shader         0x1F28B4

// ===============================
// Globals
// ===============================
static int gAOEnabled = 1;

static int (*orig_BuildSource)(int,char**,char**) = nullptr;
static void (*orig_SelectShader)(void**) = nullptr;
static void (*orig_InitShader)(void*) = nullptr;

// ===============================
// Utils
// ===============================
static uintptr_t getLibBase(const char* name)
{
    FILE* f = fopen("/proc/self/maps","r");
    if(!f) return 0;
    char line[512];
    while(fgets(line,sizeof(line),f))
    {
        if(strstr(line,name))
        {
            uintptr_t base = strtoul(line,nullptr,16);
            fclose(f);
            return base;
        }
    }
    fclose(f);
    return 0;
}

static void hookPtr(void** target, void* hook, void** orig)
{
    uintptr_t page = (uintptr_t)target & ~0xFFF;
    mprotect((void*)page, 4096, PROT_READ|PROT_WRITE);
    *orig = *target;
    *target = hook;
    mprotect((void*)page, 4096, PROT_READ);
}

// ===============================
// AO SHADER PATCH
// ===============================
static void injectAO(char* frag)
{
    if(!gAOEnabled) return;
    if(strstr(frag,"AO_INTENSITY")) return;

    const char* ao =
        "\n// === AO INJECT ===\n"
        "uniform float AO_INTENSITY;\n"
        "float ao_calc(vec3 n){return clamp(dot(n,vec3(0,0,1)),0.0,1.0);}\n";

    strncat(frag, ao, SHADER_MAX);
}

// ===============================
// Hooks
// ===============================
static int hk_BuildSource(int flags, char** pxl, char** vtx)
{
    int r = orig_BuildSource(flags, pxl, vtx);
    if(pxl && *pxl) injectAO(*pxl);
    return r;
}

static void hk_SelectShader(void** pp)
{
    orig_SelectShader(pp);
    // uniform upload handled by shader itself (cheap AO)
}

static void hk_InitShader(void* self)
{
    orig_InitShader(self);
}

// ===============================
// INIT
// ===============================
__attribute__((constructor))
static void init()
{
    uintptr_t base = getLibBase("libGTASA.so");
    if(!base)
    {
        LOGE("libGTASA.so not found");
        return;
    }

    hookPtr(
        (void**)(base + OFF_RQShaderBuildSource),
        (void*)hk_BuildSource,
        (void**)&orig_BuildSource
    );

    hookPtr(
        (void**)(base + OFF_rqSelectShader),
        (void*)hk_SelectShader,
        (void**)&orig_SelectShader
    );

    hookPtr(
        (void**)(base + OFF_InitES2Shader),
        (void*)hk_InitShader,
        (void**)&orig_InitShader
    );

    LOGI("AO Inject loaded (GTASA v2.00)");
}

// ===============================
// Lua API
// ===============================
extern "C" {
    void ao_enable(int v){ gAOEnabled = v; }
}
