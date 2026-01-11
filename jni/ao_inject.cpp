#include <dlfcn.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <android/log.h>
#include <unistd.h>
#include <sys/mman.h>

#define LOG_TAG "AO_SHADER"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

static float g_ao_intensity = 0.5f;
static bool g_ao_enabled = true;

const char* AO_FRAGMENT_CODE = R"SHADER(
uniform float u_ao_strength;
uniform vec2 u_screen_size;

float sampleDepth(vec2 uv, sampler2D tex) {
    return texture2D(tex, uv).a;
}

float computeAO(vec2 uv, sampler2D tex, float centerDepth) {
    float ao = 0.0;
    vec2 texel = 1.0 / u_screen_size;
    float threshold = 0.02;
    int samples = 0;
    
    for(int x = -1; x <= 1; x++) {
        for(int y = -1; y <= 1; y++) {
            if(x == 0 && y == 0) continue;
            vec2 offset = vec2(float(x), float(y)) * texel * 2.5;
            float sampleDepth = texture2D(tex, uv + offset).a;
            float depthDiff = abs(centerDepth - sampleDepth);
            
            if(depthDiff > threshold) {
                ao += (depthDiff / threshold) * 0.125;
                samples++;
            }
        }
    }
    
    if(samples > 0) {
        ao = ao / float(samples);
    }
    
    ao = clamp(ao * u_ao_strength, 0.0, 0.9);
    return 1.0 - ao;
}
)SHADER";

typedef int (*BuildSourceFunc)(unsigned int, char**, char**);
static BuildSourceFunc original_BuildSource = nullptr;

static char custom_fragment_shader[65536];
static bool shader_injected = false;
static int injection_count = 0;

int hooked_BuildSource(unsigned int flags, char** fragSrc, char** vertSrc) {
    int result = original_BuildSource(flags, fragSrc, vertSrc);
    
    if (!g_ao_enabled) return result;
    if (!fragSrc || !*fragSrc) return result;
    
    const char* frag = *fragSrc;
    
    if (strstr(frag, "texture2D") && !strstr(frag, "u_ao_strength")) {
        const char* main_pos = strstr(frag, "void main()");
        if (!main_pos) main_pos = strstr(frag, "void main ()");
        
        if (main_pos) {
            size_t header_len = main_pos - frag;
            
            int written = snprintf(custom_fragment_shader, sizeof(custom_fragment_shader),
                "%.*s\n"
                "%s\n"
                "void main() {\n"
                "    vec2 texCoord = v_texcoord0;\n"
                "    vec4 baseColor = texture2D(u_texture0, texCoord);\n"
                "    float depth = sampleDepth(texCoord, u_texture0);\n"
                "    float aoFactor = computeAO(texCoord, u_texture0, depth);\n"
                "    gl_FragColor = vec4(baseColor.rgb * aoFactor, baseColor.a);\n"
                "}\n",
                (int)header_len, frag,
                AO_FRAGMENT_CODE);
            
            if (written > 0 && written < sizeof(custom_fragment_shader)) {
                *fragSrc = custom_fragment_shader;
                injection_count++;
                LOGI("AO shader injected! Count: %d, Flags: 0x%X", injection_count, flags);
            }
        }
    }
    
    return result;
}

typedef void (*SelectShaderFunc)(void***);
static SelectShaderFunc original_SelectShader = nullptr;

void hooked_SelectShader(void*** shaderPtrPtr) {
    original_SelectShader(shaderPtrPtr);
    
    if (!g_ao_enabled) return;
    if (!shaderPtrPtr || !*shaderPtrPtr || !**shaderPtrPtr) return;
    
    static void* gles_handle = nullptr;
    if (!gles_handle) {
        gles_handle = dlopen("libGLESv2.so", RTLD_LAZY);
        if (!gles_handle) return;
    }
    
    typedef int (*GetUniformFunc)(unsigned int, const char*);
    typedef void (*Uniform1fFunc)(int, float);
    typedef void (*Uniform2fFunc)(int, float, float);
    
    static GetUniformFunc glGetUniformLocation = nullptr;
    static Uniform1fFunc glUniform1f = nullptr;
    static Uniform2fFunc glUniform2f = nullptr;
    
    if (!glGetUniformLocation) {
        glGetUniformLocation = (GetUniformFunc)dlsym(gles_handle, "glGetUniformLocation");
        glUniform1f = (Uniform1fFunc)dlsym(gles_handle, "glUniform1f");
        glUniform2f = (Uniform2fFunc)dlsym(gles_handle, "glUniform2f");
    }
    
    if (!glGetUniformLocation || !glUniform1f || !glUniform2f) return;
    
    void* shader = **shaderPtrPtr;
    unsigned int shader_id = *(unsigned int*)shader;
    
    int loc_strength = glGetUniformLocation(shader_id, "u_ao_strength");
    int loc_screen = glGetUniformLocation(shader_id, "u_screen_size");
    
    if (loc_strength >= 0) {
        glUniform1f(loc_strength, g_ao_intensity);
    }
    
    if (loc_screen >= 0) {
        glUniform2f(loc_screen, 1080.0f, 1920.0f);
    }
}

__attribute__((constructor))
static void ao_init() {
    LOGI("=== AO Shader Injection v1.0 ===");
    LOGI("Initializing hooks...");
    
    void* gtasa = dlopen("libGTASA.so", RTLD_LAZY);
    if (!gtasa) {
        LOGE("Failed to load libGTASA.so: %s", dlerror());
        return;
    }
    
    original_BuildSource = (BuildSourceFunc)dlsym(gtasa, "_ZN8RQShader11BuildSourceEjPPKcS2_");
    if (!original_BuildSource) {
        LOGE("Failed to find BuildSource: %s", dlerror());
        dlclose(gtasa);
        return;
    }
    LOGI("BuildSource found at: %p", original_BuildSource);
    
    original_SelectShader = (SelectShaderFunc)dlsym(gtasa, "_Z25RQ_Command_rqSelectShaderRPc");
    if (!original_SelectShader) {
        LOGE("Failed to find SelectShader: %s", dlerror());
        dlclose(gtasa);
        return;
    }
    LOGI("SelectShader found at: %p", original_SelectShader);
    
    LOGI("Hooks installed successfully!");
    LOGI("AO enabled: %s, Intensity: %.2f", g_ao_enabled ? "true" : "false", g_ao_intensity);
}

extern "C" {
    __attribute__((visibility("default")))
    void set_ao_intensity(float intensity) {
        if (intensity < 0.0f) intensity = 0.0f;
        if (intensity > 1.0f) intensity = 1.0f;
        g_ao_intensity = intensity;
        LOGI("AO intensity updated: %.2f", intensity);
    }
    
    __attribute__((visibility("default")))
    void set_ao_enabled(int enabled) {
        g_ao_enabled = (enabled != 0);
        LOGI("AO %s", g_ao_enabled ? "enabled" : "disabled");
    }
    
    __attribute__((visibility("default")))
    float get_ao_intensity() {
        return g_ao_intensity;
    }
    
    __attribute__((visibility("default")))
    int get_injection_count() {
        return injection_count;
    }
}
