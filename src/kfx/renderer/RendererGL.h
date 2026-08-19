#ifndef RENDERER_RENDERERGL_H
#define RENDERER_RENDERERGL_H

#include "kfx/renderer/IRenderer.h"

// GPU present backend. The engine still draws into the same 8-bit indexed
// surface the software backend uses; the difference is what happens at present
// time. The software backend expands that frame to RGBA on the CPU every frame;
// this one uploads the indices as-is and does the palette lookup in a fragment
// shader from a 256x1 LUT texture, which is where the CPU cost goes away.
//
// Deliberately thin: every GL call lives in bflib_render_gl.c behind the
// gl_present_* entry points. This class only adapts them to IRenderer, so the
// GL pipeline stays usable from outside the seam (the movie player presents
// truecolor frames straight through it) and so nothing here has to be revisited
// when the interface grows.
class RendererGL : public IRenderer {
public:
    bool Init() override;
    void Shutdown() override;
    const char* GetName() const override { return "opengl"; }
    void SetDisplayPalette(const unsigned char* rgb8) override;
    void ClearScreen(unsigned char colour) override;
    void PresentFrame() override;
    unsigned char* LockFramebuffer(int* out_pitch) override;
    void UnlockFramebuffer() override;
    bool ScheduleScreenshot(const char* path, int fmt) override;

private:
    bool m_inited = false;
};

#endif // RENDERER_RENDERERGL_H
