/******************************************************************************/
// Bullfrog Engine Emulation Library - for use to remake classic games like
// Syndicate Wars, Magic Carpet or Dungeon Keeper.
/******************************************************************************/
/** @file bflib_render_glworld.c
 *     GPU world renderer — "Seam B" scaffolding.
 * @par Purpose:
 *     Task 1: module skeleton with offscreen RGBA16F scene FBO, lifecycle
 *     hooks, and a debug readback/dump tool. No geometry is submitted yet;
 *     the game continues to render entirely via the existing software path.
 * @par Comment:
 *     All GL code is fenced #ifndef _WIN32. The module reuses the GL context
 *     that is already current after gl_present_init(); it does NOT create a
 *     new context or window.
 *
 *     Patterns (error-checking, logging) are modelled on bflib_render_gl.c.
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "bflib_render_glworld.h"

#include "bflib_basics.h"

#ifndef _WIN32
#include <epoxy/gl.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <spng.h>
#include <dirent.h>
#include "engine_textures.h"
#include "bflib_video.h"
#include "vidmode.h"
#endif
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/

/** True when the GL world module successfully initialised. */
TbBool gl_world_active = false;
/** True when the hi-res terrain override store is active and >=1 tile loaded. */
TbBool gl_hires_active = false;
/** True when the hi-res sprite override store is active and >=1 frame loaded. */
TbBool gl_hires_sprites_active = false;

#ifndef _WIN32

/* ----------------------------------------------------------------------- */
/* Internal state                                                           */
/* ----------------------------------------------------------------------- */

/* Terrain tile geometry: 32x32 paletted tiles. There are tens of thousands of
 * block ids — far more than GL_MAX_ARRAY_TEXTURE_LAYERS — so the tiles are
 * packed into a single 2D R8UI atlas (gw.atlas_cols tiles per row) and the
 * shader computes each tile's atlas origin from its integer id. */
#define GLW_TILE_DIM   32
#define GLW_TILE_ROW   256   /* block_mem row stride within a tile (32*8). */
#define GLW_TILE_COUNT (TEXTURE_VARIATIONS_COUNT * TEXTURE_BLOCKS_COUNT)
/* The atlas packs GLW_TILE_COUNT (~49408) 32x32 tiles into one 2D R8UI texture.
 * The number of tiles per row (and hence the atlas pixel dimensions) is chosen
 * at runtime from GL_MAX_TEXTURE_SIZE: a fixed 128-column layout produces a
 * 4096x12352 atlas whose height (12352) exceeds the 8192 GL_MAX_TEXTURE_SIZE
 * reported by many drivers, so glTexImage2D fails with GL_INVALID_VALUE during
 * setup and the GL world renderer aborts. See glw_choose_atlas_layout(). */
#define GLW_FADE_LEVELS 64   /* rows in pixmap.fade_tables (64*256). */
#define GLW_BATCH_MAX  (1u << 16) /* max vertices per world frame batch. */

/* --- Sprite (billboard) store --------------------------------------------
 * Each distinct KeeperSprite frame is RLE-decoded once into a cell of a 2D
 * atlas as two channels: the raw palette index (R) and a coverage byte (G,
 * 0=transparent / 255=opaque). The atlas is RG8. Cells are a fixed maximum
 * size; frames larger than a cell are clamped (KeeperSprite frames are small).
 */
#define GLW_SPR_CELL    256   /* max sprite frame dimension per atlas cell. */
#define GLW_SPR_COLS    16    /* cells per atlas row. */
#define GLW_SPR_ROWS_INIT 16  /* initial cell rows (256 slots). */
/* The atlas grows by whole rows when a frame needs more live slots than the
 * current capacity (see glw_sprite_slot). Capacity is capped at a generous
 * GLW_SPR_ROWS_MAX rows so a pathological frame can't grow the texture without
 * bound; beyond the cap the sprite is skipped (left to the software/composite
 * path) rather than evicting a slot a pending command still references. The cap
 * keeps the atlas height (rows*256) within the 8192 GL_MAX_TEXTURE_SIZE that
 * the terrain layout already assumes. */
#define GLW_SPR_ROWS_MAX 32   /* max cell rows (1024 slots, 8192px tall). */
#define GLW_REMAP_MAX   64    /* distinct remap LUTs cached per frame. */
#define GLW_CMD_MAX     (1u << 14) /* max draw commands per world frame. */

/** All persistent GL handles for the world module. */
static struct {
    TbBool inited;      /**< Whether glworld_init has succeeded. */
    GLuint scene_fbo;   /**< Offscreen RGBA16F framebuffer object. */
    GLuint scene_tex;   /**< RGBA16F colour texture attached to scene_fbo. */
    int    world_w;     /**< Width of the scene FBO, in pixels. */
    int    world_h;     /**< Height of the scene FBO, in pixels. */

    /* Terrain pipeline. */
    TbBool terrain_ready;   /**< Whether shaders+buffers were built. */
    GLuint program;         /**< Triangle shader program. */
    GLuint vao;             /**< Vertex array object. */
    GLuint vbo;             /**< Streaming vertex buffer. */
    GLuint tile_tex;        /**< R8UI 2D array: raw palette indices per tile. */
    GLuint fade_tex;        /**< R8UI 2D (256 x 64): fade_tables LUT. */
    GLuint pal_tex;         /**< RGB8 1D-as-2D (256 x 1): lbPaletteColors. */
    GLint  u_winsize;       /**< Uniform: engine-window (w,h) in local pixels. */
    GLint  u_tiles;         /**< Sampler: tile atlas. */
    GLint  u_fade;          /**< Sampler: fade table. */
    GLint  u_pal;           /**< Sampler: palette. */
    GLint  u_atlascols;     /**< Uniform: tiles per atlas row. */
    GLint  u_tiledim;       /**< Uniform: tile dimension (32). */
    /* Hi-res override (Task 3). */
    GLuint shade_tex;       /**< RGB32F 64x1: per-shade brightness multiplier. */
    GLint  u_ov_array;      /**< Sampler: hi-res RGBA8 2D_ARRAY. */
    GLint  u_ov_lookup;     /**< Sampler: R16UI block_id->layer lookup. */
    GLint  u_shade;         /**< Sampler: shade LUT. */
    GLint  u_ov_cols;       /**< int: lookup texture width (0 = no overrides). */
    int    atlas_cols;      /**< Tiles per atlas row, chosen at runtime. */
    int    atlas_rows;      /**< Tile rows in atlas, chosen at runtime. */
    int    atlas_w;         /**< Atlas width in pixels (atlas_cols*32). */
    int    atlas_h;         /**< Atlas height in pixels (rows*32). */

    /* Per-frame CPU batch. Each vertex carries a layer (tile id). */
    float    *batch;        /**< 6 floats per vertex: sx,sy,u,v,shade,layer. */
    unsigned  batch_count;  /**< Vertices accumulated this frame. */
    int       win_w_local;  /**< Engine window width in local (scaled) pixels. */
    int       win_h_local;  /**< Engine window height in local (scaled) pixels. */
    TbBool    texstore_dirty; /**< When set, the next sync re-uploads tiles. */

    /* Engine-window rectangle in full-resolution scene-FBO pixels (GL
     * bottom-left origin), captured each glworld_begin_frame. Used by the
     * present path to composite the world into the engine sub-window only. */
    int       rect_x;       /**< Left edge, full-res pixels. */
    int       rect_y;       /**< Bottom edge, full-res pixels (GL origin). */
    int       rect_w;       /**< Width, full-res pixels. */
    int       rect_h;       /**< Height, full-res pixels. */

    /* --- Sprite + line pipeline (Task 4) --- */
    TbBool    sprite_ready;     /**< sprite/line GL objects built. */
    GLuint    spr_program;      /**< sprite shader (atlas+remap+palette). */
    GLuint    spr_vao;          /**< sprite quad VAO. */
    GLuint    spr_vbo;          /**< sprite quad streaming VBO. */
    GLuint    spr_atlas_tex;    /**< RG8 atlas: R=index, G=coverage. */
    GLuint    remap_tex;        /**< R8 2D (256 x GLW_REMAP_MAX): remap LUTs. */
    GLint     su_winsize;       /**< sprite uniform: window (w,h) local px. */
    GLint     su_pal;           /**< sprite sampler: palette LUT. */
    GLint     su_atlas;         /**< sprite sampler: sprite atlas. */
    GLint     su_remap;         /**< sprite sampler: remap LUT array. */
    GLint     su_alpha;         /**< sprite uniform: constant alpha. */
    GLint     su_sprov;         /**< sprite sampler: hi-res override 2D array. */
    GLint     su_shade_spr;     /**< sprite sampler: shade LUT (hi-res branch). */

    GLuint    line_program;     /**< line shader. */
    GLuint    line_vao;         /**< line VAO. */
    GLuint    line_vbo;         /**< line streaming VBO. */
    GLint     lu_winsize;       /**< line uniform: window (w,h) local px. */
    GLint     lu_pal;           /**< line sampler: palette LUT. */

    /* Per-frame draw command list (TRI runs share the terrain batch; sprite
     * and line commands carry their own geometry). Kept in submission order. */
    void     *cmds;             /**< struct GlwCmd array (opaque to header). */
    unsigned  cmd_count;        /**< commands appended this frame. */
    TbBool    frame_open;       /**< true between begin_frame and end_frame. */

    /* Sprite/line vertex scratch, flushed per command. */
    float    *spr_verts;        /**< 6 verts * GLW_SPR_VFLOATS floats. */
    float    *line_verts;       /**< 2 verts * GLW_LINE_VFLOATS floats. */
} gw;

#define GLW_VERT_FLOATS 6      /* terrain vertex: sx,sy,u,v,shade,layer. */
#define GLW_SPR_VFLOATS 8      /* sprite vertex: sx,sy,u,v,remapRow,ovLayer,hi_u,hi_v. */
#define GLW_LINE_VFLOATS 3     /* line vertex: sx,sy,palIndex. */

/* --- Sprite frame cache ---------------------------------------------------
 * Decoded frames live in atlas slots, keyed by frame_key (kspr_idx). A small
 * open-addressed cache maps key -> slot. Frames are decoded once; remap LUTs
 * are cached by pointer in a separate small table (row in remap_tex). */
struct GlwSprSlot {
    TbBool   used;
    uint32_t key;       /**< frame_key (kspr_idx). */
    int      w;         /**< decoded frame width (<= GLW_SPR_CELL). */
    int      h;         /**< decoded frame height (<= GLW_SPR_CELL). */
    uint32_t epoch;     /**< frame epoch this slot was last referenced. */
    unsigned char *ic;  /**< CPU shadow of decoded index+coverage (w*h*2). */
};
/* Slots grow with the atlas. glw_spr_count is the number of live cells; the
 * atlas texture holds glw_spr_rows*GLW_SPR_COLS cells. */
static struct GlwSprSlot *glw_spr_slots = NULL;
static int glw_spr_rows = 0;        /**< current cell rows in the atlas. */
static int glw_spr_count = 0;       /**< slots in use (==rows*COLS once full). */
static int glw_spr_rows_max = 0;    /**< runtime row cap (clamped to GL_MAX_TEXTURE_SIZE / GLW_SPR_CELL). */
static uint32_t glw_frame_epoch = 0;/**< incremented each begin_frame. */

struct GlwRemapEnt {
    TbBool used;
    const unsigned char *ptr;   /**< pointer identity of the remap table. */
};
static struct GlwRemapEnt glw_remap_tab[GLW_REMAP_MAX];

/* Terrain atlas staging buffer. Allocated lazily on the first dirty sync and
 * reused; freed in glworld_shutdown so a re-init re-allocates cleanly. */
static unsigned char *glw_atlasbuf = NULL;
static unsigned glw_remap_next = 1; /**< row 0 reserved for identity remap. */


/* Tagged draw command kinds. */
enum GlwCmdKind { GLW_CMD_TRI = 0, GLW_CMD_SPRITE, GLW_CMD_LINE };

struct GlwCmd {
    unsigned char kind;
    /* TRI: a run of terrain vertices [tri_first, tri_first+tri_count). */
    unsigned tri_first;
    unsigned tri_count;
    /* SPRITE: atlas slot + screen quad + remap row + blend. */
    int   slot;
    float dx, dy, dw, dh;
    int   remap_row;
    unsigned char flip_x;
    unsigned char blend;
    float ov_layer; /**< hi-res override layer index (>= 0.0) or -1.0 (none). */
    /* LINE: endpoints + palette colour. */
    float x0, y0, x1, y1;
    unsigned char color;
};

/* ----------------------------------------------------------------------- */
/* Helpers                                                                  */
/* ----------------------------------------------------------------------- */

static void glw_flush_commands(void);

/** Drain and log any pending GL errors. Returns true if an error was seen. */
static TbBool glworld_check_error(const char *where)
{
    TbBool had_error = false;
    GLenum err;
    while ((err = glGetError()) != GL_NO_ERROR) {
        LbErrorLog("glworld: GL error 0x%04x at %s\n",
            (unsigned int)err, where);
        had_error = true;
    }
    return had_error;
}

/* ----------------------------------------------------------------------- */
/* Hi-res terrain override store (Task 2)                                   */
/* ----------------------------------------------------------------------- */

static struct {
    GLuint ov_array;      /* GL_TEXTURE_2D_ARRAY RGBA8, GLW_HIRES_DIM^2 x count */
    GLuint ov_lookup;     /* R16UI, atlas_cols x atlas_rows: block_id -> layer */
    int    count;
} gw_hires;

/* Decode one PNG file to a freshly malloc'd RGBA8 buffer of GLW_HIRES_DIM^2.
 * Returns NULL on any failure or a size mismatch. Caller frees. */
static unsigned char *glw_hires_decode_png(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n <= 0) { fclose(f); return NULL; }
    unsigned char *file = (unsigned char *)malloc((size_t)n);
    if (!file) { fclose(f); return NULL; }
    if (fread(file, 1, (size_t)n, f) != (size_t)n) { free(file); fclose(f); return NULL; }
    fclose(f);
    spng_ctx *ctx = spng_ctx_new(0);
    if (!ctx) { free(file); return NULL; }
    spng_set_png_buffer(ctx, file, (size_t)n);
    struct spng_ihdr ihdr;
    if (spng_get_ihdr(ctx, &ihdr) != 0 ||
        ihdr.width != GLW_HIRES_DIM || ihdr.height != GLW_HIRES_DIM) {
        spng_ctx_free(ctx); free(file); return NULL;
    }
    size_t out_size = 0;
    spng_decoded_image_size(ctx, SPNG_FMT_RGBA8, &out_size);
    unsigned char *rgba = (unsigned char *)malloc(out_size);
    if (!rgba) { spng_ctx_free(ctx); free(file); return NULL; }
    int r = spng_decode_image(ctx, rgba, out_size, SPNG_FMT_RGBA8, 0);
    spng_ctx_free(ctx); free(file);
    if (r != 0) { free(rgba); return NULL; }
    return rgba;
}

/* Parse "tmap_<var>_<blockid>.png" into the GPU tile/layer id the terrain
 * shader indexes uOvLookup by, or -1 if it doesn't match.  The engine forms
 * this id in engine_remap_texture_blocks() (engine_render.c:4029) as
 *     tex_id + (variation & 0x1F) * TEXTURE_BLOCKS_COUNT
 * so the override must be keyed at the identical linear index, otherwise
 * var>0 tiles would silently mis-override variation-0 blocks.  For var==0 the
 * index collapses to bid, preserving the original proof behaviour exactly. */
static int glw_hires_parse_id(const char *name)
{
    int var, bid;
    if (sscanf(name, "tmap_%d_%d.png", &var, &bid) != 2) return -1;
    if (var < 0 || bid < 0) return -1;
    return bid + (var & 0x1F) * TEXTURE_BLOCKS_COUNT;
}

/* Forward declaration — defined later in this file, after glw_terrain_ensure. */
static TbBool glw_choose_atlas_layout(void);

void glworld_hires_shutdown(void)
{
    if (gw_hires.ov_array)  { glDeleteTextures(1, &gw_hires.ov_array);  gw_hires.ov_array = 0; }
    if (gw_hires.ov_lookup) { glDeleteTextures(1, &gw_hires.ov_lookup); gw_hires.ov_lookup = 0; }
    gw_hires.count = 0;
    gl_hires_active = false;
}

void glworld_hires_load(const char *dir)
{
    glworld_hires_shutdown();
    if (!gw.inited || dir == NULL || dir[0] == '\0') return;

    /* glworld_init defers tile-atlas layout to the first frame (glw_terrain_ensure);
     * choose it now so atlas_cols/atlas_rows are valid for the lookup texture. */
    if (gw.atlas_cols == 0 || gw.atlas_rows == 0) {
        if (!glw_choose_atlas_layout()) return;
    }

    /* Collect matching files (linear tile id + path) on the heap.  A full
     * static tileset is TEXTURE_BLOCKS_STAT_COUNT_A (544) blocks per variation,
     * far more than any fixed stack buffer should hold, so the collection grows
     * dynamically with no arbitrary cap (Findings 2/3). */
    struct glw_hires_file { int id; char path[512]; };
    struct glw_hires_file *files = NULL;
    size_t found = 0, cap = 0;
    DIR *d = opendir(dir);
    if (!d) { LbErrorLog("glworld_hires_load: cannot open dir '%s'\n", dir); return; }
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        int idx = glw_hires_parse_id(de->d_name);
        if (idx < 0) continue;
        if (found == cap) {
            size_t ncap = cap ? cap * 2 : 64;
            struct glw_hires_file *nf = (struct glw_hires_file *)realloc(files, ncap * sizeof(*files));
            if (!nf) {
                LbErrorLog("glworld_hires_load: out of memory collecting files (kept %zu)\n", found);
                break;
            }
            files = nf; cap = ncap;
        }
        files[found].id = idx;
        snprintf(files[found].path, sizeof(files[found].path), "%s/%s", dir, de->d_name);
        found++;
    }
    closedir(d);
    if (found == 0) {
        free(files);
        LbSyncLog("glworld_hires_load: no matching tiles in '%s'\n", dir);
        return;
    }

    /* Clamp to runtime GL limits (array layers + texture size). */
    GLint maxlayers = 0, maxsz = 0;
    glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &maxlayers);
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxsz);
    if (maxsz < GLW_HIRES_DIM) {
        free(files);
        LbErrorLog("glworld_hires_load: GL_MAX_TEXTURE_SIZE %d < %d\n", (int)maxsz, GLW_HIRES_DIM);
        return;
    }
    if (maxlayers > 0 && found > (size_t)maxlayers) {
        LbWarnLog("glworld_hires_load: collected %zu tile(s) but GL_MAX_ARRAY_TEXTURE_LAYERS=%d; dropping %zu\n",
                  found, (int)maxlayers, found - (size_t)maxlayers);
        found = (size_t)maxlayers;
    }

    /* Build the array texture. */
    glGenTextures(1, &gw_hires.ov_array);
    glBindTexture(GL_TEXTURE_2D_ARRAY, gw_hires.ov_array);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, GLW_HIRES_DIM, GLW_HIRES_DIM, (GLsizei)found,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

    /* CPU lookup, sized like the tile atlas; default = NONE. */
    size_t lutw = (size_t)gw.atlas_cols;
    size_t luth = (size_t)gw.atlas_rows;
    uint16_t *lut = (uint16_t *)malloc(lutw * luth * sizeof(uint16_t));
    if (!lut) {
        glDeleteTextures(1, &gw_hires.ov_array); gw_hires.ov_array = 0;
        free(files);
        LbErrorLog("glworld_hires_load: out of memory for lookup table\n");
        return;
    }
    for (size_t i = 0; i < lutw * luth; i++) lut[i] = (uint16_t)GLW_HIRES_NONE;

    int layer = 0;
    for (size_t i = 0; i < found; i++) {
        unsigned char *rgba = glw_hires_decode_png(files[i].path);
        if (!rgba) { LbErrorLog("glworld_hires_load: failed to decode '%s'\n", files[i].path); continue; }
        glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, layer, GLW_HIRES_DIM, GLW_HIRES_DIM, 1,
                        GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        free(rgba);
        /* Key the override at the engine's linear tile id
         * (var*TEXTURE_BLOCKS_COUNT + block_id); var==0 -> idx==block_id. */
        int idx = files[i].id;
        if (idx >= 0 && (size_t)idx < lutw * luth) lut[idx] = (uint16_t)layer;
        layer++;
    }
    free(files);
    gw_hires.count = layer;

    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    if (layer > 0) glGenerateMipmap(GL_TEXTURE_2D_ARRAY);

    /* Upload the lookup as R16UI. */
    glGenTextures(1, &gw_hires.ov_lookup);
    glBindTexture(GL_TEXTURE_2D, gw_hires.ov_lookup);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R16UI, (GLsizei)lutw, (GLsizei)luth, 0,
                 GL_RED_INTEGER, GL_UNSIGNED_SHORT, lut);
    LbSyncLog("glworld: hires lookup %dx%d, %d override tile(s)\n",
              (int)lutw, (int)luth, layer);
    free(lut);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    glBindTexture(GL_TEXTURE_2D, 0);

    gl_hires_active = (layer > 0);
    LbSyncLog("glworld_hires_load: loaded %d tile(s) from '%s' (gl_hires_active=%d)\n",
        layer, dir, (int)gl_hires_active);
    (void)glworld_check_error("glworld_hires_load");
}

GLuint glworld_hires_array(void)      { return gw_hires.ov_array; }
GLuint glworld_hires_lookup_tex(void) { return gw_hires.ov_lookup; }
int    glworld_hires_count(void)      { return gw_hires.count; }

/* ----------------------------------------------------------------------- */
/* Hi-res sprite override store (Sub-project 3, Task 2)                    */
/* ----------------------------------------------------------------------- */

/* Hash entry for the frame_key -> layer open-addressed map.
 * UINT32_MAX is the empty-slot sentinel (frame keys are kspr_idx values,
 * small non-negative integers; UINT32_MAX is never a valid key). */
struct glw_hspr_entry { uint32_t key; int layer; };

static struct {
    GLuint ov_array;            /* GL_TEXTURE_2D_ARRAY RGBA8, GLW_HIRES_SPR_DIM^2 x count */
    struct glw_hspr_entry *map; /* open-addressed hash, cap = next-pow2 >= 2*count */
    int map_cap;
    int count;
} gw_hspr;

/* Nearest-neighbour scale of an RGBA8 buffer src (src_w x src_h) to a fresh
 * dst_dim x dst_dim RGBA8 buffer.  Caller frees the return value. */
static unsigned char *glw_hspr_resize_nn(const unsigned char *src,
    unsigned src_w, unsigned src_h, unsigned dst_dim)
{
    unsigned char *dst = (unsigned char *)malloc((size_t)dst_dim * dst_dim * 4u);
    if (!dst) return NULL;
    for (unsigned dy = 0; dy < dst_dim; dy++) {
        unsigned sy = (unsigned)((uint64_t)dy * src_h / dst_dim);
        if (sy >= src_h) sy = src_h - 1u;
        for (unsigned dx = 0; dx < dst_dim; dx++) {
            unsigned sx = (unsigned)((uint64_t)dx * src_w / dst_dim);
            if (sx >= src_w) sx = src_w - 1u;
            const unsigned char *s = src + ((size_t)sy * src_w + sx) * 4u;
            unsigned char *d = dst + ((size_t)dy * dst_dim + dx) * 4u;
            d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = s[3];
        }
    }
    return dst;
}

/* Decode one sprite PNG file to a freshly malloc'd RGBA8 buffer of
 * GLW_HIRES_SPR_DIM x GLW_HIRES_SPR_DIM pixels.
 * The PNG may be any WxH; it is nearest-neighbour scaled to the square so
 * UVs map 0..1 directly to the array layer (no per-layer uv-rect needed).
 * Returns NULL on any failure.  Caller frees. */
static unsigned char *glw_hspr_decode_png(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n <= 0) { fclose(f); return NULL; }
    unsigned char *file = (unsigned char *)malloc((size_t)n);
    if (!file) { fclose(f); return NULL; }
    if (fread(file, 1, (size_t)n, f) != (size_t)n) { free(file); fclose(f); return NULL; }
    fclose(f);
    spng_ctx *ctx = spng_ctx_new(0);
    if (!ctx) { free(file); return NULL; }
    spng_set_png_buffer(ctx, file, (size_t)n);
    struct spng_ihdr ihdr;
    if (spng_get_ihdr(ctx, &ihdr) != 0) {
        spng_ctx_free(ctx); free(file); return NULL;
    }
    unsigned png_w = ihdr.width, png_h = ihdr.height;
    size_t out_size = 0;
    spng_decoded_image_size(ctx, SPNG_FMT_RGBA8, &out_size);
    unsigned char *rgba = (unsigned char *)malloc(out_size);
    if (!rgba) { spng_ctx_free(ctx); free(file); return NULL; }
    int r = spng_decode_image(ctx, rgba, out_size, SPNG_FMT_RGBA8, 0);
    spng_ctx_free(ctx); free(file);
    if (r != 0) { free(rgba); return NULL; }
    /* Already the right size — return directly. */
    if (png_w == GLW_HIRES_SPR_DIM && png_h == GLW_HIRES_SPR_DIM) return rgba;
    /* Scale to square layer. */
    unsigned char *scaled = glw_hspr_resize_nn(rgba, png_w, png_h, GLW_HIRES_SPR_DIM);
    free(rgba);
    return scaled;
}

/* Open-addressed hash helpers (linear probe, Knuth multiplicative hash).
 * Map capacity must be a power of two. */
static int glw_hspr_map_lookup(uint32_t key)
{
    if (gw_hspr.map_cap == 0) return -1;
    int cap = gw_hspr.map_cap;
    uint32_t h = (key * 2654435761u) & (uint32_t)(cap - 1);
    for (int i = 0; i < cap; i++) {
        int slot = (int)((h + (uint32_t)i) & (uint32_t)(cap - 1));
        if (gw_hspr.map[slot].key == UINT32_MAX) return -1;
        if (gw_hspr.map[slot].key == key) return gw_hspr.map[slot].layer;
    }
    return -1;
}

static void glw_hspr_map_insert(uint32_t key, int layer)
{
    if (gw_hspr.map_cap == 0) return;
    int cap = gw_hspr.map_cap;
    uint32_t h = (key * 2654435761u) & (uint32_t)(cap - 1);
    for (int i = 0; i < cap; i++) {
        int slot = (int)((h + (uint32_t)i) & (uint32_t)(cap - 1));
        if (gw_hspr.map[slot].key == UINT32_MAX || gw_hspr.map[slot].key == key) {
            gw_hspr.map[slot].key   = key;
            gw_hspr.map[slot].layer = layer;
            return;
        }
    }
}

void glworld_hires_sprites_shutdown(void)
{
    if (gw_hspr.ov_array) { glDeleteTextures(1, &gw_hspr.ov_array); gw_hspr.ov_array = 0; }
    free(gw_hspr.map); gw_hspr.map = NULL;
    gw_hspr.map_cap = 0;
    gw_hspr.count   = 0;
    gl_hires_sprites_active = false;
}

void glworld_hires_sprites_load(const char *dir)
{
    glworld_hires_sprites_shutdown();
    if (!gw.inited || dir == NULL || dir[0] == '\0') return;

    /* Heap-collect all sprite_<key>.png files in dir (dynamic, no fixed cap). */
    struct glw_hspr_file { uint32_t key; char path[512]; };
    struct glw_hspr_file *files = NULL;
    size_t found = 0, cap = 0;
    DIR *d = opendir(dir);
    if (!d) { LbErrorLog("glworld_hires_sprites_load: cannot open dir '%s'\n", dir); return; }
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        unsigned int key_u;
        if (sscanf(de->d_name, "sprite_%u.png", &key_u) != 1) continue;
        if (key_u == UINT32_MAX) {
            LbWarnLog("glworld_hires_sprites_load: skipping '%s' (key 4294967295 is the reserved empty sentinel)\n", de->d_name);
            continue;
        }
        if (found == cap) {
            size_t ncap = cap ? cap * 2u : 64u;
            struct glw_hspr_file *nf = (struct glw_hspr_file *)realloc(files, ncap * sizeof(*files));
            if (!nf) {
                LbErrorLog("glworld_hires_sprites_load: out of memory collecting files (kept %zu)\n", found);
                break;
            }
            files = nf; cap = ncap;
        }
        files[found].key = (uint32_t)key_u;
        snprintf(files[found].path, sizeof(files[found].path), "%s/%s", dir, de->d_name);
        found++;
    }
    closedir(d);
    if (found == 0) {
        free(files);
        LbSyncLog("glworld_hires_sprites_load: no matching sprites in '%s'\n", dir);
        return;
    }

    /* Clamp to runtime GL limits. */
    GLint maxlayers = 0;
    glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &maxlayers);
    if (maxlayers > 0 && found > (size_t)maxlayers) {
        LbWarnLog("glworld_hires_sprites_load: collected %zu sprite(s) but GL_MAX_ARRAY_TEXTURE_LAYERS=%d; dropping %zu\n",
                  found, (int)maxlayers, found - (size_t)maxlayers);
        found = (size_t)maxlayers;
    }

    /* Build GL_TEXTURE_2D_ARRAY: GLW_HIRES_SPR_DIM x GLW_HIRES_SPR_DIM x N layers, RGBA8.
     * Each sprite PNG is decoded at its native size then nearest-neighbour scaled to
     * the square layer so UVs map 0..1 directly (no per-layer uv-rect needed). */
    glGenTextures(1, &gw_hspr.ov_array);
    glBindTexture(GL_TEXTURE_2D_ARRAY, gw_hspr.ov_array);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8,
                 GLW_HIRES_SPR_DIM, GLW_HIRES_SPR_DIM, (GLsizei)found,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    /* Allocate open-addressed hash table: next power-of-2 >= 2*found (load <= 0.5). */
    int map_cap = 1;
    while (map_cap < (int)(2u * found)) map_cap <<= 1;
    gw_hspr.map = (struct glw_hspr_entry *)malloc((size_t)map_cap * sizeof(*gw_hspr.map));
    if (!gw_hspr.map) {
        glDeleteTextures(1, &gw_hspr.ov_array); gw_hspr.ov_array = 0;
        free(files);
        LbErrorLog("glworld_hires_sprites_load: out of memory for lookup map\n");
        return;
    }
    gw_hspr.map_cap = map_cap;
    for (int i = 0; i < map_cap; i++) {
        gw_hspr.map[i].key   = UINT32_MAX; /* empty sentinel */
        gw_hspr.map[i].layer = -1;
    }

    int layer = 0;
    for (size_t i = 0; i < found; i++) {
        unsigned char *rgba = glw_hspr_decode_png(files[i].path);
        if (!rgba) { LbErrorLog("glworld_hires_sprites_load: failed to decode '%s'\n", files[i].path); continue; }
        glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, layer,
                        GLW_HIRES_SPR_DIM, GLW_HIRES_SPR_DIM, 1,
                        GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        free(rgba);
        glw_hspr_map_insert(files[i].key, layer);
        layer++;
    }
    free(files);
    gw_hspr.count = layer;

    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);

    gl_hires_sprites_active = (layer > 0);
    LbSyncLog("glworld_hires_sprites_load: loaded %d sprite override(s) from '%s' (gl_hires_sprites_active=%d)\n",
              layer, dir, (int)gl_hires_sprites_active);
    (void)glworld_check_error("hires_sprites_load");
}

GLuint glworld_hires_sprites_array(void) { return gw_hspr.ov_array; }
int    glworld_hires_sprites_layer(uint32_t frame_key) { return glw_hspr_map_lookup(frame_key); }

/* ----------------------------------------------------------------------- */
/* Public API                                                               */
/* ----------------------------------------------------------------------- */

TbBool glworld_init(int world_w, int world_h)
{
    if (gw.inited) {
        glworld_shutdown();
    }
    memset(&gw, 0, sizeof(gw));

    if ((world_w <= 0) || (world_h <= 0)) {
        LbErrorLog("glworld: invalid init dimensions %dx%d\n",
            world_w, world_h);
        return false;
    }

    /* Log the GL version line so the smoke test can grep for it. */
    {
        const GLubyte *ver = glGetString(GL_VERSION);
        const GLubyte *ren = glGetString(GL_RENDERER);
        LbSyncLog("glworld: init ok %dx%d RGBA16F (GL %s, renderer: %s)\n",
            world_w, world_h,
            (ver != NULL) ? (const char *)ver : "?",
            (ren != NULL) ? (const char *)ren : "?");
    }

    /* Create the RGBA16F scene texture. */
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F,
        world_w, world_h, 0,
        GL_RGBA, GL_FLOAT, NULL);
    glBindTexture(GL_TEXTURE_2D, 0);

    if (glworld_check_error("glworld_init: texture alloc")) {
        glDeleteTextures(1, &tex);
        LbErrorLog("glworld: scene texture creation failed\n");
        return false;
    }

    /* Create the FBO and attach the scene texture. */
    GLuint fbo = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
        GL_TEXTURE_2D, tex, 0);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    if (status != GL_FRAMEBUFFER_COMPLETE) {
        LbErrorLog("glworld: scene FBO incomplete (0x%04x) at %dx%d\n",
            (unsigned int)status, world_w, world_h);
        glDeleteFramebuffers(1, &fbo);
        glDeleteTextures(1, &tex);
        return false;
    }

    if (glworld_check_error("glworld_init: FBO setup")) {
        glDeleteFramebuffers(1, &fbo);
        glDeleteTextures(1, &tex);
        LbErrorLog("glworld: scene FBO setup reported GL errors\n");
        return false;
    }

    gw.scene_fbo = fbo;
    gw.scene_tex = tex;
    gw.world_w   = world_w;
    gw.world_h   = world_h;
    gw.inited    = true;
    return true;
}

void glworld_begin_frame(int win_x, int win_y, int win_w, int win_h)
{
    if (!gw.inited) {
        return;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, gw.scene_fbo);

    /* The engine window rect is given in local (pixel_size-scaled) coordinates
     * — the same space PolyPoint.X/Y live in. The scene FBO is full
     * resolution, so the viewport is scaled up by pixel_size. */
    int ps = (pixel_size > 0) ? (int)pixel_size : 1;
    int fb_x = win_x * ps;
    int fb_w = win_w * ps;
    int fb_h = win_h * ps;
    /* Convert from top-left screen origin to GL's bottom-left origin. */
    int gl_y = gw.world_h - (win_y * ps) - fb_h;
    glViewport(fb_x, gl_y, fb_w, fb_h);

    /* Record the rect (full-res, GL bottom-left origin) so the present path
     * can composite the world into exactly the engine sub-window. */
    gw.rect_x = fb_x;
    gw.rect_y = gl_y;
    gw.rect_w = fb_w;
    gw.rect_h = fb_h;

    gw.win_w_local = win_w;
    gw.win_h_local = win_h;
    gw.batch_count = 0;
    gw.cmd_count = 0;
    gw.frame_open = true;
    /* New epoch: any slot whose epoch predates this frame is not referenced by
     * a pending command and is therefore safe to evict. */
    glw_frame_epoch++;

    /* Clear to transparent black; terrain is then drawn over it. */
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

void glworld_end_frame(void)
{
    if (!gw.inited) {
        return;
    }
    glw_flush_commands();
    gw.batch_count = 0;
    gw.cmd_count = 0;
    gw.frame_open = false;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

TbBool glworld_frame_active(void)
{
    return gw.inited && gw.frame_open;
}

GLuint glworld_scene_texture(void)
{
    return gw.scene_tex;
}

TbBool glworld_get_window_rect(int *x, int *y, int *w, int *h)
{
    if (!gw.inited || (gw.rect_w <= 0) || (gw.rect_h <= 0)) {
        return false;
    }
    if (x != NULL) { *x = gw.rect_x; }
    if (y != NULL) { *y = gw.rect_y; }
    if (w != NULL) { *w = gw.rect_w; }
    if (h != NULL) { *h = gw.rect_h; }
    return true;
}

void glworld_shutdown(void)
{
    glworld_hires_shutdown();
    glworld_hires_sprites_shutdown();
    if (gw.scene_fbo != 0) {
        glDeleteFramebuffers(1, &gw.scene_fbo);
    }
    if (gw.scene_tex != 0) {
        glDeleteTextures(1, &gw.scene_tex);
    }
    if (gw.program != 0) {
        glDeleteProgram(gw.program);
    }
    if (gw.vao != 0) {
        glDeleteVertexArrays(1, &gw.vao);
    }
    if (gw.vbo != 0) {
        glDeleteBuffers(1, &gw.vbo);
    }
    if (gw.tile_tex != 0) {
        glDeleteTextures(1, &gw.tile_tex);
    }
    if (gw.fade_tex != 0) {
        glDeleteTextures(1, &gw.fade_tex);
    }
    if (gw.pal_tex != 0) {
        glDeleteTextures(1, &gw.pal_tex);
    }
    if (gw.shade_tex != 0) {
        glDeleteTextures(1, &gw.shade_tex);
    }
    if (gw.spr_program != 0) {
        glDeleteProgram(gw.spr_program);
    }
    if (gw.spr_vao != 0) {
        glDeleteVertexArrays(1, &gw.spr_vao);
    }
    if (gw.spr_vbo != 0) {
        glDeleteBuffers(1, &gw.spr_vbo);
    }
    if (gw.spr_atlas_tex != 0) {
        glDeleteTextures(1, &gw.spr_atlas_tex);
    }
    if (gw.remap_tex != 0) {
        glDeleteTextures(1, &gw.remap_tex);
    }
    if (gw.line_program != 0) {
        glDeleteProgram(gw.line_program);
    }
    if (gw.line_vao != 0) {
        glDeleteVertexArrays(1, &gw.line_vao);
    }
    if (gw.line_vbo != 0) {
        glDeleteBuffers(1, &gw.line_vbo);
    }
    free(gw.batch);
    free(gw.cmds);
    free(gw.spr_verts);
    free(gw.line_verts);
    memset(&gw, 0, sizeof(gw));
    /* Reset the file-static sprite/remap caches (atlas contents are gone). */
    if (glw_spr_slots != NULL) {
        for (int i = 0; i < glw_spr_count; i++) {
            free(glw_spr_slots[i].ic);
        }
        free(glw_spr_slots);
        glw_spr_slots = NULL;
    }
    glw_spr_rows = 0;
    glw_spr_count = 0;
    glw_frame_epoch = 0;
    memset(glw_remap_tab, 0, sizeof(glw_remap_tab));
    glw_remap_next = 1;
    /* Free the terrain atlas staging buffer so a re-init re-allocates cleanly. */
    free(glw_atlasbuf);
    glw_atlasbuf = NULL;
}

/* ----------------------------------------------------------------------- */
/* Terrain pipeline: shaders + texture store + batch                        */
/* ----------------------------------------------------------------------- */

/* The fragment shader reproduces the software per-pixel pipeline exactly:
 *   pal_idx  = tile[uv]                       (raw 0..255 palette index)
 *   shade    = clamp(round(vShade), 0, 63)    (software S>>16, fade row)
 *   faded    = fade_tables[shade*256 + idx]   (remapped palette index)
 *   rgb      = lbPaletteColors[faded]
 * No floating brightness multiply is used, so the palette-snapped result is
 * bit-identical to draw_gpoly's fade-table lookup. */

static const char *glw_vs_src =
    "#version 330 core\n"
    "layout(location=0) in vec2 aPos;\n"   /* engine-window local pixels */
    "layout(location=1) in vec2 aUV;\n"
    "layout(location=2) in float aShade;\n"
    "layout(location=3) in float aLayer;\n"
    "uniform vec2 uWinSize;\n"
    "out vec2 vUV;\n"
    "out float vShade;\n"        /* gouraud-interpolated shade level */
    "flat out float vLayer;\n"
    "void main(){\n"
    "  vUV = aUV;\n"
    "  vShade = aShade;\n"
    "  vLayer = aLayer;\n"
    /* Local pixel (0..w, 0..h), top-left origin -> NDC. The viewport is the
     * engine window, so we map across the whole [-1,1] range, flipping Y. */
    "  float ndcx = (aPos.x / uWinSize.x) * 2.0 - 1.0;\n"
    "  float ndcy = 1.0 - (aPos.y / uWinSize.y) * 2.0;\n"
    "  gl_Position = vec4(ndcx, ndcy, 0.0, 1.0);\n"
    "}\n";

static const char *glw_fs_src =
    "#version 330 core\n"
    "in vec2 vUV;\n"
    "in float vShade;\n"
    "flat in float vLayer;\n"
    "uniform usampler2D uTiles;\n"   /* 2D atlas of 32x32 tiles */
    "uniform usampler2D uFade;\n"
    "uniform sampler2D uPal;\n"
    "uniform int uAtlasCols;\n"
    "uniform int uTileDim;\n"
    /* Hi-res override uniforms (Task 3). */
    "uniform sampler2DArray uOvArray;\n"   /* RGBA8 hi-res override tiles */
    "uniform usampler2D     uOvLookup;\n"  /* R16UI block_id -> layer (0xFFFF=none) */
    "uniform sampler2D      uShade;\n"     /* 64x1 RGB: per-shade brightness multiplier */
    "uniform int            uOvCols;\n"    /* lookup texture width; 0 = no overrides */
    "out vec4 oColor;\n"
    "void main(){\n"
    /* Resolve the tile id to its atlas cell, then point-sample within it.
     * UV may span outside 0..1 (software wraps the 32px tile), so wrap. */
    "  int tid = int(vLayer + 0.5);\n"
    "  int cx = tid % uAtlasCols;\n"
    "  int cy = tid / uAtlasCols;\n"
    "  int u = int(floor(vUV.x * float(uTileDim)));\n"
    "  int v = int(floor(vUV.y * float(uTileDim)));\n"
    "  u = ((u % uTileDim) + uTileDim) % uTileDim;\n"
    "  v = ((v % uTileDim) + uTileDim) % uTileDim;\n"
    "  ivec2 texel = ivec2(cx * uTileDim + u, cy * uTileDim + v);\n"
    "  uint palIdx = texelFetch(uTiles, texel, 0).r;\n"
    "  int shade = int(floor(vShade + 0.5));\n"
    "  shade = clamp(shade, 0, 63);\n"
    /* Hi-res override branch: uOvCols<=0 means disabled, falls through to paletted. */
    "  uint ovl = (uOvCols > 0) ? texelFetch(uOvLookup, ivec2(tid % uOvCols, tid / uOvCols), 0).r : 0xFFFFu;\n"
    "  if (ovl != 0xFFFFu) {\n"
    "    vec3 hi = texture(uOvArray, vec3(vUV, float(ovl))).rgb;\n"
    "    vec3 sc = texelFetch(uShade, ivec2(shade, 0), 0).rgb;\n"
    "    oColor = vec4(hi * sc, 1.0);\n"
    "  } else {\n"
    "    uint faded = texelFetch(uFade, ivec2(int(palIdx), shade), 0).r;\n"
    "    vec3 rgb = texelFetch(uPal, ivec2(int(faded), 0), 0).rgb;\n"
    "    oColor = vec4(rgb, 1.0);\n"
    "  }\n"
    "}\n";

static GLuint glw_compile(GLenum type, const char *src, const char *label)
{
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &src, NULL);
    glCompileShader(sh);
    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(sh, sizeof(log), NULL, log);
        LbErrorLog("glworld: %s shader compile failed: %s\n", label, log);
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

static TbBool glw_build_program(void)
{
    GLuint vs = glw_compile(GL_VERTEX_SHADER, glw_vs_src, "vertex");
    GLuint fs = glw_compile(GL_FRAGMENT_SHADER, glw_fs_src, "fragment");
    if ((vs == 0) || (fs == 0)) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return false;
    }
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(prog, sizeof(log), NULL, log);
        LbErrorLog("glworld: terrain program link failed: %s\n", log);
        glDeleteProgram(prog);
        return false;
    }
    gw.program   = prog;
    gw.u_winsize   = glGetUniformLocation(prog, "uWinSize");
    gw.u_tiles     = glGetUniformLocation(prog, "uTiles");
    gw.u_fade      = glGetUniformLocation(prog, "uFade");
    gw.u_pal       = glGetUniformLocation(prog, "uPal");
    gw.u_atlascols = glGetUniformLocation(prog, "uAtlasCols");
    gw.u_tiledim   = glGetUniformLocation(prog, "uTileDim");
    /* Hi-res override uniform locations (Task 3). */
    gw.u_ov_array  = glGetUniformLocation(prog, "uOvArray");
    gw.u_ov_lookup = glGetUniformLocation(prog, "uOvLookup");
    gw.u_shade     = glGetUniformLocation(prog, "uShade");
    gw.u_ov_cols   = glGetUniformLocation(prog, "uOvCols");
    return true;
}

/** Pick atlas dimensions that fit within GL_MAX_TEXTURE_SIZE.
 *
 * GLW_TILE_COUNT 32x32 tiles are packed into a single 2D atlas. A naive fixed
 * column count can yield an atlas whose width or height exceeds the driver's
 * maximum texture size (commonly 8192), which makes glTexImage2D fail with
 * GL_INVALID_VALUE and aborts setup. We query the limit, log it, and choose a
 * roughly square column count that keeps both dimensions within it. Returns
 * false (renderer disabled, software fallback) if the atlas cannot fit. */
static TbBool glw_choose_atlas_layout(void)
{
    GLint maxsz = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxsz);
    if (maxsz <= 0) {
        maxsz = 2048; /* GL 3.3 mandates >= 1024; be conservative. */
    }

    const int tiles_per_dim = maxsz / GLW_TILE_DIM; /* max tiles along an edge. */
    /* Smallest column count whose resulting row count keeps the height within
     * the limit, i.e. ceil(count / tiles_per_dim). */
    int min_cols = (GLW_TILE_COUNT + tiles_per_dim - 1) / tiles_per_dim;

    /* Aim for a roughly square atlas (better margin), but never fewer columns
     * than min_cols and never more than tiles_per_dim (the width cap). */
    int sq_cols = 1;
    while ((sq_cols * sq_cols) < GLW_TILE_COUNT) {
        sq_cols++;
    }
    int cols = (sq_cols > min_cols) ? sq_cols : min_cols;
    if (cols > tiles_per_dim) {
        cols = tiles_per_dim;
    }

    int rows = (GLW_TILE_COUNT + cols - 1) / cols;
    int atlas_w = cols * GLW_TILE_DIM;
    int atlas_h = rows * GLW_TILE_DIM;

    LbSyncLog("glworld: GL_MAX_TEXTURE_SIZE=%d; tile atlas %d cols -> %dx%d "
        "(%d tiles)\n", (int)maxsz, cols, atlas_w, atlas_h, GLW_TILE_COUNT);

    if ((cols < min_cols) || (atlas_w > maxsz) || (atlas_h > maxsz)) {
        LbErrorLog("glworld: tile atlas %dx%d exceeds GL_MAX_TEXTURE_SIZE %d; "
            "GL world renderer disabled\n", atlas_w, atlas_h, (int)maxsz);
        return false;
    }

    gw.atlas_cols = cols;
    gw.atlas_rows = rows;
    gw.atlas_w    = atlas_w;
    gw.atlas_h    = atlas_h;
    return true;
}

/** Lazily build the shader program, VAO/VBO and the static LUT textures. */
static TbBool glw_terrain_ensure(void)
{
    if (gw.terrain_ready) {
        return true;
    }
    if (!glw_choose_atlas_layout()) {
        return false;
    }
    if (!glw_build_program()) {
        return false;
    }

    /* Streaming vertex buffer + attribute layout. */
    glGenVertexArrays(1, &gw.vao);
    glGenBuffers(1, &gw.vbo);
    glBindVertexArray(gw.vao);
    glBindBuffer(GL_ARRAY_BUFFER, gw.vbo);
    glBufferData(GL_ARRAY_BUFFER,
        (GLsizeiptr)GLW_BATCH_MAX * GLW_VERT_FLOATS * sizeof(float),
        NULL, GL_STREAM_DRAW);
    const GLsizei stride = GLW_VERT_FLOATS * sizeof(float);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, (void*)(2*sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, stride, (void*)(4*sizeof(float)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, stride, (void*)(5*sizeof(float)));
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    /* Tile atlas: R8UI 2D, gw.atlas_w x gw.atlas_h. Allocated once. */
    glGenTextures(1, &gw.tile_tex);
    glBindTexture(GL_TEXTURE_2D, gw.tile_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8UI,
        gw.atlas_w, gw.atlas_h, 0,
        GL_RED_INTEGER, GL_UNSIGNED_BYTE, NULL);
    glBindTexture(GL_TEXTURE_2D, 0);

    /* Fade table LUT: R8UI, 256 wide x 64 rows. */
    glGenTextures(1, &gw.fade_tex);
    glBindTexture(GL_TEXTURE_2D, gw.fade_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8UI, 256, GLW_FADE_LEVELS, 0,
        GL_RED_INTEGER, GL_UNSIGNED_BYTE, NULL);

    /* Palette LUT: RGB8, 256 wide x 1. */
    glGenTextures(1, &gw.pal_tex);
    glBindTexture(GL_TEXTURE_2D, gw.pal_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, 256, 1, 0,
        GL_RGB, GL_UNSIGNED_BYTE, NULL);
    glBindTexture(GL_TEXTURE_2D, 0);

    gw.batch = (float *)malloc((size_t)GLW_BATCH_MAX * GLW_VERT_FLOATS * sizeof(float));
    if (gw.batch == NULL) {
        LbErrorLog("glworld: terrain batch alloc failed\n");
        return false;
    }
    gw.cmds = malloc((size_t)GLW_CMD_MAX * sizeof(struct GlwCmd));
    if (gw.cmds == NULL) {
        LbErrorLog("glworld: command list alloc failed\n");
        return false;
    }

    if (glworld_check_error("glw_terrain_ensure")) {
        return false;
    }
    gw.terrain_ready = true;
    gw.texstore_dirty = true; /* force a full tile upload on first sync. */
    return true;
}

void glworld_texstore_mark_dirty(void)
{
    gw.texstore_dirty = true;
}

void glworld_texstore_sync(void)
{
    if (!gw.inited) {
        return;
    }
    if (!glw_terrain_ensure()) {
        return;
    }

    /* The tile atlas is large (tens of thousands of 32x32 tiles). Uploading it
     * is expensive, so only do so when marked dirty (first build, palette or
     * block_ptrs change). Build it in a single client-side buffer and upload
     * with one glTexSubImage2D for speed. */
    if (gw.texstore_dirty) {
        const size_t atlas_bytes = (size_t)gw.atlas_w * (size_t)gw.atlas_h;
        if (glw_atlasbuf == NULL) {
            glw_atlasbuf = (unsigned char *)malloc(atlas_bytes);
        }
        if (glw_atlasbuf != NULL) {
            memset(glw_atlasbuf, 0, atlas_bytes);
            for (int i = 0; i < GLW_TILE_COUNT; i++) {
                const unsigned char *src = block_ptrs[i];
                if (src == NULL) {
                    continue;
                }
                int cx = (i % gw.atlas_cols) * GLW_TILE_DIM;
                int cy = (i / gw.atlas_cols) * GLW_TILE_DIM;
                for (int row = 0; row < GLW_TILE_DIM; row++) {
                    unsigned char *dst = &glw_atlasbuf[(size_t)(cy + row) * gw.atlas_w + cx];
                    memcpy(dst, &src[(size_t)row * GLW_TILE_ROW], GLW_TILE_DIM);
                }
            }
            glBindTexture(GL_TEXTURE_2D, gw.tile_tex);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, gw.atlas_w, gw.atlas_h,
                GL_RED_INTEGER, GL_UNSIGNED_BYTE, glw_atlasbuf);
        }
        gw.texstore_dirty = false;
    }

    /* Upload the fade-table LUT (64*256 bytes of remapped palette indices). */
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glBindTexture(GL_TEXTURE_2D, gw.fade_tex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, GLW_FADE_LEVELS,
        GL_RED_INTEGER, GL_UNSIGNED_BYTE, pixmap.fade_tables);

    /* Upload the active palette as RGB8 (lbPaletteColors are 0..255 already). */
    {
        unsigned char pal[256 * 3];
        for (int i = 0; i < 256; i++) {
            pal[i*3+0] = lbPaletteColors[i].r;
            pal[i*3+1] = lbPaletteColors[i].g;
            pal[i*3+2] = lbPaletteColors[i].b;
        }
        glBindTexture(GL_TEXTURE_2D, gw.pal_tex);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 1,
            GL_RGB, GL_UNSIGNED_BYTE, pal);
    }

    /* Shade-scale LUT (Task 3): 64x1 RGB32F texture mapping shade level ->
     * brightness multiplier for hi-res tile shading. Computed from
     * pixmap.fade_tables so the multiplier matches the paletted path exactly.
     * Only the hi-res terrain branch samples gw.shade_tex, so skip the
     * ~16K-multiply LUT build and upload entirely when hi-res is off. */
    if (gl_hires_active) {
        float shade_lut[64 * 3];
        double base = 0.0;
        for (int i = 0; i < 256; i++) {
            base += 0.299 * lbPaletteColors[i].r
                  + 0.587 * lbPaletteColors[i].g
                  + 0.114 * lbPaletteColors[i].b;
        }
        base = (base / 256.0) + 1e-3;
        for (int s = 0; s < 64; s++) {
            double acc = 0.0;
            for (int i = 0; i < 256; i++) {
                unsigned char fi = pixmap.fade_tables[s * 256 + i];
                acc += 0.299 * lbPaletteColors[fi].r
                     + 0.587 * lbPaletteColors[fi].g
                     + 0.114 * lbPaletteColors[fi].b;
            }
            float sc = (float)((acc / 256.0) / base);
            if (sc < 0.0f) sc = 0.0f;
            if (sc > 2.0f) sc = 2.0f;
            shade_lut[s * 3 + 0] = shade_lut[s * 3 + 1] = shade_lut[s * 3 + 2] = sc;
        }
        if (gw.shade_tex == 0) {
            glGenTextures(1, &gw.shade_tex);
            glBindTexture(GL_TEXTURE_2D, gw.shade_tex);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB32F, 64, 1, 0,
                GL_RGB, GL_FLOAT, shade_lut);
        } else {
            glBindTexture(GL_TEXTURE_2D, gw.shade_tex);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 64, 1,
                GL_RGB, GL_FLOAT, shade_lut);
        }
    }

    glBindTexture(GL_TEXTURE_2D, 0);
    (void)glworld_check_error("glworld_texstore_sync");
}

/** Append a command, returning a pointer to it (or NULL if the list is full).*/
static struct GlwCmd *glw_cmd_push(unsigned char kind)
{
    if ((gw.cmds == NULL) || (gw.cmd_count >= GLW_CMD_MAX)) {
        return NULL;
    }
    struct GlwCmd *c = &((struct GlwCmd *)gw.cmds)[gw.cmd_count++];
    memset(c, 0, sizeof(*c));
    c->kind = kind;
    return c;
}

void glworld_submit_tri(const struct GlWorldVert v[3], uint16_t texture_id)
{
    if (!gw.terrain_ready || (gw.batch == NULL)) {
        return;
    }
    if (gw.batch_count + 3u > GLW_BATCH_MAX) {
        return; /* drop overflow rather than realloc mid-frame */
    }
    float *p = &gw.batch[(size_t)gw.batch_count * GLW_VERT_FLOATS];
    for (int i = 0; i < 3; i++) {
        p[0] = v[i].sx;
        p[1] = v[i].sy;
        p[2] = v[i].u;
        p[3] = v[i].v;
        p[4] = v[i].shade;
        p[5] = (float)texture_id;
        p += GLW_VERT_FLOATS;
    }
    /* Coalesce consecutive terrain triangles into a single TRI command so the
     * whole run is drawn with one glDrawArrays, while preserving painter's
     * order relative to interleaved sprite/line commands. */
    if (gw.cmd_count > 0) {
        struct GlwCmd *last = &((struct GlwCmd *)gw.cmds)[gw.cmd_count - 1];
        if (last->kind == GLW_CMD_TRI
            && (last->tri_first + last->tri_count == gw.batch_count)) {
            last->tri_count += 3u;
            gw.batch_count += 3u;
            return;
        }
    }
    struct GlwCmd *c = glw_cmd_push(GLW_CMD_TRI);
    if (c != NULL) {
        c->tri_first = gw.batch_count;
        c->tri_count = 3u;
    }
    gw.batch_count += 3u;
}

/* --- per-command draw helpers (defined after the sprite/line pipeline) --- */
static void glw_draw_tri_run(unsigned first, unsigned count);
static void glw_draw_sprite_cmd(const struct GlwCmd *c);
static void glw_draw_line_cmd(const struct GlwCmd *c);

/** Walk the command list in submission order, issuing the right draw per
 *  entry. Order across kinds is preserved (painter's algorithm), which is what
 *  keeps a creature behind a wall occluded by the terrain drawn after it. */
static void glw_flush_commands(void)
{
    if (!gw.terrain_ready || (gw.cmd_count == 0)) {
        return;
    }
    /* Upload the whole terrain vertex batch once; TRI commands index into it.*/
    if (gw.batch_count > 0) {
        glBindBuffer(GL_ARRAY_BUFFER, gw.vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0,
            (GLsizeiptr)gw.batch_count * GLW_VERT_FLOATS * sizeof(float),
            gw.batch);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }
    glDisable(GL_DEPTH_TEST);
    /* Sprites/lines blend against the terrain already in the FBO; opaque
     * sprite pixels still write alpha=1 so the present compositor treats them
     * as world. */
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    const struct GlwCmd *cmds = (const struct GlwCmd *)gw.cmds;
    for (unsigned i = 0; i < gw.cmd_count; i++) {
        const struct GlwCmd *c = &cmds[i];
        switch (c->kind) {
        case GLW_CMD_TRI:
            glw_draw_tri_run(c->tri_first, c->tri_count);
            break;
        case GLW_CMD_SPRITE:
            glw_draw_sprite_cmd(c);
            break;
        case GLW_CMD_LINE:
            glw_draw_line_cmd(c);
            break;
        default:
            break;
        }
    }

    glDisable(GL_BLEND);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);
    glActiveTexture(GL_TEXTURE0);
    (void)glworld_check_error("glw_flush_commands");
}

/** Draw a run of terrain triangles using the terrain program. */
static void glw_draw_tri_run(unsigned first, unsigned count)
{
    if (count == 0) {
        return;
    }
    glUseProgram(gw.program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, gw.tile_tex);
    glUniform1i(gw.u_tiles, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, gw.fade_tex);
    glUniform1i(gw.u_fade, 1);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, gw.pal_tex);
    glUniform1i(gw.u_pal, 2);
    glUniform2f(gw.u_winsize, (float)gw.win_w_local, (float)gw.win_h_local);
    glUniform1i(gw.u_atlascols, gw.atlas_cols);
    glUniform1i(gw.u_tiledim, GLW_TILE_DIM);
    /* Hi-res override textures (Task 3).  Units 4/5/6 are free (SP1 uses 0-2).
     * When gl_hires_active is false uOvCols is set to 0, which makes the shader
     * take the original paletted branch unconditionally (identity guard). */
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D_ARRAY,
        gl_hires_active ? glworld_hires_array() : 0);
    glUniform1i(gw.u_ov_array, 4);
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D,
        gl_hires_active ? glworld_hires_lookup_tex() : 0);
    glUniform1i(gw.u_ov_lookup, 5);
    glActiveTexture(GL_TEXTURE6);
    glBindTexture(GL_TEXTURE_2D, gw.shade_tex);
    glUniform1i(gw.u_shade, 6);
    glUniform1i(gw.u_ov_cols, gl_hires_active ? gw.atlas_cols : 0);
    glBindVertexArray(gw.vao);
    glDrawArrays(GL_TRIANGLES, (GLint)first, (GLsizei)count);
    glBindVertexArray(0);
    /* Reset to unit 0 so no later pass accidentally targets unit 6 (shade_tex). */
    glActiveTexture(GL_TEXTURE0);
}

/* ----------------------------------------------------------------------- */
/* Sprite (billboard) + line pipeline — Task 4                              */
/* ----------------------------------------------------------------------- */

/* Sprite fragment shader reproduces the software sprite blit colour chain:
 *   idx     = atlas.R           (raw source palette index)
 *   cov     = atlas.G           (0 = transparent run -> discard)
 *   remapd  = remap[idx]        (player colour / tint / shade fade-table)
 *   rgb     = palette[remapd]
 * The remap LUT row encodes lbSpriteReMapPtr (row 0 = identity). Alpha is a
 * uniform: 1.0 opaque, 0.5/0.25 for the dithered TRANSPAR modes, per-sprite
 * for alpha sprites. GL_NEAREST throughout for a point-sampled match. */
static const char *glw_spr_vs_src =
    "#version 330 core\n"
    "layout(location=0) in vec2 aPos;\n"     /* engine-window local pixels */
    "layout(location=1) in vec2 aUV;\n"      /* atlas-space UV for paletted path */
    "layout(location=2) in float aRemapRow;\n"
    "layout(location=3) in float aOvLayer;\n" /* hi-res override layer (-1 = none) */
    "layout(location=4) in vec2 aHiUV;\n"    /* 0..1 UV for hi-res override path */
    "uniform vec2 uWinSize;\n"
    "out vec2 vUV;\n"
    "out vec2 vHiUV;\n"
    "flat out float vRemapRow;\n"
    "flat out float vOvLayer;\n"
    "void main(){\n"
    "  vUV = aUV;\n"
    "  vHiUV = aHiUV;\n"
    "  vRemapRow = aRemapRow;\n"
    "  vOvLayer = aOvLayer;\n"
    "  float ndcx = (aPos.x / uWinSize.x) * 2.0 - 1.0;\n"
    "  float ndcy = 1.0 - (aPos.y / uWinSize.y) * 2.0;\n"
    "  gl_Position = vec4(ndcx, ndcy, 0.0, 1.0);\n"
    "}\n";

static const char *glw_spr_fs_src =
    "#version 330 core\n"
    "in vec2 vUV;\n"
    "in vec2 vHiUV;\n"
    "flat in float vRemapRow;\n"
    "flat in float vOvLayer;\n"
    "uniform usampler2D uAtlas;\n"        /* RG8UI: R=index, G=coverage */
    "uniform usampler2D uRemap;\n"        /* R8UI 256 x N: remap LUT rows */
    "uniform sampler2D uPal;\n"           /* RGB8 256 x 1: palette */
    "uniform float uAlpha;\n"
    "uniform sampler2DArray uSprOv;\n"    /* RGBA8 2D_ARRAY: hi-res frame overrides */
    "uniform sampler2D uShade;\n"         /* RGB32F 64x1: shade LUT (reserved for v2) */
    "out vec4 oColor;\n"
    "void main(){\n"
    "  if (vOvLayer >= 0.0) {\n"
    "    /* Hi-res override path: sample RGBA8 array with 0..1 UV per frame. */\n"
    "    vec4 hi = texture(uSprOv, vec3(vHiUV, vOvLayer));\n"
    "    if (hi.a < 0.02) { discard; }\n"
    "    /* v1: full-bright for hi-res objects; shade LUT (uShade) reserved v2. */\n"
    "    oColor = vec4(hi.rgb, hi.a * uAlpha);\n"
    "  } else {\n"
    "    /* Paletted path: atlas.R -> remap -> palette -> coverage, unchanged. */\n"
    "    ivec2 sz = textureSize(uAtlas, 0);\n"
    "    ivec2 texel = ivec2(int(vUV.x * float(sz.x)), int(vUV.y * float(sz.y)));\n"
    "    uvec2 ag = texelFetch(uAtlas, texel, 0).rg;\n"
    "    if (ag.g == 0u) { discard; }\n"
    "    int row = int(vRemapRow + 0.5);\n"
    "    uint remapd = texelFetch(uRemap, ivec2(int(ag.r), row), 0).r;\n"
    "    vec3 rgb = texelFetch(uPal, ivec2(int(remapd), 0), 0).rgb;\n"
    "    oColor = vec4(rgb, uAlpha);\n"
    "  }\n"
    "}\n";

/* Line shader: solid colour resolved through the palette LUT. */
static const char *glw_line_vs_src =
    "#version 330 core\n"
    "layout(location=0) in vec2 aPos;\n"
    "layout(location=1) in float aPal;\n"
    "uniform vec2 uWinSize;\n"
    "flat out float vPal;\n"
    "void main(){\n"
    "  vPal = aPal;\n"
    "  float ndcx = (aPos.x / uWinSize.x) * 2.0 - 1.0;\n"
    "  float ndcy = 1.0 - (aPos.y / uWinSize.y) * 2.0;\n"
    "  gl_Position = vec4(ndcx, ndcy, 0.0, 1.0);\n"
    "}\n";

static const char *glw_line_fs_src =
    "#version 330 core\n"
    "flat in float vPal;\n"
    "uniform sampler2D uPal;\n"
    "out vec4 oColor;\n"
    "void main(){\n"
    "  vec3 rgb = texelFetch(uPal, ivec2(int(vPal + 0.5), 0), 0).rgb;\n"
    "  oColor = vec4(rgb, 1.0);\n"
    "}\n";

static GLuint glw_link(const char *vs_src, const char *fs_src, const char *lbl)
{
    GLuint vs = glw_compile(GL_VERTEX_SHADER, vs_src, lbl);
    GLuint fs = glw_compile(GL_FRAGMENT_SHADER, fs_src, lbl);
    if ((vs == 0) || (fs == 0)) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return 0;
    }
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(prog, sizeof(log), NULL, log);
        LbErrorLog("glworld: %s program link failed: %s\n", lbl, log);
        glDeleteProgram(prog);
        return 0;
    }
    return prog;
}

/** Build the sprite + line GL programs, VAOs/VBOs and the sprite atlas/remap
 *  textures. Lazy — runs on the first sprite/line submit of a session. */
static TbBool glw_sprite_ensure(void)
{
    if (gw.sprite_ready) {
        return true;
    }
    /* The sprite pipeline reuses the terrain palette LUT, so the terrain
     * objects must exist first. */
    if (!glw_terrain_ensure()) {
        return false;
    }

    /* Compute the runtime sprite atlas row cap from GL_MAX_TEXTURE_SIZE.
     * glw_choose_atlas_layout() already queried this for the tile atlas; we
     * re-query here so the sprite path is self-contained.  The sprite atlas
     * width is fixed at GLW_SPR_COLS*GLW_SPR_CELL (16*256 = 4096 px); if that
     * already exceeds the runtime limit the GPU cannot host the atlas at all,
     * so disable the sprite GL path entirely (sprites fall back to the
     * software/composite path). */
    {
        GLint maxsz = 0;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxsz);
        if (maxsz <= 0) {
            maxsz = 2048; /* GL 3.3 mandates >= 1024; be conservative. */
        }
        if ((GLW_SPR_COLS * GLW_SPR_CELL) > (int)maxsz) {
            LbErrorLog("glworld: sprite atlas width %d exceeds "
                "GL_MAX_TEXTURE_SIZE %d; sprite GL path disabled\n",
                GLW_SPR_COLS * GLW_SPR_CELL, (int)maxsz);
            return false;
        }
        int cap = (int)maxsz / GLW_SPR_CELL;
        glw_spr_rows_max = (cap < GLW_SPR_ROWS_MAX) ? cap : GLW_SPR_ROWS_MAX;
        LbSyncLog("glworld: sprite atlas row cap %d (GL_MAX_TEXTURE_SIZE=%d)\n",
            glw_spr_rows_max, (int)maxsz);
    }

    gw.spr_program = glw_link(glw_spr_vs_src, glw_spr_fs_src, "sprite");
    gw.line_program = glw_link(glw_line_vs_src, glw_line_fs_src, "line");
    if ((gw.spr_program == 0) || (gw.line_program == 0)) {
        return false;
    }
    gw.su_winsize = glGetUniformLocation(gw.spr_program, "uWinSize");
    gw.su_pal     = glGetUniformLocation(gw.spr_program, "uPal");
    gw.su_atlas   = glGetUniformLocation(gw.spr_program, "uAtlas");
    gw.su_remap   = glGetUniformLocation(gw.spr_program, "uRemap");
    gw.su_alpha     = glGetUniformLocation(gw.spr_program, "uAlpha");
    gw.su_sprov     = glGetUniformLocation(gw.spr_program, "uSprOv");
    gw.su_shade_spr = glGetUniformLocation(gw.spr_program, "uShade");
    gw.lu_winsize   = glGetUniformLocation(gw.line_program, "uWinSize");
    gw.lu_pal     = glGetUniformLocation(gw.line_program, "uPal");

    /* Sprite quad VBO: 6 verts/quad, GLW_SPR_VFLOATS floats each, streamed. */
    glGenVertexArrays(1, &gw.spr_vao);
    glGenBuffers(1, &gw.spr_vbo);
    glBindVertexArray(gw.spr_vao);
    glBindBuffer(GL_ARRAY_BUFFER, gw.spr_vbo);
    glBufferData(GL_ARRAY_BUFFER,
        (GLsizeiptr)6 * GLW_SPR_VFLOATS * sizeof(float), NULL, GL_STREAM_DRAW);
    {
        const GLsizei st = GLW_SPR_VFLOATS * sizeof(float);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, st, (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, st, (void*)(2*sizeof(float)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, st, (void*)(4*sizeof(float)));
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, st, (void*)(5*sizeof(float)));
        glEnableVertexAttribArray(4);
        glVertexAttribPointer(4, 2, GL_FLOAT, GL_FALSE, st, (void*)(6*sizeof(float)));
    }
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    /* Line VBO: 2 verts/line, GLW_LINE_VFLOATS floats each. */
    glGenVertexArrays(1, &gw.line_vao);
    glGenBuffers(1, &gw.line_vbo);
    glBindVertexArray(gw.line_vao);
    glBindBuffer(GL_ARRAY_BUFFER, gw.line_vbo);
    glBufferData(GL_ARRAY_BUFFER,
        (GLsizeiptr)2 * GLW_LINE_VFLOATS * sizeof(float), NULL, GL_STREAM_DRAW);
    {
        const GLsizei st = GLW_LINE_VFLOATS * sizeof(float);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, st, (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, st, (void*)(2*sizeof(float)));
    }
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    /* Sprite atlas: RG8UI, (cols*cell) x (rows*cell). R=index, G=coverage.
     * Allocated at the initial row count; grown on demand (see
     * glw_sprite_slot). The slot array tracks one cell per atlas cell. */
    glGenTextures(1, &gw.spr_atlas_tex);
    glBindTexture(GL_TEXTURE_2D, gw.spr_atlas_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    /* Clamp the initial row count to the runtime cap (it may be below
     * GLW_SPR_ROWS_INIT on very constrained GPUs). */
    int init_rows = GLW_SPR_ROWS_INIT;
    if (init_rows > glw_spr_rows_max) {
        init_rows = glw_spr_rows_max;
    }
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RG8UI,
        GLW_SPR_COLS * GLW_SPR_CELL, init_rows * GLW_SPR_CELL, 0,
        GL_RG_INTEGER, GL_UNSIGNED_BYTE, NULL);
    glw_spr_rows = init_rows;
    glw_spr_count = 0;
    glw_spr_slots = (struct GlwSprSlot *)calloc(
        (size_t)init_rows * GLW_SPR_COLS, sizeof(struct GlwSprSlot));
    if (glw_spr_slots == NULL) {
        LbErrorLog("glworld: sprite slot table alloc failed\n");
        return false;
    }

    /* Remap LUT: R8UI, 256 x GLW_REMAP_MAX. Row 0 = identity. */
    glGenTextures(1, &gw.remap_tex);
    glBindTexture(GL_TEXTURE_2D, gw.remap_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8UI, 256, GLW_REMAP_MAX, 0,
        GL_RED_INTEGER, GL_UNSIGNED_BYTE, NULL);
    {
        unsigned char ident[256];
        for (int i = 0; i < 256; i++) { ident[i] = (unsigned char)i; }
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 1,
            GL_RED_INTEGER, GL_UNSIGNED_BYTE, ident);
    }
    glBindTexture(GL_TEXTURE_2D, 0);

    gw.spr_verts = (float *)malloc((size_t)6 * GLW_SPR_VFLOATS * sizeof(float));
    gw.line_verts = (float *)malloc((size_t)2 * GLW_LINE_VFLOATS * sizeof(float));
    if ((gw.spr_verts == NULL) || (gw.line_verts == NULL)) {
        LbErrorLog("glworld: sprite/line scratch alloc failed\n");
        return false;
    }

    if (glworld_check_error("glw_sprite_ensure")) {
        return false;
    }
    gw.sprite_ready = true;
    return true;
}

/** Resolve a remap pointer to a LUT row, uploading + caching new ones.
 *  NULL maps to row 0 (identity). */
static int glw_remap_row(const unsigned char *remap)
{
    if (remap == NULL) {
        return 0;
    }
    for (unsigned i = 1; i < GLW_REMAP_MAX; i++) {
        if (glw_remap_tab[i].used && (glw_remap_tab[i].ptr == remap)) {
            return (int)i;
        }
    }
    /* Allocate a new row (round-robin, never row 0). Re-uploading on identity
     * clash is fine — the pointer is the cache key and tints change rarely. */
    unsigned row = glw_remap_next;
    glw_remap_next++;
    if (glw_remap_next >= GLW_REMAP_MAX) {
        glw_remap_next = 1;
    }
    glw_remap_tab[row].used = true;
    glw_remap_tab[row].ptr = remap;
    glBindTexture(GL_TEXTURE_2D, gw.remap_tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, (GLint)row, 256, 1,
        GL_RED_INTEGER, GL_UNSIGNED_BYTE, remap);
    glBindTexture(GL_TEXTURE_2D, 0);
    return (int)row;
}

/** Decode a KeeperSprite RLE frame into a flat index+coverage buffer.
 *  The RLE format (per row, src->height rows): a stream of signed-byte opcodes
 *  terminated by 0. opcode>0 -> that many literal pixel bytes follow (visible);
 *  opcode<0 -> skip -opcode transparent pixels. Output is row-major w*h, two
 *  bytes per pixel (index, coverage). Returns false on bad input. */
static TbBool glw_decode_rle(const unsigned char *data, size_t data_len,
    int w, int h, unsigned char *out_ic /* w*h*2 */)
{
    if ((data == NULL) || (w <= 0) || (h <= 0)) {
        return false;
    }
    const unsigned char *p = data;
    const unsigned char *pend = data + data_len;
    for (int row = 0; row < h; row++) {
        unsigned char *orow = &out_ic[(size_t)row * (size_t)w * 2u];
        int x = 0;
        while (1) {
            if (p >= pend) {
                /* Malformed frame: the row terminator never arrived within the
                 * source extent. Mirror the software trust model (stop reading)
                 * but do not read past the buffer; remaining cols pad below. */
                goto pad_row;
            }
            int op = (signed char)(*p++);
            if (op == 0) {
                break;
            }
            if (op < 0) {
                /* transparent run */
                int n = -op;
                while ((n-- > 0) && (x < w)) {
                    orow[x*2+0] = 0;
                    orow[x*2+1] = 0;   /* coverage 0 */
                    x++;
                }
            } else {
                /* literal run of op pixels */
                for (int k = 0; k < op; k++) {
                    if (p >= pend) {
                        goto pad_row;
                    }
                    unsigned char idx = *p++;
                    if (x < w) {
                        orow[x*2+0] = idx;
                        orow[x*2+1] = 255; /* coverage opaque */
                        x++;
                    }
                }
            }
        }
    pad_row:
        /* pad any remaining columns transparent */
        while (x < w) {
            orow[x*2+0] = 0;
            orow[x*2+1] = 0;
            x++;
        }
    }
    return true;
}

/** Upload one slot's CPU-shadow pixels to its atlas cell. */
static void glw_spr_upload_slot(int slot)
{
    const struct GlwSprSlot *s = &glw_spr_slots[slot];
    if (!s->used || (s->ic == NULL)) {
        return;
    }
    int cx = (slot % GLW_SPR_COLS) * GLW_SPR_CELL;
    int cy = (slot / GLW_SPR_COLS) * GLW_SPR_CELL;
    glBindTexture(GL_TEXTURE_2D, gw.spr_atlas_tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, cx, cy, s->w, s->h,
        GL_RG_INTEGER, GL_UNSIGNED_BYTE, s->ic);
    glBindTexture(GL_TEXTURE_2D, 0);
}

/** Grow the atlas by GLW_SPR_ROWS_INIT rows (up to GLW_SPR_ROWS_MAX),
 *  reallocating the texture and re-uploading every live slot from its CPU
 *  shadow. Returns true on success. Live slots survive the grow, so a slot a
 *  pending command references is never overwritten. */
static TbBool glw_spr_atlas_grow(void)
{
    int new_rows = glw_spr_rows + GLW_SPR_ROWS_INIT;
    if (new_rows > glw_spr_rows_max) {
        new_rows = glw_spr_rows_max;
    }
    if (new_rows <= glw_spr_rows) {
        return false; /* already at cap. */
    }
    int new_cells = new_rows * GLW_SPR_COLS;
    struct GlwSprSlot *ns = (struct GlwSprSlot *)realloc(glw_spr_slots,
        (size_t)new_cells * sizeof(struct GlwSprSlot));
    if (ns == NULL) {
        return false;
    }
    glw_spr_slots = ns;
    memset(&glw_spr_slots[glw_spr_rows * GLW_SPR_COLS], 0,
        (size_t)(new_cells - glw_spr_rows * GLW_SPR_COLS)
            * sizeof(struct GlwSprSlot));

    /* Reallocate the texture (no in-place resize) and re-upload all shadows. */
    glBindTexture(GL_TEXTURE_2D, gw.spr_atlas_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RG8UI,
        GLW_SPR_COLS * GLW_SPR_CELL, new_rows * GLW_SPR_CELL, 0,
        GL_RG_INTEGER, GL_UNSIGNED_BYTE, NULL);
    glBindTexture(GL_TEXTURE_2D, 0);
    if (glworld_check_error("glw_spr_atlas_grow")) {
        /* Texture realloc failed (e.g. exceeded a driver limit): the old
         * contents are gone, so drop every slot rather than risk stale cells.
         * Keep the enlarged slot array; rows stays at the (now empty) new size
         * so callers fall back to the software path until cells refill. */
        for (int i = 0; i < glw_spr_count; i++) {
            free(glw_spr_slots[i].ic);
            glw_spr_slots[i].ic = NULL;
            glw_spr_slots[i].used = false;
        }
        glw_spr_count = 0;
        glw_spr_rows = new_rows;
        return false;
    }
    glw_spr_rows = new_rows;
    for (int i = 0; i < glw_spr_count; i++) {
        glw_spr_upload_slot(i);
    }
    return true;
}

/** Find or build the atlas slot for a frame. Returns slot index or -1.
 *  No slot referenced by a pending (current-epoch) command is ever evicted:
 *  the atlas grows instead, up to GLW_SPR_ROWS_MAX; past that the sprite is
 *  skipped (-1) so it falls back to the software/composite path. */
static int glw_sprite_slot(uint32_t key, const void *src_data, size_t src_len,
    int src_w, int src_h)
{
    int w = src_w; int h = src_h;
    if (w > GLW_SPR_CELL) { w = GLW_SPR_CELL; }
    if (h > GLW_SPR_CELL) { h = GLW_SPR_CELL; }
    if ((w <= 0) || (h <= 0)) {
        return -1;
    }
    int cells = glw_spr_rows * GLW_SPR_COLS;
    /* Cache hit? Refresh the epoch so this slot is treated as live this frame.*/
    for (int i = 0; i < glw_spr_count; i++) {
        if (glw_spr_slots[i].used && (glw_spr_slots[i].key == key)
            && (glw_spr_slots[i].w == w) && (glw_spr_slots[i].h == h)) {
            glw_spr_slots[i].epoch = glw_frame_epoch;
            return i;
        }
    }

    /* Pick a slot to (re)use. Prefer a never-used cell; else the oldest slot
     * whose epoch predates this frame (safe to evict — no pending command
     * references it). A slot used THIS frame must not be evicted. */
    int slot = -1;
    if (glw_spr_count < cells) {
        slot = glw_spr_count; /* fresh cell. */
    } else {
        uint32_t oldest = glw_frame_epoch;
        for (int i = 0; i < cells; i++) {
            if (glw_spr_slots[i].epoch != glw_frame_epoch) {
                /* candidate; pick the least-recently-used among them. */
                if ((slot < 0) || (glw_spr_slots[i].epoch < oldest)) {
                    slot = i;
                    oldest = glw_spr_slots[i].epoch;
                }
            }
        }
        if (slot < 0) {
            /* Every slot is live this frame: grow rather than corrupt one. */
            if (!glw_spr_atlas_grow()) {
                return -1; /* at cap or grow failed: skip, software path. */
            }
            cells = glw_spr_rows * GLW_SPR_COLS;
            slot = glw_spr_count; /* first newly-added cell. */
        }
    }

    size_t icbytes = (size_t)w * (size_t)h * 2u;
    unsigned char *ic = (unsigned char *)malloc(icbytes);
    if (ic == NULL) {
        return -1;
    }
    if (!glw_decode_rle((const unsigned char *)src_data, src_len, w, h, ic)) {
        free(ic);
        return -1;
    }

    /* Replace any prior shadow on an evicted slot. */
    free(glw_spr_slots[slot].ic);
    glw_spr_slots[slot].ic = ic;
    glw_spr_slots[slot].used = true;
    glw_spr_slots[slot].key = key;
    glw_spr_slots[slot].w = w;
    glw_spr_slots[slot].h = h;
    glw_spr_slots[slot].epoch = glw_frame_epoch;
    if (slot >= glw_spr_count) {
        glw_spr_count = slot + 1;
    }
    glw_spr_upload_slot(slot);
    return slot;
}

void glworld_submit_keepersprite(int dx, int dy, int dw, int dh,
    const void *src_data, size_t src_len, int src_w, int src_h,
    uint32_t frame_key, const unsigned char *remap,
    TbBool flip_x, enum GlWorldSpriteBlend blend)
{
    if (!gw.inited) {
        return;
    }
    if (!glw_sprite_ensure()) {
        return;
    }
    if ((dw <= 0) || (dh <= 0)) {
        return;
    }
    int slot = glw_sprite_slot(frame_key, src_data, src_len, src_w, src_h);
    if (slot < 0) {
        return;
    }
    int row = glw_remap_row(remap);
    struct GlwCmd *c = glw_cmd_push(GLW_CMD_SPRITE);
    if (c == NULL) {
        return;
    }
    c->slot = slot;
    c->dx = (float)dx;
    c->dy = (float)dy;
    c->dw = (float)dw;
    c->dh = (float)dh;
    c->remap_row = row;
    c->flip_x = flip_x ? 1u : 0u;
    c->blend = (unsigned char)blend;
    c->ov_layer = gl_hires_sprites_active
        ? (float)glworld_hires_sprites_layer(frame_key)
        : -1.0f;
}

void glworld_submit_line(float x0, float y0, float x1, float y1,
    unsigned char color)
{
    if (!gw.inited) {
        return;
    }
    if (!glw_sprite_ensure()) {
        return;
    }
    struct GlwCmd *c = glw_cmd_push(GLW_CMD_LINE);
    if (c == NULL) {
        return;
    }
    c->x0 = x0; c->y0 = y0; c->x1 = x1; c->y1 = y1;
    c->color = color;
}

/** Draw one sprite quad. The atlas cell is GLW_SPR_CELL wide but the frame
 *  occupies only w x h of it; UVs span that sub-rect so empty cell area is not
 *  sampled. flip_x mirrors U. */
static void glw_draw_sprite_cmd(const struct GlwCmd *c)
{
    if ((c->slot < 0) || (c->slot >= glw_spr_rows * GLW_SPR_COLS)
        || !glw_spr_slots[c->slot].used) {
        return;
    }
    const int fw = glw_spr_slots[c->slot].w;
    const int fh = glw_spr_slots[c->slot].h;
    const float atlas_w = (float)(GLW_SPR_COLS * GLW_SPR_CELL);
    const float atlas_h = (float)(glw_spr_rows * GLW_SPR_CELL);
    const int cx = (c->slot % GLW_SPR_COLS) * GLW_SPR_CELL;
    const int cy = (c->slot / GLW_SPR_COLS) * GLW_SPR_CELL;
    float u0 = (float)cx / atlas_w;
    float u1 = (float)(cx + fw) / atlas_w;
    float v0 = (float)cy / atlas_h;
    float v1 = (float)(cy + fh) / atlas_h;
    if (c->flip_x) {
        float t = u0; u0 = u1; u1 = t;
    }
    const float x0 = c->dx;
    const float y0 = c->dy;
    const float x1 = c->dx + c->dw;
    const float y1 = c->dy + c->dh;
    /* Two triangles, top-left origin (matches terrain vertex mapping).
     * hi_u0/hi_u1: normalized 0..1 UV for the hi-res override path; flip_x
     * is already baked into u0/u1 (atlas), so mirror hi_u similarly. */
    const float hi_u0 = c->flip_x ? 1.0f : 0.0f;
    const float hi_u1 = c->flip_x ? 0.0f : 1.0f;
    const float ov = c->ov_layer;
    const float verts[6][GLW_SPR_VFLOATS] = {
        { x0, y0, u0, v0, (float)c->remap_row, ov, hi_u0, 0.0f },
        { x1, y0, u1, v0, (float)c->remap_row, ov, hi_u1, 0.0f },
        { x1, y1, u1, v1, (float)c->remap_row, ov, hi_u1, 1.0f },
        { x0, y0, u0, v0, (float)c->remap_row, ov, hi_u0, 0.0f },
        { x1, y1, u1, v1, (float)c->remap_row, ov, hi_u1, 1.0f },
        { x0, y1, u0, v1, (float)c->remap_row, ov, hi_u0, 1.0f },
    };
    float alpha = 1.0f;
    switch (c->blend) {
    case GLW_BLEND_TRANSPAR8: alpha = 0.5f;  break;
    case GLW_BLEND_TRANSPAR4: alpha = 0.25f; break;
    case GLW_BLEND_ALPHA:     alpha = 0.5f;  break;
    default:                  alpha = 1.0f;  break;
    }

    glUseProgram(gw.spr_program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, gw.spr_atlas_tex);
    glUniform1i(gw.su_atlas, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, gw.remap_tex);
    glUniform1i(gw.su_remap, 1);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, gw.pal_tex);
    glUniform1i(gw.su_pal, 2);
    /* Hi-res sprite override (Task 3): units 3 (RGBA8 array) + 4 (shade LUT).
     * When gl_hires_sprites_active is false every ov_layer is -1, so the
     * shader override branch is never taken — off-path identity guaranteed. */
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D_ARRAY,
        gl_hires_sprites_active ? glworld_hires_sprites_array() : 0);
    glUniform1i(gw.su_sprov, 3);
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, gw.shade_tex);
    glUniform1i(gw.su_shade_spr, 4);
    glUniform2f(gw.su_winsize, (float)gw.win_w_local, (float)gw.win_h_local);
    glUniform1f(gw.su_alpha, alpha);

    glBindVertexArray(gw.spr_vao);
    glBindBuffer(GL_ARRAY_BUFFER, gw.spr_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    /* Reset active unit so subsequent passes don't target unit 4 accidentally. */
    glActiveTexture(GL_TEXTURE0);
}

static void glw_draw_line_cmd(const struct GlwCmd *c)
{
    const float verts[2][GLW_LINE_VFLOATS] = {
        { c->x0, c->y0, (float)c->color },
        { c->x1, c->y1, (float)c->color },
    };
    glUseProgram(gw.line_program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, gw.pal_tex);
    glUniform1i(gw.lu_pal, 0);
    glUniform2f(gw.lu_winsize, (float)gw.win_w_local, (float)gw.win_h_local);
    glBindVertexArray(gw.line_vao);
    glBindBuffer(GL_ARRAY_BUFFER, gw.line_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
    glDrawArrays(GL_LINES, 0, 2);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

/* ----------------------------------------------------------------------- */
/* Debug dump                                                               */
/* ----------------------------------------------------------------------- */

/** Write a raw PPM image (binary P6 format) to path.
 *  rgb_data must be a packed R8G8B8 buffer of w*h bytes (3 per pixel). */
static void glworld_write_ppm(const char *path,
    const unsigned char *rgb_data, int w, int h)
{
    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        LbErrorLog("glworld: debug_dump: cannot open '%s' for writing\n", path);
        return;
    }
    /* PPM header. */
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    /* Flip vertically: GL pixel (0,0) is bottom-left; image row 0 is top. */
    for (int row = h - 1; row >= 0; row--) {
        fwrite(rgb_data + (size_t)row * (size_t)w * 3u, 3u, (size_t)w, f);
    }
    fclose(f);
    LbSyncLog("glworld: debug_dump: wrote PPM %dx%d → %s\n", w, h, path);
}

void glworld_debug_dump(const char *path)
{
    if (!gw.inited) {
        LbWarnLog("glworld: debug_dump called but module not initialised\n");
        return;
    }
    if (path == NULL) {
        LbErrorLog("glworld: debug_dump: null path\n");
        return;
    }

    int w = gw.world_w;
    int h = gw.world_h;

    /* Allocate RGBA8 readback buffer (glReadPixels will convert from RGBA16F).
     * w*h*4 bytes; use size_t arithmetic to avoid 32-bit overflow on LP64. */
    size_t rgba_bytes = (size_t)w * (size_t)h * 4u;
    unsigned char *rgba = (unsigned char *)malloc(rgba_bytes);
    if (rgba == NULL) {
        LbErrorLog("glworld: debug_dump: out of memory (%zu bytes)\n",
            rgba_bytes);
        return;
    }

    /* Read pixels from the scene FBO. */
    glBindFramebuffer(GL_READ_FRAMEBUFFER, gw.scene_fbo);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);

    if (glworld_check_error("glworld_debug_dump: glReadPixels")) {
        LbErrorLog("glworld: debug_dump: glReadPixels reported errors\n");
        free(rgba);
        return;
    }

    /* Convert RGBA → RGB for the PPM writer. */
    size_t rgb_bytes = (size_t)w * (size_t)h * 3u;
    unsigned char *rgb = (unsigned char *)malloc(rgb_bytes);
    if (rgb == NULL) {
        LbErrorLog("glworld: debug_dump: out of memory for RGB buffer\n");
        free(rgba);
        return;
    }
    for (int i = 0; i < w * h; i++) {
        rgb[i * 3 + 0] = rgba[i * 4 + 0];
        rgb[i * 3 + 1] = rgba[i * 4 + 1];
        rgb[i * 3 + 2] = rgba[i * 4 + 2];
    }
    free(rgba);

    /* Write as PPM (simple, no extra deps beyond stdio). */
    glworld_write_ppm(path, rgb, w, h);
    free(rgb);
}

#else /* _WIN32: stubs — the module compiles to nothing on Windows. */

/* gl_world_active is defined above (outside the #ifndef) — no stub needed. */

#endif /* !_WIN32 */

#ifdef __cplusplus
}
#endif
