/*
 * Bring-up test: create an EGL context the way SurfaceFlinger's
 * GLESRenderEngine does, and print every EGL error on the way.
 */
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <stdio.h>

static const char* err(EGLint e) {
    switch (e) {
        case EGL_SUCCESS: return "EGL_SUCCESS";
        case EGL_NOT_INITIALIZED: return "EGL_NOT_INITIALIZED";
        case EGL_BAD_ACCESS: return "EGL_BAD_ACCESS";
        case EGL_BAD_ALLOC: return "EGL_BAD_ALLOC";
        case EGL_BAD_ATTRIBUTE: return "EGL_BAD_ATTRIBUTE";
        case EGL_BAD_CONFIG: return "EGL_BAD_CONFIG";
        case EGL_BAD_CONTEXT: return "EGL_BAD_CONTEXT";
        case EGL_BAD_DISPLAY: return "EGL_BAD_DISPLAY";
        case EGL_BAD_MATCH: return "EGL_BAD_MATCH";
        case EGL_BAD_PARAMETER: return "EGL_BAD_PARAMETER";
        default: return "other";
    }
}

static void tryContext(EGLDisplay dpy, EGLConfig cfg, EGLint version) {
    const EGLint attrs[] = {EGL_CONTEXT_CLIENT_VERSION, version, EGL_NONE};
    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, attrs);
    EGLint e = eglGetError();
    printf("  eglCreateContext(version %d, cfg %p): %s (0x%x)\n", version, cfg,
           ctx == EGL_NO_CONTEXT ? "FAILED" : "ok", e);
    if (ctx != EGL_NO_CONTEXT) eglDestroyContext(dpy, ctx);
}

int main() {
    EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint major = 0, minor = 0;
    if (!eglInitialize(dpy, &major, &minor)) {
        printf("eglInitialize failed: %s\n", err(eglGetError()));
        return 1;
    }
    printf("EGL %d.%d vendor=%s\n", major, minor, eglQueryString(dpy, EGL_VENDOR));
    printf("extensions: %s\n", eglQueryString(dpy, EGL_EXTENSIONS));
    printf("client APIs: %s\n", eglQueryString(dpy, EGL_CLIENT_APIS));

    EGLint n = 0;
    eglGetConfigs(dpy, nullptr, 0, &n);
    printf("%d configs\n", n);
    EGLConfig cfgs[64];
    if (n > 64) n = 64;
    eglGetConfigs(dpy, cfgs, n, &n);
    for (int i = 0; i < n; i++) {
        EGLint r, g, b, a, rt, st, id, rec = -1, nvid;
        eglGetConfigAttrib(dpy, cfgs[i], EGL_RED_SIZE, &r);
        eglGetConfigAttrib(dpy, cfgs[i], EGL_GREEN_SIZE, &g);
        eglGetConfigAttrib(dpy, cfgs[i], EGL_BLUE_SIZE, &b);
        eglGetConfigAttrib(dpy, cfgs[i], EGL_ALPHA_SIZE, &a);
        eglGetConfigAttrib(dpy, cfgs[i], EGL_RENDERABLE_TYPE, &rt);
        eglGetConfigAttrib(dpy, cfgs[i], EGL_SURFACE_TYPE, &st);
        eglGetConfigAttrib(dpy, cfgs[i], EGL_CONFIG_ID, &id);
        eglGetConfigAttrib(dpy, cfgs[i], EGL_RECORDABLE_ANDROID, &rec);
        eglGetConfigAttrib(dpy, cfgs[i], EGL_NATIVE_VISUAL_ID, &nvid);
        printf("cfg %2d %p id=%d rgba=%d%d%d%d renderable=0x%x surface=0x%x recordable=%d visual=%d\n",
               i, cfgs[i], id, r, g, b, a, rt, st, rec, nvid);
    }

    // Same attributes as GLESRenderEngine::chooseEglConfig for RGBA_8888
    const EGLint want[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
                           EGL_RECORDABLE_ANDROID, EGL_TRUE,
                           EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
                           EGL_NONE};
    EGLConfig chosen = nullptr;
    EGLint found = 0;
    eglChooseConfig(dpy, want, &chosen, 1, &found);
    printf("chooseConfig(ES2, recordable, 8888): found=%d cfg=%p err=%s\n", found, chosen,
           err(eglGetError()));

    printf("contexts with the chosen config:\n");
    if (found) {
        tryContext(dpy, chosen, 3);
        tryContext(dpy, chosen, 2);
        tryContext(dpy, chosen, 1);
    }
    printf("contexts with each config:\n");
    for (int i = 0; i < n && i < 8; i++) tryContext(dpy, cfgs[i], 2);
    printf("contexts with EGL_NO_CONFIG_KHR:\n");
    tryContext(dpy, EGL_NO_CONFIG_KHR, 3);
    tryContext(dpy, EGL_NO_CONFIG_KHR, 2);
    eglTerminate(dpy);
    return 0;
}
