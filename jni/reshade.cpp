#include <mod/amlmod.h>

#include <mod/logger.h>

#include <GLES3/gl3.h>

#include <GLES3/gl3ext.h>

#include <dlfcn.h>

#include <android/log.h>

#include <EGL/egl.h>



#define LOG_TAG "ReShade"

#define LOGD(...) ((void)__android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__))



// ========================================

// GLOBALS

// ========================================

uintptr_t g_libGTASA = 0;



// Shader resources

GLuint g_shaderProgram = 0;

GLuint g_quadVAO = 0;

GLuint g_quadVBO = 0;



// Screen capture texture

GLuint g_screenTexture = 0;

int g_lastWidth = 0;

int g_lastHeight = 0;

bool g_textureAllocated = false;



// Compatibility flag

bool g_useVAO = true;



// Minimal state backup

struct GLStateMinimal {

    GLint program;

    GLint vao;

    GLint arrayBuffer;

    GLint fbo;

    GLint viewport[4];

    GLboolean depthTest;

    GLboolean blend;

    GLint texture2D;

    GLint unpackAlignment;

} g_savedState;



// ========================================

// SETTINGS

// ========================================

enum Quality {

    QUALITY_LOW = 0,

    QUALITY_MEDIUM = 1,

    QUALITY_HIGH = 2,

    QUALITY_ULTRA = 3

};



struct ReshadeSettings {

    bool enabled;

    Quality quality;

    float brightness;

    float contrast;

    float saturation;

    float sharpness;

    float vignette;

    float gamma;

} g_settings = {

    .enabled = true,

    .quality = QUALITY_MEDIUM,

    .brightness = 1.0f,

    .contrast = 1.0f,

    .saturation = 1.0f,

    .sharpness = 0.3f,

    .vignette = 0.3f,

    .gamma = 1.0f

};



// ========================================

// MINIMAL STATE SAVE/RESTORE

// ========================================

inline void SaveGLStateMinimal() {

    glGetIntegerv(GL_CURRENT_PROGRAM, &g_savedState.program);

    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &g_savedState.arrayBuffer);

    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &g_savedState.fbo);

    glGetIntegerv(GL_VIEWPORT, g_savedState.viewport);

    glGetIntegerv(GL_TEXTURE_BINDING_2D, &g_savedState.texture2D);

    glGetIntegerv(GL_UNPACK_ALIGNMENT, &g_savedState.unpackAlignment);

    

    g_savedState.depthTest = glIsEnabled(GL_DEPTH_TEST);

    g_savedState.blend = glIsEnabled(GL_BLEND);

    

    if (g_useVAO) {

        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &g_savedState.vao);

    }

}



inline void RestoreGLStateMinimal() {

    glUseProgram(g_savedState.program);

    glBindBuffer(GL_ARRAY_BUFFER, g_savedState.arrayBuffer);

    glBindFramebuffer(GL_FRAMEBUFFER, g_savedState.fbo);

    glViewport(g_savedState.viewport[0], g_savedState.viewport[1],

               g_savedState.viewport[2], g_savedState.viewport[3]);

    glBindTexture(GL_TEXTURE_2D, g_savedState.texture2D);

    glPixelStorei(GL_UNPACK_ALIGNMENT, g_savedState.unpackAlignment);

    

    if (g_savedState.depthTest) glEnable(GL_DEPTH_TEST);

    else glDisable(GL_DEPTH_TEST);

    

    if (g_savedState.blend) glEnable(GL_BLEND);

    else glDisable(GL_BLEND);

    

    if (g_useVAO) {

        glBindVertexArray(g_savedState.vao);

    }

}



// ========================================

// SHADERS

// ========================================

const char* g_vertexShaderSource = R"(

#version 300 es

precision highp float;



layout (location = 0) in vec2 aPos;

layout (location = 1) in vec2 aTexCoords;



out vec2 TexCoords;



void main() {

    TexCoords = aTexCoords;

    gl_Position = vec4(aPos, 0.0, 1.0);

}

)";



const char* g_fragmentShaderSource = R"(

#version 300 es

precision mediump float;



out vec4 FragColor;

in vec2 TexCoords;



uniform sampler2D screenTexture;

uniform float u_brightness;

uniform float u_contrast;

uniform float u_saturation;

uniform float u_sharpness;

uniform float u_vignette;

uniform float u_gamma;

uniform int u_quality;



float getLuma(vec3 color) {

    return dot(color, vec3(0.299, 0.587, 0.114));

}



void main() {

    vec2 uv = TexCoords;

    vec3 color = texture(screenTexture, uv).rgb;

    

    // Brightness & Contrast

    color = (color - 0.5) * u_contrast + 0.5;

    color *= u_brightness;

    

    // Saturation

    float luma = getLuma(color);

    color = mix(vec3(luma), color, u_saturation);

    

    // Sharpness (MEDIUM+ only, 5-tap)

    if (u_sharpness > 0.01 && u_quality >= 1) {

        vec2 ts = 1.0 / vec2(textureSize(screenTexture, 0));

        vec3 blur = texture(screenTexture, uv).rgb;

        blur += texture(screenTexture, uv + vec2(ts.x, 0.0)).rgb;

        blur += texture(screenTexture, uv - vec2(ts.x, 0.0)).rgb;

        blur += texture(screenTexture, uv + vec2(0.0, ts.y)).rgb;

        blur += texture(screenTexture, uv - vec2(0.0, ts.y)).rgb;

        blur *= 0.2;

        

        color = mix(color, color + (color - blur) * u_sharpness, u_sharpness);

    }

    

    // Vignette

    if (u_vignette > 0.01) {

        float dist = distance(uv, vec2(0.5));

        float vig = smoothstep(0.8, 0.4, dist);

        color = mix(color * 0.3, color, vig * (1.0 - u_vignette) + u_vignette);

    }

    

    // Gamma

    color = pow(max(color, vec3(0.0)), vec3(1.0 / max(u_gamma, 0.1)));

    

    FragColor = vec4(clamp(color, 0.0, 1.0), 1.0);

}

)";



// ========================================

// SHADER COMPILE

// ========================================

GLuint CompileShader(GLenum type, const char* source) {

    GLuint shader = glCreateShader(type);

    if (!shader) return 0;

    

    glShaderSource(shader, 1, &source, nullptr);

    glCompileShader(shader);

    

    GLint success;

    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);

    if (!success) {

        char log[512];

        glGetShaderInfoLog(shader, 512, nullptr, log);

        LOGD("Shader error: %s", log);

        glDeleteShader(shader);

        return 0;

    }

    

    return shader;

}



// ========================================

// INIT RESHADE

// ========================================

void InitReshade() {

    LOGD("Init ReShade...");

    

    // Compile shaders

    GLuint vs = CompileShader(GL_VERTEX_SHADER, g_vertexShaderSource);

    GLuint fs = CompileShader(GL_FRAGMENT_SHADER, g_fragmentShaderSource);

    

    if (!vs || !fs) {

        LOGD("Shader compilation failed!");

        return;

    }

    

    g_shaderProgram = glCreateProgram();

    glAttachShader(g_shaderProgram, vs);

    glAttachShader(g_shaderProgram, fs);

    glLinkProgram(g_shaderProgram);

    

    GLint success;

    glGetProgramiv(g_shaderProgram, GL_LINK_STATUS, &success);

    if (!success) {

        char log[512];

        glGetProgramInfoLog(g_shaderProgram, 512, nullptr, log);

        LOGD("Link error: %s", log);

    }

    

    glDeleteShader(vs);

    glDeleteShader(fs);

    

    // Setup quad

    float quad[] = {

        -1.0f,  1.0f,  0.0f, 1.0f,

        -1.0f, -1.0f,  0.0f, 0.0f,

         1.0f,  1.0f,  1.0f, 1.0f,

         1.0f, -1.0f,  1.0f, 0.0f

    };

    

    glGenBuffers(1, &g_quadVBO);

    glBindBuffer(GL_ARRAY_BUFFER, g_quadVBO);

    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);

    

    // Try to use VAO

    glGenVertexArrays(1, &g_quadVAO);

    if (glGetError() == GL_NO_ERROR && g_quadVAO != 0) {

        glBindVertexArray(g_quadVAO);

        

        glEnableVertexAttribArray(0);

        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);

        glEnableVertexAttribArray(1);

        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

        

        glBindVertexArray(0);

        g_useVAO = true;

        LOGD("VAO supported");

    } else {

        g_useVAO = false;

        LOGD("VAO not supported, using fallback");

    }

    

    glBindBuffer(GL_ARRAY_BUFFER, 0);

    

    // Create screen texture

    glGenTextures(1, &g_screenTexture);

    glBindTexture(GL_TEXTURE_2D, g_screenTexture);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_2D, 0);

    

    LOGD("ReShade initialized!");

}



// ========================================

// APPLY RESHADE

// ========================================

void ApplyReshade(EGLDisplay dpy, EGLSurface surface) {

    if (!g_settings.enabled || !g_shaderProgram) return;

    

    // Get actual screen size from EGL

    EGLint width = 0, height = 0;

    eglQuerySurface(dpy, surface, EGL_WIDTH, &width);

    eglQuerySurface(dpy, surface, EGL_HEIGHT, &height);

    

    if (width <= 0 || height <= 0) return;

    

    // Save state

    SaveGLStateMinimal();

    

    // Bind texture

    glBindTexture(GL_TEXTURE_2D, g_screenTexture);

    

    // Allocate texture once

    if (!g_textureAllocated || width != g_lastWidth || height != g_lastHeight) {

        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);

        g_lastWidth = width;

        g_lastHeight = height;

        g_textureAllocated = true;

        LOGD("Texture allocated: %dx%d", width, height);

    }

    

    // Set pixel store for Adreno compatibility

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    

    // Copy framebuffer to texture (NO REALLOC)

    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, width, height);

    

    // Clear screen (COLOR ONLY, NO DEPTH)

    glClear(GL_COLOR_BUFFER_BIT);

    

    glDisable(GL_DEPTH_TEST);

    glDisable(GL_BLEND);

    

    // Use shader

    glUseProgram(g_shaderProgram);

    

    // Set uniforms

    glUniform1f(glGetUniformLocation(g_shaderProgram, "u_brightness"), g_settings.brightness);

    glUniform1f(glGetUniformLocation(g_shaderProgram, "u_contrast"), g_settings.contrast);

    glUniform1f(glGetUniformLocation(g_shaderProgram, "u_saturation"), g_settings.saturation);

    glUniform1f(glGetUniformLocation(g_shaderProgram, "u_sharpness"), g_settings.sharpness);

    glUniform1f(glGetUniformLocation(g_shaderProgram, "u_vignette"), g_settings.vignette);

    glUniform1f(glGetUniformLocation(g_shaderProgram, "u_gamma"), g_settings.gamma);

    glUniform1i(glGetUniformLocation(g_shaderProgram, "u_quality"), (int)g_settings.quality);

    

    // Bind texture

    glActiveTexture(GL_TEXTURE0);

    glBindTexture(GL_TEXTURE_2D, g_screenTexture);

    glUniform1i(glGetUniformLocation(g_shaderProgram, "screenTexture"), 0);

    

    // Draw quad

    if (g_useVAO) {

        glBindVertexArray(g_quadVAO);

        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        glBindVertexArray(0);

    } else {

        // Fallback: manual vertex setup

        glBindBuffer(GL_ARRAY_BUFFER, g_quadVBO);

        glEnableVertexAttribArray(0);

        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);

        glEnableVertexAttribArray(1);

        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

        

        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        

        glDisableVertexAttribArray(0);

        glDisableVertexAttribArray(1);

        glBindBuffer(GL_ARRAY_BUFFER, 0);

    }

    

    // Restore state

    RestoreGLStateMinimal();

}



// ========================================

// HOOK: eglSwapBuffers ONLY

// ========================================

typedef EGLBoolean (*eglSwapBuffers_t)(EGLDisplay, EGLSurface);

eglSwapBuffers_t original_eglSwapBuffers = nullptr;



EGLBoolean hooked_eglSwapBuffers(EGLDisplay dpy, EGLSurface surface) {

    static bool initialized = false;

    

    if (!initialized) {

        InitReshade();

        initialized = true;

    }

    

    // Apply post-process

    ApplyReshade(dpy, surface);

    

    // Call original

    return original_eglSwapBuffers(dpy, surface);

}



// ========================================

// EXPORTED FUNCTIONS

// ========================================

extern "C" {

    __attribute__((visibility("default")))

    void RS_SetEnabled(bool enabled) {

        g_settings.enabled = enabled;

        LOGD("ReShade: %s", enabled ? "ON" : "OFF");

    }

    

    __attribute__((visibility("default")))

    void RS_SetQuality(int quality) {

        if (quality < 0) quality = 0;

        if (quality > 3) quality = 3;

        g_settings.quality = (Quality)quality;

        LOGD("Quality: %d", quality);

    }

    

    __attribute__((visibility("default")))

    void RS_SetPreset(int preset) {

        switch (preset) {

            case 0: // Low

                g_settings.quality = QUALITY_LOW;

                g_settings.sharpness = 0.0f;

                g_settings.vignette = 0.2f;

                LOGD("Preset: LOW");

                break;

            case 1: // Medium

                g_settings.quality = QUALITY_MEDIUM;

                g_settings.sharpness = 0.3f;

                g_settings.vignette = 0.3f;

                LOGD("Preset: MEDIUM");

                break;

            case 2: // High

                g_settings.quality = QUALITY_HIGH;

                g_settings.sharpness = 0.5f;

                g_settings.vignette = 0.3f;

                LOGD("Preset: HIGH");

                break;

            case 3: // Ultra

                g_settings.quality = QUALITY_ULTRA;

                g_settings.sharpness = 0.7f;

                g_settings.vignette = 0.4f;

                LOGD("Preset: ULTRA");

                break;

        }

    }

    

    __attribute__((visibility("default")))

    void RS_SetBrightness(float v) { g_settings.brightness = v; }

    

    __attribute__((visibility("default")))

    void RS_SetContrast(float v) { g_settings.contrast = v; }

    

    __attribute__((visibility("default")))

    void RS_SetSaturation(float v) { g_settings.saturation = v; }

    

    __attribute__((visibility("default")))

    void RS_SetSharpness(float v) { g_settings.sharpness = v; }

    

    __attribute__((visibility("default")))

    void RS_SetVignette(float v) { g_settings.vignette = v; }

    

    __attribute__((visibility("default")))

    void RS_SetGamma(float v) { g_settings.gamma = v; }

}



// ========================================

// AML ENTRY

// ========================================

MYMOD(net.bekasuyy.gtasa.reshade, ReShade, 1.0, bekasuyy)



BEGIN_LOAD() {

    LOGD("ReShade loading...");

    

    g_libGTASA = (uintptr_t)aml->GetLibHandle("libGTASA.so");

    if (!g_libGTASA) {

        LOGD("Failed to get libGTASA!");

        return;

    }

    

    // Hook eglSwapBuffers

    void* eglLib = dlopen("libEGL.so", RTLD_NOW);

    if (eglLib) {

        void* addr = dlsym(eglLib, "eglSwapBuffers");

        if (addr) {

            aml->Hook(addr, (void*)hooked_eglSwapBuffers, (void**)&original_eglSwapBuffers);

            LOGD("eglSwapBuffers hooked!");

        } else {

            LOGD("Failed to find eglSwapBuffers!");

        }

    } else {

        LOGD("Failed to load libEGL.so!");

    }

    

    LOGD("ReShade loaded!");

}

END_LOAD()
