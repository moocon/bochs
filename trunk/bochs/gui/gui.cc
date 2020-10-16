/////////////////////////////////////////////////////////////////////////
// $Id$
/////////////////////////////////////////////////////////////////////////
//
//  Copyright (C) 2002-2020  The Bochs Project
//
//  This library is free software; you can redistribute it and/or
//  modify it under the terms of the GNU Lesser General Public
//  License as published by the Free Software Foundation; either
//  version 2 of the License, or (at your option) any later version.
//
//  This library is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
//  Lesser General Public License for more details.
//
//  You should have received a copy of the GNU Lesser General Public
//  License along with this library; if not, write to the Free Software
//  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA


#include <signal.h>
#include "iodev.h"
#include "virt_timer.h"
#include "keymap.h"
#include "gui/bitmaps/floppya.h"
#include "gui/bitmaps/floppyb.h"
#include "gui/bitmaps/mouse.h"
#include "gui/bitmaps/reset.h"
#include "gui/bitmaps/power.h"
#include "gui/bitmaps/snapshot.h"
#include "gui/bitmaps/copy.h"
#include "gui/bitmaps/paste.h"
#include "gui/bitmaps/configbutton.h"
#include "gui/bitmaps/cdromd.h"
#include "gui/bitmaps/userbutton.h"
#include "gui/bitmaps/saverestore.h"

#if BX_USE_GUI_CONSOLE
#include "sdl.h"
#endif

#if BX_WITH_MACOS
#  include <Disks.h>
#endif

bx_gui_c *bx_gui = NULL;

#define BX_GUI_THIS bx_gui->
#define LOG_THIS BX_GUI_THIS

// Enable this if you want confirm quit after pressing power button.
//#define BX_GUI_CONFIRM_QUIT

// user button shortcut stuff

#define BX_KEY_UNKNOWN 0x7fffffff
#define N_USER_KEYS 38

typedef struct {
  const char *key;
  Bit32u symbol;
} user_key_t;

static user_key_t user_keys[N_USER_KEYS] =
{
  { "f1",    BX_KEY_F1 },
  { "f2",    BX_KEY_F2 },
  { "f3",    BX_KEY_F3 },
  { "f4",    BX_KEY_F4 },
  { "f5",    BX_KEY_F5 },
  { "f6",    BX_KEY_F6 },
  { "f7",    BX_KEY_F7 },
  { "f8",    BX_KEY_F8 },
  { "f9",    BX_KEY_F9 },
  { "f10",   BX_KEY_F10 },
  { "f11",   BX_KEY_F11 },
  { "f12",   BX_KEY_F12 },
  { "alt",   BX_KEY_ALT_L },
  { "bksl",  BX_KEY_BACKSLASH },
  { "bksp",  BX_KEY_BACKSPACE },
  { "ctrl",  BX_KEY_CTRL_L },
  { "del",   BX_KEY_DELETE },
  { "down",  BX_KEY_DOWN },
  { "end",   BX_KEY_END },
  { "enter", BX_KEY_ENTER },
  { "esc",   BX_KEY_ESC },
  { "home",  BX_KEY_HOME },
  { "ins",   BX_KEY_INSERT },
  { "left",  BX_KEY_LEFT },
  { "menu",  BX_KEY_MENU },
  { "minus", BX_KEY_MINUS },
  { "pgdwn", BX_KEY_PAGE_DOWN },
  { "pgup",  BX_KEY_PAGE_UP },
  { "plus",  BX_KEY_KP_ADD },
  { "right", BX_KEY_RIGHT },
  { "shift", BX_KEY_SHIFT_L },
  { "space", BX_KEY_SPACE },
  { "tab",   BX_KEY_TAB },
  { "up",    BX_KEY_UP },
  { "win",   BX_KEY_WIN_L },
  { "print", BX_KEY_PRINT },
  { "power", BX_KEY_POWER_POWER },
  { "scrlck", BX_KEY_SCRL_LOCK }
};

Bit32u get_user_key(char *key)
{
  int i = 0;

  while (i < N_USER_KEYS) {
    if (!strcmp(key, user_keys[i].key))
      return user_keys[i].symbol;
    i++;
  }
  return BX_KEY_UNKNOWN;
}

// font bitmap helper function

Bit8u reverse_bitorder(Bit8u b)
{
  Bit8u ret = 0;

  for (unsigned i = 0; i < 8; i++) {
    ret |= (b & 0x01) << (7 - i);
    b >>= 1;
  }
  return ret;
}

// common gui implementation

bx_gui_c::bx_gui_c(void): disp_mode(DISP_MODE_SIM)
{
  put("GUI"); // Init in specific_init
  bx_headerbar_entries = 0;
  statusitem_count = 0;
  led_timer_index = BX_NULL_TIMER_HANDLE;
  framebuffer = NULL;
  guest_textmode = 1;
  guest_fsize = (16 << 4) | 8;
  guest_xres = 640;
  guest_yres = 480;
  guest_bpp = 8;
  snapshot_mode = 0;
  snapshot_buffer = NULL;
  command_mode.present = 0;
  command_mode.active = 0;
  marker_count = 0;
  memset(palette, 0, sizeof(palette));
}

bx_gui_c::~bx_gui_c()
{
  if (framebuffer != NULL) {
    delete [] framebuffer;
  }
#if BX_USE_GUI_CONSOLE
  if (console.running) {
    console_cleanup();
  }
#endif
}

void bx_gui_c::init(int argc, char **argv, unsigned max_xres, unsigned max_yres,
                    unsigned tilewidth, unsigned tileheight)
{
  BX_GUI_THIS new_gfx_api = 0;
  BX_GUI_THIS host_xres = 640;
  BX_GUI_THIS host_yres = 480;
  BX_GUI_THIS host_bpp = 8;
  BX_GUI_THIS max_xres = max_xres;
  BX_GUI_THIS max_yres = max_yres;
  BX_GUI_THIS x_tilesize = tilewidth;
  BX_GUI_THIS y_tilesize = tileheight;
  BX_GUI_THIS dialog_caps = BX_GUI_DLG_RUNTIME | BX_GUI_DLG_SAVE_RESTORE;
#if BX_USE_GUI_CONSOLE
  BX_GUI_THIS console.present = 0;
  BX_GUI_THIS console.running = 0;
#endif
  BX_GUI_THIS toggle_method = SIM->get_param_enum(BXPN_MOUSE_TOGGLE)->get();
  BX_GUI_THIS toggle_keystate = 0;
  switch (toggle_method) {
    case BX_MOUSE_TOGGLE_CTRL_MB:
      strcpy(mouse_toggle_text, "CTRL + 3rd button");
      break;
    case BX_MOUSE_TOGGLE_CTRL_F10:
      strcpy(mouse_toggle_text, "CTRL + F10");
      break;
    case BX_MOUSE_TOGGLE_CTRL_ALT:
      strcpy(mouse_toggle_text, "CTRL + ALT");
      break;
    case BX_MOUSE_TOGGLE_F12:
      strcpy(mouse_toggle_text, "F12");
      break;
  }

  specific_init(argc, argv, BX_HEADER_BAR_Y);

  // Define some bitmaps to use in the headerbar
  BX_GUI_THIS floppyA_bmap_id = create_bitmap(bx_floppya_bmap,
                          BX_FLOPPYA_BMAP_X, BX_FLOPPYA_BMAP_Y);
  BX_GUI_THIS floppyA_eject_bmap_id = create_bitmap(bx_floppya_eject_bmap,
                          BX_FLOPPYA_BMAP_X, BX_FLOPPYA_BMAP_Y);
  BX_GUI_THIS floppyB_bmap_id = create_bitmap(bx_floppyb_bmap,
                          BX_FLOPPYB_BMAP_X, BX_FLOPPYB_BMAP_Y);
  BX_GUI_THIS floppyB_eject_bmap_id = create_bitmap(bx_floppyb_eject_bmap,
                          BX_FLOPPYB_BMAP_X, BX_FLOPPYB_BMAP_Y);
  BX_GUI_THIS cdrom1_bmap_id = create_bitmap(bx_cdromd_bmap,
                          BX_CDROMD_BMAP_X, BX_CDROMD_BMAP_Y);
  BX_GUI_THIS cdrom1_eject_bmap_id = create_bitmap(bx_cdromd_eject_bmap,
                          BX_CDROMD_BMAP_X, BX_CDROMD_BMAP_Y);
  BX_GUI_THIS mouse_bmap_id = create_bitmap(bx_mouse_bmap,
                          BX_MOUSE_BMAP_X, BX_MOUSE_BMAP_Y);
  BX_GUI_THIS nomouse_bmap_id = create_bitmap(bx_nomouse_bmap,
                          BX_MOUSE_BMAP_X, BX_MOUSE_BMAP_Y);

  BX_GUI_THIS power_bmap_id = create_bitmap(bx_power_bmap, BX_POWER_BMAP_X, BX_POWER_BMAP_Y);
  BX_GUI_THIS reset_bmap_id = create_bitmap(bx_reset_bmap, BX_RESET_BMAP_X, BX_RESET_BMAP_Y);
  BX_GUI_THIS snapshot_bmap_id = create_bitmap(bx_snapshot_bmap, BX_SNAPSHOT_BMAP_X, BX_SNAPSHOT_BMAP_Y);
  BX_GUI_THIS copy_bmap_id = create_bitmap(bx_copy_bmap, BX_COPY_BMAP_X, BX_COPY_BMAP_Y);
  BX_GUI_THIS paste_bmap_id = create_bitmap(bx_paste_bmap, BX_PASTE_BMAP_X, BX_PASTE_BMAP_Y);
  BX_GUI_THIS config_bmap_id = create_bitmap(bx_config_bmap, BX_CONFIG_BMAP_X, BX_CONFIG_BMAP_Y);
  BX_GUI_THIS user_bmap_id = create_bitmap(bx_user_bmap, BX_USER_BMAP_X, BX_USER_BMAP_Y);
  BX_GUI_THIS save_restore_bmap_id = create_bitmap(bx_save_restore_bmap,
                          BX_SAVE_RESTORE_BMAP_X, BX_SAVE_RESTORE_BMAP_Y);

  // Add the initial bitmaps to the headerbar, and enable callback routine, for use
  // when that bitmap is clicked on. The floppy and cdrom devices are not
  // initialized yet. so we just set the bitmaps to ejected for now.

  // Floppy A:
  BX_GUI_THIS floppyA_hbar_id = headerbar_bitmap(BX_GUI_THIS floppyA_eject_bmap_id,
                          BX_GRAVITY_LEFT, floppyA_handler);
  BX_GUI_THIS set_tooltip(BX_GUI_THIS floppyA_hbar_id, "Change floppy A: media");

  // Floppy B:
  BX_GUI_THIS floppyB_hbar_id = headerbar_bitmap(BX_GUI_THIS floppyB_eject_bmap_id,
                          BX_GRAVITY_LEFT, floppyB_handler);
  BX_GUI_THIS set_tooltip(BX_GUI_THIS floppyB_hbar_id, "Change floppy B: media");

  // First CD-ROM
  BX_GUI_THIS cdrom1_hbar_id = headerbar_bitmap(BX_GUI_THIS cdrom1_eject_bmap_id,
                          BX_GRAVITY_LEFT, cdrom1_handler);
  BX_GUI_THIS set_tooltip(BX_GUI_THIS cdrom1_hbar_id, "Change first CDROM media");

  // Mouse button
  if (SIM->get_param_bool(BXPN_MOUSE_ENABLED)->get())
    BX_GUI_THIS mouse_hbar_id = headerbar_bitmap(BX_GUI_THIS mouse_bmap_id,
                          BX_GRAVITY_LEFT, toggle_mouse_enable);
  else
    BX_GUI_THIS mouse_hbar_id = headerbar_bitmap(BX_GUI_THIS nomouse_bmap_id,
                          BX_GRAVITY_LEFT, toggle_mouse_enable);
  BX_GUI_THIS set_tooltip(BX_GUI_THIS mouse_hbar_id, "Enable mouse capture");

  // These are the buttons on the right side.  They are created in order
  // of right to left.

  // Power button
  BX_GUI_THIS power_hbar_id = headerbar_bitmap(BX_GUI_THIS power_bmap_id,
                          BX_GRAVITY_RIGHT, power_handler);
  BX_GUI_THIS set_tooltip(BX_GUI_THIS power_hbar_id, "Turn power off");
  // Save/Restore Button
  BX_GUI_THIS save_restore_hbar_id = headerbar_bitmap(BX_GUI_THIS save_restore_bmap_id,
                          BX_GRAVITY_RIGHT, save_restore_handler);
  BX_GUI_THIS set_tooltip(BX_GUI_THIS save_restore_hbar_id, "Save simulation state");
  // Reset button
  BX_GUI_THIS reset_hbar_id = headerbar_bitmap(BX_GUI_THIS reset_bmap_id,
                          BX_GRAVITY_RIGHT, reset_handler);
  BX_GUI_THIS set_tooltip(BX_GUI_THIS reset_hbar_id, "Reset the system");
  // Configure button
  BX_GUI_THIS config_hbar_id = headerbar_bitmap(BX_GUI_THIS config_bmap_id,
                          BX_GRAVITY_RIGHT, config_handler);
  BX_GUI_THIS set_tooltip(BX_GUI_THIS config_hbar_id, "Runtime config dialog");
  // Snapshot button
  BX_GUI_THIS snapshot_hbar_id = headerbar_bitmap(BX_GUI_THIS snapshot_bmap_id,
                          BX_GRAVITY_RIGHT, snapshot_handler);
  BX_GUI_THIS set_tooltip(BX_GUI_THIS snapshot_hbar_id, "Save snapshot of the Bochs screen");
  // Paste button
  BX_GUI_THIS paste_hbar_id = headerbar_bitmap(BX_GUI_THIS paste_bmap_id,
                          BX_GRAVITY_RIGHT, paste_handler);
  BX_GUI_THIS set_tooltip(BX_GUI_THIS paste_hbar_id, "Paste clipboard text as emulated keystrokes");
  // Copy button
  BX_GUI_THIS copy_hbar_id = headerbar_bitmap(BX_GUI_THIS copy_bmap_id,
                          BX_GRAVITY_RIGHT, copy_handler);
  BX_GUI_THIS set_tooltip(BX_GUI_THIS copy_hbar_id, "Copy text mode screen to the clipboard");
  // User button
  BX_GUI_THIS user_hbar_id = headerbar_bitmap(BX_GUI_THIS user_bmap_id,
                          BX_GRAVITY_RIGHT, userbutton_handler);
  BX_GUI_THIS set_tooltip(BX_GUI_THIS user_hbar_id, "Send keyboard shortcut");

  if (!parse_user_shortcut(SIM->get_param_string(BXPN_USER_SHORTCUT)->getptr())) {
    SIM->get_param_string(BXPN_USER_SHORTCUT)->set("none");
  }

  BX_GUI_THIS charmap_updated = 0;

  if (!BX_GUI_THIS new_gfx_api && (BX_GUI_THIS framebuffer == NULL)) {
    BX_GUI_THIS framebuffer = new Bit8u[max_xres * max_yres * 4];
  }
  show_headerbar();

  // register timer for status bar LEDs
  if (BX_GUI_THIS led_timer_index == BX_NULL_TIMER_HANDLE) {
    BX_GUI_THIS led_timer_index =
      bx_virt_timer.register_timer(this, led_timer_handler, 100000, 1, 1, 1,
                                   "status bar LEDs");
  }
}

void bx_gui_c::cleanup(void)
{
  statusitem_count = 0;
}

void bx_gui_c::update_drive_status_buttons(void)
{
  BX_GUI_THIS floppyA_status = (SIM->get_param_enum(BXPN_FLOPPYA_STATUS)->get() == BX_INSERTED);
  BX_GUI_THIS floppyB_status = (SIM->get_param_enum(BXPN_FLOPPYB_STATUS)->get() == BX_INSERTED);
  Bit32u handle = DEV_hd_get_first_cd_handle();
  BX_GUI_THIS cdrom1_status = DEV_hd_get_cd_media_status(handle);
  if (BX_GUI_THIS floppyA_status)
    replace_bitmap(BX_GUI_THIS floppyA_hbar_id, BX_GUI_THIS floppyA_bmap_id);
  else {
#if BX_WITH_MACOS
    // If we are using the Mac floppy driver, eject the disk
    // from the floppy drive.  This doesn't work in MacOS X.
    if (!strcmp(SIM->get_param_string(BXPN_FLOPPYA_PATH)->getptr(), SuperDrive))
      DiskEject(1);
#endif
    replace_bitmap(BX_GUI_THIS floppyA_hbar_id, BX_GUI_THIS floppyA_eject_bmap_id);
  }
  if (BX_GUI_THIS floppyB_status)
    replace_bitmap(BX_GUI_THIS floppyB_hbar_id, BX_GUI_THIS floppyB_bmap_id);
  else {
#if BX_WITH_MACOS
    // If we are using the Mac floppy driver, eject the disk
    // from the floppy drive.  This doesn't work in MacOS X.
    if (!strcmp(SIM->get_param_string(BXPN_FLOPPYB_PATH)->getptr(), SuperDrive))
      DiskEject(1);
#endif
    replace_bitmap(BX_GUI_THIS floppyB_hbar_id, BX_GUI_THIS floppyB_eject_bmap_id);
  }
  if (BX_GUI_THIS cdrom1_status)
    replace_bitmap(BX_GUI_THIS cdrom1_hbar_id, BX_GUI_THIS cdrom1_bmap_id);
  else {
    replace_bitmap(BX_GUI_THIS cdrom1_hbar_id, BX_GUI_THIS cdrom1_eject_bmap_id);
  }
}

void bx_gui_c::floppyA_handler(void)
{
  if (SIM->get_param_enum(BXPN_FLOPPYA_DEVTYPE)->get() == BX_FDD_NONE)
    return; // no primary floppy device present
  if (!BX_GUI_THIS fullscreen_mode &&
      (BX_GUI_THIS dialog_caps & BX_GUI_DLG_FLOPPY)) {
    // instead of just toggling the status, bring up a dialog asking what disk
    // image you want to switch to.
    int ret = SIM->ask_param(BXPN_FLOPPYA);
    if (ret > 0) {
      SIM->update_runtime_options();
    }
    return;
  }
  BX_GUI_THIS floppyA_status = !BX_GUI_THIS floppyA_status;
  DEV_floppy_set_media_status(0, BX_GUI_THIS floppyA_status);
  BX_GUI_THIS update_drive_status_buttons();
}

void bx_gui_c::floppyB_handler(void)
{
  if (SIM->get_param_enum(BXPN_FLOPPYB_DEVTYPE)->get() == BX_FDD_NONE)
    return; // no secondary floppy device present
  if (!BX_GUI_THIS fullscreen_mode &&
      (BX_GUI_THIS dialog_caps & BX_GUI_DLG_FLOPPY)) {
    // instead of just toggling the status, bring up a dialog asking what disk
    // image you want to switch to.
    int ret = SIM->ask_param(BXPN_FLOPPYB);
    if (ret > 0) {
      SIM->update_runtime_options();
    }
    return;
  }
  BX_GUI_THIS floppyB_status = !BX_GUI_THIS floppyB_status;
  DEV_floppy_set_media_status(1, BX_GUI_THIS floppyB_status);
  BX_GUI_THIS update_drive_status_buttons();
}

void bx_gui_c::cdrom1_handler(void)
{
  Bit32u handle = DEV_hd_get_first_cd_handle();
  if (BX_GUI_THIS dialog_caps & BX_GUI_DLG_CDROM) {
    // instead of just toggling the status, bring up a dialog asking what disk
    // image you want to switch to.
    // This code handles the first cdrom only. The cdrom drives #2, #3 and
    // #4 are handled in the runtime configuaration.
    bx_param_c *cdrom = SIM->get_first_cdrom();
    if (cdrom == NULL)
      return;  // no cdrom found
    int ret = SIM->ask_param(cdrom);
    if (ret > 0) {
      SIM->update_runtime_options();
    }
    return;
  }
  BX_GUI_THIS cdrom1_status =
    DEV_hd_set_cd_media_status(handle, !BX_GUI_THIS cdrom1_status);
  BX_GUI_THIS update_drive_status_buttons();
}

void bx_gui_c::reset_handler(void)
{
  BX_INFO(("system RESET callback"));
  bx_pc_system.Reset(BX_RESET_HARDWARE);
}

void bx_gui_c::power_handler(void)
{
#ifdef BX_GUI_CONFIRM_QUIT
  if (!SIM->ask_yes_no("Quit Bochs", "Are you sure ?", 0))
    return;
#endif
  // the user pressed power button, so there's no doubt they want bochs
  // to quit.  Change panics to fatal for the GUI and then do a panic.
  bx_user_quit = 1;
  BX_FATAL(("POWER button turned off."));
  // shouldn't reach this point, but if you do, QUIT!!!
  fprintf (stderr, "Bochs is exiting because you pressed the power button.\n");
  BX_EXIT(1);
}

void bx_gui_c::make_text_snapshot(char **snapshot, Bit32u *length)
{
  Bit8u* raw_snap = NULL;
  char *clean_snap;
  unsigned line_addr, txt_addr, txHeight, txWidth;

  DEV_vga_get_text_snapshot(&raw_snap, &txHeight, &txWidth);
  clean_snap = new char[txHeight*(txWidth+2)+1];
  txt_addr = 0;
  for (unsigned i=0; i<txHeight; i++) {
    line_addr = i * txWidth * 2;
    for (unsigned j=0; j<(txWidth*2); j+=2) {
      if (!raw_snap[line_addr+j])
        raw_snap[line_addr+j] = 0x20;
      clean_snap[txt_addr++] = raw_snap[line_addr+j];
    }
    while ((txt_addr > 0) && (clean_snap[txt_addr-1] == ' ')) txt_addr--;
#ifdef WIN32
    clean_snap[txt_addr++] = 13;
#endif
    clean_snap[txt_addr++] = 10;
  }
  clean_snap[txt_addr] = 0;
  *snapshot = clean_snap;
  *length = txt_addr;
}

Bit32u bx_gui_c::set_snapshot_mode(bx_bool mode)
{
  unsigned pixel_bytes, bufsize;

  BX_GUI_THIS snapshot_mode = mode;
  if (mode) {
    pixel_bytes = ((BX_GUI_THIS guest_bpp + 1) >> 3);
    bufsize = BX_GUI_THIS guest_xres * BX_GUI_THIS guest_yres * pixel_bytes;
    BX_GUI_THIS snapshot_buffer = new Bit8u[bufsize];
    if (BX_GUI_THIS snapshot_buffer != NULL) {
      memset(BX_GUI_THIS snapshot_buffer, 0, bufsize);
      DEV_vga_refresh(1);
      return bufsize;
    }
  } else {
    if (BX_GUI_THIS snapshot_buffer != NULL) {
      delete [] BX_GUI_THIS snapshot_buffer;
      BX_GUI_THIS snapshot_buffer = NULL;
      DEV_vga_redraw_area(0, 0, BX_GUI_THIS guest_xres, BX_GUI_THIS guest_yres);
    }
  }
  return 0;
}

// create a text snapshot and copy to the system clipboard.  On guis that
// we haven't figured out how to support yet, dump to a file instead.
void bx_gui_c::copy_handler(void)
{
  Bit32u len;
  char *text_snapshot;

  if (BX_GUI_THIS guest_textmode) {
    make_text_snapshot(&text_snapshot, &len);
    if (!BX_GUI_THIS set_clipboard_text(text_snapshot, len)) {
      // platform specific code failed, use portable code instead
      FILE *fp = fopen("copy.txt", "w");
      if (fp != NULL) {
        fwrite(text_snapshot, 1, len, fp);
        fclose(fp);
      }
    }
    delete [] text_snapshot;
  } else {
    BX_ERROR(("copy button failed, graphics mode not implemented"));
  }
}

#define BX_SNAPSHOT_TXT 0
#define BX_SNAPSHOT_BMP 1

// create a text snapshot and dump it to a file
void bx_gui_c::snapshot_handler(void)
{
  int fd, i, j, pitch;
  Bit8u *snapshot_ptr = NULL;
  Bit8u *row_buffer, *pixel_ptr, *row_ptr;
  Bit8u bmp_header[54], iBits, b1, b2;
  Bit32u ilen, len, rlen;
  char filename[BX_PATHNAME_LEN], *ext;
  Bit8u snap_fmt;

  if (BX_GUI_THIS guest_textmode) {
    strcpy(filename, "snapshot.txt");
    snap_fmt = BX_SNAPSHOT_TXT;
  } else {
    strcpy(filename, "snapshot.bmp");
    snap_fmt = BX_SNAPSHOT_BMP;
  }
  if (!BX_GUI_THIS fullscreen_mode &&
      (BX_GUI_THIS dialog_caps & BX_GUI_DLG_SNAPSHOT)) {
    int ret = SIM->ask_filename(filename, sizeof(filename),
                                "Save snapshot as...", filename,
                                bx_param_string_c::SAVE_FILE_DIALOG);
    if (ret < 0) { // cancelled
      return;
    }
    ext = strrchr(filename, '.');
    if (ext == NULL) {
      BX_ERROR(("Unknown snapshot file format"));
      return;
    } else {
      ext++;
      if (BX_GUI_THIS guest_textmode && !strcmp(ext, "txt")) {
        snap_fmt = BX_SNAPSHOT_TXT;
      } else if (!strcmp(ext, "bmp")) {
        snap_fmt = BX_SNAPSHOT_BMP;
      } else {
        BX_ERROR(("Unsupported snapshot file format '%s'", ext));
        return;
      }
    }
  }
  if (snap_fmt == BX_SNAPSHOT_BMP) {
    fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC
#ifdef O_BINARY
              | O_BINARY
#endif
              , S_IRUSR | S_IWUSR
              );
    if (fd < 0) {
      BX_ERROR(("snapshot button failed: cannot create BMP file"));
      return;
    }
    ilen =  BX_GUI_THIS set_snapshot_mode(1);
    if (ilen > 0) {
      BX_INFO(("GFX snapshot: %u x %u x %u bpp (%u bytes)", BX_GUI_THIS guest_xres,
               BX_GUI_THIS guest_yres, BX_GUI_THIS guest_bpp, ilen));
    } else {
      close(fd);
      BX_ERROR(("snapshot button failed: cannot allocate memory"));
      return;
    }
    iBits = (BX_GUI_THIS guest_bpp == 8) ? 8 : 24;
    rlen = (BX_GUI_THIS guest_xres * (iBits >> 3) + 3) & ~3;
    len = rlen * BX_GUI_THIS guest_yres + 54;
    if (BX_GUI_THIS guest_bpp == 8) {
      len += (256 * 4);
    }
    memset(bmp_header, 0, 54);
    bmp_header[0] = 0x42;
    bmp_header[1] = 0x4d;
    bmp_header[2] = len & 0xff;
    bmp_header[3] = (len >> 8) & 0xff;
    bmp_header[4] = (len >> 16) & 0xff;
    bmp_header[5] = (len >> 24) & 0xff;
    bmp_header[10] = 54;
    if (BX_GUI_THIS guest_bpp == 8) {
      bmp_header[11] = 4;
    }
    bmp_header[14] = 40;
    bmp_header[18] = BX_GUI_THIS guest_xres & 0xff;
    bmp_header[19] = (BX_GUI_THIS guest_xres >> 8) & 0xff;
    bmp_header[22] = BX_GUI_THIS guest_yres & 0xff;
    bmp_header[23] = (BX_GUI_THIS guest_yres >> 8) & 0xff;
    bmp_header[26] = 1;
    bmp_header[28] = iBits;
    write(fd, bmp_header, 54);
    if (BX_GUI_THIS guest_bpp == 8) {
      write(fd, BX_GUI_THIS palette, 256 * 4);
    }
    pitch = BX_GUI_THIS guest_xres * ((BX_GUI_THIS guest_bpp + 1) >> 3);
    row_buffer = new Bit8u[rlen];
    row_ptr = BX_GUI_THIS snapshot_buffer + ((BX_GUI_THIS guest_yres - 1) * pitch);
    for (i = BX_GUI_THIS guest_yres; i > 0; i--) {
      memset(row_buffer, 0, rlen);
      if ((BX_GUI_THIS guest_bpp == 8) || (BX_GUI_THIS guest_bpp == 24)) {
        memcpy(row_buffer, row_ptr, pitch);
      } else if ((BX_GUI_THIS guest_bpp == 15) || (BX_GUI_THIS guest_bpp == 16)) {
        pixel_ptr = row_ptr;
        for (j = 0; j < (int)(BX_GUI_THIS guest_xres * 3); j+=3) {
          b1 = *(pixel_ptr++);
          b2 = *(pixel_ptr++);
          *(row_buffer+j)   = (b1 << 3);
          if (BX_GUI_THIS guest_bpp == 15) {
            *(row_buffer+j+1) = ((b1 & 0xe0) >> 2) | (b2 << 6);
            *(row_buffer+j+2) = (b2 & 0x7c) << 1;
          } else {
            *(row_buffer+j+1) = ((b1 & 0xe0) >> 3) | (b2 << 5);
            *(row_buffer+j+2) = (b2 & 0xf8);
          }
        }
      } else if (BX_GUI_THIS guest_bpp == 32) {
        pixel_ptr = row_ptr;
        for (j = 0; j < (int)(BX_GUI_THIS guest_xres * 3); j+=3) {
          *(row_buffer+j)   = *(pixel_ptr++);
          *(row_buffer+j+1) = *(pixel_ptr++);
          *(row_buffer+j+2) = *(pixel_ptr++);
          pixel_ptr++;
        }
      }
      write(fd, row_buffer, rlen);
      row_ptr -= pitch;
    }
    delete [] row_buffer;
    close(fd);
    BX_GUI_THIS set_snapshot_mode(0);
  } else {
    make_text_snapshot((char**)&snapshot_ptr, &len);
    FILE *fp = fopen(filename, "wb");
    if (fp != NULL) {
      fwrite(snapshot_ptr, 1, len, fp);
      fclose(fp);
    } else {
      BX_ERROR(("snapshot button failed: cannot create text file"));
    }
    delete [] snapshot_ptr;
  }
}

// Read ASCII chars from the system clipboard and paste them into bochs.
// Note that paste cannot work with the key mapping tables loaded.
void bx_gui_c::paste_handler(void)
{
  Bit32s nbytes;
  Bit8u *bytes;
  if (!bx_keymap.isKeymapLoaded ()) {
    BX_ERROR (("keyboard_mapping disabled, so paste cannot work"));
    return;
  }
  if (!BX_GUI_THIS get_clipboard_text(&bytes, &nbytes)) {
    BX_ERROR (("paste not implemented on this platform"));
    return;
  }
  BX_INFO (("pasting %d bytes", nbytes));
  DEV_kbd_paste_bytes (bytes, nbytes);
}

void bx_gui_c::config_handler(void)
{
  if (BX_GUI_THIS dialog_caps & BX_GUI_DLG_RUNTIME) {
    SIM->configuration_interface(NULL, CI_RUNTIME_CONFIG);
  }
}

void bx_gui_c::toggle_mouse_enable(void)
{
  int old = SIM->get_param_bool(BXPN_MOUSE_ENABLED)->get();
  BX_DEBUG (("toggle mouse_enabled, now %d", !old));
  SIM->get_param_bool(BXPN_MOUSE_ENABLED)->set(!old);
}

bx_bool bx_gui_c::mouse_toggle_check(Bit32u key, bx_bool pressed)
{
  Bit32u newstate;
  bx_bool toggle = 0;

  if (console_running())
    return 0;
  newstate = toggle_keystate;
  if (pressed) {
    newstate |= key;
    if (newstate == toggle_keystate) return 0;
    switch (toggle_method) {
      case BX_MOUSE_TOGGLE_CTRL_MB:
        toggle = (newstate & BX_GUI_MT_CTRL_MB) == BX_GUI_MT_CTRL_MB;
        if (!toggle) {
          toggle = (newstate & BX_GUI_MT_CTRL_LRB) == BX_GUI_MT_CTRL_LRB;
        }
        break;
      case BX_MOUSE_TOGGLE_CTRL_F10:
        toggle = (newstate & BX_GUI_MT_CTRL_F10) == BX_GUI_MT_CTRL_F10;
        break;
      case BX_MOUSE_TOGGLE_CTRL_ALT:
        toggle = (newstate & BX_GUI_MT_CTRL_ALT) == BX_GUI_MT_CTRL_ALT;
        break;
      case BX_MOUSE_TOGGLE_F12:
        toggle = (newstate == BX_GUI_MT_F12);
        break;
    }
    toggle_keystate = newstate;
  } else {
    toggle_keystate &= ~key;
  }
  return toggle;
}

const char* bx_gui_c::get_toggle_info(void)
{
  return mouse_toggle_text;
}

Bit8u bx_gui_c::get_modifier_keys(void)
{
  if ((keymodstate & BX_MOD_KEY_CAPS) > 0) {
    return ((keymodstate & ~BX_MOD_KEY_CAPS) | BX_MOD_KEY_SHIFT);
  } else {
    return keymodstate;
  }
}

Bit8u bx_gui_c::set_modifier_keys(Bit8u modifier, bx_bool pressed)
{
  Bit8u newstate = keymodstate, changestate = 0;

  if (modifier == BX_MOD_KEY_CAPS) {
    if (pressed) {
      newstate ^= modifier;
    }
  } else {
    if (pressed) {
      newstate |= modifier;
    } else {
      newstate &= ~modifier;
    }
  }
  changestate = keymodstate ^ newstate;
  keymodstate = newstate;
  return changestate;
}

bx_bool bx_gui_c::parse_user_shortcut(const char *val)
{
  char *ptr, shortcut_tmp[512];
  Bit32u symbol;

  user_shortcut_len = 0;
  if ((strlen(val) == 0) || !strcmp(val, "none")) {
    return 1;
  } else {
    strcpy(shortcut_tmp, val);
    ptr = strtok(shortcut_tmp, "-");
    while (ptr) {
      symbol = get_user_key(ptr);
      if (symbol == BX_KEY_UNKNOWN) {
        BX_ERROR(("Unknown key symbol '%s' ignored", ptr));
        return 0;
      }
      if (user_shortcut_len < 3) {
        user_shortcut[user_shortcut_len++] = symbol;
        ptr = strtok(NULL, "-");
      } else {
        BX_ERROR(("Ignoring extra key symbol '%s'", ptr));
        break;
      }
    }
    return 1;
  }
}

void bx_gui_c::userbutton_handler(void)
{
  int i, ret = 1;

  if (!BX_GUI_THIS fullscreen_mode &&
      (BX_GUI_THIS dialog_caps & BX_GUI_DLG_USER)) {
    ret = SIM->ask_param(BXPN_USER_SHORTCUT);
  }
  if ((ret > 0) && (BX_GUI_THIS user_shortcut_len > 0)) {
    i = 0;
    while (i < BX_GUI_THIS user_shortcut_len) {
      DEV_kbd_gen_scancode(BX_GUI_THIS user_shortcut[i++]);
    }
    i--;
    while (i >= 0) {
      DEV_kbd_gen_scancode(BX_GUI_THIS user_shortcut[i--] | BX_KEY_RELEASED);
    }
  }
}

void bx_gui_c::save_restore_handler(void)
{
  int ret;
  char sr_path[BX_PATHNAME_LEN];

  if (BX_GUI_THIS dialog_caps & BX_GUI_DLG_SAVE_RESTORE) {
    BX_GUI_THIS set_display_mode(DISP_MODE_CONFIG);
    sr_path[0] = 0;
    ret = SIM->ask_filename(sr_path, sizeof(sr_path),
                            "Save Bochs state to folder...", "none",
                            bx_param_string_c::SELECT_FOLDER_DLG);
    if ((ret >= 0) && (strcmp(sr_path, "none"))) {
      if (SIM->save_state(sr_path)) {
        if (!SIM->ask_yes_no("WARNING",
              "The state of cpu, memory, devices and hard drive images is saved now.\n"
              "It is possible to continue, but when using the restore function in a\n"
              "new Bochs session, all changes after this checkpoint will be lost.\n\n"
              "Do you want to continue?", 0)) {
          power_handler();
        }
      }
    }
    BX_GUI_THIS set_display_mode(DISP_MODE_SIM);
  }
}

void bx_gui_c::marklog_handler(void)
{
  BX_INFO(("### MARKER #%u", BX_GUI_THIS marker_count++));
}

void bx_gui_c::headerbar_click(int x)
{
  int xorigin;

  for (unsigned i=0; i<bx_headerbar_entries; i++) {
    if (bx_headerbar_entry[i].alignment == BX_GRAVITY_LEFT)
      xorigin = bx_headerbar_entry[i].xorigin;
    else
      xorigin = guest_xres - bx_headerbar_entry[i].xorigin;
    if ((x>=xorigin) && (x<(xorigin+int(bx_headerbar_entry[i].xdim)))) {
      if (console_running() && (i != power_hbar_id))
        return;
      bx_headerbar_entry[i].f();
      return;
    }
  }
}

void bx_gui_c::mouse_enabled_changed(bx_bool val)
{
  // This is only called when SIM->get_init_done is 1.  Note that VAL
  // is the new value of mouse_enabled, which may not match the old
  // value which is still in SIM->get_param_bool(BXPN_MOUSE_ENABLED)->get().
  BX_DEBUG (("replacing the mouse bitmaps"));
  if (val)
    BX_GUI_THIS replace_bitmap(BX_GUI_THIS mouse_hbar_id, BX_GUI_THIS mouse_bmap_id);
  else
    BX_GUI_THIS replace_bitmap(BX_GUI_THIS mouse_hbar_id, BX_GUI_THIS nomouse_bmap_id);
  // give the GUI a chance to respond to the event.  Most guis will hide
  // the native mouse cursor and do something to trap the mouse inside the
  // bochs VGA display window.
  BX_GUI_THIS mouse_enabled_changed_specific (val);
}

void bx_gui_c::init_signal_handlers()
{
#if BX_GUI_SIGHANDLER
  if (bx_gui_sighandler)
  {
    Bit32u mask = bx_gui->get_sighandler_mask ();
    for (Bit32u sig=0; sig<32; sig++)
    {
      if (mask & (1<<sig))
        signal (sig, bx_signal_handler);
    }
  }
#endif
}

void bx_gui_c::set_text_charmap(Bit8u *fbuffer)
{
  memcpy(& BX_GUI_THIS vga_charmap, fbuffer, 0x2000);
  for (unsigned i=0; i<256; i++) BX_GUI_THIS char_changed[i] = 1;
  BX_GUI_THIS charmap_updated = 1;
}

void bx_gui_c::set_text_charbyte(Bit16u address, Bit8u data)
{
  BX_GUI_THIS vga_charmap[address] = data;
  BX_GUI_THIS char_changed[address >> 5] = 1;
  BX_GUI_THIS charmap_updated = 1;
}

void bx_gui_c::beep_on(float frequency)
{
  BX_DEBUG(("GUI Beep ON (frequency=%.2f)", frequency));
}

void bx_gui_c::beep_off()
{
  BX_DEBUG(("GUI Beep OFF"));
}

int bx_gui_c::register_statusitem(const char *text, bx_bool auto_off)
{
  unsigned id = statusitem_count;

  for (unsigned i = 0; i < statusitem_count; i++) {
    if (!statusitem[i].in_use) {
      id = i;
      break;
    }
  }
  if (id == statusitem_count) {
    if (++statusitem_count > BX_MAX_STATUSITEMS) {
      return -1;
    }
  }
  statusitem[id].in_use = 1;
  strncpy(statusitem[id].text, text, 8);
  statusitem[id].text[7] = 0;
  statusitem[id].auto_off = auto_off;
  statusitem[id].counter = 0;
  statusitem[id].active = 0;
  statusitem[id].mode = 0;
  statusbar_setitem_specific(id, 0, 0);
  return id;
}

void bx_gui_c::unregister_statusitem(int id)
{
  if ((id >= 0) && (id < (int)statusitem_count)) {
    strcpy(statusitem[id].text, "      ");
    statusbar_setitem_specific(id, 0, 0);
    if (id == (int)(statusitem_count - 1)) {
      statusitem_count--;
    } else {
      statusitem[id].in_use = 0;
    }
  }
}

void bx_gui_c::statusbar_setitem(int element, bx_bool active, bx_bool w)
{
  if (element < 0) {
    for (unsigned i = 0; i < statusitem_count; i++) {
      statusbar_setitem_specific(i, 0, 0);
    }
  } else if ((unsigned)element < statusitem_count) {
    if ((active != statusitem[element].active) ||
        (w != statusitem[element].mode)) {
      statusbar_setitem_specific(element, active, w);
      statusitem[element].active = active;
      statusitem[element].mode = w;
    }
    if (active && statusitem[element].auto_off) {
      statusitem[element].counter = 5;
    }
  }
}

void bx_gui_c::led_timer_handler(void *this_ptr)
{
  bx_gui_c *class_ptr = (bx_gui_c *) this_ptr;
  class_ptr->led_timer();
}

void bx_gui_c::led_timer()
{
  for (unsigned i = 0; i < statusitem_count; i++) {
    if (statusitem[i].auto_off) {
      if (statusitem[i].counter > 0) {
        if (!(--statusitem[i].counter)) {
          statusbar_setitem(i, 0);
        }
      }
    }
  }
}

void bx_gui_c::get_capabilities(Bit16u *xres, Bit16u *yres, Bit16u *bpp)
{
  *xres = 1024;
  *yres = 768;
  *bpp = 32;
}

bx_bool bx_gui_c::palette_change_common(Bit8u index, Bit8u red, Bit8u green, Bit8u blue)
{
  BX_GUI_THIS palette[index].red = red;
  BX_GUI_THIS palette[index].green = green;
  BX_GUI_THIS palette[index].blue = blue;
  return palette_change(index, red, green, blue);
}

void bx_gui_c::show_ips(Bit32u ips_count)
{
#if BX_SHOW_IPS
  BX_INFO(("ips = %3.3fM", ips_count / 1000000.0));
#endif
}

Bit8u bx_gui_c::get_mouse_headerbar_id()
{
  return BX_GUI_THIS mouse_hbar_id;
}

#if BX_DEBUGGER && BX_DEBUGGER_GUI
void bx_gui_c::init_debug_dialog()
{
  extern void InitDebugDialog();
  InitDebugDialog();
}

void bx_gui_c::close_debug_dialog()
{
  extern void CloseDebugDialog();
  CloseDebugDialog();
}
#endif

// new graphics API (compatibility code)

bx_svga_tileinfo_t *bx_gui_c::graphics_tile_info(bx_svga_tileinfo_t *info)
{
  BX_GUI_THIS host_pitch = BX_GUI_THIS host_xres * ((BX_GUI_THIS host_bpp + 1) >> 3);

  info->bpp = BX_GUI_THIS host_bpp;
  info->pitch = BX_GUI_THIS host_pitch;
  switch (info->bpp) {
    case 15:
      info->red_shift = 15;
      info->green_shift = 10;
      info->blue_shift = 5;
      info->red_mask = 0x7c00;
      info->green_mask = 0x03e0;
      info->blue_mask = 0x001f;
      break;
    case 16:
      info->red_shift = 16;
      info->green_shift = 11;
      info->blue_shift = 5;
      info->red_mask = 0xf800;
      info->green_mask = 0x07e0;
      info->blue_mask = 0x001f;
      break;
    case 24:
    case 32:
      info->red_shift = 24;
      info->green_shift = 16;
      info->blue_shift = 8;
      info->red_mask = 0xff0000;
      info->green_mask = 0x00ff00;
      info->blue_mask = 0x0000ff;
      break;
  }
  info->is_indexed = (BX_GUI_THIS host_bpp == 8);
#ifdef BX_LITTLE_ENDIAN
  info->is_little_endian = 1;
#else
  info->is_little_endian = 0;
#endif

  return info;
}

Bit8u *bx_gui_c::graphics_tile_get(unsigned x0, unsigned y0,
                            unsigned *w, unsigned *h)
{
  if (x0+BX_GUI_THIS x_tilesize > BX_GUI_THIS host_xres) {
    *w = BX_GUI_THIS host_xres - x0;
  } else {
    *w = BX_GUI_THIS x_tilesize;
  }

  if (y0+BX_GUI_THIS y_tilesize > BX_GUI_THIS host_yres) {
    *h = BX_GUI_THIS host_yres - y0;
  } else {
    *h = BX_GUI_THIS y_tilesize;
  }

  return (Bit8u *)framebuffer + y0 * BX_GUI_THIS host_pitch +
                  x0 * ((BX_GUI_THIS host_bpp + 1) >> 3);
}

void bx_gui_c::graphics_tile_update_in_place(unsigned x0, unsigned y0,
                                        unsigned w, unsigned h)
{
  Bit8u *tile;
  Bit8u *tile_ptr, *fb_ptr;
  Bit16u xc, yc, fb_pitch, tile_pitch;
  Bit8u r, diffx, diffy;

  tile = new Bit8u[BX_GUI_THIS x_tilesize * BX_GUI_THIS y_tilesize * 4];
  diffx = (x0 % BX_GUI_THIS x_tilesize);
  diffy = (y0 % BX_GUI_THIS y_tilesize);
  if (diffx > 0) {
    x0 -= diffx;
    w += diffx;
  }
  if (diffy > 0) {
    y0 -= diffy;
    h += diffy;
  }
  fb_pitch = BX_GUI_THIS host_pitch;
  tile_pitch = BX_GUI_THIS x_tilesize * ((BX_GUI_THIS host_bpp + 1) >> 3);
  for (yc=y0; yc<(y0+h); yc+=BX_GUI_THIS y_tilesize) {
    for (xc=x0; xc<(x0+w); xc+=BX_GUI_THIS x_tilesize) {
      fb_ptr = BX_GUI_THIS framebuffer + (yc * fb_pitch + xc * ((BX_GUI_THIS host_bpp + 1) >> 3));
      tile_ptr = &tile[0];
      for (r=0; r<h; r++) {
        memcpy(tile_ptr, fb_ptr, tile_pitch);
        fb_ptr += fb_pitch;
        tile_ptr += tile_pitch;
      }
      BX_GUI_THIS graphics_tile_update(tile, xc, yc);
    }
  }
  delete [] tile;
}

void bx_gui_c::text_update_common(Bit8u *old_text, Bit8u *new_text,
                                  Bit16u cursor_address,
                                  bx_vga_tminfo_t *tm_info)
{
  Bit16u cursor_x, cursor_y;
  Bit16u curs, rows, hchars, text_cols, font_row, mask, offset;
  Bit8u fheight, fwidth, fontline, fontrows, fontpixels, fgcolor, bgcolor;
  Bit8u *new_line, *font_ptr, *buf, *buf_row, *buf_char;
  bx_bool cursor_visible, dwidth, gfxcharw9, invert;

  if (cursor_address >= tm_info->start_address) {
    cursor_x = ((cursor_address - tm_info->start_address) % tm_info->line_offset) / 2;
    cursor_y = ((cursor_address - tm_info->start_address) / tm_info->line_offset);
  } else {
    cursor_x = 0xffff;
    cursor_y = 0xffff;
  }
  if (BX_GUI_THIS snapshot_mode) {
    if (BX_GUI_THIS snapshot_buffer != NULL) {
      fheight = BX_GUI_THIS guest_fsize >> 4;
      fwidth = BX_GUI_THIS guest_fsize & 0x0f;
      dwidth = (fwidth > 9);
      rows = BX_GUI_THIS guest_yres / fheight;
      text_cols = BX_GUI_THIS guest_xres / fwidth;
      cursor_visible = ((tm_info->cs_start <= tm_info->cs_end) && (tm_info->cs_start < fheight));
      if ((cursor_visible) && (cursor_y < rows) && (cursor_x < text_cols)) {
        curs = cursor_address;
      } else {
        curs = 0xffff;
      }
      offset = tm_info->start_address;
      buf_row = BX_GUI_THIS snapshot_buffer;
      do {
        buf = buf_row;
        hchars = text_cols;
        new_line = new_text;
        do {
          font_ptr = &vga_charmap[new_text[0] << 5];
          gfxcharw9 = ((tm_info->line_graphics) && ((new_text[0] & 0xE0) == 0xC0));
          fgcolor = tm_info->actl_palette[new_text[1] & 0x0f];
          bgcolor = tm_info->actl_palette[(new_text[1] >> 4) & 0x07];
          invert = (offset == curs);
          fontrows = fheight;
          fontline = 0;
          buf_char = buf;
          do {
            font_row = *font_ptr++;
            if (gfxcharw9) {
              font_row = (font_row << 1) | (font_row & 0x01);
            } else {
              font_row <<= 1;
            }
            fontpixels = fwidth;
            if ((invert) && (fontline >= tm_info->cs_start) && (fontline <= tm_info->cs_end))
              mask = 0x100;
            else
              mask = 0x00;
            do {
              if ((font_row & 0x100) == mask)
                *buf = bgcolor;
              else
                *buf = fgcolor;
              buf++;
              if (!dwidth || (fontpixels & 1)) font_row <<= 1;
            } while (--fontpixels);
            buf -= fwidth;
            buf += BX_GUI_THIS guest_xres;
            fontline++;
          } while (--fontrows);
          buf = buf_char;
          buf += fwidth;
          new_text += 2;
          offset += 2;
        } while (--hchars);
        buf_row += (BX_GUI_THIS guest_xres * fheight);
        new_text = new_line + tm_info->line_offset;
        offset += tm_info->line_offset;
      } while (--rows);
    }
  } else {
    text_update(old_text, new_text, cursor_x, cursor_y, tm_info);
  }
}

void bx_gui_c::graphics_tile_update_common(Bit8u *tile, unsigned x, unsigned y)
{
  unsigned i, pitch, pixel_bytes, nbytes, tilebytes;
  Bit8u *src, *dst;

  if (BX_GUI_THIS snapshot_mode) {
    if (BX_GUI_THIS snapshot_buffer != NULL) {
      pixel_bytes = ((BX_GUI_THIS guest_bpp + 1) >> 3);
      tilebytes = BX_GUI_THIS x_tilesize * pixel_bytes;
      if ((x + BX_GUI_THIS x_tilesize) <= BX_GUI_THIS guest_xres) {
        nbytes = tilebytes;
      } else {
        nbytes = (BX_GUI_THIS guest_xres - x) * pixel_bytes;
      }
      pitch = BX_GUI_THIS guest_xres * pixel_bytes;
      src = tile;
      dst = BX_GUI_THIS snapshot_buffer + (y * pitch) + x;
      for (i = 0; i < y_tilesize; i++) {
        memcpy(dst, src, nbytes);
        src += tilebytes;
        dst += pitch;
        if (++y >= BX_GUI_THIS guest_yres) break;
      }
    }
  } else {
    graphics_tile_update(tile, x, y);
  }
}

bx_svga_tileinfo_t * bx_gui_c::graphics_tile_info_common(bx_svga_tileinfo_t *info)
{
  if (!info) {
    info = new bx_svga_tileinfo_t;
    if (!info) {
      return NULL;
    }
  }
  info->snapshot_mode = BX_GUI_THIS snapshot_mode;
  if (BX_GUI_THIS snapshot_mode) {
    info->pitch = BX_GUI_THIS guest_xres * ((BX_GUI_THIS guest_bpp + 1) >> 3);
  } else {
    return graphics_tile_info(info);
  }

  return info;
}

#if BX_USE_GUI_CONSOLE

#define BX_CONSOLE_BUFSIZE 4000

void bx_gui_c::console_init(void)
{
  int i;

  console.screen = new Bit8u[BX_CONSOLE_BUFSIZE];
  console.oldscreen = new Bit8u[BX_CONSOLE_BUFSIZE];
  for (i = 0; i < BX_CONSOLE_BUFSIZE; i+=2) {
    console.screen[i] = 0x20;
    console.screen[i+1] = 0x07;
  }
  memset(console.oldscreen, 0xff, BX_CONSOLE_BUFSIZE);
  console.saved_xres = guest_xres;
  console.saved_yres = guest_yres;
  console.saved_bpp = guest_bpp;
  console.saved_fsize = guest_fsize;
  for (i = 0; i < 256; i++) {
    memcpy(&BX_GUI_THIS vga_charmap[0]+i*32, &sdl_font8x16[i], 16);
    BX_GUI_THIS char_changed[i] = 1;
  }
  BX_GUI_THIS charmap_updated = 1;
  console.cursor_x = 0;
  console.cursor_y = 0;
  memset(&console.tminfo, 0, sizeof(bx_vga_tminfo_t));
  console.tminfo.line_offset = 160;
  console.tminfo.line_compare = 1023;
  console.tminfo.cs_start = 0x2e;
  console.tminfo.cs_end = 0x0f;
  console.tminfo.actl_palette[7] = 0x07;
  memcpy(console.saved_palette, palette, 32);
  palette_change_common(0x00, 0x00, 0x00, 0x00);
  palette_change_common(0x07, 0xa8, 0xa8, 0xa8);
  dimension_update(720, 400, 16, 9, 8);
  console.n_keys = 0;
  console.running = 1;
}

void bx_gui_c::console_cleanup(void)
{
  delete [] console.screen;
  delete [] console.oldscreen;
  palette_change_common(0x00, console.saved_palette[2], console.saved_palette[1],
                        console.saved_palette[0]);
  palette_change_common(0x07, console.saved_palette[30], console.saved_palette[29],
                        console.saved_palette[28]);
  unsigned fheight = (console.saved_fsize >> 4);
  unsigned fwidth = (console.saved_fsize & 0x0f);
  dimension_update(console.saved_xres, console.saved_yres, fheight, fwidth,
                   console.saved_bpp);
  console.running = 0;
}

void bx_gui_c::console_refresh(bx_bool force)
{
  if (force) memset(console.oldscreen, 0xff, BX_CONSOLE_BUFSIZE);
  text_update(console.oldscreen, console.screen, console.cursor_x,
              console.cursor_y, &console.tminfo);
  flush();
  memcpy(console.oldscreen, console.screen, BX_CONSOLE_BUFSIZE);
}

void bx_gui_c::console_key_enq(Bit8u key)
{
  if (console.n_keys < 16) {
    console.keys[console.n_keys++] = key;
  }
}

int bx_gui_c::bx_printf(const char *s)
{
  unsigned offset;

  if (!console.running) {
    console_init();
  }
  for (unsigned i = 0; i < strlen(s); i++) {
    offset = console.cursor_y * 160 + console.cursor_x * 2;
    if ((s[i] != 0x08) && (s[i] != 0x0a)) {
      console.screen[offset] = s[i];
      console.screen[offset+1] = 0x07;
      console.cursor_x++;
    }
    if ((s[i] == 0x0a) || (console.cursor_x == 80)) {
      console.cursor_x = 0;
      console.cursor_y++;
    }
    if (s[i] == 0x08) {
      if (offset > 0) {
        console.screen[offset-2] = 0x20;
        console.screen[offset-1] = 0x07;
        if (console.cursor_x > 0) {
          console.cursor_x--;
        } else {
          console.cursor_x = 79;
          console.cursor_y--;
        }
      }
    }
    if (console.cursor_y == 25) {
      memmove(console.screen, console.screen+160, BX_CONSOLE_BUFSIZE-160);
      console.cursor_y--;
      offset = console.cursor_y * 160 + console.cursor_x * 2;
      for (int j = 0; j < 160; j+=2) {
        console.screen[offset+j] = 0x20;
        console.screen[offset+j+1] = 0x07;
      }
    }
  }
  console_refresh(0);
  return strlen(s);
}

char* bx_gui_c::bx_gets(char *s, int size)
{
  char keystr[2];
  int pos = 0, done = 0;
  int cs_counter = 1, cs_visible = 0;

  set_console_edit_mode(1);
  keystr[1] = 0;
  do {
    handle_events();
    while (console.n_keys > 0) {
      if ((console.keys[0] >= 0x20) && (pos < (size-1))) {
        s[pos++] = console.keys[0];
        keystr[0] = console.keys[0];
        bx_printf(keystr);
      } else if (console.keys[0] == 0x0d) {
        s[pos] = 0x00;
        keystr[0] = 0x0a;
        bx_printf(keystr);
        done = 1;
      } else if ((console.keys[0] == 0x08) && (pos > 0)) {
        pos--;
        keystr[0] = 0x08;
        bx_printf(keystr);
      }
      memmove(&console.keys[0], &console.keys[1], 15);
      console.n_keys--;
    }
#if BX_HAVE_USLEEP
    usleep(25000);
#else
    msleep(25);
#endif
    if (--cs_counter == 0) {
      cs_counter = 10;
      cs_visible ^= 1;
      if (cs_visible) {
        console.tminfo.cs_start &= ~0x20;
      } else {
        console.tminfo.cs_start |= 0x20;
      }
      console_refresh(0);
    }
  } while (!done);
  console.tminfo.cs_start |= 0x20;
  set_console_edit_mode(0);
  return s;
}
#endif

void bx_gui_c::set_command_mode(bx_bool active)
{
  if (command_mode.present) {
    command_mode.active = active;
  }
}
