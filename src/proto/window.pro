/* src/window.c */
int wayland_init_client(CS display);
void wayland_uninit_client(void);
int wayland_client_is_connected(int quiet);
int wayland_client_update(void);
int wayland_cb_init(const char *seat);
void wayland_cb_uninit(void);
ArrayList *wayland_cb_get_mime_types(WaylandSelection selection);
int wayland_cb_receive_data(const char *mime_type, WaylandSelection selection);
int wayland_cb_own_selection(wayland_cb_send_data_func_T send_cb, wayland_cb_selection_cancelled_func_T cancelled_cb, const char **mime_types, int len, WaylandSelection selection);
void wayland_cb_lose_selection(WaylandSelection selection);
int wayland_cb_selection_is_owned(WaylandSelection selection);
int wayland_cb_is_ready(void);
int wayland_cb_reload(void);
int wayland_may_restore_connection(void);
void c_wlrestore(Invocation *invo);
/* eegl: set ft=c : */
