//EEGL - the Extensible development Environment for GNU/Linux
//Licensed under GPLv3, see the LICENSE file (c) Egor Sozonov

//## window.c: functions for displaying the window in Wayland

#include "eegl.h"

#ifndef PROTO
// for shm_open:
#include <sys/mman.h>
#include <fcntl.h>
int fstat(int fd, struct stat* statbuf); //from sys/stat.h
int stat(const char* restrict path, struct stat* restrict buf);

#include <wayland-client.h>
#include "../libs/wayland/ext-data-control-v1.h"
#include "../libs/wayland/xdg-shell.h"
#include "../libs/wayland/primary-selection-unstable-v1.h"
#endif

// Struct that represents a seat. (Should be accessed via vwl_get_seat()).
typedef struct {
   struct wl_seat* proxy;
   char* label;      // Name of seat as text (e.g. seat0, seat1...).
   Unt capabilities;  // Bitmask of the capabilites of the seat (pointer, keyboard, touch)
} WaylandSeat;

// Global objects
typedef struct {
   // Data control protocols
   struct ext_data_control_manager_v1* ext_data_control_manager_v1;
   struct wl_data_device_manager* wl_data_device_manager;
   struct wl_shm* wl_shm;
   struct wl_compositor* wl_compositor;
   struct xdg_wm_base* xdg_wm_base;
   struct zwp_primary_selection_device_manager_v1* zwp_primary_selection_device_manager_v1;
} GlobalObjects;

// Struct wrapper for Wayland display and registry
typedef struct {
   struct wl_display* proxy;
   int fd;   // File descriptor for display

   struct {
      struct wl_registry *proxy;
   } registry;
} WaylandDisplay;

typedef struct {
   struct wl_shm_pool* pool;
   int fd;

   struct wl_buffer* buffer;
   Boole available;

   int width;
   int height;
   int stride;
   int size;
} BufferStore;

typedef struct {
   void* user_data;
   void (*on_focus)(void *data, Unt serial);

   struct wl_surface* surface;
   struct wl_keyboard* keyboard;

   struct {
      struct xdg_surface* surface;
      struct xdg_toplevel* toplevel;
   } shell;

   int got_focus;
} vwl_fs_surface_T; // fs = focus steal

// Wayland protocols for accessing the selection
typedef enum {
   VWL_DATA_PROTOCOL_NONE,
   VWL_DATA_PROTOCOL_EXT,
   VWL_DATA_PROTOCOL_CORE,
   VWL_DATA_PROTOCOL_PRIMARY
} DataProtocol;

// DATA RELATED OBJECT WRAPPERS
// These wrap around a proxy and act as a generic container.
// The `data` member is used to pass other needed stuff around such as a
// WaylandClipboardSelection pointer.

typedef struct {
   void* proxy;
   void* data; // Is not set when a new offer is created on a
               // data_offer event. Only set when listening to a data offer.
   DataProtocol protocol;
} DataOffer;

typedef struct {
   void* proxy;
   void* data;
   DataProtocol protocol;
} DataSource;

typedef struct {
   void* proxy;
   void* data;
   DataProtocol protocol;
} DataDevice;

typedef struct {
   void* proxy;
   DataProtocol protocol;
} vwl_data_device_manager_T;

// LISTENER WRAPPERS

typedef struct {
   void (*data_offer)(DataDevice *device, DataOffer *offer);

   // If the protocol that the data device uses doesn't support a specific
   // selection, then this callback will never be called with that selection.
   void (*selection)(
      DataDevice *device,
      DataOffer *offer,
      WaylandSelection selection);

   // This event is only relevant for data control protocols
   void (*finished)(DataDevice *device);
} vwl_data_device_Listener;

typedef struct {
   void (*send)(DataSource* source, char const* mime_type, int fd);
   void (*cancelled)(DataSource* source);
} DataSourceListener;

typedef struct {
   void (*offer)(DataOffer *offer, char const* mime_type);
} DataOfferListener;

typedef struct {
   // What selection this refers to
   WaylandSelection      selection;

   // Do not destroy here
   vwl_data_device_manager_T   manager;

   DataDevice device;
   DataSource      source;
   DataOffer* offer;   // Current offer for the selection

   ArrayList         mime_types;   // Mime types supported by the current offer

   ArrayList         tmp_mime_types;   // Temporary array for mime types when we are receiving
                  // them. When the selection event arrives and it is the
                  // one we want, then copy it over to mime_types

   // To be populated by callbacks from outside this file
   wayland_cb_send_data_func_T          send_cb;
   wayland_cb_selection_cancelled_func_T   cancelled_cb;

   int requires_focus;      // If focus needs to be given to us to work
} WaylandClipboardSelection;

// Holds stuff related to the clipboard/selections
typedef struct {
   // Do not destroy here, will be destroyed when vwl_disconnect_display() is called.
   WaylandSeat         *seat;

   WaylandClipboardSelection   regular;
   WaylandClipboardSelection   primary;

   BufferStore      *fs_buffer;
} vwl_clipboard_T;

//{{{forward declarations

private void clip_own_selection(ClipBoard *cbd);
private void startSelection(int col, int row, int repeated_click);
private void processSelection(int button, int col, int row, Unt repeated_click);
private void freeSelection(ClipBoard* cbd);
private void clip_get_selection(ClipBoard* cbd);
private int   vwl_display_flush(WaylandDisplay *display);
private void   vwl_callback_done(void *data, struct wl_callback *callback,
          Unt cb_data);
private int   vwl_display_roundtrip(WaylandDisplay *display);
private int   vwl_display_dispatch(WaylandDisplay *display);
private int vwl_display_dispatch_any(WaylandDisplay *display);

private void   vwl_log_handler(char const* fmt, va_list args); // it MUST be "char"
private int   vwl_connect_display(CS display);
private void   vwl_disconnect_display(void);

private void vwl_xdg_wm_base_listener_ping(void *data, struct xdg_wm_base *base, Unt serial);
private int   vwl_listen_to_registry(void);

private void   vwl_registry_listener_global(
    void *data, struct wl_registry *registry, Unt name, const char *interface, Unt version
);
private void   vwl_registry_listener_global_remove(void *data,
          struct wl_registry *registry,  Unt name);

private void vwl_add_seat(struct wl_seat *seat);
private void vwl_seat_listener_name(void *data, struct wl_seat *seat, const char *name);
private void vwl_seat_listener_capabilities(void *data, struct wl_seat *seat, Unt capabilities);
private void vwl_destroy_seat(WaylandSeat *seat);

private WaylandSeat* vwl_get_seat(CS label);
private struct wl_keyboard   *vwl_seat_get_keyboard(WaylandSeat *seat);

private int vwl_focus_stealing_available(void);
private void vwl_xdg_surface_listener_configure(void *data,
          struct xdg_surface *surface, Unt serial);

private void vwl_bs_buffer_listener_release(void *data,
          struct wl_buffer *buffer);
private void vwl_destroy_buffer_store(BufferStore *store);
private BufferStore *vwl_init_buffer_store(int width, int height);

private void vwl_destroy_fs_surface(vwl_fs_surface_T *store);
private int vwl_init_fs_surface(WaylandSeat *seat,
          BufferStore *buffer_store,
          void (*on_focus)(void *, Unt), void *user_data);

private void vwl_fs_keyboard_listener_enter(void *data,
          struct wl_keyboard *keyboard, Unt serial,
          struct wl_surface *surface, struct wl_array *keys);
private void vwl_fs_keyboard_listener_keymap(void *data,
          struct wl_keyboard *keyboard, Unt format,
          int fd, Unt size);
private void vwl_fs_keyboard_listener_leave(void *data,
          struct wl_keyboard *keyboard, Unt serial,
          struct wl_surface *surface);
private void vwl_fs_keyboard_listener_key(void *data,
          struct wl_keyboard *keyboard, Unt serial,
          Unt time, Unt key, Unt state);
private void vwl_fs_keyboard_listener_modifiers(void *data,
          struct wl_keyboard *keyboard, Unt serial,
          Unt mods_depressed, Unt mods_latched,
          Unt mods_locked, Unt group);
private void   vwl_fs_keyboard_listener_repeat_info(void *data,
          struct wl_keyboard *keyboard, int32_t rate, int32_t delay);

private void   vwl_gen_data_device_listener_data_offer(void *data,
          void *offer_proxy);
private void   vwl_gen_data_device_listener_selection(void *data,
          void *offer_proxy, WaylandSelection selection,
          DataProtocol protocol);

private void vwl_data_device_destroy(DataDevice* device, int alloced);
private void vwl_data_offer_destroy(DataOffer *offer, int alloced);
private void vwl_data_source_destroy(DataSource* source, int alloced);

private void vwl_data_device_add_listener(DataDevice *device, void *data);
private void vwl_data_source_add_listener(DataSource *source, void *data);
private void vwl_data_offer_add_listener(DataOffer *offer, void *data);

private void   vwl_data_device_set_selection(DataDevice *device,
          DataSource *source, Unt serial,
          WaylandSelection selection);
private void vwl_data_offer_receive(DataOffer *offer, char const* mime_type, int fd);
private int   vwl_get_data_device_manager(vwl_data_device_manager_T *manager,
          WaylandSelection selection);
private void   vwl_get_data_device(vwl_data_device_manager_T *manager,
          WaylandSeat *seat, DataDevice *device);
private void   vwl_create_data_source(vwl_data_device_manager_T *manager,
          DataSource *source);
private void   vwl_data_source_offer(DataSource *source,
          const char *mime_type);

private void   vwl_clipboard_free_mime_types(
          WaylandClipboardSelection *clip_sel);
private int   vwl_clipboard_selection_is_ready(
          WaylandClipboardSelection *clip_sel);

private void   vwl_data_device_listener_data_offer(
          DataDevice *device, DataOffer *offer);
private void vwl_data_offer_listener_offer(DataOffer* offer, char const* mime_type);
private void vwl_data_device_listener_selection(DataDevice *device,
          DataOffer *offer, WaylandSelection selection);
private void vwl_data_device_listener_finished(DataDevice *device);
private void vwl_data_source_listener_send(DataSource *source, char const* mime_type, int fd);
private void vwl_data_source_listener_cancelled(DataSource *source);
private void vwl_on_focus_set_selection(void *data, Unt serial);
private void wayland_set_display(CS display);

//}}}

private vwl_data_device_Listener   vwl_data_device_listener = {
    .data_offer       = vwl_data_device_listener_data_offer,
    .selection       = vwl_data_device_listener_selection,
    .finished       = vwl_data_device_listener_finished
};

private DataSourceListener dataSourceListener = {
    .send       = vwl_data_source_listener_send,
    .cancelled       = vwl_data_source_listener_cancelled
};

private DataOfferListener dataOfferListener = {.offer = vwl_data_offer_listener_offer};

private struct xdg_wm_base_listener  vwl_xdg_wm_base_listener = {
    .ping       = vwl_xdg_wm_base_listener_ping
};

private struct xdg_surface_listener  vwl_xdg_surface_listener = {
    .configure       = vwl_xdg_surface_listener_configure
};

private struct wl_buffer_listener    vwl_cb_buffer_listener = {
    .release       = vwl_bs_buffer_listener_release
};

private struct wl_keyboard_listener  vwl_fs_keyboard_listener = {
    .enter       = vwl_fs_keyboard_listener_enter,
    .key       = vwl_fs_keyboard_listener_key,
    .keymap       = vwl_fs_keyboard_listener_keymap,
    .leave       = vwl_fs_keyboard_listener_leave,
    .modifiers       = vwl_fs_keyboard_listener_modifiers,
    .repeat_info    = vwl_fs_keyboard_listener_repeat_info
};

private struct wl_callback_listener  vwl_callback_listener = {
    .done       = vwl_callback_done
};

private struct wl_registry_listener  vwl_registry_listener = {
    .global       = vwl_registry_listener_global,
    .global_remove  = vwl_registry_listener_global_remove
};

private struct wl_seat_listener vwl_seat_listener = {
    .name       = vwl_seat_listener_name,
    .capabilities   = vwl_seat_listener_capabilities
};

private WaylandDisplay vwl_display;
private GlobalObjects vwl_gobjects;
private ArrayList vwl_seats;

// Make sure to sync this with vwl_cb_uninit since it memsets this to zero
private vwl_clipboard_T   vwl_clipboard = {
    .regular.selection = WAYLAND_SELECTION_REGULAR,
    .primary.selection = WAYLAND_SELECTION_PRIMARY,
};

//{{{copy-and-paste registers

//Registers:
//  0 = unnamed register, for normal yanks and puts
//  1..9 = registers '1' to '9', for deletes
//10..35 = registers 'a' to 'z' ('A' to 'Z' for appending)
//    36 = delete register '-'
//    37 = Selection register '*'.
//    38 = Clipboard register '+'.
private YankReg   y_regs[NUM_REGISTERS];

private YankReg   *y_current;       // ptr to current yankreg
private int      y_append;       // true when appending
private YankReg   *y_previous = NULL; // ptr to last written yankreg

private int   stuff_yank(int, CS);
private void   put_reedit_in_typeBufG(int silent);
private int   put_in_typeBufG(CS s, int esc, int colon, int silent);
private int   yank_copy_line(BlockDef* bd, long y_idx, int exclude_trailing_space);
private void   copy_yank_reg(YankReg *reg);
private void   dis_msg(CS p, int skip_esc);

private void may_set_selection(void);
private void clip_gen_set_selection(ClipBoard *cbd);

YankReg *
get_y_regs(void) {
   return y_regs;
}

private YankReg *
getYRegister(int reg) {
   return &y_regs[reg];
}

YankReg *
get_y_current(void) {
   return y_current;
}

YankReg *
get_y_previous(void) {
   return y_previous;
}

void
set_y_current(YankReg *yreg) {
   y_current = yreg;
}

void
set_y_previous(YankReg *yreg) {
   y_previous = yreg;
}

void
reset_y_append(void) {
   y_append = false;
}


//Keep the last expression line here, for repeating.
private Byte   *expr_line = NULL;
private Invocation   *exprInvoS = NULL;

//Get an expression for the "\"=expr1" or "CTRL-R =expr1" Return '=' when OK, ZERO otherwise.
int
get_expr_register(void) {
   CS new_line = getCommline('=', 0L, 0, 0);
   if (!new_line)
      return ZERO;
   if (*new_line == ZERO)   // use previous line
      eeglFree(new_line);
   else
      set_expr_line(new_line, NULL);
   return '=';
}

//Set the expression for the '=' register. Argument must be an allocated string.
//"invo" may be used if the next line needs to be checked when evaluating the expression.
void
set_expr_line(CS new_line, Invocation* invo) {
   eeglFree(expr_line);
   expr_line = new_line;
   exprInvoS = invo;
}

//Get the result of the '=' register expression.
//Return a pointer to allocated memory, or NULL for failure.
CS
get_expr_line(void) {
   Byte   *expr_copy;
   Byte   *rv;
   static int   nested = 0;

   if (!expr_line)
      return NULL;

   // Make a copy of the expression, because evaluating it may cause it to be changed.
   expr_copy = copyStr(expr_line);

   //When we are invoked recursively limit the evaluation to 10 levels. Then return the string as-is
   if (nested >= 10)
      return expr_copy;

   ++nested;
   rv = evalToStringWithInvo(expr_copy, true, exprInvoS, false);
   --nested;
   eeglFree(expr_copy);
   return rv;
}

//Get the '=' register expression itself, without evaluating it.
private CS
get_expr_line_src(void) {
   if (!expr_line)
      return NULL;
   return copyStr(expr_line);
}

//Check if 'regname' is a valid name of a yank register.
//Note: There is no check for 0 (default register), caller should do this
int
valid_yank_reg(int regname, Boole writing) {      // if true check for writable registers
   if (      (regname > 0 && ASCII_ISALNUM(regname))
          || (!writing && firstOccurrence((CS)"/.%:=", regname) != NULL)
          || regname == '#'
          || regname == '"'
          || regname == '-'
          || regname == '_'
          || regname == '*'
          || regname == '+'
          || (!writing && regname == '~')
                        )
      return true;
   // clipboard support not enabled in this build
   ei (regname == '*' || regname == '+') {
      // Warn about missing clipboard support once
      msg_warn_missing_clipboard();
      return false;
   }
   return false;
}

//Set y_current and y_append, according to the value of "regname".
//Cannot handle the '_' register.
//Must only be called with a valid register name!
//
//If regname is 0 and writing, use register 0. If regname is 0 and reading, use previous register
//
//Return true when the register should be inserted literally (selection or clipboard).
int
get_yank_register(int regname, int writing) {
   int       i;
   int       ret = false;

   y_append = false;
   if ((regname == 0 || regname == '"') && !writing && y_previous != NULL) {
      y_current = y_previous;
      return ret;
   }
   i = regname;
   if (EE_ISDIGIT(i))
      i -= '0';
   ei (ASCII_ISLOWER(i))
      i = i - 'a' + 10;
   ei (ASCII_ISUPPER(i)) {
      i = i - 'A' + 10;
      y_append = true;
   } ei (regname == '-')
      i = DELETION_REGISTER;
   //When selection is not available, use register 0 instead of '*'
   ei (regname == '*') {
      i = STAR_REGISTER;
      ret = true;
   }
   // When clipboard is not available, use register 0 instead of '+'
   ei (regname == '+') {
      i = PLUS_REGISTER;
      ret = true;
   } ei (!writing && regname == '~')
      i = TILDE_REGISTER;
   else      // not 0-9, a-z, A-Z or '-': use register 0
      i = 0;
   y_current = &(y_regs[i]);
   if (writing)   // remember the register we write into for do_put()
      y_previous = y_current;
   return ret;
}

//Obtain the contents of a "normal" register. The register is made empty.
//The returned pointer has allocated memory, use put_register() later.
void *
get_register(int      name, int copy) {  // make a copy, if false make register empty.
   YankReg   *reg;
   int      i;

   // When Visual area changed, may have to update selection. Obtain the selection too.
   if (name == '*') {
      clip_update_selection(&clipboard);
      may_get_selection(name);
   }
   if (name == '+') {
      clip_update_selection(&clipboard);
      may_get_selection(name);
   }

   get_yank_register(name, 0);
   reg = ALLOC_ONE(YankReg);
   if (reg == NULL)
      return (void *)NULL;

   *reg = *y_current;
   if (copy) {
      // If we run out of memory some or all of the lines are empty.
      if (reg->y_size == 0 || y_current->y_array == NULL)
         reg->y_array = NULL;
      else
         reg->y_array = ALLOC_MULT(Text, reg->y_size);
      if (reg->y_array != NULL) {
         for (i = 0; i < reg->y_size; ++i) {
            reg->y_array[i].c = copySubstr(
                  y_current->y_array[i].c, y_current->y_array[i].len
            );
            reg->y_array[i].len = y_current->y_array[i].len;
         }
      }
   }
   else
      y_current->y_array = NULL;
   return (void *)reg;
}

// Put "reg" into register "name".  Free any previous contents and "reg".
void
put_register(int name, void *reg) {
   get_yank_register(name, 0);
   free_yank_all();
   *y_current = *(YankReg *)reg;
   eeglFree(reg);

   // Send text written to clipboard register to the clipboard.
   may_set_selection();
}

void
free_register(void *reg) {
    YankReg tmp;

    tmp = *y_current;
    *y_current = *(YankReg *)reg;
    free_yank_all();
    eeglFree(reg);
    *y_current = tmp;
}

// return true if the current yank register has type MLINE
int
yank_register_mline(int regname) {
   if (regname != 0 && !valid_yank_reg(regname, false))
      return false;
   if (regname == '_')      // black hole is always empty
      return false;
   get_yank_register(regname, false);
   return (y_current->y_type == MLINE);
}

// Start or stop recording into a yank register. Return FAIL for failure, OK otherwise.
int
do_record(int c) {
   Byte       *p;
   static int       regname;
   YankReg       *old_y_previous, *old_y_current;
   int          retval;

   if (reg_recording == 0) {      // start recording
      // registers 0-9, a-z and " are allowed
      if (c < 0 || (!ASCII_ISALNUM(c) && c != '"'))
         retval = FAIL;
      else {
         reg_recording = c;
         showmode();
         regname = c;
         retval = OK;
      }
   } else {       // stop recording
      // Get the recorded key hits.  K_SPECIAL and CSI will be escaped, this
      // needs to be removed again to put it in a register.  exec_reg then
      // adds the escaping back later.
      reg_recording = 0;
      msg(E);
      p = get_recorded();
      if (!p)
         retval = FAIL;
      else {
         // Remove escaping for CSI and K_SPECIAL in multi-byte chars.
         eeUnescapeCsi(p);

         // We don't want to change the default register here, so save and
         // restore the current register name.
         old_y_previous = y_previous;
         old_y_current = y_current;

         retval = stuff_yank(regname, p);

         y_previous = old_y_previous;
         y_current = old_y_current;
      }
   }
   return retval;
}

//Stuff string "p" into yank register "regname" as a single line (append if
//uppercase).   "p" must have been alloced.
//
//return FAIL for failure, OK otherwise
private int
stuff_yank(int regname, CS p) {
   // check for read-only register
   if (regname != 0 && !valid_yank_reg(regname, true)) {
      eeglFree(p);
      return FAIL;
   }
   if (regname == '_') {        // black hole: don't do anything
      eeglFree(p);
      return OK;
   }

   Unt plen = STRLEN(p);
   get_yank_register(regname, true);
   if (y_append && y_current->y_array != NULL) {
      Text    *pp;
      Byte       *tmp;
      Unt       tmplen;

      pp = &(y_current->y_array[y_current->y_size - 1]);
      tmplen = pp->len + plen;
      tmp = alloc(tmplen + 1);
      STRCPY(tmp, pp->c);
      STRCPY(tmp + pp->len, p);
      eeglFree(p);
      eeglFree(pp->c);
      pp->c = tmp;
      pp->len = tmplen;
   } else {
      free_yank_all();
      if ((y_current->y_array = ALLOC_ONE(Text)) == NULL) {
         eeglFree(p);
         return FAIL;
      }
      y_current->y_array[0].c = p;
      y_current->y_array[0].len = plen;
      y_current->y_size = 1;
      y_current->y_type = MCHAR;  // used to be MLINE, why?
      y_current->y_time_set = eeTime();
   }
   return OK;
}

// Last executed register (@ command)
private int execreg_lastc = ZERO;

int
get_execreg_lastc(void) {
   return execreg_lastc;
}

void
set_execreg_lastc(int lastc) {
   execreg_lastc = lastc;
}

/*
 * When executing a register as a series of ex-commands, if the
 * line-continuation character is used for a line, then join it with one or
 * more previous lines. Note that lines are processed backwards starting from
 * the last line in the register.
 *
 * Arguments:
 *   lines - list of lines in the register
 *   idx - index of the line starting with \ or "\. Join this line with all the
 *      immediate predecessor lines that start with a \ and the first line
 *      that doesn't start with a \. Lines that start with a comment "\
 *      character are ignored.
 *
 * Returns the concatenated line. The index of the line that should be
 * processed next is returned in idx.
 */
private CS
execreg_line_continuation(Arr(Text) lines, long *idx) {
   ArrayList   ga;
   long   i = *idx;
   Byte   *p;
   int      cmd_start;
   int      cmd_end = i;
   int      j;

   ga_init2(&ga, sizeof(Byte), 400);

   // search backwards to find the first line of this command.
   // Any line not starting with \ or "\ is the start of the command.
   while (--i > 0) {
      p = skipwhite(lines[i].c);
      if (*p != '\\' && (p[0] != '"' || p[1] != '\\' || p[2] != ' '))
          break;
   }
   cmd_start = i;

   // join all the lines
   ga_concat(&ga, lines[cmd_start].c);
   for (j = cmd_start + 1; j <= cmd_end; j++) {
      p = skipwhite(lines[j].c);
      if (*p == '\\') {
         // Adjust the growsize to the current length to
         // speed up concatenating many lines.
         if (ga.len > 400) {
            if (ga.len > 8000)
               ga.ga_growsize = 8000;
            else
               ga.ga_growsize = ga.len;
         }
         ga_concat(&ga, p + 1);
      }
   }
   ga_append(&ga, ZERO);
   CS retVal = copySubstr(ga.c, ga.len);
   ga_clear(&ga);

   *idx = i;
   return retVal;
}

//Execute a yank register: copy it into the stuff buffer.
//
//Return FAIL for failure, OK otherwise.
int
do_execreg(
    int       regname,
    int       colon,      // insert ':' before each line
    int       addcr,      // always add '\n' to end of line
    int       silent)      // set "silent" flag in typeahead buffer
{
    long   i;
    Byte   *p;
    int      retval = OK;
    int      remap;

   // repeat previous one
   if (regname == '@') {
      if (execreg_lastc == ZERO) {
         emsg(_(e_no_previously_used_register));
         return FAIL;
      }
      regname = execreg_lastc;
   }
   // check for valid regname
   if (regname == '%' || regname == '#' || !valid_yank_reg(regname, false)) {
      emsg_invreg(regname);
      return FAIL;
   }
   execreg_lastc = regname;

   regname = may_get_selection(regname);

   // black hole: don't stuff anything
   if (regname == '_')
      return OK;

    // use last command line
   if (regname == ':') {
      if (lastCommlineG == NULL) {
          emsg(_(e_no_previous_command_line));
          return FAIL;
      }
      // don't keep the cmdline containing @:
      EE_CLEAR(newLastCommlineG);
      // Escape all control characters with a CTRL-V
      p = copyStr_escaped_ext(
            lastCommlineG,
            S"\001\002\003\004\005\006\007" "\010\011\012\013\014\015\016\017"
             "\020\021\022\023\024\025\026\027" "\030\031\032\033\034\035\036\037",
            Ctrl_V, false, null
      );
      if (p != NULL) {
          // When in Visual mode "'<,'>" will be prepended to the command.
          // Remove it when it's already there.
          if (VIsual_active && STRNCMP(p, "'<,'>", 5) == 0)
         retval = put_in_typeBufG(p + 5, true, true, silent);
          else
         retval = put_in_typeBufG(p, true, true, silent);
      }
      eeglFree(p);
   } ei (regname == '=') {
      p = get_expr_line();
      if (p == NULL)
          return FAIL;
      retval = put_in_typeBufG(p, true, colon, silent);
      eeglFree(p);
    } ei (regname == '.') {      // use last inserted text
      p = get_last_insert_save();
      if (p == NULL)    {
         emsg(_(e_no_inserted_text_yet));
         return FAIL;
   }
   retval = put_in_typeBufG(p, false, colon, silent);
   eeglFree(p);
   } else {
      get_yank_register(regname, false);
      if (y_current->y_array == NULL)
          return FAIL;

      // Disallow remapping for ":@r".
      remap = colon ? REMAP_NONE : REMAP_YES;

      // Insert lines into typeahead buffer, from last one to first one.
      put_reedit_in_typeBufG(silent);
      for (i = y_current->y_size; --i >= 0; ) {
         CS escaped;
         CS str;
         int free_str = false;

         // insert NL between lines and after last line if type is MLINE
         if (y_current->y_type == MLINE || i < y_current->y_size - 1 || addcr) {
            if (insertIntoTypebuf((CS)"\n", remap, 0, true, silent) == FAIL)
               return FAIL;
         }

         // Handle line-continuation for :@<register>
         str = y_current->y_array[i].c;
         if (colon && i > 0) {
            p = skipwhite(str);
            if (*p == '\\' || (p[0] == '"' && p[1] == '\\' && p[2] == ' ')) {
               str = execreg_line_continuation(y_current->y_array, &i);
               if (str == NULL)
                  return FAIL;
               free_str = true;
            }
         }
         escaped = copyStr_escape_csi(str);
         if (free_str)
            eeglFree(str);
         retval = insertIntoTypebuf(escaped, remap, 0, true, silent);
         eeglFree(escaped);
         if (retval == FAIL)
            return FAIL;
         if (colon && insertIntoTypebuf((CS)":", remap, 0, true, silent) == FAIL)
            return FAIL;
      }
      reg_executing = regname == 0 ? '"' : regname; // disable "q" command
      pending_end_reg_executing = false;
   }
   return retval;
}

//If "restart_edit" is not zero, put it in the typeahead buffer, so that it's
//used only after other typeahead has been processed.
private void
put_reedit_in_typeBufG(int silent) {
   Byte   buf[3];

   if (restart_edit == ZERO)
      return;

   if (restart_edit == 'V') {
      buf[0] = 'g';
      buf[1] = 'R';
      buf[2] = ZERO;
   } else {
      buf[0] = restart_edit == 'I' ? 'i' : restart_edit;
      buf[1] = ZERO;
   }
   if (insertIntoTypebuf(buf, REMAP_NONE, 0, true, silent) == OK)
      restart_edit = ZERO;
}

//Insert register contents "s" into the typeahead buffer, so that it will be executed again.
//When "esc" is true it is to be taken literally: Escape CSI characters and no remapping.
private int
put_in_typeBufG(
    CS s,
    int      esc,
    int      colon,       // add ':' before the line
    int      silent)
{
    int      retval = OK;

   put_reedit_in_typeBufG(silent);
   if (colon)
      retval = insertIntoTypebuf((CS)"\n", REMAP_NONE, 0, true, silent);
   if (retval == OK) {
      Byte   *p;

      if (esc)
         p = copyStr_escape_csi(s);
      else
         p = s;
      if (p == NULL)
         retval = FAIL;
      else
         retval = insertIntoTypebuf(p, esc ? REMAP_NONE : REMAP_YES, 0, true, silent);
      if (esc)
         eeglFree(p);
   }
   if (colon && retval == OK)
       retval = insertIntoTypebuf((CS)":", REMAP_NONE, 0, true, silent);
   return retval;
}

//Insert a yank register: copy it into the Read buffer.
//Used by CTRL-R command and middle mouse button in insert mode.
//
//return FAIL for failure, OK otherwise
int
insert_reg(
    int      regname,
    int      literally_arg)   // insert literally, not as if typed
{
    long   i;
    int      retval = OK;
    Byte   *arg;
    int      allocated;
    int      literally = literally_arg;

    // It is possible to get into an endless loop by having CTRL-R a in
    // register a and then, in insert mode, doing CTRL-R a.
    // If you hit CTRL-C, the loop will be broken here.
    ui_breakcheck();
    if (gotInterruptG)
   return FAIL;

    // check for valid regname
    if (regname != ZERO && !valid_yank_reg(regname, false))
   return FAIL;

    regname = may_get_selection(regname);

    if (regname == '.')         // insert last inserted text
   retval = stuff_inserted(ZERO, 1L, true);
    ei (get_spec_reg(regname, &arg, &allocated, true)) {
   if (arg == NULL)
       return FAIL;
   stuffescaped(arg, literally);
   if (allocated)
       eeglFree(arg);
    } else {           // name or number register
   if (get_yank_register(regname, false))
       literally = true;
   if (y_current->y_array == NULL)
       retval = FAIL;
   else {
       for (i = 0; i < y_current->y_size; ++i) {
      if (regname == '-' && y_current->y_type == MCHAR) {
          int dir = BACKWARD;

          AppendCharToRedobuff(Ctrl_R);
          AppendCharToRedobuff(regname);
          do_put(regname, NULL, dir, 1L, PUT_CURSEND);
      } else {
          stuffescaped(y_current->y_array[i].c, literally);
          // Insert a newline between lines and after last line if
          // y_type is MLINE.
          if (y_current->y_type == MLINE || i < y_current->y_size - 1)
         stuffcharReadbuff('\n');
      }
       }
   }
    }

    return retval;
}

//If "regname" is a special register, return true and store a pointer to its value in "retVal".
int
get_spec_reg(
   int      regname,
   OUT CS* retVal,
   int      *allocated,   // return: true when value was allocated
   int      errmsg)      // give error message when failing
{
   int      cnt;

   *retVal = E;
   *allocated = false;
   switch (regname) {
   case '%':      // file name
      if (errmsg)
         check_fname();   // will give emsg if not set
      *retVal = curBook->currFileName;
      return true;

   case '#':      // alternate file name
      *retVal = getaltfname(errmsg);   // may give emsg if not set
      return true;

   case '=':      // result of expression
      *retVal = get_expr_line();
      *allocated = true;
      return true;

   case ':':      // last command line
      if (!lastCommlineG && errmsg)
         emsg(_(e_no_previous_command_line));
      *retVal = lastCommlineG != E ? lastCommlineG : E;
      return true;

   case '/':      // last search-pattern
      CS lastPat = last_search_pat().c;
      if (!lastPat && errmsg)
         emsg(_(e_no_previous_regular_expression));
      *retVal = lastPat ? lastPat : S"";
      return true;

   case '.':      // last inserted text
      *retVal = get_last_insert_save();
      *allocated = true;
      if (*retVal == S"" && errmsg)
         emsg(_(e_no_inserted_text_yet));
      return true;

   case Ctrl_F:      // Filename under cursor
   case Ctrl_P:      // Path under cursor, expand via "path"
      if (!errmsg)
         return false;
      *retVal = file_name_at_cursor(
            FNAME_MESS | FNAME_HYP | (regname == Ctrl_P ? FNAME_EXP : 0), 1L, NULL
      );
      *allocated = true;
      return true;

   case Ctrl_W:      // word under cursor
   case Ctrl_A:      // WORD (mnemonic All) under cursor
       if (!errmsg)
      return false;
       cnt = find_ident_under_cursor(retVal, regname == Ctrl_W
               ?  (FIND_IDENT|FIND_STRING) : FIND_STRING);
       *retVal = cnt ? copySubstr(*retVal, cnt) : E;
       *allocated = true;
       return true;

   case Ctrl_L:      // Line under cursor
      if (!errmsg)
         return false;

      *retVal = memGetLine(curPor->book,
         curPor->cursor.lnum, false);
       return true;

   case '_':      // black hole: always empty
       *retVal = (CS)"";
       return true;
   }

   return false;
}

//Paste a yank register into the command line. Only for non-special registers.
//Used by CTRL-R command in command-line mode.
//insert_reg() can't be used here, because special characters from the
//register contents will be interpreted as commands.
//return FAIL for failure, OK otherwise
int
cmdline_paste_reg(
   int regname,
   int literally_arg,   // Insert text literally instead of "as typed"
   int remcr)      // don't add CR characters
{
   long   i;
   int      literally = literally_arg;

   if (get_yank_register(regname, false))
      literally = true;
   if (y_current->y_array == NULL)
      return FAIL;

   for (i = 0; i < y_current->y_size; ++i) {
      cmdline_paste_str(y_current->y_array[i].c, literally);

      // Insert ^M between lines and after last line if type is MLINE.
      // Don't do this when "remcr" is true.
      if ((y_current->y_type == MLINE || i < y_current->y_size - 1) && !remcr)
          cmdline_paste_str((CS)"\r", literally);

      // Check for CTRL-C, in case someone tries to paste a few thousand
      // lines and gets bored.
      ui_breakcheck();
      if (gotInterruptG)
          return FAIL;
   }
   return OK;
}

//Shift the delete registers: "9 is cleared, "8 becomes "9, etc.
void
shift_delete_registers(void) {
   int      n;

   y_current = &y_regs[9];
   free_yank_all();         // free register nine
   for (n = 9; n > 1; --n)
      y_regs[n] = y_regs[n - 1];
   y_current = &y_regs[1];
   if (!y_append)
      y_previous = y_current;
   y_regs[1].y_array = NULL;      // set register one to empty
}

void
yank_do_autocmd(Operator* opArg, YankReg *reg) {
   static int recursive = false;
   Byte buf[NUMBUFLEN + 2];
   long reglen = 0;
   SaveVEvent save_v_event;

   if (recursive)
      return;

   Bag* v_event = get_v_event(&save_v_event);

   List* list = list_alloc();

   // yanked text contents
   for (int n = 0; n < reg->y_size; n++)
      list_append_string(list, reg->y_array[n].c, -1);
   list->lock = VAR_FIXED;
   (void)bagAddList(v_event, S"regcontents", list);

   // register name or empty string for unnamed operation
   buf[0] = (Byte)opArg->regname;
   buf[1] = ZERO;
   (void)bagAddString(v_event, S"regname", buf);

   // motion type: inclusive or exclusive
   (void)bagAdd_bool(v_event, S"inclusive", opArg->inclusive);

   // kind of operation (yank, delete, change)
   buf[0] = get_op_char(opArg->opTy);
   buf[1] = get_extra_op_char(opArg->opTy);
   buf[2] = ZERO;
   (void)bagAddString(v_event, S"operator", buf);

   // register type
   buf[0] = ZERO;
   buf[1] = ZERO;
   switch (get_reg_type(opArg->regname, &reglen)) {
   case MLINE: buf[0] = 'V'; break;
   case MCHAR: buf[0] = 'v'; break;
   case MBLOCK:
      eeSnprintf(buf, sizeof(buf), "%c%ld", Ctrl_V, reglen + 1);
      break;
   }
   (void)bagAddString(v_event, S"regtype", buf);

   // selection type - visual or not
   (void)bagAdd_bool(v_event, S"visual", opArg->is_VIsual);

   // Lock the dictionary and its keys
   bagSetItemsRo(v_event);

   recursive = true;
   textlock++;
   applyAutocomms(EVENT_TEXTYANKPOST, NULL, NULL, false, curBook);
   textlock--;
   recursive = false;

   // Empty the dictionary, v:event is still valid
   restore_v_event(v_event, &save_v_event);
}

// set all the yank registers to empty (called from main())
void
init_yank(void) {
   for (int i = 0; i < NUM_REGISTERS; ++i)
      y_regs[i].y_array = NULL;
}

#if defined(EXITFREE) || defined(PROTO)
void
clear_registers(void) {
   for (int i = 0; i < NUM_REGISTERS; ++i) {
      y_current = &y_regs[i];
      if (y_current->y_array != NULL)
         free_yank_all();
   }
}
#endif

//Free "n" lines from the current yank register. Called for normal freeing and in case of error.
private void
free_yank(long n) {
   if (y_current->y_array == NULL)
      return;

   for (long i = n; --i >= 0; )
      EE_CLEAR_STRING(y_current->y_array[i]);
   EE_CLEAR(y_current->y_array);
}

void
free_yank_all(void) {
   free_yank(y_current->y_size);
}

//Yank the text between "opArg->start" and "opArg->end" into a yank register.
//If we are to append (uppercase register), we first yank into a new yank
//register and then concatenate the old and the new one (so we keep the old
//one in case of out-of-memory).
//
//Return FAIL for failure, OK otherwise.
int
op_yank(Operator *opArg, int deleting, int mess) {
   long      y_idx;      // index in y_array[]
   YankReg      *curr;      // copy of y_current
   YankReg      newreg;      // new yank register when appending
   LineNr      lnum;      // current line number
   int         yanktype = opArg->motion_type;
   long      yanklines = opArg->line_count;
   LineNr      yankendlnum = opArg->end.lnum;
   Byte      *pnew;
   BlockDef   bd;

                // check for read-only register
   if (opArg->regname != 0 && !valid_yank_reg(opArg->regname, true)) {
      beep_flush();
      return FAIL;
   }
   if (opArg->regname == '_')       // black hole: nothing to do
      return OK;

   if (!deleting)          // op_delete() already set y_current
      get_yank_register(opArg->regname, true);

   curr = y_current;
                // append to existing contents
   if (y_append && y_current->y_array != NULL)
      y_current = &newreg;
   else
      free_yank_all();       // free previously yanked lines

    // If the cursor was in column 1 before and after the movement, and the
    // operator is not inclusive, the yank is always linewise.
    if (       opArg->motion_type == MCHAR
       && opArg->start.col == 0
       && !opArg->inclusive
       && (!opArg->is_VIsual)
       && !opArg->block_mode
       && opArg->end.col == 0
       && yanklines > 1)
    {
      yanktype = MLINE;
      --yankendlnum;
      --yanklines;
   }

   y_current->y_size = yanklines;
   y_current->y_type = yanktype;   // set the yank register type
   y_current->y_width = 0;
   y_current->y_array = lallocZeroed(sizeof(Text)* yanklines, true);
   y_current->y_time_set = eeTime();

   y_idx = 0;
   lnum = opArg->start.lnum;

   if (opArg->block_mode) {
      // Visual block mode
      y_current->y_type = MBLOCK;       // set the yank register type
      y_current->y_width = opArg->end_vcol - opArg->start_vcol;

      if (curPor->cursWant == MAXCOL && y_current->y_width > 0)
         y_current->y_width--;
   }

   for ( ; lnum <= yankendlnum; lnum++, y_idx++) {
      switch (y_current->y_type) {
      case MBLOCK:
         block_prep(opArg, OUT &bd, lnum, false);
         if (yank_copy_line(&bd, y_idx, opArg->excludeTrailingWhitespace) == FAIL)
            goto fail;
         break;

      case MLINE:
         y_current->y_array[y_idx].len = ml_get_len(lnum);
         if ((y_current->y_array[y_idx].c = copySubstr(ml_get(lnum),
                  y_current->y_array[y_idx].len)) == NULL
         ) {
            EE_CLEAR_STRING(y_current->y_array[y_idx]);
            goto fail;
         }
         break;

      case MCHAR: {
            int tmp;

            jugCharwiseBlockPrep(opArg->start, opArg->end, &bd, lnum, opArg->inclusive);

            // make sure bd.textlen is not longer than the text
            tmp = (int)STRLEN(bd.textstart);
            if (tmp < bd.textlen)
               bd.textlen = tmp;

            if (yank_copy_line(&bd, y_idx, false) == FAIL)
               goto fail;
            break;
         }
         // NOTREACHED
      }
   }

   if (curr != y_current) {  // append the new block to the old block
      Text *new_ptr;
      long j;

      new_ptr = ALLOC_MULT(Text, curr->y_size + y_current->y_size);
      for (j = 0; j < curr->y_size; ++j)
         new_ptr[j] = curr->y_array[j];
      eeglFree(curr->y_array);
      curr->y_array = new_ptr;
      curr->y_time_set = eeTime();

      if (yanktype == MLINE)   // MLINE overrides MCHAR and MBLOCK
          curr->y_type = MLINE;

      // Concatenate the last line of the old block with the first line of the new block
      if (curr->y_type == MCHAR) {
         pnew = alloc(curr->y_array[curr->y_size - 1].len + y_current->y_array[0].len + 1);

         --j;
         STRCPY(pnew, curr->y_array[j].c);
         STRCPY(pnew + curr->y_array[j].len, y_current->y_array[0].c);
         eeglFree(curr->y_array[j].c);
         curr->y_array[j].c = pnew;
         curr->y_array[j].len = curr->y_array[j].len + y_current->y_array[0].len;
         ++j;
         EE_CLEAR_STRING(y_current->y_array[0]);
         y_idx = 1;
      } else
         y_idx = 0;
      while (y_idx < y_current->y_size)
         curr->y_array[j++] = y_current->y_array[y_idx++];
      curr->y_size = j;
      eeglFree(y_current->y_array);
      y_current = curr;
    }

   if (mess) {        // Display message about yank?
      if (yanktype == MCHAR && !opArg->block_mode && yanklines == 1)
         yanklines = 0;
      // Some versions of Vi use ">=" here, some don't...
      Byte namebuf[100];

      if (opArg->regname == ZERO)
         *namebuf = ZERO;
      else
         eeSnprintf(namebuf, sizeof(namebuf), _(" into \"%c"), opArg->regname);

      // redisplay now, so message is not deleted
      update_topline_redraw();
      if (opArg->block_mode) {
         smsg(NGETTEXT("block of %ld line yanked%s", "block of %ld lines yanked%s", yanklines),
            yanklines, namebuf);
      } else {
         smsg(NGETTEXT("%ld line yanked%s", "%ld lines yanked%s", yanklines), yanklines, namebuf);
      }
   }

   if ((commModifierG.cmod_flags & CMOD_LOCKMARKS) == 0) {
      // Set "'[" and "']" marks.
      curBook->opStart = opArg->start;
      curBook->opEnd = opArg->end;
      if (yanktype == MLINE && !opArg->block_mode) {
         curBook->opStart.col = 0;
         curBook->opEnd.col = MAXCOL;
      }
      if (yanktype != MLINE && !opArg->inclusive)
         // Exclude the end position.
         decl(&curBook->opEnd);
    }

   //If we were yanking to the '*' register, send result to clipboard.
   //If no register was specified, and "unnamed" in 'clipboard', make a copy to the '*' register.
   if ((curr == &(y_regs[STAR_REGISTER]) || (!deleting && opArg->regname == 0))) {
      if (curr != &(y_regs[STAR_REGISTER]))
         // Copy the text from register 0 to the clipboard register.
         copy_yank_reg(&(y_regs[STAR_REGISTER]));

      clip_own_selection(&clipboard);
      clip_gen_set_selection(&clipboard);
   }

   //If we were yanking to the '+' register, send result to selection.
   //Also copy to the '*' register, in case auto-select is off. But not when
   //'clipboard' has "unnamedplus" and not "unnamed"; and not when
   //deleting and both "unnamedplus" and "unnamed".
   if ((curr == &(y_regs[PLUS_REGISTER]) || (!deleting && opArg->regname == 0))) {
      if (curr != &(y_regs[PLUS_REGISTER]))
         // Copy the text from register 0 to the clipboard register.
         copy_yank_reg(&(y_regs[PLUS_REGISTER]));

      clip_own_selection(&clipboard);
      clip_gen_set_selection(&clipboard);
   }

   if (!deleting && has_textyankpost())
      yank_do_autocmd(opArg, y_current);
   return OK;

fail:      // free the allocated lines
   free_yank(y_idx + 1);
   y_current = curr;
   return FAIL;
}

//Copy a block range into a register.
//If "exclude_trailing_space" is set, do not copy trailing whitespaces.
private int
yank_copy_line(BlockDef* bd, long y_idx, int exclude_trailing_space) {
   if (exclude_trailing_space)
      bd->endspaces = 0;
   CS pnew = alloc(bd->startspaces + bd->endspaces + bd->textlen + 1);
   y_current->y_array[y_idx].c = pnew;
   memset(pnew, ' ', (Unt)bd->startspaces);
   pnew += bd->startspaces;
   MEMMOVE(pnew, bd->textstart, (Unt)bd->textlen);
   pnew += bd->textlen;
   memset(pnew, ' ', (Unt)bd->endspaces);
   pnew += bd->endspaces;
   if (exclude_trailing_space) {
      int s = bd->textlen + bd->endspaces;

      while (s > 0 && SPACE_OR_TAB(*(bd->textstart + s - 1))) {
         s = s - (*mb_head_off)(bd->textstart, bd->textstart + s - 1) - 1;
          pnew--;
      }
   }
   *pnew = ZERO;

   y_current->y_array[y_idx].len = (Unt)(pnew - y_current->y_array[y_idx].c);

   return OK;
}

//Make a copy of the y_current register to register "reg".
private void
copy_yank_reg(YankReg *reg) {
   YankReg   *curr = y_current;
   y_current = reg;
   free_yank_all();
   *y_current = *curr;
   y_current->y_array = lallocZeroed(sizeof(Text) * y_current->y_size, true);
   for (long j = 0; j < y_current->y_size; ++j) {
       if ((y_current->y_array[j].c = copySubstr(curr->y_array[j].c, curr->y_array[j].len)) 
            == NULL
      ) {
         free_yank(j);
         y_current->y_size = 0;
         break;
      }
      y_current->y_array[j].len = curr->y_array[j].len;
   }
   y_current = curr;
}

//Put contents of register "regname" into the text.
//Caller must check "regname" to be valid!
//"flags": PUT_FIXINDENT   make indent look nice
//      PUT_CURSEND      leave cursor after end of new text
//      PUT_LINE      force linewise put (":put")
//      PUT_BLOCK_INNER     in block mode, do not add trailing spaces
void
do_put(
   int      regname,
   CS expr_result,   // result for regname "=" when compiled
   Unt dir,      // BACKWARD for 'P', FORWARD for 'p'
   long   count,
   Unt      flags
) {
   Byte   *ptr;
   Byte   *newp;
   Byte   *oldp;
   int      yanklen;
   int      totlen = 0;      // init for gcc
   LineNr   lnum;
   ColNr   col;
   long   i;         // index in y_array[]
   int      y_type;
   long   y_size;
   int      oldlen;
   long   y_width = 0;
   ColNr   vcol;
   Text* y_array = NULL;
   YankReg   *y_current_used = NULL;
   long   nr_lines = 0;
   int      allocated = false;
   Pos   orig_start = curBook->opStart;
   Pos   orig_end = curBook->opEnd;

   // Adjust register name for "unnamed" in 'clipboard'.
   clipGetDefaultRegister(&regname);
   (void)may_get_selection(regname);
   
   curBook->opStart = curPor->cursor;   // default for '[ mark
   curBook->opEnd = curPor->cursor;   // default for '] mark

   // Using inserted text works differently, because the register includes
   // special characters (newlines, etc.).
   if (regname == '.') {
      if (VIsual_active)
          stuffcharReadbuff(VIsual_mode);
      (void)stuff_inserted((dir == FORWARD ? (count == -1 ? 'o' : 'a') :
                   (count == -1 ? 'O' : 'i')), count, false);
      // Putting the text is done later, so can't really move the cursor to
      // the next character.  Use "l" to simulate it.
      if ((flags & PUT_CURSEND) && gchar_cursor() != ZERO)
          stuffcharReadbuff('l');
      return;
   }

   // For special registers '%' (file name), '#' (alternate file name) and
   // ':' (last command line), etc. we have to create a fake yank register.
   // For compiled code "expr_result" holds the expression result.
   Text insertText = (Text){null, 0};
   if (regname == '=' && expr_result)
      insertText.c = expr_result;
   ei (get_spec_reg(regname, &insertText.c, &allocated, true) && insertText.c == NULL)
      return;

   // Autocommands may be executed when saving lines for undo.  This might
   // make "y_array" invalid, so we start undo now to avoid that.
   if (u_save(curPor->cursor.lnum, curPor->cursor.lnum + 1) == FAIL)
      goto end;

   if (insertText.c != E) {
      insertText.len = STRLEN(insertText.c);

      y_type = MCHAR;
      if (regname == '=') {
         Unt  ptrlen;
         Byte  *tmp;

         // For the = register we need to split the string at NL
         // characters.
         // Loop twice: count the number of lines and save them.
         for (;;) {
            y_size = 0;
            ptr = insertText.c;
            ptrlen = insertText.len;
            while (ptr != NULL) {
                if (y_array != NULL)
               y_array[y_size].c = ptr;
                ++y_size;
                tmp = firstOccurrence(ptr, '\n');
                if (tmp == NULL) {
               if (y_array != NULL)
                   y_array[y_size - 1].len = ptrlen;
                }
                else {
               if (y_array != NULL) {
                   *tmp = ZERO;
                   y_array[y_size - 1].len = (Unt)(tmp - ptr);
                   ptrlen -= y_array[y_size - 1].len + 1;
               }
               ++tmp;
               // A trailing '\n' makes the register linewise.
               if (*tmp == ZERO) {
                   y_type = MLINE;
                   break;
               }
                }
                ptr = tmp;
            }
            if (y_array != NULL)
                break;
            y_array = ALLOC_MULT(Text, y_size);
          }
      } else {
          y_size = 1;      // use fake one-line yank register
          y_array = &insertText;
      }
   } else {
      get_yank_register(regname, false);

      y_type = y_current->y_type;
      y_width = y_current->y_width;
      y_size = y_current->y_size;
      y_array = y_current->y_array;
      y_current_used = y_current;
   }

   if (y_type == MLINE) {
      if ((flags & PUT_LINE_SPLIT) != 0) {
         // "p" or "P" in Visual mode: split the lines to put the text in between.
         if (u_save_cursor() == FAIL)
            goto end;
         CS p = ml_get_cursor();
         CS p_orig = p;
         
         Unt plen = ml_get_cursor_len();
         if (dir == FORWARD && *p != ZERO)
            MB_PTR_ADV(p);
         ptr = copySubstr(p, plen - (Unt)(p - p_orig));
         if (!ptr)
            goto end;
         ml_append(curPor->cursor.lnum, ptr, (ColNr)0, false);
         eeglFree(ptr);

         oldp = ml_get_curline();
         p = oldp + curPor->cursor.col;
         if (dir == FORWARD && *p != ZERO)
            MB_PTR_ADV(p);
         ptr = copySubstr(oldp, (Unt)(p - oldp));
         if (ptr == NULL)
            goto end;
         ml_replace(curPor->cursor.lnum, ptr, false);
         ++nr_lines;
         dir = FORWARD;
      }
      if ((flags & PUT_LINE_FORWARD) != 0) {
          // Must be "p" for a Visual block, put lines below the block.
          curPor->cursor = curBook->visual.vi_end;
          dir = FORWARD;
      }
      curBook->opStart = curPor->cursor;   // default for '[ mark
      curBook->opEnd = curPor->cursor;   // default for '] mark
   }

   if (flags & PUT_LINE)   // :put command or "p" in Visual line mode.
      y_type = MLINE;

   if (y_size == 0 || y_array == NULL) {
      showErrFmtMsg(_(e_nothing_in_register_str),
           regname == 0 ? (CS)"\"" : transchar(regname));
      goto end;
   }

   if (y_type == MBLOCK) {
      lnum = curPor->cursor.lnum + y_size + 1;
      if (lnum > curBook->mem.lineCount)
          lnum = curBook->mem.lineCount + 1;
      if (u_save(curPor->cursor.lnum - 1, lnum) == FAIL)
          goto end;
   } ei (y_type == MLINE) {
      lnum = curPor->cursor.lnum;
      // Correct line number for closed fold.  Don't move the cursor yet,
      // u_save() uses it.
      if (dir == BACKWARD)
          (void)getFolds(lnum, OUT &lnum, NULL);
      else
          (void)getFolds(lnum, NULL, OUT &lnum);
      if (dir == FORWARD)
          ++lnum;
      //In an empty buffer the empty line is going to be replaced, include it in the saved lines.
      if ((CURBOOK_EMPTY() ? u_save(0, 2) : u_save(lnum - 1, lnum)) == FAIL)
          goto end;
      if (dir == FORWARD)
          curPor->cursor.lnum = lnum - 1;
      else
          curPor->cursor.lnum = lnum;
      curBook->opStart = curPor->cursor;   // for markAdjust()
   } ei (u_save_cursor() == FAIL)
      goto end;

   lnum = curPor->cursor.lnum;
   col = curPor->cursor.col;

   // Block mode
   if (y_type == MBLOCK) {
      int   delcount;
      int   incr = 0;
      BlockDef bd;
      long   j;
      int   c = gchar_cursor();
      ColNr   endcol2 = 0;

      if (dir == FORWARD && c != ZERO) {
         getvcol(curPor, &curPor->cursor, NULL, NULL, &col);

         // move to start of next multi-byte character
         curPor->cursor.col += utfCharLen(ml_get_cursor());
         ++col;
      } else
         getvcol(curPor, &curPor->cursor, &col, NULL, &endcol2);

      col += curPor->cursor.coladd;
      curPor->cursor.coladd = 0;
      bd.textcol = 0;
      for (i = 0; i < y_size; ++i) {
         int spaces = 0;
         CharTableSize   cts;

         bd.startspaces = 0;
         bd.endspaces = 0;
         vcol = 0;
         delcount = 0;

         // add a new line
         if (curPor->cursor.lnum > curBook->mem.lineCount) {
            if (ml_append(curBook->mem.lineCount, (CS)"", (ColNr)1, false) == FAIL)
               break;
            ++nr_lines;
         }
         // get the old line and advance to the position to insert at
         oldp = ml_get_curline();
         oldlen = ml_get_curline_len();
         bookInitCharsForKeywordsSizeArg(&cts, curPor, curPor->cursor.lnum, 0, oldp, oldp);

         while (cts.cts_vcol < col && *cts.cts_ptr != ZERO) {
            // Count a tab for what it's worth (if list mode not on)
            incr = lbr_chartabsize_adv(&cts);
            cts.cts_vcol += incr;
         }
         vcol = cts.cts_vcol;
         ptr = cts.cts_ptr;
         bd.textcol = (ColNr)(ptr - oldp);
         clear_chartabsize_arg(&cts);

         char shortline = (vcol < col) || (vcol == col && !*ptr) ;

         if (vcol < col) // line too short, pad with spaces
            bd.startspaces = col - vcol;
         ei (vcol > col) {
            bd.endspaces = vcol - col;
            bd.startspaces = incr - bd.endspaces;
            --bd.textcol;
            delcount = 1;
            bd.textcol -= (*mb_head_off)(oldp, oldp + bd.textcol);
            if (oldp[bd.textcol] != TAB) {
               //Only a Tab can be split into spaces. Other characters will have to be moved 
               //to after the block, causing misalignment.
               delcount = 0;
               bd.endspaces = 0;
            }
         }

         yanklen = (int)y_array[i].len;

         if ((flags & PUT_BLOCK_INNER) == 0) {
            // calculate number of spaces required to fill right side of block
            spaces = y_width + 1;
            bookInitCharsForKeywordsSizeArg(&cts, curPor, 0, 0, y_array[i].c, y_array[i].c);

            while (*cts.cts_ptr != ZERO) {
               spaces -= lbr_chartabsize_adv(&cts);
               cts.cts_vcol = 0;
            }
            clear_chartabsize_arg(&cts);
            if (spaces < 0)
               spaces = 0;
          }

         //Insert the new text. First check for multiplication overflow.
         if (yanklen + spaces != 0
              && count > ((INT_MAX - (bd.startspaces + bd.endspaces)) / (yanklen + spaces))
         ) {
            emsg(_(e_resulting_text_too_long));
            break;
         }

         totlen = count * (yanklen + spaces) + bd.startspaces + bd.endspaces;
         newp = alloc(totlen + oldlen + 1);

         // copy part up to cursor to new line
         ptr = newp;
         MEMMOVE(ptr, oldp, (Unt)bd.textcol);
         ptr += bd.textcol;

         // may insert some spaces before the new text
         memset(ptr, ' ', (Unt)bd.startspaces);
         ptr += bd.startspaces;

         // insert the new text
         for (j = 0; j < count; ++j) {
            MEMMOVE(ptr, y_array[i].c, (Unt)yanklen);
            ptr += yanklen;

            // insert block's trailing spaces only if there's text behind
            if ((j < count - 1 || !shortline) && spaces > 0) {
               memset(ptr, ' ', (Unt)spaces);
               ptr += spaces;
            } else
               totlen -= spaces;  // didn't use these spaces
         }

         // may insert some spaces after the new text
         memset(ptr, ' ', (Unt)bd.endspaces);
         ptr += bd.endspaces;

         // move the text after the cursor to the end of the line.
         MEMMOVE(ptr, oldp + bd.textcol + delcount,
               (Unt)(oldlen - bd.textcol - delcount + 1));
         ml_replace(curPor->cursor.lnum, newp, false);

         ++curPor->cursor.lnum;
         if (i == 0)
            curPor->cursor.col += bd.startspaces;
      }

      changed_lines(lnum, 0, curPor->cursor.lnum, nr_lines);

      // Set '[ mark.
      curBook->opStart = curPor->cursor;
      curBook->opStart.lnum = lnum;

      // adjust '] mark
      curBook->opEnd.lnum = curPor->cursor.lnum - 1;
      curBook->opEnd.col = bd.textcol + totlen - 1;
      if (curBook->opEnd.col < 0)
         curBook->opEnd.col = 0;
      curBook->opEnd.coladd = 0;
      if (flags & PUT_CURSEND) {
         ColNr len;

         curPor->cursor = curBook->opEnd;
         curPor->cursor.col++;

         // in Insert mode we might be after the ZERO, correct for that
         len = ml_get_curline_len();
         if (curPor->cursor.col > len)
            curPor->cursor.col = len;
      } else
         curPor->cursor.lnum = lnum;
   } else {
      Pos necursor;

      yanklen = (int)y_array[0].len;

      // Character or Line mode
      if (y_type == MCHAR) {
         // if type is MCHAR, FORWARD is the same as BACKWARD on the next char
         if (dir == FORWARD && gchar_cursor() != ZERO) {
            int bytelen = utfCharLen(ml_get_cursor());

            // put it on the next of the multi-byte character.
            col += bytelen;
            if (yanklen) {
               curPor->cursor.col += bytelen;
               curBook->opEnd.col += bytelen;
            }
         }
         curBook->opStart = curPor->cursor;
      }
      // Line mode: BACKWARD is the same as FORWARD on the previous line
      ei (dir == BACKWARD)
         --lnum;
      necursor = curPor->cursor;

      // simple case: insert into one line at a time
      if (y_type == MCHAR && y_size == 1) {
         LineNr   end_lnum = 0; // init for gcc
         LineNr   start_lnum = lnum;
         int      first_byte_off = 0;

         if (VIsual_active) {
            end_lnum = curBook->visual.vi_end.lnum;
            if (end_lnum < curBook->visual.vi_start.lnum)
               end_lnum = curBook->visual.vi_start.lnum;
            if (end_lnum > start_lnum) {
               Pos   pos;

               // "col" is valid for the first line, in following lines the virtual column needs 
               // to be used.  Matters for multi-byte characters.
               pos.lnum = lnum;
               pos.col = col;
               pos.coladd = 0;
               getvcol(curPor, &pos, NULL, &vcol, NULL);
            }
         }

         if (count == 0 || yanklen == 0) {
            if (VIsual_active)
                lnum = end_lnum;
         } ei (count > INT_MAX / yanklen)
            // multiplication overflow
            emsg(_(e_resulting_text_too_long));
         else {
            totlen = count * yanklen;
            do {
               oldp = ml_get(lnum);
               oldlen = ml_get_len(lnum);
               if (lnum > start_lnum) {
               Pos   pos;

               pos.lnum = lnum;
               if (getvpos(&pos, vcol) == OK)
                   col = pos.col;
               else
                   col = MAXCOL;
                }
               if (VIsual_active && col > oldlen) {
                  lnum++;
                  continue;
               }
               newp = alloc(totlen + oldlen + 1);
               MEMMOVE(newp, oldp, (Unt)col);
               ptr = newp + col;
               for (i = 0; i < count; ++i) {
                  MEMMOVE(ptr, y_array[0].c, (Unt)yanklen);
                  ptr += yanklen;
               }
               MEMMOVE(ptr, oldp + col, (Unt)(oldlen - col) + 1);       // +1 for ZERO

                // compute the byte offset for the last character
                first_byte_off = mb_head_off(newp, ptr - 1);

                // Note: this may free "newp"
                ml_replace(lnum, newp, false);

                inserted_bytes(lnum, col, totlen);

                // Place cursor on last putted char.
                if (lnum == curPor->cursor.lnum)
                {
               // make sure curPor->virtCol is updated
               changed_cline_bef_curs();
               invalidate_botline();
               curPor->cursor.col += (ColNr)(totlen - 1);
                }
                if (VIsual_active)
               lnum++;
            } while (VIsual_active && lnum <= end_lnum);

            if (VIsual_active) // reset lnum to the last visual line
               lnum--;
         }

         // put '] at the first byte of the last character
         curBook->opEnd = curPor->cursor;
         curBook->opEnd.col -= first_byte_off;

         // For "CTRL-O p" in Insert mode, put cursor after last char
         if (totlen && (restart_edit != 0 || (flags & PUT_CURSEND)))
            ++curPor->cursor.col;
         else
            curPor->cursor.col -= first_byte_off;
      } else {
         LineNr   new_lnum = necursor.lnum;
         int      indent;
         int      orig_indent = 0;
         int      indent_diff = 0;   // init for gcc
         int      first_indent = true;
         int      lendiff = 0;
         long   cnt;

         if (flags & PUT_FIXINDENT)
            orig_indent = get_indent();

         // Insert at least one line.  When y_type is MCHAR, break the first line in two.
         for (cnt = 1; cnt <= count; ++cnt) {
            i = 0;
            if (y_type == MCHAR) {
               // Split the current line in two at the insert position.
               // First insert y_array[size - 1] in front of second line.
               // Then append y_array[0] to first line.
               lnum = necursor.lnum;
               ptr = ml_get(lnum) + col;
               totlen = (int)y_array[y_size - 1].len;
               newp = alloc(ml_get_len(lnum) - col + totlen + 1);
               STRCPY(newp, y_array[y_size - 1].c);
               STRCPY(newp + totlen, ptr);
               // insert second line
               ml_append(lnum, newp, (ColNr)0, false);
               ++new_lnum;
               eeglFree(newp);

               oldp = ml_get(lnum);
               newp = alloc(col + yanklen + 1); // copy first part of line
               MEMMOVE(newp, oldp, (Unt)col); // append to first line
               MEMMOVE(newp + col, y_array[0].c, (Unt)(yanklen + 1));
               ml_replace(lnum, newp, false);

               curPor->cursor.lnum = lnum;
               i = 1;
            }

            for (; i < y_size; ++i) {
                if (y_type != MCHAR || i < y_size - 1) {
               if (ml_append(lnum, y_array[i].c, (ColNr)0, false) == FAIL)
                   goto error;
               new_lnum++;
               }
                lnum++;
                ++nr_lines;
                if (flags & PUT_FIXINDENT) {
               Pos   old_pos = curPor->cursor;

               curPor->cursor.lnum = lnum;
               ptr = ml_get(lnum);
               if (cnt == count && i == y_size - 1)
                   lendiff = ml_get_len(lnum);
               if (*ptr == '#' && preprocs_left())
                   indent = 0;     // Leave # lines at start
               ei (*ptr == ZERO)
                   indent = 0;     // Ignore empty lines
               ei (first_indent)
               {
                   indent_diff = orig_indent - get_indent();
                   indent = orig_indent;
                   first_indent = false;
               }
               ei ((indent = get_indent() + indent_diff) < 0)
                   indent = 0;
               (void)set_indent(indent, 0);
               curPor->cursor = old_pos;
               // remember how many chars were removed
               if (cnt == count && i == y_size - 1)
                   lendiff -= ml_get_len(lnum);
                }
            }
            if (cnt == 1)
               new_lnum = lnum;
         }

   error:
         // Adjust marks.
         if (y_type == MLINE) {
            curBook->opStart.col = 0;
           if (dir == FORWARD)
                curBook->opStart.lnum++;
         }
         markAdjust(curBook->opStart.lnum + (y_type == MCHAR), (LineNr)MAXLNUM, nr_lines, 0L, true);

         // note changed text for displaying and folding
         if (y_type == MCHAR)
            changed_lines(curPor->cursor.lnum, col, curPor->cursor.lnum + 1, nr_lines);
         else
            changed_lines(curBook->opStart.lnum, 0, curBook->opStart.lnum, nr_lines);
         if (y_current_used != NULL && (y_current_used != y_current
                       || y_current->y_array != y_array)
         ) {
            //Something invoked through changed_lines() has changed the
            //yank buffer, e.g. a GUI clipboard callback.
            emsg(_(e_yank_register_changed_while_using_it));
            goto end;
         }

         // Put the '] mark on the first byte of the last inserted character.
         // Correct the length for change in indent.
         curBook->opEnd.lnum = new_lnum;
         col = MAX(0, (ColNr)y_array[y_size - 1].len - lendiff);
         if (col > 1) {
            curBook->opEnd.col = col - 1;
            if (y_array[y_size - 1].len > 0)
                curBook->opEnd.col -= mb_head_off(y_array[y_size - 1].c,
                        y_array[y_size - 1].c + y_array[y_size - 1].len - 1);
         } else
            curBook->opEnd.col = 0;

         if (flags & PUT_CURSLINE) {
            // ":put": put cursor on last inserted line
            curPor->cursor.lnum = lnum;
            beginline(BL_WHITE | BL_FIX);
         } ei (flags & PUT_CURSEND) {
            // put cursor after inserted text
            if (y_type == MLINE) {
               if (lnum >= curBook->mem.lineCount)
                  curPor->cursor.lnum = curBook->mem.lineCount;
               else
                  curPor->cursor.lnum = lnum + 1;
               curPor->cursor.col = 0;
            } else {
               curPor->cursor.lnum = new_lnum;
               curPor->cursor.col = col;
               curBook->opEnd = curPor->cursor;
               if (col > 1)
                  curBook->opEnd.col = col - 1;
            }
         } ei (y_type == MLINE) {
            // put cursor on first non-blank in first inserted line
            curPor->cursor.col = 0;
            if (dir == FORWARD)
                ++curPor->cursor.lnum;
            beginline(BL_WHITE | BL_FIX);
         } else   // put cursor on first inserted character
            curPor->cursor = necursor;
      }
   }

   msgmore(nr_lines);
   curPor->setCursWant = true;

   // Make sure the cursor is not after the ZERO.
   int len = ml_get_curline_len();
   if (curPor->cursor.col > len) {
      curPor->cursor.col = len;
   }

end:
   if (commModifierG.cmod_flags & CMOD_LOCKMARKS) {
      curBook->opStart = orig_start;
      curBook->opEnd = orig_end;
   }
   if (allocated)
      eeglFree(insertText.c);
   if (regname == '=')
      eeglFree(y_array);

    VIsual_active = false;

    // If the cursor is past the end of the line put it at the end.
    adjust_cursor_eol();
}

// Return the character name of the register with the given number.
int
get_register_name(int num) {
   if (num == -1)
      return '"';
   ei (num < 10)
      return num + '0';
   ei (num == DELETION_REGISTER)
      return '-';
   ei (num == STAR_REGISTER)
      return '*';
   ei (num == PLUS_REGISTER)
      return '+';
   else
      return num + 'a' - 10;
}

// Return the index of the register "" points to.
int
get_unname_register(void) {
   return y_previous == NULL ? -1 : y_previous - &y_regs[0];
}

// ":dis" and ":registers": Display the contents of the yank registers.
void
c_display(Invocation* invo) {
   int      i, n;
   long   j;
   Byte   *p;
   YankReg   *yb;
   int      name;
   Byte   *arg = invo->arg;
   int      clen;
   int      type;
   Text   insert;

   if (arg && *arg == ZERO)
      arg = NULL;
   char flags = getDecoFlags(HLF_8);

   // Hilite the title
   msg_puts_title(_("\nType Name Content"));
   for (i = -1; i < NUM_REGISTERS && !gotInterruptG; ++i) {
      name = get_register_name(i);
      switch (get_reg_type(name, NULL)) {
      case MLINE: type = 'l'; break;
      case MCHAR: type = 'c'; break;
      default:   type = 'b'; break;
      }
      if (arg && firstOccurrence(arg, name) == NULL
#ifdef ONE_CLIPBOARD
          // Star register and plus register contain the same thing.
         && (name != '*' || firstOccurrence(arg, '+') == NULL)
#endif
         )
          continue;       // did not ask for this register

      // Adjust register name for "unnamed" in 'clipboard'.
      // When it's a clipboard register, fill it with the current contents
      // of the clipboard.
      clipGetDefaultRegister(&name);
      (void)may_get_selection(name);

      if (i == -1) {
         if (y_previous)
            yb = y_previous;
         else
            yb = &(y_regs[0]);
      } else
          yb = &(y_regs[i]);

      if (name == MB_TOLOWER(redir_reg)
         || (firstOccurrence((CS)"\"*+", redir_reg) != NULL &&
             (yb == y_previous || yb == &y_regs[0])))
          continue;       // do not list register being written to, the
                // pointer can be freed

      if (yb->y_array) {
         int do_show = false;
         for (j = 0; !do_show && j < yb->y_size; ++j)
            do_show = !message_filtered(yb->y_array[j].c);

         if (do_show || yb->y_size == 0) {
            msg_putchar('\n');
            msg_puts(S"  ");
            msg_putchar(type);
            msg_puts(S"  ");
            msg_putchar('"');
            msg_putchar(name);
            msg_puts(S"   ");

            n = (int)visibleColsG - 11;
            for (j = 0; j < yb->y_size && n > 1; ++j) {
               if (j) {
                  msgPutsDeco(S"^J", flags);
                  n -= 2;
               }
               for (p = yb->y_array[j].c; *p != ZERO && (n -= bookPtr2Cells(p)) >= 0; ++p) {
                  clen = utfCharLen(p);
                  msgTranslatedSlice((Text){p, clen});
                  p += clen - 1;
               }
            }
            if (n > 1 && yb->y_type == MLINE)
               msgPutsDeco(S"^J", flags);
            out_flush();          // show one line at a time
          }
          ui_breakcheck();
      }
   }

   // display last inserted text
   insert = get_last_insert();
   if ((p = insert.c) != NULL
        && (arg || firstOccurrence(arg, '.') != NULL) && !gotInterruptG && !message_filtered(p)
   ) {
      msg_puts(S"\n  c  \".   ");
      dis_msg(p, true);
   }

   // display last command line
   if (lastCommlineG != NULL && (arg == NULL || firstOccurrence(arg, ':') != NULL)
                && !gotInterruptG && !message_filtered(lastCommlineG))
   {
      msg_puts(S"\n  c  \":   ");
      dis_msg(lastCommlineG, false);
   }

   // display current file name
   if (curBook->currFileName != NULL
       && (arg == NULL || firstOccurrence(arg, '%') != NULL) && !gotInterruptG
               && !message_filtered(curBook->currFileName))
    {
      msg_puts(S"\n  c  \"%   ");
      dis_msg(curBook->currFileName, false);
   }

   // display alternate file name
   if ((arg == NULL || firstOccurrence(arg, '%') != NULL) && !gotInterruptG) {
      Byte       *fname;
      LineNr    dummy;

      if (bookGetFnameByFileId(0, &fname, &dummy) != FAIL && !message_filtered(fname)) {
          msg_puts(S"\n  c  \"#   ");
          dis_msg(fname, false);
      }
   }

   // display last search pattern
   if (last_search_pat().len != 0
       && (!arg || firstOccurrence(arg, '/') != NULL) && !gotInterruptG
                  && !message_filtered(last_search_pat().c)
   ) {
      msg_puts(S"\n  c  \"/   ");
      dis_msg(last_search_pat().c, false);
   }

   // display last used expression
   if (expr_line && (!arg || firstOccurrence(arg, '=') != NULL)
              && !gotInterruptG && !message_filtered(expr_line)) {
      msg_puts(S"\n  c  \"=   ");
      dis_msg(expr_line, false);
   }
}

//display a string for do_dis(); truncate at end of screen line
private void
dis_msg(
   Byte   *p,
   int      skip_esc       // if true, ignore trailing ESC
){
   int n = (int)visibleColsG - 6;
   while (*p != ZERO && !(*p == ESC && skip_esc && *(p + 1) == ZERO)
         && (n -= bookPtr2Cells(p)) >= 0
   ) {
      int l = utfCharLen(p);
      msgTranslatedSlice((Text){p, l});
      p += l;
   }
   ui_breakcheck();
}

// Put a string into a register.  When the register is not empty, the string is appended.
private void
str_to_reg(
   OUT YankReg   *yReg,      // pointer to yank register
   int      yank_type,   // MCHAR, MLINE, MBLOCK, MAUTO
   CS str,      // string to put in register
   long   len,      // length of string
   long   blocklen,   // width of Visual block
   int      str_list)   // true if str is a CString
{
   int      type;         // MCHAR, MLINE or MBLOCK
   int      lnum;
   long   start;
   long   i;
   int      extra;
   int      extraline = 0;      // extra line at the end
   Byte   *s;
   Byte   **ss;

   if (yReg->y_array == NULL)      // NULL means empty register
      yReg->y_size = 0;

   if (yank_type == MAUTO)
      type = ((str_list || (len > 0 && (str[len - 1] == NL || str[len - 1] == ENTER)))
                          ? MLINE : MCHAR);
   else
      type = yank_type;

   // Count the number of lines within the string
   int newlines = 0;   // number of lines added
   Boole append = false;      // append to last line in register
   if (str_list) {
      for (ss = (Byte **) str; *ss != NULL; ++ss)
          ++newlines;
   } else {
      for (i = 0; i < len; i++) {
         if (str[i] == '\n')
            ++newlines;
      } 
      if (type == MCHAR || len == 0 || str[len - 1] != '\n') {
         extraline = 1;
         ++newlines;   // count extra newline at the end
      }
      if (yReg->y_size > 0 && yReg->y_type == MCHAR) {
         append = true;
         --newlines;   // uncount newline when appending first line
      }
   }

   // Without any lines make the register empty.
   if (yReg->y_size + newlines == 0) {
      EE_CLEAR(yReg->y_array);
      return;
   }

   // Allocate an array to hold the pointers to the new register lines.
   // If the register was not empty, move the existing lines to the new array.
   Text* pp = lallocZeroed((yReg->y_size + newlines) * sizeof(Text), true);
   for (lnum = 0; lnum < yReg->y_size; ++lnum)
      pp[lnum] = yReg->y_array[lnum];
   eeglFree(yReg->y_array);
   yReg->y_array = pp;
   long maxlen = 0;

   // Find the end of each line and save it into the array.
   if (str_list) {
      for (ss = (Byte **) str; *ss != NULL; ++ss, ++lnum) {
         pp[lnum].len = STRLEN(*ss);
         pp[lnum].c = copySubstr(*ss, pp[lnum].len);
         if (type == MBLOCK) {
            int charlen = mb_string2cells(*ss, -1);
            if (charlen > maxlen)
               maxlen = charlen;
         }
      }
   } else {
      for (start = 0; start < len + extraline; start += i + 1) {
         int charlen = 0;

         for (i = start; i < len;) { // find the end of the line
            if (str[i] == '\n')
                break;
            if (type == MBLOCK)
                charlen += mb_ptr2cells_len(str + i, len - i);

            if (str[i] == ZERO)
                i++; // registers can have ZERO chars
            else
                i += utfCharLen_len(str + i, len - i);
         }
         i -= start;         // i is now length of line
         if (charlen > maxlen)
            maxlen = charlen;
         if (append) {
            --lnum;
            extra = (int)yReg->y_array[lnum].len;
         } else
            extra = 0;
         s = alloc(i + extra + 1);
         if (extra)
            MEMMOVE(s, yReg->y_array[lnum].c, (Unt)extra);
         if (append)
            eeglFree(yReg->y_array[lnum].c);
         if (i > 0)
            MEMMOVE(s + extra, str + start, (Unt)i);
         extra += i;
         s[extra] = ZERO;
         yReg->y_array[lnum].c = s;
         yReg->y_array[lnum].len = extra;
         ++lnum;
         while (--extra >= 0) {
            if (*s == ZERO)
               *s = '\n';       // replace ZERO with newline
            ++s;
         }
         append = false;          // only first line is appended
      }
   }
   yReg->y_type = type;
   yReg->y_size = lnum;
   if (type == MBLOCK)
      yReg->y_width = (blocklen < 0 ? maxlen - 1 : blocklen);
   else
      yReg->y_width = 0;
   yReg->y_time_set = eeTime();
}

// Replace the contents of the '~' register with str.
void
dnd_yank_drag_data(CS str, long len) {
   YankReg* curr = y_current;
   y_current = &y_regs[TILDE_REGISTER];
   free_yank_all();
   str_to_reg(OUT y_current, MCHAR, str, len, 0L, false);
   y_current = curr;
}


//Return the type of a register. MAUTO for error. Used for getregtype().
Byte
get_reg_type(int regname, long *reglen) {
   switch (regname) {
   case '%':      // file name
   case '#':      // alternate file name
   case '=':      // expression
   case ':':      // last command line
   case '/':      // last search-pattern
   case '.':      // last inserted text
   case Ctrl_F:   // Filename under cursor
   case Ctrl_P:   // Path under cursor, expand via "path"
   case Ctrl_W:   // word under cursor
   case Ctrl_A:   // WORD (mnemonic All) under cursor
   case '_':      // black hole: always empty
       return MCHAR;
   }

   regname = may_get_selection(regname);

   if (regname != ZERO && !valid_yank_reg(regname, false))
      return MAUTO;

   get_yank_register(regname, false);

   if (y_current->y_array != NULL) {
      if (reglen != NULL && y_current->y_type == MBLOCK)
         *reglen = y_current->y_width;
      return y_current->y_type;
   }
   return MAUTO;
}

//When "flags" has GREG_LIST, return a list with text "s". Otherwise just return "s".
private CS
getreg_wrap_one_line(CS s, int flags) {
   if ((flags & GREG_LIST) != 0){
      List *list = list_alloc();
      if (list_append_string(list, NULL, -1) == FAIL) {
         list_free(list);
         return NULL;
      }
      list->first->c.string = s;
      return (CS)list;
   }
   return s;
}

//Return the contents of a register as a single allocated string or as a list.
//Used for "@r" in expressions and for getreg(). Return NULL for error.
//Flags:
//  GREG_NO_EXPR   Do not allow expression register
//  GREG_EXPR_SRC   For the expression register: return expression itself,
//        not the result of its evaluation.
//  GREG_LIST   Return a list of lines instead of a single string.
CS
get_reg_contents(int regname, int flags) {
   LineNr   i;
   Byte   *retval;
   int      allocated;
   long   len;

   // Don't allow using an expression register inside an expression
   if (regname == '=') {
      if (flags & GREG_NO_EXPR)
         return NULL;
      if (flags & GREG_EXPR_SRC)
         return getreg_wrap_one_line(get_expr_line_src(), flags);
      return getreg_wrap_one_line(get_expr_line(), flags);
   }

   if (regname == '@')       // "@@" is used for unnamed register
      regname = '"';

   // check for valid regname
   if (regname != ZERO && !valid_yank_reg(regname, false))
      return NULL;

   regname = may_get_selection(regname);

   if (get_spec_reg(regname, &retval, &allocated, false)) {
      if (retval == NULL)
         return NULL;
      if (allocated)
         return getreg_wrap_one_line(retval, flags);
      return getreg_wrap_one_line(copyStr(retval), flags);
   }

   get_yank_register(regname, false);
   if (y_current->y_array == NULL)
      return NULL;

   if ((flags & GREG_LIST) != 0){
      List   *list = list_alloc();
      Boole error = false;
      for (i = 0; i < y_current->y_size; ++i) {
         if (list_append_string(list, y_current->y_array[i].c, -1) == FAIL)
            error = true;
      } 
      if (error) {
         list_free(list);
         return NULL;
      }
      return (Byte*)list;
   }

   // Compute length of resulting string.
   len = 0;
   for (i = 0; i < y_current->y_size; ++i) {
      len += (long)y_current->y_array[i].len;

      // Insert a newline between lines and after the last line if y_type is MLINE.
      if (y_current->y_type == MLINE || i < y_current->y_size - 1)
         ++len;
   }

   retval = alloc(len + 1);

   // Copy the lines of the yank register into the string.
   len = 0;
   for (i = 0; i < y_current->y_size; ++i) {
      STRCPY(retval + len, y_current->y_array[i].c);
      len += (long)y_current->y_array[i].len;

      // Insert a newline between lines and after the last line if y_type is MLINE.
      if (y_current->y_type == MLINE || i < y_current->y_size - 1)
          retval[len++] = '\n';
    }
    retval[len] = ZERO;

    return retval;
}

private int
init_write_reg(
   int      name,
   YankReg   **old_y_previous,
   YankReg   **old_y_current,
   int      must_append,
   int      *yank_type UNUSED
) {
   if (!valid_yank_reg(name, true)) {     // check for valid reg name
      emsg_invreg(name);
      return FAIL;
   }

   // Don't want to change the current (unnamed) register
   *old_y_previous = y_previous;
   *old_y_current = y_current;

   get_yank_register(name, true);
   if (!y_append && !must_append)
      free_yank_all();
   return OK;
}

private void
finish_write_reg(
   int      name,
   YankReg   *old_y_previous,
   YankReg   *old_y_current)
{
   // Send text of clipboard register to the clipboard.
   may_set_selection();

   // ':let @" = "val"' should change the meaning of the "" register
   if (name != '"')
      y_previous = old_y_previous;
   y_current = old_y_current;
}

//Store string "str" in register "name".
//"maxlen" is the maximum number of bytes to use, -1 for all bytes.
//If "must_append" is true, always append to the register.  Otherwise append
//if "name" is an uppercase letter.
//Note: "maxlen" and "must_append" don't work for the "/" register.
//Careful: 'str' is modified, you may have to use a copy!
//If "str" ends in '\n' or '\r', use linewise, otherwise use characterwise.
void
write_reg_contents(
   int      name,
   CS str,
   int maxlen,
   int must_append)
{
   write_reg_contents_ex(name, str, maxlen, must_append, MAUTO, 0L);
}

void
write_reg_contents_lst(
   int      name,
   Byte   **strings,
   int      maxlen UNUSED,
   int      must_append,
   int      yank_type,
   long   block_len)
{
   YankReg  *old_y_previous, *old_y_current;

   if (name == '/' || name == '=') {
      Byte   *s;

      if (strings[0] == NULL)
         s = E;
      ei (strings[1] != NULL) {
         emsg(_(e_search_pattern_and_expression_register_may_not_contain_two_or_more_lines));
         return;
      } else
         s = strings[0];
      write_reg_contents_ex(name, s, -1, must_append, yank_type, block_len);
      return;
   }

   if (name == '_')       // black hole: nothing to do
      return;

   if (init_write_reg(name, &old_y_previous, &old_y_current, must_append,
      &yank_type) == FAIL)
   return;

   str_to_reg(OUT y_current, yank_type, (CS)strings, -1, block_len, true);
   finish_write_reg(name, old_y_previous, old_y_current);
}

void
write_reg_contents_ex(
   int name,
   CS str,
   int maxlen,
   int must_append,
   int yank_type,
   long block_len
) {
   YankReg   *old_y_previous, *old_y_current;
   long   len = (maxlen >= 0) ? maxlen :  (long)STRLEN(str);
      
   // Special case: '/' search pattern
   if (name == '/') {
      set_last_search_pat(str, RE_SEARCH, true, true);
      return;
   }

   if (name == '#') {
      Book   *buf;

      if (EE_ISDIGIT(*str)) {
         int   num = atoi((char *)str);
         buf = bookFindFileByBookNr(num);
         if (!buf)
            showErrFmtMsg(_(e_book_nr_does_not_exist), (long)num);
      } else
         buf = bookFindFileByBookNr(booklistFindPattern(str, str + len, true, false, false));
      if (!buf)
         return;
      curPor->altFnum = buf->fiNum;
      return;
   }

   if (name == '=') {
      CS p = copySubstr(str, (Unt)len);
      CS s;
      if (must_append && expr_line) {
         s = concat_str(expr_line, p);
         eeglFree(p);
         p = s;
      }
      set_expr_line(p, NULL);
      return;
   }

   if (name == '_')       // black hole: nothing to do
      return;

   if (init_write_reg(name, &old_y_previous, &old_y_current, must_append, &yank_type) == FAIL)
      return;

   str_to_reg(OUT y_current, yank_type, str, len, block_len, false);
   finish_write_reg(name, old_y_previous, old_y_current);
}

//}}}
//{{{clipboard

//Functions for copying and pasting text between applications.
//This is always included in a GUI version, but may also be included when the
//clipboard and mouse is available to a terminal version such as xterm.
//Note: there are some more functions in ops.c that handle selection stuff.
//
//Also note that the majority of functions here deal with the X 'primary'
//(visible - for Visual mode use) selection, and only that. There are no
//versions of these for the 'clipboard' selection, as Visual mode has no use for them.

//EE_ATOM_NAME is the older Eegl-specific selection type for X11.  Still
//supported for when a mix of Eegl versions is used.
#define EE_ATOM_NAME "_EE_TEXT"
# define SELECT_MODE_CHAR   0
# define SELECT_MODE_WORD   1
# define SELECT_MODE_LINE   2

//{{{wayland

//Mime types we support sending and receiving
//Mimes with a lower index in the array are prioritized first when we are receiving data.
private CS supported_mimes[] = {SMAP((CS),
   EE_ATOM_NAME,
   "text/plain;charset=utf-8",
   "text/plain",
   "UTF8_STRING",
   "STRING",
   "TEXT"
 )};

private void clip_wl_receive_data(ClipBoard *cbd, CS mime_type, int fd);
private void clip_wl_request_selection(ClipBoard *cbd);
private void clip_wl_send_data(const char *mime_type, int fd, WaylandSelection);
private int clip_wl_own_selection(ClipBoard *cbd);
private void clip_wl_lose_selection(ClipBoard *cbd);
private void clip_wl_set_selection(ClipBoard *cbd);
private void clip_wl_selection_cancelled(WaylandSelection selection);


//}}}

//Selection stuff using Visual mode, for cutting and pasting text to other windows.

//Call this to initialise the clipboard. Pass it false if the clipboard code is included, but the 
//clipboard can not be used, or true if the clipboard can be used. Eg unix may call this with 
//false, then call it again with true if the GUI starts.
void
clip_init(){
   ClipBoard* cb = &clipboard;
   cb->owned      = false;
   cb->start.lnum = 0;
   cb->start.col  = 0;
   cb->end.lnum   = 0;
   cb->end.col    = 0;
   cb->state      = SELECT_CLEARED;
}

//Check whether the VIsual area has changed, and if so try to become the owner
//of the selection, and free any old converted selection we may still have
//lying around.  If the VIsual mode has ended, make a copy of what was
//selected so we can still give it to others.   Will probably have to make sure
//this is called whenever VIsual mode is ended.
void
clip_update_selection(ClipBoard *clip){
   Pos start, end;

   // If visual mode is only due to a redo command ("."), then ignore it
   if (!isRedoVisualBusy && VIsual_active && (stateG & MODE_NORMAL)) {
      if (LT_POS(VIsual, curPor->cursor)) {
         start = VIsual;
         end = curPor->cursor;
         end.col += utfCharLen(ml_get_cursor()) - 1;
      } else {
         start = curPor->cursor;
         end = VIsual;
      }
      if (!EQUAL_POS(clip->start, start)
         || !EQUAL_POS(clip->end, end)
         || clip->vmode != VIsual_mode)
      {
         clip_clear_selection(clip);
         clip->start = start;
         clip->end = end;
         clip->vmode = VIsual_mode;
         freeSelection(clip);
         clip_own_selection(clip);
         clip_gen_set_selection(clip);
      }
   }
}

private int
clip_gen_own_selection(ClipBoard *cbd){
   return clip_wl_own_selection(cbd);
}

private void
clip_own_selection(ClipBoard *cbd){
   //Also want to check somehow that we are reading from the keyboard rather than a mapping etc
   //Always own the selection, we might have lost it without being notified, e.g. during a ":sh" 
   //command.
   int was_owned = cbd->owned;

   cbd->owned = (clip_gen_own_selection(cbd) == OK);
   if (!was_owned && cbd == &clipboard) {
      // May have to show a different kind of hiliting for the selected area. There is no specific
      // redraw command for this, just redraw all portals into the current book.
      if (cbd->owned
            && (get_real_state() == MODE_VISUAL)
            && getDecoFlags(HLF_V) != getDecoFlags(HLF_VNC)
      )
         drawCurBookLater(UPD_INVERTED_ALL);
   }
}

private void
clip_gen_lose_selection(ClipBoard *cbd) {
   clip_wl_lose_selection(cbd);
}

void
clip_lose_selection(ClipBoard* cbd) {
   Boole isVisual = (cbd == &clipboard);
   freeSelection(cbd);
   cbd->owned = false;
   if (isVisual)
      clip_clear_selection(cbd);
   clip_gen_lose_selection(cbd);
}

private void
clip_copy_selection(ClipBoard *clip) {
   if (VIsual_active && (stateG & MODE_NORMAL) != 0) {
      clip_update_selection(clip);
      freeSelection(clip);
      clip_own_selection(clip);
      if (clip->owned)
         clip_get_selection(clip);
      clip_gen_set_selection(clip);
   }
}

private int global_change_count = 0; // if set, inside a start_global_changes
private int clipboard_needs_update = false; // clipboard needs to be updated
private int clip_did_set_selection = true;

// Save state and reset it.
void
start_global_changes(void) {
   if (++global_change_count > 1)
      return;
   clipboard_needs_update = false;

   if (clip_did_set_selection) {
      clip_did_set_selection = false;
   }
}

//Return true if setting the clipboard was postponed, it already contains the right text.
private int
is_clipboard_needs_update(void){
   return clipboard_needs_update;
}

void
end_global_changes(void){
   if (--global_change_count > 0)
      // recursive
      return;
   if (!clip_did_set_selection) {
      clip_did_set_selection = true;
      if (clipboard_needs_update) {
         // only store something in the clipboard if we have yanked anything to it
         clip_own_selection(&clipboard);
         clip_gen_set_selection(&clipboard);
      }
   }
   clipboard_needs_update = false;
}

// Called when Visual mode is ended: update the selection.
void
clip_auto_select(void){
   clip_copy_selection(&clipboard);
}

// Stuff for general mouse selection, without using Visual mode.

//Compare two screen positions ala strcmp()
private int
clip_compare_pos(int row1, int col1, int row2, int col2) {
   if (row1 > row2) return(1);
   if (row1 < row2) return(-1);
   if (col1 > col2) return(1);
   if (col1 < col2) return(-1);
   return(0);
}

// "how" flags for clip_invert_area()
#define CLIP_CLEAR   1
#define CLIP_SET     2
#define CLIP_TOGGLE  3

#define CLIP_ZINDEX 32000

//Invert or un-invert a rectangle of the screen. "invert" is true if the result is inverted.
private void
clip_invert_rectangle(
   ClipBoard* cbd,
   int row_arg,
   int col_arg,
   int height_arg,
   int width_arg,
   Boole invert
) {
   int row = row_arg;
   int col = col_arg;
   int height = height_arg;
   int width = width_arg;

   // this goes on top of all popup portals
   screenZindexG = CLIP_ZINDEX;

   if (col < cbd->min_col) {
      width -= cbd->min_col - col;
      col = cbd->min_col;
   }
   if (width > cbd->max_col - col)
      width = cbd->max_col - col;
   if (row < cbd->min_row) {
      height -= cbd->min_row - row;
      row = cbd->min_row;
   }
   if (height > cbd->max_row - row + 1)
      height = cbd->max_row - row + 1;
   screen_draw_rectangle(row, col, height, width, invert);
   screenZindexG = 0;
}

//Invert a region of the display between a starting and ending row and column Values for "how":
//CLIP_CLEAR:  undo inversion
//CLIP_SET:    set inversion
//CLIP_TOGGLE: set inversion if pos1 < pos2, undo inversion otherwise.
//0: invert (GUI only).
private void
clip_invert_area(
   ClipBoard* cbd,
   int      row1,
   int      col1,
   int      row2,
   int      col2,
   int      how
) {
   Boole invert = false;
   int max_col = cbd->max_col - 1;

   if (how == CLIP_SET)
      invert = true;

   // Swap the from and to positions so the from is always before
   if (clip_compare_pos(row1, col1, row2, col2) > 0) {
      int tmp_row, tmp_col;

      tmp_row = row1;
      tmp_col = col1;
      row1   = row2;
      col1   = col2;
      row2   = tmp_row;
      col2   = tmp_col;
   } ei (how == CLIP_TOGGLE)
      invert = true;

   // If all on the same line, do it the easy way
   if (row1 == row2) {
      clip_invert_rectangle(cbd, row1, col1, 1, col2 - col1, invert);
   } else {
      // Handle a piece of the first line
      if (col1 > 0) {
         clip_invert_rectangle(cbd, row1, col1, 1, (int)visibleColsG - col1, invert);
         row1++;
      }

      // Handle a piece of the last line
      if (col2 < max_col) {
         clip_invert_rectangle(cbd, row2, 0, 1, col2, invert);
         row2--;
      }

      // Handle the rectangle that's left
      if (row2 >= row1)
         clip_invert_rectangle(cbd, row1, 0, row2 - row1 + 1, (int)visibleColsG, invert);
   }
}

//Start, continue or end a modeless selection.  Used when editing the
//command-line, in the commline portal and when the mouse is in a popup portal.
void
clip_modeless(int button, int is_click, int is_drag){
   int repeat = ((clipboard.mode == SELECT_MODE_CHAR
      || clipboard.mode == SELECT_MODE_LINE) && (modMaskG & MOD_MASK_2CLICK))
      || (clipboard.mode == SELECT_MODE_WORD && (modMaskG & MOD_MASK_3CLICK));
   if (is_click && button == MOUSE_RIGHT) {
      //Right mouse button: If there was no selection, start one. Otherwise extend the 
      //existing selection.
      if (clipboard.state == SELECT_CLEARED)
         startSelection(mouseColG, mouseRowG, false);
      processSelection(button, mouseColG, mouseRowG, repeat);
   } ei (is_click)
      startSelection(mouseColG, mouseRowG, repeat);
   ei (is_drag) {
      // Don't try extending a selection if there isn't one.  Happens when
      // button-down is in the cmdline and them moving mouse upwards.
      if (clipboard.state != SELECT_CLEARED)
         processSelection(button, mouseColG, mouseRowG, repeat);
   } else // release
      processSelection(MOUSE_RELEASE, mouseColG, mouseRowG, false);
}

//Update the currently selected region by adding and/or subtracting from the
//beginning or end and inverting the changed area(s).
private void
clip_update_modeless_selection(ClipBoard* cb, int row1, int col1, int row2, int col2){
   // See if we changed at the beginning of the selection
   if (row1 != cb->start.lnum || col1 != (int)cb->start.col) {
      clip_invert_area(cb, row1, col1, (int)cb->start.lnum, cb->start.col, CLIP_TOGGLE);
      cb->start.lnum = row1;
      cb->start.col  = col1;
   }

   // See if we changed at the end of the selection
   if (row2 != cb->end.lnum || col2 != (int)cb->end.col) {
      clip_invert_area(cb, (int)cb->end.lnum, cb->end.col, row2, col2, CLIP_TOGGLE);
      cb->end.lnum = row2;
      cb->end.col  = col2;
   }
}

//Find the starting and ending positions of the word at the given row and
//column.  Only white-separated words are recognized here.
#define CHAR_CLASS(c)   (c <= ' ' ? ' ' : eeIsWordc(c))

private void
clip_get_word_boundaries(ClipBoard *cb, int row, int col) {
   if (row >= screenLinesRowsG || col >= screenLinesColsG || !drawHasLines())
      return;

   CS p = drawGetLinesWithOffset(row);
   if (p[col] == 0)
      --col;
   int start_class = CHAR_CLASS(p[col]);

   int temp_col = col;
   for ( ; temp_col > 0; temp_col--) {
      if (CHAR_CLASS(p[temp_col - 1]) != start_class && !(p[temp_col - 1] == 0))
          break;
   } 
   cb->word_start_col = temp_col;

   temp_col = col;
   for ( ; temp_col < screenLinesColsG; temp_col++) {
      if (CHAR_CLASS(p[temp_col]) != start_class && !(p[temp_col] == 0))
         break;
   } 
   cb->word_end_col = temp_col;
}

//Find the column position for the last non-whitespace character on the given
//line at or before start_col.
private int
clip_get_line_end(ClipBoard *cbd, int row){
   if (row >= screenLinesRowsG || !drawHasLines())
      return 0;
      
   int i;
   for (i = cbd->max_col; i > 0; i--) {
      if (drawGetLine(drawGetOffset(row) + i - 1) != ' ')
         break;
   } 
   return i;
}

private void
startSelection(int col, int row, int repeated_click) {
   ClipBoard   *cb = &clipboard;
   int row_cp = row;
   int col_cp = col;

   Portal* po = mouseFindPortal(&row_cp, &col_cp, FIND_POPUP);
   if (po && PORTAL_IS_POPUP(po) && popup_is_in_scrollbar(po, row_cp, col_cp))
      // click or double click in scrollbar does not start a selection
      return;

   if (cb->state == SELECT_DONE)
      clip_clear_selection(cb);

   row = check_row(row);
   col = check_col(col);

   cb->start.lnum  = row;
   cb->start.col   = col;
   cb->end       = cb->start;
   cb->origin_row  = (Short)cb->start.lnum;
   cb->state       = SELECT_IN_PROGRESS;
   if (po && PORTAL_IS_POPUP(po)) {
      //Click in a popup portal restricts selection to that portal, excluding the border.
      cb->min_col = po->windowCol + po->pup.border[3];
      cb->max_col = po->windowCol + popup_width(po) - po->pup.border[1] - po->pup.hasScrollbar;
      if (cb->max_col > screenLinesColsG)
         cb->max_col = screenLinesColsG;
      cb->min_row = po->windowRow + po->pup.border[0];
      cb->max_row = po->windowRow + popup_height(po) - 1 - po->pup.border[2];
   } else {
      cb->min_col = 0;
      cb->max_col = screenLinesColsG;
      cb->min_row = 0;
      cb->max_row = screenLinesRowsG;
   }

   if (repeated_click) {
      if (++cb->mode > SELECT_MODE_LINE)
         cb->mode = SELECT_MODE_CHAR;
   } else
      cb->mode = SELECT_MODE_CHAR;

   switch (cb->mode) {
   case SELECT_MODE_CHAR:
      cb->origin_start_col = cb->start.col;
      cb->word_end_col = clip_get_line_end(cb, (int)cb->start.lnum);
      break;

   case SELECT_MODE_WORD:
      clip_get_word_boundaries(cb, (int)cb->start.lnum, cb->start.col);
      cb->origin_start_col = cb->word_start_col;
      cb->origin_end_col   = cb->word_end_col;

      clip_invert_area(
         cb, (int)cb->start.lnum, cb->word_start_col, (int)cb->end.lnum, cb->word_end_col, CLIP_SET
      );
      cb->start.col = cb->word_start_col;
      cb->end.col   = cb->word_end_col;
      break;

   case SELECT_MODE_LINE:
      clip_invert_area(
         cb, (int)cb->start.lnum, 0, (int)cb->start.lnum, (int)visibleColsG, CLIP_SET
      );
      cb->start.col = 0;
      cb->end.col   = visibleColsG;
      break;
   }

   cb->prev = cb->start;

#ifdef DEBUG_SELECTION
   printf("Selection started at (%ld,%d)\n", cb->start.lnum, cb->start.col);
#endif
}

// Continue processing the selection
private void
processSelection(int button, int col, int row, Unt repeated_click) {
   ClipBoard   *cb = &clipboard;
   int diff;
   int slen = 1;   // cursor shape width

   if (button == MOUSE_RELEASE) {
      if (cb->state != SELECT_IN_PROGRESS)
         return;

      // Check to make sure we have something selected
      if (cb->start.lnum == cb->end.lnum && cb->start.col == cb->end.col) {
         cb->state = SELECT_CLEARED;
         return;
      }

#ifdef DEBUG_SELECTION
      printf("Selection ended: (%ld,%d) to (%ld,%d)\n", cb->start.lnum,
         cb->start.col, cb->end.lnum, cb->end.col);
#endif
      clip_copy_modeless_selection();

      cb->state = SELECT_DONE;
      return;
   }

   row = check_row(row);
   col = check_col(col);

   if (col == (int)cb->prev.col && row == cb->prev.lnum && !repeated_click)
      return;

   //When extending the selection with the right mouse button, swap the
   //start and end if the position is before half the selection
   if (cb->state == SELECT_DONE && button == MOUSE_RIGHT) {
      //If the click is before the start, or the click is inside the
      //selection and the start is the closest side, set the origin to the
      //end of the selection.
      if (clip_compare_pos(row, col, (int)cb->start.lnum, cb->start.col) < 0
         || (clip_compare_pos(row, col,
                     (int)cb->end.lnum, cb->end.col) < 0
             && (((cb->start.lnum == cb->end.lnum
                && cb->end.col - col > col - cb->start.col))
            || ((diff = (cb->end.lnum - row) -
                        (row - cb->start.lnum)) > 0
                || (diff == 0 && col < (int)(cb->start.col +
                         cb->end.col) / 2)))))
      {
          cb->origin_row = (Short)cb->end.lnum;
          cb->origin_start_col = cb->end.col - 1;
          cb->origin_end_col = cb->end.col;
      } else {
          cb->origin_row = (Short)cb->start.lnum;
          cb->origin_start_col = cb->start.col;
          cb->origin_end_col = cb->start.col;
      }
      if (cb->mode == SELECT_MODE_WORD && !repeated_click)
         cb->mode = SELECT_MODE_CHAR;
   }

   // set state, for when using the right mouse button
   cb->state = SELECT_IN_PROGRESS;

#ifdef DEBUG_SELECTION
   printf("Selection extending to (%d,%d)\n", row, col);
#endif

   if (repeated_click && ++cb->mode > SELECT_MODE_LINE)
      cb->mode = SELECT_MODE_CHAR;

   switch (cb->mode) {
   case SELECT_MODE_CHAR:
      // If we're on a different line, find where the line ends
      if (row != cb->prev.lnum)
         cb->word_end_col = clip_get_line_end(cb, row);

      // See if we are before or after the origin of the selection
      if (clip_compare_pos(row, col, cb->origin_row, cb->origin_start_col) >= 0) {
         if (col >= (int)cb->word_end_col)
            clip_update_modeless_selection(cb, cb->origin_row,
                cb->origin_start_col, row, (int)visibleColsG);
         else {
            clip_update_modeless_selection(cb, cb->origin_row, cb->origin_start_col, row, col + slen);
         }
      } else {
         if (col >= (int)cb->word_end_col)
            clip_update_modeless_selection(cb, row, cb->word_end_col,
                cb->origin_row, cb->origin_start_col + slen);
         else
            clip_update_modeless_selection(cb, row, col,
                cb->origin_row, cb->origin_start_col + slen);
      }
      break;

   case SELECT_MODE_WORD:
      // If we are still within the same word, do nothing
      if (row == cb->prev.lnum && col >= (int)cb->word_start_col
             && col < (int)cb->word_end_col && !repeated_click)
         return;

      // Get new word boundaries
      clip_get_word_boundaries(cb, row, col);

      // Handle being after the origin point of selection
      if (clip_compare_pos(row, col, cb->origin_row, cb->origin_start_col) >= 0)
         clip_update_modeless_selection(
            cb, cb->origin_row, cb->origin_start_col, row, cb->word_end_col
         );
      else
         clip_update_modeless_selection(
            cb, row, cb->word_start_col, cb->origin_row, cb->origin_end_col
         );
      break;

   case SELECT_MODE_LINE:
      if (row == cb->prev.lnum && !repeated_click)
         return;

      if (clip_compare_pos(row, col, cb->origin_row, cb->origin_start_col) >= 0)
         clip_update_modeless_selection(cb, cb->origin_row, 0, row, (int)visibleColsG);
      else
         clip_update_modeless_selection(cb, row, 0, cb->origin_row, (int)visibleColsG);
      break;
   }

   cb->prev.lnum = row;
   cb->prev.col  = col;

#ifdef DEBUG_SELECTION
   printf("Selection is: (%ld,%d) to (%ld,%d)\n", cb->start.lnum,
      cb->start.col, cb->end.lnum, cb->end.col);
#endif
}

// Called from outside to clear selected region from the display
void
clip_clear_selection(ClipBoard *cbd){
   if (cbd->state == SELECT_CLEARED)
      return;

   clip_invert_area(
      cbd, (int)cbd->start.lnum, cbd->start.col, (int)cbd->end.lnum, cbd->end.col, CLIP_CLEAR
   );
   cbd->state = SELECT_CLEARED;
}

// Clear the selection if any lines from "row1" to "row2" are inside of it.
void
clip_may_clear_selection(int row1, int row2){
   if (clipboard.state == SELECT_DONE
          && row2 >= clipboard.start.lnum
          && row1 <= clipboard.end.lnum)
      clip_clear_selection(&clipboard);
}

//Called before the screen is scrolled up or down.  Adjusts the line numbers
//of the selection.  Call with big number when clearing the screen.
void
clip_scroll_selection(int rows)  {    // negative for scroll down
   if (clipboard.state == SELECT_CLEARED)
      return;

   int lnum = clipboard.start.lnum - rows;
   if (lnum <= 0)
      clipboard.start.lnum = 0;
   ei (lnum >= screenLinesRowsG)   // scrolled off of the screen
      clipboard.state = SELECT_CLEARED;
   else
      clipboard.start.lnum = lnum;

   lnum = clipboard.end.lnum - rows;
   if (lnum < 0)         // scrolled off of the screen
      clipboard.state = SELECT_CLEARED;
   ei (lnum >= screenLinesRowsG)
      clipboard.end.lnum = screenLinesRowsG - 1;
   else
      clipboard.end.lnum = lnum;
}

// Convert from the GUI selection string into the '*'/'+' register.
private void
clip_yank_selection(int type, CS str, long len, ClipBoard* cbd) {
   YankReg* yReg = (cbd == &clipboard) ? getYRegister(PLUS_REGISTER) : getYRegister(STAR_REGISTER);
   freeSelection(cbd);
   str_to_reg(OUT yReg, type, str, len, -1, false);
}

//Copy the currently selected area into the '*' register so it will be available for pasting.
//When "both" is true also copy to the '+' register.
void
clip_copy_modeless_selection() {
   // Can't use screenLinesP unless initialized
   if (!drawHasLines())
      return;
      
   int row;
   int start_col;
   int end_col;
   int line_end_col;
   int add_newline_flag = false;
   int row1 = clipboard.start.lnum;
   int col1 = clipboard.start.col;
   int row2 = clipboard.end.lnum;
   int col2 = clipboard.end.col;

   //Make sure row1 <= row2, and if row1 == row2 that col1 <= col2.
   if (row1 > row2) {
      row = row1; 
      row1 = row2; 
      row2 = row;
      row = col1; 
      col1 = col2; 
      col2 = row;
   } ei (row1 == row2 && col1 > col2) {
      row = col1; 
      col1 = col2; 
      col2 = row;
   }
   if (col1 < clipboard.min_col)
      col1 = clipboard.min_col;
   if (col2 > clipboard.max_col)
      col2 = clipboard.max_col;
   if (row1 > clipboard.max_row || row2 < clipboard.min_row)
      return;
   if (row1 < clipboard.min_row)
      row1 = clipboard.min_row;
   if (row2 > clipboard.max_row)
      row2 = clipboard.max_row;

   // Create a temporary buffer for storing the text
   int len = ((row2 - row1 + 1) * visibleColsG + 1) * MB_MAXBYTES;
   CS buffer = alloc(len);

   // Process each row in the selection
   Byte* bufp;
   for (bufp = buffer, row = row1; row <= row2; row++) {
      start_col = (row == row1) ? col1 : clipboard.min_col;
      end_col = (row == row2) ? col2 : clipboard.max_col;
      line_end_col = clip_get_line_end(&clipboard, row);

      // See if we need to nuke some trailing whitespace
      if (end_col >= clipboard.max_col && (row < row2 || end_col > line_end_col)
      ){
         // Get rid of trailing whitespace
         end_col = line_end_col;
         if (end_col < start_col)
            end_col = start_col;

         // If the last line extended to the end, add an extra newline
         if (row == row2)
            add_newline_flag = true;
      }

      //If after the first row, we need to always add a newline
      if (row > row1 && drawGetLineWrap(row - 1) == 0)
         *bufp++ = NL;

      //Safety check for in case resizing went wrong
      if (row < screenLinesRowsG && end_col <= screenLinesColsG) {
         int off = drawGetOffset(row);
         for (int i = start_col; i < end_col; ++i) {
            // The base character is either in draw.c:screenLinesUCG[] or draw.c:screenLinesP[].
            Unt unicodeChar = drawGetScreenUnicodeChar(off + i);
            if (unicodeChar == 0)
               *bufp++ = drawGetLine(off + i);
            else {
               bufp += mb_char2bytes(unicodeChar, bufp);
               for (Unt ci = 0; ci < MAX_COMBINED_SYMBOLS; ++ci) {
                  Unt compChar = drawGetScreenComposingChar(off + i, ci);
                  // Add a composing character.
                  if (compChar == 0)
                     break;
                  bufp += mb_char2bytes(compChar, bufp);
               }
            }
         }
      }
   }

   // Add a newline at the end if the selection ended there
   if (add_newline_flag)
      *bufp++ = NL;

   // First cleanup any old selection and become the owner.
   freeSelection(&clipboard);
   clip_own_selection(&clipboard);

   // Yank the text into the '*' register.
   clip_yank_selection(MCHAR, buffer, (long)(bufp - buffer), &clipboard);

   // Make the register contents available to the outside world.
   clip_gen_set_selection(&clipboard);

   eeglFree(buffer);
}

private void
clip_gen_set_selection(ClipBoard *cbd){
   if (!clip_did_set_selection) {
      //Updating postponed, so that accessing the system clipboard won't
      //hang Eegl when accessing it many times (e.g. on a :g command).
      if (cbd == &clipboard) {
          clipboard_needs_update = true;
          return;
      }
   }
   clip_wl_set_selection(cbd);
}

private void
clip_gen_request_selection(ClipBoard *cbd){
   clip_wl_request_selection(cbd);
}

//SELECTION / PRIMARY ('*')
//
//Text selection stuff that uses the selection register '*'.  It is the last text we
//had highlighted with VIsual mode.  With mouse support, clicking the middle
//button performs the paste, otherwise you will need to do <"*p>. "
//If not under X, it is synonymous with the clipboard register '+'.
//
//X CLIPBOARD ('+')
//
//Text selection stuff that uses the clipboard register '+'.
//Under X, this matches the standard cut/paste buffer CLIPBOARD selection.
//It will be used for unnamed cut/pasting is 'clipboard' contains "unnamed",
//otherwise you will need to do <"+p>. "
//If not under X, it is synonymous with the selection register '*'.

private void
freeSelection(ClipBoard* cbd) {
   YankReg* yReg = get_y_current();

   if (cbd == &clipboard)
      set_y_current(getYRegister(PLUS_REGISTER));
   else
      set_y_current(getYRegister(STAR_REGISTER));
   free_yank_all();
   get_y_current()->y_size = 0;
   set_y_current(yReg);
}

//Get the selected text and put it in register '*' or '+'.
private void
clip_get_selection(ClipBoard* cbd) {
   Multistring mu = {};
   appendToMulti(tConst("wl-paste"), OUT &mu);
   PolyWithStatus fromShell = fiCallShell(&mu, 0);
   _bp(true);
   if (cbd->owned) {
      if ((cbd == &clipboard && getYRegister(PLUS_REGISTER)->y_array != NULL)
            || (cbd == &clipboard && getYRegister(STAR_REGISTER)->y_array != NULL)
      )
         return;

      // Avoid triggering autocmds such as TextYankPost.
      block_autocmds();

      // Get the text between clipboard.start & clipboard.end
      YankReg* old_y_previous = get_y_previous();
      YankReg* old_y_current = get_y_current();
      Pos old_cursor = curPor->cursor;
      ColNr old_curswant = curPor->cursWant;
      int old_set_curswant = curPor->setCursWant;
      Pos old_op_start = curBook->opStart;
      Pos old_op_end = curBook->opEnd;
      Pos old_visual = VIsual;
      int old_visual_mode = VIsual_mode;
      
      Operator oa;
      clear_oparg(&oa);
      oa.regname = (cbd == &clipboard ? '+' : '*');
      oa.opTy = OP_YANK;
      
      ActionArg ca;
      CLEAR_FIELD(ca);
      ca.oper = &oa;
      ca.cmdchar = 'y';
      ca.count1 = 1;
      ca.retval = CA_NO_ADJ_OP_END;
      jugExecuteVisualOperator(&ca, 0, true);

      // restore things
      set_y_previous(old_y_previous);
      set_y_current(old_y_current);
      curPor->cursor = old_cursor;
      changed_cline_bef_curs();   // need to update virtCol et al
      curPor->cursWant = old_curswant;
      curPor->setCursWant = old_set_curswant;
      curBook->opStart = old_op_start;
      curBook->opEnd = old_op_end;
      VIsual = old_visual;
      VIsual_mode = old_visual_mode;

      unblock_autocmds();
   } ei (!is_clipboard_needs_update()) {
      freeSelection(cbd);

      // Try to get selected text from another window
      clip_gen_request_selection(cbd);
   }
}

//Convert the '*'/'+' register into a selection string returned in *str with length *len.
//Return the motion type, or -1 for failure.
private int
clip_convert_selection(OUT Byte** str, OUT Ulong *len, ClipBoard* cbd) {
   YankReg* yReg = (cbd == &clipboard) ? getYRegister(PLUS_REGISTER) : getYRegister(STAR_REGISTER);
   *str = NULL;
   *len = 0;
   if (yReg->y_array == NULL)
      return -1;

   for (int i = 0; i < yReg->y_size; i++)
      *len += (Ulong)yReg->y_array[i].len + 1; // 1 for the end of line char

   // Don't want newline character at end of last line if we're in MCHAR mode.
   if (yReg->y_type == MCHAR && *len >= 1)
      (*len)--;

   *str = alloc(*len + 1);   // add one to avoid zero
   Byte* p = *str;
   int lnum = 0;
   int j = 0;
   for (int i = 0; i < (int)*len; i++, j++) {
      if (yReg->y_array[lnum].c[j] == '\n')
         p[i] = ZERO;
      ei (yReg->y_array[lnum].c[j] == ZERO) {
         p[i] = '\n';
         lnum++;
         j = -1;
      } else
         p[i] = yReg->y_array[lnum].c[j];
   }
   return yReg->y_type;
}

//When "regname" is a clipboard register, obtain the selection.  If it's not
//available return zero, otherwise return "regname".
int
may_get_selection(int regname) {
   if (regname == '*') {
      clip_get_selection(&clipboard);
   } ei (regname == '+') {
      clip_get_selection(&clipboard);
   }
   return regname;
}

// If we have written to a clipboard register, send the text to the clipboard.
private void
may_set_selection(void){
   if ((get_y_current() == getYRegister(STAR_REGISTER))) {
      clip_own_selection(&clipboard);
      clip_gen_set_selection(&clipboard);
   } ei ((get_y_current() == getYRegister(PLUS_REGISTER))) {
      clip_own_selection(&clipboard);
      clip_gen_set_selection(&clipboard);
   }
}

//Adjust the register name pointed to with "rp" for the clipboard being used always.
void
clipGetDefaultRegister(OUT int* rp){
   if (*rp == 0) {
      *rp = '+';
   }
}

//Read data from a file descriptor and write it to the given clipboard.
private void
clip_wl_receive_data(ClipBoard* cbd, CS mime_type, int fd) {
   Byte   *start, *final;
   ArrayList   buf;
   int      motion_type = MAUTO;
   Long   r = 0;
   fd_set rfds;
   TimeVal  tv;

   FD_ZERO(&rfds);
   FD_SET(fd, &rfds);

   // Make pipe (read end) non-blocking
   if (fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK) == -1)
      return;

   ga_init2(&buf, 1, 4096);

   // 4096 bytes seems reasonable for initial buffer size
   if (ga_grow(&buf, 4096) == FAIL)
      return;

   start = buf.c;

   // Only poll before reading when we first start, then we do non-blocking
   // reads and check for EAGAIN or EINTR to signal to poll again.
   goto poll_data;

   while (errno = 0, true) {
      r = read(fd, start, buf.cap - 1 - buf.len);

      if (r == 0)
          break;
      ei (r < 0) {
          if (errno == EAGAIN || errno == EINTR) {
   poll_data:
         tv.tv_sec = 0;
         tv.tv_usec = p_wtm * 1000;
         if (select(fd + 1, &rfds, NULL, NULL, &tv) > 0)
            continue;
         }
         break;
      }

      start += r;
      buf.len += r;

      // Realloc if we are at the end of the buffer
      if (buf.len >= buf.cap - 1) {
         if (ga_grow(&buf, 8192) == FAIL)
            break;
         start = buf.c + buf.len;
      }
   }

   if (buf.len == 0) {
      freeSelection(cbd); // Nothing received, clear register
      ga_clear(&buf);
      return;
   }

   final = buf.c;

   if (STRCMP(mime_type, EE_ATOM_NAME) == 0 && buf.len >= 2) {
      motion_type = *final++;;
      buf.len--;
   }

   clip_yank_selection(motion_type, final, (long)buf.len, cbd);
   ga_clear(&buf);
}

//Get the current selection and fill the respective register for cbd with the data.
private void
clip_wl_request_selection(ClipBoard* cbd) {
   WaylandSelection selection;
   ArrayList* mime_types;
   CS chosen_mime = NULL;

   if (cbd == &clipboard)
      selection = WAYLAND_SELECTION_PRIMARY;
   else
      return;

   // Get mime types that the source client offers
   mime_types = wayland_cb_get_mime_types(selection);

   if (mime_types == NULL || mime_types->len == 0) {
      // Selection is empty/cleared
      freeSelection(cbd);
      return;
   }

   int len = ARRAY_LENGTH(supported_mimes);

   // Loop through and pick the one we want to receive from
   for (int i = 0; i < len && !chosen_mime; i++) {
      for (int k = 0; k < mime_types->len && !chosen_mime; k++) {
         char *mime_type = ((char**)mime_types->c)[k];

         if (STRCMP(mime_type, supported_mimes[i]) == 0)
            chosen_mime = supported_mimes[i];
      }
   }
   if (!chosen_mime)
      return;

   int fd = wayland_cb_receive_data(chosen_mime, selection);

   if (fd == -1)
      return;

   // Start reading the file descriptor returned
   clip_wl_receive_data(cbd, chosen_mime, fd);

   close(fd);
}

//Write data from either the clip or plus register, depending on the given
//selection, to the file descriptor that the receiving client will read from.
private void
clip_wl_send_data(const char* mime_type, int fd, WaylandSelection selection) {
   ClipBoard       *cbd;
   Ulong length;
   CS string;
   Long written = 0;
   Unt total = 0;
   int did_motion_type = true;
   int motion_type;
   int skip_len_check = false;
   fd_set wfds;
   TimeVal  tv;

   FD_ZERO(&wfds);
   FD_SET(fd, &wfds);
   tv.tv_sec = 0;
   tv.tv_usec = p_wtm * 1000;
   if (selection == WAYLAND_SELECTION_REGULAR)
      cbd = &clipboard;
   ei (selection == WAYLAND_SELECTION_PRIMARY)
      cbd = &clipboard;
   else
      return;

   // Shouldn't happen unless there is a bug.
   if (!cbd->owned)
      return;

   // Get the current selection
   clip_get_selection(cbd);
   motion_type = clip_convert_selection(OUT &string, OUT &length, cbd);

   if (motion_type < 0)
      goto exit;

   if (STRCMP(mime_type, EE_ATOM_NAME) == 0)
      did_motion_type = false;

   while ((total < (Unt)length || skip_len_check) 
         && select(fd + 1, NULL, &wfds, NULL, &tv) > 0
   ) {
      // First byte sent is motion type for Eegl-specific formats
      if (!did_motion_type) {
         if (total == 1) {
            total = 0;
            did_motion_type = true;
            continue;
         }
         // We cast to char so that we only send one byte
         written = write( fd, (Byte*)&motion_type, 1);
         skip_len_check = true;
      } else {
         // write the actual selection to the fd
         written = write(fd, string + total, length - total);
         if (skip_len_check)
             skip_len_check = false;
      }

      if (written == -1)
         break;
      total += written;

      tv.tv_sec = 0;
      tv.tv_usec = p_wtm * 1000;
   }
exit:
   eeglFree(string);
}

//Called if another client gains ownership of the given selection. If so then
//lose the selection internally.
private void
clip_wl_selection_cancelled(WaylandSelection selection) {
   if (selection == WAYLAND_SELECTION_REGULAR)
      clip_lose_selection(&clipboard);
   ei (selection == WAYLAND_SELECTION_PRIMARY)
      clip_lose_selection(&clipboard);
}

//Own the selection that cbd corresponds to. Start listening for requests from
//other Wayland clients so they can receive data from us. Return OK on success and FAIL on failure.
private int
clip_wl_own_selection(ClipBoard* cbd) {
   WaylandSelection selection;

   if (cbd == &clipboard)
      selection = WAYLAND_SELECTION_PRIMARY;
   else
      return FAIL;

   return wayland_cb_own_selection(
      clip_wl_send_data,
      clip_wl_selection_cancelled,
      supported_mimes,
      sizeof(supported_mimes)/sizeof(*supported_mimes),
      selection
   );
}

//Disown the selection that cbd corresponds to. Note that the the cancelled
//event is not sent when the data source is destroyed.
private void
clip_wl_lose_selection(ClipBoard *cbd) {
   if (cbd == &clipboard)
      wayland_cb_lose_selection(WAYLAND_SELECTION_REGULAR);

   // wayland_cb_lose_selection(selection);
}

//Send the current selection to the clipboard. Do nothing for Wayland because
//we will fill in the selection only when requested by another client.
private void
clip_wl_set_selection(ClipBoard *cbd UNUSED) {
}

//}}}
//{{{window

// Only really used for debugging/testing purposes in order to force focus
// stealing even when a data control protocol is available.
private int force_fs  = false;

//Like wl_display_flush but always writes all the data in the buffer to the
//display fd. Returns FAIL on failure and OK on success.
private int
vwl_display_flush(WaylandDisplay *display) {
   int ret;

   fd_set       wfds;
   TimeVal  tv;

   FD_ZERO(&wfds);
   FD_SET(display->fd, &wfds);

   tv.tv_sec   = p_wtm / 1000;
   tv.tv_usec   = (p_wtm % 1000) * 1000;

   if (display->proxy == NULL)
      return FAIL;

   //Send the requests we have made to the compositor, until we have written
   //all the data. Poll in order to check if the display fd is writable; if
   //not, then wait until it is and continue writing or until we timeout.
   while (errno = 0, (ret = wl_display_flush(display->proxy)) == -1 && errno == EAGAIN) {
         if (select(display->fd + 1, NULL, &wfds, NULL, &tv) <= 0)
            return FAIL;
      tv.tv_sec   = 0;
      tv.tv_usec   = p_wtm * 1000;
   }
   //Return FAIL on error or timeout
   if ((errno != 0 && errno != EAGAIN) || ret == -1)
      return FAIL;

   return OK;
}

//Called when compositor is done processing requests/events.
private void
vwl_callback_done(void *data, struct wl_callback *callback, Unt cb_data UNUSED) {
   *((int*)data) = true;
   wl_callback_destroy(callback);
}

//Like wl_display_roundtrip but polls the display fd with a timeout. Return OK/FAIL
private int
vwl_display_roundtrip(WaylandDisplay *display) {
   struct wl_callback   *callback;
   int         ret, done = false;
   TimeVal start, now;

   if (display->proxy == NULL)
      return FAIL;

   //Tell compositor to emit 'done' event after processing all requests we
   //have sent and handling events.
   callback = wl_display_sync(display->proxy);

   if (callback == NULL)
      return FAIL;

   wl_callback_add_listener(callback, &vwl_callback_listener, &done);

   gettimeofday(&start, NULL);

   //Wait till we get the done event (which will set `done` to true), unless we timeout
   while (true) {
      ret = vwl_display_dispatch(display);

      if (done || ret == -1)
         break;

      gettimeofday(&now, NULL);

      if ((now.tv_sec * 1000000 + now.tv_usec) -
         (start.tv_sec * 1000000 + start.tv_usec) >= p_wtm * 1000)
      {
          ret = -1;
          break;
      }
   }

   if (ret == -1) {
      if (!done)
         wl_callback_destroy(callback);
      return FAIL;
   }

   return OK;
}

//Like wl_display_roundtrip but poll the display fd with a timeout. Return
//number of events dispatched on success else -1 on failure.
private int
vwl_display_dispatch(WaylandDisplay *display) {
   fd_set          rfds;
   TimeVal  tv;

   FD_ZERO(&rfds);
   FD_SET(display->fd, &rfds);

   tv.tv_sec = p_wtm / 1000;
   tv.tv_usec = (p_wtm % 1000) * 1000;

   if (display->proxy == NULL)
      return -1;

   while (wl_display_prepare_read(display->proxy) == -1) {
      // Dispatch any queued events so that we can start reading
      if (wl_display_dispatch_pending(display->proxy) == -1)
         return -1;
   } 

   // Send any requests before we starting blocking to read display fd
   if (vwl_display_flush(display) == FAIL) {
      wl_display_cancel_read(display->proxy);
      return -1;
   }

   // Poll until there is data to read from the display fd.
   if (select(display->fd + 1, &rfds, NULL, NULL, &tv) <= 0) {
      wl_display_cancel_read(display->proxy);
      return -1;
   }

   // Read events into the queue
   if (wl_display_read_events(display->proxy) == -1)
      return -1;

   // Dispatch those events (call the handlers associated for each event)
   return wl_display_dispatch_pending(display->proxy);
}

// Same as vwl_display_dispatch but poll/select is never called. This is useful
// is poll/select was already called before or if you just want to dispatch any
// events that happen to be waiting to be dispatched on the display fd.
private int
vwl_display_dispatch_any(WaylandDisplay* display) {
   if (display->proxy == NULL)
      return -1;

   while (wl_display_prepare_read(display->proxy) == -1) {
      // Dispatch any queued events so that we can start reading
      if (wl_display_dispatch_pending(display->proxy) == -1)
          return -1;
   }

   // Send any requests before we starting blocking to read display fd
   if (vwl_display_flush(display) == FAIL) {
      wl_display_cancel_read(display->proxy);
      return -1;
   }

   // Read events into the queue
   if (wl_display_read_events(display->proxy) == -1)
      return -1;

   // Dispatch those events (call the handlers associated for each event)
   return wl_display_dispatch_pending(display->proxy);
}

// Redirect libwayland logging to use ch_log + emsg instead. It must be "char const*"
private void
vwl_log_handler(char const* fmt, va_list args) {
   // 512 bytes should be big enough
   CS buf = alloc(512);
   CS prefix = _("wayland protocol error -> ");
   Unt len = STRLEN(prefix);
   copySubstrToAllocation(buf, (Text){prefix, len});
   VSNPRINTF(buf + len, 4096 - len, fmt, args);

   // Remove newline that libwayland puts
   buf[STRLEN(buf) - 1] = ZERO;

   lo("%s", buf);
   emsg(buf);

   eeglFree(buf);
}

//Connect to the display with name; passing NULL will use libwayland's way of
//getting the display. Additionally get the registry object but will not
//starting listening. Returns OK on sucess and FAIL on failure.
private int
vwl_connect_display(CS display) {
   if (wayland_no_connect)
      return FAIL;

   // We will get an error if XDG_RUNTIME_DIR is not set.
   if (mch_getenv("XDG_RUNTIME_DIR") == NULL)
      return FAIL;

   // Must set log handler before we connect display in order to work.
   wl_log_set_handler_client(vwl_log_handler);

   vwl_display.proxy = wl_display_connect((char*)display);

   if (vwl_display.proxy == NULL)
      return FAIL;

   wayland_set_display(display);
   vwl_display.fd = wl_display_get_fd(vwl_display.proxy);

   vwl_display.registry.proxy = wl_display_get_registry(vwl_display.proxy);

   if (vwl_display.registry.proxy == NULL) {
      vwl_disconnect_display();
      return FAIL;
   }

   return OK;
}

#define destroy_gobject(object) \
    if (vwl_gobjects.object != NULL) \
    { \
   object##_destroy(vwl_gobjects.object); \
   vwl_gobjects.object = NULL; \
    }

// Disconnect the display and frees up all resources, including all global objects.
private void
vwl_disconnect_display(void) {
   destroy_gobject(ext_data_control_manager_v1)
   destroy_gobject(wl_data_device_manager)
   destroy_gobject(wl_shm)
   destroy_gobject(wl_compositor)
   destroy_gobject(xdg_wm_base)
   destroy_gobject(zwp_primary_selection_device_manager_v1)

   for (int i = 0; i < vwl_seats.len; i++)
      vwl_destroy_seat(&((WaylandSeat *)vwl_seats.c)[i]);
   ga_clear(&vwl_seats);
   vwl_seats.len = 0;

   if (vwl_display.registry.proxy != NULL) {
      wl_registry_destroy(vwl_display.registry.proxy);
      vwl_display.registry.proxy = NULL;
   }
   if (vwl_display.proxy != NULL) {
      wl_display_disconnect(vwl_display.proxy);
      vwl_display.proxy = NULL;
   }
}

// Tell the compositor we are still responsive.
private void
vwl_xdg_wm_base_listener_ping(
   void* data UNUSED,
   struct xdg_wm_base *base,
   Unt serial
) {
    xdg_wm_base_pong(base, serial);
}

// Start listening to the registry and get initial set of global objects/interfaces.
private int
vwl_listen_to_registry(void) {
   // Only meant for debugging/testing purposes
   CS env = mch_getenv("EEGL_WAYLAND_FORCE_FS");

   if (env != NULL && STRCMP(env, "1") == 0)
      force_fs = true;
   else
      force_fs = false;

   ga_init2(&vwl_seats, sizeof(WaylandSeat), 1);

   wl_registry_add_listener( vwl_display.registry.proxy, &vwl_registry_listener, NULL);

   if (vwl_display_roundtrip(&vwl_display) == FAIL)
      return FAIL;

   // If we have a suitable data control protocol discard the rest. If we only
   // have wlr data control protocol but its version is 1, then don't discard
   // globals if we also have the primary selection protocol.
   if (!force_fs && vwl_gobjects.ext_data_control_manager_v1) {
      destroy_gobject(wl_data_device_manager)
      destroy_gobject(wl_shm)
      destroy_gobject(wl_compositor)
      destroy_gobject(xdg_wm_base)
   } else {
      // Be ready for ping events
      xdg_wm_base_add_listener( vwl_gobjects.xdg_wm_base, &vwl_xdg_wm_base_listener, NULL);
   } 
   return OK;
}

#define SET_GOBJECT(object, min_ver) \
    do { \
   chosen_interface = &object##_interface; \
   object_member = (void*)&vwl_gobjects.object; \
   min_version = min_ver; \
    } while (0)

//Callback for global event, for each global interface the compositor supports.
//Keep in sync with vwl_disconnect_display().
private void
vwl_registry_listener_global(
   void* data UNUSED,
   struct wl_registry  *registry UNUSED,
   Unt name,
   const char* interface,
   Unt version
) {
   const struct wl_interface   *chosen_interface = NULL;
   void* proxy;
   Unt min_version;
   void** object_member;

   if (STRCMP(interface, wl_seat_interface.name) == 0) {
      chosen_interface = &wl_seat_interface;
      min_version = 2;
   } ei (STRCMP(interface, ext_data_control_manager_v1_interface.name) == 0)
      SET_GOBJECT(ext_data_control_manager_v1, 1);

   ei (STRCMP(interface, wl_data_device_manager_interface.name) == 0)
      SET_GOBJECT(wl_data_device_manager, 1);
   ei (STRCMP(interface, wl_shm_interface.name) == 0)
      SET_GOBJECT(wl_shm, 1);
   ei (STRCMP(interface, wl_compositor_interface.name) == 0)
      SET_GOBJECT(wl_compositor, 2);
   ei (STRCMP(interface, xdg_wm_base_interface.name) == 0)
      SET_GOBJECT(xdg_wm_base, 1);
   ei (STRCMP(interface, zwp_primary_selection_device_manager_v1_interface.name) == 0)
      SET_GOBJECT(zwp_primary_selection_device_manager_v1, 1);

   if (chosen_interface == NULL || version < min_version)
      return;

   proxy = wl_registry_bind(vwl_display.registry.proxy, name, chosen_interface, version);

   if (chosen_interface == &wl_seat_interface)
      // Add seat to vwl_seats array, as we can have multiple seats.
      vwl_add_seat(proxy);
   else
      // Hold proxy & name in the vwl_gobject struct
      *object_member = proxy;
}

// Called when a global object is removed, if so, then do nothing. This is to
// avoid a global being removed while it is in the process of being used. Let
// the user call :wlrestore in order to reset everything. Requests to that
// global will just be ignored on the compositor side.
private void
vwl_registry_listener_global_remove(
      void* data UNUSED, struct wl_registry* registry UNUSED, Unt name UNUSED
) {
}

// Add a new seat given its proxy to the global grow array
private void
vwl_add_seat(struct wl_seat *seat_proxy) {
   WaylandSeat *seat;

   if (ga_grow(&vwl_seats, 1) == FAIL)
      return;

   seat = &((WaylandSeat *)vwl_seats.c)[vwl_seats.len];

   seat->proxy = seat_proxy;

   // Get label and capabilities
   wl_seat_add_listener(seat_proxy, &vwl_seat_listener, seat);

   if (vwl_display_roundtrip(&vwl_display) == FAIL)
      return;

   // Check if label has been allocated
   if (seat->label == NULL)
      return;

   vwl_seats.len++;
}

// Callback for seat text label/name
private void
vwl_seat_listener_name(void* data, struct wl_seat* seat_proxy UNUSED, const char* name) {
   WaylandSeat *seat = data;
   seat->label = (char *)copyStr((CS)name);
}

// Callback for seat capabilities
private void
vwl_seat_listener_capabilities(
   void      *data,
   struct wl_seat   *seat_proxy UNUSED,
   Unt   capabilities
) {
   WaylandSeat *seat = data;
   seat->capabilities = capabilities;
}

// Destroy/free seat.
private void
vwl_destroy_seat(WaylandSeat *seat) {
   if (seat->proxy) {
      if (wl_seat_get_version(seat->proxy) >= 5)
         // Helpful for the compositor
         wl_seat_release(seat->proxy);
      else
         wl_seat_destroy(seat->proxy);
      seat->proxy = NULL;
   }
   eeglFree(seat->label);
   seat->label = NULL;
}

// Return a seat with the give name/label. If none exists then NULL is returned.
// If NULL or an empty string is passed as the label then the first available
// seat found is used.
private WaylandSeat *
vwl_get_seat(CS label) {
   if (!label && vwl_seats.len > 0)
      return &((WaylandSeat *)vwl_seats.c)[0];

   for (int i = 0; i < vwl_seats.len; i++) {
      WaylandSeat *seat = &((WaylandSeat *)vwl_seats.c)[i];
      if (STRCMP(seat->label, label) == 0)
         return seat;
   }
   return NULL;
}

// Get keyboard object from seat and return it. NULL is returned on
// failure such as when a keyboard is not available for seat.
private struct wl_keyboard *
vwl_seat_get_keyboard(WaylandSeat *seat) {
   if (!(seat->capabilities & WL_SEAT_CAPABILITY_KEYBOARD))
      return NULL;

   return wl_seat_get_keyboard(seat->proxy);
}

// Connect to the Wayland display with given name and binds to global objects
// as needed. If display is NULL then the $WAYLAND_DISPLAY environment variable
// will be used (handled by libwayland). Returns FAIL on failure and OK on success
int
wayland_init_client(CS display) {
   wayland_set_display(display);

   if (vwl_connect_display(display) == FAIL || vwl_listen_to_registry() == FAIL)
      goto fail;

   wayland_display_fd = vwl_display.fd;

   return OK;
fail:
   // Set v:wayland_display to empty string (but not wayland_display_name)
   wayland_set_display(S"");
   return FAIL;
}

// Disconnect Wayland client and free up all resources used.
void
wayland_uninit_client(void) {
    wayland_cb_uninit();
    vwl_disconnect_display();

    wayland_set_display(S"");
}

// true if Wayland display connection is valid and ready.
int
wayland_client_is_connected(int quiet) {
   if (vwl_display.proxy == NULL)
      goto error;

   // Display errors are always fatal
   if (wl_display_get_error(vwl_display.proxy) != 0 || vwl_display_flush(&vwl_display) == FAIL)
      goto error;

   return true;
error:
   if (!quiet)
      emsg(e_wayland_connection_unavailable);
   return false;
}

// Flush requests and process new Wayland events, does not poll the display file descriptor.
int
wayland_client_update(void) {
   return vwl_display_dispatch_any(&vwl_display) == -1 ? FAIL : OK;
}


// If globals required for focus stealing method is available.
private int
vwl_focus_stealing_available(void) {
   return (p_wst || force_fs) &&
      vwl_gobjects.wl_compositor != NULL &&
      vwl_gobjects.wl_shm != NULL &&
      vwl_gobjects.xdg_wm_base != NULL;
}

// Configure xdg_surface
private void
vwl_xdg_surface_listener_configure(
   void          *data UNUSED,
   struct xdg_surface  *surface,
   Unt       serial)
{
   xdg_surface_ack_configure(surface, serial);
}

// Called when compositor isn't using the buffer anymore, we can reuse it again.
private void
vwl_bs_buffer_listener_release(void* data, struct wl_buffer* buffer UNUSED) {
   BufferStore* store = data;
   store->available = true;
}

// Destroy a buffer store structure.
private void
vwl_destroy_buffer_store(BufferStore* store) {
   if (store->buffer != NULL)
      wl_buffer_destroy(store->buffer);
   if (store->pool != NULL)
      wl_shm_pool_destroy(store->pool);

   close(store->fd);

   eeglFree(store);
}

// Create an anonymous/temporary file/object and return its file descriptor. Return -1 on error.
private int
mch_create_anon_file(void) {
   int fd = -1;
   const char template[] = "/eeglXXXXXX";

   for (int i = 0; i < 100; i++) {
      mch_get_random((Byte*)template + 4, 6);
      errno = 0;
      fd = shm_open(template, O_CREAT | O_RDWR | O_EXCL, 0600);

      if (fd >= 0 || errno != EEXIST)
         break;
   }
   // Remove object name from namespace
   shm_unlink(template);
    // Last resort
   if (fd == -1) {
      Byte   *tempname;
      // get a name for the temp file
      if ((tempname = eeTempName('w', false)) == NULL) {
         emsg(_(e_cant_get_temp_file_name));
         return -1;
      }
      fd = open((char *)tempname, O_CREAT | O_RDWR | O_EXCL, 0600);
      mch_remove(tempname);
      eeglFree(tempname);
   }
   return fd;
}


// Initialize a buffer and its backing memory pool.
private BufferStore *
vwl_init_buffer_store(int width, int height) {
   if (vwl_gobjects.wl_shm == NULL)
      return NULL;

   BufferStore* store = alloc(sizeof(BufferStore));

   store->available = false;

   store->width = width;
   store->height = height;
   store->stride = store->width * 4;
   store->size = store->stride * store->height;

   int fd = mch_create_anon_file();
   int r = ftruncate(fd, store->size);

   if (r == -1) {
      if (fd >= 0)
         close(fd);
      return NULL;
   }

   store->pool = wl_shm_create_pool(vwl_gobjects.wl_shm, fd, store->size);
   store->buffer = wl_shm_pool_create_buffer(
       store->pool,
       0,
       store->width,
       store->height,
       store->stride,
       WL_SHM_FORMAT_ARGB8888);

   store->fd = fd;

   wl_buffer_add_listener(store->buffer, &vwl_cb_buffer_listener, store);

   if (vwl_display_roundtrip(&vwl_display) == -1) {
      vwl_destroy_buffer_store(store);
      return NULL;
   }

   store->available = true;

   return store;
}

// Destroy a focus stealing store structure.
private void
vwl_destroy_fs_surface(vwl_fs_surface_T *store) {
   if (store->shell.toplevel != NULL)
      xdg_toplevel_destroy(store->shell.toplevel);
   if (store->shell.surface != NULL)
      xdg_surface_destroy(store->shell.surface);
   if (store->surface != NULL)
      wl_surface_destroy(store->surface);
   if (store->keyboard != NULL) {
      if (wl_keyboard_get_version(store->keyboard) >= 3)
         wl_keyboard_release(store->keyboard);
      else
         wl_keyboard_destroy(store->keyboard);
   }
    eeglFree(store);
}

// Create an invisible surface in order to gain focus and call on_focus() with
// serial that was given.
private int
vwl_init_fs_surface(
   WaylandSeat       *seat,
   BufferStore* buffer_store,
   void (*on_focus)(void *, Unt),
   void *user_data
) {
   vwl_fs_surface_T *store;

   if (vwl_gobjects.wl_compositor == NULL || vwl_gobjects.xdg_wm_base == NULL)
      return FAIL;
   if (buffer_store == NULL || seat == NULL)
      return FAIL;

   store = allocZeroed(sizeof(*store));

   // Get keyboard
   store->keyboard = vwl_seat_get_keyboard(seat);

   if (store->keyboard == NULL)
      goto fail;

   wl_keyboard_add_listener(store->keyboard, &vwl_fs_keyboard_listener, store);

   if (vwl_display_dispatch(&vwl_display) == -1)
      goto fail;

   store->surface = wl_compositor_create_surface(vwl_gobjects.wl_compositor);
   store->shell.surface = xdg_wm_base_get_xdg_surface( vwl_gobjects.xdg_wm_base, store->surface);
   store->shell.toplevel = xdg_surface_get_toplevel(store->shell.surface);

   xdg_toplevel_set_title(store->shell.toplevel, "Eegl clipboard");

   xdg_surface_add_listener(store->shell.surface, &vwl_xdg_surface_listener, NULL);

   wl_surface_commit(store->surface);

   store->on_focus = on_focus;
   store->user_data = user_data;
   store->got_focus = false;

   if (vwl_display_roundtrip(&vwl_display) == FAIL)
      goto fail;

   // We may get the enter event early, if we do then we will set `got_focus` to true.
   if (store->got_focus)
      goto early_exit;

   // Book hasn't been released yet, abort. This shouldn't happen but still check for it.
   if (!buffer_store->available)
      goto fail;

   buffer_store->available = false;

   wl_surface_attach(store->surface, buffer_store->buffer, 0, 0);
   wl_surface_damage(store->surface, 0, 0, buffer_store->width, buffer_store->height);
   wl_surface_commit(store->surface);

   {
   // Dispatch events until we receive the enter event. Add a max delay of
   // 'p_wtm' when waiting for it (may be longer depending on how long we
   // poll when dispatching events)
   TimeVal start, now;

   gettimeofday(&start, NULL);

   while (vwl_display_dispatch(&vwl_display) != -1) {
      if (store->got_focus)
         break;

      gettimeofday(&now, NULL);

      if ((now.tv_sec * 1000000 + now.tv_usec) - (start.tv_sec * 1000000 + start.tv_usec)
             >= p_wtm * 1000)
         goto fail;
   }
   }
early_exit:
   vwl_destroy_fs_surface(store);
   vwl_display_flush(&vwl_display);

   return OK;
fail:
   vwl_destroy_fs_surface(store);
   vwl_display_flush(&vwl_display);

   return FAIL;
}

// Called when the keyboard focus is on our surface
private void
vwl_fs_keyboard_listener_enter(
   void      *data,
   struct wl_keyboard   *keyboard UNUSED,
   Unt      serial,
   struct wl_surface   *surface UNUSED,
   struct wl_array   *keys UNUSED
) {
   vwl_fs_surface_T *store = data;

   store->got_focus = true;

   if (store->on_focus != NULL)
      store->on_focus(store->user_data, serial);
}

// Dummy functions to handle keyboard events we don't care about.

private void
vwl_fs_keyboard_listener_keymap(
   void* data UNUSED,
   struct wl_keyboard   *keyboard UNUSED,
   Unt      format UNUSED,
   int         fd,
   Unt      size UNUSED
) {
   close(fd);
}

private void
vwl_fs_keyboard_listener_leave(
   void      *data UNUSED,
   struct wl_keyboard   *keyboard UNUSED,
   Unt      serial UNUSED,
   struct wl_surface   *surface UNUSED)
{
}

private void
vwl_fs_keyboard_listener_key(
   void      *data UNUSED,
   struct wl_keyboard   *keyboard UNUSED,
   Unt      serial UNUSED,
   Unt      time UNUSED,
   Unt      key UNUSED,
   Unt      state UNUSED)
{
}

private void
vwl_fs_keyboard_listener_modifiers(
    void      *data UNUSED,
    struct wl_keyboard   *keyboard UNUSED,
    Unt      serial UNUSED,
    Unt      mods_depressed UNUSED,
    Unt      mods_latched UNUSED,
    Unt      mods_locked UNUSED,
    Unt      group UNUSED)
{
}

private void
vwl_fs_keyboard_listener_repeat_info(
   void      *data UNUSED,
   struct wl_keyboard   *keyboard UNUSED,
   int32_t      rate UNUSED,
   int32_t      delay UNUSED)
{
}

#define VWL_CODE_DATA_OBJECT_DESTROY(type) \
do { \
   if (type == NULL || type->proxy == NULL) \
      return; \
    switch (type->protocol) \
    { \
   case VWL_DATA_PROTOCOL_EXT:  \
       ext_data_control_##type##_v1_destroy(type->proxy); \
       break; \
   case VWL_DATA_PROTOCOL_CORE: \
       wl_data_##type##_destroy(type->proxy); \
       break; \
   case VWL_DATA_PROTOCOL_PRIMARY: \
       zwp_primary_selection_##type##_v1_destroy(type->proxy); \
       break; \
   default: \
       break; \
    } \
    if (alloced) \
   eeglFree(type); \
    else \
   type->proxy = NULL; \
} while (0)

private void
vwl_data_device_destroy(DataDevice *device, int alloced) {
   VWL_CODE_DATA_OBJECT_DESTROY(device);
}

private void
vwl_data_offer_destroy(DataOffer *offer, int alloced) {
   VWL_CODE_DATA_OBJECT_DESTROY(offer);
}

private void
vwl_data_source_destroy(DataSource *source, int alloced) {
   VWL_CODE_DATA_OBJECT_DESTROY(source);
}


// Used to pass a DataOffer struct from the data_offer event to the offer
// event and to the selection event.
private DataOffer *tmp_vwl_offer;

// These functions handle the more complicated data_offer and selection events.

private void
vwl_gen_data_device_listener_data_offer(void *data, void *offer_proxy) {
   DataDevice *device = data;
   tmp_vwl_offer = alloc(sizeof(*tmp_vwl_offer));
   tmp_vwl_offer->proxy = offer_proxy;
   tmp_vwl_offer->protocol = device->protocol;

   vwl_data_device_listener.data_offer(device, tmp_vwl_offer);
}

private void
vwl_gen_data_device_listener_selection(
   void          *data,
   void          *offer_proxy,
   WaylandSelection selection,
   DataProtocol protocol)
{
   if (tmp_vwl_offer == NULL) {
   // Memory allocation failed or selection cleared (data_offer is never
   // sent when selection is cleared/empty).
   DataOffer tmp = {
       .proxy = offer_proxy,
       .protocol = protocol
   };

   vwl_data_offer_destroy(&tmp, false);

   // If offer proxy is NULL then we know the selection has been cleared.
   if (offer_proxy == NULL)
       vwl_data_device_listener.selection(data, NULL, selection);
   } else {
      vwl_data_device_listener.selection(data, tmp_vwl_offer, selection);
      tmp_vwl_offer = NULL;
   }
}

// Boilerplate macros. Each just calls its respective generic callback.
#define VWL_FUNC_DATA_DEVICE_DATA_OFFER(device_name, offer_name) \
private void device_name##_listener_data_offer( \
       void *data, struct device_name *device_proxy UNUSED, \
       struct offer_name *offer_proxy) \
{ \
    vwl_gen_data_device_listener_data_offer(data, offer_proxy); \
}
#define VWL_FUNC_DATA_DEVICE_SELECTION( \
   device_name, offer_name, type, selection_type, protocol) \
   private void device_name##_listener_##type( \
      void *data, struct device_name *device_proxy UNUSED, \
      struct offer_name *offer_proxy UNUSED) \
{ \
    vwl_gen_data_device_listener_selection( \
       data, offer_proxy, selection_type, protocol); \
}
#define VWL_FUNC_DATA_DEVICE_FINISHED(device_name) \
private void device_name##_listener_finished( \
       void *data, struct device_name *device_proxy UNUSED) \
{ \
    vwl_data_device_listener.finished(data); \
}
#define VWL_FUNC_DATA_SOURCE_SEND(source_name) \
private void source_name##_listener_send(void *data, \
       struct source_name *source_proxy UNUSED, \
       char const* mime_type, int fd) \
{ \
    dataSourceListener.send(data, mime_type, fd); \
}
#define VWL_FUNC_DATA_SOURCE_CANCELLED(source_name) \
private void source_name##_listener_cancelled(void *data, \
       struct source_name *source_proxy UNUSED) \
{ \
    dataSourceListener.cancelled(data); \
}
#define VWL_FUNC_DATA_OFFER_OFFER(offer_name) \
private void offer_name##_listener_offer(void *data, \
       struct offer_name *offer_proxy UNUSED, \
       const char *mime_type) \
{ \
    dataOfferListener.offer(data, mime_type); \
}

VWL_FUNC_DATA_DEVICE_DATA_OFFER(
   ext_data_control_device_v1, ext_data_control_offer_v1)
VWL_FUNC_DATA_DEVICE_DATA_OFFER(wl_data_device, wl_data_offer)
VWL_FUNC_DATA_DEVICE_DATA_OFFER(
   zwp_primary_selection_device_v1, zwp_primary_selection_offer_v1)

VWL_FUNC_DATA_DEVICE_SELECTION(
   ext_data_control_device_v1, ext_data_control_offer_v1,
   selection, WAYLAND_SELECTION_REGULAR, VWL_DATA_PROTOCOL_EXT)
VWL_FUNC_DATA_DEVICE_SELECTION(
   wl_data_device, wl_data_offer, selection,
   WAYLAND_SELECTION_REGULAR, VWL_DATA_PROTOCOL_CORE)

VWL_FUNC_DATA_DEVICE_SELECTION(
   ext_data_control_device_v1, ext_data_control_offer_v1,
   primary_selection, WAYLAND_SELECTION_PRIMARY, VWL_DATA_PROTOCOL_EXT)
VWL_FUNC_DATA_DEVICE_SELECTION(
   zwp_primary_selection_device_v1, zwp_primary_selection_offer_v1,
   primary_selection, WAYLAND_SELECTION_PRIMARY, VWL_DATA_PROTOCOL_PRIMARY)

VWL_FUNC_DATA_DEVICE_FINISHED(ext_data_control_device_v1)

VWL_FUNC_DATA_SOURCE_SEND(ext_data_control_source_v1)
VWL_FUNC_DATA_SOURCE_SEND(wl_data_source)
VWL_FUNC_DATA_SOURCE_SEND(zwp_primary_selection_source_v1)

VWL_FUNC_DATA_SOURCE_CANCELLED(ext_data_control_source_v1)
VWL_FUNC_DATA_SOURCE_CANCELLED(wl_data_source)
VWL_FUNC_DATA_SOURCE_CANCELLED(zwp_primary_selection_source_v1)

VWL_FUNC_DATA_OFFER_OFFER(ext_data_control_offer_v1)
VWL_FUNC_DATA_OFFER_OFFER(wl_data_offer)
VWL_FUNC_DATA_OFFER_OFFER(zwp_primary_selection_offer_v1)

// Listener handlers. Used via VWL_CODE_DATA_OBJECT_ADD_LISTENER macro

// DATA DEVICES
private struct ext_data_control_device_v1_listener
ext_data_control_device_v1_listenerObj = {
    .data_offer = ext_data_control_device_v1_listener_data_offer,
    .selection  = ext_data_control_device_v1_listener_selection,
    .primary_selection = ext_data_control_device_v1_listener_primary_selection,
    .finished   = ext_data_control_device_v1_listener_finished
};

private struct wl_data_device_listener wl_data_device_listenerObj = {
    .data_offer = wl_data_device_listener_data_offer,
    .selection  = wl_data_device_listener_selection,
};

private struct zwp_primary_selection_device_v1_listener
zwp_primary_selection_device_v1_listenerObj = {
    .selection  = zwp_primary_selection_device_v1_listener_primary_selection,
    .data_offer = zwp_primary_selection_device_v1_listener_data_offer
};

// DATA SOURCES
private struct ext_data_control_source_v1_listener
ext_data_control_source_v1_listenerObj = {
    .send      = ext_data_control_source_v1_listener_send,
    .cancelled = ext_data_control_source_v1_listener_cancelled
};

private struct wl_data_source_listener 
wl_data_source_listenerObj = {
    .send      = wl_data_source_listener_send,
    .cancelled = wl_data_source_listener_cancelled
};

private struct zwp_primary_selection_source_v1_listener
zwp_primary_selection_source_v1_listenerObj = {
    .send      = zwp_primary_selection_source_v1_listener_send,
    .cancelled = zwp_primary_selection_source_v1_listener_cancelled,
};

// OFFERS
private struct ext_data_control_offer_v1_listener
ext_data_control_offer_v1_listenerObj = {
    .offer = ext_data_control_offer_v1_listener_offer
};

private struct wl_data_offer_listener wl_data_offer_listenerObj = {
    .offer = wl_data_offer_listener_offer
};

private struct zwp_primary_selection_offer_v1_listener
zwp_primary_selection_offer_v1_listenerObj = {
    .offer = zwp_primary_selection_offer_v1_listener_offer
};

// `type` is also used as the user data
#define VWL_CODE_DATA_OBJECT_ADD_LISTENER(type) \
do { \
   if (type->proxy == NULL) \
      return; \
   type->data = data; \
   switch (type->protocol){ \
   case VWL_DATA_PROTOCOL_EXT: \
       ext_data_control_##type##_v1_add_listener(type->proxy, \
          &ext_data_control_##type##_v1_listenerObj, type); \
       break; \
   case VWL_DATA_PROTOCOL_CORE: \
       wl_data_##type##_add_listener(type->proxy, \
          &wl_data_##type##_listenerObj, type); \
       break; \
   case VWL_DATA_PROTOCOL_PRIMARY: \
       zwp_primary_selection_##type##_v1_add_listener(type->proxy, \
          &zwp_primary_selection_##type##_v1_listenerObj, type); \
       break; \
   default: \
       break; \
    } \
} while (0)

private void
vwl_data_device_add_listener(DataDevice* device, void* data) {
   VWL_CODE_DATA_OBJECT_ADD_LISTENER(device);
}

private void
vwl_data_source_add_listener(DataSource *source, void *data) {
   VWL_CODE_DATA_OBJECT_ADD_LISTENER(source);
}

private void
vwl_data_offer_add_listener(DataOffer *offer, void *data) {
   VWL_CODE_DATA_OBJECT_ADD_LISTENER(offer);
}

//Sets the selection using the given data device with the given selection. If the device does
//not support the selection then nothing happens. For data control protocols the serial argument is 
//ignored.
private void
vwl_data_device_set_selection(
   DataDevice* device,
   DataSource* source,
   Unt serial,
   WaylandSelection selection
) {
   if (selection == WAYLAND_SELECTION_REGULAR) {
      switch (device->protocol) {
      case VWL_DATA_PROTOCOL_EXT:
         ext_data_control_device_v1_set_selection( device->proxy, source->proxy);
         break;
      case VWL_DATA_PROTOCOL_CORE:
         wl_data_device_set_selection( device->proxy, source->proxy, serial);
         break;
      default:
         break;
      }
   } ei (selection == WAYLAND_SELECTION_PRIMARY) {
      switch (device->protocol) {
      case VWL_DATA_PROTOCOL_EXT:
         ext_data_control_device_v1_set_primary_selection( device->proxy, source->proxy);
         break;
      case VWL_DATA_PROTOCOL_PRIMARY:
         zwp_primary_selection_device_v1_set_selection( device->proxy, source->proxy, serial);
         break;
      default:
         break;
      }
   }
}

// Start receiving data from offer object, which sends the given fd to the
// source client to write into.
private void
vwl_data_offer_receive(DataOffer *offer, const char *mime_type, int fd) {
   switch (offer->protocol) {
   case VWL_DATA_PROTOCOL_EXT:
      ext_data_control_offer_v1_receive(offer->proxy, mime_type, fd);
      break;
   case VWL_DATA_PROTOCOL_CORE:
      wl_data_offer_receive(offer->proxy, mime_type, fd);
      break;
   case VWL_DATA_PROTOCOL_PRIMARY:
      zwp_primary_selection_offer_v1_receive(offer->proxy, mime_type, fd);
      break;
   default:
      break;
   }
}

#define SET_MANAGER(manager_name, protocol_enum, focus) \
   do { \
   manager->proxy = vwl_gobjects.manager_name; \
   manager->protocol = protocol_enum; \
   return focus; \
   } while (0)

// Get a data device manager that supports the given selection. If none if found
// then the manager protocol is set to VWL_DATA_PROTOCOL_NONE. true is returned
// if the given data device manager requires focus to work else false.
private int
vwl_get_data_device_manager(
   vwl_data_device_manager_T   *manager,
   WaylandSelection       selection)
{
   // Prioritize data control protocols first then try using the focus steal
   // method with the core protocol data objects.
   if (force_fs)
      goto focus_steal;

   // Ext data control protocol supports both selections, try it first
   if (vwl_gobjects.ext_data_control_manager_v1 != NULL)
      SET_MANAGER(ext_data_control_manager_v1, VWL_DATA_PROTOCOL_EXT, false);

focus_steal:
   if (vwl_focus_stealing_available()) {
      if (vwl_gobjects.wl_data_device_manager != NULL
         && selection == WAYLAND_SELECTION_REGULAR)
          SET_MANAGER(wl_data_device_manager, VWL_DATA_PROTOCOL_CORE, true);

      ei (vwl_gobjects.zwp_primary_selection_device_manager_v1 != NULL
         && selection == WAYLAND_SELECTION_PRIMARY)
          SET_MANAGER(zwp_primary_selection_device_manager_v1,
             VWL_DATA_PROTOCOL_PRIMARY, true);
   }

    manager->protocol = VWL_DATA_PROTOCOL_NONE;

    return false;
}

// Get a data device that manages the given seat's selection.
private void
vwl_get_data_device(
   vwl_data_device_manager_T   *manager,
   WaylandSeat          *seat,
   DataDevice       *device)
{
   switch (manager->protocol) {
   case VWL_DATA_PROTOCOL_EXT:
      device->proxy = ext_data_control_manager_v1_get_data_device(manager->proxy, seat->proxy);
      break;
   case VWL_DATA_PROTOCOL_CORE:
       device->proxy = wl_data_device_manager_get_data_device(
          manager->proxy, seat->proxy);
       break;
   case VWL_DATA_PROTOCOL_PRIMARY:
       device->proxy = zwp_primary_selection_device_manager_v1_get_device(
          manager->proxy, seat->proxy);
       break;
   default:
       device->protocol = VWL_DATA_PROTOCOL_NONE;
       return;
    }
    device->protocol = manager->protocol;
}

private void
vwl_create_data_source( vwl_data_device_manager_T   *manager, DataSource* source) {
   switch (manager->protocol) {
   case VWL_DATA_PROTOCOL_EXT:
      source->proxy = ext_data_control_manager_v1_create_data_source(manager->proxy);
      break;
   case VWL_DATA_PROTOCOL_CORE:
      source->proxy = wl_data_device_manager_create_data_source(manager->proxy);
      break;
   case VWL_DATA_PROTOCOL_PRIMARY:
      source->proxy = zwp_primary_selection_device_manager_v1_create_source( manager->proxy);
      break;
   default:
      source->protocol = VWL_DATA_PROTOCOL_NONE;
      return;
   }
   source->protocol = manager->protocol;
}

// Offer a new mime type to be advertised by us to other clients.
private void
vwl_data_source_offer(DataSource *source, const char *mime_type) {
   switch (source->protocol) {
   case VWL_DATA_PROTOCOL_EXT:
      ext_data_control_source_v1_offer(source->proxy, mime_type);
      break;
   case VWL_DATA_PROTOCOL_CORE:
      wl_data_source_offer(source->proxy, mime_type);
      break;
   case VWL_DATA_PROTOCOL_PRIMARY:
      zwp_primary_selection_source_v1_offer(source->proxy, mime_type);
      break;
   default:
      break;
   }
}

// Free the mime types arraylists in the given clip_sel struct.
private void
vwl_clipboard_free_mime_types(WaylandClipboardSelection *clip_sel) {
   // Don't want to be double freeing
   if (clip_sel->mime_types.c == clip_sel->tmp_mime_types.c) {
      ga_clear_strings(&clip_sel->mime_types);
      ga_init(&vwl_clipboard.primary.tmp_mime_types);
   } else {
      ga_clear_strings(&clip_sel->mime_types);
      ga_clear_strings(&clip_sel->tmp_mime_types);
   }
}

// Setup required objects to interact with Wayland selections/clipboard on given
// seat. Returns OK on success and FAIL on failure.
int
wayland_cb_init(CS seat) {
   vwl_clipboard.seat = vwl_get_seat(seat);

   if (vwl_clipboard.seat == NULL)
      return FAIL;

   // Get data device managers for each selection. If there wasn't any manager
   // that could be found that supports the given selection, then it will be unavailable.
   vwl_clipboard.regular.requires_focus = vwl_get_data_device_manager(
       &vwl_clipboard.regular.manager,
       WAYLAND_SELECTION_REGULAR);
   vwl_clipboard.primary.requires_focus = vwl_get_data_device_manager(
       &vwl_clipboard.primary.manager,
       WAYLAND_SELECTION_PRIMARY);

   // Initialize shm pool and buffer if core data protocol is available
   if (vwl_focus_stealing_available() &&
          (vwl_clipboard.regular.requires_focus || vwl_clipboard.primary.requires_focus)
   )
      vwl_clipboard.fs_buffer = vwl_init_buffer_store(1, 1);

   // Get data devices for each selection. If one of the above function calls
   // results in an unavailable manager, then the device coming from it will
   // have its protocol set to VWL_DATA_PROTOCOL_NONE.
   vwl_get_data_device( &vwl_clipboard.regular.manager,
       vwl_clipboard.seat,
       &vwl_clipboard.regular.device);
   vwl_get_data_device(
       &vwl_clipboard.primary.manager,
       vwl_clipboard.seat,
       &vwl_clipboard.primary.device);

   // Initialize grow arrays for the offer mime types.
   // I find most applications to have below 10 mime types that they offer.
   ga_init2(&vwl_clipboard.regular.tmp_mime_types, sizeof(char*), 10);
   ga_init2(&vwl_clipboard.primary.tmp_mime_types, sizeof(char*), 10);

   // We dont need to use ga_init2 because tmp_mime_types will be copied over
   // to mime_types anyways.
   ga_init(&vwl_clipboard.regular.mime_types);
   ga_init(&vwl_clipboard.primary.mime_types);

   // Start listening for data offers/new selections. Don't do anything when we
   // get a new data offer other than saving the mime types and saving the data
   // offer. Then when we want the data we use the saved data offer to receive
   // data from it along with the saved mime_types. For each new selection just
   // destroy the previous offer/free mime_types, if any.
   vwl_data_device_add_listener(&vwl_clipboard.regular.device, &vwl_clipboard.regular);
   vwl_data_device_add_listener(&vwl_clipboard.primary.device, &vwl_clipboard.primary);

   if (vwl_display_roundtrip(&vwl_display) == FAIL) {
      wayland_cb_uninit();
      return FAIL;
   }
   clip_init();

   return OK;
}

// Free up resources used for Wayland selections. Does not destroy global
// objects such as data device managers.
void
wayland_cb_uninit(void) {
   if (vwl_clipboard.fs_buffer != NULL) {
      vwl_destroy_buffer_store(vwl_clipboard.fs_buffer);
      vwl_clipboard.fs_buffer = NULL;
   }

   // Destroy the current offer if it exists
   vwl_data_offer_destroy(vwl_clipboard.regular.offer, true);
   vwl_data_offer_destroy(vwl_clipboard.primary.offer, true);

   // Destroy any devices or sources
   vwl_data_device_destroy(&vwl_clipboard.regular.device, false);
   vwl_data_device_destroy(&vwl_clipboard.primary.device, false);
   vwl_data_source_destroy(&vwl_clipboard.regular.source, false);
   vwl_data_source_destroy(&vwl_clipboard.primary.source, false);

   // Free mime types
   vwl_clipboard_free_mime_types(&vwl_clipboard.regular);
   vwl_clipboard_free_mime_types(&vwl_clipboard.primary);

   vwl_display_flush(&vwl_display);

   memset(&vwl_clipboard, 0, sizeof(vwl_clipboard));
   vwl_clipboard.regular.selection = WAYLAND_SELECTION_REGULAR;
   vwl_clipboard.primary.selection = WAYLAND_SELECTION_PRIMARY;
}

// If the given selection can be used.
private int
vwl_clipboard_selection_is_ready(WaylandClipboardSelection *clip_sel) {
   return clip_sel->manager.protocol != VWL_DATA_PROTOCOL_NONE 
      && clip_sel->device.protocol != VWL_DATA_PROTOCOL_NONE;
}

// Callback for data offer event. Start listening to the given offer immediately
// in order to get mime types.
private void
vwl_data_device_listener_data_offer(
   DataDevice   *device,
   DataOffer    *offer)
{
   WaylandClipboardSelection *clip_sel = device->data;

   // Get mime types and save them so we can use them when we want to paste the
   // selection.
   if (clip_sel->source.proxy != NULL)
      // We own the selection, no point in getting mime types
      return;

   vwl_data_offer_add_listener(offer, device->data);
}

// Callback for offer event. Save each mime type given to be used later.
private void
vwl_data_offer_listener_offer(DataOffer* offer, char const* mime_type) {
    WaylandClipboardSelection* clip_sel = offer->data;

    // Save string into temporary grow array, which will be finalized into the
    // actual grow array if the selection matches with the selection that the device manages.
    ga_copy_string(&clip_sel->tmp_mime_types, (Byte*)mime_type);
}

// Callback for selection event, for either the regular or primary selection.
// Don't try receiving data from the offer, instead destroy the previous offer
// if any and set the current offer to the given offer, along with the
// respective mime types.
private void
vwl_data_device_listener_selection(
   DataDevice* device,
   DataOffer    *offer,
   WaylandSelection selection
) {
   WaylandClipboardSelection   *clip_sel = device->data;
   DataOffer* prev_offer = clip_sel->offer;

   // Save offer if it selection and clip_sel match, else discard it
   if (clip_sel->selection == selection)
      clip_sel->offer = offer;
   else {
      // Example: selection event is for the primary selection but this device
      // is only for the regular selection, if so then just discard the offer and tmp_mime_types.
      vwl_data_offer_destroy(offer, true);
      tmp_vwl_offer = NULL;
      ga_clear_strings(&clip_sel->tmp_mime_types);
      return;
   }

   // There are two cases when clip_sel->offer is NULL
   // 1. No one owns the selection
   // 2. We own the selection (we'll just access the register directly)
   if (offer == NULL) {
      // Selection cleared/empty
      ga_clear_strings(&clip_sel->tmp_mime_types);
      clip_sel->offer = NULL;
      goto exit;
   } ei (clip_sel->source.proxy != NULL) {
      // We own the selection, ignore it
      vwl_data_offer_destroy(offer, true);
      ga_clear_strings(&clip_sel->tmp_mime_types);
      clip_sel->offer = NULL;
      goto exit;
   }

exit:
   // Destroy previous offer if any
   vwl_data_offer_destroy(prev_offer, true);
   ga_clear_strings(&clip_sel->mime_types);

   // Copy the grow array over
   clip_sel->mime_types = clip_sel->tmp_mime_types;

   // Clear tmp_mime_types so next data_offer doesn't try to resize/grow it
   // (Don't free it though using ga_clear() because mime_types->c is the same pointer)r
   if (clip_sel->offer != NULL)
      ga_init(&clip_sel->tmp_mime_types);
}

// Callback for finished event. Destroy device and all related objects/resources
// such as offers and mime types.
private void
vwl_data_device_listener_finished(DataDevice *device) {
   WaylandClipboardSelection *clip_sel = device->data;

   vwl_data_device_destroy(&clip_sel->device, false);
   vwl_data_offer_destroy(clip_sel->offer, true);
   vwl_data_source_destroy(&clip_sel->source, false);
   vwl_clipboard_free_mime_types(clip_sel);
}

//Return a pointer to a grow array of mime types that the current offer
//supports sending. If the returned garray has NULL for c or a len of
//0, then the selection is cleared. If focus stealing is required, a surface
//will be created to steal focus first.
ArrayList *
wayland_cb_get_mime_types(WaylandSelection selection) {
   WaylandClipboardSelection *clip_sel;

   if (selection == WAYLAND_SELECTION_REGULAR)
      clip_sel = &vwl_clipboard.regular;
   ei (selection == WAYLAND_SELECTION_PRIMARY)
      clip_sel = &vwl_clipboard.primary;
   else
      return NULL;

   if (clip_sel->requires_focus) {
      // We don't care about the on_focus callback since once we gain focus
      // the data offer events will come immediately.
      if (vwl_init_fs_surface(vwl_clipboard.seat,
             vwl_clipboard.fs_buffer, NULL, NULL) == FAIL)
          return NULL;
   } ei (vwl_display_roundtrip(&vwl_display) == FAIL)
      return NULL;

   return &clip_sel->mime_types;
}

// Receive data from the given selection, and return the fd to read data from.
// On failure -1 is returned.
int
wayland_cb_receive_data(CS mime_type, WaylandSelection selection) {
   WaylandClipboardSelection *clip_sel;

   // Create pipe that source client will write to
   int fds[2];

   if (selection == WAYLAND_SELECTION_REGULAR)
      clip_sel = &vwl_clipboard.regular;
   ei (selection == WAYLAND_SELECTION_PRIMARY)
      clip_sel = &vwl_clipboard.primary;
   else
      return -1;

   if (!wayland_client_is_connected(false) || !vwl_clipboard_selection_is_ready(clip_sel))
      return -1;

   if (clip_sel->offer == NULL || clip_sel->offer->proxy == NULL)
      return -1;

   if (pipe(fds) == -1)
      return -1;

   vwl_data_offer_receive(clip_sel->offer, (char const*)mime_type, fds[1]);

   close(fds[1]); // Close before we read data so that when the source client
         // closes their end we receive an EOF.

   if (vwl_display_flush(&vwl_display) == OK)
      return fds[0];

   close(fds[0]);

   return -1;
}

//Callback for send event. Just call the user callback which will handle it and do the writing stuff
private void
vwl_data_source_listener_send(DataSource* source, char const* mime_type, int32_t fd) {
   WaylandClipboardSelection *clip_sel = source->data;

   if (clip_sel->send_cb != NULL)
      clip_sel->send_cb(mime_type, fd, clip_sel->selection);
   close(fd);
}

// Callback for cancelled event, just call the user callback.
private void
vwl_data_source_listener_cancelled(DataSource *source) {
   WaylandClipboardSelection *clip_sel = source->data;

   if (clip_sel->send_cb != NULL)
      clip_sel->cancelled_cb(clip_sel->selection);
   vwl_data_source_destroy(source, false);
}

// Set the selection when we gain focus
private void
vwl_on_focus_set_selection(void* data, Unt serial) {
    WaylandClipboardSelection* clip_sel = data;

    vwl_data_device_set_selection(
       &clip_sel->device,
       &clip_sel->source,
       serial,
       clip_sel->selection);
    vwl_display_roundtrip(&vwl_display);
}

// Become the given selection's owner, and advertise to other clients the mime
// types found in mime_types array. Returns FAIL on failure and OK on success.
int
wayland_cb_own_selection(
   wayland_cb_send_data_func_T send_cb,
   wayland_cb_selection_cancelled_func_T cancelled_cb,
   Arr(CS) mime_types,
   int len,
   WaylandSelection selection
) {
   WaylandClipboardSelection* clip_sel;

   if (selection == WAYLAND_SELECTION_REGULAR)
      clip_sel = &vwl_clipboard.regular;
   ei (selection == WAYLAND_SELECTION_PRIMARY)
      clip_sel = &vwl_clipboard.primary;
   else
      return FAIL;

   if (clip_sel->source.proxy != NULL) {
      if (selection == WAYLAND_SELECTION_PRIMARY)
          // We already own the selection, ignore (only do this for primary selection). We don't 
          // reset the selection because then we would be setting the selection every time the 
          // user moves the visual selection cursor, which is messy and inefficient.
          //
          // Eegl doesn't have a mechanism to only set the selection when the user stops selecting 
          // (such as the user releasing the mouse button in graphical Wayland applications). So 
          // this behavior in Eegl differs from other Wayland applications.
          return OK;
      ei (selection == WAYLAND_SELECTION_REGULAR) {
          // Technically we don't need to do this as we already own the selection, however if a 
          // user yanks text a second time, the text yanked won't appear in their clipboard 
          // manager if they are using one.
          //
          // This can be unexpected behaviour for the user so its probably better to do it this 
          // way. Additionally other Wayland applications seem to set the selection every time.
          //
          // There should be no noticable performance change since its not like this is running 
          // in the background constantly in Eegl, only runs once when the user yanks text to the 
          // system clipboard.
          vwl_data_source_destroy(&clip_sel->source, false);
          vwl_display_flush(&vwl_display);
      } else
          // Shouldn't happen
          return FAIL;
   }

   if (!wayland_client_is_connected(false) || !vwl_clipboard_selection_is_ready(clip_sel))
      return FAIL;

   clip_sel->send_cb = send_cb;
   clip_sel->cancelled_cb = cancelled_cb;

   vwl_create_data_source(&clip_sel->manager, &clip_sel->source);

   vwl_data_source_add_listener(&clip_sel->source, clip_sel);

   // Advertise mime types
   for (int i = 0; i < len; i++)
      vwl_data_source_offer(&clip_sel->source, (char const*)mime_types[i]);

   if (clip_sel->requires_focus) {
      // Call set_selection later when we gain focus
      if (vwl_init_fs_surface(vwl_clipboard.seat, vwl_clipboard.fs_buffer,
             vwl_on_focus_set_selection, clip_sel) == FAIL)
          goto fail;
   } else {
      vwl_data_device_set_selection(&clip_sel->device,
         &clip_sel->source, 0, selection);
      if (vwl_display_roundtrip(&vwl_display) == FAIL)
          goto fail;
   }

   return OK;
fail:
   vwl_data_source_destroy(&clip_sel->source, false);
   return FAIL;
}

// Disown the given selection, so that we are not the source client that other
// clients receive data from.
void
wayland_cb_lose_selection(WaylandSelection selection) {
   if (selection == WAYLAND_SELECTION_REGULAR)
      vwl_data_source_destroy(&vwl_clipboard.regular.source, false);
   ei (selection == WAYLAND_SELECTION_PRIMARY)
      vwl_data_source_destroy(&vwl_clipboard.primary.source, false);
   vwl_display_flush(&vwl_display);
}

// Return true if the selection is owned by either us or another client.
int
wayland_cb_selection_is_owned(WaylandSelection selection) {
   vwl_display_roundtrip(&vwl_display);

   if (selection == WAYLAND_SELECTION_REGULAR)
      return vwl_clipboard.regular.source.proxy != NULL
          || vwl_clipboard.regular.offer != NULL;
   ei (selection == WAYLAND_SELECTION_PRIMARY)
      return vwl_clipboard.primary.source.proxy != NULL
          || vwl_clipboard.primary.offer != NULL;
   else
      return false;
}

// Return true if the Wayland clipboard/selections are ready to use.
int
wayland_cb_is_ready(void) {
   vwl_display_roundtrip(&vwl_display);

   // Clipboard is ready if we have at least one selection available
   return wayland_client_is_connected(true) &&
       (vwl_clipboard_selection_is_ready(&vwl_clipboard.regular) ||
       vwl_clipboard_selection_is_ready(&vwl_clipboard.primary));
}

// Reload Wayland clipboard, useful if changing seat.
int
wayland_cb_reload(void) {
   // Lose any selections we own
   if (clipboard.owned)
       clip_lose_selection(&clipboard);

   wayland_cb_uninit();

   if (wayland_cb_init(p_wse) == FAIL)
      return FAIL;

   return OK;
}


private int wayland_ct_restore_count = 0;

//Attempt to restore the Wayland display connection. Returns OK if display
//connection was/is now valid, else FAIL if the display connection is invalid.
int
wayland_may_restore_connection(void) {
   // No point if we still are already connected properly
   if (wayland_client_is_connected(true))
      return OK;

   // No point in restoring the connection if we are exiting or dying.
   if (isExitingG || v_dying || wayland_ct_restore_count <= 0) {
      wayland_set_display(S"");
      return FAIL;
   }

   --wayland_ct_restore_count;
   wayland_uninit_client();

   return wayland_init_client(wayland_display_name);
}

// Disconnect then reconnect Wayland connection
void
c_wlrestore(Invocation *invo) {
   CS display = (!invo->arg || STRLEN(invo->arg) == 0) ? wayland_display_name : invo-> arg;

   // Return early if shebang is not passed, we are still connected, and if not
   // changing to a new Wayland display.
   if (!invo->forceit && wayland_client_is_connected(true) &&
       (display == wayland_display_name 
           || (wayland_display_name && STRCMP(wayland_display_name, display) == 0))
   )
      return;

   // Lose any selections we own
   if (clipboard.owned)
      clip_lose_selection(&clipboard);

   if (display)
      display = copyStr((Byte*)display);

   wayland_uninit_client();

   // Reset amount of available tries to reconnect the display to 5
   wayland_ct_restore_count = 5;

   if (wayland_init_client(display) == OK) {
      smsg(_("restoring Wayland display %s"), wayland_display_name);

      wayland_cb_init(p_wse);
   } else
      msg(_("failed restoring, lost connection to Wayland display"));

   eeglFree(display);
}

// Set wayland_display_name to display. Note that this allocate a copy of the
// string, unless NULL is passed. If NULL is passed then v:wayland_display is
// set to $WAYLAND_DISPLAY, but wayland_display_name is set to NULL.
private void
wayland_set_display(CS display) {
   if (!display)
      display = mch_getenv((Byte*)"WAYLAND_DISPLAY");
   ei (display == wayland_display_name)
      // Don't want to be freeing vwl_display_strname then trying to copy it after.
      goto exit;

   if (!display)
      // $WAYLAND_DISPLAY is not set
      display = S"";

   // Leave unchanged if display is empty (but not NULL)
   if (STRCMP(display, "") != 0) {
      eeglFree(wayland_display_name);
      wayland_display_name = copyStr((Byte*)display);
   }

exit:
   set_EeglVar_string(VV_WAYLAND_DISPLAY, (Byte*)display, -1);
}

//}}}
//{{{new clipboard

private int
copy0() {
    const char *text_to_copy = "Hello from my C program!";

    // Open a pipe to wl-copy
    FILE *fp = popen("wl-copy", "w");
    if (fp == NULL) {
        perror("Failed to run wl-copy");
        return 1;
    }

    // Write the text into the pipe
    fputs(text_to_copy, fp);

    // Close the pipe and check status
    int status = pclose(fp);
    if (status != 0) {
        fprintf(stderr, "wl-copy exited with error\n");
    } else {
        printf("Successfully copied text to Wayland clipboard.\n");
    }

    return 0;
}

private int
paste0() {
   char buffer[128];

   // Open a pipe to read from wl-paste
   FILE *fp = popen("wl-paste", "r");
   if (fp == NULL) {
       perror("Failed to run wl-paste");
       return 1;
   }

   // Read the output from the command a chunk at a time
   printf("Clipboard contents:\n");
   while (fgets(buffer, sizeof(buffer), fp) != NULL) {
       printf("%s", buffer);
   }
   printf("\n");

   // Close the pipe
   int status = pclose(fp);
   if (status != 0) {
       fprintf(stderr, "wl-paste exited with error\n");
   }

   return 0;
}

//}}}
