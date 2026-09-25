int highest_patch(void);
void c_version(Invocation* invo);
void maybe_intro_message(void);
void c_intro(Invocation*);
int libMain(void);
void init0(void);
void init1(OUT MainParams* par);
int appMain(int argc, char** argv);
int is_not_a_term(void);
int is_not_a_term_or_gui(void);
void free_vbuf(void);
void may_trigger_safestate(Boole safe);
void state_no_longer_safe(CS reason);
Boole get_was_safe_state(void);
void may_trigger_safestateagain(void);
int work_pending(void);
void mainLoop(Boole inCommPort);
void exitEegl(int exitval);
void mainerr_arg_missing(CS str);
CS mainProgramVersion();
void __attribute__((noinline)) __bp();
void mch_exit(int r);
CS get_users(Expand*, int idx);
int match_user(CS name);
void free_homedir(void);
void free_users(void);
void c_mkrc(Invocation* invo);
int put_eol(FILE *fd);
int put_line(FILE *fd, CS s);
int get_eeglinfo_parameter(int type);
void check_marks_read(void);
int read_eeglinfo(
   CS file,       // file name or NULL to use default name
   Unt flags       // EIF_WANT_INFO et al.
);
void write_eeglinfo(CS file, Boole forceit);
