#include "nanovg.h"

#include <cstring>

NVGcolor nvgRGB(unsigned char r, unsigned char g, unsigned char b) {
    return nvgRGBA(r, g, b, 255);
}
NVGcolor nvgRGBf(float r, float g, float b) { return nvgRGBAf(r, g, b, 1.f); }
NVGcolor nvgRGBA(unsigned char r, unsigned char g, unsigned char b, unsigned char a) {
    NVGcolor c{};
    c.r = r / 255.f;
    c.g = g / 255.f;
    c.b = b / 255.f;
    c.a = a / 255.f;
    return c;
}
NVGcolor nvgRGBAf(float r, float g, float b, float a) {
    NVGcolor c{};
    c.r = r;
    c.g = g;
    c.b = b;
    c.a = a;
    return c;
}

void nvgBeginFrame(NVGcontext*, float, float, float) {}
void nvgEndFrame(NVGcontext*) {}
void nvgResetTransform(NVGcontext*) {}
void nvgScale(NVGcontext*, float, float) {}
void nvgFontFace(NVGcontext*, const char*) {}
void nvgSave(NVGcontext*) {}
void nvgRestore(NVGcontext*) {}
void nvgIntersectScissor(NVGcontext*, float, float, float, float) {}
void nvgScissor(NVGcontext*, float, float, float, float) {}
void nvgBeginPath(NVGcontext*) {}
void nvgMoveTo(NVGcontext*, float, float) {}
void nvgLineTo(NVGcontext*, float, float) {}
void nvgArc(NVGcontext*, float, float, float, float, float, int) {}
void nvgArcTo(NVGcontext*, float, float, float, float, float) {}
void nvgClosePath(NVGcontext*) {}
void nvgPathWinding(NVGcontext*, int) {}
void nvgRect(NVGcontext*, float, float, float, float) {}
void nvgRoundedRect(NVGcontext*, float, float, float, float, float) {}
void nvgCircle(NVGcontext*, float, float, float) {}
void nvgFill(NVGcontext*) {}
void nvgStroke(NVGcontext*) {}
void nvgFillColor(NVGcontext*, NVGcolor) {}
void nvgStrokeColor(NVGcontext*, NVGcolor) {}
void nvgFillPaint(NVGcontext*, NVGpaint) {}
void nvgStrokePaint(NVGcontext*, NVGpaint) {}
void nvgStrokeWidth(NVGcontext*, float) {}
void nvgLineJoin(NVGcontext*, int) {}
void nvgLineCap(NVGcontext*, int) {}
NVGpaint nvgLinearGradient(NVGcontext*, float, float, float, float, NVGcolor, NVGcolor) { return {}; }
NVGpaint nvgBoxGradient(NVGcontext*, float, float, float, float, float, float, NVGcolor, NVGcolor) { return {}; }
NVGpaint nvgRadialGradient(NVGcontext*, float, float, float, float, NVGcolor, NVGcolor) { return {}; }
NVGpaint nvgImagePattern(NVGcontext*, float, float, float, float, float, int, float) { return {}; }
void nvgTextAlign(NVGcontext*, int) {}
void nvgFontSize(NVGcontext*, float) {}
void nvgTextLineHeight(NVGcontext*, float) {}
float nvgText(NVGcontext*, float, float, const char*, const char*) { return 0.f; }
void nvgTextBox(NVGcontext*, float, float, float, const char*, const char*) {}
float nvgTextBounds(NVGcontext*, float, float, const char*, const char*, float* bounds) {
    if (bounds != nullptr) std::memset(bounds, 0, sizeof(float) * 4);
    return 0.f;
}
void nvgTextBoxBounds(NVGcontext*, float, float, float, const char*, const char*, float* bounds) {
    if (bounds != nullptr) std::memset(bounds, 0, sizeof(float) * 4);
}
int nvgCreateImage(NVGcontext*, const char*, int) { return 0; }
int nvgCreateImageRGBA(NVGcontext*, int, int, int, const unsigned char*) { return 0; }
void nvgImageSize(NVGcontext*, int, int* w, int* h) {
    if (w != nullptr) *w = 0;
    if (h != nullptr) *h = 0;
}
void nvgDeleteImage(NVGcontext*, int) {}
int nvgCreateFont(NVGcontext*, const char*, const char*) { return -1; }
int nvgCreateFontMem(NVGcontext*, const char*, unsigned char*, int, int) { return 0; }
int nvgAddFallbackFontId(NVGcontext*, int, int) { return 0; }
