/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file frontmenu_saves.c
 *     GUI menus for saved games support (save and load screens).
 * @par Purpose:
 *     Functions to show and maintain menus used for saving and loading.
 * @par Comment:
 *     None.
 * @author   KeeperFX Team
 * @date     05 Jan 2009 - 09 Oct 2010
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "kfx/renderer/RendererManager.h"
#include "frontmenu_saves.h"
#include "globals.h"
#include "bflib_basics.h"

#include "bflib_guibtns.h"
#include "bflib_sprite.h"
#include "bflib_sprfnt.h"
#include "config_strings.h"
#include "config_translation.h"
#include "game_saves.h"
#include "gui_draw.h"
#include "gui_frontbtns.h"
#include "gui_soundmsgs.h"
#include "player_data.h"
#include "packets.h"
#include "frontend.h"
#include "front_input.h"
#include "gui_vscroll.h"
#include "game_legacy.h"
#include "kjm_input.h"
#include "sprites.h"
#include "keeperfx.hpp"
#include "post_inc.h"

/******************************************************************************/
int frontend_load_game_button_to_index(struct GuiButton *gbtn)
{
    long gbidx = gbtn->content.lval;
    int k = -1;
    for (int i = gbidx + load_game_scroll_offset - 45; i >= 0; i--)
    {
        struct CatalogueEntry* centry;
        do
        {
            k++;
            if (k >= save_game_catalogue_count)
                return -1;
            centry = &save_game_catalogue[k];
        } while ((centry->flags & CEF_InUse) == 0);
  }
  return k;
}

/** Maps a position in the compacted display order to an actual catalogue slot.
 *  The display packs entries as: every in-use slot in ascending slot order,
 *  then every free slot in ascending slot order - so deleting a save in the
 *  middle of the list closes the gap on screen without renumbering any file
 *  on disk. `target` is the row index plus the scroll offset. Returns
 *  save_game_catalogue_count (an intentionally out-of-range sentinel, not -1)
 *  when target has no entry - every call site already tests
 *  `slot_num < save_game_catalogue_count` before dereferencing, and a
 *  negative return would slip past that check instead of failing it. */
static long loadsave_compacted_slot(long target)
{
    long pos = 0;
    for (long slot = 0; slot < save_game_catalogue_count; slot++)
    {
        if ((save_game_catalogue[slot].flags & CEF_InUse) != 0)
        {
            if (pos == target)
                return slot;
            pos++;
        }
    }
    for (long slot = 0; slot < save_game_catalogue_count; slot++)
    {
        if ((save_game_catalogue[slot].flags & CEF_InUse) == 0)
        {
            if (pos == target)
                return slot;
            pos++;
        }
    }
    return save_game_catalogue_count;
}

/** Actual savegame slot shown in on-screen row btype_value (0..7), taking the
 *  scroll offset into account and packing the display per
 *  loadsave_compacted_slot(). */
static long loadsave_row_slot(const struct GuiButton *gbtn)
{
    long row = (gbtn != NULL) ? (gbtn->btype_value & LbBFeF_IntValueMask) : 0;
    return loadsave_compacted_slot(gui_vscroll_offset + row);
}

void gui_load_game_maintain(struct GuiButton *gbtn)
{
    long slot_num = loadsave_row_slot(gbtn);
    struct CatalogueEntry* centry = &save_game_catalogue[slot_num];
    if ((slot_num < save_game_catalogue_count) && ((centry->flags & CEF_InUse) != 0))
        gbtn->flags |= LbBtnF_Enabled;
    else
        gbtn->flags &=  ~LbBtnF_Enabled;
}

void gui_load_game(struct GuiButton *gbtn)
{
    struct PlayerInfo* player = get_my_player();
    long slot_num = loadsave_row_slot(gbtn);
    if (!load_game(slot_num))
    {
        // A save this build cannot read is not a reason to end the session.
        // load_game() refuses such a file before it disturbs anything, so the
        // game the player was already in is still there to go back to; say why
        // and leave them in the menus.
        WARNLOG("Loading game %d failed; staying in the menu.", (int)slot_num);
        create_error_box(save_load_failure_stridx());
        // Clicking a row fades the load menu out either way, so hand the pause
        // state back exactly as gui_save_game does - the load did not happen,
        // so the game must be left in the state the menu found it in.
        set_players_packet_action(player, PckA_UpdatePause, player->paused_state_restore, 0, 0, 0);
        return;
    }
}

void draw_load_button(struct GuiButton *gbtn)
{
    if (gbtn == NULL) return;
    int bs_units_per_px = simple_button_sprite_height_units_per_px(gbtn, GBS_frontend_button_std_c, 94);
    int width = gbtn->width;
    TbBool low_res = (MyScreenHeight < 400);
    if (low_res)
    {
        width += 32;
    }
    if ((gbtn->button_state_left_pressed) || (gbtn->button_state_right_pressed))
    {
        draw_bar64k(gbtn->scr_pos_x, gbtn->scr_pos_y, bs_units_per_px, width);
        int lit_width = gbtn->width + 6*units_per_pixel/16;
        if (low_res)
        {
            lit_width += 32;
        }
        draw_lit_bar64k(gbtn->scr_pos_x - 6*units_per_pixel/16, gbtn->scr_pos_y - 6*units_per_pixel/16, bs_units_per_px, lit_width);
    } else
    {
        draw_bar64k(gbtn->scr_pos_x, gbtn->scr_pos_y, bs_units_per_px, width);
    }
    if (gbtn->content.str != NULL)
    {
        snprintf(gui_textbuf, sizeof(gui_textbuf), "%s", gbtn->content.str);
        draw_button_string(gbtn, (gbtn->width*32 + 16)/gbtn->height, gui_textbuf);
    }
}

void gui_save_game(struct GuiButton *gbtn)
{
    struct PlayerInfo* player = get_my_player();
    if (strcasecmp(gbtn->content.str, get_string(GUIStr_SlotUnused)) != 0)
    {
        long slot_num = loadsave_row_slot(gbtn);
        fill_game_catalogue_slot(slot_num, gbtn->content.str);
        if (save_game(slot_num))
        {
            output_message(SMsg_GameSaved, 0);
        } else
      {
          ERRORLOG("Error in save!");
          create_error_box(GUIStr_ErrorSaving);
      }
  }
  set_players_packet_action(player, PckA_UpdatePause, player->paused_state_restore, 0, 0, 0);
}

void update_loadsave_input_strings(struct CatalogueEntry *game_catalg)
{
    SYNCDBG(6,"Starting");
    // Fill the 8 on-screen rows from the current scroll window [offset, offset+8),
    // through loadsave_compacted_slot() so the text on a row always matches the
    // slot loadsave_row_slot() will act on for that same row.
    for (long row = 0; row < GUI_VSCROLL_VISIBLE; row++)
    {
        long slot_num = loadsave_compacted_slot(gui_vscroll_offset + row);
        const char* text;
        if ((slot_num < save_game_catalogue_count) && ((game_catalg[slot_num].flags & CEF_InUse) != 0))
            text = game_catalg[slot_num].textname;
        else
            text = get_string(GUIStr_SlotUnused);
        snprintf(input_string[row], SAVE_TEXTNAME_LEN, "%s", text);
    }
}

/** Slot the pending delete confirmation refers to, or -1 when none is pending.
 *  The confirmation is a menu of its own, so the slot cannot simply be carried
 *  on the button that opened it. */
static long delete_save_slot_num = -1;
char delete_save_name[DELETE_SAVE_NAME_LEN] = "";

/** Tooltip string for the delete buttons, or 0 for none.
 *
 *  It comes from translation.toml rather than the compiled gtext tables, which
 *  are shipped binaries the fork does not generate. Resolved when either menu
 *  opens rather than cached for the session: campaigns, maps and mods may each
 *  add their own translation.toml, and that renumbers the table. */
static TextStringId delete_tooltip_stridx = 0;

/** Header line of the delete-confirmation dialog, or 0 to fall back to the
 *  stock GUIStr_ConfirmYouSure text. Deletion is recoverable (moved to the
 *  trash, not erased), so this dialog gets its own wording via translation.toml
 *  rather than reusing GUIStr_ConfirmYouSure -- that string is shared with the
 *  quit-game confirmation (frontmenu_ingame_opts_data.cpp), so it cannot be
 *  reworded here without also changing what quitting says. */
static TextStringId delete_confirm_stridx = 0;

static void resolve_delete_tooltip(void)
{
    TextStringId stridx = get_string_id_by_alias("DELETE_SAVED_GAME");
    // A mod shipping its own translation.toml without this entry should cost a
    // tooltip, not a wrong string.
    delete_tooltip_stridx = (stridx > 0) ? stridx : 0;

    stridx = get_string_id_by_alias("SAVE_TRASH_CONFIRM");
    delete_confirm_stridx = (stridx > 0) ? stridx : 0;
}

void gui_delete_confirm_header_maintain(struct GuiButton *gbtn)
{
    if (gbtn == NULL)
        return;
    // gui_area_text draws its label from tooltip_stridx, not a dedicated label
    // field -- see draw_delete_save_button's sibling gui_area_text callers.
    gbtn->tooltip_stridx = (delete_confirm_stridx > 0) ? delete_confirm_stridx : GUIStr_ConfirmYouSure;
}

void gui_delete_save_maintain(struct GuiButton *gbtn)
{
    if (gbtn == NULL)
        return;
    // Through loadsave_row_slot, not the raw row index: with the scrollbar the
    // button in row N acts on slot N + the scroll offset, and the catalogue is
    // sized to the saves that exist, so the row can point past its end.
    long slot_num = loadsave_row_slot(gbtn);
    struct CatalogueEntry* centry = &save_game_catalogue[slot_num];
    if ((slot_num < save_game_catalogue_count) && ((centry->flags & CEF_InUse) != 0))
        gbtn->flags |= LbBtnF_Enabled;
    else
        gbtn->flags &= ~LbBtnF_Enabled;
    // The button tables cannot name a translation.toml string, so the id is
    // filled in here once the table has been read.
    gbtn->tooltip_stridx = delete_tooltip_stridx;
}

void draw_delete_save_button(struct GuiButton *gbtn)
{
    if (gbtn == NULL)
        return;
    // A free slot has nothing to delete, so leave the margin empty rather than
    // showing an icon that does nothing.
    if ((gbtn->flags & LbBtnF_Enabled) == 0)
        return;
    int spr_idx = GPS_plyrsym_symbol_player_any_dead;
    int ps_units_per_px = simple_gui_panel_sprite_height_units_per_px(gbtn, spr_idx, 100);
    int cntr_x = gbtn->scr_pos_x + (gbtn->width >> 1);
    int cntr_y = gbtn->scr_pos_y + (gbtn->height >> 1);
    if ((gbtn->button_state_left_pressed) || (gbtn->button_state_right_pressed))
    {
        // The nudge the stock buttons use to acknowledge a press.
        cntr_x += 2*units_per_pixel/16;
        cntr_y += 2*units_per_pixel/16;
    }
    draw_gui_panel_sprite_centered(cntr_x, cntr_y, ps_units_per_px, spr_idx);
}

void gui_ask_delete_save(struct GuiButton *gbtn)
{
    if (gbtn == NULL)
        return;
    long slot_num = loadsave_row_slot(gbtn);
    // Buttons still fire their click handler while greyed out, so decide from
    // the catalogue rather than trusting the button state.
    if ((slot_num >= save_game_catalogue_count) || ((save_game_catalogue[slot_num].flags & CEF_InUse) == 0))
        return;
    delete_save_slot_num = slot_num;
    snprintf(delete_save_name, sizeof(delete_save_name), "%s", save_game_catalogue[slot_num].textname);
    create_menu(&delete_save_menu);
}

void gui_delete_save_confirmed(struct GuiButton *gbtn)
{
    long slot_num = delete_save_slot_num;
    delete_save_slot_num = -1;
    if (slot_num < 0)
        return;
    delete_save_game(slot_num);
    // Re-read from disk either way: on success the row reverts to "Unused" and
    // the skull greys out without leaving the menu, and on failure the menu
    // goes back to showing what is actually still there.
    load_game_save_catalogue();
    update_loadsave_input_strings(save_game_catalogue);
}

void gui_delete_save_cancelled(struct GuiButton *gbtn)
{
    delete_save_slot_num = -1;
}

void frontend_load_game(struct GuiButton *gbtn)
{
    int i = frontend_load_game_button_to_index(gbtn);
    if (i < 0)
        return;
    game.save_game_slot = i;
    if (is_save_game_loadable(i))
    {
        frontend_set_state(FeSt_LOAD_GAME);
  } else
  {
    save_catalogue_slot_disable(i);
    if (!initialise_load_game_slots())
      frontend_set_state(FeSt_MAIN_MENU);
  }
}

void frontend_draw_load_game_button(struct GuiButton *gbtn)
{
    int i = frontend_load_game_button_to_index(gbtn);
    if (i < 0)
        return;
    // Select font to draw
    int font_idx = frontend_button_caption_font(gbtn, frontend_mouse_over_button);
    LbTextSetFont(frontend_font[font_idx]);
    RendererSetDrawFlags(Lb_TEXT_HALIGN_LEFT);
    // Set drawing window and draw the text
    int tx_units_per_px = (gbtn->height * 13 / 11) * 16 / LbTextLineHeight();
    int height = LbTextLineHeight() * tx_units_per_px / 16;
    LbTextSetWindow(gbtn->scr_pos_x, gbtn->scr_pos_y, gbtn->width, height);
    LbTextDrawResized(0, 0, tx_units_per_px, save_game_catalogue[i].textname);
}

void frontend_load_game_up_maintain(struct GuiButton *gbtn)
{
    if (load_game_scroll_offset != 0)
    {
        gbtn->flags |= LbBtnF_Enabled;
    }
    else
    {
        gbtn->flags &=  ~LbBtnF_Enabled;
    }
    if (wheel_scrolled_up || (is_key_pressed(KC_UP,KMod_NONE)))
    {
        if (load_game_scroll_offset > 0)
        {
            load_game_scroll_offset--;
        }
    }
}

void frontend_load_game_down_maintain(struct GuiButton *gbtn)
{
    if (load_game_scroll_offset < number_of_saved_games-frontend_load_menu_items_visible+1)
    {
        gbtn->flags |= LbBtnF_Enabled;
    }
    else
    {
        gbtn->flags &=  ~LbBtnF_Enabled;
    }
    if (wheel_scrolled_down || (is_key_pressed(KC_DOWN,KMod_NONE)))
    {
        if (load_game_scroll_offset < number_of_saved_games-frontend_load_menu_items_visible+1)
        {
            load_game_scroll_offset++;
        }
    }
}

void frontend_load_game_up(struct GuiButton *gbtn)
{
  if (load_game_scroll_offset > 0)
    load_game_scroll_offset--;
}

void frontend_load_game_down(struct GuiButton *gbtn)
{
    if (load_game_scroll_offset < number_of_saved_games-frontend_load_menu_items_visible+1)
      load_game_scroll_offset++;
}

void frontend_load_game_scroll(struct GuiButton *gbtn)
{
    load_game_scroll_offset = frontend_scroll_tab_to_offset(gbtn, GetMouseY(), frontend_load_menu_items_visible-2, number_of_saved_games);
}

void frontend_draw_games_scroll_tab(struct GuiButton *gbtn)
{
    frontend_draw_scroll_tab(gbtn, load_game_scroll_offset, frontend_load_menu_items_visible-2, number_of_saved_games);
}

void init_load_menu(struct GuiMenu *gmnu)
{
  SYNCDBG(6,"Starting");
  resolve_delete_tooltip();
  struct PlayerInfo* player = get_my_player();
  set_players_packet_action(player, PckA_UpdatePause, 1, 1, 0, 0);
  load_game_save_catalogue();
  gui_vscroll_offset = 0;   // load list starts at the top
  update_loadsave_input_strings(save_game_catalogue);
}

void init_save_menu(struct GuiMenu *gmnu)
{
  SYNCDBG(6,"Starting");
  resolve_delete_tooltip();
  struct PlayerInfo* player = get_my_player();
  player->paused_state_restore = flag_is_set(game.operation_flags, GOF_Paused);
  set_players_packet_action(player, PckA_UpdatePause, 1, 1, 0, 0);
  load_game_save_catalogue();
  gui_vscroll_offset = 0;
  update_loadsave_input_strings(save_game_catalogue);
}
/******************************************************************************/
