/* src/window.c */
int serverRegisterName(Display *dpy, Byte *name);
int serverSendToEegl(Display *dpy, Byte *name, Byte *cmd, Byte **result, Window *server, Boole asExpr, int timeout, Boole localLoop, int silent);
CS serverGetEeglNames(Display *dpy);
Window serverStrToWin(CS str);
int serverSendReply(Byte *name, Byte *str);
int serverReadReply(Display *dpy, Window win, Byte **str, int localLoop, int timeout);
int serverPeekReply(Display *dpy, Window win, Byte **str);
void serverEventProc(Display *dpy, XEvent *eventPtr, int immediate);
void server_parse_messages(void);
int server_waiting(void);
void may_restore_x11_clipboard(void);
void c_xrestore(Invocation *invo);
Boole isXtermShellDefined(void);
void setup_term_clip(void);
void start_xterm_trace(int button);
void stop_xterm_trace(void);
void clear_xterm_clip(void);
void clip_update(void);
void xterm_update(void);
int clip_xterm_own_selection(ClipBoard *cbd);
void clip_xterm_lose_selection(ClipBoard *cbd);
void clip_xterm_request_selection(ClipBoard *cbd);
void clip_xterm_set_selection(ClipBoard *cbd);
/* eegl: set ft=c : */
