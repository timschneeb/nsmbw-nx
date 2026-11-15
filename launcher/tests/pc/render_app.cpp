#include "app.hpp"
#include "pc_app_common.hpp"

#include "ui/widget.hpp"

#include <epoxy/egl.h>
#include <epoxy/gl.h>
#include <nanovg.h>
#define NANOVG_GL3 1
#include "nanovg_gl.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <cstdio>
#include <filesystem>
#include <vector>

namespace launcher {

App::App() {
    pc::CurrentApp() = this;
    pc::LoadLogo();

    auto get_platform = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
        eglGetProcAddress("eglGetPlatformDisplayEXT"));
    EGLDisplay display = get_platform
        ? get_platform(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr)
        : eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display == EGL_NO_DISPLAY || !eglInitialize(display, nullptr, nullptr)) {
        std::fprintf(stderr, "render: eglInitialize failed\n");
        return;
    }
    m_egl_display = display;
    if (!eglBindAPI(EGL_OPENGL_API)) {
        std::fprintf(stderr, "render: eglBindAPI failed\n");
        return;
    }

    const EGLint context_attribs[] = {
        EGL_CONTEXT_MAJOR_VERSION, 3,
        EGL_CONTEXT_MINOR_VERSION, 3,
        EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
        EGL_NONE,
    };
    EGLContext context = eglCreateContext(display, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, context_attribs);
    if (context == EGL_NO_CONTEXT ||
        !eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context)) {
        std::fprintf(stderr, "render: EGL context failed 0x%x\n", eglGetError());
        return;
    }
    m_egl_context = context;

    while (glGetError() != GL_NO_ERROR) {
    }

    glGenFramebuffers(1, &m_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glGenTextures(1, &m_color);
    glBindTexture(GL_TEXTURE_2D, m_color);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_width, m_height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_color, 0);
    glGenRenderbuffers(1, &m_depth);
    glBindRenderbuffer(GL_RENDERBUFFER, m_depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, m_width, m_height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, m_depth);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::fprintf(stderr, "render: framebuffer incomplete\n");
    }

    m_vg = nvgCreateGL3(NVG_ANTIALIAS | NVG_STENCIL_STROKES);
    if (m_vg == nullptr) {
        std::fprintf(stderr, "render: nvgCreateGL3 failed\n");
        return;
    }
    pc::LoadFonts(m_vg);
    pc::LoadTheme(Theme());
}

App::~App() {
    m_widgets.clear();
    if (m_vg != nullptr) {
        nvgDeleteGL3(m_vg);
        m_vg = nullptr;
    }
    if (m_fbo != 0) {
        glDeleteFramebuffers(1, &m_fbo);
        glDeleteTextures(1, &m_color);
        glDeleteRenderbuffers(1, &m_depth);
    }
    if (m_egl_context != EGL_NO_CONTEXT) {
        eglMakeCurrent(m_egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroyContext(m_egl_display, m_egl_context);
    }
    if (m_egl_display != EGL_NO_DISPLAY) {
        eglTerminate(m_egl_display);
    }
    if (pc::CurrentApp() == this) {
        pc::CurrentApp() = nullptr;
    }
}

auto App::RenderScreenshot(const std::string& path) -> bool {
    if (m_vg == nullptr) {
        return false;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glViewport(0, 0, m_width, m_height);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0.f, 0.f, 0.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    RenderFrame();
    glFinish();

    std::vector<unsigned char> pixels(static_cast<size_t>(m_width) * m_height * 4);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glReadPixels(0, 0, m_width, m_height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    stbi_flip_vertically_on_write(1);
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    return stbi_write_png(path.c_str(), m_width, m_height, 4, pixels.data(), m_width * 4) != 0;
}

} // namespace launcher
