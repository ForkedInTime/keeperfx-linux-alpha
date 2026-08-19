#include "pre_inc.h"
#include "kfx/renderer/RendererGL.h"
#include "bflib_basics.h"      // SYNCDBG, ERRORLOG
#include "bflib_video.h"       // PALETTE_COLORS, lbWindow, SDL
#include "bflib_vidsurface.h"  // lbDrawSurface (goes away when the framebuffer migrates)
#include "bflib_mouse.h"       // LbMouseOnBeginSwap/EndSwap (software cursor around present)
#include "bflib_render_gl.h"   // gl_present_* -- every GL call this backend makes
#include "post_inc.h"

bool RendererGL::Init()
{
    // The GPU backend binds to a window and to the engine's draw surface. Neither
    // exists yet at the first, pre-window RendererInit() in main(), so report
    // unavailable and let AUTO resolve to software for the time being;
    // LbScreenSetup() calls RendererReinit() once both are up, and the GPU
    // backend wins the second attempt.
    if ((lbWindow == NULL) || (lbDrawSurface == NULL))
    {
        SYNCDBG(4, "OpenGL backend unavailable: no window or draw surface yet");
        return false;
    }
    if (!gl_present_init(lbWindow, lbDrawSurface->w, lbDrawSurface->h))
        return false;
    m_inited = true;
    // The movie player bypasses the seam to present truecolor frames through the
    // same GL context (bflib_fmvids.cpp); this is what tells it the context is up.
    lbUseGLPresent = true;
    return true;
}

void RendererGL::Shutdown()
{
    lbUseGLPresent = false;
    m_inited = false;
    gl_present_shutdown();
}

void RendererGL::SetDisplayPalette(const unsigned char* rgb8)
{
    SDL_Color colors[PALETTE_COLORS];
    for (int i = 0; i < PALETTE_COLORS; i++)
    {
        colors[i].r = rgb8[3 * i + 0];
        colors[i].g = rgb8[3 * i + 1];
        colors[i].b = rgb8[3 * i + 2];
        colors[i].a = SDL_ALPHA_OPAQUE;
    }
    // The GPU LUT is what the present pass samples, so that upload is the one
    // that matters for what the player sees.
    gl_present_set_palette(colors, PALETTE_COLORS);
    // The draw surface keeps its own palette in step regardless. Nothing in the
    // present path reads it here, but everything else that treats lbDrawSurface
    // as a complete image does -- screenshots most of all -- and letting it drift
    // would make those silently wrong only on this backend.
    if (lbDrawSurface != NULL)
    {
        SDL_Palette* surfpal = SDL_GetSurfacePalette(lbDrawSurface);
        if (surfpal != NULL)
            SDL_SetPaletteColors(surfpal, colors, 0, PALETTE_COLORS);
    }
}

void RendererGL::ClearScreen(unsigned char colour)
{
    // Identical to the software backend: both present the same indexed surface,
    // so clearing is a surface operation either way.
    if (lbDrawSurface == NULL)
        return;
    if (!SDL_FillSurfaceRect(lbDrawSurface, NULL, colour))
        ERRORLOG("Error while clearing screen: %s", SDL_GetError());
}

unsigned char* RendererGL::LockFramebuffer(int* out_pitch)
{
    if (lbDrawSurface == NULL || !SDL_LockSurface(lbDrawSurface))
        return nullptr;
    if (out_pitch != nullptr)
        *out_pitch = lbDrawSurface->pitch;
    return static_cast<unsigned char*>(lbDrawSurface->pixels);
}

void RendererGL::UnlockFramebuffer()
{
    if (lbDrawSurface != NULL)
        SDL_UnlockSurface(lbDrawSurface);
}

bool RendererGL::ScheduleScreenshot(const char* path, int fmt)
{
    // Deliberately unimplemented here: saving lbDrawSurface would miss anything
    // the GPU adds after the palette lookup (the post-FX chain), so what a
    // screenshot should contain on this backend is a decision of its own.
    (void)path;
    (void)fmt;
    return false;
}

void RendererGL::PresentFrame()
{
    if (lbDrawSurface == NULL || !m_inited)
        return;
    // Same bracket as the software backend: the cursor is drawn into the surface
    // by BeginSwap and taken back out by EndSwap. Presenting outside it smears it.
    LbMouseOnBeginSwap();
    gl_present_frame(lbDrawSurface->pixels, lbDrawSurface->w, lbDrawSurface->h,
                     lbDrawSurface->pitch);
    LbMouseOnEndSwap();
}
