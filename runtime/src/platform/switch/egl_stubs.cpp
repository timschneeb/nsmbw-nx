// Local EGL declarations matching the stable Khronos ABI. The portlibs
// include dir is not on the build path, and these stubs must not depend on it.
using EGLint = int;
using EGLBoolean = unsigned int;
using EGLenum = unsigned int;
using EGLDisplay = void*;
using EGLConfig = void*;
using EGLSurface = void*;
using EGLContext = void*;
using EGLNativeDisplayType = void*;
using EGLNativeWindowType = void*;
#define EGL_FALSE 0
#define EGL_SUCCESS 0x3000
#define EGL_NO_DISPLAY ((EGLDisplay)0)
#define EGL_NO_CONTEXT ((EGLContext)0)
#define EGL_NO_SURFACE ((EGLSurface)0)

// Aurora presents through Dawn/NVK on the default NWindow directly; SDL's
// Switch video backend references these only from its GL context functions
// (SWITCH_GL_*), which nothing ever calls. Value-returning stubs keep the
// link whole without dragging in a second GL implementation.

extern "C" {

EGLDisplay eglGetDisplay(EGLNativeDisplayType) { return EGL_NO_DISPLAY; }
EGLBoolean eglInitialize(EGLDisplay, EGLint*, EGLint*) { return EGL_FALSE; }
EGLBoolean eglTerminate(EGLDisplay) { return EGL_FALSE; }
EGLBoolean eglChooseConfig(EGLDisplay, const EGLint*, EGLConfig*, EGLint, EGLint*) {
    return EGL_FALSE;
}
EGLSurface eglCreateWindowSurface(EGLDisplay, EGLConfig, EGLNativeWindowType, const EGLint*) {
    return EGL_NO_SURFACE;
}
EGLBoolean eglDestroySurface(EGLDisplay, EGLSurface) { return EGL_FALSE; }
EGLContext eglCreateContext(EGLDisplay, EGLConfig, EGLContext, const EGLint*) {
    return EGL_NO_CONTEXT;
}
EGLBoolean eglDestroyContext(EGLDisplay, EGLContext) { return EGL_FALSE; }
EGLBoolean eglMakeCurrent(EGLDisplay, EGLSurface, EGLSurface, EGLContext) { return EGL_FALSE; }
EGLBoolean eglSwapBuffers(EGLDisplay, EGLSurface) { return EGL_FALSE; }
EGLBoolean eglSwapInterval(EGLDisplay, EGLint) { return EGL_FALSE; }
EGLint eglGetError() { return EGL_SUCCESS; }
const char* eglQueryString(EGLDisplay, EGLint) { return ""; }
EGLBoolean eglBindAPI(EGLenum) { return EGL_FALSE; }
void* eglGetProcAddress(const char*) { return nullptr; }

}  // extern "C"
