#include "pre_inc.h"
#include "kfx/renderer/RendererManager.h"
#include "kfx/renderer/RendererSoftware.h"
#ifndef _WIN32
// The GPU backend is Linux-only in this fork: bflib_render_gl.c is not in the
// Windows build's object list at all, so referring to RendererGL there would be
// an undefined reference rather than a backend that merely declines to start.
#include "kfx/renderer/RendererGL.h"
#endif
#include "bflib_basics.h"
#include "bflib_video.h"
#include "bflib_sprfnt.h"   // LbTextDrawResizedImmediate
#include "kfx/renderer/ITextRenderer.h"
#include "kfx/renderer/IUIRenderer.h"
#include "bflib_vidraw.h"   // LbSpriteDraw*Immediate
#include "gui_draw.h"       // draw_slab64k_background_immediate
#include "post_inc.h"

static IRenderer*   s_active_renderer = nullptr;
static RendererType s_active_type     = RENDERER_INVALID;
// What was asked for, as opposed to what AUTO resolved to. Kept so the backend
// can be rebuilt against a window that did not exist the first time round.
static RendererType s_requested_type  = RENDERER_AUTO;
static unsigned char s_draw_colour = 0;
static unsigned short s_draw_flags = 0;

static void push_active_palette(void);

// Allocate a backend for the requested type, or nullptr if unknown.
static IRenderer* create_renderer(RendererType type)
{
    switch (type)
    {
#ifndef _WIN32
        case RENDERER_OPENGL:   return new RendererGL();
#endif
        case RENDERER_SOFTWARE: return new RendererSoftware();
        default:                return nullptr;
    }
}

int RendererInit(RendererType type)
{
    if (s_active_renderer != nullptr)
        RendererShutdown();
    s_requested_type = type;

    // AUTO prefers the GPU backend and drops to software when it cannot come up:
    // no GL 3.3, or simply no window yet at the first call. Anything else is
    // taken literally, so forcing a backend stays a one-word change. Where the
    // GPU backend is not built, AUTO is software and nothing else -- it must not
    // fall through a type that has no factory case.
    static const RendererType auto_order[] = {
#ifndef _WIN32
        RENDERER_OPENGL,
#endif
        RENDERER_SOFTWARE,
    };
    const RendererType* order   = (type == RENDERER_AUTO) ? auto_order : &type;
    const int           n_order = (type == RENDERER_AUTO)
                                ? (int)(sizeof(auto_order) / sizeof(auto_order[0])) : 1;

    for (int i = 0; i < n_order; i++)
    {
        IRenderer* rend = create_renderer(order[i]);
        if (rend == nullptr)
        {
            ERRORLOG("Unknown renderer type %d", (int)order[i]);
            continue;
        }
        if (!rend->Init())
        {
            // Never silent once there is a window to judge against: a GPU backend
            // that quietly stopped being selected is a tenfold present cost that
            // nobody notices for a month. Before the window exists, though, a
            // backend that needs one is *expected* to decline -- that is the
            // bootstrap, not a failure -- and saying so makes an ordinary startup
            // read like an OpenGL fault in every log a user sends us. The backend
            // records the real reason at SYNCDBG(4) either way.
            if (type != RENDERER_AUTO)
                ERRORLOG("Renderer '%s' failed to initialise", rend->GetName());
            else if (lbWindow != NULL)
                SYNCLOG("Renderer backend '%s' unavailable; trying the next one", rend->GetName());
            delete rend;
            continue;
        }
        s_active_renderer = rend;
        s_active_type     = order[i];
        SYNCLOG("Renderer backend '%s' active", rend->GetName());
        // A backend starts with no palette of its own -- the GPU one's LUT texture
        // is blank -- while the engine's palette is usually unchanged across a
        // rebuild, so nothing else would ever re-send it. That is precisely the
        // black screen that used to follow a resolution change.
        push_active_palette();
        return 1;
    }
    ERRORLOG("No renderer backend could be initialised (requested %d)", (int)type);
    return 0;
}

void RendererReinit(void)
{
    RendererInit(s_requested_type);
}

void RendererShutdown(void)
{
    if (s_active_renderer == nullptr)
        return;
    s_active_renderer->Shutdown();
    delete s_active_renderer;
    s_active_renderer = nullptr;
    s_active_type     = RENDERER_INVALID;
}

RendererType RendererGetActiveType(void)
{
    return s_active_type;
}

const unsigned char* RendererGetActivePalette(void)
{
    return LbPaletteGetReadonly();
}

// Palette channels are stored 6-bit (0..63) - because of VGA constraint, convert to 8-bit (0..255) for display.
static inline unsigned char chan6_to_8(unsigned char v)
{
    return (unsigned char)((v * 255) / 63);
}

// Re-send the palette the engine already holds to whichever backend is active.
static void push_active_palette(void)
{
    const unsigned char* pal6 = LbPaletteGetReadonly();
    if (pal6 == NULL)
        return; // nothing stored yet; the first LbPaletteSet will supply it
    unsigned char rgb8[PALETTE_SIZE];
    for (int i = 0; i < PALETTE_SIZE; i++)
        rgb8[i] = chan6_to_8(pal6[i]);
    RendererSetDisplayPalette(rgb8);
}

TbResult RendererPaletteSet(unsigned char *palette)
{
    if (!lbScreenInitialised)
        return Lb_FAIL;
    TbResult ret = LbPaletteStore(palette);
    if (ret == Lb_SUCCESS)
    {
        const unsigned char* pal6 = LbPaletteGetReadonly();
        unsigned char rgb8[PALETTE_SIZE];
        for (int i = 0; i < PALETTE_SIZE; i++)
            rgb8[i] = chan6_to_8(pal6[i]);
        RendererSetDisplayPalette(rgb8);
    }
    return ret;
}

void RendererSetDisplayPalette(const unsigned char *rgb8)
{
    if (s_active_renderer != nullptr)
        s_active_renderer->SetDisplayPalette(rgb8);
}

void RendererClearScreen(unsigned char colour)
{
    if (s_active_renderer != nullptr)
        s_active_renderer->ClearScreen(colour);
}

void RendererPresentFrame(void)
{
    if (s_active_renderer != nullptr)
        s_active_renderer->PresentFrame();
}

TbResult RendererLockFramebuffer(void)
{
    if (!lbScreenInitialised || s_active_renderer == nullptr)
        return Lb_FAIL;
    int pitch = 0;
    unsigned char* px = s_active_renderer->LockFramebuffer(&pitch);
    if (px == nullptr)
    {
        lbDisplay.GraphicsWindowPtr = NULL;
        lbDisplay.WScreen = NULL;
        return Lb_FAIL;
    }
    lbDisplay.WScreen = px;
    lbDisplay.GraphicsScreenWidth = pitch;
    lbDisplay.GraphicsWindowPtr = &lbDisplay.WScreen[lbDisplay.GraphicsWindowX +
        lbDisplay.GraphicsScreenWidth * lbDisplay.GraphicsWindowY];
    return Lb_SUCCESS;
}

TbResult RendererUnlockFramebuffer(void)
{
    lbDisplay.WScreen = NULL;
    lbDisplay.GraphicsWindowPtr = NULL;
    if (s_active_renderer != nullptr)
        s_active_renderer->UnlockFramebuffer();
    return Lb_SUCCESS;
}

TbBool RendererScheduleScreenshot(const char* path, int fmt)
{
    return (s_active_renderer != nullptr) ? s_active_renderer->ScheduleScreenshot(path, fmt) : 0;
}

TbResult RendererSetupScreen(TbScreenMode mode, TbScreenCoord width, TbScreenCoord height,
    unsigned char *palette, short buffers_count, TbBool wscreen_vid)
{
    return LbScreenSetup(mode, width, height, palette, buffers_count, wscreen_vid);
}

TbResult RendererResetScreen(TbBool exiting_application)
{
    return LbScreenReset(exiting_application);
}

TbResult RendererScreenInitialize(void)
{
    return LbScreenInitialize();
}

TbResult RendererSetDoubleBuffering(TbBool state)
{
    return LbScreenSetDoubleBuffering(state);
}

TbBool RendererTextDrawResized(int posx, int posy, int units_per_px, const char *text)
{
    ITextRenderer* tr = (s_active_renderer != nullptr) ? s_active_renderer->GetTextRenderer() : nullptr;
    if (tr == nullptr)
        return LbTextDrawResizedImmediate(posx, posy, units_per_px, text);
    return tr->DrawTextResized(posx, posy, units_per_px, text);
}

static IUIRenderer* active_ui_renderer(void)
{
    return (s_active_renderer != nullptr) ? s_active_renderer->GetUIRenderer() : nullptr;
}

/* The ambient draw state is what the caller set before the call, so it travels with
 * the submission and is reapplied if the draw is replayed later. */
static KfxDrawState ambient_draw_state(void)
{
    return draw_state_make(RendererGetDrawFlags(), RendererGetDrawColour());
}

void RendererDrawSlabBackground(int32_t x, int32_t y, int32_t width, int32_t height)
{
    IUIRenderer* ui = active_ui_renderer();
    if (ui == nullptr) { draw_slab64k_background_immediate(x, y, width, height); return; }
    ui->SubmitSlabBackground((int32_t)x, (int32_t)y, (int32_t)width, (int32_t)height);
}

TbResult RendererDrawBox(int32_t x, int32_t y, uint32_t width, uint32_t height, unsigned char colour)
{
    IUIRenderer* ui = active_ui_renderer();
    if (ui == nullptr) return LbDrawBoxImmediate(x, y, width, height, colour);
    ui->SubmitSolidBox(x, y, (int32_t)width, (int32_t)height, colour, ambient_draw_state());
    return Lb_SUCCESS;
}

TbResult RendererSpriteDraw(int32_t x, int32_t y, const struct TbSprite *spr)
{
    IUIRenderer* ui = active_ui_renderer();
    if (ui == nullptr) return LbSpriteDrawImmediate(x, y, spr);
    return ui->SubmitRawSprite(x, y, spr, ambient_draw_state());
}

TbResult RendererSpriteDrawOneColour(int32_t x, int32_t y, const struct TbSprite *spr, unsigned char colour)
{
    IUIRenderer* ui = active_ui_renderer();
    if (ui == nullptr) return LbSpriteDrawOneColourImmediate(x, y, spr, colour);
    return ui->SubmitRawSpriteOneColour(x, y, spr, colour, ambient_draw_state());
}

TbResult RendererSpriteDrawScaled(int32_t x, int32_t y, const struct TbSprite *spr, int32_t w, int32_t h)
{
    IUIRenderer* ui = active_ui_renderer();
    if (ui == nullptr) return LbSpriteDrawScaledImmediate(x, y, spr, w, h);
    return ui->SubmitRawSpriteScaled(x, y, spr, w, h, ambient_draw_state());
}

TbResult RendererSpriteDrawScaledOneColour(int32_t x, int32_t y, const struct TbSprite *spr, int32_t w, int32_t h, unsigned char colour)
{
    IUIRenderer* ui = active_ui_renderer();
    if (ui == nullptr) return LbSpriteDrawScaledOneColourImmediate(x, y, spr, w, h, colour);
    return ui->SubmitRawSpriteScaledOneColour(x, y, spr, w, h, colour, ambient_draw_state());
}

int RendererSpriteDrawScaledRemap(int32_t x, int32_t y, const struct TbSprite *spr, int32_t w, int32_t h, const unsigned char *cmap)
{
    IUIRenderer* ui = active_ui_renderer();
    if (ui == nullptr) return LbSpriteDrawScaledRemapImmediate(x, y, spr, w, h, cmap);
    return ui->SubmitRawSpriteScaledRemap(x, y, spr, w, h, cmap, ambient_draw_state());
}

unsigned char RendererGetDrawColour(void) { return s_draw_colour; }
void RendererSetDrawColour(unsigned char colour) { s_draw_colour = colour; }

unsigned short RendererGetDrawFlags(void) { return s_draw_flags; }
void RendererSetDrawFlags(unsigned short flags) { s_draw_flags = flags; }
void RendererAddDrawFlags(unsigned short flags) { s_draw_flags |= flags; }
void RendererClearDrawFlags(unsigned short flags) { s_draw_flags &= ~flags; }
void RendererToggleDrawFlags(unsigned short flags) { s_draw_flags ^= flags; }

TbResult RendererPaletteGet(unsigned char *palette)
{
    return LbPaletteGet(palette);
}
