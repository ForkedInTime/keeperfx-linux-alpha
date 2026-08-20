#include "pre_inc.h"
#include "kfx/renderer/RendererGL.h"
#include "bflib_basics.h"      // SYNCDBG, ERRORLOG
#include "bflib_video.h"       // PALETTE_COLORS, lbWindow, SDL
#include "bflib_vidsurface.h"  // lbDrawSurface (goes away when the framebuffer migrates)
#include "bflib_mouse.h"       // LbMouseOnBeginSwap/EndSwap (software cursor around present)
#include "bflib_render_gl.h"   // gl_present_* -- every GL call this backend makes
#include <SDL3_image/SDL_image.h> // IMG_SavePNG (screenshots)
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
    if (lbDrawSurface == NULL)
        return false;
    // Decision: always save the pre-post-FX indexed surface, exactly like
    // RendererSoftware does -- even when the post-FX chain (bloom/tonemap/
    // grade, KFX_POSTFX=1) is the active present path and the screen is
    // showing something the surface no longer matches. Deliberate, not an
    // oversight:
    //  - It keeps screenshots byte-identical in content across backends
    //    (AUTO can pick either one on a given machine), rather than having
    //    the same key produce a different kind of image depending on which
    //    backend won.
    //  - Post-FX is an opt-in local "atmosphere" toggle, off by default; the
    //    indexed surface is the faithful classic frame, which is the more
    //    useful thing to archive and share (bug reports, comparisons) than
    //    one player's exposure/bloom/grade settings baked in.
    //  - A correct post-FX capture needs glReadPixels(GL_BACK) timed to land
    //    between gl_run_composite() and SDL_GL_SwapWindow() in
    //    gl_present_frame(), with the letterboxed viewport cropped back out
    //    and a vertical flip undone -- real GL surface area (see
    //    gl_present_postfx_active()'s doc comment) that a debug/QoL feature
    //    does not need to earn just because it is possible.
    //  - It is also a structural guarantee, not just an intended behaviour:
    //    gl_upload_indices() (bflib_render_gl.c) only ever reads fb_pixels
    //    into the GL index texture. Nothing on the GL present path, post-FX
    //    or not, ever writes back into lbDrawSurface -- there is no code
    //    path by which this save could pick up post-FX output even by
    //    accident.
    if (gl_present_postfx_active())
    {
        // Informational, not a warning: this is the documented, deliberate
        // behaviour above, not a failure. SYNCLOG (not SYNCDBG) so the one
        // user actually affected -- someone running with KFX_POSTFX=1 -- sees
        // it in their log at the default build level; screenshots are
        // user-initiated and infrequent, so once per screenshot is not spam.
        SYNCLOG("Screenshot saved the pre-post-FX frame: post-FX "
                "(KFX_POSTFX=1) is active but its output is not included, "
                "by design -- see ScheduleScreenshot()");
    }
    bool ok;
    switch (fmt)
    {
        case 1:  ok = IMG_SavePNG(lbDrawSurface, path); break;
        case 2:  ok = SDL_SaveBMP(lbDrawSurface, path); break;
        default: return false;
    }
    if (!ok)
        ERRORLOG("Screenshot save failed (%s): %s", path, SDL_GetError());
    return ok;
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
