#define EGL_EGL_PROTOTYPES 0
#define GL_GLES_PROTOTYPES 0
#include "blit_gles.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <gbm.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef EGL_PLATFORM_SURFACELESS_MESA
#define EGL_PLATFORM_SURFACELESS_MESA 0x31DD
#endif
#ifndef EGL_PLATFORM_GBM_KHR
#define EGL_PLATFORM_GBM_KHR 0x31D7
#endif

typedef struct gbm_device *(*BlitGbmCreateDevice)(int);
typedef void (*BlitGbmDeviceDestroy)(struct gbm_device *);
typedef struct gbm_surface *(*BlitGbmSurfaceCreate)(struct gbm_device *,
    uint32_t, uint32_t, uint32_t, uint32_t);
typedef void (*BlitGbmSurfaceDestroy)(struct gbm_surface *);
typedef struct gbm_bo *(*BlitGbmLockFront)(struct gbm_surface *);
typedef void (*BlitGbmRelease)(struct gbm_surface *, struct gbm_bo *);
typedef union gbm_bo_handle (*BlitGbmBoHandle)(struct gbm_bo *);
typedef uint32_t (*BlitGbmBoStride)(struct gbm_bo *);

struct BlitGles {
    void *egl_library;
    void *gl_library;
    void *gbm_library;
    struct gbm_device *gbm;
    EGLDisplay display;
    EGLConfig config;
    EGLContext context;
    int surfaceless;
    GLuint program;
    GLint sampler;
    GLuint quad;
    char identity[256];
    /* Composition: one program, a client-side vertex batch. */
    GLuint compose_program;
    GLint compose_sampler, compose_size;
    GLuint compose_buffer;
    GLuint white;
    struct BlitGlesVertex *vertices;
    int vertex_count, vertex_capacity;
    GLuint batch_texture;
    BlitGlesTarget *frame_target;
    int bound_height;
    unsigned long long draw_calls;

    PFNEGLGETPROCADDRESSPROC GetProcAddress;
    PFNEGLGETPLATFORMDISPLAYPROC GetPlatformDisplay;
    PFNEGLINITIALIZEPROC Initialize;
    PFNEGLTERMINATEPROC Terminate;
    PFNEGLBINDAPIPROC BindAPI;
    PFNEGLCHOOSECONFIGPROC ChooseConfig;
    PFNEGLGETCONFIGATTRIBPROC GetConfigAttrib;
    PFNEGLCREATECONTEXTPROC CreateContext;
    PFNEGLDESTROYCONTEXTPROC DestroyContext;
    PFNEGLMAKECURRENTPROC MakeCurrent;
    PFNEGLCREATEPLATFORMWINDOWSURFACEPROC CreatePlatformWindowSurface;
    PFNEGLDESTROYSURFACEPROC DestroySurface;
    PFNEGLSWAPBUFFERSPROC SwapBuffers;
    PFNEGLGETERRORPROC GetError;

    PFNGLGETSTRINGPROC glGetString;
    PFNGLGETERRORPROC glGetError;
    PFNGLCREATESHADERPROC glCreateShader;
    PFNGLSHADERSOURCEPROC glShaderSource;
    PFNGLCOMPILESHADERPROC glCompileShader;
    PFNGLGETSHADERIVPROC glGetShaderiv;
    PFNGLGETSHADERINFOLOGPROC glGetShaderInfoLog;
    PFNGLDELETESHADERPROC glDeleteShader;
    PFNGLCREATEPROGRAMPROC glCreateProgram;
    PFNGLATTACHSHADERPROC glAttachShader;
    PFNGLBINDATTRIBLOCATIONPROC glBindAttribLocation;
    PFNGLLINKPROGRAMPROC glLinkProgram;
    PFNGLGETPROGRAMIVPROC glGetProgramiv;
    PFNGLDELETEPROGRAMPROC glDeleteProgram;
    PFNGLUSEPROGRAMPROC glUseProgram;
    PFNGLGETUNIFORMLOCATIONPROC glGetUniformLocation;
    PFNGLUNIFORM1IPROC glUniform1i;
    PFNGLGENBUFFERSPROC glGenBuffers;
    PFNGLBINDBUFFERPROC glBindBuffer;
    PFNGLBUFFERDATAPROC glBufferData;
    PFNGLDELETEBUFFERSPROC glDeleteBuffers;
    PFNGLENABLEVERTEXATTRIBARRAYPROC glEnableVertexAttribArray;
    PFNGLVERTEXATTRIBPOINTERPROC glVertexAttribPointer;
    PFNGLGENTEXTURESPROC glGenTextures;
    PFNGLBINDTEXTUREPROC glBindTexture;
    PFNGLTEXPARAMETERIPROC glTexParameteri;
    PFNGLTEXIMAGE2DPROC glTexImage2D;
    PFNGLTEXSUBIMAGE2DPROC glTexSubImage2D;
    PFNGLDELETETEXTURESPROC glDeleteTextures;
    PFNGLPIXELSTOREIPROC glPixelStorei;
    PFNGLACTIVETEXTUREPROC glActiveTexture;
    PFNGLGENFRAMEBUFFERSPROC glGenFramebuffers;
    PFNGLBINDFRAMEBUFFERPROC glBindFramebuffer;
    PFNGLFRAMEBUFFERTEXTURE2DPROC glFramebufferTexture2D;
    PFNGLCHECKFRAMEBUFFERSTATUSPROC glCheckFramebufferStatus;
    PFNGLDELETEFRAMEBUFFERSPROC glDeleteFramebuffers;
    PFNGLVIEWPORTPROC glViewport;
    PFNGLDISABLEPROC glDisable;
    PFNGLDRAWARRAYSPROC glDrawArrays;
    PFNGLREADPIXELSPROC glReadPixels;
    PFNGLFINISHPROC glFinish;
    PFNGLENABLEPROC glEnable;
    PFNGLBLENDFUNCPROC glBlendFunc;
    PFNGLSCISSORPROC glScissor;
    PFNGLUNIFORM2FPROC glUniform2f;
    PFNGLBUFFERSUBDATAPROC glBufferSubData;
    PFNGLGETATTRIBLOCATIONPROC glGetAttribLocation;
    PFNGLDISABLEVERTEXATTRIBARRAYPROC glDisableVertexAttribArray;

    BlitGbmCreateDevice gbm_create_device;
    BlitGbmDeviceDestroy gbm_device_destroy;
    BlitGbmSurfaceCreate gbm_surface_create;
    BlitGbmSurfaceDestroy gbm_surface_destroy;
    BlitGbmLockFront gbm_surface_lock_front_buffer;
    BlitGbmRelease gbm_surface_release_buffer;
    BlitGbmBoHandle gbm_bo_get_handle;
    BlitGbmBoStride gbm_bo_get_stride;
};

struct BlitGlesVertex {
    GLfloat x, y, u, v;
    GLubyte r, g, b, a;
    GLfloat mode;
};

struct BlitGlesScene {
    int width, height;
    GLuint texture, framebuffer;
};

struct BlitGlesTarget {
    int width;
    int height;
    /* The frame last uploaded, so only rows that changed are sent again; the
       texture keeps the rest. NULL until the first upload. */
    unsigned char *shadow;
    unsigned long long uploaded_rows;
    GLuint texture;
    GLuint framebuffer;
    GLuint framebuffer_texture;
    struct gbm_surface *gbm_surface;
    EGLSurface surface;
};

static void gles_flush(BlitGles *gles);

static void gles_error(char *error, size_t capacity, const char *what)
{
    if (error != NULL && capacity > 0) snprintf(error, capacity, "%s", what);
}

static int gles_load_symbols(void *library, void **slots, const char *const *names)
{
    int i;
    for (i = 0; names[i] != NULL; ++i) {
        slots[i] = dlsym(library, names[i]);
        if (slots[i] == NULL) return -1;
    }
    return 0;
}

#define BLIT_LOAD(library, field, name) \
    ((gles->field = (void *)dlsym(library, name)) != NULL)

static int gles_load_egl_gl(BlitGles *gles, char *error, size_t capacity)
{
    int ok;
    gles->egl_library = dlopen("libEGL.so.1", RTLD_NOW | RTLD_LOCAL);
    gles->gl_library = dlopen("libGLESv2.so.2", RTLD_NOW | RTLD_LOCAL);
    if (gles->egl_library == NULL || gles->gl_library == NULL) {
        gles_error(error, capacity, "libEGL.so.1 or libGLESv2.so.2 is not available");
        return -1;
    }
    ok = BLIT_LOAD(gles->egl_library, GetProcAddress, "eglGetProcAddress") &&
        BLIT_LOAD(gles->egl_library, GetPlatformDisplay, "eglGetPlatformDisplay") &&
        BLIT_LOAD(gles->egl_library, Initialize, "eglInitialize") &&
        BLIT_LOAD(gles->egl_library, Terminate, "eglTerminate") &&
        BLIT_LOAD(gles->egl_library, BindAPI, "eglBindAPI") &&
        BLIT_LOAD(gles->egl_library, ChooseConfig, "eglChooseConfig") &&
        BLIT_LOAD(gles->egl_library, GetConfigAttrib, "eglGetConfigAttrib") &&
        BLIT_LOAD(gles->egl_library, CreateContext, "eglCreateContext") &&
        BLIT_LOAD(gles->egl_library, DestroyContext, "eglDestroyContext") &&
        BLIT_LOAD(gles->egl_library, MakeCurrent, "eglMakeCurrent") &&
        BLIT_LOAD(gles->egl_library, CreatePlatformWindowSurface,
                  "eglCreatePlatformWindowSurface") &&
        BLIT_LOAD(gles->egl_library, DestroySurface, "eglDestroySurface") &&
        BLIT_LOAD(gles->egl_library, SwapBuffers, "eglSwapBuffers") &&
        BLIT_LOAD(gles->egl_library, GetError, "eglGetError");
    if (!ok) {
        gles_error(error, capacity, "libEGL lacks an EGL 1.5 entry point");
        return -1;
    }
    {
        static const char *const names[] = {
            "glGetString", "glGetError", "glCreateShader", "glShaderSource",
            "glCompileShader", "glGetShaderiv", "glGetShaderInfoLog",
            "glDeleteShader", "glCreateProgram", "glAttachShader",
            "glBindAttribLocation", "glLinkProgram", "glGetProgramiv",
            "glDeleteProgram", "glUseProgram", "glGetUniformLocation",
            "glUniform1i", "glGenBuffers", "glBindBuffer", "glBufferData",
            "glDeleteBuffers", "glEnableVertexAttribArray",
            "glVertexAttribPointer", "glGenTextures", "glBindTexture",
            "glTexParameteri", "glTexImage2D", "glTexSubImage2D",
            "glDeleteTextures", "glPixelStorei", "glActiveTexture",
            "glGenFramebuffers", "glBindFramebuffer", "glFramebufferTexture2D",
            "glCheckFramebufferStatus", "glDeleteFramebuffers", "glViewport",
            "glDisable", "glDrawArrays", "glReadPixels", "glFinish",
            "glEnable", "glBlendFunc", "glScissor", "glUniform2f",
            "glBufferSubData", "glGetAttribLocation",
            "glDisableVertexAttribArray", NULL
        };
        if (gles_load_symbols(gles->gl_library, (void **)&gles->glGetString, names) < 0) {
            gles_error(error, capacity, "libGLESv2 lacks a GLES 2 entry point");
            return -1;
        }
    }
    return 0;
}

static int gles_load_gbm(BlitGles *gles, char *error, size_t capacity)
{
    int ok;
    gles->gbm_library = dlopen("libgbm.so.1", RTLD_NOW | RTLD_LOCAL);
    if (gles->gbm_library == NULL) {
        gles_error(error, capacity, "libgbm.so.1 is not available");
        return -1;
    }
    ok = BLIT_LOAD(gles->gbm_library, gbm_create_device, "gbm_create_device") &&
        BLIT_LOAD(gles->gbm_library, gbm_device_destroy, "gbm_device_destroy") &&
        BLIT_LOAD(gles->gbm_library, gbm_surface_create, "gbm_surface_create") &&
        BLIT_LOAD(gles->gbm_library, gbm_surface_destroy, "gbm_surface_destroy") &&
        BLIT_LOAD(gles->gbm_library, gbm_surface_lock_front_buffer,
                  "gbm_surface_lock_front_buffer") &&
        BLIT_LOAD(gles->gbm_library, gbm_surface_release_buffer,
                  "gbm_surface_release_buffer") &&
        BLIT_LOAD(gles->gbm_library, gbm_bo_get_handle, "gbm_bo_get_handle") &&
        BLIT_LOAD(gles->gbm_library, gbm_bo_get_stride, "gbm_bo_get_stride");
    if (!ok) {
        gles_error(error, capacity, "libgbm lacks an entry point");
        return -1;
    }
    return 0;
}

static GLuint gles_compile(BlitGles *gles, GLenum type, const char *source,
                      char *error, size_t capacity)
{
    GLuint shader = gles->glCreateShader(type);
    GLint status = 0;
    gles->glShaderSource(shader, 1, &source, NULL);
    gles->glCompileShader(shader);
    gles->glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (!status) {
        char log[256];
        log[0] = '\0';
        gles->glGetShaderInfoLog(shader, sizeof(log), NULL, log);
        if (error != NULL && capacity > 0)
            snprintf(error, capacity, "shader does not compile: %s", log);
        gles->glDeleteShader(shader);
        return 0;
    }
    return shader;
}

/* One textured quad. The frame's rows are uploaded top-down, so texture row
   0 is the top of the screen; XRGB8888 bytes arrive as B,G,R,X in an RGBA
   texture and are swizzled back (core GLES 2 has no BGRA format). */
static int gles_build_program(BlitGles *gles, char *error, size_t capacity)
{
    static const char *vertex_source =
        "attribute vec2 position;\n"
        "varying vec2 coordinate;\n"
        "void main() {\n"
        "  coordinate = vec2((position.x + 1.0) * 0.5, (1.0 - position.y) * 0.5);\n"
        "  gl_Position = vec4(position, 0.0, 1.0);\n"
        "}\n";
    static const char *fragment_source =
        "#ifdef GL_FRAGMENT_PRECISION_HIGH\n"
        "precision highp float;\n"
        "#else\n"
        "precision mediump float;\n"
        "#endif\n"
        "varying vec2 coordinate;\n"
        "uniform sampler2D frame;\n"
        "void main() {\n"
        "  gl_FragColor = vec4(texture2D(frame, coordinate).bgr, 1.0);\n"
        "}\n";
    static const GLfloat quad[8] = { -1, -1, 1, -1, -1, 1, 1, 1 };
    GLuint vertex = gles_compile(gles, GL_VERTEX_SHADER, vertex_source, error, capacity);
    GLuint fragment;
    GLint status = 0;
    if (vertex == 0) return -1;
    fragment = gles_compile(gles, GL_FRAGMENT_SHADER, fragment_source, error, capacity);
    if (fragment == 0) {
        gles->glDeleteShader(vertex);
        return -1;
    }
    gles->program = gles->glCreateProgram();
    gles->glAttachShader(gles->program, vertex);
    gles->glAttachShader(gles->program, fragment);
    gles->glBindAttribLocation(gles->program, 0, "position");
    gles->glLinkProgram(gles->program);
    gles->glDeleteShader(vertex);
    gles->glDeleteShader(fragment);
    gles->glGetProgramiv(gles->program, GL_LINK_STATUS, &status);
    if (!status) {
        gles_error(error, capacity, "shader program does not link");
        return -1;
    }
    gles->sampler = gles->glGetUniformLocation(gles->program, "frame");
    {
        static const char *compose_vertex =
            "uniform vec2 size;\n"
            "attribute vec2 position;\n"
            "attribute vec2 texel;\n"
            "attribute vec4 colour;\n"
            "attribute float mode;\n"
            "varying vec2 coordinate;\n"
            "varying vec4 tint;\n"
            "varying float kind;\n"
            "void main() {\n"
            "  coordinate = texel; tint = colour; kind = mode;\n"
            "  gl_Position = vec4(position.x / size.x * 2.0 - 1.0,\n"
            "                     1.0 - position.y / size.y * 2.0, 0.0, 1.0);\n"
            "}\n";
        static const char *compose_fragment =
            "#ifdef GL_FRAGMENT_PRECISION_HIGH\n"
            "precision highp float;\n"
            "#else\n"
            "precision mediump float;\n"
            "#endif\n"
            "varying vec2 coordinate;\n"
            "varying vec4 tint;\n"
            "varying float kind;\n"
            "uniform sampler2D sprite;\n"
            "void main() {\n"
            "  vec4 s = texture2D(sprite, coordinate);\n"
            "  if (kind < 0.5) gl_FragColor = vec4(tint.rgb * tint.a, tint.a);\n"
            "  else if (kind < 1.5) gl_FragColor = vec4(tint.rgb * s.a, s.a);\n"
            "  else if (kind < 2.5) gl_FragColor = s;\n"
            "  else gl_FragColor = vec4(s.rgb * s.a, s.a);\n"
            "}\n";
        static const GLubyte white[4] = { 255, 255, 255, 255 };
        GLuint v = gles_compile(gles, GL_VERTEX_SHADER, compose_vertex, error, capacity);
        GLuint f = v ? gles_compile(gles, GL_FRAGMENT_SHADER, compose_fragment, error, capacity) : 0;
        if (f == 0) {
            if (v) gles->glDeleteShader(v);
            return -1;
        }
        gles->compose_program = gles->glCreateProgram();
        gles->glAttachShader(gles->compose_program, v);
        gles->glAttachShader(gles->compose_program, f);
        gles->glBindAttribLocation(gles->compose_program, 0, "position");
        gles->glBindAttribLocation(gles->compose_program, 1, "texel");
        gles->glBindAttribLocation(gles->compose_program, 2, "colour");
        gles->glBindAttribLocation(gles->compose_program, 3, "mode");
        gles->glLinkProgram(gles->compose_program);
        gles->glDeleteShader(v);
        gles->glDeleteShader(f);
        gles->glGetProgramiv(gles->compose_program, GL_LINK_STATUS, &status);
        if (!status) {
            gles_error(error, capacity, "composition program does not link");
            return -1;
        }
        gles->compose_sampler = gles->glGetUniformLocation(gles->compose_program, "sprite");
        gles->compose_size = gles->glGetUniformLocation(gles->compose_program, "size");
        gles->glGenBuffers(1, &gles->compose_buffer);
        gles->glGenTextures(1, &gles->white);
        gles->glBindTexture(GL_TEXTURE_2D, gles->white);
        gles->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        gles->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        gles->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA,
                           GL_UNSIGNED_BYTE, white);
    }
    gles->glGenBuffers(1, &gles->quad);
    gles->glBindBuffer(GL_ARRAY_BUFFER, gles->quad);
    gles->glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    return 0;
}

static BlitGles *gles_open_common(int surfaceless, int drm_fd, char *error,
                             size_t capacity)
{
    static const EGLint context_attributes[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE
    };
    BlitGles *gles = calloc(1, sizeof(*gles));
    EGLint major = 0, minor = 0;
    if (gles == NULL) {
        gles_error(error, capacity, "out of memory");
        return NULL;
    }
    gles->surfaceless = surfaceless;
    gles->display = EGL_NO_DISPLAY;
    gles->context = EGL_NO_CONTEXT;
    if (gles_load_egl_gl(gles, error, capacity) < 0) goto failed;
    if (surfaceless) {
        gles->display = gles->GetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA,
                                                 EGL_DEFAULT_DISPLAY, NULL);
    } else {
        if (gles_load_gbm(gles, error, capacity) < 0) goto failed;
        gles->gbm = gles->gbm_create_device(drm_fd);
        if (gles->gbm == NULL) {
            gles_error(error, capacity, "gbm_create_device failed on the KMS device");
            goto failed;
        }
        gles->display = gles->GetPlatformDisplay(EGL_PLATFORM_GBM_KHR,
                                                 gles->gbm, NULL);
    }
    if (gles->display == EGL_NO_DISPLAY ||
        !gles->Initialize(gles->display, &major, &minor)) {
        gles_error(error, capacity, "eglInitialize failed");
        goto failed;
    }
    gles->BindAPI(EGL_OPENGL_ES_API);
    if (!surfaceless) {
        static const EGLint wanted[] = {
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RED_SIZE, 8,
            EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 0,
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE
        };
        EGLConfig configs[64];
        EGLint count = 0, i;
        gles->config = NULL;
        if (!gles->ChooseConfig(gles->display, wanted, configs, 64, &count))
            count = 0;
        for (i = 0; i < count; ++i) {
            EGLint visual = 0;
            gles->GetConfigAttrib(gles->display, configs[i],
                                  EGL_NATIVE_VISUAL_ID, &visual);
            if ((uint32_t)visual == GBM_FORMAT_XRGB8888) {
                gles->config = configs[i];
                break;
            }
        }
        if (gles->config == NULL) {
            gles_error(error, capacity, "no EGL config scans out as XRGB8888");
            goto failed;
        }
    }
    gles->context = gles->CreateContext(gles->display, gles->config,
                                        EGL_NO_CONTEXT, context_attributes);
    if (gles->context == EGL_NO_CONTEXT ||
        !gles->MakeCurrent(gles->display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                           gles->context)) {
        gles_error(error, capacity, "no GLES 2 context");
        goto failed;
    }
    if (gles_build_program(gles, error, capacity) < 0) goto failed;
    snprintf(gles->identity, sizeof(gles->identity), "%s | %s",
             (const char *)gles->glGetString(GL_RENDERER),
             (const char *)gles->glGetString(GL_VERSION));
    return gles;
failed:
    blit_gles_close(gles);
    return NULL;
}

BlitGles *blit_gles_open_surfaceless(char *error, size_t capacity)
{
    return gles_open_common(1, -1, error, capacity);
}

BlitGles *blit_gles_open_gbm(int drm_fd, char *error, size_t capacity)
{
    return gles_open_common(0, drm_fd, error, capacity);
}

const char *blit_gles_identity(const BlitGles *gles)
{
    return gles != NULL ? gles->identity : "";
}

void blit_gles_close(BlitGles *gles)
{
    if (gles == NULL) return;
    if (gles->display != EGL_NO_DISPLAY) {
        if (gles->context != EGL_NO_CONTEXT) {
            gles->MakeCurrent(gles->display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                              gles->context);
            if (gles->program) gles->glDeleteProgram(gles->program);
            if (gles->compose_program) gles->glDeleteProgram(gles->compose_program);
            if (gles->compose_buffer) gles->glDeleteBuffers(1, &gles->compose_buffer);
            if (gles->white) gles->glDeleteTextures(1, &gles->white);
            if (gles->quad) gles->glDeleteBuffers(1, &gles->quad);
            gles->MakeCurrent(gles->display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                              EGL_NO_CONTEXT);
            gles->DestroyContext(gles->display, gles->context);
        }
        gles->Terminate(gles->display);
    }
    free(gles->vertices);
    if (gles->gbm != NULL) gles->gbm_device_destroy(gles->gbm);
    if (gles->gbm_library) dlclose(gles->gbm_library);
    if (gles->gl_library) dlclose(gles->gl_library);
    if (gles->egl_library) dlclose(gles->egl_library);
    free(gles);
}

static int gles_make_current(BlitGles *gles, BlitGlesTarget *target)
{
    if (gles->surfaceless) {
        if (!gles->MakeCurrent(gles->display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                               gles->context))
            return -1;
        gles->glBindFramebuffer(GL_FRAMEBUFFER, target->framebuffer);
        return 0;
    }
    if (!gles->MakeCurrent(gles->display, target->surface, target->surface,
                           gles->context))
        return -1;
    gles->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return 0;
}

static GLuint gles_new_texture(BlitGles *gles, int width, int height)
{
    GLuint texture = 0;
    gles->glGenTextures(1, &texture);
    gles->glBindTexture(GL_TEXTURE_2D, texture);
    gles->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gles->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gles->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gles->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gles->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA,
                       GL_UNSIGNED_BYTE, NULL);
    return texture;
}

BlitGlesTarget *blit_gles_target_open(BlitGles *gles, int width, int height,
                                      char *error, size_t capacity)
{
    BlitGlesTarget *target;
    if (gles == NULL || width < 1 || height < 1) {
        gles_error(error, capacity, "invalid target");
        return NULL;
    }
    target = calloc(1, sizeof(*target));
    if (target == NULL) {
        gles_error(error, capacity, "out of memory");
        return NULL;
    }
    target->width = width;
    target->height = height;
    target->surface = EGL_NO_SURFACE;
    if (!gles->surfaceless) {
        target->gbm_surface = gles->gbm_surface_create(gles->gbm,
            (uint32_t)width, (uint32_t)height, GBM_FORMAT_XRGB8888,
            GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
        if (target->gbm_surface != NULL)
            target->surface = gles->CreatePlatformWindowSurface(
                gles->display, gles->config, target->gbm_surface, NULL);
        if (target->surface == EGL_NO_SURFACE) {
            gles_error(error, capacity, "no GBM scanout surface for the output");
            blit_gles_target_close(gles, target);
            return NULL;
        }
    }
    if (gles->surfaceless &&
        !gles->MakeCurrent(gles->display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                           gles->context)) {
        gles_error(error, capacity, "eglMakeCurrent failed");
        blit_gles_target_close(gles, target);
        return NULL;
    }
    if (!gles->surfaceless && gles_make_current(gles, target) < 0) {
        gles_error(error, capacity, "eglMakeCurrent failed");
        blit_gles_target_close(gles, target);
        return NULL;
    }
    target->texture = gles_new_texture(gles, width, height);
    if (gles->surfaceless) {
        target->framebuffer_texture = gles_new_texture(gles, width, height);
        gles->glGenFramebuffers(1, &target->framebuffer);
        gles->glBindFramebuffer(GL_FRAMEBUFFER, target->framebuffer);
        gles->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                     GL_TEXTURE_2D, target->framebuffer_texture, 0);
        if (gles->glCheckFramebufferStatus(GL_FRAMEBUFFER) !=
            GL_FRAMEBUFFER_COMPLETE) {
            gles_error(error, capacity, "offscreen framebuffer is incomplete");
            blit_gles_target_close(gles, target);
            return NULL;
        }
    }
    if (gles->glGetError() != GL_NO_ERROR) {
        gles_error(error, capacity, "GL error creating the target");
        blit_gles_target_close(gles, target);
        return NULL;
    }
    return target;
}

void blit_gles_target_close(BlitGles *gles, BlitGlesTarget *target)
{
    if (gles == NULL || target == NULL) return;
    gles->MakeCurrent(gles->display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                      gles->context);
    if (target->framebuffer) gles->glDeleteFramebuffers(1, &target->framebuffer);
    if (target->framebuffer_texture)
        gles->glDeleteTextures(1, &target->framebuffer_texture);
    if (target->texture) gles->glDeleteTextures(1, &target->texture);
    if (target->surface != EGL_NO_SURFACE)
        gles->DestroySurface(gles->display, target->surface);
    if (target->gbm_surface != NULL)
        gles->gbm_surface_destroy(target->gbm_surface);
    free(target->shadow);
    free(target);
}

int blit_gles_draw_frame(BlitGles *gles, BlitGlesTarget *target,
                         const void *pixels, int pitch,
                         char *error, size_t capacity)
{
    const unsigned char *rows = pixels;
    int y;
    if (gles == NULL || target == NULL || pixels == NULL ||
        pitch < target->width * 4) {
        gles_error(error, capacity, "invalid frame");
        return -1;
    }
    if (gles_make_current(gles, target) < 0) {
        gles_error(error, capacity, "eglMakeCurrent failed");
        return -1;
    }
    gles->glBindTexture(GL_TEXTURE_2D, target->texture);
    gles->glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    {
        size_t row_bytes = (size_t)target->width * 4U;
        int first_upload = target->shadow == NULL;
        if (first_upload) {
            target->shadow = malloc(row_bytes * (size_t)target->height);
            if (target->shadow == NULL) {
                gles_error(error, capacity, "out of memory");
                return -1;
            }
        }
        /* Upload each run of changed rows. GLES 2 has no row length, so a
           padded source pitch is sent a row at a time. */
        y = 0;
        while (y < target->height) {
            int end;
            if (!first_upload &&
                memcmp(target->shadow + (size_t)y * row_bytes,
                       rows + (size_t)y * (size_t)pitch, row_bytes) == 0) {
                ++y;
                continue;
            }
            end = y + 1;
            while (end < target->height &&
                   (first_upload ||
                    memcmp(target->shadow + (size_t)end * row_bytes,
                           rows + (size_t)end * (size_t)pitch, row_bytes) != 0))
                ++end;
            if ((size_t)pitch == row_bytes) {
                gles->glTexSubImage2D(GL_TEXTURE_2D, 0, 0, y, target->width,
                                      end - y, GL_RGBA, GL_UNSIGNED_BYTE,
                                      rows + (size_t)y * (size_t)pitch);
                memcpy(target->shadow + (size_t)y * row_bytes,
                       rows + (size_t)y * (size_t)pitch,
                       row_bytes * (size_t)(end - y));
            } else {
                int row;
                for (row = y; row < end; ++row) {
                    gles->glTexSubImage2D(GL_TEXTURE_2D, 0, 0, row, target->width, 1,
                                          GL_RGBA, GL_UNSIGNED_BYTE,
                                          rows + (size_t)row * (size_t)pitch);
                    memcpy(target->shadow + (size_t)row * row_bytes,
                           rows + (size_t)row * (size_t)pitch, row_bytes);
                }
            }
            target->uploaded_rows += (unsigned long long)(end - y);
            y = end;
        }
    }
    gles->glViewport(0, 0, target->width, target->height);
    gles->glDisable(GL_BLEND);
    gles->glUseProgram(gles->program);
    gles->glActiveTexture(GL_TEXTURE0);
    gles->glBindTexture(GL_TEXTURE_2D, target->texture);
    gles->glUniform1i(gles->sampler, 0);
    gles->glBindBuffer(GL_ARRAY_BUFFER, gles->quad);
    gles->glDisableVertexAttribArray(1);
    gles->glDisableVertexAttribArray(2);
    gles->glDisableVertexAttribArray(3);
    gles->glEnableVertexAttribArray(0);
    gles->glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, (void *)0);
    gles->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    if (gles->glGetError() != GL_NO_ERROR) {
        gles_error(error, capacity, "GL error drawing the frame");
        return -1;
    }
    if (!gles->surfaceless &&
        !gles->SwapBuffers(gles->display, target->surface)) {
        gles_error(error, capacity, gles->GetError() == EGL_CONTEXT_LOST ?
                  "the GLES context was lost" : "eglSwapBuffers failed");
        return -1;
    }
    return 0;
}

unsigned long long blit_gles_uploaded_rows(const BlitGlesTarget *target)
{
    return target != NULL ? target->uploaded_rows : 0ULL;
}

int blit_gles_read_frame(BlitGles *gles, BlitGlesTarget *target,
                         void *pixels, int pitch)
{
    unsigned char *rgba;
    int x, y;
    if (gles == NULL || target == NULL || pitch < target->width * 4)
        return -1;
    /* On GBM this reads the frame being composed, before its swap. */
    gles_flush(gles);
    if (gles_make_current(gles, target) < 0) return -1;
    rgba = malloc((size_t)target->width * (size_t)target->height * 4U);
    if (rgba == NULL) return -1;
    gles->glPixelStorei(GL_PACK_ALIGNMENT, 4);
    gles->glReadPixels(0, 0, target->width, target->height, GL_RGBA,
                       GL_UNSIGNED_BYTE, rgba);
    /* glReadPixels starts at the bottom row. */
    for (y = 0; y < target->height; ++y) {
        const unsigned char *source = rgba +
            (size_t)(target->height - 1 - y) * (size_t)target->width * 4U;
        unsigned char *out = (unsigned char *)pixels + (size_t)y * (size_t)pitch;
        for (x = 0; x < target->width; ++x) {
            out[x * 4 + 0] = source[x * 4 + 2];
            out[x * 4 + 1] = source[x * 4 + 1];
            out[x * 4 + 2] = source[x * 4 + 0];
            out[x * 4 + 3] = 0;
        }
    }
    free(rgba);
    return gles->glGetError() == GL_NO_ERROR ? 0 : -1;
}

BlitGlesTexture blit_gles_texture_create(BlitGles *gles, int width, int height,
                                         int bytes_per_pixel)
{
    GLuint texture = 0;
    GLenum format = bytes_per_pixel == 3 ? GL_RGB : GL_RGBA;
    if (gles == NULL || width < 1 || height < 1) return 0;
    /* Binding a new texture must not change what queued quads sample. */
    gles_flush(gles);
    /* Only this call's errors decide its result. */
    while (gles->glGetError() != GL_NO_ERROR) {}
    gles->glGenTextures(1, &texture);
    gles->glBindTexture(GL_TEXTURE_2D, texture);
    gles->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gles->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gles->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gles->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gles->glTexImage2D(GL_TEXTURE_2D, 0, (GLint)format, width, height, 0, format,
                       GL_UNSIGNED_BYTE, NULL);
    gles->batch_texture = (GLuint)-1;
    if (gles->glGetError() != GL_NO_ERROR) {
        gles->glDeleteTextures(1, &texture);
        return 0;
    }
    return texture;
}

static void gles_flush(BlitGles *gles);

int blit_gles_texture_update(BlitGles *gles, BlitGlesTexture texture,
                             int bytes_per_pixel, int x, int y, int width,
                             int height, const void *pixels, int pitch)
{
    GLenum format = bytes_per_pixel == 3 ? GL_RGB : GL_RGBA;
    const unsigned char *rows = pixels;
    int row;
    if (gles == NULL || texture == 0 || pixels == NULL || width < 1 || height < 1)
        return -1;
    /* Queued quads may sample the old contents. */
    gles_flush(gles);
    while (gles->glGetError() != GL_NO_ERROR) {}
    gles->glBindTexture(GL_TEXTURE_2D, texture);
    gles->batch_texture = (GLuint)-1;
    gles->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (pitch == width * bytes_per_pixel)
        gles->glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, width, height, format,
                              GL_UNSIGNED_BYTE, pixels);
    else
        for (row = 0; row < height; ++row)
            gles->glTexSubImage2D(GL_TEXTURE_2D, 0, x, y + row, width, 1, format,
                                  GL_UNSIGNED_BYTE, rows + (size_t)row * (size_t)pitch);
    gles->glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    return gles->glGetError() == GL_NO_ERROR ? 0 : -1;
}

void blit_gles_texture_delete(BlitGles *gles, BlitGlesTexture texture)
{
    if (gles == NULL || texture == 0) return;
    gles_flush(gles);
    gles->glDeleteTextures(1, &texture);
    gles->batch_texture = (GLuint)-1;
}

static void gles_flush(BlitGles *gles)
{
    size_t stride = sizeof(struct BlitGlesVertex);
    if (gles->vertex_count == 0) return;
    gles->glBindBuffer(GL_ARRAY_BUFFER, gles->compose_buffer);
    gles->glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(stride * (size_t)gles->vertex_count),
                       gles->vertices, GL_STREAM_DRAW);
    gles->glEnableVertexAttribArray(0);
    gles->glEnableVertexAttribArray(1);
    gles->glEnableVertexAttribArray(2);
    gles->glEnableVertexAttribArray(3);
    gles->glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, (GLsizei)stride, (void *)0);
    gles->glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, (GLsizei)stride,
                                (void *)(sizeof(GLfloat) * 2));
    gles->glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, (GLsizei)stride,
                                (void *)(sizeof(GLfloat) * 4));
    gles->glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, (GLsizei)stride,
                                (void *)(sizeof(GLfloat) * 4 + 4));
    gles->glDrawArrays(GL_TRIANGLES, 0, gles->vertex_count);
    ++gles->draw_calls;
    gles->vertex_count = 0;
}

static void gles_vertex(BlitGles *gles, float x, float y, float u, float v,
                        uint32_t rgb, unsigned alpha, int mode)
{
    struct BlitGlesVertex *out;
    if (gles->vertex_count == gles->vertex_capacity) {
        int capacity = gles->vertex_capacity ? gles->vertex_capacity * 2 : 6 * 4096;
        struct BlitGlesVertex *grown = realloc(gles->vertices,
                                               sizeof(*grown) * (size_t)capacity);
        if (grown == NULL) return;
        gles->vertices = grown;
        gles->vertex_capacity = capacity;
    }
    out = &gles->vertices[gles->vertex_count++];
    out->x = x; out->y = y; out->u = u; out->v = v;
    out->r = (GLubyte)(rgb >> 16); out->g = (GLubyte)(rgb >> 8); out->b = (GLubyte)rgb;
    out->a = (GLubyte)alpha;
    out->mode = (GLfloat)mode;
}

static void gles_rectangle(BlitGles *gles, float x0, float y0, float x1, float y1,
                           float u0, float v0, float u1, float v1,
                           uint32_t rgb, unsigned alpha, int mode)
{
    gles_vertex(gles, x0, y0, u0, v0, rgb, alpha, mode);
    gles_vertex(gles, x1, y0, u1, v0, rgb, alpha, mode);
    gles_vertex(gles, x0, y1, u0, v1, rgb, alpha, mode);
    gles_vertex(gles, x1, y0, u1, v0, rgb, alpha, mode);
    gles_vertex(gles, x1, y1, u1, v1, rgb, alpha, mode);
    gles_vertex(gles, x0, y1, u0, v1, rgb, alpha, mode);
}

int blit_gles_frame_begin(BlitGles *gles, BlitGlesTarget *target,
                          char *error, size_t capacity)
{
    if (gles == NULL || target == NULL ||
        gles_make_current(gles, target) < 0) {
        gles_error(error, capacity, "eglMakeCurrent failed");
        return -1;
    }
    gles->frame_target = target;
    gles->bound_height = target->height;
    gles->vertex_count = 0;
    gles->glViewport(0, 0, target->width, target->height);
    gles->glDisable(GL_SCISSOR_TEST);
    gles->glEnable(GL_BLEND);
    gles->glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    gles->glUseProgram(gles->compose_program);
    gles->glUniform2f(gles->compose_size, (GLfloat)target->width,
                      (GLfloat)target->height);
    gles->glUniform1i(gles->compose_sampler, 0);
    gles->glActiveTexture(GL_TEXTURE0);
    gles->glBindTexture(GL_TEXTURE_2D, gles->white);
    gles->batch_texture = gles->white;
    return 0;
}

void blit_gles_rect(BlitGles *gles, int x, int y, int width, int height,
                    uint32_t rgb, unsigned alpha)
{
    if (gles == NULL || width < 1 || height < 1 || alpha == 0) return;
    gles_rectangle(gles, (float)x, (float)y, (float)(x + width), (float)(y + height),
                   0, 0, 1, 1, rgb, alpha > 255 ? 255 : alpha, BLIT_GLES_SOLID);
}

void blit_gles_quad(BlitGles *gles, int mode, BlitGlesTexture texture,
                    int texture_width, int texture_height,
                    int source_x, int source_y, int source_width,
                    int source_height, int x, int y, int width, int height,
                    uint32_t rgb)
{
    if (gles == NULL || texture == 0 || width < 1 || height < 1 ||
        texture_width < 1 || texture_height < 1)
        return;
    if (gles->batch_texture != texture) {
        gles_flush(gles);
        gles->glBindTexture(GL_TEXTURE_2D, texture);
        gles->batch_texture = texture;
    }
    gles_rectangle(gles, (float)x, (float)y, (float)(x + width), (float)(y + height),
                   (float)source_x / (float)texture_width,
                   (float)source_y / (float)texture_height,
                   (float)(source_x + source_width) / (float)texture_width,
                   (float)(source_y + source_height) / (float)texture_height,
                   rgb, 255, mode);
}

void blit_gles_clip(BlitGles *gles, int x, int y, int width, int height)
{
    if (gles == NULL || gles->bound_height == 0) return;
    gles_flush(gles);
    if (width <= 0 || height <= 0) {
        gles->glDisable(GL_SCISSOR_TEST);
        return;
    }
    gles->glEnable(GL_SCISSOR_TEST);
    /* GL's scissor is bottom-up. */
    gles->glScissor(x, gles->bound_height - (y + height), width, height);
}

int blit_gles_frame_end(BlitGles *gles, BlitGlesTarget *target,
                        char *error, size_t capacity)
{
    if (gles == NULL || target == NULL) return -1;
    gles_flush(gles);
    gles->glDisable(GL_SCISSOR_TEST);
    gles->glDisable(GL_BLEND);
    gles->frame_target = NULL;
    gles->bound_height = 0;
    if (gles->glGetError() != GL_NO_ERROR) {
        gles_error(error, capacity, "GL error composing the frame");
        return -1;
    }
    if (!gles->surfaceless && !gles->SwapBuffers(gles->display, target->surface)) {
        gles_error(error, capacity, gles->GetError() == EGL_CONTEXT_LOST ?
                   "the GLES context was lost" : "eglSwapBuffers failed");
        return -1;
    }
    return 0;
}

BlitGlesScene *blit_gles_scene_open(BlitGles *gles, int width, int height)
{
    BlitGlesScene *scene;
    if (gles == NULL || width < 1 || height < 1) return NULL;
    scene = calloc(1, sizeof(*scene));
    if (scene == NULL) return NULL;
    scene->width = width;
    scene->height = height;
    scene->texture = gles_new_texture(gles, width, height);
    gles->glGenFramebuffers(1, &scene->framebuffer);
    gles->glBindFramebuffer(GL_FRAMEBUFFER, scene->framebuffer);
    gles->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                 GL_TEXTURE_2D, scene->texture, 0);
    gles->batch_texture = (GLuint)-1;
    if (gles->glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE ||
        gles->glGetError() != GL_NO_ERROR) {
        blit_gles_scene_close(gles, scene);
        return NULL;
    }
    if (blit_gles_scene_begin(gles, scene) == 0) {
        blit_gles_rect(gles, 0, 0, width, height, 0, 255);
        blit_gles_scene_end(gles);
    }
    return scene;
}

void blit_gles_scene_close(BlitGles *gles, BlitGlesScene *scene)
{
    if (gles == NULL || scene == NULL) return;
    gles_flush(gles);
    if (scene->framebuffer) gles->glDeleteFramebuffers(1, &scene->framebuffer);
    if (scene->texture) gles->glDeleteTextures(1, &scene->texture);
    gles->batch_texture = (GLuint)-1;
    free(scene);
}

int blit_gles_scene_begin(BlitGles *gles, BlitGlesScene *scene)
{
    if (gles == NULL || scene == NULL) return -1;
    gles_flush(gles);
    if (gles->frame_target == NULL &&
        !gles->MakeCurrent(gles->display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                           gles->context))
        return -1;
    gles->glBindFramebuffer(GL_FRAMEBUFFER, scene->framebuffer);
    gles->bound_height = scene->height;
    gles->glViewport(0, 0, scene->width, scene->height);
    gles->glDisable(GL_SCISSOR_TEST);
    gles->glEnable(GL_BLEND);
    gles->glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    gles->glUseProgram(gles->compose_program);
    gles->glUniform2f(gles->compose_size, (GLfloat)scene->width,
                      (GLfloat)scene->height);
    gles->glUniform1i(gles->compose_sampler, 0);
    gles->glActiveTexture(GL_TEXTURE0);
    gles->glBindTexture(GL_TEXTURE_2D, gles->white);
    gles->batch_texture = gles->white;
    return 0;
}

void blit_gles_scene_end(BlitGles *gles)
{
    if (gles == NULL) return;
    gles_flush(gles);
    gles->glDisable(GL_SCISSOR_TEST);
    if (gles->frame_target != NULL) {
        /* Back to the frame being composed. */
        BlitGlesTarget *target = gles->frame_target;
        gles_make_current(gles, target);
        gles->bound_height = target->height;
        gles->glViewport(0, 0, target->width, target->height);
        gles->glUniform2f(gles->compose_size, (GLfloat)target->width,
                          (GLfloat)target->height);
    } else {
        gles->bound_height = 0;
    }
}

void blit_gles_scene_draw(BlitGles *gles, BlitGlesScene *scene)
{
    if (gles == NULL || scene == NULL) return;
    /* The scene is opaque, so premultiplied source-over is a copy. Its rows
       are bottom-up as rendered, so the source runs from its top down. */
    blit_gles_quad(gles, BLIT_GLES_PREMULT, scene->texture, scene->width,
                   scene->height, 0, scene->height, scene->width, -scene->height,
                   0, 0, scene->width, scene->height, 0);
}

unsigned long long blit_gles_draw_calls(const BlitGles *gles)
{
    return gles != NULL ? gles->draw_calls : 0ULL;
}

void *blit_gles_lock_front(BlitGles *gles, BlitGlesTarget *target,
                           uint32_t *handle, uint32_t *pitch)
{
    struct gbm_bo *bo;
    if (gles == NULL || target == NULL || target->gbm_surface == NULL)
        return NULL;
    bo = gles->gbm_surface_lock_front_buffer(target->gbm_surface);
    if (bo == NULL) return NULL;
    if (handle != NULL) *handle = gles->gbm_bo_get_handle(bo).u32;
    if (pitch != NULL) *pitch = gles->gbm_bo_get_stride(bo);
    return bo;
}

void blit_gles_release(BlitGles *gles, BlitGlesTarget *target, void *buffer)
{
    if (gles != NULL && target != NULL && target->gbm_surface != NULL &&
        buffer != NULL)
        gles->gbm_surface_release_buffer(target->gbm_surface, buffer);
}
