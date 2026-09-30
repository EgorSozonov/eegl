//EEGL - the Extensible development Environment for GNU/Linux
//Licensed under GPLv3, see the LICENSE file (c) Egor Sozonov

//## motor.c: the appliation runner of Eegl

#include "base.h"
#include "eegl.h"
#include "h/data.types.h"
#include "h/data.h"
#include "h/book.h"
#include "h/diff.h"
#include "h/do.h"
#include "h/draw.types.h"
#include "h/draw.h"
#include "h/eval.h"
#include "h/fileio.h"
#include "h/hilite.types.h"
#include "h/hilite.h"
#include "h/input.types.h"
#include "h/input.h"
#include "h/location.types.h"
#include "h/location.h"
#include "h/message.h"
#include "h/option.h"
#include "h/portal.h"
#include "h/script.h"
#include "h/strings.h"
#include "h/tag.h"
#include "h/term.h"
#include "h/ui.h"
#include "h/wheel.types.h"
#include "h/wheel.h"
#include "h/window.h"

#include <errno.h> //for errno
#include <ctype.h> //for isalpha()
#include <poll.h> 
#include <sys/file.h> //for open
#include <sys/stat.h> //for stat, fstat, S_ISDIR
#include <sys/socket.h> //for socket()
#include <sys/un.h> //for sockaddr_un()
#include <sys/wait.h> //for waitpid()
#include <pwd.h> //for setpwent()
#include <time.h> //for timespec_get()
#include <libintl.h> //for gettext()
#include <inttypes.h> //for PRIu32
#include <string.h> //for strstr()
#include <stddef.h> //for offsetof
#include <sys/ioctl.h> //for ioctl
#include <sys/utsname.h> //for vutsname

//{{{types

//Values for edit_type.
#define EDIT_NONE   0       //no edit type yet
#define EDIT_FILE   1       //file name argument[s] given, use argument list
#define EDIT_STDIN  2       //read file from stdin
#define EDIT_TAG    3       //tag name argument given, use tagname
#define EDIT_QF     4       //start in quickfix mode

//Maximum number of commands from + or -c arguments.
pub
#define MAX_ARG_CMDS 10

//Struct for various parameters passed between main() and other functions.
pub
typedef struct {
   int argc;
   Arr(Arr(char)) argv;

   CS fname;         //first file to edit

   CS altInitFile;      //alternative init file name from -u argument
   int clean;         //--clean argument

   int n_commands;                 //no. of commands from + or -c
   CS commands[MAX_ARG_CMDS];      //commands from + or -c arg.
   Byte cmds_tofree[MAX_ARG_CMDS]; //commands that need free()
   int n_pre_commands;             //no. of commands from --cmd
   CS pre_commands[MAX_ARG_CMDS];  //commands from --cmd argument

   int edit_type; //type of editing to do
   CS tagname;    //tag from -t argument
   CS use_ef;     //@errorfile from -q argument

   int want_full_screen;
   int not_a_term;      //no warning for missing term?
   int tty_fail;      //exit if not a tty
   CS term;         //specified terminal name
   int no_swap_file;      //"-n" argument used
   int use_debug_break_level;
   Unt portalCount;      //number of portals to use
   int portalLayout;     //0, WIN_HOR, WIN_VER or WIN_TABS

   int serverArg;      //TRUE when argument for a server
   CS serverName_arg;  //cmdline arg for server name
   CS serverStr;       //remote server command
   CS servername;      //allocated name for our server
   int diff_mode;      //start with 'diff' set
} MainParams;

//Variable flavor
typedef enum {
   VAR_FLAVOR_DEFAULT,   //doesn't start with uppercase
   VAR_FLAVOR_SESSION,   //starts with uppercase, some lower
   VAR_FLAVOR_EEGLINFO      //all uppercase
} VarFlavor;

//Structure used for reading from the eeglinfo file.
typedef struct {
   CS line;   //text of the current line
   FILE* vir_fd;   //file descriptor
   int vir_version;   //eeglinfo version detected or -1
   ArrayList vir_barlines;   //lines starting with |
} Vir;


#define TIME_MSG(s) do { if (time_fd != NULL) time_msg((CS)s, NULL); } while (0)

typedef sigset_t SignalSet;

#define EXEC_FAILED 122 //Exit code when shell didn't execute. Don't use
                         //127, some shells use that already
#define OPEN_NULL_FAILED 123 //Exit code if /dev/null can't be opened

#define SIGSET_DECL(set) SignalSet set;
#define BLOCK_SIGNALS(set) block_signals(set)
#define UNBLOCK_SIGNALS(set) unblock_signals(set)

private int dontCheckJobEndedP = 0;

typedef int waitstatus;

#define SOCK_ERRNO
#define sock_write(sd, buf, len) write(sd, buf, len)
#define sock_read(sd, buf, len) read(sd, buf, len)
#define sock_close(sd) close(sd)
#define fd_read(fd, buf, len) read(fd, buf, len)
#define fd_write(sd, buf, len) write(sd, buf, len)
#define fd_close(sd) close(sd)

//Structure to hold info about an async shell Job
struct Job {
   Unt refCount; //reference count
   Job* next;
   Job* prev;
   ProId pid;
   JobStatus status;
   Arr(Byte) ttyIn;    //controlling tty input, allocated
   Arr(Byte) ttyOut;   //controlling tty output, allocated
   Arr(Byte) jv_stoponexit;//allocated
   Arr(Byte) jv_termsig;   //allocated
   int exitVal;
   void (*nativeCb)(void); //native C function to call when the job finishes
   Callback exitCb;

   Book* inBook;   //book from "in-name"

   int copyId;

   Channel* channel; //channel for I/O, reference-counted
   Arr(CS) argv;   //command line used to start the job
};


#define FOR_ALL_CHANNELS(ch) \
    for ((ch) = firstChannelP; (ch) != NULL; (ch) = (ch)->next)
    
#define FOR_ALL_JOBS(job) \
    for ((job) = firstJobS; (job) != NULL; (job) = (job)->next)
    
//The per-fd info for a channel.
pub
typedef struct {
   int fd;       //socket/stdin/stdout/stderr, -1 if not used

   int pollIdx;   //used by motChannelPollSetup()
   ChannelMode ch_mode;
   JobIoMode ch_io;
   int ch_timeout;   //request timeout in msec

   ReadChunk head;   //header for circular raw read queue
   JsonQ ch_json_head;   //header for circular json read queue
   ArrayList ch_block_ids;   //list of IDs that channel_read_json_block() is waiting for
   //When ch_wait_len is non-zero use deadline to wait for incomplete message to be complete. 
   //The value is the length of the incomplete message when the deadline was set.  If it gets 
   //longer (something was received) the deadline is reset.
   Unt ch_wait_len;
   TimeSpec deadline;
   int ch_block_write; //for testing: 0 when not used, -1 when write
                       //does not block, 1 simulate blocking
   int ch_nonblocking; //write() is non-blocking
   WriteQueue ch_writeque;   //header for write queue

   CbNode ch_cb_head;   //dummy node for per-request callbacks
   void (*nativeCb)(Arr(Byte));
   Callback ch_callback;   //call when a msg is not handled

   BookRef bookref;   //book to read from or write to
   int ch_nomodifiable; //TRUE when book can be not 'modifiable'
   int ch_nomod_error;   //TRUE when e_modifiable was given
   int ch_buf_append;   //write appended lines instead top-bot
   LineNr ch_buf_top;   //next line to send
   LineNr ch_buf_bot;   //last line to send
} ChannelFd;

pub
struct Channel {
   Channel* next;
   Channel* prev;

   int id;      //ID of the channel
   int lastMsgId;   //ID of the last message
   CS socketName;      //Unix domain socket name
   ChannelFd fds[PART_COUNT]; //info for socket, out, err and in
   int writeTextMode; //write book lines with CR, not NL

   Boole ch_to_be_closed; //bitset of readable fds to be closed.
            //When all readable fds have been closed, set to (1 << PART_COUNT).
   Boole ch_to_be_freed; //When TRUE, channel must be freed when it's safe to invoke callbacks
   int error;   //When TRUE an error was reported.  Avoids giving pages full of error 
                //messages when the other side has exited, only mention the first error 
                //until the connection works again.

   Callback ch_callback;   //call when any msg is not handled
   Callback ch_close_cb;   //call when channel is closed
   int ch_drop_never;
   int ch_keep_open;   //do not close on read error
   int ch_nonblock;

   Job* job;   //Job that uses this channel; this does not count as a reference to avoid a 
                  //circular reference, the job refers to the channel.
   int ch_job_killed;   //TRUE when there was a job and it was killed or we know it died.
   int ch_anonymous_pipe;  //ConPTY
   int isBeingKilled;       //TerminateJobObject() was called

   Unt refCount;   //reference count
   int copyId;
};

typedef enum {
   CW_READY,
   CW_NOT_READY,
   CW_ERROR
} channel_wait_result;

typedef struct sockaddr_un SockAddrUn;
typedef struct sockaddr SockAddr;

//}}}
#include "h/motor.h"
#include "h/motor.time.h"
//{{{@@forward declarations
private void list_version(void);
private void intro_message(int colon);
private void do_intro_line(int row, CS mesg, int add_version);
private void init1(OUT MainParams* par);
private void initUi(void);
private int isSafeNow(void);
private void earlyArgScan(MainParams* par);
private int getNumericArg(
   CS p,       //pointer to argument
   int* idx,       //index in argument, is incremented
   int def       //default value
);
private void parseCommandName(MainParams* par);
private void scanCommandLineArgs(MainParams *par);
private void check_tty(MainParams* par);
private void readStdin(void);
private void createPortals(MainParams* par);
private void editBuffers(MainParams* par, CS cwd);
private void executePreCommands(MainParams* par);
private void exeCommands(MainParams* par);
private void sourceStartupScripts(MainParams* par);
private void mainerr(
   Unt n,   //one of the ME_ defines
   NULLABLE CS str   //extra argument
);
private void main_msg(CS s);
private void usage(void);
private void check_swap_exists_action(void);
private void set_progpath(CS argv0);
private void catch_sigint(int);
private void catch_sigusr1(int);
private void catch_sigpwr(int);
private void deathtrap(int sigarg);
private void after_sigcont(void);
private void sigcont_handler(int);
private void catch_int_signal(void);
private void catch_signals(void (*func_deadly)(int), void (*func_other)(int));
private void setupSignalHandlers(void);
private void init_signal_stack(void);
private CS get_signal_name(int sig);
private void block_signals(SignalSet* set);
private void unblock_signals(SignalSet* set);
private void exit_scroll(void);
private Tm * eeLocaltime(
   Tyme const* timep,      //timestamp for local representation
   OUT Tm* result //pointer to caller return buffer
);
private int list2proftime(Var *arg, ProfTime *tm);
private void insert_timer(Timer* timer);
private void remove_timer(Timer* timer);
private void free_timer(Timer* timer);
private void timer_callback(Timer *timer);
private Timer * find_timer(long id);
private void stop_all_timers(void);
private void add_timer_info(OUT Var* returnVar, Timer *timer);
private void add_timer_info_all(OUT Var* returnVar);
private void time_diff(TimeSpec* then, TimeSpec* now);
private double profile_float(ProfTime *tm);
private void set_flag(union sigval);
private void set_flag(union sigval);
private void channel_free_contents(Channel* channel);
private void channel_free_channel(Channel* channel);
private void channel_free(Channel* channel);
private int channel_may_free(Channel* channel);
private int channel_connect(Channel* channel, SockAddr* server_addr, int server_addrlen, int *waittime);
private Channel* channel_open_unix(CS path);
private void setCallback(Callback* cbp, Callback* callback);
private void prepareBookForWriting(Book* book);
private Book* chaFindBook(CS name, int err, int msg);
private void channel_set_options(Channel* channel, JobOptions* opt);
private Channel * channel_open_func(Arr(Var) argvars);
private void channel_set_req_callback(Channel* channel, ChannelFdKind part, Callback* callback, int id);
private void write_buf_line(Book* book, LineNr lnum, Channel* channel);
private int can_write_buf_line(Channel* channel);
private void channel_write_input(Channel* channel);
private void invoke_callback(Channel* channel, Callback* callback, Var* argv);
private CS channel_get(Channel* channel, ChannelFdKind part, int *outlen);
private CS channel_get_all(Channel *channel, ChannelFdKind part, int *outlen);
private int saveMsg(Channel* channel, ChannelFdKind part, CS msg, int len, int prepend, CS logLead);
private int channel_fill(JsReader* reader);
private int channel_process_lsp_http_hdr(JsReader* reader);
private int channel_parse_json(Channel* channel, ChannelFdKind part);
private void remove_cb_node(CbNode* head, CbNode* node);
private void remove_json_node(JsonQ* head, JsonQ* node);
private void channel_add_block_id(ChannelFd* chanpart, int id);
private void channel_remove_block_id(ChannelFd* chanpart, int id);
private int channel_has_block_id(ChannelFd* chanpart, int id);
private int channel_get_json(
   Channel   *channel,
   ChannelFdKind   part,
   int       id,
   int       without_callback,
   Var    **returnVar
);
private void channel_push_json(Channel* channel, ChannelFdKind part, Var* returnVar);
private void channel_exe_cmd(Channel* channel, ChannelFdKind part, Var* argv);
private void invoke_one_time_callback(Channel* channel, CbNode* cbhead, CbNode* item, Var* argv);
private void appendToBook(Book* book, CS msg, Channel* channel, ChannelFdKind part);
private void drop_messages(Channel* channel, ChannelFdKind part);
private int channel_use_json_head(Channel* channel, ChannelFdKind part);
private int may_invoke_callback(Channel* channel, ChannelFdKind part);
private int channel_can_write_to(Channel* channel);
private void * channel_readahead_pointer(Channel* channel, ChannelFdKind part);
private int channel_has_readahead(Channel *channel, ChannelFdKind part);
private CS channel_status(Channel *channel, int req_part);
private void channel_part_info(Channel* channel, Bag* bag, CS name, ChannelFdKind part);
private void channelInfoIntoDict(Channel *channel, OUT Bag *dict);
private void channel_close(Channel *channel, int invoke_close_cb);
private void channel_close_in(Channel *channel);
private void remove_from_writeque(WriteQueue *wq, WriteQueue *entry);
private void channel_clear_one(Channel *channel, ChannelFdKind part);
private int is_channel_write_remaining(ChannelFd* intake);
private int fillIntake(int nfd_in, Arr(PollFd) fds);
private channel_wait_result channel_wait(Channel* channel, Socket fd, int timeout);
private void ch_close_part_on_error(Channel *channel, ChannelFdKind part, int is_err, char *func);
private void channel_close_now(Channel *channel);
private void channel_read(Channel *channel, ChannelFdKind part, char *func);
private CS channel_read_block(Channel *channel, ChannelFdKind part, int timeout, int raw, int *outlen);
private int channel_read_json_block(
   Channel* channel,
   ChannelFdKind part,
   int timeout_arg,
   int id,
   Var** returnVar
);
private void commonChannelRead(Var* argvars, Var* returnVar, int raw, int blob);
private Channel* send_common(
   Var* argvars,
   CS text,
   int len,
   int id,
   int eval,
   JobOptions* opt,
   char* fun,
   ChannelFdKind* part_read
);
private void ch_expr_common(Arr(Var) argvars, Var* returnVar, int eval);
private void ch_raw_common(Var* argvars, OUT Var* returnVar, int eval);
private int checkPollResult(int ret_in, OUT Arr(PollFd) fds);
private ChannelFdKind channel_part_send(Channel* channel);
private ChannelFdKind channel_part_read(Channel* channel);
private ChannelMode channel_get_mode(Channel* channel, ChannelFdKind part);
private int channel_get_timeout(Channel *channel, ChannelFdKind part);
private int build_argv_from_list(List *l, Byte*** argv, int *argc);
private Long get_signal_stack_size(void);
private void may_send_sigint(Unt c, ProId pid, ProId wpid);
private ProId wait4pid(ProId child, waitstatus *status);
private void writeFromCurBookToShell(int fromShell, int toShell);
private PolyWithStatus callShellImpl(Text cmd, Unt opt);
private void open_pty(int* pty_master_fd, int* pty_slave_fd, Byte** name1, Byte** name2);
private void set_child_environment(Long rows, Long columns, CS term, Boole is_terminal);
private void set_default_child_environment(Boole is_terminal);
private void mch_job_start(Byte** argv, Job* job, JobOptions* options, Boole is_terminal);
private CS mch_job_status(Job* job);
private Job * mch_detect_ended_job(Job* job_list);
private int handle_mode(Var* item, JobOptions* opt, ChannelMode* modep, int jo);
private int handle_io(Var* item, ChannelFdKind part, JobOptions* opt);
private void unref_job_callback(Callback *cb);
private int part_from_char(int c);
private void job_free_contents(Job* job);
private void job_unlink(Job* job);
private void job_free_job(Job* job);
private void job_free(Job* job);
private void job_free_later(Job* job);
private void free_jobs_to_free_later(void);
private int job_need_end_check(Job* job);
private int job_channel_still_useful(Job* job);
private int job_channel_can_close(Job* job);
private int job_still_useful(Job* job);
private void job_cleanup(Job* job);
private CS buf_prompt_text(Book* book);
private Job * get_job_arg(Var* tv);
private void job_info(Job* job, Bag* bag);
private void job_info_all(List* l);
private void logLead(CS what, Channel* ch, ChannelFdKind part);
private void ch_log_literal(CS lead, Channel* ch, ChannelFdKind part, OUT Text builder);
private void add_user(Byte *user, int need_copy);
private void init_users(void);
private int ses_put_fname(FILE *fd, CS name);
private int ses_fname(FILE* fd, Book* book, int add_eol);
private int ses_arglist(FILE* fd, CS cmd, ArrayList* gap, int fullname);
private Boole portNeedsToBeSaved(Portal* po);
private Boole ses_do_frame(Frame* fr);
private Frame* ses_skipframe(Frame* fr);
private int recreatePortals(FILE* fd, Frame* fr);
private int portalSizes(FILE* fd, int restore_size, Portal* tab_firstPor);
private int put_view_curpos(FILE *fd, Portal *wp, char *spaces);
private int put_view(
   FILE* fd,
   Portal* wp,
   Tab* tp,
   int add_edit,        //add ":edit" command to view
   int current_arg_idx,     //current argument index of the portal, use -1 if unknown
   EeSet* terminal_bufs //already encountered terminal books, can be NULL
);
private VarFlavor getVarFlavor(CS varname);
private int store_session_globals(FILE *fd);
private int makeopens(FILE   *fd, Byte   *currDir);
private CS find_eeglinfo_parameter(int type);
private CS eeglinfo_filename(CS file);
private void eeglinfo_writestring(FILE* fd, CS p);
private int barline_writestring(FILE *fd, CS s, int remaining_start);
private CS eeglinfo_readstring(Vir* virp, int off);
private int eeglinfo_readline(Vir* virp);
private int readEeglinfoBookList(Vir* virp, int writing);
private Boole removable(CS name);
private void writeEeglInfoBookList(FILE* fp);
private int hist_type2char(int type, int use_question);
private void prepare_eeglinfo_history(int asklen, int writing);
private int read_eeglinfo_history(Vir* virp, int writing);
private void handle_eeglinfo_history(ArrayList* values, int writing);
private void concat_history(int type);
private int sort_hist(const void *s1, const void *s2);
private void merge_history(int type);
private void finish_eeglinfo_history(Vir *virp);
private void write_eeglinfo_history(FILE *fp, int merge);
private void write_eeglinfo_barlines(Vir *virp, FILE *fp_out);
private int barline_parse(Vir* virp, CS text, ArrayList* values);
private void write_eeglinfo_version(FILE* fp_out);
private int no_eeglinfo(void);
private int eeglinfo_error(CS errnum, CS message, Byte *line);
private int read_eeglinfo_varlist(Vir* virp, int writing);
private void write_eeglinfo_varlist(FILE* fp);
private int read_eeglinfo_sub_string(Vir* virp, int force);
private void write_eeglinfo_sub_string(FILE *fp);
private int read_eeglinfo_search_pattern(Vir* virp, Boole force);
private void wvsp_one(
   FILE* fp,   //file to write to
   int idx,   //spats[] index
   CS s,   //search pat
   int sc   //dir char
);
private void write_eeglinfo_search_pattern(FILE* fp);
private void prepare_eeglinfo_registers(void);
private void finish_eeglinfo_registers(void);
private int read_eeglinfo_register(Vir* virp, Boole force);
private void handle_eeglinfo_register(ArrayList *values, int force);
private void write_eeglinfo_registers(FILE* fp);
private void write_one_mark(FILE* fp_out, int c, Pos* pos);
private void writeBookMarks(Book* book, FILE* fp_out);
private int skip_for_eeglinfo(Book *book);
private void write_eeglinfo_marks(FILE* fp_out, ArrayList* buflist);
private void write_one_filemark(FILE* fp, FileMarkExt* fm, int c1, int c2);
private void write_eeglinfo_filemarks(FILE* fp);
private void copy_eeglinfo_marks(
   Vir* virp,
   FILE* fp_out,
   ArrayList* buflist,
   int eof,
   int flags
);
private int read_eeglinfo_filemark(Vir *virp, int force);
private void prepare_eeglinfo_marks(void);
private void finish_eeglinfo_marks(void);
private void handle_eeglinfo_mark(ArrayList *values, int force);
private int read_eeglinfo_barline(Vir* virp, Boole force, int writing);
private int read_eeglinfo_up_to_marks(Vir* virp, Boole forceit, int writing);
private void do_eeglinfo(FILE* fp_in, FILE* fp_out, Unt flags);
//}}}

//Whether we are inside channel_parse_messages() or another situation where it
//is safe to invoke callbacks.
private int safe_to_invoke_callback = 0;

//The list of all allocated channels.
private Channel *firstChannelP = NULL;
private int next_ch_id = 0;
private int ignore_sigtstp = false;

#define LOG_ALWAYS 9//must be different from true and false

//pub GEN_TYPE_L(PollFd);
//generic(2) GEN_add_L(rivate, PollFd)

//{{{the intro screen and version info about the current build

//Vim originated from Stevie version 3.6 (Fish disk 217) by GRWalter (Fred)
//It has been changed beyond recognition since then.
//Now there is a simple IDE forked off from it, Eegl.

private CS programVersion = (CS)EEGL_VERSION_SHORT;
private CS mediumVersion = (CS)EEGL_VERSION_MEDIUM;

//char longVersion[sizeof(EEGL_VERSION_LONG_DATE) + sizeof(__DATE__) + sizeof(__TIME__) + 3];

private Byte longVersion[] = EEGL_VERSION_LONG_DATE __DATE__ " " __TIME__ ")";


private int included_patches[] = {   
//Add new patch number below this line */
   0
};

//Place to put a short description when adding a feature with a patch.
//Keep it short, e.g.,: "relative numbers", "persistent undo".
//Also add a comment marker to separate the lines.
//See the official Eegl patches for the diff format: It must use a context of
//one line only.  Create it by hand or use "diff -C2" and edit the patch.
private CS extra_patches[] = {
   //Add your patch description below this line
   NULL
};

pub int
highest_patch(void) {
   //this relies on the highest patch number to be the first entry
   return included_patches[0];
}

private void
list_version(void) {
   int i;
   int first;
   CS s = S"";

   //When adding features here, don't forget to update the list of internal variables in eval.c!
   msg(longVersion);


   //Print the list of patch numbers if there is at least one.
   //Print a range when patches are consecutive: "1-10, 12, 15-40, 42-45"
   if (included_patches[0] != 0) {
      msg_puts(_("\nIncluded patches: "));
      first = -1;
      i = (int)ARRAY_LENGTH(included_patches) - 1;
      while (--i >= 0) {
         if (first < 0)
            first = included_patches[i];
         if (i == 0 || included_patches[i - 1] != included_patches[i] + 1) {
            msg_puts(s);
            s = S", ";
            msg_outnum((long)first);
            if (first != included_patches[i]) {
               msg_puts(S"-");
               msg_outnum((long)included_patches[i]);
            }
            first = -1;
         }
      }
   }

   //Print the list of extra patch descriptions if there is at least one.
   if (extra_patches[0] != NULL) {
      msg_puts(_("\nExtra patches: "));
      s = S"";
      for (i = 0; extra_patches[i] != NULL; ++i) {
         msg_puts(s);
         s = S", ";
         msg_puts(extra_patches[i]);
      }
   }

   if (msgColG > 0)
      msg_putchar('\n');

   printMsgWithWrap(_("       defaults file: \""));
   printMsgWithWrap(EE_DEFAULTS_FILE);
   printMsgWithWrap((CS)"\"\n");
#ifdef DEBUG
   printMsgWithWrap("\n");
   printMsgWithWrap(_("  DEBUG BUILD"));
#endif
}


pub void
c_version(Invocation* invo) {
   //Ignore a ":version 9.99" command.
   if (*invo->arg == ZERO) {
      msg_putchar('\n');
      list_version();
   }
}


private void do_intro_line(int row, CS mesg, int add_version);
private void intro_message(int colon);

//Show the intro message when not editing a file.
pub void
maybe_intro_message(void) {
   if (CURBOOK_EMPTY() && !curBook->currFileName && !firstPor->next && p_intro)
      intro_message(false);
}

//Give an introductory message about Eegl.
//Only used when starting Eegl on an empty file, without a file name.
//Or with the ":intro" command (for Sven :-).
private void
intro_message(int colon) {     //true for ":intro"
   int i;
   CS p;
   static CS lines[] = { SMAP((CS),
      "Eegl - Extensible editor for GNU/Linux",
      "",
      "version ",
      "by Bram Moolenaar, Egor Sozonov et al.",
      "Eegl is open source and freely distributable",
      "",
      "type  :q<Enter>               to exit         ",
      "type  :help<Enter>  or  <F1>  for on-line help",
      "type  :help version9<Enter>   for version info",
      "",
      ""
   )};

   //blanklines = screen height - # message lines
   int blanklines = (int)visibleRowsG - (ARRAY_LENGTH(lines) - 1) + 4;

   //Don't overwrite a statusline.  Depends on @commheight.
   blanklines -= visibleRowsG - topframeG->width;
   if (blanklines < 0)
      blanklines = 0;
   //Show the sponsor and register message one out of four times, the Uganda
   //message two out of four times.
   int sponsor = (int)time(NULL);
   sponsor = ((sponsor & 2) == 0) - ((sponsor & 4) == 0);

   //start displaying the message lines after half of the blank lines
   int row = blanklines / 2;
   if ((row >= 2 && topframeG->width >= 50) || colon) {
      for (i = 0; i < (int)ARRAY_LENGTH(lines); ++i) {
         p = lines[i];
         if (!p) {
            break;
         }
         if (sponsor != 0) {
            if (STRSTR(p, "children") != NULL)
               p = sponsor < 0
                  ? N_("Sponsor Eegl development!")
                  : N_("Become a registered Eegl user!");
            ei (STRSTR(p, "iccf") != NULL)
               p = sponsor < 0
                  ? N_("type  :help sponsor<Enter>    for information ")
                  : N_("type  :help register<Enter>   for information ");
            ei (STRSTR(p, "Orphans") != NULL)
               p = N_("menu  Help->Sponsor/Register  for information    ");
         }
         if (*p != ZERO)
            do_intro_line(row, (CS)_(p), i == 2);
         ++row;
      }
   }

   //Make the wait-return message appear just below the text.
   if (colon)
      msgRowG = row;
}

private void
do_intro_line(int row, CS mesg, int add_version){
   Byte vers[20];
   //Center the message horizontally.
   int col = eeglStrSize(mesg);
   if (add_version) {
      STRCPY(vers, mediumVersion);
      if (highest_patch()) {
         //Check for 9.9x or 9.9xx, alpha/beta version
         if (SAFE_isalpha((int)vers[3])) {
            int len = (SAFE_isalpha((int)vers[4])) ? 5 : 4;
            sprintf((char *)vers + len, ".%d%s", highest_patch(), mediumVersion + len);
         } else
            sprintf((char *)vers + 3, ".%d", highest_patch());
      }
      col += (int)STRLEN(vers);
   }
   col = (topframeG->width - col) / 2;
   if (col < 0)
      col = 0;

   //Split up in parts to highlight <> items differently.
   int l;
   for (CS p = mesg; *p != ZERO; p += l) {
      int clen = 0;
      for (l = 0; p[l] != ZERO
             && (l == 0 || (p[l] != '<' && p[l - 1] != '>')); ++l
      ){
         clen += bookPtr2Cells(p + l);
         l += utfCharLen(p + l) - 1;
      }
      drawTextLen(p, l, row, col + firstPor->windowCol, *p == '<' ? getDecoFlags(HLF_8) : 0);
      col += clen;
   }

   //Add the version number to the version line.
   if (add_version)
      drawText(vers, row, col + firstPor->windowCol, 0);
}

//":intro": clear screen, display intro screen and wait for return.
pub void
c_intro(Invocation*){
   screenclear();
   draw_tabpanel();
   intro_message(true);
   wait_return(true);
}

//}}}

//Various parameters passed between main() and other functions.
private MainParams paramsP;

//volatile because it is used in signal handler deathtrap().
private volatile SigAtomic inMchDelayP = false; //sleeping in mch_delay()
private void* virtualBuf = null;      //buffer for setvbuf()
private CS start_dir = NULL;   //current working dir on startup

//Different types of error messages.
private CS main_errors[] = {
    N_("Unknown option argument"),
#define ME_UNKNOWN_OPTION   0
    N_("Too many edit arguments"),
#define ME_TOO_MANY_ARGS    1
    N_("Argument missing after"),
#define ME_ARG_MISSING      2
    N_("Garbage after option argument"),
#define ME_GARBAGE          3
    N_("Too many \"+command\", \"-c command\" or \"--comm command\" arguments"),
#define ME_EXTRA_CMD        4
    N_("Invalid argument for"),
#define ME_INVALID_ARG      5
};

//flags for read_eeglinfo() and children
#define EIF_WANT_INFO       1   //load non-mark info
#define EIF_WANT_MARKS      2   //load file marks
#define EIF_ONLY_CURBOOK    4   //bail out after loading marks for curBook
#define EIF_FORCEIT         8   //overwrite info already read
#define EIF_GET_OLDFILES   16   //load v:oldfiles

pub int
libMain(void) {
   //Decide about portal layout for diff mode after reading init.vim.
   if (paramsP.diff_mode && paramsP.portalLayout == 0) {
      if (diffopt_horizontal())
         paramsP.portalLayout = WIN_HOR;   //use horizontal split
      else
         paramsP.portalLayout = WIN_VER;   //use vertical split
   }

   //Recovery mode without a file name
   if (recoveryModeG && paramsP.fname == NULL) {
      mch_exit(0);
   }

   //Set a few option defaults after reading .vimrc files: @shellpipe and @shellredir.
   optInit1();
   TIME_MSG("inits 1");

   //"-n" argument: Disable swap file by setting 'updatecount' to 0.
   //Note that this overrides anything from a vimrc file.
   if (paramsP.no_swap_file)
      { swapEnabledG = false; }

   //Read in registers, history etc, but not marks, from the eeglinfo file.
   //This is where v:oldfiles gets filled.
   if (p_eeglinfo) {
      read_eeglinfo(NULL, EIF_WANT_INFO | EIF_GET_OLDFILES);
      TIME_MSG("reading eeglinfo");
   }

   //"-q errorfile": Load the error file now.
   //If the error file can't be read, exit before doing anything else.
   if (paramsP.edit_type == EDIT_QF) {
      if (paramsP.use_ef)
         optChangeStringOptionDirect(S"errorfile", paramsP.use_ef, 0, SID_CARG);
      eeSnprintf(IObuff, IOSIZE, "cfile %s", p_ef);
      if (llInitFromFile(NULL, p_ef, curBook->o.errorFormat, true, IObuff) < 0) {
         out_char('\n');
         mch_exit(3);
      }
      TIME_MSG("reading errorfile");
   }

   //Start putting things on the screen.
   //Scroll screen down before drawing over it
   //Clear screen now, so file message will not be cleared.
   starting = NO_BOOKS;
   no_wait_return = false;
   msg_scroll = false;

   //If "-" argument given: Read file from stdin. Do this before starting Raw mode, because it may 
   //change things that the writing end of the pipe doesn't like, e.g., in case stdin and stderr
   //are the same terminal: "cat | eegl -". Using autocommands here may cause trouble...
   if (paramsP.edit_type == EDIT_STDIN && !recoveryModeG)
      readStdin();

   //When switching screens and something caused a message from a vimrc
   //script, need to output an extra newline on exit.
   if ((anyEmsgG || msg_didout) && *termCodesG[KS_TI] != ZERO && paramsP.edit_type != EDIT_STDIN)
      newlineOnExitG = true;

   //When done something that is not allowed or given an error message call wait_return(). This 
   //must be done before starttermcap(), because it may switch to another screen. It must be done
   //after termSetMode(TMODE_RAW), because we want to react on a single key stroke.
   //Call termSetMode and starttermcap here, so the KS_KS and KS_TI may be defined by 
   //termInitTerminfo()
   termSetMode(TMODE_RAW);
   TIME_MSG("setting raw mode");

   if (need_wait_return || msg_didany) {
      wait_return(true);
      TIME_MSG("waiting for return");
   }

   starttermcap();       //start termcap if not done by wait_return()
   TIME_MSG("start termcap");

   setmouse();            //may start using the mouse
   if (scroll_region)
      scroll_region_reset();      //In case visibleRowsG changed
   scroll_start();   //may scroll the screen to the right position

   screenclear();         //clear screen
   TIME_MSG("clearing screen");

   no_wait_return = true;

   //Create the requested number of portals and edit buffers.
   //Also does recovery if "recoveryModeG" set.
   createPortals(&paramsP);
   TIME_MSG("opening buffers");

   applyAutocomms(EVENT_BUFENTER, NULL, NULL, false, curBook);
   TIME_MSG("BufEnter autocommands");
   setpcmark();

   //When started with "-q errorfile" jump to first error now.
   if (paramsP.edit_type == EDIT_QF) {
      llJump(NULL, 0, 0, false);
      TIME_MSG("jump to first error");
   }

   //If opened more than one portal, start editing files in the other portals.
   editBuffers(&paramsP, start_dir);
   eeglFree(start_dir);

   if (paramsP.diff_mode) {
      //set options in each portal for "eegldiff".
      Portal* port;
      FOR_ALL_PORTALS(port)
         diff_win_options(port, true);
   }

   //Shorten any of the filenames, but only when absolute.
   shorten_fnames(false);

   //Need to jump to the tag before executing the '-c command'. Makes "eegl -c '/return' -t main" work
   if (paramsP.tagname != NULL) {
      swap_exists_did_quit = false;

      eeSnprintf(IObuff, IOSIZE, "ta %s", paramsP.tagname);
      executeCommLine(IObuff);
      TIME_MSG("jumping to tag");

      //If the user doesn't want to edit the file then we quit here.
      if (swap_exists_did_quit)
         exitEegl(1);
   }

   //Execute any "+", "-c" and "-S" arguments.
   if (paramsP.n_commands > 0)
      exeCommands(&paramsP);

   //Must come before the may_req_ calls.
   starting = 0;

   //Must be done before redrawing, puts a few characters on the screen.
   check_terminal_behavior();

   isRedrawingDisabledG = 0;
   redraw_all_later(UPD_NOT_VALID);
   no_wait_return = false;

   //'autochdir' has been postponed
   DO_AUTOCHDIR;

   applyAutocomms(EVENT_EEGLENTER, NULL, NULL, false, curBook);
   TIME_MSG("EeglEnter autocommands");

   //Adjust default register name for "unnamed" in 'clipboard'. Can only be
   //done after the clipboard is available and all initial commands that may
   //modify the 'clipboard' setting have run; i.e. just before entering the main loop.
   reset_reg_var();

   //When a startup script or session file setup for diffing and scrollbind, sync the scrollbind now
   if (curPor->o.diff) {
      update_topline();
      check_scrollbind((LineNr)0, 0L);
      TIME_MSG("diff scrollbinding");
   }

   //If ":startinsert" command used, stuff a dummy command to be able to
   //call normalAction(), which will then start Insert mode.
   if (restart_edit != 0)
      stuffcharReadbuff(K_NOP);

   //Redraw at least once, also when 'lazyredraw' is set, to be sure the window title gets updated
   //do_redraw = true;

   TIME_MSG("before starting main loop");

   //Call the main command loop. This never returns.
   mainLoop(false);

   return 0;
}

//Initialization #1 shared by main() and some tests.
pub void
init0(void) {
   estack_init();
   cmdline_init();
   bookInitGlobalCharTable();

   CS errMsg = inputInitCharLens();
   if (errMsg) {
      emsg(errMsg);
      return;
   }
   //optsInitializeGlobalDefaults();
   evalInitGlobals();   //init global variables

   //Allocate space for the generic buffers (needed for optInit0() and emsg()).
   IObuff = alloc(IOSIZE);
   nameBuffG = alloc(MAXPATHL);
   TIME_MSG("Allocated generic buffers");
}

//Initialization #1 shared by main() and some tests.
private void
init1(OUT MainParams* par) {
   //Setup to use the current locale (for ctype() and many other things).
   //NOTE: Translated messages with encodings other than latin1 will not work until 
   //optInit0() has been called!
   init_locale();
   TIME_MSG("locale set");
   
   //Set the default values for the options.
   //First find out the home directory, needed to expand "~" in options.
   init_homedir();      //find real value of $HOME
   TIME_MSG("inits 0");

   swapDirG = fiInitSwapDir((CS)par->argv[0]);

   //Do a first scan of the arguments in "argv[]":
   //  -display or --display
   //  --server...
   //  --socketid
   //  --windowid
   earlyArgScan(par);

   TIME_MSG("clipboard setup");

   //Check if we have an interactive window.
   stdout_isatty = (isatty(1) != FAIL);
   TIME_MSG("window checked");

   //Initialize global values of all options
   optInit0();
   
   //Allocate the first portal and book. Can't do anything without it, exit when it fails.
   if (portAllocFirst() == FAIL)
      mch_exit(0);

   init_yank();      //init yank buffers

   alist_init(&argListG);   //Init the argument list to empty.
   argListG.id = 0;

   init_signs();
   
   set_internal_string_var(S"g:mapleader", S",");

   //initialize location lists. don't send an error message when memory allocation fails.
   //do it when the user tries to access a location list
   llInitStacksOnce();
}

private void
initUi(void) {
   lo("initUi");
   visibleColsG = 80;
   visibleRowsG = 24;

   termOutFlush();

   //Check whether we were invoked with SIGTSTP set to be ignored. If it is
   //that indicates the shell (or program) that launched us does not support
   //tty job control and thus we should ignore that signal.
   ignore_sigtstp = SIG_IGN == motSignalHandler(SIGTSTP, SIG_ERR);
   setupSignalHandlers();
}

pub int
appMain(int argc, char** argv) {
   //Do any system-specific initialisations.  These can NOT use IObuff or nameBuffG.  
   //Thus emsg2() cannot be called!
   mch_early_init();

   //Many variables are in "par" so that we can pass them to invoked functions without a lot 
   //of arguments. "argc" and "argv" are also copied, so that they can be changed.
   CLEAR_FIELD(paramsP);
   paramsP.argc = argc;
   paramsP.argv = argv;
   paramsP.want_full_screen = true;
   paramsP.use_debug_break_level = -1;
   paramsP.portalCount = UNT;

   autocmd_init();

#ifdef MEM_PROFILE
   atexit(eeMemProfileDump);
#endif

   //Various initializations #0 shared with tests.
   init0();

   //Need to find "--startuptime" and "--log" before actually parsing arguments.
   for (int i = 1; i < argc - 1; ++i) {
      if (caseInsensitiveCompare(argv[i], "--startuptime") == 0 && time_fd == NULL) {
         time_fd = fopen(argv[i + 1], "a");
         TIME_MSG("--- EEGL RISING ---");
      }
      if (caseInsensitiveCompare(argv[i], "--log") == 0)
         ch_logfile((CS)(argv[i + 1]), S"ao");
   }

   //Various initializations #1 shared with tests.
   init1(OUT &paramsP);

   //Figure out the way to work from the command name argv[0]. "eegldiff" starts diff mode, etc.
   parseCommandName(OUT &paramsP);
   
   p_modifiable = true;

   //Process command line arguments. File names are put into the global argument list "argListG"
   scanCommandLineArgs(&paramsP);
   TIME_MSG("parsing arguments");

   //On some systems, when we compile with the GUI, we always use it.  On Mac
   //there is no terminal version, and on Portals we can't fork one off with :gui.
   if (GARGCOUNT > 0) {
      paramsP.fname = alist_name(&GARGLIST[0]);
   }

   TIME_MSG("expanding arguments");

   if (paramsP.diff_mode && paramsP.portalCount == UNT)
      paramsP.portalCount = 0;   //open up to 3 portals

   //Don't redraw until much later.
   ++isRedrawingDisabledG;

   //When listing swap file names, don't do cursor positioning et. al.
   if (recoveryModeG && paramsP.fname == NULL)
      paramsP.want_full_screen = false;

   //initUi() sets up the terminal (window) for use. This must be done after resetting 
   //fullScreenG, otherwise it may move the cursor. Note that we may use mch_exit() before initUi()!
   initUi();
   TIME_MSG("shell init");

   //Print a warning if stdout is not a terminal.
   check_tty(&paramsP);

   if (silentModeG) {
      //Ensure output works usefully without a tty: buffer lines instead of fully buffered.
      virtualBuf = malloc(BUFSIZ);
      setvbuf(stdout, virtualBuf, _IOLBF, BUFSIZ);
   }

   //This message comes before term inits, but after setting "silentModeG"
   //when the input is not a tty. Omit the message with --not-a-term.
   if (GARGCOUNT > 1 && !silentModeG && !is_not_a_term())
      printf((char*)_("%d files to edit\n"), GARGCOUNT);

   initHilite(true); //set the default hilite groups
   drawInit();
   if (paramsP.want_full_screen && !silentModeG) {
      //set terminal name and get terminal capabilities (will set fullScreenG)
      termInitTerminfo(paramsP.term);
      screen_start();      //don't know where cursor is now
      TIME_MSG("Termcap init");
   }

   //Set the default values for the options that use visibleRowsG and visibleColsG.
   ui_get_shellsize();      //inits Rows and Columns
   portalInitSize();
   //Set the @diff option now, so that it can be checked for in an init.vim
   //file. There is no book yet, though.
   if (paramsP.diff_mode)
      diff_win_options(firstPor, false);

   commlineRowG = visibleRowsG - commlineHeightG;
   msgRowG = commlineRowG;
   screenalloc(false);      //allocate screen buffers
   optInit1();
   TIME_MSG("inits 0");

   msg_scroll = true;
   no_wait_return = true;

   TIME_MSG("init hilite");

   termInitProps(true);

   //Set the break level after the terminal is initialized.
   debug_break_level = paramsP.use_debug_break_level;

   //Execute --comm arguments.
   executePreCommands(&paramsP);

   //Source startup scripts.
   sourceStartupScripts(&paramsP);

   return libMain();
}

//Return true when the --not-a-term argument was found.
pub int
is_not_a_term(void) {
   return paramsP.not_a_term;
}

//Return true when the --not-a-term argument was found or the GUI is in use.
pub int
is_not_a_term_or_gui(void) {
   return paramsP.not_a_term;
}

#if defined(EXITFREE)
pub void
free_vbuf(void) {
   if (virtualBuf) {
      setvbuf(stdout, NULL, _IONBF, 0);
      free(virtualBuf);
      virtualBuf = NULL;
   }
}
#endif

//When true in a safe state when starting to wait for a character.
private Boole wasSafeP = false;

//Return whether currently it is safe, assuming it was safe before (high level state didn't change)
private int
isSafeNow(void) {
   return stuff_empty()
      && typeBufG.validLen == 0
      && scriptin[curscript] == NULL
      && !debug_mode
      && !global_busy;
}

//Trigger SafeState if currently in a safe state, that is "safe" is true and there is no typeahead
pub void
may_trigger_safestate(Boole safe) {
   Boole is_safe = safe && isSafeNow();
   if (wasSafeP != is_safe)
      //Only log when the state changes, otherwise it happens at nearly every key stroke.
      lo(is_safe ? "SafeState: Start triggering" : "SafeState: Stop triggering");
   if (is_safe)
      applyAutocomms(EVENT_SAFESTATE, NULL, NULL, false, curBook);
   wasSafeP = is_safe;
}

//Something changed which causes the state possibly to be unsafe, e.g. a
//character was typed.  It will remain unsafe until the next call to may_trigger_safestate().
pub void
state_no_longer_safe(CS reason) {
   if (wasSafeP)
      lo("SafeState: reset: %s", reason);
   wasSafeP = false;
}

pub Boole
get_was_safe_state(void) {
   return wasSafeP;
}

//Invoked when leaving code that invokes callbacks.  Then trigger
//SafeStateAgain, if it was safe when starting to wait for a character.
pub void
may_trigger_safestateagain(void) {
   if (!wasSafeP)     {
      //If the safe state was reset in state_no_longer_safe(), e.g. because
      //of calling feedkeys(), we check if it's now safe again (all keys were consumed).
      wasSafeP = isSafeNow();
      if (wasSafeP)
         lo("SafeState: undo reset");
   }
   if (wasSafeP) {
      //Only do this message when another message was given, otherwise we get lots of them.
      if ((did_repeated_msg & REPEATED_MSG_SAFESTATE) == 0)    {
         int did = did_repeated_msg;

         lo("SafeState: back to waiting, triggering SafeStateAgain");
         did_repeated_msg = did | REPEATED_MSG_SAFESTATE;
      }
      applyAutocomms(EVENT_SAFESTATEAGAIN, NULL, NULL, false, curBook);
   } else
      lo("SafeState: back to waiting, not triggering SafeStateAgain");
}

//Return true if there is any typeahead, pending operator or command.
pub int
work_pending(void) {
   return op_pending() || !isSafeNow();
}

//Main loop: Execute Normal mode commands until exiting Eegl.
//Also used to handle commands in the command-line portal, until the portal is closed.
//Also used to handle ":visual" command after ":global": execute Normal mode commands.
pub void
mainLoop(Boole inCommPort) {  //true when working in the command-line window
   Operator oper;      //operator arguments
   Operator* operPrev = currOperatorG; //operator arguments
   currOperatorG = &oper;

   doClearOpArg(OUT &oper);
   while (!inCommPort || commPortResultG == 0) {
      if (stuff_empty()) {
         did_check_timestamps = false;
         if (need_check_timestamps)
            check_timestamps(false);
         if (need_wait_return)   //if wait_return() still needed ...
            wait_return(false);   //... call it now
      }

      //Reset "gotInterruptG" now that we got back to the main loop.  Except when
      //inside a ":g/pat/comm" command, then the "gotInterruptG" needs to abort the ":g" command.
      //For ":g/pat/vi" we reset "gotInterruptG" when used once.  When used
      //a second time we go back to Ex mode and abort the ":g" command.
      if (gotInterruptG) {
         if (!quitMoreG) {
            (void)vgetc();      //flush all buffers
         }
         gotInterruptG = false;
      }

      //At the toplevel there is no exception handling.  Discard any that
      //may be hanging around (e.g. from "interrupt" at the debug prompt).
      if (did_throw && !ex_normal_busy)
         discard_current_exception();

      msg_scroll = false;
      quitMoreG = false;

      //it's not safe unless may_trigger_safestate_main() is called
      wasSafeP = false;

      //If skip redraw is set (for ":" in wait_return()), don't redraw now.
      //If there is nothing in the stuff_buffer or do_redraw is true, update cursor and redraw.
      if (skip_redraw) {
         skip_redraw = false;
         setcursor();
         cursor_on();
      } ei (do_redraw || stuff_empty()) {
         if (!finish_op && popup_visible
               && !EQUAL_POS(last_cursormoved, curPor->cursor)) {
            if (popup_visible)
               popup_check_cursor_pos();
            last_cursormoved = curPor->cursor;
         }

         //Ensure curPor->topLine and curPor->leftCol are up to date before triggering a 
         //WinScrolled autocommand.
         update_topline();
         validate_cursor();

         if (!finish_op)
            may_trigger_win_scrolled_resized();

         //If nothing is pending and we are going to wait for the user to
         //type a character, trigger SafeState.
         may_trigger_safestate(!op_pending() && restart_edit == 0);

         //Updating diffs from changed() does not always work properly,
         //esp. updating folds.  Do an update just before redrawing if needed.
         if (curtab->diff_update || curtab->diff_invalid) {
            c_diffupdate(NULL);
            curtab->diff_update = false;
         }

         //Scroll-binding for diff mode may have been postponed until
         //here.  Avoids doing it for every change.
         if (diff_need_scrollbind) {
            check_scrollbind((LineNr)0, 0L);
            diff_need_scrollbind = false;
         }
         //Include a closed fold completely in the Visual area.
         foldAdjustVisual();
         //When 'foldclose' is set, apply 'foldlevel' to folds that don't contain the cursor.
         //When 'foldopen' is "all", open the fold(s) under the cursor.
         //This may mark the window for redrawing.
         if (hasAnyFolding(curPor) && !char_avail()) {
            foldCheckClose();
            if (p_fdo & FDO_ALL)
               foldOpenCursor();
         }

         //Before redrawing, make sure topLine is correct, and leftCol
         //if lines don't wrap, and skipCol if lines wrap.
         update_topline();
         validate_cursor();

         if (VIsual_active)
            drawUpdateCurBook(UPD_INVERTED); //update inverted part
         ei (mustRedrawG) {
            drawUpdateScreen(0);
         } ei (redrawCommlineG || mustClearCommlineG || redrawModeG)
            showmode();
         redraw_statuslines();
         curBook->lastUsed = eeTime();
         //display message after redraw
         if (msgAfterRedrawG) {
            CS p = copyStr(msgAfterRedrawG);
            //msg_start() will set msgAfterRedrawG to NULL, make a copy first. Don't reset 
            //msgAfterRedrawG, msgDeco_keep() uses it to check for duplicates. Never append this 
            //message to history.
            msg_hist_off = true;
            msgDeco(p, decoAfterRedrawG);
            msg_hist_off = false;
            eeglFree(p);
         }
         if (needFileinfoG) {     //show file info after redraw
            fileinfo(false, true, false);
            needFileinfoG = false;
         }

         emsg_on_display = false;   //can delete error message now
         anyEmsgG = false;
         msg_didany = false;      //reset lines_left in msg_start()
         may_clear_sb_text();   //clear scroll-back text on next msg
         showruler(false);

         setcursor();
         cursor_on();

         do_redraw = false;

         //Now that we have drawn the first screen all the startup stuff
         //has been done, close any file for startup messages.
         if (time_fd != NULL) {
            TIME_MSG("first screen update");
            TIME_MSG("--- EEGL STARTED ---");
            fclose(time_fd);
            time_fd = NULL;
         }
         //After the first screen update may start triggering WinScrolled
         //autocmd events.  Store all the scroll positions and sizes now.
         may_make_initial_scroll_size_snapshot();
      }

      //May request the keyboard protocol state now.
      may_send_t_RK();

      //Update cursWant if setCursWant has been set.
      //Postponed until here to avoid computing virtCol too often.
      update_curswant();

      //May perform garbage collection when waiting for a character, but
      //only at the very toplevel. Otherwise we may be using a List or Dict internally somewhere.
      //"may_garbage_collect" is reset in vgetc() which is invoked through normalAction().
      may_garbage_collect = (!inCommPort);
      //get and execute a normal mode command.
      if (term_use_loop()
          && oper.opTy == OP_NOP && oper.regname == ZERO
          && !VIsual_active
          && !skip_term_loop
      ){
         //If terminal_loop() returns OK we got a key that is handled in Normal mode.  With FAIL 
         //we first need to position the cursor and the screen needs to be redrawn.
         if (terminal_loop(true) == OK) {
            normalAction(OUT &oper);
         }
      } else {
         skip_term_loop = false;
         normalAction(&oper);
      }
   }

   currOperatorG = operPrev;
}

//Exit properly. This is the only way to exit Eegl after startup has succeeded. We are certain 
//to exit here, no way to abort it.
pub void
exitEegl(int exitval) {
   isExitingG = true;
   lo("Exiting...");

   //Position the cursor on the last screen line, below all the text
   if (!is_not_a_term_or_gui())
      windgoto((int)visibleRowsG - 1, 0);

   //Invoked all deferred functions in the function stack.
   invoke_all_defer();

   //Optionally print hashtable efficiency.
   hash_debug_results();

   if (v_dying <= 1) {
      Portal      *wp;
      int      unblock = 0;

      //Trigger BufWinLeave for all portals, but only once per buffer.
      Tab* next_tp;
      for (Tab* tp = firstTabG; tp; tp = next_tp) {
         next_tp = tp->next;
         FOR_ALL_PORTALS_IN_TAB(tp, wp) {
            if (wp->book == NULL || !bookIsValid(wp->book))
               //Autocmd must have close the buffer already, skip.
               continue;
            Book* book = wp->book;
            if (CHANGEDTICK(book) != -1) {
               BookRef bookRef;

               bookStoreInRef(OUT &bookRef, book);
               applyAutocomms(EVENT_BUFWINLEAVE, book->currFileName, book->currFileName, false, book);
               if (bookRefValid(&bookRef))
                  CHANGEDTICK(book) = -1;  //note we did it already

               //start all over, autocommands may mess up the lists
               next_tp = firstTabG;
               break;
            }
         }
      }

      Book* book;
      //Trigger BufUnload for loaded books
      FOR_ALL_BOOKS(book) {
         if (book->mem.mfile) {
            BookRef bookRef;
            bookStoreInRef(OUT &bookRef, book);
            applyAutocomms(EVENT_BUFUNLOAD, book->currFileName, book->currFileName, false, book);
            if (!bookRefValid(&bookRef))
               //autocmd deleted the book
               break;
         }
      }

      //deathtrap() blocks autocommands, but we do want to trigger EeglLeavePre.
      if (areAutocommsBlocked()) {
         unblock_autocmds();
         ++unblock;
      }
      applyAutocomms(EVENT_EEGLLEAVEPRE, NULL, NULL, false, curBook);
      if (unblock)
         block_autocmds();
   }

   if (
#ifdef EXITFREE
       entered_free_all_mem == false &&
#endif
         p_eeglinfo
   )
      //Write out the registers, history, marks etc, to the eeglinfo file
      write_eeglinfo(NULL, false);

   if (v_dying <= 1) {
      int unblock = 0;

      //deathtrap() blocks autocommands, but we do want to trigger EeglLeave.
      if (areAutocommsBlocked()) {
          unblock_autocmds();
          ++unblock;
      }
      applyAutocomms(EVENT_EEGLLEAVE, NULL, NULL, false, curBook);
      if (unblock)
         block_autocmds();
   }

   if (anyEmsgG) {
      //give the user a chance to read the (error) message
      no_wait_return = false;
      wait_return(false);
   }

   //Position the cursor again, the autocommands may have moved it
   if (!is_not_a_term_or_gui())
      windgoto((int)visibleRowsG - 1, 0);

   job_stop_on_exit();
   cs_end();
   if (garbage_collect_at_exit)
      garbage_collect(false);

   mch_exit(exitval);
}

//Get the name of the display, before gui_prepare() removes it from
//argv[].  Used for the xterm-clipboard display.
//
//Also find the --server... arguments and --socketid and --windowid
private void
earlyArgScan(MainParams* par) {
   int      argc = par->argc;
   char   **argv = par->argv;
   int      i;

   for (i = 1; i < argc; i++) {
      if (STRCMP(argv[i], "--") == 0)
          break;
   }
}

//Get an (optional) count for a Eegl argument.
private int
getNumericArg(
   CS p,       //pointer to argument
   int* idx,       //index in argument, is incremented
   int def       //default value
){
   if (eeIsDigit(p[*idx])) {
      def = atoi((char *)&(p[*idx]));
      while (eeIsDigit(p[*idx]))
         *idx = *idx + 1;
   }
   return def;
}

//Check for: [eegl|view][diff]  (sort of)
//If the next characters are "view" we start in readonly mode.
//If the next characters are "diff" or "eegldiff" we start in diff mode.
private void
parseCommandName(MainParams* par) {
   CS initstr;

   initstr = fiGetShortFiName((CS)par->argv[0]);

   set_progpath((CS)par->argv[0]);

   if (STRNICMP(initstr, "view", 4) == 0) {
      optSetByName(S"modifiable", optBoole(false), SET_GLOBAL);
      curBook->o.modifiable = false;
      swapEnabledG = true;         //don't update very often
      initstr += 4;
   } ei (STRNICMP(initstr, "eegl", 3) == 0)
      initstr += 3;

   //Catch "eegldiff" and "viewdiff".
   if (caseInsensitiveCompare(initstr, "diff") == 0) {
      par->diff_mode = true;
   }
}

//{{{ Scan the command line arguments.
private void
scanCommandLineArgs(MainParams *par) {
   int argc = par->argc;
   char** argv = par->argv;
   int argv_idx;      //index in argv[n][]
   int had_minmin = false;   //found "--" argument
   int want_argument;      //option argument with argument
   int c;
   CS text = NULL;

   --argc;
   ++argv;
   argv_idx = 1;       //active option letter is argv[0][argv_idx]
   while (argc > 0) {
      //"+" or "+{number}" or "+/{pat}" or "+{command}" argument.
      if (argv[0][0] == '+' && !had_minmin) {
         if (par->n_commands >= MAX_ARG_CMDS)
            mainerr(ME_EXTRA_CMD, NULL);
         argv_idx = -1;       //skip to next argument
         if (argv[0][1] == ZERO)
            par->commands[par->n_commands++] = (CS)"$";
         else
            par->commands[par->n_commands++] = (CS)&(argv[0][1]);
      }
      //Optional argument.
      ei (argv[0][0] == '-' && !had_minmin) {
         want_argument = false;
         c = argv[0][argv_idx++];
         switch (c) {
         case ZERO:      //"eegl -"  read from stdin. "ex -" silent mode
            if (par->edit_type != EDIT_NONE)
               mainerr(ME_TOO_MANY_ARGS, (CS)argv[0]);
            par->edit_type = EDIT_STDIN;
            read_cmd_fd = 2;   //read from stderr instead of stdin
            argv_idx = -1;      //skip to next argument
            break;

         case '-': 
            //"--" don't take any more option arguments
            //"--help" give help message
            //"--version" give version message
            //"--clean" clean context
            //"--literal" take files literally
            //"--startuptime fname" write timing info
            //"--log fname" start logging early
            //"--nofork" don't fork
            //"--not-a-term" don't warn for not a term
            //"--gui-dialog-file fname" write dialog text
            //"--ttyfail" exit if not a term
            //"--noplugin[s]" skip plugins
            //"--comm <command>" execute command before init.vim
            if (caseInsensitiveCompare(argv[0] + argv_idx, "help") == 0)
                usage();
            ei (caseInsensitiveCompare(argv[0] + argv_idx, "version") == 0) {
                visibleColsG = 80;
                info_message = true; //use mch_msg(), not mch_errmsg()
                list_version();
                msg_putchar('\n');
                msg_didout = false;
                mch_exit(0);
            } ei (STRNICMP(argv[0] + argv_idx, "clean", 5) == 0) {
                par->altInitFile = (CS)"DEFAULTS";
                par->clean = true;
                optChangeAndReportError(S"eeglinfofile", optStr("NONE"), SET_GLOBAL);
            } ei (STRNICMP(argv[0] + argv_idx, "literal", 7) == 0) {
            } ei (STRNICMP(argv[0] + argv_idx, "nofork", 6) == 0) {
            } ei (STRNICMP(argv[0] + argv_idx, "not-a-term", 10) == 0)
                par->not_a_term = true;
            ei (STRNICMP(argv[0] + argv_idx, "gui-dialog-file", 15) == 0) {
                want_argument = true;
                argv_idx += 15;
            } ei (STRNICMP(argv[0] + argv_idx, "ttyfail", 7) == 0)
                par->tty_fail = true;
            ei (STRNICMP(argv[0] + argv_idx, "comm", 3) == 0) {
                want_argument = true;
                argv_idx += 3;
            } ei (STRNICMP(argv[0] + argv_idx, "startuptime", 11) == 0) {
                want_argument = true;
                argv_idx += 11;
            } ei (STRNICMP(argv[0] + argv_idx, "log", 3) == 0) {
                want_argument = true;
                argv_idx += 3;
            } ei (STRNICMP(argv[0] + argv_idx, "serverlist", 10) == 0)
                ; //already processed -- no arg
            ei (STRNICMP(argv[0] + argv_idx, "servername", 10) == 0
                   || STRNICMP(argv[0] + argv_idx, "serversend", 10) == 0
            ){
               //already processed -- snatch the following arg
               if (argc > 1) {
                  --argc;
                  ++argv;
               }
            } else {
               if (argv[0][argv_idx])
                  mainerr(ME_UNKNOWN_OPTION, (CS)argv[0]);
               had_minmin = true;
            }
            if (!want_argument)
               argv_idx = -1;   //skip to next argument
            break;

         case 'b':      //"-b" binary mode. binary file I/O
            OptionChange cha = (OptionChange){.newVal = optBoole(true), .setScope = SET_LOCAL,
               .ref = (OptionRef){.tag = OPTION_BOOLE, .boole = &curBook->o.binary}
            };
            optSetBinary(&cha);
            break;

         case 'h':      //"-h" give help message
            usage();
            break;

         case 'M':      //"-M"  no changes or writing of files
            //FALLTHROUGH
         case 'm':
            p_modifiable = false;
            break;

         case 'n':      //"-n" no swap file
            par->no_swap_file = true;
            break;

         case 'p':      //"-p[N]" open N tabs
            //default is 0: open portal for each file
            par->portalCount = getNumericArg((CS)argv[0], &argv_idx, 0);
            par->portalLayout = WIN_TABS;
            break;

         case 'o':      //"-o[N]" open N horizontal split windows
            //default is 0: open window for each file
            par->portalCount = getNumericArg((CS)argv[0], &argv_idx, 0);
            par->portalLayout = WIN_HOR;
            break;

         case 'O':   //"-O[N]" open N vertical split windows
            //default is 0: open window for each file
            par->portalCount = getNumericArg((CS)argv[0], &argv_idx, 0);
            par->portalLayout = WIN_VER;
            break;

         case 'q':      //"-q" QuickFix mode
            if (par->edit_type != EDIT_NONE) 
               mainerr(ME_TOO_MANY_ARGS, (CS)argv[0]);
            par->edit_type = EDIT_QF;
            if (argv[0][argv_idx]) {     //"-q{errorfile}"
               par->use_ef = (CS)argv[0] + argv_idx;
               argv_idx = -1;
            } ei (argc > 1)      //"-q {errorfile}"
               want_argument = true;
            break;

         case 'R':      //"-R" readonly mode, equivalent to "-m" or "-M"
            p_modifiable = false;
            break;

         case 'r':      //"-r" recovery mode
         case 'L':      //"-L" recovery mode
            recoveryModeG = 1;
            break;

         case 's':
            //"-s {scriptin}" read from script file
            want_argument = true;
            break;

         case 't':      //"-t {tag}" or "-t{tag}" jump to tag
            if (par->edit_type != EDIT_NONE)
               mainerr(ME_TOO_MANY_ARGS, (CS)argv[0]);
            par->edit_type = EDIT_TAG;
            if (argv[0][argv_idx]) {     //"-t{tag}"
               par->tagname = (CS)argv[0] + argv_idx;
               argv_idx = -1;
            } else            //"-t {tag}"
                want_argument = true;
            break;

         case 'D':      //"-D"      Debugging
            par->use_debug_break_level = 9999;
            break;
         case 'd':      //"-d"      'diff'
            par->diff_mode = true;
            break;
         case 'V':      //"-V{N}"   Verbose level
            //default is 10: a little bit verbose
            p_verbose = getNumericArg((CS)argv[0], &argv_idx, 10);
            if (argv[0][argv_idx] != ZERO) {
               optChangeAndReportError(
                  S"verbosefile", optStr((CS)argv[0] + argv_idx), SET_GLOBAL 
               );
               argv_idx = (int)STRLEN(argv[0]);
            }
            break;


         case 'w': //"-w {scriptout}"   write to script
            want_argument = true;
            break;

         case 'c':      //"-c{command}" or "-c {command}" execute command
            if (argv[0][argv_idx] != ZERO) {
               if (par->n_commands >= MAX_ARG_CMDS)
                  mainerr(ME_EXTRA_CMD, NULL);
               par->commands[par->n_commands++] = (CS)argv[0] + argv_idx;
               argv_idx = -1;
               break;
            }
            //FALLTHROUGH
         case 'P':      //"-P {dir}" project mode at dir
         case 'S':      //"-S {file}" execute Vimscript
         case 'i':      //"-i {eeglinfo}" use for eeglinfo
         case 'T':      //"-T {terminal}" terminal name
         case 'u':      //"-u {vimrc}" Eegl inits file
         case 'W':      //"-W {scriptout}" overwrite
            want_argument = true;
            break;

         default:
            mainerr(ME_UNKNOWN_OPTION, (CS)argv[0]);
         }

         //Handle option arguments with argument.
         if (want_argument) {
            //Check for garbage immediately after the option letter.
            if (argv[0][argv_idx] != ZERO)
                mainerr(ME_GARBAGE, (CS)argv[0]);

            --argc;
            if (argc < 1 && c != 'S')  //-S has an optional argument
                mainerr_arg_missing((CS)argv[0]);
            ++argv;
            argv_idx = -1;

            switch (c) {
            case 'c':   //"-c {command}" execute command
            case 'S':   //"-S {file}" execute Vim script
               if (par->n_commands >= MAX_ARG_CMDS)
                  mainerr(ME_EXTRA_CMD, NULL);
               if (c == 'S') {
                  Arr(char) fName;

                  if (argc < 1)
                     //"-S" without argument: use default session file name.
                     fName = SESSION_FILE;
                  ei (argv[0][0] == '-') {
                     //"-S" followed by another option: use default session file name.
                     fName = SESSION_FILE;
                     ++argc;
                     --argv;
                  } else
                     fName = argv[0];
                  text = alloc(STRLEN(fName) + 4);
                  sprintf((char *)text, "so %s", fName);
                  par->cmds_tofree[par->n_commands] = true;
                  par->commands[par->n_commands++] = text;
               } else
                  par->commands[par->n_commands++] = (CS)argv[0];
               break;
               
            case 'P':   //"-P {dir}" project mode at dir
               projectDirG = (CS)argv[0];
               break;

            case '-':
               if (argv[-1][2] == 'c') {
                  //"--comm {command}" execute command
                  if (par->n_pre_commands >= MAX_ARG_CMDS)
                     mainerr(ME_EXTRA_CMD, NULL);
                  par->pre_commands[par->n_pre_commands++] = (CS)argv[0];
               }

               //"--startuptime <file>" already handled
               //"--log <file>" already handled
               break;

            case 'q':   //"-q {errorfile}" QuickFix mode
               par->use_ef = (CS)argv[0];
               break;

            case 'i':   //"-i {eeglinfo}" use for eeglinfo
               optChangeAndReportError(S"eeglinfofile", optStr(argv[0]), SET_GLOBAL);
               break;

            case 's':   //"-s {scriptin}" read from script file
               if (scriptin[0]) {
scripterror:
                  mch_errmsg(_("Attempt to open script file again: \""));
                  mch_errmsg(argv[-1]);
                  mch_errmsg(" ");
                  mch_errmsg(argv[0]);
                  mch_errmsg("\"\n");
                  mch_exit(2);
               } 
               if ((scriptin[0] = fopen(argv[0], READBIN)) == NULL) {
                  mch_errmsg(_("Cannot open for reading: \""));
                  mch_errmsg(argv[0]);
                  mch_errmsg("\"\n");
                  mch_exit(2);
               }
               if (save_typebuf() == FAIL)
                  mch_exit(2);   //out of memory
               break;

            case 't':   //"-t {tag}"
                par->tagname = (CS)argv[0];
                break;

            case 'T':   //"-T {terminal}" terminal name
               //The -T term argument is always available and when
               //HAVE_TERMLIB is supported it overrides the environment variable TERM.
               par->term = (CS)argv[0];
               break;

            case 'u':   //"-u {vimrc}" Eegl inits file
                par->altInitFile = (CS)argv[0];
                break;

            case 'w': //"-w {scriptout}" append to script file
            case 'W': //"-W {scriptout}" overwrite script file
               if (scriptout)
                  goto scripterror;
               if ((scriptout = fopen(argv[0], c == 'w' ? APPENDBIN : WRITEBIN)) == NULL) {
                  mch_errmsg(_("Cannot open for script output: \""));
                  mch_errmsg(argv[0]);
                  mch_errmsg("\"\n");
                  mch_exit(2);
               }
               break;

            }
         }
      } else {
      //File name argument.
         argv_idx = -1;       //skip to next argument

         //Check for only one type of editing.
         if (par->edit_type != EDIT_NONE && par->edit_type != EDIT_FILE)
            mainerr(ME_TOO_MANY_ARGS, (CS)argv[0]);
         par->edit_type = EDIT_FILE;

         //Add the file to the global argument list.
         if (ga_grow(&argListG.al_ga, 1) == FAIL)
            mch_exit(2);
         text = copyStr((CS)argv[0]); 
         if (
            par->diff_mode && mch_isdir(text) 
            && GARGCOUNT > 0 
            && !mch_isdir(alist_name(&GARGLIST[0]))
         ) {
            CS concattedFnames = 
               concat_fnames(text, fiGetShortFiName(alist_name(&GARGLIST[0])), true);
            if (concattedFnames) {
               eeglFree(text);
               text = concattedFnames;
            }
         }

         arglistIngest(&argListG, text, 2); //add buffer number now and use curBook
      }

      //If there are no more letters after the current "-", go to next
      //argument.  argv_idx is set to -1 when the current argument is to be skipped.
      if (argv_idx <= 0 || argv[0][argv_idx] == ZERO) {
          --argc;
          ++argv;
          argv_idx = 1;
      }
   }

   //If there is a "+123" or "-c" command, set v:swapcommand to the first one.
   if (par->n_commands > 0) {
      text = alloc(STRLEN(par->commands[0]) + 3);
      sprintf((char *)text, ":%s\r", par->commands[0]);
      eeglFree(text);
   }
}

//}}}

//Print a warning if stdout is not a terminal.
private void
check_tty(MainParams* par) {
   int input_isatty;      //is active input a terminal?

   input_isatty = mch_input_isatty();
   if (par->want_full_screen && (!stdout_isatty || !input_isatty) && !par->not_a_term) {
      if (!stdout_isatty)
         mch_errmsg(_("Eegl: Warning: Output is not to a terminal\n"));
      if (!input_isatty)
         mch_errmsg(_("Eegl: Warning: Input is not from a terminal\n"));
      termOutFlush();
      if (par->tty_fail && (!stdout_isatty || !input_isatty))
         exit(1);
      if (scriptin[0] == NULL)
         ui_delay(2005L, true);
      TIME_MSG("Warning delay");
   }
}

//Read text from stdin.
private void
readStdin(void) {
   //When getting the ATTENTION prompt here, use a dialog
   swap_exists_action = SEA_DIALOG;

   no_wait_return = true;
   int i = msg_didany;
   bookSetBooklisted(true);

   //Create memfile and read from stdin.
   (void)bookOpenFromInvo(true, NULL, 0);

   no_wait_return = false;
   msg_didany = i;
   TIME_MSG("reading stdin");

   check_swap_exists_action();

   //Dup stdin from stderr to read commands from, so that shell commands work.
   //TODO: why is this needed, even though readfile() has done this?
   close(0);
   (void)dup(2);
}

//Create the requested number of portals and edit books in them.
//Also do recovery if "recoveryModeG" set.
private void
createPortals(MainParams* par) {
   int dorewind;

   //Create the number of portals that was requested.
   if (par->portalCount == UNT)   //was not set
      par->portalCount = 1;
   if (par->portalCount == 0)
      par->portalCount = GARGCOUNT;
   if (par->portalCount > 1) {
      //Don't change the portals if there was a command in .vimrc that already split some portals
      if (par->portalLayout == 0)
          par->portalLayout = WIN_HOR;
      ei (par->portalLayout == WIN_TABS) {
          par->portalCount = portMakeTabs(par->portalCount);
          TIME_MSG("making tabs");
      } ei (!firstPor->next) {
          par->portalCount = portMakePortals(par->portalCount, par->portalLayout == WIN_VER);
          TIME_MSG("making portals");
      } else
         par->portalCount = portCount();
   } else
      par->portalCount = 1;

   if (recoveryModeG) {         //do recover
      msg_scroll = true;      //scroll message up
      ml_recover(true);
      if (bookNoMemfile(curBook)) //failed
         exitEegl(1);
   } else {
      //Open a buffer for portals that don't have one yet. Commands in the .vimrc might have loaded 
      //a file or split the window. Watch out for autocommands that delete a portal. Don't execute 
      //Win/Buf Enter/Leave autocommands here
      ++autocmd_no_enter;
      ++autocmd_no_leave;
      dorewind = true;
      for (int done = 0; done < 1000; done++) {
         if (dorewind) {
            if (par->portalLayout == WIN_TABS)
               gotoTabById(1);
            else
               curPor = firstPor;
         } ei (par->portalLayout == WIN_TABS) {
            if (!curtab->next)
               break;
            gotoTabById(0);
         } else {
            if (!curPor->next)
               break;
            curPor = curPor->next;
         }
         dorewind = false;
         curBook = curPor->book;
         if (!curBook->mem.mfile) {
            if (foldLevelStart >= 0)
               curPor->o.foldLevel = foldLevelStart;
            //When getting the ATTENTION prompt here, use a dialog
            swap_exists_action = SEA_DIALOG;

            bookSetBooklisted(true);

            //create memfile, read file
            (void)bookOpenFromInvo(false, NULL, 0);

            if (swap_exists_action == SEA_QUIT) {
               if (gotInterruptG || onlyOnePortal()) {
                  //abort selected or quit and only one portal
                  anyEmsgG = false;   //avoid hit-enter prompt
                  exitEegl(1);
               }
               //We can't close the window, it would disturb what happens next. Clear the file 
               //name and set the arg index to -1 to delete it later.
               setfname(curBook, NULL, NULL, false);
               curPor->argListInd = -1;
               swap_exists_action = SEA_NONE;
            } else
               handle_swap_exists(NULL);
            dorewind = true;      //start again
         }
         ui_breakcheck();
         if (gotInterruptG) {
            (void)vgetc();   //only break the file loading, not the rest
            break;
         }
      }
      if (par->portalLayout == WIN_TABS)
         gotoTabById(1);
      else
         curPor = firstPor;
      curBook = curPor->book;
      --autocmd_no_enter;
      --autocmd_no_leave;
   }
}

//If opened more than one portal, start editing files in the other portals. portMakePortals() has 
//already opened the portals.
private void
editBuffers(MainParams* par, CS cwd) {        //current working dir
   int arg_idx;      //index in argument list
   int advance = true;

   //Don't execute Win/Buf Enter/Leave autocommands here
   ++autocmd_no_enter;
   ++autocmd_no_leave;

   //When argListInd is -1 remove the window (see createPortals()).
   if (curPor->argListInd == -1) {
      closePortal(curPor, true);
      advance = false;
   }

   arg_idx = 1;
   for (Unt i = 1; i < par->portalCount; ++i) {
      if (cwd)
         mch_chdir(cwd);
      //When argListInd is -1 remove the window (see createPortals()).
      if (curPor->argListInd == -1) {
         ++arg_idx;
         closePortal(curPor, true);
         advance = false;
         continue;
      }
      if (advance) {
         if (par->portalLayout == WIN_TABS) {
            if (!curtab->next)   //just checking
               break;
            gotoTabById(0);
         } else {
            if (!curPor->next)   //just checking
               break;
            enterPortal(curPor->next, false);
         }
      }
      advance = true;

      //Only open the file if there is no file in this window yet (that can
      //happen when .vimrc contains ":sall").
      if (curBook == firstPor->book || curBook->fullFileName == NULL) {
         curPor->argListInd = arg_idx;
         //Edit file from arg list, if there is one.  When "Quit" selected
         //at the ATTENTION prompt close the window.
         swap_exists_did_quit = false;
         (void)startEditingFile(0, 
            arg_idx < GARGCOUNT ? alist_name(&GARGLIST[arg_idx]) : NULL,
            NULL, NULL, ECMD_LASTL, ECMD_HIDE, curPor
         );
         if (swap_exists_did_quit) {
            //abort or quit selected
            if (gotInterruptG || onlyOnePortal()) {
               //abort selected and only one portal
               anyEmsgG = false;  //avoid hit-enter prompt
               exitEegl(1);
            }
            closePortal(curPor, true);
            advance = false;
         }
         if (arg_idx == GARGCOUNT - 1)
            arg_had_last = true;
         ++arg_idx;
      }
      ui_breakcheck();
      if (gotInterruptG) {
         (void)vgetc();   //only break the file loading, not the rest
         break;
      }
   }

   if (par->portalLayout == WIN_TABS)
      gotoTabById(1);
   --autocmd_no_enter;

   //make the first portal the current one
   Portal* po = firstPor;
   //Avoid making a preview portal the current one.
   while (po->isPreview) {
      po = po->next;
      if (!po) {
         po = firstPor;
         break;
      }
   }
   enterPortal(po, false);

   --autocmd_no_leave;
   TIME_MSG("editing files in windows");
   if (par->portalCount > 1 && par->portalLayout != WIN_TABS)
      portEqualizeHeight(curPor, false, EAD_BOTH);   //adjust heights
}

//Execute the commands from --comm arguments "comms[cnt]".
private void
executePreCommands(MainParams* par) {
   Arr(CS) comms = par->pre_commands;
   int cnt = par->n_pre_commands;
   int i;
   ESTACK_CHECK_DECLARATION;

   if (cnt <= 0)
      return;

   curPor->cursor.lnum = 0; //just in case..
   estack_push(ETYPE_ARGS, (CS)_("pre-vimrc command line"), 0);
   ESTACK_CHECK_SETUP;
   scriptPosG.sid = SID_CMDARG;
   for (i = 0; i < cnt; ++i) {
      executeCommLine(comms[i]);
   } 
   ESTACK_CHECK_NOW;
   estack_pop();
   scriptPosG.sid = 0;
   TIME_MSG("--comm commands");
}

//Execute "+", "-c" and "-S" arguments.
private void
exeCommands(MainParams* par) {
   ESTACK_CHECK_DECLARATION;

   //We start commands on line 0, make "eegl +/pat file" match a
   //pattern on line 1.  But don't move the cursor when an autocommand with g`" was used.
   msg_scroll = true;
   if (par->tagname == NULL && curPor->cursor.lnum <= 1)
      curPor->cursor.lnum = 0;
   estack_push(ETYPE_ARGS, S"command line", 0);
   ESTACK_CHECK_SETUP;
   scriptPosG.sid = SID_CARG;
   scriptPosG.seq = 0;
   for (int i = 0; i < par->n_commands; ++i) {
      executeCommLine(par->commands[i]);
      if (par->cmds_tofree[i])
          eeglFree(par->commands[i]);
   }
   ESTACK_CHECK_NOW;
   estack_pop();
   scriptPosG.sid = 0;
   if (curPor->cursor.lnum == 0)
      curPor->cursor.lnum = 1;

   msg_scroll = false;

   //When started with "-q errorfile" jump to first error again.
   if (par->edit_type == EDIT_QF)
      llJump(NULL, 0, 0, false);
   TIME_MSG("executing command arguments");
}

//Source startup scripts.
private void
sourceStartupScripts(MainParams* par) {
   //If -u argument given, use only the initializations from that file and nothing else.
   if (par->altInitFile) {
      if (STRCMP(par->altInitFile, "DEFAULTS") == 0) {
         if (scriptRunFile((CS)EE_DEFAULTS_FILE, NULL) != OK)
            emsg(_(e_failed_to_source_defaults));
      } ei (STRCMP(par->altInitFile, "NONE") == 0 || STRCMP(par->altInitFile, "NORC") == 0) {
      } else {
         if (scriptRunFile(par->altInitFile, NULL) != OK)
            showErrFmtMsg(_(e_cannot_read_from_str_2), par->altInitFile);
      }
   } ei (!silentModeG) {
      //Get system wide defaults, if the file name is defined.
      (void)scriptRunFile(INIT_FILE, NULL);
      //(void)scriptRunFile(FILETYPES_FILE, NULL);
   }
   TIME_MSG(S"sourcing init.vim file(s)");
}

//Give an error message main_errors["n"] and exit.
private void
mainerr(
   Unt n,   //one of the ME_ defines
   NULLABLE CS str   //extra argument
){
   reset_signals();      //kill us with CTRL-C here, if you like

   mch_errmsg(longVersion);
   mch_errmsg("\n");
   mch_errmsg(_(main_errors[n]));
   if (str != NULL) {
      mch_errmsg(": \"");
      mch_errmsg((char *)str);
      mch_errmsg("\"");
   }
   mch_errmsg(_("\nMore info with: \"eegl -h\"\n"));

   mch_exit(1);
}

pub void
mainerr_arg_missing(CS str) {
   mainerr(ME_ARG_MISSING, str);
}

//print a message with three spaces prepended and '\n' appended.
private void
main_msg(CS s) {
   mch_msg("   ");
   mch_msg(s);
   mch_msg("\n");
}

pub CS
mainProgramVersion() {
   return programVersion;
}

//Print messages for "eegl -h" or "eegl --help" and exit.
private void
usage(void) {
   int      i;
   static CS use[] = {
      N_("[file ..]       edit specified file(s)"),
      N_("-               read text from stdin"),
      N_("-t tag          edit file where tag is defined"),
      N_("-q [errorfile]  edit file with first error")
   };

   reset_signals();      //kill us with CTRL-C here, if you like

   mch_msg(longVersion);
   mch_msg(_("\n\nUsage:"));
   for (i = 0; ; ++i) {
      mch_msg(_(" eegl [arguments] "));
      mch_msg(_(use[i]));
      if (i == ARRAY_LENGTH(use) - 1)
         break;
      mch_msg(_("\n   or:"));
   }

   mch_msg(_("\n\nArguments:\n"));
   main_msg(_("--\t\t\tOnly file names after this"));
   main_msg(_("-v\t\t\tVi mode (like \"vi\")"));
   main_msg(_("-e\t\t\tEx mode (like \"ex\")"));
   main_msg(_("-E\t\t\tImproved Ex mode"));
   main_msg(_("-s\t\t\tSilent (batch) mode (only for \"ex\")"));
   main_msg(_("-d\t\t\tDiff mode (like \"eegldiff\")"));
   main_msg(_("-R\t\t\tReadonly mode (like \"view\")"));
   main_msg(_("-m\t\t\tModifications (writing files) not allowed"));
   main_msg(_("-M\t\t\tModifications in text not allowed"));
   main_msg(_("-b\t\t\tBinary mode"));
   main_msg(_("-l\t\t\tLisp mode"));
   main_msg(_("-C\t\t\tCompatible with Vi: 'compatible'"));
   main_msg(_("-N\t\t\tNot fully Vi compatible: 'nocompatible'"));
   main_msg(_("-V[N][fname]\t\tBe verbose [level N] [log messages to fname]"));
   main_msg(_("-D\t\t\tDebugging mode"));
   main_msg(_("-n\t\t\tNo swap file, use memory only"));
   main_msg(_("-r\t\t\tList swap files and exit"));
   main_msg(_("-r (with file name)\tRecover crashed session"));
   main_msg(_("-L\t\t\tSame as -r"));
   main_msg(_("-T <terminal>\tSet terminal type to <terminal>"));
   main_msg(_("--not-a-term\t\tSkip warning for input/output not being a terminal"));
   main_msg(_("--ttyfail\t\tExit if input or output is not a terminal"));
   main_msg(_("-u <vimrc>\t\tUse <vimrc> instead of any .vimrc"));
   main_msg(_("--noplugin\t\tDon't load plugin scripts"));
   main_msg(_("-p[N]\t\tOpen N tabs (default: one for each file)"));
   main_msg(_("-o[N]\t\tOpen N windows (default: one for each file)"));
   main_msg(_("-O[N]\t\tLike -o but split vertically"));
   main_msg(_("+\t\t\tStart at end of file"));
   main_msg(_("+<lnum>\t\tStart at line <lnum>"));
   main_msg(_("--comm <command>\tExecute <command> before loading any vimrc file"));
   main_msg(_("-c <command>\t\tExecute <command> after loading the first file"));
   main_msg(_("-S <session>\t\tSource file <session> after loading the first file"));
   main_msg(_("-s <scriptin>\tRead Normal mode commands from file <scriptin>"));
   main_msg(_("-w <scriptout>\tAppend all typed commands to file <scriptout>"));
   main_msg(_("-W <scriptout>\tWrite all typed commands to file <scriptout>"));
   main_msg(_("--remote <files>\tEdit <files> in a Eegl server if possible"));
   main_msg(_("--remote-silent <files>  Same, don't complain if there is no server"));
   main_msg(_("--remote-wait <files>  As --remote but wait for files to have been edited"));
   main_msg(_("--remote-wait-silent <files>  Same, don't complain if there is no server"));
   main_msg(_("--remote-tab[-wait][-silent] <files>  As --remote but use tab per file"));
   main_msg(_("--remote-send <keys>\tSend <keys> to a Eegl server and exit"));
   main_msg(_("--remote-expr <expr>\tEvaluate <expr> in a Eegl server and print result"));
   main_msg(_("--serverlist\t\tList available Eegl server names and exit"));
   main_msg(_("--servername <name>\tSend to/become the Eegl server <name>"));
   main_msg(_("--startuptime <file>\tWrite startup timing messages to <file>"));
   main_msg(_("--log <file>\t\tStart logging to <file> early"));
   main_msg(_("-i <eeglinfo>\t\tUse <eeglinfo> instead of .eeglinfo"));
   main_msg(_("--clean\t\t'nocompatible', Eegl defaults, no plugins, no eeglinfo"));
   main_msg(_("-h  or  --help\tPrint Help (this message) and exit"));
   main_msg(_("--version\t\tPrint version information and exit"));

   mch_exit(0);
}

//Check the result of the ATTENTION dialog:
//When "Quit" selected, exit Eegl.
//When "Recover" selected, recover the file.
private void
check_swap_exists_action(void) {
   if (swap_exists_action == SEA_QUIT)
      exitEegl(1);
   handle_swap_exists(NULL);
}

pub void __attribute__((noinline))
__bp() { //breakpoints for debugger
   ;
}

private void
set_progpath(CS argv0) {
   CS val = argv0;
   Byte buf[MAXPATHL + 1];
   Byte linkBuf[MAXPATHL + 1];
   Long len = readlink("/proc/self/exe", OUT (char*)linkBuf, MAXPATHL);
   if (len > 0) {
      linkBuf[len] = ZERO;
      val = linkBuf;
   }

   if (strIsRelative(val) 
         && fiGetShortFiName(val) != val && eeFullFileName(val, OUT buf, MAXPATHL, true) != FAIL
   )
      val = buf;
}

//{{{signal handlers

//volatile because it is used in signal handler deathtrap().
private volatile SigAtomic deadlySignalP = 0;      //The signal we caught

typedef struct {
   int sig;   //Signal number, eg. SIGSEGV etc
   char* name;   //Signal name (not Byte!).
   char deadly;   //Catch as a deadly signal?
} SignalInfo;

private SignalInfo signalInfos[] = {
    {SIGHUP,       "HUP",   true},
    {SIGQUIT,       "QUIT",   true},
    {SIGILL,       "ILL",   true},
    {SIGTRAP,       "TRAP",   true},
    {SIGABRT,       "ABRT",   true},
    {SIGFPE,       "FPE",   true},
    {SIGBUS,       "BUS",   true},
    {SIGSEGV,       "SEGV",   true},
    {SIGSYS,       "SYS",   true},
    {SIGALRM,       "ALRM",   false},   //Perl's alarm() can trigger it
    {SIGTERM,       "TERM",   true},
    {SIGVTALRM,       "VTALRM",   true},
#if !defined(WE_ARE_PROFILING)
    //With profiling this makes Eegl exit. WE_ARE_PROFILING is defined in Makefile.
    {SIGPROF,       "PROF",   true},
#endif
    {SIGXCPU,       "XCPU",   true},
    {SIGXFSZ,       "XFSZ",   true},
    {SIGUSR1,       "USR1",   false},
    //Used for sysmouse handling
    {SIGUSR2,       "USR2",   true},
    {SIGPIPE,       "PIPE",   false},
    {-1,       "Unknown!", false}
};


//We need correct prototypes for a signal function, otherwise mean compilers
//will barf when the second argument to signal() is ``wrong''.
//Let me try it with a few tricky defines from my own osdef.h   (jw).
pub void
sig_winch(int) {
   //this is not required on all systems, but it doesn't hurt anybody
   motSignalHandler(SIGWINCH, sig_winch);
   doResizeG = true;
}

pub void
sig_tstp(int) {
   motSignalHandler(SIGTSTP, sig_tstp);
}

private void
catch_sigint(int) {
   //this is not required on all systems, but it doesn't hurt anybody
   motSignalHandler(SIGINT, catch_sigint);
   gotInterruptG = true;
}

private void
catch_sigusr1(int) {
   //this is not required on all systems, but it doesn't hurt anybody
   motSignalHandler(SIGUSR1, catch_sigusr1);
   got_sigusr1 = true;
}

private void
catch_sigpwr(int) {
   //this is not required on all systems, but it doesn't hurt anybody
   motSignalHandler(SIGPWR, catch_sigpwr);
   //I'm not sure we get the SIGPWR signal when the system is really going down or when the 
   //batteries are almost empty. Just preserve the swap files and don't exit, that can't do any 
   //harm.
   ml_sync_all(false, false);
}

//This function handles deadly signals.
//It tries to preserve any swap files and exit properly.
//(partly from Elvis).
//NOTE: Avoid unsafe functions, such as allocating memory, they can result in a deadlock.
private void
deathtrap(int sigarg) {
   static int entered = 0;  //count the number of times we got here.
                            //Note: when memory has been corrupted this may get an arbitrary 
                            //value!

   //While in mch_delay() we go to cooked mode to allow a CTRL-C to interrupt us. But in cooked 
   //mode we may also get SIGQUIT, e.g., when pressing CTRL-\, but we don't want Eegl to exit then.
   if ((inMchDelayP && sigarg == SIGQUIT) != 0)
      return;

   //When SIGHUP, SIGQUIT, etc. are blocked: postpone the effect and return here. This avoids that 
   //a non-reentrant function is interrupted, e.g., free(). Calling free() again may then cause a 
   //crash.
   if (entered == 0
       && ( sigarg == SIGHUP
         || sigarg == SIGQUIT
         || sigarg == SIGTERM
         || sigarg == SIGPWR
         || sigarg == SIGUSR1
         || sigarg == SIGUSR2
         )
          && !eeHandleSignal(sigarg)
   )
      return;

   //Remember how often we have been called.
   ++entered;

   //Executing autocommands is likely to use more stack space than we have
   //available in the signal stack.
   block_autocmds();

   v_dying = entered;

#if 0
   //This is for opening gdb the moment Eegl crashes.
   //You need to manually adjust the file name and Eegl executable name.
   //Suggested by SungHyun Nam.
   {
# define EE_GDB_FILE "/tmp/eegdb"
# define EE_NAME PREFIX "/bin/eegl"
   FILE *fp = fopen(VI_GDB_FILE, "w");
   if (fp) {
      fprintf(fp,
         "file %s\n"
         "attach %d\n"
         "set height 1000\n"
         "bt full\n"
         , EE_NAME, getpid());
      fclose(fp);
      system("xterm -e gdb -x "EE_GDB_FILE);
      unlink(EE_GDB_FILE);
   }
   }
#endif

   //try to find the name of this signal
   int i;
   for (i = 0; signalInfos[i].sig != -1; i++) {
      if (sigarg == signalInfos[i].sig)
         break;
   } 
   deadlySignalP = sigarg;

   fullScreenG = false; //don't write messages to the UI, it might be part of the problem...
   //If something goes wrong after entering here, we may get here again.
   //When this happens, give a message and try to exit nicely (resetting the terminal mode, etc)
   //When this happens twice, just exit, don't even try to give a message,
   //stack may be corrupt or something weird.
   //When this still happens again (or memory was corrupted in such a way
   //that "entered" was clobbered) use _exit(), don't try freeing resources.
   if (entered >= 3) {
      reset_signals();   //don't catch any signals anymore
      may_core_dump();
      if (entered >= 4)
         _exit(8);
      exit(7);
   }
   if (entered == 2) {
      //No translation, it may call malloc().
      OUT_STR("Eegl: Double signal, exiting\n");
      termOutFlush();
      exitEegl(1);
   }

   //No translation, it may call malloc().
   sprintf((char *)IObuff, "Eegl: Caught deadly signal %s\r\n", signalInfos[i].name);

   //Preserve files and exit. This sets the really_exiting flag to prevent calling free().
   preserve_exit();

   //NOTREACHED
}

//Invoked after receiving SIGCONT. We don't know what happened while
//sleeping, deal with part of that.
private void
after_sigcont(void) {
   termSetMode(TMODE_RAW);
   need_check_timestamps = true;
   did_check_timestamps = false;
}

//With multi-threading, suspending might not work immediately. Catch the
//SIGCONT signal, which will be used as an indication whether the suspending has been done or not.
//
//On Linux, signal is not always handled immediately either.
//See https://bugs.launchpad.net/bugs/291373
//Probably because the signal is handled in another thread.
//
//volatile because it is used in signal handler sigcont_handler().
private volatile SigAtomic sigcont_received;
private void sigcont_handler(int);

//signal handler for SIGCONT
private void
sigcont_handler(int) {
   //We didn't suspend ourselves, assume we were stopped by a SIGSTOP signal (which can't 
   //be intercepted) and get a SIGCONT. Need to get back to a sane mode. We should redraw, but
   //we can't really do that in a signal handler, do a redraw later.
   after_sigcont();
   redraw_later(UPD_CLEAR);
   cursor_on_force();
   termOutFlush();
}

//Catch CTRL-C (only works while in Cooked mode).
private void
catch_int_signal(void) {
   motSignalHandler(SIGINT, catch_sigint);
}

pub void
reset_signals(void) {
   catch_signals(SIG_DFL, SIG_DFL);
   //SIGCONT isn't in the list, because its default action is ignore
   motSignalHandler(SIGCONT, SIG_DFL);
}

private void
catch_signals(void (*func_deadly)(int), void (*func_other)(int)) {
   for (int i = 0; signalInfos[i].sig != -1; i++) {
      if (signalInfos[i].deadly) {
         SignalAction sa;

         //Setup to use the alternate stack for the signal function.
         sa.sa_handler = func_deadly;
         sigemptyset(&sa.sa_mask);
         sa.sa_flags = 0;
         sigaction(signalInfos[i].sig, &sa, NULL);
      } ei (func_other != SIG_ERR) {
         //Deal with non-deadly signals.
         motSignalHandler(
            signalInfos[i].sig, 
            signalInfos[i].sig == SIGTSTP && ignore_sigtstp ? SIG_IGN : func_other
         );
      }
   }
}

private void
setupSignalHandlers(void) {
   //WINDOW CHANGE signal is handled with sig_winch().
   motSignalHandler(SIGWINCH, sig_winch);

   //See mch_init() for the conditions under which we ignore SIGTSTP.
   //In the GUI default TSTP processing is OK.
   //Checking both gui.in_use and gui.starting because gui.in_use is not set
   //at this point (set after menus are displayed), but gui.starting is set.
   motSignalHandler(SIGTSTP, ignore_sigtstp ? SIG_IGN : sig_tstp);
   motSignalHandler(SIGCONT, sigcont_handler);
   //We want to ignore breaking of PIPEs.
   motSignalHandler(SIGPIPE, SIG_IGN);

   catch_int_signal();

   //Call user's handler on SIGUSR1
   motSignalHandler(SIGUSR1, catch_sigusr1);

   //Ignore alarm signals (Perl's alarm() generates it).
   motSignalHandler(SIGALRM, SIG_IGN);

   //Catch SIGPWR (power failure?) to preserve the swap files, so that no work will be lost.
   motSignalHandler(SIGPWR, catch_sigpwr);

   //Arrange for other signals to gracefully shutdown Eegl.
   catch_signals(deathtrap, SIG_ERR);
}

private CS signal_stack = null;

private stack_t sigstk;         //for sigaltstack()

private void
init_signal_stack(void) {
   if (!signal_stack)
      return;

   sigstk.ss_sp = signal_stack;
   sigstk.ss_size = get_signal_stack_size();
   sigstk.ss_flags = 0;
   (void)sigaltstack(&sigstk, NULL);
}

private CS
get_signal_name(int sig) {
   Byte   numbuf[NUMBUFLEN];

   if (sig == SIGKILL)
      return copySubstr((CS)"kill", STRLEN_LITERAL("kill"));

   int i;
   for (i = 0; signalInfos[i].sig != -1; i++) {
      if (sig == signalInfos[i].sig)
         return strlow_save((CS)signalInfos[i].name);
   } 

   i = eeSnprintf(numbuf, NUMBUFLEN, "%d", sig);
   return copySubstr(numbuf, i);
}

private void
block_signals(SignalSet* set) {
   SignalSet newset;
   sigemptyset(&newset);
   for (int i = 0; signalInfos[i].sig != -1; i++)
      sigaddset(&newset, signalInfos[i].sig);

   //SIGCONT isn't in the list, because its default action is ignore
   sigaddset(&newset, SIGCONT);
   sigprocmask(SIG_BLOCK, &newset, set);
}

private void
unblock_signals(SignalSet* set) {
   sigprocmask(SIG_SETMASK, set, NULL);
}


//Handling of SIGHUP, SIGQUIT and SIGTERM:
//"when" == a signal:       when busy, postpone and return false, otherwise return true
//"when" == SIGNAL_BLOCK:   Going to be busy, block signals
//"when" == SIGNAL_UNBLOCK: Going to wait, unblock signals, use postponed signal
//Return true when Eegl should exit.
pub int
eeHandleSignal(int sig) {
   static int got_signal = 0;
   static int blocked = true;

   switch (sig) {
   case SIGNAL_BLOCK:   
      blocked = true;
      break;

   case SIGNAL_UNBLOCK: 
      blocked = false;
      if (got_signal != 0) {
         kill(getpid(), got_signal);
         got_signal = 0;
      }
      break;

   default:
      if (!blocked)
         return true;   //exit!
      got_signal = sig;
      if (sig != SIGPWR)
         gotInterruptG = true;    //break any loops
      break;
    }
    return false;
}

pub void
may_core_dump(void) {
   if (deadlySignalP != 0) {
      motSignalHandler(deadlySignalP, SIG_DFL);
      kill(getpid(), deadlySignalP);   //Die using the signal we caught
   }
}

//}}}
//{{{resource cleanup at exit

//Output a newline when exiting. Make sure the newline goes to the same stream as the text.
private void
exit_scroll(void) {
   if (silentModeG)
      return;
   if (newlineOnExitG || msg_didout) {
      if (msg_use_printf()) {
         if (info_message)
            mch_msg("\n");
         else
            mch_errmsg("\r\n");
      } else
         out_char('\n');
   } ei (!is_not_a_term()) {
      msg_clr_eos_force();      //clear the rest of the display
      //windgoto((int)visibleRowsG - 1, 0);   //may have moved the cursor
   }
}


//Low-level resourse cleanup function
pub void
mch_exit(int r) {
   isExitingG = true;
   termSetMode(TMODE_COOK);

   //When t_ti is not empty but it doesn't cause swapping terminal pages, need to output a 
   //newline when msg_didout is set. But when t_ti does swap pages it should not go to the shell 
   //page. Do this before termStopTerminfo().
   if (termIsScreenBeingSwapped() && !newlineOnExitG)
      exit_scroll();

   //Stop termcap: May need to check for KS_CRV response, which requires RAW mode.
   termStopTerminfo();

   //A newline is only required after a message in the alternate screen.
   //This is set to true by wait_return().
   if (!termIsScreenBeingSwapped() || newlineOnExitG)
      exit_scroll();

   //Cursor may have been switched off without calling starttermcap()
   //when doing "eegl -u vimrc" and vimrc contains ":q".
   if (fullScreenG)
      cursor_on();
   
   termOutFlush();
   ml_close_all(true);      //remove all memfiles

#ifdef USE_GCOV_FLUSH
   //Flush coverage info before possibly being killed by a deadly signal.
   __gcov_flush();
#endif

   may_core_dump();


#ifdef EXITFREE
   free_all_mem();
#endif

   exit(r);
}

//}}}
//{{{time

//Cache of the current timezone name as retrieved from TZ, or an empty string
//where unset, up to 64 octets long including trailing null byte.
private Byte tz_cache[64];

#define FOR_ALL_TIMERS(t) \
    for ((t) = firstTimerS; (t) != NULL; (t) = (t)->next)
    

//Call either localtime(3) or localtime_r(3) from POSIX libc time.h, with the
//latter version preferred for reentrancy.
//
//If we use localtime_r(3) and we have tzset(3) available, check to see if the environment variable 
//TZ has changed since the last run, and call tzset(3) to update the global timezone variables if 
//it has.  This is because the POSIX standard doesn't require localtime_r(3) implementations to do 
//that as it does with localtime(3), and we don't want to call tzset(3) every time.
private Tm *
eeLocaltime(
   Tyme const* timep,      //timestamp for local representation
   OUT Tm* result //pointer to caller return buffer
){
   CS tz = mch_getenv(S"TZ");      //pointer for TZ environment var
   if (tz == NULL)
      tz = S"";
   if (STRNCMP(tz_cache, tz, sizeof(tz_cache) - 1) != 0) {
      tzset();
      copySubstrToAllocation((CS)tz_cache, (Text){tz, sizeof(tz_cache) - 1});
   }
   return localtime_r(timep, result);
}

//Return the current time in seconds.  Calls time(), unless test_settime() was used.
pub Tyme
eeTime(void) {
   return time_for_testing == 0 ? time(NULL) : time_for_testing;
}

//Replacement for ctime(), which is not safe to use.
//Requires strftime(), otherwise returns "(unknown)".
//If "thetime" is invalid returns "(invalid)".  Never returns NULL.
//When "add_newline" is true add a newline like ctime() does. Use a static buffer.
pub CS
get_ctime(Tyme thetime, int add_newline) {
   static Byte buf[100];  //hopefully enough for every language
   Tm tmval;
   Tm* curtime = eeLocaltime(&thetime, &tmval);
   if (!curtime)
      copySubstrToAllocation(buf, (Text){_("(Invalid)"), sizeof(buf) - 2});
   else {
      //xgettext:no-c-format
      if (STRFTIME(buf, sizeof(buf) - 2, _("%a %b %d %H:%M:%S %Y"), curtime) == 0) {
         //Quoting "man strftime":
         //> If the length of the result string (including the terminating
         //> null byte) would exceed max bytes, then strftime() returns 0,
         //> and the contents of the array are undefined.
         copySubstrToAllocation((CS)buf, (Text){_("(Invalid)"), sizeof(buf) - 2});
      }
   }
   if (add_newline)
      STRCAT(buf, "\n");
   return buf;
}


//"localtime()" function
pub void
f_localtime(Arr(Var), OUT Var* returnVar) {
   returnVar->number = (Long)time(NULL);
}

//Convert a List to ProfTime. Return FAIL when there is something wrong.
private int
list2proftime(Var *arg, ProfTime *tm) {
   if (arg->tag != VAR_LIST || arg->list == NULL || arg->list->len != 2)
      return FAIL;
      
   Boole error = false;
   long n1 = list_find_nr(arg->list, 0L, &error);
   long n2 = list_find_nr(arg->list, 1L, &error);
   tm->tv_sec = n1;
   tm->tv_fsec = n2;
   return error ? FAIL : OK;
}


//"reltime()" function
pub void
f_reltime(Arr(Var) argVars, OUT Var* returnVar) {
   ProfTime   res;
   ProfTime   start;

   allocReturnList(returnVar);

   if (argVars[0].tag == VAR_UNKNOWN) {
      //No arguments: get current time.
      profile_start(&res);
   } ei (argVars[1].tag == VAR_UNKNOWN) {
      if (list2proftime(&argVars[0], &res) == FAIL) {
         return;
      }
      profile_end(&res);
   } else {
      //Two arguments: compute the difference.
      if (list2proftime(&argVars[0], &start) == FAIL || list2proftime(&argVars[1], &res) == FAIL) {
         return;
      }
      profile_sub(&res, &start);
   }

   long n1 = res.tv_sec;
   long n2 = res.tv_fsec;
   list_append_number(returnVar->list, (Long)n1);
   list_append_number(returnVar->list, (Long)n2);
}

pub void
f_reltimefloat(Arr(Var) argVars, OUT Var* returnVar) {
   ProfTime   tm;

   returnVar->tag = VAR_FLOAT;
   returnVar->floatt = 0;

   if (list2proftime(&argVars[0], &tm) == OK)
      returnVar->floatt = profile_float(&tm);
}

pub void
f_reltimestr(Arr(Var) argVars, OUT Var* returnVar) {
   returnVar->tag = VAR_STRING;
   returnVar->string = NULL;

   ProfTime   tm;
   if (list2proftime(&argVars[0], &tm) == OK) {
      static Byte buf[50];
      long usec = tm.tv_fsec / (TV_FSEC_SEC / 1000000);
      eeSnprintf(buf, sizeof(buf), "%3ld.%06ld", (long)tm.tv_sec, usec);
      returnVar->string = copyStr(buf);
   }
}


//"strftime({format}[, {time}])" function
pub void
f_strftime(Arr(Var) argVars, OUT Var* returnVar) {
   Tm tmval;
   Tyme seconds;

   returnVar->tag = VAR_STRING;

   CS arg = tv_get_string(&argVars[0]);
   if (argVars[1].tag == VAR_UNKNOWN)
      seconds = time(NULL);
   else
      seconds = (Tyme)tv_get_number(&argVars[1]);
   Tm* curtime = eeLocaltime(&seconds, &tmval);
   if (!curtime) {
      returnVar->string = copyStr((CS)_("(Invalid)"));
      return;
   }

   Byte result_buf[256];

   if (!arg || STRFTIME(result_buf, sizeof(result_buf), arg, curtime) == 0)
      result_buf[0] = ZERO;

   returnVar->string = copyStr(result_buf);
}

//"strptime({format}, {timestring})" function
pub void
f_strptime(Var* argVars, Var* returnVar) {
   Tm tmval;

   CLEAR_FIELD(tmval);
   tmval.tm_isdst = -1;
   Byte* fmt = tv_get_string(&argVars[0]);
   Byte* str = tv_get_string(&argVars[1]);

   if (!fmt
          || strptime((char *)str, (char *)fmt, &tmval) == NULL
          || (returnVar->number = mktime(&tmval)) == -1
   )
      returnVar->number = 0;
}

private Timer* firstTimerS = NULL;
private long lastTimerIdS = 0;

//Return time left, in "msec", until "due".  Negative if past "due".
pub long
proftime_time_left(ProfTime *due, ProfTime *now) {
   if (now->tv_sec > due->tv_sec)
      return 0;
   return (due->tv_sec - now->tv_sec)*1000 + (due->tv_fsec - now->tv_fsec) / (TV_FSEC_SEC / 1000);
}

//Insert a timer into the list of timers.
private void
insert_timer(Timer* timer) {
   timer->next = firstTimerS;
   timer->prev = NULL;
   if (firstTimerS != NULL)
      firstTimerS->prev = timer;
   firstTimerS = timer;
   did_add_timer = true;
}

//Take a timer out of the list of timers.
private void
remove_timer(Timer* timer) {
   if (!timer->prev)
      firstTimerS = timer->next;
   else
      timer->prev->next = timer->next;
   if (timer->next)
      timer->next->prev = timer->prev;
}

private void
free_timer(Timer* timer) {
   evFreeCallback(&timer->callback);
   eeglFree(timer);
}

//Create a timer and return it. Caller should set the callback.
pub Timer*
create_timer(long msec, int repeat) {
   Timer* timer = ALLOC_CLEAR_ONE(Timer);
   long   prev_id = lastTimerIdS;

   if (++lastTimerIdS <= prev_id)
      //Overflow!  Might cause duplicates...
      lastTimerIdS = 0;
   timer->id = lastTimerIdS;
   insert_timer(timer);
   if (repeat != 0)
      timer->tr_repeat = repeat - 1;
   timer->tr_interval = msec;

   timer_start(timer);
   return timer;
}

//(Re)start a timer.
pub void
timer_start(Timer *timer) {
   profile_setlimit(timer->tr_interval, &timer->due);
   timer->tr_paused = false;
}

//Invoke the callback of "timer".
private void
timer_callback(Timer *timer) {
   Var   returnVar;
   Var   argv[2];

   if (ch_log_active()) {
      Callback *cb = &timer->callback;
      lo("invoking timer callback %s", cb->cb_partial != NULL ? cb->cb_partial->name : cb->name);
   }

   argv[0].tag = VAR_NUMBER;
   argv[0].number = (Long)timer->id;
   argv[1].tag = VAR_UNKNOWN;

   returnVar.tag = VAR_UNKNOWN;
   call_callback(&timer->callback, -1, &returnVar, 1, argv);
   clearVar(&returnVar);

   lo("timer callback finished");
}

//Call timers that are due. Return the time in msec until the next timer is due.
//Return -1 if there are no pending timers.
pub long
check_due_timer(void) {
   Timer* timer_next;
   long this_due;
   long next_due = -1;
   ProfTime now;
   Boole did_one = false;
   Boole need_drawUpdateScreen = false;
   long current_id = lastTimerIdS;

   //Don't run any timers while exiting, dealing with an error or at the debug prompt.
   if (isExitingG || aborting() || debug_mode)
      return next_due;

   profile_start(&now);
   for (Timer* timer = firstTimerS; timer != NULL && !gotInterruptG; timer = timer_next) {
      timer_next = timer->next;

      if (timer->id == -1 || timer->tr_firing || timer->tr_paused)
         continue;
      this_due = proftime_time_left(&timer->due, &now);
      if (this_due <= 1) {
         //Save and restore a lot of flags, because the timer fires while
         //waiting for a character, which might be halfway a command.
         int save_timer_busy = timer_busy;
         int save_vgetcBusyG = vgetcBusyG;
         int save_anyEmsgG = anyEmsgG;
         int prev_uncaught_emsg = uncaught_emsg;
         int save_called_emsg = called_emsg;
         Unt mustRedrawSaved = mustRedrawG;
         int save_ex_pressedreturn = get_pressedreturn();
         int save_may_garbage_collect = may_garbage_collect;
         ExceptionState estate;

         exception_state_save(&estate);

         //Create a scope for running the timer callback, ignoring most of
         //the current scope, such as being inside a try/catch.
         timer_busy = timer_busy > 0 || vgetcBusyG > 0;
         vgetcBusyG = 0;
         called_emsg = 0;
         anyEmsgG = false;
         mustRedrawG = 0;
         may_garbage_collect = false;
         exception_state_clear();

         //Invoke the callback.
         timer->tr_firing = true;
         timer_callback(timer);
         timer->tr_firing = false;

         //Restore stuff.
         timer_next = timer->next;
         did_one = true;
         timer_busy = save_timer_busy;
         vgetcBusyG = save_vgetcBusyG;
         if (uncaught_emsg > prev_uncaught_emsg)
            ++timer->tr_emsg_count;
         anyEmsgG = save_anyEmsgG;
         called_emsg = save_called_emsg;
         exception_state_restore(&estate);
         if (mustRedrawG != 0)
            need_drawUpdateScreen = true;
         mustRedrawG = mustRedrawG > mustRedrawSaved ? mustRedrawG : mustRedrawSaved;
         set_pressedreturn(save_ex_pressedreturn);
         may_garbage_collect = save_may_garbage_collect;

         //Only fire the timer again if it repeats and stop_timer() wasn't
         //called while inside the callback (id == -1).
         if (timer->tr_repeat != 0 && timer->id != -1 && timer->tr_emsg_count < 3) {
            profile_setlimit(timer->tr_interval, &timer->due);
            this_due = proftime_time_left(&timer->due, &now);
            if (this_due < 1)
               this_due = 1;
            if (timer->tr_repeat > 0)
               --timer->tr_repeat;
         } else {
            this_due = -1;
            if (timer->tr_keep)
               timer->tr_paused = true;
            else {
               remove_timer(timer);
               free_timer(timer);
            }
         }
      }
      if (this_due > 0 && (next_due == -1 || next_due > this_due))
         next_due = this_due;
   }

   if (did_one)
      redraw_after_callback(need_drawUpdateScreen, false);

   if (bevalexpr_due_set) {
      this_due = proftime_time_left(&bevalexpr_due, &now);
      if (this_due <= 1) {
         bevalexpr_due_set = false;
         if (balloonEval == NULL) {
            balloonEval = ALLOC_CLEAR_ONE(BalloonEval);
            balloonEvalForTerm = true;
         }
         if (balloonEval != NULL) {
            general_beval_cb(balloonEval, 0);
            setcursor();
            termOutFlush();
         }
      } ei (next_due == -1 || next_due > this_due)
         next_due = this_due;
   }
   //Some terminal portals may need their book updated.
   next_due = term_check_timers(next_due, &now);

   return current_id != lastTimerIdS ? 1 : next_due;
}

//Find a timer by ID.  Returns NULL if not found;
private Timer *
find_timer(long id) {
   Timer *timer;

   if (id < 0)
      return NULL;

   FOR_ALL_TIMERS(timer) {
      if (timer->id == id)
          return timer;
   } 
   return NULL;
}


//Stop a timer and delete it.
pub void
stop_timer(Timer *timer) {
   if (timer->tr_firing)
      //Free the timer after the callback returns.
      timer->id = -1;
   else {
      remove_timer(timer);
      free_timer(timer);
   }
}

private void
stop_all_timers(void) {
   Timer *timer;
   Timer *timer_next;

   for (timer = firstTimerS; timer != NULL; timer = timer_next) {
      timer_next = timer->next;
      stop_timer(timer);
   }
}

private void
add_timer_info(OUT Var* returnVar, Timer *timer) {
   List   *list = returnVar->list;
   Bag   *dict = allocBag();
   long   remaining;
   ProfTime   now;

   listAppendBag(list, dict);

   bagAddNumber(dict, S"id", timer->id);
   bagAddNumber(dict, S"time", (long)timer->tr_interval);

   profile_start(&now);
   remaining = proftime_time_left(&timer->due, &now);
   bagAddNumber(dict, S"remaining", (long)remaining);

   bagAddNumber(dict, S"repeat",
       (long)(timer->tr_repeat < 0 ? -1
              : timer->tr_repeat + (timer->tr_firing ? 0 : 1)));
   bagAddNumber(dict, S"paused", (long)(timer->tr_paused));

   DictItem* di = dictitem_alloc(tConst("callback"));
   if (bagAdd(dict, di) == FAIL)
      eeglFree(di);
   else
      putCallback(OUT &di->c, &timer->callback);
}

private void
add_timer_info_all(OUT Var* returnVar) {
   Timer *timer;

   FOR_ALL_TIMERS(timer) {
      if (timer->id != -1)
         add_timer_info(returnVar, timer);
   } 
}

//Mark references in partials of timers.
pub int
set_ref_in_timer(int copyID) {
   int abort = false;
   Var   tv;

   for (Timer* timer = firstTimerS; !abort && timer; timer = timer->next) {
      if (timer->callback.cb_partial) {
         tv.tag = VAR_PARTIAL;
         tv.partial = timer->callback.cb_partial;
      } else {
         tv.tag = VAR_FUNC;
         tv.string = timer->callback.name;
      }
      abort = abort || set_ref_in_item(&tv, copyID, NULL, NULL);
   }
   return abort;
}

//Return true if "timer" exists in the list of timers.
pub int
timer_valid(Timer *timer) {
   if (!timer)
      return false;

   Timer *t;
   FOR_ALL_TIMERS(t) {
      if (t == timer)
         return true;
   } 
   return false;
}

# if defined(EXITFREE)
pub void
timer_free_all(void) {
   while (firstTimerS != NULL) {
      Timer *timer = firstTimerS;
      remove_timer(timer);
      free_timer(timer);
   }
}
# endif

//"timer_info([timer])" function
pub void
f_timer_info(Arr(Var) argVars, OUT Var* returnVar) {
   Timer *timer = NULL;

   allocReturnList(returnVar);

   if (check_for_opt_number_arg(argVars, 0) == FAIL)
      return;

   if (argVars[0].tag != VAR_UNKNOWN) {
      timer = find_timer((int)tv_get_number(&argVars[0]));
      if (timer != NULL)
         add_timer_info(returnVar, timer);
   } else
      add_timer_info_all(returnVar);
}

//"timer_pause(timer, paused)" function
pub void
f_timer_pause(Arr(Var) argVars, OUT Var*) {
   if (argVars[0].tag != VAR_NUMBER) {
      emsg(_(e_number_expected));
      return;
   }

   int paused = (int)tv_get_bool(&argVars[1]);

   Timer* timer = find_timer((int)tv_get_number(&argVars[0]));
   if (timer != NULL)
      timer->tr_paused = paused;
}

//"timer_start(time, callback [, options])" function
pub void
f_timer_start(Arr(Var) argVars, OUT Var* returnVar) {
   int repeat = 0;
   Bag* dict;

   returnVar->number = -1;

   long msec = (long)tv_get_number(&argVars[0]);
   if (argVars[2].tag != VAR_UNKNOWN) {
      if (check_for_nonnull_dict_arg(argVars, 2) == FAIL)
         return;

      dict = argVars[2].bag;
      if (bagHasKey(dict, tConst("repeat")))
         repeat = bagGetNumber(dict, tConst("repeat"));
   }

   Callback callback = get_callback(&argVars[1]);
   if (!callback.name)
      return;

   Timer* timer = create_timer(msec, repeat);
   if (!timer) {
      evFreeCallback(&callback);
      return;
   }
   set_callback(&timer->callback, &callback);
   if (callback.needsFreeing)
      eeglFree(callback.name);
   returnVar->number = (Long)timer->id;
}

//"timer_stop(timer)" function
pub void
f_timer_stop(Arr(Var) argVars, OUT Var*) {
   if (check_for_number_arg(argVars, 0) == FAIL)
      return;

   Timer* timer = find_timer((int)tv_get_number(&argVars[0]));
   if (timer)
      stop_timer(timer);
}

//"timer_stopall()" function
pub void
f_timer_stopall(Arr(Var), OUT Var*) {
   stop_all_timers();
}

private TimeSpec prev_timeval;

//Save the previous time before doing something that could nest.
//set "*tv_rel" to the time elapsed so far.
//Not public because there's a special header for this file, motor.time.h!
void
time_push(TimeSpec* tv_rel, TimeSpec* tv_start) {
   *tv_rel = prev_timeval;
   timespec_get(&prev_timeval, TIME_UTC);
   tv_rel->tv_nsec = prev_timeval.tv_nsec - tv_rel->tv_nsec;
   tv_rel->tv_sec = prev_timeval.tv_sec - tv_rel->tv_sec;
   if (tv_rel->tv_nsec < 0) {
      tv_rel->tv_nsec += 1000000;
      --tv_rel->tv_sec;
   }
   *tv_start = prev_timeval;
}

//Compute the previous time after doing something that could nest.
//Subtract "*tp" from prev_timeval;
//Not public because there's a special header for this file, motor.time.h!
void
time_pop(TimeSpec* tp) {
   prev_timeval.tv_nsec -= tp->tv_nsec;
   prev_timeval.tv_sec -= tp->tv_sec;
   if (prev_timeval.tv_nsec < 0) {
      prev_timeval.tv_nsec += 1000000;
      --prev_timeval.tv_sec;
   }
}

private void
time_diff(TimeSpec* then, TimeSpec* now) {
   long usec = now->tv_nsec - then->tv_nsec;
   long msec = (now->tv_sec - then->tv_sec) * 1000L + usec / 1000L;
   usec = usec % 1000L;
   fprintf(time_fd, "%03ld.%03ld", msec, usec >= 0 ? usec : usec + 1000L);
}

//Not public because there's a special header for this file, motor.time.h!
void
time_msg(CS mesg, TimeSpec* tv_start){
//only for scriptRunFile: start time;
   static TimeSpec start;

   if (!time_fd)
      return;

   if (STRSTR(mesg, S"STARTING") != NULL) {
      timespec_get(OUT &start, TIME_UTC);
      prev_timeval = start;
      fprintf(time_fd, "\n\ntimes in msec\n");
      fprintf(time_fd, " clock   self+sourced   self:  sourced script\n");
      fprintf(time_fd, " clock   elapsed:              other lines\n\n");
   }
   
   TimeSpec now;
   timespec_get(OUT &now, TIME_UTC);
   time_diff(&start, &now);
   if (tv_start) {
      fprintf(time_fd, "  ");
      time_diff(tv_start, &now);
   }
   fprintf(time_fd, "  ");
   time_diff(&prev_timeval, &now);
   prev_timeval = now;
   fprintf(time_fd, ": %s\n", mesg);
}

//Not public because there's a special header for this file, motor.time.h!
Long
motElapsedMs(TimeSpec since) {
   TimeSpec now;
   timespec_get(OUT &now, TIME_UTC);
   return (now.tv_nsec - since.tv_nsec)/1000000;
}

//Read 8 bytes from "fd" and turn them into a Tyme, MSB first. Returns -1 when encountering EOF.
pub Tyme
get8ctime(FILE *fd) {
   Tyme   n = 0;

   for (int i = 0; i < 8; ++i) {
      int c = getc(fd);
      if (c == EOF) return -1;
      n = (n << 8) + c;
   }
   return n;
}

//Write Tyme to file "fd" in 8 bytes. Returns FAIL when the write failed.
pub int
put_time(FILE *fd, Tyme the_time) {
   Byte buf[8];

   time_to_bytes(the_time, buf);
   return fwrite(buf, 8, 1, fd) == 1 ? OK : FAIL;
}

//Write Tyme to "buf[8]".
pub void
time_to_bytes(Tyme the_time, CS buf) {
   int      c;
   int      i;
   int      bi = 0;
   Tyme   wtime = the_time;

   //Tyme can be up to 8 bytes in size, more than Ulong, thus we can't use put_bytes() here.
   //Another problem is that ">>" may do an arithmetic shift that keeps the sign. This happens 
   //for large values of wtime. A cast to Ulong may truncate if Tyme is 8 bytes. So only use a 
   //cast when it is 4 bytes, it's safe to assume that Ulong is 4 bytes or more and when using 8
   //bytes the top bit won't be set.
   for (i = 7; i >= 0; --i) {
      if (i + 1 > (int)sizeof(Tyme))
         //">>" doesn't work well when shifting more bits than avail
         buf[bi++] = 0;
      else {
         c = (int)(wtime >> (i * 8));
         buf[bi++] = c;
      }
   }
}

//Put timestamp "tt" in "buf[buflen]" in a nice format.
pub void
add_time(CS buf, Unt buflen, Tyme tt) {
   Tm tmval;
   Tm* curtime;
   Unt   n;

   if (eeTime() - tt >= 100) {
      curtime = eeLocaltime(&tt, &tmval);
      if (eeTime() - tt < (60L * 60L * 12L))
         //within 12 hours
         n = STRFTIME(buf, buflen, "%H:%M:%S", curtime);
      else
         //longer ago
         n = STRFTIME(buf, buflen, "%Y/%m/%d %H:%M:%S", curtime);
      if (n == 0)
         buf[0] = ZERO;
   } else {
      long seconds = (long)(eeTime() - tt);

      eeSnprintf(buf, buflen, NGETTEXT("%ld second ago", "%ld seconds ago", seconds), seconds);
   }
}

//Store the current time in "tm".
pub void
profile_start(ProfTime *tm){
   PROF_GET_TIME(tm);
}

//Put the time "msec" past now in "tm".
pub void
profile_setlimit(long msec, ProfTime *tm) {
   if (msec <= 0)   //no limit
      profile_zero(tm);
   else {
      PROF_GET_TIME(tm);
      Long fsec = (Long)tm->tv_fsec + (Long)msec * (Long)(TV_FSEC_SEC / 1000);
      tm->tv_fsec = fsec % (long)TV_FSEC_SEC;
      tm->tv_sec += fsec / (long)TV_FSEC_SEC;
   }
}

//Return true if the current time is past "tm".
pub int
profile_passed_limit(ProfTime *tm) {
   if (tm->tv_sec == 0)    //timer was not set
      return false;
      
   ProfTime   now;
   PROF_GET_TIME(&now);
   return (now.tv_sec > tm->tv_sec || (now.tv_sec == tm->tv_sec && now.tv_fsec > tm->tv_fsec));
}


//Compute the elapsed time from "tm" till now and store in "tm".
pub void
profile_end(ProfTime *tm) {
   ProfTime now;

   PROF_GET_TIME(OUT &now);
   tm->tv_fsec = now.tv_fsec - tm->tv_fsec;
   tm->tv_sec = now.tv_sec - tm->tv_sec;
   if (tm->tv_fsec < 0) {
      tm->tv_fsec += TV_FSEC_SEC;
      --tm->tv_sec;
   }
}

//Subtract the time "tm2" from "tm".
pub void
profile_sub(ProfTime *tm, ProfTime *tm2){
   tm->tv_fsec -= tm2->tv_fsec;
   tm->tv_sec -= tm2->tv_sec;
   if (tm->tv_fsec < 0) {
      tm->tv_fsec += TV_FSEC_SEC;
      --tm->tv_sec;
   }
}

//Return a float that represents the time in "tm".
private double
profile_float(ProfTime *tm){
   return (double)tm->tv_sec + (double)tm->tv_fsec / (double)TV_FSEC_SEC;
}

//Set the time in "tm" to zero.
pub void
profile_zero(ProfTime *tm) {
   tm->tv_fsec = 0;
   tm->tv_sec = 0;
}

//Return a string that represents the time in "tm". Use a static buffer!
pub CS
profile_msg(ProfTime *tm){
   static Byte buf[50];

   SPRINTF(buf, PROF_TIME_FORMAT, (long)tm->tv_sec, (long)tm->tv_fsec);
   return buf;
}

# ifdef ELAPSED_TIMEVAL
//Return time in msec since "start".
pub Long
elapsed(TimeSpec* start) {
   TimeSpec now;
   timespec_get(OUT &now, TIME_UTC);
   return (now.tv_sec - start->tv_sec) * 1000L + (now.tv_usec - start->tv_usec) / 1000L;
}
# endif

# if defined(PROF_NSEC)
//Implement timeout with timer_create() and timer_settime().
private volatile SigAtomic timeout_flag = false;
private timer_t timer_id;
private int timer_created = false;

//Callback for when the timer expires.
private void
set_flag(union sigval) {
   timeout_flag = true;
}

//Stop any active timeout.
pub void
stop_timeout(void) {
   static struct itimerspec disarm = {{0, 0}, {0, 0}};

   if (timer_created) {
      int ret = timer_settime(timer_id, 0, &disarm, NULL);

      if (ret < 0)
         showErrFmtMsg(_(e_could_not_clear_timeout_str), strerror(errno));
   }

   //Clear the current timeout flag; any previous timeout should be
   //considered _not_ triggered.
   timeout_flag = false;
}

//Start the timeout timer.
//
//The return value is a pointer to a flag that is initialised to false. If the
//timeout expires, the flag is set to true. This will only return pointers to
//static memory; i.e. any pointer returned by this function may always be
//safely dereferenced.
//
//This function is not expected to fail, but if it does it will still return a
//valid flag pointer; the flag will remain stuck as false .
pub volatile SigAtomic *
start_timeout(long msec) {
   struct itimerspec interval = {
       {0, 0},               //Do not repeat.
       {msec / 1000, (msec % 1000) * 1000000}};   //Timeout interval

   //This is really the caller's responsibility, but let's make sure the
   //previous timer has been stopped.
   stop_timeout();

   if (!timer_created) {
      struct sigevent action = {0};

      action.sigev_notify = SIGEV_THREAD;
      action.sigev_notify_function = set_flag;
      int ret = timer_create(CLOCK_MONOTONIC, &action, &timer_id);
      if (ret < 0) {
         showErrFmtMsg(_(e_could_not_set_timeout_str), strerror(errno));
         return &timeout_flag;
      }
      timer_created = true;
   }

   lo("setting timeout timer to %d sec %ld nsec",
          (int)interval.it_value.tv_sec, (long)interval.it_value.tv_nsec);
   int ret = timer_settime(timer_id, 0, &interval, NULL);
   if (ret < 0)
      showErrFmtMsg(_(e_could_not_set_timeout_str), strerror(errno));

   return &timeout_flag;
}

//To be used before fork/exec: delete any created timer.
pub void
delete_timer(void) {
   if (!timer_created)
      return;

   timer_delete(timer_id);
   timer_created = false;
}

# else //PROF_NSEC

//Implement timeout with setitimer()
private SignalAction      prev_sigaction;
private volatile SigAtomic   timeout_flag        = false;
private int         timer_active        = false;
private int         timer_handler_active = false;
private volatile SigAtomic   alarm_pending        = false;

//Handle SIGALRM for a timeout.
private void
set_flag(union sigval) {
   if (alarm_pending)
      alarm_pending = false;
   else
      timeout_flag = true;
}

//Stop any active timeout.
pub void
stop_timeout(void) {
   static struct itimerval disarm = {{0, 0}, {0, 0}};
   int             ret;

   if (timer_active) {
      timer_active = false;
      ret = setitimer(ITIMER_REAL, &disarm, NULL);
      if (ret < 0)
         //Should only get here as a result of coding errors.
         showErrFmtMsg(_(e_could_not_clear_timeout_str), strerror(errno));
   }

   if (timer_handler_active) {
      timer_handler_active = false;
      ret = sigaction(SIGALRM, &prev_sigaction, NULL);
      if (ret < 0)
         //Should only get here as a result of coding errors.
         showErrFmtMsg(_(e_could_not_reset_handler_for_timeout_str), strerror(errno));
   }
   timeout_flag = false;
}

//Start the timeout timer.
//
//The return value is a pointer to a flag that is initialised to false. If the timeout expires, the
//flag is set to true. This will only return pointers to static memory; i.e. any pointer returned 
//by this function may always be safely dereferenced.
//
//This function is not expected to fail, but if it does it will still return a valid flag pointer;
//the flag will remain stuck as false.
pub volatile SigAtomic*
start_timeout(long msec) {
   struct itimerval   interval = {
       {0, 0},                //Do not repeat.
       {msec / 1000, (msec % 1000) * 1000}};   //Timeout interval
   SignalAction handle_alarm;
   int ret;
   SignalSet sigs;
   SignalSet saved_sigs;

   //This is really the caller's responsibility, but let's make sure the
   //previous timer has been stopped.
   stop_timeout();

   //There is a small chance that SIGALRM is pending and so the handler must
   //ignore it on the first call.
   alarm_pending = false;
   ret = sigemptyset(&sigs);
   ret = ret == 0 ? sigaddset(&sigs, SIGALRM) : ret;
   ret = ret == 0 ? sigprocmask(SIG_BLOCK, &sigs, &saved_sigs) : ret;
   timeout_flag = false;
   ret = ret == 0 ? sigpending(&sigs) : ret;
   if (ret == 0) {
      alarm_pending = sigismember(&sigs, SIGALRM);
      ret = sigprocmask(SIG_SETMASK, &saved_sigs, NULL);
   }
   if (unlikely(ret != 0 || alarm_pending < 0)) {
      //Just catching coding errors. Write an error message, but carry on.
      showErrFmtMsg(_(e_could_not_check_for_pending_sigalrm_str), strerror(errno));
      alarm_pending = false;
   }

   //Set up the alarm handler first.
   ret = sigemptyset(&handle_alarm.sa_mask);
   handle_alarm.sa_handler = set_flag;
   
   handle_alarm.sa_flags = 0;
   ret = ret == 0 ?  sigaction(SIGALRM, &handle_alarm, &prev_sigaction) : ret;
   if (ret < 0) {
      //Should only get here as a result of coding errors.
      showErrFmtMsg(_(e_could_not_set_handler_for_timeout_str), strerror(errno));
      return &timeout_flag;
   }
   timer_handler_active = true;

   //Set up the interval timer once the alarm handler is in place.
   ret = setitimer(ITIMER_REAL, &interval, NULL);
   if (ret < 0) {
      //Should only get here as a result of coding errors.
      showErrFmtMsg(_(e_could_not_set_timeout_str), strerror(errno));
      stop_timeout();
      return &timeout_flag;
   }

   timer_active = true;
   return &timeout_flag;
}
# endif //PROF_NSEC


//}}}
//{{{auxiliary


//Allocate a new channel. The refcount is set to 1.
//The channel isn't actually used until it is opened.
pub Channel*
add_channel(void) {
   ChannelFdKind part;
   Channel* channel = ALLOC_CLEAR_ONE(Channel);

   channel->id = next_ch_id++;
   ch_log(channel, "Created channel");

   for (part = PART_SOCK; part < PART_COUNT; ++part) {
      channel->fds[part].fd = INVALID_FD;
      channel->fds[part].ch_timeout = 2000;
   }

   if (firstChannelP != NULL) {
      firstChannelP->prev = channel;
      channel->next = firstChannelP;
   }
   firstChannelP = channel;

   channel->refCount = 1;
   return channel;
}

pub int
has_any_channel(void){
   return firstChannelP != NULL;
}

//Called when the refcount of a channel is zero.
//Return true if "channel" has a callback and the associated job wasn't killed.
pub int
channel_still_useful(Channel *channel) {
   int has_sock_msg;
   int has_out_msg;
   int has_err_msg;

   //If the job was killed the channel is not expected to work anymore.
   if (channel->isBeingKilled && channel->job == NULL)
      return false;

   //If there is a close callback it may still need to be invoked.
   if (channel->ch_close_cb.name != NULL)
      return true;

   //If reading from or a book it's still useful.
   if (channel->fds[PART_IN].bookref.c != NULL)
      return true;

   //If there is no callback then nobody can get readahead.  If the fd is
   //closed and there is no readahead then the callback won't be called.
   has_sock_msg = channel->fds[PART_SOCK].fd != INVALID_FD
      || channel->fds[PART_SOCK].head.next != NULL
      || channel->fds[PART_SOCK].ch_json_head.jq_next != NULL;
   has_out_msg = channel->fds[PART_OUT].fd != INVALID_FD
        || channel->fds[PART_OUT].head.next != NULL
        || channel->fds[PART_OUT].ch_json_head.jq_next != NULL;
   has_err_msg = channel->fds[PART_ERR].fd != INVALID_FD
        || channel->fds[PART_ERR].head.next != NULL
        || channel->fds[PART_ERR].ch_json_head.jq_next != NULL;
   return (channel->ch_callback.name && (has_sock_msg || has_out_msg || has_err_msg))
       || ((channel->fds[PART_OUT].ch_callback.name != NULL
             || channel->fds[PART_OUT].bookref.c != NULL)
          && has_out_msg)
       || ((channel->fds[PART_ERR].ch_callback.name != NULL
             || channel->fds[PART_ERR].bookref.c != NULL)
          && has_err_msg);
}

//Return true if "channel" is closeable (i.e. all readable fds are closed).
pub int
channel_can_close(Channel* channel) {
   return channel->ch_to_be_closed == 0;
}

//Close a channel and free all its resources. The "channel" pointer remains valid.
private void
channel_free_contents(Channel* channel) {
   channel_close(channel, true);
   channel_clear(channel);
   ch_log(channel, "Freeing channel");
}

//Unlink "channel" from the list of channels and free it.
private void
channel_free_channel(Channel* channel) {
   if (channel->next)
      channel->next->prev = channel->prev;
   if (!channel->prev)
      firstChannelP = channel->next;
   else
      channel->prev->next = channel->next;
   eeglFree(channel);
}

private void
channel_free(Channel* channel) {
   if (in_free_unref_items)
      return;

   if (safe_to_invoke_callback == 0)
      channel->ch_to_be_freed = true;
   else {
      channel_free_contents(channel);
      channel_free_channel(channel);
   }
}

//Close a channel and free all its resources if there is no further action
//possible, there is no callback to be invoked or the associated job was
//killed. Return true if the channel was freed.
private int
channel_may_free(Channel* channel) {
   if (!channel_still_useful(channel)) {
      channel_free(channel);
      return true;
   }
   return false;
}

//Decrement the reference count on "channel" and maybe free it when it goes
//down to zero.  Don't free it if there is a pending action.
//Return true when the channel is no longer referenced.
pub int
channel_unref(Channel* channel) {
   if (channel && --channel->refCount <= 0)
      return channel_may_free(channel);
   return false;
}

pub int
free_unused_channels_contents(int copyID, int mask) {
   int did_free = false;

   //This is invoked from the garbage collector, which only runs at a safe point.
   ++safe_to_invoke_callback;

   Channel* ch;
   FOR_ALL_CHANNELS(ch) {
      if (!channel_still_useful(ch) && (ch->copyId & mask) != (copyID & mask)) {
          //Free the channel and ordinary items it contains, but don't
          //recurse into Lists, Dictionaries etc.
          channel_free_contents(ch);
          did_free = true;
      }
   } 

   --safe_to_invoke_callback;
   return did_free;
}

pub void
free_unused_channels(int copyID, int mask) {
   Channel* next;
   for (Channel* ch = firstChannelP; ch; ch = next) {
      next = ch->next;
      if (!channel_still_useful(ch) && (ch->copyId & mask) != (copyID & mask))
         //Free the channel struct itself.
         channel_free_channel(ch);
   }
}

//"flags": MCH_DELAY_IGNOREINPUT - don't read input
//     MCH_DELAY_SETTMODE - use termSetMode() even for short delays
pub void
mch_delay(long msec, int flags) {
   TermInputMode old_tmode;
   int call_termSetMode;

   if (flags & MCH_DELAY_IGNOREINPUT) {
      //Go to cooked mode without echo, to allow SIGINT interrupting us
      //here. But we don't want QUIT to kill us (CTRL-\ used in a
      //shell may produce SIGQUIT). Only do this if sleeping for more than half a second.
      inMchDelayP = true;
      call_termSetMode = mch_cur_tmode == TMODE_RAW
                   && (msec > 500 || (flags & MCH_DELAY_SETTMODE));
      if (call_termSetMode) {
          old_tmode = mch_cur_tmode;
          termSetMode(TMODE_SLEEP);
      }

      //Everybody sleeps in a different way...
      //Prefer nanosleep(), some versions of usleep() can only sleep up to one second.
      TimeSpec ts;

      ts.tv_sec = msec / 1000;
      ts.tv_nsec = (msec % 1000) * 1000000;
      (void)nanosleep(&ts, NULL);

      if (call_termSetMode)
         termSetMode(old_tmode);
      inMchDelayP = false;
   } else
      waitForChar(msec, NULL, false);
}

//We need to call connect() again after connect() failed.
private int
channel_connect(Channel* channel, SockAddr* server_addr, int server_addrlen, int *waittime) {
   int sd = -1;

   while (true) {
      if (sd >= 0)
         sock_close(sd);
      sd = socket(server_addr->sa_family, SOCK_STREAM, 0);
      if (sd == -1) {
         ch_error(channel, "in socket() in channel_connect().");
         PERROR(_(e_socket_in_channel_connect));
         return -1;
      }

      if (*waittime >= 0) {
         //Make connect() non-blocking.
         if (fcntl(sd, F_SETFL, O_NONBLOCK) < 0) {
            SOCK_ERRNO;
            ch_error(channel, "channel_connect: Connect failed with errno %d", errno);
            sock_close(sd);
            return -1;
         }
      }

      //Try connecting to the server.
      ch_log(channel, "Connecting...");

      int ret = connect(sd, server_addr, server_addrlen);
      if (ret == 0)
         //The connection could be established.
         break;

      SOCK_ERRNO;
      if (*waittime < 0 
            || (errno != EWOULDBLOCK && errno != ECONNREFUSED && errno != EINPROGRESS)
      ) {
         ch_error(channel, "channel_connect: Connect failed with errno %d", errno);
         PERROR(_(e_cannot_connect_to_port));
         sock_close(sd);
         return -1;
      } ei (errno == ECONNREFUSED) {
         ch_error(channel, "channel_connect: Connection refused");
         sock_close(sd);
         return -1;
      }

      //Limit the waittime to 50 msec.  If it doesn't work within this
      //time we close the socket and try creating it again.
      int waitnowMs = *waittime > 50 ? 50 : *waittime;

      Long elapsed_msec = 0;
      //If connect() didn't finish then try using poll() to wait for the connection to be made.
      {
         int so_error = 0;
         socklen_t so_error_len = sizeof(so_error);
         TimeSpec start_tv;
         TimeSpec end_tv;
         PollFd pollFd = (PollFd){.fd = sd, .events = POLLIN|POLLOUT, .revents = 0};

         timespec_get(OUT &start_tv, TIME_UTC);
         ch_log(channel, "Waiting for connection (waiting %d msec)...", waitnowMs);

         ret = poll(&pollFd, 1, waitnowMs);
         if (ret < 0) {
            SOCK_ERRNO;
            ch_error(channel, "channel_connect: Connect failed with errno %d", errno);
            PERROR(_(e_cannot_connect_to_port));
            sock_close(sd);
            return -1;
         }

         //See socket(7) for the behavior
         //After putting the socket in non-blocking mode, connect() will return EINPROGRESS, 
         //poll() will not wait (as if writing is possible), need to use getsockopt() to check 
         //if the socket is actually able to connect. We detect a failure to connect when either 
         //read and write fds are set. Use getsockopt() to find out what kind of failure.
         if ((pollFd.revents & (POLLIN|POLLOUT)) != 0) {
            ret = getsockopt(sd, SOL_SOCKET, SO_ERROR, &so_error, &so_error_len);
            if (ret < 0 || (so_error != 0
               && so_error != EWOULDBLOCK
               && so_error != ECONNREFUSED
               && so_error != EINPROGRESS
               )
            ) {
               ch_error(channel, "channel_connect: Connect failed with errno %d", so_error);
               PERROR(_(e_cannot_connect_to_port));
               sock_close(sd);
               return -1;
            } ei (errno == ECONNREFUSED) {
               ch_error(channel, "channel_connect: Connection refused");
               sock_close(sd);
               return -1;
            }
         }

         if ((pollFd.revents & POLLOUT) != 0 && so_error == 0)
            //Did not detect an error, connection is established.
            break;

         timespec_get(OUT &end_tv, TIME_UTC);
         elapsed_msec = (end_tv.tv_sec - start_tv.tv_sec) * 1000
                + (end_tv.tv_nsec - start_tv.tv_nsec) / 1000000;
      }

      if (*waittime > 1 && elapsed_msec < *waittime) {
         //The port isn't ready but we also didn't get an error. This happens when the server 
         //didn't open the socket yet. poll() may return early, wait until the remaining
         //"waitnow"  and try again.
         waitnowMs -= elapsed_msec;
         *waittime -= elapsed_msec;
         if (waitnowMs > 0) {
            mch_delay((Long)waitnowMs, MCH_DELAY_IGNOREINPUT);
            ui_breakcheck();
            *waittime -= waitnowMs;
         }
         if (!gotInterruptG) {
            if (*waittime <= 0)
               //give it one more try
               *waittime = 1;
            continue;
         }
         //we were interrupted, behave as if timed out
      }

      //We timed out.
      ch_error(channel, "Connection timed out");
      sock_close(sd);
      return -1;
   }

   if (*waittime >= 0) {
      (void)fcntl(sd, F_SETFL, 0);
   }

   return sd;
}

//Open a socket channel to the Unix socket at "path".
//Return the channel for success. NULL for failure.
private Channel*
channel_open_unix(CS path) {
   Unt path_len = STRLEN(path);
   SockAddrUn server;

   if (*path == ZERO || path_len >= sizeof(server.sun_path)) {
      showErrFmtMsg(_(e_invalid_argument_str), path);
      return NULL;
   }

   Channel* channel = add_channel();
   if (!channel) {
      ch_error(NULL, "Cannot allocate channel.");
      return NULL;
   }

   CLEAR_FIELD(server);
   server.sun_family = AF_UNIX;
   STRNCPY(server.sun_path, path, sizeof(server.sun_path) - 1);

   ch_log(channel, "Trying to connect to %s", path);

   Unt server_len = offsetof(SockAddrUn, sun_path) + path_len + 1;
   int waittime = -1;
   int sd = channel_connect(channel, (SockAddr *)&server, (int)server_len, &waittime);
   if (sd < 0) {
      channel_free(channel);
      return NULL;
   }

   ch_log(channel, "Connection made");

   channel->fds[PART_SOCK].fd = (Socket)sd;
   channel->socketName = copyStr((CS)path);
   channel->ch_to_be_closed |= (1U << PART_SOCK);

   return channel;
}

private void
setCallback(Callback* cbp, Callback* callback) {
   evFreeCallback(cbp);

   if (callback->name && *callback->name != ZERO)
      evCopyCallback(cbp, callback);
   else
      cbp->name = NULL;
}

//Prepare book "book" for writing channel output to.
private void
prepareBookForWriting(Book* book) {
   Book* curBookSaved = curBook;

   optsCopyToBook(book, BCO_ENTER);
   curBook = book;
   optChangeAndReportError(S"booktype", optStr("nofile"), SET_LOCAL);
   optChangeAndReportError(S"bufhidden", optStr("hide"), SET_LOCAL);
   if (!curBook->mem.mfile)
      ml_open(curBook);
   curBook = curBookSaved;
}

//Find a buffer matching "name" or create a new one.
//Return NULL if there is something very wrong (error already reported).
private Book*
chaFindBook(CS name, int err, int msg) {
   Book* book = NULL;
   Book* curBookSaved = curBook;

   if (name && *name != ZERO) {
      book = booklistFindName(name);
      if (!book)
         book = booklistFindByNameExpandingLinks(name);
   }

   if (book)
      return book;

   book = bookNew(!name || *name == ZERO ? NULL : name, NULL, (LineNr)0, BLN_LISTED | BLN_NEW);
   if (!book)
      return NULL;
   prepareBookForWriting(book);

   curBook = book;
   if (msg) {
      ml_replace(1, (CS)(err ? "Reading from channel error..."
          : "Reading from channel output..."), true);
   } 
   changed_bytes(1, 0);
   curBook = curBookSaved;

   return book;
}

//Set various properties from an "opt" argument.
private void
channel_set_options(Channel* channel, JobOptions* opt) {
   ChannelFdKind part;
   if ((opt->set & JO_MODE) != 0) {
      for (part = PART_SOCK; part < PART_COUNT; ++part)
         channel->fds[part].ch_mode = opt->mode;
   } 
   if (opt->set & JO_IN_MODE)
      channel->fds[PART_IN].ch_mode = opt->jo_in_mode;
   if (opt->set & JO_OUT_MODE)
      channel->fds[PART_OUT].ch_mode = opt->jo_out_mode;
   if (opt->set & JO_ERR_MODE)
      channel->fds[PART_ERR].ch_mode = opt->jo_err_mode;
   channel->ch_nonblock = opt->jo_noblock;

   if (opt->set & JO_TIMEOUT) {
      for (part = PART_SOCK; part < PART_COUNT; ++part)
         channel->fds[part].ch_timeout = opt->jo_timeout;
   } 
   if (opt->set & JO_OUT_TIMEOUT)
      channel->fds[PART_OUT].ch_timeout = opt->jo_out_timeout;
   if (opt->set & JO_ERR_TIMEOUT)
      channel->fds[PART_ERR].ch_timeout = opt->jo_err_timeout;
   if (opt->set & JO_BLOCK_WRITE)
      channel->fds[PART_IN].ch_block_write = 1;

   if (opt->set & JO_CALLBACK)
      setCallback(&channel->ch_callback, &opt->jo_callback);
   if (opt->outNativeCb) {
      channel->fds[PART_OUT].nativeCb = opt->outNativeCb;
   } ei(opt->set & JO_OUT_CALLBACK) {
      setCallback(&channel->fds[PART_OUT].ch_callback, &opt->jo_out_cb);
   }
   
   if (opt->errNativeCb) {
      channel->fds[PART_ERR].nativeCb = opt->errNativeCb;
   } ei(opt->set & JO_ERR_CALLBACK) {
      setCallback(&channel->fds[PART_ERR].ch_callback, &opt->jo_err_cb);
   }
   
   if (opt->set & JO_CLOSE_CALLBACK)
      setCallback(&channel->ch_close_cb, &opt->closeCb);
   channel->ch_drop_never = opt->dropNever;

   if ((opt->set & JO_OUT_IO) && opt->ioMode[PART_OUT] == JIO_BUFFER) {
      Book *book;

      //writing output to a buffer. Default mode is NL.
      if (!(opt->set & JO_OUT_MODE))
         channel->fds[PART_OUT].ch_mode = CH_MODE_NL;
      if (opt->set & JO_OUT_BUF) {
         book = bookFindFileByBookNr(opt->ioText[PART_OUT]);
         if (book == NULL)
            showErrFmtMsg(_(e_book_nr_does_not_exist), (long)opt->ioText[PART_OUT]);
      } else {
         int msg = true;

         if (opt->set1 & JO2_OUT_MSG)
            msg = opt->jo_message[PART_OUT];
         book = chaFindBook(opt->name[PART_OUT], false, msg);
      }
      if (book) {
         if (opt->set & JO_OUT_MODIFIABLE)
            channel->fds[PART_OUT].ch_nomodifiable = !opt->jo_modifiable[PART_OUT];

         if ((IMMUTABLE) && !channel->fds[PART_OUT].ch_nomodifiable) {
            emsg(_(e_cannot_make_changes_modifiable_is_off));
         } else {
            ch_log(channel, "writing out to book '%s'", book->fullFileName);
            bookStoreInRef(OUT &channel->fds[PART_OUT].bookref, book);
            //if the buffer was deleted or unloaded resurrect it
            if (bookNoMemfile(book))
               prepareBookForWriting(book);
         }
      }
    }

   if ((opt->set & JO_ERR_IO) 
         && (opt->ioMode[PART_ERR] == JIO_BUFFER
          || (opt->ioMode[PART_ERR] == JIO_OUT && (opt->set & JO_OUT_IO)
                      && opt->ioMode[PART_OUT] == JIO_BUFFER))
   ) {
      Book* book;

      //writing err to a buffer. Default mode is NL.
      if (!(opt->set & JO_ERR_MODE))
         channel->fds[PART_ERR].ch_mode = CH_MODE_NL;
      if (opt->ioMode[PART_ERR] == JIO_OUT)
         book = channel->fds[PART_OUT].bookref.c;
      ei (opt->set & JO_ERR_BUF) {
         book = bookFindFileByBookNr(opt->ioText[PART_ERR]);
         if (!book)
            showErrFmtMsg(_(e_book_nr_does_not_exist), (long)opt->ioText[PART_ERR]);
      } else {
         int msg = true;

         if (opt->set1 & JO2_ERR_MSG)
            msg = opt->jo_message[PART_ERR];
         book = chaFindBook(opt->name[PART_ERR], true, msg);
      }
      if (book) {
         if (opt->set & JO_ERR_MODIFIABLE)
            channel->fds[PART_ERR].ch_nomodifiable = !opt->jo_modifiable[PART_ERR];
         if ((IMMUTABLE) && !channel->fds[PART_ERR].ch_nomodifiable) {
            emsg(_(e_cannot_make_changes_modifiable_is_off));
         } else {
            ch_log(channel, "writing err to book '%s'", book->fullFileName);
            bookStoreInRef(OUT &channel->fds[PART_ERR].bookref, book);
            //if the book was deleted or unloaded, resurrect it
            if (bookNoMemfile(book))
                prepareBookForWriting(book);
         }
      }
   }

   channel->fds[PART_OUT].ch_io = opt->ioMode[PART_OUT];
   channel->fds[PART_ERR].ch_io = opt->ioMode[PART_ERR];
   channel->fds[PART_IN].ch_io = opt->ioMode[PART_IN];
}

//Implement ch_open().
private Channel *
channel_open_func(Arr(Var) argvars) {
   JobOptions opt;

   CS address = tv_get_string(&argvars[0]);
   if (argvars[1].tag != VAR_UNKNOWN && check_for_nonnull_dict_arg(argvars, 1) == FAIL)
      return NULL;

   if (*address == ZERO) {
      showErrFmtMsg(_(e_invalid_argument_str), address);
      return NULL;
   }

   if (STRNCMP(address, "unix:", 5) == 0) {
      address += 5;
   } else {
      showErrFmtMsg(_(e_invalid_argument_str), address);
      return null;
   } 

   //parse options
   CLEAR_POINTER(&opt);
   opt.mode = CH_MODE_JSON;
   opt.jo_timeout = 2000;
   if (get_job_options(&argvars[1], OUT &opt, JO_MODE_ALL + JO_CB_ALL + JO_TIMEOUT_ALL, 0) == FAIL)
      goto theend;
   if (opt.jo_timeout < 0) {
      emsg(_(e_invalid_argument));
      goto theend;
   }

   Channel* channel = channel_open_unix(address);
   if (channel) {
      opt.set = JO_ALL;
      channel_set_options(channel, &opt);
   }
theend:
   free_job_options(&opt);
   return channel;
}

pub void
ch_close_part(Channel *channel, ChannelFdKind part) {
   Socket *fd = &channel->fds[part].fd;

   if (*fd == INVALID_FD)
      return;

   if (part == PART_SOCK)
      sock_close(*fd);
   else {
      //When using a pty the same FD is set on multiple parts, only
      //close it when the last reference is closed.
      if ((part == PART_IN || channel->fds[PART_IN].fd != *fd)
         && (part == PART_OUT || channel->fds[PART_OUT].fd != *fd)
         && (part == PART_ERR || channel->fds[PART_ERR].fd != *fd)
      ){
          fd_close(*fd);
      }
   }
   *fd = INVALID_FD;

   //channel is closed, may want to end the job if it was the last
   channel->ch_to_be_closed &= ~(1U << part);
}

pub void
channel_set_pipes(Channel *channel, Socket in, Socket out, Socket err) {
   if (in != INVALID_FD) {
      ch_close_part(channel, PART_IN);
      channel->fds[PART_IN].fd = in;
      //Do not end the job when all output channels are closed, wait until the job ended.
      if (mch_isatty(in))
         channel->ch_to_be_closed |= (1U << PART_IN);
   }
   if (out != INVALID_FD) {
      ch_close_part(channel, PART_OUT);
      channel->fds[PART_OUT].fd = out;
      channel->ch_to_be_closed |= (1U << PART_OUT);
   }
   if (err != INVALID_FD) {
      ch_close_part(channel, PART_ERR);
      channel->fds[PART_ERR].fd = err;
      channel->ch_to_be_closed |= (1U << PART_ERR);
   }
}

//Set the job the channel is associated with and associated options.
//This does not keep a refcount, when the job is freed job is cleared.
pub void
channel_set_job(Channel* channel, Job* job, JobOptions* options) {
   channel->job = job;
   channel_set_options(channel, options);

   if (!job->inBook)
      return;

   ChannelFd* intake = &channel->fds[PART_IN];

   bookStoreInRef(OUT &intake->bookref, job->inBook);
   ch_log(channel, "reading from buffer '%s'", (char *)intake->bookref.c->fullFileName);
   if (options->set & JO_IN_TOP) {
      if (options->jo_in_top == 0 && !(options->set & JO_IN_BOT)) {
         //Special mode: send last-but-one line when appending a line to the buffer.
         intake->bookref.c->writeToChannel = true;
         intake->ch_buf_append = true;
         intake->ch_buf_top =
         intake->bookref.c->mem.lineCount + 1;
      } else
         intake->ch_buf_top = options->jo_in_top;
   } else
      intake->ch_buf_top = 1;
   if (options->set & JO_IN_BOT)
      intake->ch_buf_bot = options->jo_in_bot;
   else
      intake->ch_buf_bot = intake->bookref.c->mem.lineCount;
}

//Set the callback for "channel"/"part" for the response with "id".
private void
channel_set_req_callback(Channel* channel, ChannelFdKind part, Callback* callback, int id) {
   CbNode* head = &channel->fds[part].ch_cb_head;
   CbNode* item = ALLOC_ONE(CbNode);

   if (!item)
      return;

   evCopyCallback(&item->cq_callback, callback);
   item->cq_seq_nr = id;
   item->cq_prev = head->cq_prev;
   head->cq_prev = item;
   item->cq_next = NULL;
   if (!item->cq_prev)
      head->cq_next = item;
   else
      item->cq_prev->cq_next = item;
}

private void
write_buf_line(Book* book, LineNr lnum, Channel* channel) {
   CS line = memGetLine(book, lnum, false);
   int len = memGetBookLen(book, lnum);
   int i;

   //Need to make a copy to be able to append a NL.
   CS p = alloc(len + 2);
   memcpy((char *)p, (char *)line, len);

   if (channel->writeTextMode)
      p[len] = ENTER;
   else {
      for (i = 0; i < len; ++i) {
         if (p[i] == NL)
            p[i] = ZERO;
      } 

      p[len] = NL;
   }
   p[len + 1] = ZERO;
   channel_send(channel, PART_IN, p, len + 1, "write_buf_line");
   eeglFree(p);
}

//true if "channel" can be written to. * false if the input is closed or the write would block.
private int
can_write_buf_line(Channel* channel) {
   ChannelFd* intake = &channel->fds[PART_IN];

   if (intake->fd == INVALID_FD)
      return false;  //pipe was closed

   //for testing: block every other attempt to write
   if (intake->ch_block_write == 1)
      intake->ch_block_write = -1;
   ei (intake->ch_block_write == -1)
      intake->ch_block_write = 1;

   int ret;

   PollFd channelFd = (PollFd){intake->fd, POLLIN, 0};
   for (;;) {
      ret = poll(&channelFd, 1, 100);
      SOCK_ERRNO;
      if (ret == -1 && errno == EINTR)
         continue;
      if (ret <= 0 || intake->ch_block_write == 1) {
         if (ret > 0)
            ch_log(channel, "FAKED Input not ready for writing");
         else
            ch_log(channel, "Input not ready for writing");
         return false;
      }
      break;
   }
   return true;
}

//Write any book lines to the input channel.
pub void
channel_write_in(Channel* channel) {
   ChannelFd* intake = &channel->fds[PART_IN];
   Book* book = intake->bookref.c;

   if (!book || intake->ch_buf_append)
      return;
   if (!bookRefValid(&intake->bookref) || !book->mem.mfile) {
      //book was wiped out or unloaded
      ch_log(channel, "input book has been wiped out");
      intake->bookref.c = NULL;
      return;
   }

   int written = 0;
   LineNr lnum;
   for (lnum = intake->ch_buf_top; 
        lnum <= intake->ch_buf_bot && lnum <= book->mem.lineCount; 
        ++lnum
   ) {
      if (!can_write_buf_line(channel))
         break;
      write_buf_line(book, lnum, channel);
      ++written;
   }

   if (written == 1)
      ch_log(channel, "written line %d to channel", (int)lnum - 1);
   ei (written > 1)
      ch_log(channel, "written %d lines to channel", written);

   intake->ch_buf_top = lnum;
   if (lnum > book->mem.lineCount || lnum > intake->ch_buf_bot) {
      //Send CTRL-D to close stdin
      if (channel->job)
         term_send_eof(channel);

      //Writing is done, no longer need the book.
      intake->bookref.c = NULL;
      ch_log(channel, "Finished writing all lines to channel");

      //Close the pipe/socket, so that the other side gets EOF.
      ch_close_part(channel, PART_IN);
   } else
      ch_log(channel, "Still %ld more lines to write", (long)(book->mem.lineCount - lnum + 1));
}

//Handle book "book" being freed, remove it from any channels.
pub void
chaFreeBook(Book* book) {
   Channel* channel;

   FOR_ALL_CHANNELS(channel) {
      for (ChannelFdKind part = PART_SOCK; part < PART_COUNT; ++part) {
         ChannelFd* fds = &channel->fds[part];

         if (fds->bookref.c == book) {
            ch_log(channel, "%s buffer has been wiped out", chanFdNames[part]);
            fds->bookref.c = NULL;
         }
      }
   } 
}

//Write any lines waiting to be written to "channel".
private void
channel_write_input(Channel* channel) {
   ChannelFd* intake = &channel->fds[PART_IN];

   if (intake->ch_writeque.next)
      channel_send(channel, PART_IN, S"", 0, "channel_write_input");
   ei (intake->bookref.c) {
      if (intake->ch_buf_append)
         channel_write_new_lines(intake->bookref.c);
      else
         channel_write_in(channel);
    }
}

//Write any lines waiting to be written to a channel.
pub void
channel_write_any_lines(void) {
   Channel* channel;
   FOR_ALL_CHANNELS(channel) {
      channel_write_input(channel);
   } 
}

//Write appended lines above the last one in "book" to the channel.
pub void
channel_write_new_lines(Book* book) {
   Channel* channel;
   int found_one = false;

   //There could be more than one channel for the buffer, loop over all of them.
   FOR_ALL_CHANNELS(channel) {
      ChannelFd* intake = &channel->fds[PART_IN];
      LineNr    lnum;
      int       written = 0;

      if (intake->bookref.c == book && intake->ch_buf_append) {
         if (intake->fd == INVALID_FD)
            continue;  //pipe was closed
         found_one = true;
         for (lnum = intake->ch_buf_bot; lnum < book->mem.lineCount; ++lnum) {
            if (!can_write_buf_line(channel))
               break;
            write_buf_line(book, lnum, channel);
            ++written;
         }

         if (written == 1)
            ch_log(channel, "written line %d to channel", (int)lnum - 1);
         ei (written > 1)
            ch_log(channel, "written %d lines to channel", written);
         if (lnum < book->mem.lineCount)
            ch_log(channel, "Still %ld more lines to write", (long)(book->mem.lineCount - lnum));

         intake->ch_buf_bot = lnum;
      }
   }
   if (!found_one)
      book->writeToChannel = false;
}

//Invoke the "callback" on channel "channel". This does not redraw but sets channel_need_redraw;
private void
invoke_callback(Channel* channel, Callback* callback, Var* argv) {
   if (safe_to_invoke_callback == 0)
      internalErrMsg(S"Invoking callback when it is not safe");

   argv[0].tag = VAR_CHANNEL;
   argv[0].channel = channel;

   Var returnVar;
   call_callback(callback, -1, OUT &returnVar, 2, argv);
   clearVar(&returnVar);
   channel_need_redraw = true;
}

//Return the first node from "channel"/"part" without removing it. Return NULL if there is nothing.
pub ReadChunk *
channel_peek(Channel *channel, ChannelFdKind part) {
    ReadChunk *head = &channel->fds[part].head;

    return head->next;
}

//Return a pointer to the first NL in "node".
//Skip over ZERO characters. Return NULL if there is no NL.
pub CS
channel_first_nl(ReadChunk* node) {
   CS buffer = node->c;
   for (Unt i = 0; i < node->len; ++i) {
      if (buffer[i] == NL)
         return buffer + i;
   } 
   return NULL;
}

//Return the first buffer from channel "channel"/"part" and remove it.
//The caller owns it. Return NULL if there is nothing.
private CS
channel_get(Channel* channel, ChannelFdKind part, int *outlen) {
   ReadChunk* head = &channel->fds[part].head;
   ReadChunk* node = head->next;

   if (!node)
      return NULL;
   if (outlen)
      *outlen += node->len;
   //dispose of the node but keep the buffer
   CS p = node->c;
   head->next = node->next;
   if (!node->next)
      head->prev = NULL;
   else
      node->next->prev = NULL;
   eeglFree(node);
   return p;
}

//Return the whole buffer contents concatenated for "channel"/"part". Replace ZERO bytes with NL.
private CS
channel_get_all(Channel *channel, ChannelFdKind part, int *outlen) {
   ReadChunk* head = &channel->fds[part].head;
   ReadChunk* node;
   Ulong  len = 0;

   //Concatenate everything into one buffer.
   for (node = head->next; node; node = node->next)
      len += node->len;
   CS res = alloc(len + 1);
   CS p = res;
   for (node = head->next; node; node = node->next) {
      MEMMOVE(p, node->c, node->len);
      p += node->len;
   }
   *p = ZERO;

   //Free all buffers
   do {
      p = channel_get(channel, part, NULL);
      eeglFree(p);
   } while (p);

   if (outlen) {
      //Returning the length, keep ZERO characters.
      *outlen += len;
      return res;
   }

   //Turn all ZERO into newlines, so that the result can be used as a string.
   p = res;
   while (p < res + len) {
      if (*p == ZERO)
         *p = NL;
      ei (*p == 0x1b) {
         //crush the escape sequence OSC 0/1/2: ESC ]0;
         if (p + 3 < res + len
            && p[1] == ']'
            && (p[2] == '0' || p[2] == '1' || p[2] == '2')
            && p[3] == ';'
         ) {
            //'\a' becomes a NL
            while (p < res + (len - 1) && *p != '\a')
               ++p;
            //BEL is zero width characters, suppress display mistake
            //ConPTY (after 10.0.18317) requires advance checking
            if (p[-1] == ZERO)
               p[-1] = 0x07;
         }
      }
      ++p;
   }

    return res;
}

//Consume "len" bytes from the head of "node". Caller must check these bytes are available.
pub void
channel_consume(Channel *channel, ChannelFdKind part, int len) {
   ReadChunk *head = &channel->fds[part].head;
   ReadChunk *node = head->next;
   CS buf = node->c;

   MEMMOVE(buf, buf + len, node->len - len);
   node->len -= len;
   node->c[node->len] = ZERO;
}

//Collapses the first and second buffer for "channel"/"part". Return FAIL if nothing was done.
//When "want_nl" is true collapse more buffers until a NL is found. When the channel part mode 
//is "lsp", collapse all the buffers as the http header and the JSON content can be present in 
//multiple buffers.
pub int
channel_collapse(Channel *channel, ChannelFdKind part, int want_nl) {
   ChannelMode   mode = channel->fds[part].ch_mode;
   ReadChunk* head = &channel->fds[part].head;
   ReadChunk* node = head->next;
   ReadChunk* n;

   if (!node || !node->next)
      return FAIL;

   ReadChunk* last_node = node->next;
   Ulong len = node->len + last_node->len;
   if (want_nl || mode == CH_MODE_LSP) {
      while (last_node->next && (mode == CH_MODE_LSP || channel_first_nl(last_node) == NULL)) {
          last_node = last_node->next;
          len += last_node->len;
      }
   } 
   CS newbuf = alloc(len + 1);
   CS p = newbuf;
   MEMMOVE(p, node->c, node->len);
   p += node->len;
   eeglFree(node->c);
   node->c = newbuf;
   for (n = node; n != last_node; ) {
      n = n->next;
      MEMMOVE(p, n->c, n->len);
      p += n->len;
      eeglFree(n->c);
   }
   *p = ZERO;
   node->len = (Ulong)(p - newbuf);

   //dispose of the collapsed nodes and their buffers
   for (n = node->next; n != last_node; ) {
      n = n->next;
      eeglFree(n->prev);
   }
   node->next = last_node->next;
   if (!last_node->next)
      head->prev = node;
   else
      last_node->next->prev = node;
   eeglFree(last_node);
   return OK;
}

//Store "buf[len]" on "channel"/"part".
//When "prepend" is true put in front, otherwise append at the end. Return OK or FAIL.
private int
saveMsg(Channel* channel, ChannelFdKind part, CS msg, int len, int prepend, CS logLead) {
   ReadChunk *head = &channel->fds[part].head;
   Byte  *p;
   int       i;
   ReadChunk* node = ALLOC_ONE(ReadChunk);
   //A ZERO is added at the end, because netbeans code expects that.
   //Otherwise a ZERO may appear inside the text.
   node->c = alloc(len + 1);

   if (channel->fds[part].ch_mode == CH_MODE_NL) {
      //Drop any CR before a newline.
      p = node->c;
      for (i = 0; i < len; ++i) {
         if (msg[i] != ENTER || i + 1 >= len || msg[i + 1] != NL)
            *p++ = msg[i];
      } 
      *p = ZERO;
      node->len = (Ulong)(p - node->c);
   } else {
      MEMMOVE(node->c, msg, len);
      node->c[len] = ZERO;
      node->len = (Ulong)len;
   }

   if (prepend) {
      //prepend node to the head of the queue
      node->next = head->next;
      node->prev = NULL;
      if (head->next == NULL)
         head->prev = node;
      else
         head->next->prev = node;
      head->next = node;
   } else {
      //append node to the tail of the queue
      node->next = NULL;
      node->prev = head->prev;
      if (head->prev == NULL)
         head->next = node;
      else
         head->prev->next = node;
      head->prev = node;
   }

   if (ch_log_active() && logLead)
      ch_log_literal(logLead, channel, part, OUT (Text){msg, len});

   return OK;
}

//Try to fill the buffer of "reader". Returns false when nothing was added.
private int
channel_fill(JsReader* reader) {
   Channel* channel = (Channel *)reader->cookie;
   ChannelFdKind part = reader->cookieArg;
   CS next = channel_get(channel, part, NULL);
   CS p;

   if (!next)
      return false;

   int keeplen = reader->js_end - reader->js_buf;
   if (keeplen > 0) {
      //Prepend unused text.
      int addlen = (int)STRLEN(next);
      p = alloc(keeplen + addlen + 1);
      MEMMOVE(p, reader->js_buf, keeplen);
      MEMMOVE(p + keeplen, next, addlen + 1);
      eeglFree(next);
      next = p;
   }

    eeglFree(reader->js_buf);
    reader->js_buf = next;
    return true;
}

//Process the HTTP header in a Language Server Protocol (LSP) message.
//
//The message format is described in the LSP specification:
//https://microsoft.github.io/language-server-protocol/specification
//
//It has the following two fields:
//
//  Content-Length: ...
//  Content-Type: application/vscode-jsonrpc; charset=utf-8
//
//Each field ends with "\r\n". The header ends with an additional "\r\n".
//
//Return OK if a valid header is received and FAIL if some fields in the
//header are not correct. Return MAYBE if a partial header is received and
//need to wait for more data to arrive.
private int
channel_process_lsp_http_hdr(JsReader* reader) {
   Byte   *line_start;
   Byte   *p;
   Unt   hdr_len;
   int      payload_len = -1;
   Unt   jsbuf_len;

   //We find the end once, to avoid calling strlen() many times.
   jsbuf_len = (Unt)STRLEN(reader->js_buf);
   reader->js_end = reader->js_buf + jsbuf_len;

   p = reader->js_buf;

   //Process each line in the header till an empty line is read (header separator).
   while (true) {
      line_start = p;
      while (*p != ZERO && *p != '\n')
         p++;
      if (*p == ZERO)         //partial header
         return MAYBE;
      p++;

      //process the content length field (if present)
      if ((p - line_start > 16) && STRNICMP(line_start, "Content-Length: ", 16) == 0) {
         errno = 0;
         payload_len = strtol((char *)line_start + 16, NULL, 10);
         if (errno == ERANGE || payload_len < 0)
            //invalid length, discard the payload
            return FAIL;
      }

      if ((p - line_start) == 2 && line_start[0] == '\r' &&
         line_start[1] == '\n')
         //reached the empty line
         break;
   }

   if (payload_len == -1)
      //Content-Length field is not present in the header
      return FAIL;

   hdr_len = p - reader->js_buf;

    //if the entire payload is not received, wait for more data to arrive
   if (jsbuf_len < hdr_len + payload_len)
      return MAYBE;

   reader->js_used += hdr_len;
   //recalculate the end based on the length read from the header.
   reader->js_end = reader->js_buf + hdr_len + payload_len;

   return OK;
}

//Use the read buffer of "channel"/"part" and parse a JSON message that is
//complete.  The messages are added to the queue. Return true if there is more to read.
private int
channel_parse_json(Channel* channel, ChannelFdKind part) {
   Var   listtv;
   ChannelFd   *chanpart = &channel->fds[part];
   JsonQ   *head = &chanpart->ch_json_head;
   int      status = OK;
   int      ret;

   if (channel_peek(channel, part) == NULL)
      return false;

   JsReader reader;
   reader.js_buf = channel_get(channel, part, NULL);
   reader.js_used = 0;
   reader.js_fill = channel_fill;
   reader.cookie = channel;
   reader.cookieArg = part;

   if (chanpart->ch_mode == CH_MODE_LSP)
      status = channel_process_lsp_http_hdr(&reader);

   //When a message is incomplete we wait for a short while for more to
   //arrive.  After the delay drop the input, otherwise a truncated string
   //or list will make us hang.
   //Do not generate error messages, they will be written in a channel log.
   if (status == OK) {
      ++emsg_silent;
      status = json_decode(OUT &listtv, &reader);
      --emsg_silent;
   }
   if (status == OK) {
      //Only accept the response when it is a list with at least two items.
      if (chanpart->ch_mode == CH_MODE_LSP && listtv.tag != VAR_BAG) {
         ch_error(channel, "Did not receive a LSP dict, discarding");
         clearVar(&listtv);
      }
      ei (chanpart->ch_mode != CH_MODE_LSP && (listtv.tag != VAR_LIST || listtv.list->len < 2)) {
         if (listtv.tag != VAR_LIST)
            ch_error(channel, "Did not receive a list, discarding");
         else
            ch_error(channel, "Expected list with two items, got %d", listtv.list->len);
         clearVar(&listtv);
      } else {
         JsonQ* item = ALLOC_ONE(JsonQ);
         if (item == NULL)
            clearVar(&listtv);
         else {
            item->jq_no_callback = false;
            item->jq_value = allocVar();
            if (item->jq_value == NULL) {
               eeglFree(item);
               clearVar(&listtv);
            } else {
               *item->jq_value = listtv;
               item->jq_prev = head->jq_prev;
               head->jq_prev = item;
               item->jq_next = NULL;
               if (item->jq_prev == NULL)
                  head->jq_next = item;
               else
                  item->jq_prev->jq_next = item;
            }
          }
      }
   }

   if (status == OK)
      chanpart->ch_wait_len = 0;
   ei (status == MAYBE) {
      Unt buflen = STRLEN(reader.js_buf);

      if (chanpart->ch_wait_len < buflen) {
         //First time encountering incomplete message or after receiving
         //more (but still incomplete): set a deadline of 100 msec.
         ch_log(channel,
            "Incomplete message (%d bytes) - wait 100 msec for more",
            (int)buflen);
         reader.js_used = 0;
         chanpart->ch_wait_len = buflen;
         timespec_get(OUT &chanpart->deadline, TIME_UTC);
         chanpart->deadline.tv_nsec += 100 * 1000000;
         if (chanpart->deadline.tv_nsec > 1000 * 1000000) {
           chanpart->deadline.tv_nsec -= 1000 * 1000000;
           ++chanpart->deadline.tv_sec;
         }
      } else {
         int timeout;
         {
         TimeSpec now_tv;

         timespec_get(OUT &now_tv, TIME_UTC);
         timeout = now_tv.tv_sec > chanpart->deadline.tv_sec
               || (now_tv.tv_sec == chanpart->deadline.tv_sec
               && now_tv.tv_nsec > chanpart->deadline.tv_nsec);
         }
         if (timeout) {
            status = FAIL;
            chanpart->ch_wait_len = 0;
            ch_log(channel, "timed out");
         } else {
            reader.js_used = 0;
            ch_log(channel, "still waiting on incomplete message");
         }
      }
   }

   if (status == FAIL) {
      ch_error(channel, "Decoding failed - discarding input");
      ret = false;
      chanpart->ch_wait_len = 0;
   } ei (reader.js_buf[reader.js_used] != ZERO) {
      //Put the unread part back into the channel.
      saveMsg(channel, part, reader.js_buf + reader.js_used,
            (int)(reader.js_end - reader.js_buf) - reader.js_used, true, NULL);
      ret = status == MAYBE ? false: true;
   } else
      ret = false;

   eeglFree(reader.js_buf);
   return ret;
}

//Remove "node" from the queue that it is in.  Does not free it.
private void
remove_cb_node(CbNode* head, CbNode* node) {
   if (node->cq_prev == NULL)
      head->cq_next = node->cq_next;
   else
      node->cq_prev->cq_next = node->cq_next;
   if (node->cq_next == NULL)
      head->cq_prev = node->cq_prev;
   else
      node->cq_next->cq_prev = node->cq_prev;
}

//Remove "node" from the queue that it is in and free it.
//Caller should have freed or used node->jq_value.
private void
remove_json_node(JsonQ* head, JsonQ* node) {
   if (!node->jq_prev)
      head->jq_next = node->jq_next;
   else
      node->jq_prev->jq_next = node->jq_next;
   if (!node->jq_next)
      head->jq_prev = node->jq_prev;
   else
      node->jq_next->jq_prev = node->jq_prev;
   eeglFree(node);
}

//Add "id" to the list of JSON message IDs we are waiting on.
private void
channel_add_block_id(ChannelFd* chanpart, int id) {
   ArrayList* gap = &chanpart->ch_block_ids;

   if (gap->ga_growsize == 0)
      ga_init2(gap, sizeof(int), 10);
   if (ga_grow(gap, 1) == OK) {
      ((int *)gap->c)[gap->len] = id;
      ++gap->len;
   }
}

//Remove "id" from the list of JSON message IDs we are waiting on.
private void
channel_remove_block_id(ChannelFd* chanpart, int id) {
   ArrayList* gap = &chanpart->ch_block_ids;

   for (int i = 0; i < gap->len; ++i) {
      if (((int *)gap->c)[i] == id) {
         --gap->len;
         if (i < gap->len) {
            int *p = ((int *)gap->c) + i;
            MEMMOVE(p, p + 1, (gap->len - i) * sizeof(int));
         }
         return;
      }
   } 
   internalErrFmtMsg("channel_remove_block_id(): cannot find id %d", id);
}

//Return true if "id" is in the list of JSON message IDs we are waiting on.
private int
channel_has_block_id(ChannelFd* chanpart, int id) {
   ArrayList   *gap = &chanpart->ch_block_ids;
   for (int i = 0; i < gap->len; ++i) {
      if (((int *)gap->c)[i] == id)
          return true;
   } 
   return false;
}

//Get a message from the JSON queue for channel "channel". When "id" is positive it must match 
//the first number in the list. When "id" is zero or negative jut get the first message. But not 
//one in the ch_block_ids list. When "without_callback" is true also get messages that were 
//pushed back. Return OK when found and return the value in "returnVar". FAIL otherwise.
private int
channel_get_json(
   Channel   *channel,
   ChannelFdKind   part,
   int       id,
   int       without_callback,
   Var    **returnVar
) {
   JsonQ* head = &channel->fds[part].ch_json_head;
   JsonQ* item = head->jq_next;

   while (item) {
      List* l;
      Var* tv;

      if (channel->fds[part].ch_mode != CH_MODE_LSP) {
         l = item->jq_value->list;
         CHECK_LIST_MATERIALIZE(l);
         tv = &l->first->c;
      } else {
         //LSP message payload is a JSON-RPC dict. For RPC requests and responses, the 'id' 
         //item will be present. For notifications, it will not be present.
         if (id > 0) {
            if (item->jq_value->tag != VAR_BAG)
               goto nextitem;
            Bag* d = item->jq_value->bag;
            if (!d)
               goto nextitem;
            //When looking for a response message from the LSP server,
            //ignore new LSP request and notification messages.  LSP
            //request and notification messages have the "method" field in
            //the header and the response messages do not have this field.
            if (bagHasKey(d, tConst("method")))
                goto nextitem;
            DictItem* di = bagFind(d, tConst("id"));
            if (!di)
               goto nextitem;
            tv = &di->c;
         } else
            tv = item->jq_value;
      }

      if ((without_callback || !item->jq_no_callback)
          && ((id > 0 && tv->tag == VAR_NUMBER && tv->number == id)
            || (id <= 0 && (tv->tag != VAR_NUMBER
             || tv->number == 0
             || !channel_has_block_id( &channel->fds[part], tv->number))))
      ) {
         *returnVar = item->jq_value;
         if (tv->tag == VAR_NUMBER)
            ch_log(channel, "Getting JSON message %ld", (long)tv->number);
         remove_json_node(head, item);
         return OK;
      }
   nextitem:
      item = item->jq_next;
   }
   return FAIL;
}

//Put back "returnVar" into the JSON queue, there was no callback for it.
//Take over the values in "returnVar".
private void
channel_push_json(Channel* channel, ChannelFdKind part, Var* returnVar) {
   JsonQ* head = &channel->fds[part].ch_json_head;
   JsonQ* item = head->jq_next;

   if (head->jq_prev && head->jq_prev->jq_no_callback)
      //last item was pushed back, append to the end
      item = NULL;
   else while (item && item->jq_no_callback)
      //append after the last item that was pushed back
      item = item->jq_next;

   JsonQ* newitem = ALLOC_ONE(JsonQ);
   newitem->jq_value = allocVar();

   newitem->jq_no_callback = false;
   *newitem->jq_value = *returnVar;
   if (!item) {
      //append to the end
      newitem->jq_prev = head->jq_prev;
      head->jq_prev = newitem;
      newitem->jq_next = NULL;
      if (!newitem->jq_prev)
         head->jq_next = newitem;
      else
         newitem->jq_prev->jq_next = newitem;
   } else {
      //append after "item"
      newitem->jq_prev = item;
      newitem->jq_next = item->jq_next;
      item->jq_next = newitem;
      if (!newitem->jq_next)
         head->jq_prev = newitem;
      else
         newitem->jq_next->jq_prev = newitem;
   }
}

#define CH_JSON_MAX_ARGS 4

//Execute a command received over "channel"/"part"
//"argv[0]" is the command string.
//"argv[1]" etc. have further arguments, type is VAR_UNKNOWN if missing.
private void
channel_exe_cmd(Channel* channel, ChannelFdKind part, Var* argv) {
   CS cmd = argv[0].string;
   if (argv[1].tag != VAR_STRING) {
      ch_error(channel, "received command with non-string argument");
      if (p_verbose > 2)
         emsg(_(e_received_command_with_non_string_argument));
      return;
   }
   CS arg = argv[1].string;
   if (!arg)
      arg = S"";

   if (STRCMP(cmd, "ex") == 0) {
      CS p = arg;

      ch_log(channel, "Executing command '%s'", (char *)arg);
      int do_emsg_silent = !checkforcmd(&p, S"echoerr", 5);
      if (do_emsg_silent != 0)
         ++emsg_silent;
      executeCommLine(arg);
      if (do_emsg_silent != 0)
          --emsg_silent;
   } ei (STRCMP(cmd, "normal") == 0) {
      ch_log(channel, "Executing normal command '%s'", (char *)arg);
      Invocation invo;
      CLEAR_FIELD(invo);
      invo.arg = arg;
      invo.addr_count = 0;
      invo.forceit = true; //no mapping
      c_normal(&invo);
   } ei (STRCMP(cmd, "redraw") == 0) {
      ch_log(channel, "redraw");
      redraw_cmd(*arg != ZERO);
      showruler(false);
      setcursor();
      termOutFlush();
   } ei (STRCMP(cmd, "expr") == 0 || STRCMP(cmd, "call") == 0) {
      int is_call = cmd[0] == 'c';
      int id_idx = is_call ? 3 : 2;

      if (argv[id_idx].tag != VAR_UNKNOWN && argv[id_idx].tag != VAR_NUMBER) {
         ch_error(channel, "last argument for expr/call must be a number");
         if (p_verbose > 2)
            emsg(_(e_last_argument_for_expr_call_must_be_number));
      } ei (is_call && argv[2].tag != VAR_LIST) {
         ch_error(channel, "third argument for call must be a list");
         if (p_verbose > 2)
            emsg(_(e_third_argument_for_call_must_be_list));
      } else {
         Var* tv = NULL;
         Var res_tv;
         Var err_tv;
         Byte* json = NULL;

         //Don't pollute the display with errors. Do generate the errors so that try/catch works.
         ++emsg_silent;
         if (!is_call) {
            ch_log(channel, "Evaluating expression '%s'", (char *)arg);
            tv = eval_expr(arg, NULL);
         } else {
            ch_log(channel, "Calling '%s'", (char *)arg);
            if (func_call(arg, &argv[2], NULL, NULL, &res_tv) == OK)
               tv = &res_tv;
         }

         if (argv[id_idx].tag == VAR_NUMBER) {
            int id = argv[id_idx].number;

            if (tv)
               json = json_encode_nr_expr(id, tv, JSON_NL);
            if (tv == NULL || (json != NULL && *json == ZERO)) {
               //If evaluation failed or the result can't be encoded
               //then return the string "ERROR".
               eeglFree(json);
               err_tv.tag = VAR_STRING;
               err_tv.string = (CS)"ERROR";
               json = json_encode_nr_expr(id, &err_tv, JSON_NL);
            }
            if (json) {
               channel_send(channel,
                   part == PART_SOCK ? PART_SOCK : PART_IN,
                   json, (int)STRLEN(json), (char *)cmd
               );
               eeglFree(json);
            }
         }
         --emsg_silent;
         if (tv == &res_tv)
            clearVar(tv);
         else
            freeVar(tv);
      }
   } ei (p_verbose > 2) {
      ch_error(channel, "Received unknown command: %s", (char *)cmd);
      showErrFmtMsg(_(e_received_unknown_command_str), cmd);
   } }

//Invoke the callback at "cbhead". Does not redraw but sets channel_need_redraw.
private void
invoke_one_time_callback(Channel* channel, CbNode* cbhead, CbNode* item, Var* argv) {
   ch_log(channel, "Invoking one-time callback %s", (char *)item->cq_callback.name);
   //Remove the item from the list first, if the callback
   //invokes ch_close() the list will be cleared.
   remove_cb_node(cbhead, item);
   invoke_callback(channel, &item->cq_callback, argv);
   evFreeCallback(&item->cq_callback);
   eeglFree(item);
}

private void
appendToBook(Book* book, CS msg, Channel* channel, ChannelFdKind part) {
   AutocommSave   aco;
   LineNr    lnum = book->mem.lineCount;
   int      save_write_to = book->writeToChannel;
   ChannelFd* fds = &channel->fds[part];
   Boole      save_p_ma = book->o.modifiable;
   int      empty = (book->mem.flags & ML_EMPTY) ? 1 : 0;

   if (!book->o.modifiable && !fds->ch_nomodifiable) {
      if (!fds->ch_nomod_error) {
         ch_error(channel, "Book is not modifiable, cannot append");
         fds->ch_nomod_error = true;
      }
      return;
   }

   //If the book is also used as input insert above the last line. Don't write these lines.
   if (save_write_to) {
      --lnum;
      book->writeToChannel = false;
   }

   //Append to the book
   ch_log(channel, "appending line %d to book %s", (int)lnum + 1 - empty, book->currFileName);

   book->o.modifiable = true;

   //Set curBook to "book", temporarily.
   auCommPrepareBook(&aco, book);
   if (curBook != book) {
      //Could not find a portal into this book, the following might cause trouble, better bail out.
      return;
   }

   u_sync(true);
   //ignore undo failure, undo is not very useful here
   (void)u_save(lnum - empty, lnum + 1);

   if (empty) {
      //The book is empty, replace the first (dummy) line.
      ml_replace(lnum, msg, true);
      lnum = 0;
   } else
      ml_append(lnum, msg, 0, false);
   appended_lines_mark(lnum, 1L);

   //reset notion of book
   auCommRestoreBook(&aco);

   if (fds->ch_nomodifiable) {
      book->o.modifiable = false;
   } else {
      book->o.modifiable = save_p_ma;
   }

   if (book->countPortals > 0) {
      Portal   *wp;
      FOR_ALL_PORTALS(wp) {
         if (wp->book == book) {
            int move_cursor = save_write_to
                   ? wp->cursor.lnum == lnum + 1
                   : (wp->cursor.lnum == lnum && wp->cursor.col == 0);

            //If the cursor is at or above the new line, move it one line
            //down.  If the topline is outdated update it now.
            if (move_cursor || wp->topLine > book->mem.lineCount) {
               Portal *save_curPor = curPor;

               if (move_cursor)
                  ++wp->cursor.lnum;
               curPor = wp;
               curBook = curPor->book;
               scroll_cursor_bot(0, false);
               curPor = save_curPor;
               curBook = curPor->book;
            }
         }
      }
      drawBookAndStatusLater(book, UPD_VALID);
      channel_need_redraw = true;
    }

   if (save_write_to) {
      Channel *ch;

      //Find channels reading from this book and adjust their next-to-read line number.
      book->writeToChannel = true;
      FOR_ALL_CHANNELS(ch) {
         ChannelFd  *intake = &ch->fds[PART_IN];

         if (intake->bookref.c == book)
            intake->ch_buf_bot = book->mem.lineCount;
      }
   }
}

private void
drop_messages(Channel* channel, ChannelFdKind part) {
   CS msg;
   while ((msg = channel_get(channel, part, NULL)) != NULL) {
      ch_log(channel, "Dropping message '%s'", (char *)msg);
      eeglFree(msg);
   }
}

//true if for "channel" / "part" ch_json_head should be used.
private int
channel_use_json_head(Channel* channel, ChannelFdKind part) {
   ChannelMode   ch_mode = channel->fds[part].ch_mode;
   return ch_mode == CH_MODE_JSON || ch_mode == CH_MODE_LSP;
}

//Invoke a callback for "channel"/"part" if needed. This does not redraw but sets 
//channel_need_redraw when redraw is needed. Return true when a message was handled, there might 
//be another one.
private int
may_invoke_callback(Channel* channel, ChannelFdKind part) {
   Byte* msg = NULL;
   Var* listtv = NULL;
   Var argv[CH_JSON_MAX_ARGS];
   int seq_nr = -1;
   ChannelFd* fdData = &channel->fds[part];
   ChannelMode ch_mode = fdData->ch_mode;
   CbNode* cbhead = &fdData->ch_cb_head;
   CbNode* cbitem;
   Callback* callback = NULL;
   Byte* p;

   //Use a message-specific callback, part callback or channel callback
   for (cbitem = cbhead->cq_next; cbitem != NULL; cbitem = cbitem->cq_next) {
      if (cbitem->cq_seq_nr == 0)
          break;
   } 
   
   void (*nativeCallback)(Arr(Byte)) = NULL; //if non-null, overtakes non-native callbacks
   if (fdData->nativeCb != NULL) {
      nativeCallback = fdData->nativeCb;
   } else {
      if (cbitem != NULL)
         callback = &cbitem->cq_callback;
      ei (fdData->ch_callback.name != NULL)
         callback = &fdData->ch_callback;
      ei (channel->ch_callback.name != NULL)
         callback = &channel->ch_callback;
   } 

   Book* book = fdData->bookref.c;
   if (book && (!bookRefValid(&fdData->bookref) || bookNoMemfile(book))) {
      //book was wiped out or unloaded
      ch_log(channel, "%s book has been wiped out", chanFdNames[part]);
      fdData->bookref.c = NULL;
      book = NULL;
   }

   if (channel_use_json_head(channel, part)) {
      ListItem* item;
      int      argc = 0;

      //Get any json message in the queue.
      if (channel_get_json(channel, part, -1, false, &listtv) == FAIL) {
         if (ch_mode == CH_MODE_LSP)
            //In the "lsp" mode, the http header and the json payload may
            //be received in multiple messages. So concatenate all the received messages.
            (void)channel_collapse(channel, part, false);

         //Parse readahead, return when there is still no message.
         channel_parse_json(channel, part);
         if (channel_get_json(channel, part, -1, false, &listtv) == FAIL)
            return false;
      }

      if (ch_mode == CH_MODE_LSP) {
         Bag* d = listtv->bag;
         seq_nr = 0;
         if (d) {
            DictItem* di = bagFind(d, tConst("id"));
            if (di && di->c.tag == VAR_NUMBER)
               seq_nr = di->c.number;
         }

         argv[1] = *listtv;
      } else {
         for (item = listtv->list->first;
             item != NULL && argc < CH_JSON_MAX_ARGS;
             item = item->next
         )
            argv[argc++] = item->c;
         while (argc < CH_JSON_MAX_ARGS)
            argv[argc++].tag = VAR_UNKNOWN;

         if (argv[0].tag == VAR_STRING) {
            //["cmd", arg] or ["cmd", arg, arg] or ["cmd", arg, arg, arg]
            channel_exe_cmd(channel, part, argv);
            freeVar(listtv);
            return true;
         }

         if (argv[0].tag != VAR_NUMBER) {
            ch_error(channel, "Dropping message with invalid sequence number type");
            freeVar(listtv);
            return false;
         }
         seq_nr = argv[0].number;
      }
   } ei (channel_peek(channel, part) == NULL) {
      //nothing to read on RAW or NL channel
      return false;
   }  else {
      //If there is no callback or book, drop the message.
      if (!nativeCallback && !callback && !book) {
         //If there is a close callback it may use ch_read() to get the messages.
         if (channel->ch_close_cb.name == NULL && !channel->ch_drop_never)
            drop_messages(channel, part);
         return false;
      }

      if (ch_mode == CH_MODE_NL) {
         CS nl = NULL;
         ReadChunk *node;

         //See if we have a message ending in NL in the first book.  If
         //not try to concatenate the first and the second book.
         while (true) {
            node = channel_peek(channel, part);
            nl = channel_first_nl(node);
            if (nl)
                break;
            if (channel_collapse(channel, part, true) == FAIL) {
               if (fdData->fd == INVALID_FD && node->len > 0)
                  break;
               return false; //incomplete message
            }
         }
         CS buf = node->c;

         //Convert ZERO to NL, the internal representation.
         for (p = buf; (nl == NULL || p < nl) && p < buf + node->len; ++p) {
            if (*p == ZERO)
               *p = NL;
         } 

         if (nl == NULL) {
            //get the whole buffer, drop the NL
            msg = channel_get(channel, part, NULL);
         } ei (nl + 1 == buf + node->len) {
            //get the whole buffer
            msg = channel_get(channel, part, NULL);
            *nl = ZERO;
         } else {
            //Copy the message into allocated memory (excluding the NL)
            //and remove it from the buffer (including the NL).
            msg = copySubstr(buf, nl - buf);
            channel_consume(channel, part, (int)(nl - buf) + 1);
         }
      } else {
          //For a raw channel we don't know where the message ends, just get everything we have.
          //Convert ZERO to NL, the internal representation.
          msg = channel_get_all(channel, part, NULL);
      }

      if (msg == NULL)
         return false; //out of memory (and avoids Coverity warning)

      argv[1].tag = VAR_STRING;
      argv[1].string = msg;
   }

   Boole called_otc = false; //one-time callbackup
   if (seq_nr > 0) {
      //JSON or LSP mode: invoke the one-time callback with the matching nr
      int lsp_req_msg = false;

      //Don't use a LSP server request message with the same sequence number
      //as the client request message as the response message.
      if (ch_mode == CH_MODE_LSP && argv[1].tag == VAR_BAG 
            && bagHasKey(argv[1].bag, tConst("method"))) {
         lsp_req_msg = true;
      } 

      if (!lsp_req_msg) {
         for (cbitem = cbhead->cq_next; cbitem != NULL; cbitem = cbitem->cq_next) {
            if (cbitem->cq_seq_nr == seq_nr) {
               invoke_one_time_callback(channel, cbhead, cbitem, argv);
               called_otc = true;
               break;
            }
         }
      }
   }

   if (seq_nr > 0 && (ch_mode != CH_MODE_LSP || called_otc)) {
      if (!called_otc) {
          //If the 'drop' channel attribute is set to 'never' or if
          //ch_evalexpr() is waiting for this response message, then don't drop this message.
          if (channel->ch_drop_never) {
            //message must be read with ch_read()
            channel_push_json(channel, part, listtv);

            //Change the type to avoid the value being freed.
            listtv->tag = VAR_NUMBER;
            freeVar(listtv);
            listtv = NULL;
         } else
            ch_log(channel, "Dropping message %d without callback", seq_nr);
      }
   } ei (nativeCallback != NULL || callback != NULL || book != NULL) {
      if (book) {
         if (msg == NULL)
            //JSON or JS mode: re-encode the message.
            msg = json_encode(listtv, ch_mode);
         if (msg != NULL) {
            if (book->term != NULL)
               write_to_term(book, msg, channel);
            else
               appendToBook(book, msg, channel, part);
         }
      }
      if (nativeCallback != NULL && msg != NULL) {
         (*nativeCallback)(msg);
      } ei (callback) {
         if (cbitem)
            invoke_one_time_callback(channel, cbhead, cbitem, argv);
         else {
            //invoke the channel callback
            ch_log(channel, "Invoking channel callback %s", (char *)callback->name);
            invoke_callback(channel, callback, argv);
         }
      }
   } else
      ch_log(channel, "Dropping message %d", seq_nr);

   if (listtv)
      freeVar(listtv);
   eeglFree(msg);

   return true;
}

//Return true when channel "channel" is open for writing to. false for invalid "channel".
//TODO delete
private int
channel_can_write_to(Channel* channel) {
   return channel 
      && (channel->fds[PART_SOCK].fd != INVALID_FD || channel->fds[PART_IN].fd != INVALID_FD);
}

//Return true when channel "channel" is open for reading or writing. false for invalid "channel".
pub int
channel_is_open(Channel *channel) {
    return channel != NULL && (channel->fds[PART_SOCK].fd != INVALID_FD
           || channel->fds[PART_IN].fd != INVALID_FD
           || channel->fds[PART_OUT].fd != INVALID_FD
           || channel->fds[PART_ERR].fd != INVALID_FD);
}

//Return a pointer indicating the readahead.  Can only be compared between
//calls.  Returns NULL if there is no readahead.
private void *
channel_readahead_pointer(Channel* channel, ChannelFdKind part) {
   if (channel_use_json_head(channel, part)) {
      JsonQ   *head = &channel->fds[part].ch_json_head;

      if (head->jq_next == NULL)
          //Parse json from readahead, there might be a complete message to process.
          channel_parse_json(channel, part);

      return head->jq_next;
   }
   return channel_peek(channel, part);
}

//true if "channel" has JSON or other typeahead.
private int
channel_has_readahead(Channel *channel, ChannelFdKind part) {
   return channel_readahead_pointer(channel, part) != NULL;
}

//Return a string indicating the status of the channel.
//If "req_part" is not negative check that part.
private CS
channel_status(Channel *channel, int req_part) {
   ChannelFdKind part;
   int has_readahead = false;

   if (!channel)
      return S"fail";
   if (req_part == PART_OUT) {
      if (channel->fds[PART_OUT].fd != INVALID_FD)
         return S"open";
      if (channel_has_readahead(channel, PART_OUT))
         has_readahead = true;
   } ei (req_part == PART_ERR) {
      if (channel->fds[PART_ERR].fd != INVALID_FD)
         return S"open";
      if (channel_has_readahead(channel, PART_ERR))
         has_readahead = true;
   } else {
      if (channel_is_open(channel))
         return S"open";
      for (part = PART_SOCK; part < PART_IN; ++part) {
         if (channel_has_readahead(channel, part)) {
            has_readahead = true;
            break;
         }
      } 
   }

   if (has_readahead)
      return S"buffered";
   return S"closed";
}

private void
channel_part_info(Channel* channel, Bag* bag, CS name, ChannelFdKind part) {
   ChannelFd* chanpart = &channel->fds[part];
   Byte namebuf[20];  //longest is "sock_timeout"
   CS s = S"";

   copySubstrToAllocation(namebuf, (Text){name, 4});
   STRCAT(namebuf, "_");
   Unt tail = STRLEN(namebuf);

   STRCPY(namebuf + tail, "status");
   CS status;
   if (chanpart->fd != INVALID_FD)
      status = S"open";
   ei (channel_has_readahead(channel, part))
      status = S"buffered";
   else
      status = S"closed";
   bagAddString(bag, namebuf, (CS)status);

   STRCPY(namebuf + tail, "mode");
   switch (chanpart->ch_mode) {
   case CH_MODE_NL: s = S"NL"; break;
   case CH_MODE_RAW: s = S"RAW"; break;
   case CH_MODE_JSON: s = S"JSON"; break;
   case CH_MODE_LSP: s = S"LSP"; break;
   }
   bagAddString(bag, namebuf, s);

   STRCPY(namebuf + tail, "io");
   if (part == PART_SOCK)
      s = S"socket";
   else switch (chanpart->ch_io) {
      case JIO_NULL: s = S"null"; break;
      case JIO_PIPE: s = S"pipe"; break;
      case JIO_FILE: s = S"file"; break;
      case JIO_BUFFER: s = S"buffer"; break;
      case JIO_OUT: s = S"out"; break;
   }
   bagAddString(bag, namebuf, (CS)s);

   STRCPY(namebuf + tail, "timeout");
   bagAddNumber(bag, namebuf, chanpart->ch_timeout);
}

private void
channelInfoIntoDict(Channel *channel, OUT Bag *dict) {
   bagAddNumber(dict, S"id", channel->id);
   bagAddString(dict, S"status", channel_status(channel, -1));

   if (channel->socketName) {
      bagAddString(dict, S"path", (CS)channel->socketName);
      channel_part_info(channel, dict, S"sock", PART_SOCK);
   } else {
      channel_part_info(channel, dict, S"out", PART_OUT);
      channel_part_info(channel, dict, S"err", PART_ERR);
      channel_part_info(channel, dict, S"in", PART_IN);
   }
}

//Close channel "channel".
//Trigger the close callback if "invoke_close_cb" is true. Does not clear the buffers.
private void
channel_close(Channel *channel, int invoke_close_cb) {
    ch_log(channel, "Closing channel");

    ch_close_part(channel, PART_SOCK);
    ch_close_part(channel, PART_IN);
    ch_close_part(channel, PART_OUT);
    ch_close_part(channel, PART_ERR);

   if (invoke_close_cb) {
      ChannelFdKind   part;

      //let the terminal know it is closing to avoid getting stuck
      term_channel_closing(channel);
      //Invoke callbacks and flush buffers before the close callback.
      if (channel->ch_close_cb.name != NULL)
         ch_log(channel, "Invoking callbacks and flushing buffers before closing");
      for (part = PART_SOCK; part < PART_IN; ++part) {
         if (channel->ch_close_cb.name || channel->fds[part].bookref.c) {
            //Increment the refcount to avoid the channel being freed halfway.
            ++channel->refCount;
            if (channel->ch_close_cb.name == NULL)
                ch_log(channel, "flushing %s buffers before closing", chanFdNames[part]);
            while (may_invoke_callback(channel, part))
               {} 
            --channel->refCount;
         }
      }

      if (channel->ch_close_cb.name) {
         Var argv[1];
         Var returnVar;

         //Increment the refcount to avoid the channel being freed halfway.
         ++channel->refCount;
         ch_log(channel, "Invoking close callback %s", (char *)channel->ch_close_cb.name);
         argv[0].tag = VAR_CHANNEL;
         argv[0].channel = channel;
         call_callback(&channel->ch_close_cb, -1, &returnVar, 1, argv);
         clearVar(&returnVar);
         channel_need_redraw = true;

          //the callback is only called once
          evFreeCallback(&channel->ch_close_cb);

          if (channel_need_redraw) {
             channel_need_redraw = false;
             redraw_after_callback(true, false);
          }

          if (!channel->ch_drop_never) {
             //any remaining messages are useless now
             for (part = PART_SOCK; part < PART_IN; ++part)
                 drop_messages(channel, part);
          } 

          --channel->refCount;
      }
   }

   term_channel_closed(channel);
}

//Close the "in" part channel "channel".
private void
channel_close_in(Channel *channel) {
   ch_close_part(channel, PART_IN);
}

private void
remove_from_writeque(WriteQueue *wq, WriteQueue *entry) {
   ga_clear(&entry->wq_ga);
   wq->next = entry->next;
   if (wq->next == NULL)
      wq->prev = NULL;
   else
      wq->next->prev = NULL;
    eeglFree(entry);
}

//Clear the read buffer on "channel"/"part".
private void
channel_clear_one(Channel *channel, ChannelFdKind part) {
    ChannelFd *fds = &channel->fds[part];
    JsonQ *json_head = &fds->ch_json_head;
    CbNode   *cb_head = &fds->ch_cb_head;

   while (channel_peek(channel, part) != NULL)
      eeglFree(channel_get(channel, part, NULL));

   while (cb_head->cq_next != NULL) {
      CbNode *node = cb_head->cq_next;

      remove_cb_node(cb_head, node);
      evFreeCallback(&node->cq_callback);
      eeglFree(node);
   }

   while (json_head->jq_next != NULL) {
      freeVar(json_head->jq_next->jq_value);
      remove_json_node(json_head, json_head->jq_next);
   }

   evFreeCallback(&fds->ch_callback);
   ga_clear(&fds->ch_block_ids);

   while (fds->ch_writeque.next)
      remove_from_writeque(&fds->ch_writeque, fds->ch_writeque.next);
}

//Clear all the read buffers on "channel".
pub void
channel_clear(Channel* channel) {
   ch_log(channel, "Clearing channel");
   EE_CLEAR(channel->socketName);
   channel_clear_one(channel, PART_SOCK);
   channel_clear_one(channel, PART_OUT);
   channel_clear_one(channel, PART_ERR);
   channel_clear_one(channel, PART_IN);
   evFreeCallback(&channel->ch_callback);
   evFreeCallback(&channel->ch_close_cb);
}

#if defined(EXITFREE)
pub void
channel_free_all(void) {
   Channel *channel;

   lo("channel_free_all()");
   FOR_ALL_CHANNELS(channel)
      channel_clear(channel);
}
#endif

//Book size for reading incoming messages.
#define MAXMSGSIZE 4096

//Check if there are remaining data that should be written for "intake".
private int
is_channel_write_remaining(ChannelFd* intake) {
   Book* book = intake->bookref.c;

   if (intake->ch_writeque.next)
      return true;
   if (!book)
      return false;
   return intake->ch_buf_append
       ? (intake->ch_buf_bot < book->mem.lineCount)
       : (intake->ch_buf_top <= intake->ch_buf_bot && intake->ch_buf_top <= book->mem.lineCount);
}

private int
fillIntake(int nfd_in, Arr(PollFd) fds) {
   int nfd = nfd_in;

   for (Channel* ch = firstChannelP; ch; ch = ch->next) {
      ChannelFd* intake = &ch->fds[PART_IN];

      if (intake->fd != INVALID_FD && (intake->bookref.c || intake->ch_writeque.next)) {
         intake->pollIdx = nfd;
         fds[nfd].fd = intake->fd;
         fds[nfd].events = POLLOUT;
         ++nfd;
      } else
         intake->pollIdx = -1;
   }
   return nfd;
}

pub
#define MAX_OPEN_CHANNELS 16

//Check for reading from "fd" with "timeout" msec. Return CW_READY when there is something to read.
//CW_NOT_READY when there is nothing to read. CW_ERROR when there is an error.
private channel_wait_result
channel_wait(Channel* channel, Socket fd, int timeout) {
   if (timeout > 0)
      ch_log(channel, "Waiting for up to %d msec", timeout);
cycle:
   //Write lines to a pipe when a pipe can be written to.
   PollFd fds[MAX_OPEN_CHANNELS + 1];
   fds[0].fd = fd;
   fds[0].events = POLLIN;
   int nfd = fillIntake(1, fds); 
   if (poll(fds, nfd, timeout) > 0) {
      if ((fds[0].revents & POLLIN) > 0) {
         return CW_READY;
      }
      channel_write_any_lines();
      goto cycle;
   }
   return CW_NOT_READY;
}

private void
ch_close_part_on_error(Channel *channel, ChannelFdKind part, int is_err, char *func) {
   char   msg[] = "%s(): Read %s from fds[%d], closing";

   if (is_err)
      //Do not call emsg(), most likely the other end just exited.
      ch_error(channel, msg, func, "error", part);
   else
      ch_log(channel, msg, func, "EOF", part);


   //When reading is not possible close this part of the channel.  Don't
   //close the channel yet, there may be something to read on another part.
   //When stdout and stderr use the same FD we get the error only on one of
   //them, also close the other.
   if (part == PART_OUT || part == PART_ERR) {
      ChannelFdKind other = part == PART_OUT ? PART_ERR : PART_OUT;

      if (channel->fds[part].fd == channel->fds[other].fd)
          ch_close_part(channel, other);
   }
   ch_close_part(channel, part);
}

private void
channel_close_now(Channel *channel) {
   ch_log(channel, "Closing channel because all readable fds are closed");
   channel_close(channel, true);
}

//Read from channel "channel" for as long as there is something to read. "part" is PART_SOCK, 
//PART_OUT or PART_ERR. The data is put in the read queue.  No callbacks are invoked here.
private void
channel_read(Channel *channel, ChannelFdKind part, char *func) {
   static CS buf = NULL;
   int len = 0;
   int readlen = 0;
   int use_socket = false;

   Socket fd = channel->fds[part].fd;
   if (fd == INVALID_FD) {
      ch_error(channel, "channel_read() called while %s part is closed", chanFdNames[part]);
      return;
   }
   use_socket = fd == channel->fds[PART_SOCK].fd;

   //Allocate a buffer to read into.
   if (!buf) {
      buf = alloc(MAXMSGSIZE);
   }

   //Keep on reading for as long as there is something to read.
   //Use poll() to avoid blocking on a message that is exactly MAXMSGSIZE long.
   for (;;) {
      if (channel_wait(channel, fd, 0) != CW_READY)
         break;
      if (use_socket)
         len = sock_read(fd, (char *)buf, MAXMSGSIZE);
      else
         len = fd_read(fd, (char *)buf, MAXMSGSIZE);
      if (len <= 0)
         break;   //error or nothing more to read

      //Store the read message in the queue.
      saveMsg(channel, part, buf, len, false, S"RECV ");
      readlen += len;
   }

   //Reading a disconnection (readlen == 0), or an error.
   if (readlen <= 0) {
      if (!channel->ch_keep_open)
         ch_close_part_on_error(channel, part, (len < 0), func);
   }
}

//Read from RAW or NL "channel"/"part".  Blocks until there is something to read or the timeout 
//expires. When "raw" is true don't block waiting on a NL. Does not trigger timers or handle 
//messages. Return what was read in allocated memory. NULL in case of error or timeout.
private CS
channel_read_block(Channel *channel, ChannelFdKind part, int timeout, int raw, int *outlen){
   CS buf;
   CS msg;
   ChannelMode mode = channel->fds[part].ch_mode;
   Socket fd = channel->fds[part].fd;
   Byte* nl;
   ReadChunk* node;

   ch_log(channel, "Blocking %s read, timeout: %d msec",
              mode == CH_MODE_RAW ? "RAW" : "NL", timeout);

   while (true) {
      node = channel_peek(channel, part);
      if (node != NULL) {
          if (mode == CH_MODE_RAW || (mode == CH_MODE_NL && channel_first_nl(node) != NULL))
            //got a complete message
            break;
         if (channel_collapse(channel, part, mode == CH_MODE_NL) == OK)
            continue;
         //If not blocking or nothing more is coming then return what we
         //have.
         if (raw || fd == INVALID_FD)
            break;
      }

      //Wait for up to the channel timeout.
      if (fd == INVALID_FD)
         return NULL;
      if (channel_wait(channel, fd, timeout) != CW_READY) {
         ch_log(channel, "Timed out");
         return NULL;
      }
      channel_read(channel, part, "channel_read_block");
   }

    //We have a complete message now.
   if (mode == CH_MODE_RAW || outlen != NULL) {
      msg = channel_get_all(channel, part, outlen);
   } else {
      buf = node->c;
      nl = channel_first_nl(node);

      //Convert ZERO to NL, the internal representation.
      for (CS p = buf; (nl == NULL || p < nl) && p < buf + node->len; ++p) {
         if (*p == ZERO)
            *p = NL;
      } 

      if (!nl) {
         //must be a closed channel with missing NL
         msg = channel_get(channel, part, NULL);
      } ei (nl + 1 == buf + node->len) {
         //get the whole buffer
         msg = channel_get(channel, part, NULL);
         *nl = ZERO;
      } else {
         //Copy the message into allocated memory and remove it from the buffer.
         msg = copySubstr(buf, nl - buf);
         channel_consume(channel, part, (int)(nl - buf) + 1);
      }
   }
   if (ch_log_active())
      ch_log(channel, "Returning %d bytes", (int)STRLEN(msg));
   return msg;
}

private int channel_blocking_wait = 0;

//Return true if in a blocking wait that might trigger callbacks.
pub int
channel_in_blocking_wait(void) {
   return channel_blocking_wait > 0;
}

//Read one JSON message with ID "id" from "channel"/"part" and store the result in "returnVar".
//When "id" is -1 accept any message;
//Blocks until the message is received or the timeout is reached.
//In corner cases this can be called recursively, that is why ch_block_ids is * a list.
private int
channel_read_json_block(
   Channel* channel,
   ChannelFdKind part,
   int timeout_arg,
   int id,
   Var** returnVar
) {
   int      more;
   Socket   fd;
   int      timeout;
   ChannelFd   *chanpart = &channel->fds[part];
   ChannelMode   mode = channel->fds[part].ch_mode;
   int      retval = FAIL;

   ch_log(channel, "Blocking read JSON for id %d", id);
   ++channel_blocking_wait;

   if (id >= 0)
      channel_add_block_id(chanpart, id);

   for (;;) {
      if (mode == CH_MODE_LSP)
          //In the "lsp" mode, the http header and the json payload may be
          //received in multiple messages. So concatenate all the received
          //messages.
          (void)channel_collapse(channel, part, false);

      more = channel_parse_json(channel, part);

      //search for message "id"
      if (channel_get_json(channel, part, id, true, returnVar) == OK) {
          ch_log(channel, "Received JSON for id %d", id);
          retval = OK;
          break;
      }

      if (!more) {
         void *prev_readahead_ptr = channel_readahead_pointer(channel, part);
         void *readahead_ptr;

         //Handle any other messages in the queue.  If done some more messages may have arrived.
         if (channel_parse_messages())
            continue;

         //channel_parse_messages() may fill the queue with new data to process.  Only loop when 
         //the readahead changed, otherwise we would busy-loop.
         readahead_ptr = channel_readahead_pointer(channel, part);
         if (readahead_ptr != NULL && readahead_ptr != prev_readahead_ptr)
            continue;

         //Wait for up to the timeout. If there was an incomplete message use the deadline for that
         timeout = timeout_arg;
         if (chanpart->ch_wait_len > 0) { {
             TimeSpec now_tv;
             timespec_get(&now_tv, TIME_UTC);
             timeout = (chanpart->deadline.tv_sec - now_tv.tv_sec) * 1000
                        + (chanpart->deadline.tv_nsec - now_tv.tv_nsec) / 1000
                        + 1;
         }
         if (timeout < 0) {
             //Something went wrong, channel_parse_json() didn't discard message.  Cancel waiting.
             chanpart->ch_wait_len = 0;
             timeout = timeout_arg;
         } ei (timeout > timeout_arg)
             timeout = timeout_arg;
         }
         fd = chanpart->fd;
         if (fd == INVALID_FD || channel_wait(channel, fd, timeout) != CW_READY) {
            if (timeout == timeout_arg) {
               if (fd != INVALID_FD)
                  ch_log(channel, "Timed out on id %d", id);
               break;
            }
         } else
            channel_read(channel, part, "channel_read_json_block");
      }
   }
   if (id >= 0)
      channel_remove_block_id(chanpart, id);
   --channel_blocking_wait;

   return retval;
}

//Get the channel from the argument.
//Returns NULL if the handle is invalid.
//When "check_open" is true check that the channel can be used.
//When "reading" is true "check_open" considers typeahead useful.
//"part" is used to check typeahead, when PART_COUNT use the default part.
pub Channel *
get_channel_arg(Var* tv, int check_open, int reading, ChannelFdKind part) {
   Channel* channel = NULL;
   int has_readahead = false;

   if (tv->tag == VAR_JOB) {
      if (tv->job)
         channel = tv->job->channel;
   } ei (tv->tag == VAR_CHANNEL) {
      channel = tv->channel;
   } else {
      showErrFmtMsg(_(e_invalid_argument_str), tv_get_string(tv));
      return NULL;
   }
   if (channel != NULL && reading)
      has_readahead = 
         channel_has_readahead(channel, part != PART_COUNT ? part : channel_part_read(channel));

   if (check_open && 
         (channel == NULL || (!channel_is_open(channel) && !(reading && has_readahead)))
   ) {
      emsg(_(e_not_an_open_channel));
      return NULL;
   }
   return channel;
}

//Common for ch_read() and ch_readraw().
private void
commonChannelRead(Var* argvars, Var* returnVar, int raw, int blob) {
   ChannelFdKind part = PART_COUNT;
   int id = -1;
   Var* listtv = NULL;

   //return an empty string by default
   returnVar->tag = VAR_STRING;
   returnVar->string = NULL;

   JobOptions opt;
   CLEAR_POINTER(OUT &opt);
   if (get_job_options(&argvars[1], OUT &opt, JO_TIMEOUT + JO_PART + JO_ID, 0) == FAIL)
      goto theend;

   if ((opt.set & JO_PART) != 0)
      part = opt.part;
   Channel* channel = get_channel_arg(&argvars[0], true, true, part);
   if (!channel)
      goto theend;

   if (part == PART_COUNT)
      part = channel_part_read(channel);
   int mode = channel_get_mode(channel, part);
   int timeout = channel_get_timeout(channel, part);
   if (opt.set & JO_TIMEOUT)
      timeout = opt.jo_timeout;

   if (blob) {
      int outlen = 0;
      Arr(Byte) channelContent = channel_read_block(channel, part, timeout, true, &outlen);
      if (channelContent) {
         Blob* blob = blob_alloc();
         blob->c.len = outlen;
         if (ga_grow(&blob->c, outlen) == FAIL)
            blob_free(blob);
         else {
            memcpy(blob->c.c, channelContent, outlen);
            returnVar_blob_set(returnVar, blob);
         }
         eeglFree(channelContent);
      }
   } ei (raw || mode == CH_MODE_RAW || mode == CH_MODE_NL)
      returnVar->string = channel_read_block(channel, part, timeout, raw, NULL);
   else {
      if ((opt.set & JO_ID) != 0)
         id = opt.id;
      channel_read_json_block(channel, part, timeout, id, &listtv);
      if (listtv) {
         *returnVar = *listtv;
         eeglFree(listtv);
      } else {
         returnVar->tag = VAR_VOID;
         returnVar->number = 0;
      }
   }

theend:
   free_job_options(&opt);
}


//Set "channel"/"part" to non-blocking. Only works for sockets and pipes.
pub void
channel_set_nonblock(Channel *channel, ChannelFdKind part) {
   ChannelFd* fds = &channel->fds[part];

   if (fds->fd == INVALID_FD)
      return;

   int fd = fds->fd;
   (void)fcntl(fd, F_SETFL, O_NONBLOCK);
   fds->ch_nonblocking = true;
}

//Write "buf" (ZERO terminated string) to "channel"/"part".
//When "fun" is not NULL an error message might be given. Return FAIL or OK.
pub int
channel_send(
   Channel* channel,
   ChannelFdKind part,
   CS buf_arg,
   int len_arg,
   char* fun
) {
   int res;
   ChannelFd* fds = &channel->fds[part];
   int did_use_queue = false;

   Socket fd = fds->fd;
   if (fd == INVALID_FD) {
      if (!channel->error && fun) {
         ch_error(channel, "%s(): write while not connected", fun);
         showErrFmtMsg(_(e_str_write_while_not_connected), fun);
      }
      channel->error = true;
      return FAIL;
   }

   if (channel->ch_nonblock && !fds->ch_nonblocking)
      channel_set_nonblock(channel, part);

   if (ch_log_active()) {
      ch_log_literal(S"SEND ", channel, part, OUT (Text){buf_arg, len_arg});
      did_repeated_msg = 0;
   }

   for (;;) {
      WriteQueue* wq = &fds->ch_writeque;
      CS buf;
      int len;

      if (wq->next) {
         //first write what was queued
         buf = wq->next->wq_ga.c;
         len = wq->next->wq_ga.len;
         did_use_queue = true;
      } else {
         if (len_arg == 0)
            //nothing to write, called from checkPollResult()
            return OK;
         buf = buf_arg;
         len = len_arg;
      }

      if (part == PART_SOCK)
         res = sock_write(fd, (char *)buf, len);
      else {
         res = fd_write(fd, (char *)buf, len);
      }
      if (res < 0 && (errno == EWOULDBLOCK || errno == EAGAIN))
         res = 0; //nothing got written

      if (res >= 0 && fds->ch_nonblocking) {
         WriteQueue* entry = wq->next;

         if (did_use_queue)
            ch_log(channel, "Sent %d bytes now", res);
         if (res == len) {
            //Wrote all the buf[len] bytes.
            if (entry) {
               //Remove the entry from the write queue.
               remove_from_writeque(wq, entry);
               continue;
            }
            if (did_use_queue)
               ch_log(channel, "Write queue empty");
         }  else {
            //Wrote only buf[res] bytes, can't write more now.
            if (entry != NULL) {
               if (res > 0) {
                  //Remove the bytes that were written.
                  MEMMOVE(entry->wq_ga.c, (char *)entry->wq_ga.c + res, len - res);
                  entry->wq_ga.len -= res;
               }
               buf = buf_arg;
               len = len_arg;
            } else {
               buf += res;
               len -= res;
            }
            ch_log(channel, "Adding %d bytes to the write queue", len);

            //Append the unwritten bytes of the argument to the write buffer. Limit entries to 
            //4000 bytes.
            if (wq->prev && wq->prev->wq_ga.len + len < 4000) {
               WriteQueue *last = wq->prev;
               //append to the last entry
               if (len > 0 && ga_grow(&last->wq_ga, len) == OK) {
                  MEMMOVE((char *)last->wq_ga.c + last->wq_ga.len, buf, len);
                  last->wq_ga.len += len;
               }
            } else {
               WriteQueue* last = ALLOC_ONE(WriteQueue);

               if (last != NULL) {
                  last->prev = wq->prev;
                  last->next = NULL;
                  if (wq->prev == NULL)
                      wq->next = last;
                  else
                      wq->prev->next = last;
                  wq->prev = last;
                  ga_init2(&last->wq_ga, 1, 1000);
                  if (len > 0 && ga_grow(&last->wq_ga, len) == OK) {
                      MEMMOVE(last->wq_ga.c, buf, len);
                      last->wq_ga.len = len;
                  }
               }
            }
         }
      } ei (res != len) {
         if (!channel->error && fun) {
            ch_error(channel, "%s(): write failed", fun);
            showErrFmtMsg(_(e_str_write_failed), fun);
         }
         channel->error = true;
         return FAIL;
      }

      channel->error = false;
      return OK;
   }
}

//Common for "ch_sendexpr()" and "ch_sendraw()". Return the channel if the caller should read the 
//response. Sets "part_read" to the read fd. Otherwise returns NULL.
private Channel*
send_common(
   Var* argvars,
   CS text,
   int len,
   int id,
   int eval,
   JobOptions* opt,
   char* fun,
   ChannelFdKind* part_read
) {
   CLEAR_POINTER(opt);
   Channel* channel = get_channel_arg(&argvars[0], true, false, 0);
   if (!channel)
      return NULL;
   ChannelFdKind part_send = channel_part_send(channel);
   *part_read = channel_part_read(channel);

   if (get_job_options(&argvars[2], OUT opt, JO_CALLBACK + JO_TIMEOUT, 0) == FAIL)
      return NULL;

   //Set the callback. An empty callback means no callback and not reading
   //the response. With "ch_evalexpr()" and "ch_evalraw()" a callback is not
   //allowed.
   if (opt->jo_callback.name && *opt->jo_callback.name != ZERO) {
      if (eval) {
         showErrFmtMsg(_(e_cannot_use_callback_with_str), fun);
         return NULL;
      }
      channel_set_req_callback(channel, *part_read, &opt->jo_callback, id);
   }

   if (channel_send(channel, part_send, text, len, fun) == OK && opt->jo_callback.name == NULL)
      return channel;
   return NULL;
}

//common for "ch_evalexpr()" and "ch_sendexpr()"
private void
ch_expr_common(Arr(Var) argvars, Var* returnVar, int eval) {
   CS text;
   Var* listtv;
   int id;
   ChannelMode ch_mode;
   JobOptions opt;
   int timeout;
   int callback_present = false;

   //return an empty string by default
   returnVar->tag = VAR_STRING;
   returnVar->string = NULL;

   Channel* channel = get_channel_arg(&argvars[0], true, false, 0);
   if (!channel)
      return;
   ChannelFdKind part_send = channel_part_send(channel);

   ch_mode = channel_get_mode(channel, part_send);
   if (ch_mode == CH_MODE_RAW || ch_mode == CH_MODE_NL) {
      emsg(_(e_cannot_use_evalexpr_sendexpr_with_raw_or_nl_channel));
      return;
   }

   if (ch_mode == CH_MODE_LSP) {
      //return an empty dict by default
      allocReturnDict(returnVar);

      if (check_for_dict_arg(argvars, 1) == FAIL)
          return;

      Bag* d = argvars[1].bag;
      DictItem* di = bagFind(d, tConst("id"));
      if (di && di->c.tag != VAR_NUMBER) {
          //only number type is supported for the 'id' item
          showErrFmtMsg(_(e_invalid_value_for_argument_str), "id");
          return;
      }

      if (argvars[2].tag == VAR_BAG && bagHasKey(argvars[2].bag, tConst("callback")))
         callback_present = true;

      if (eval || callback_present) {
         //When evaluating an expression or sending an expression with a
         //callback, always assign a generated ID
         id = ++channel->lastMsgId;
         if (di == NULL)
            bagAddNumber(d, (CS)"id", id);
         else
            di->c.number = id;
      } else {
         //When sending an expression, if the message has an 'id' item,
         //then use it.
         id = 0;
         if (di)
            id = di->c.number;
      }
      if (!bagHasKey(d, tConst("jsonrpc")))
         bagAddString(d, (CS)"jsonrpc", (CS)"2.0");
      text = json_encode_lsp_msg(&argvars[1]);
   } else {
      id = ++channel->lastMsgId;
      text = json_encode_nr_expr(id, &argvars[1], JSON_NL);
   }
   if (!text)
      return;

   ChannelFdKind part_read;
   channel = send_common(argvars, text, (int)STRLEN(text), id, eval, &opt,
             eval ? "ch_evalexpr" : "ch_sendexpr", OUT &part_read);
   eeglFree(text);
   if (channel && eval) {
      if (opt.set & JO_TIMEOUT)
          timeout = opt.jo_timeout;
      else
          timeout = channel_get_timeout(channel, part_read);
      if (channel_read_json_block(channel, part_read, timeout, id, &listtv) == OK) {
         if (ch_mode == CH_MODE_LSP) {
            *returnVar = *listtv;
            //Change the type to avoid the value being freed.
            listtv->tag = VAR_NUMBER;
            freeVar(listtv);
         } else {
            List *list = listtv->list;

            //Move the item from the list and then change the type to
            //avoid the value being freed.
            *returnVar = list->lv_u.mat.last->c;
            list->lv_u.mat.last->c.tag = VAR_NUMBER;
            freeVar(listtv);
         }
      }
   }
   free_job_options(&opt);
   if (ch_mode == CH_MODE_LSP && !eval && callback_present) {
      //if ch_sendexpr() is used to send a LSP message and a callback function is specified, then 
      //return the generated identifier for the message. The user can use this to cancel the 
      //request (if needed).
      if (returnVar->bag)
         bagAddNumber(returnVar->bag, S"id", id);
   }
}

//common for "ch_evalraw()" and "ch_sendraw()"
private void
ch_raw_common(Var* argvars, OUT Var* returnVar, int eval) {
   Byte buf[NUMBUFLEN];
   int len;
   Channel* channel;
   ChannelFdKind part_read;
   JobOptions opt;
   int timeout;

   //return an empty string by default
   returnVar->tag = VAR_STRING;
   returnVar->string = NULL;

   CS text;
   if (argvars[1].tag == VAR_BLOB) {
      text = argvars[1].blob->c.c;
      len = argvars[1].blob->c.len;
   } else {
      text = tv_get_string_buf(&argvars[1], buf);
      len = (int)STRLEN(text);
   }
   channel = send_common(argvars, text, len, 0, eval, &opt,
               eval ? "ch_evalraw" : "ch_sendraw", &part_read);
   if (channel && eval) {
      if ((opt.set & JO_TIMEOUT) != 0)
         timeout = opt.jo_timeout;
      else
         timeout = channel_get_timeout(channel, part_read);
      returnVar->string = channel_read_block(channel, part_read, timeout, true, NULL);
   }
   free_job_options(&opt);
}

#define KEEP_OPEN_TIME 20  //msec

private int
checkPollResult(int ret_in, OUT Arr(PollFd) fds) {
   int ret = ret_in;
   Channel* channel;
   ChannelFdKind part;

   FOR_ALL_CHANNELS(channel) {
      for (part = PART_SOCK; part < PART_IN; ++part) {
         int idx = channel->fds[part].pollIdx;

         if (ret > 0 && idx != -1 && (fds[idx].revents & POLLIN) != 0) {
            channel_read(channel, part, "checkPollResult");
            --ret;
         } ei (channel->fds[part].fd != INVALID_FD && channel->ch_keep_open) {
            //polling a keep-open channel
            channel_read(channel, part, "channel_select_check_keep_open");
         }
      }

      ChannelFd* intake = &channel->fds[PART_IN];
      int idx = intake->pollIdx; 
      if (ret > 0 && idx != INVALID_FD && (fds[idx].revents & POLLOUT) != 0) {
         channel_write_input(channel);
         --ret;
      }
   }

   return ret;
}

//Execute queued up commands. Invoked from the main loop when it's safe to execute received 
//commands, and during a blocking wait for ch_evalexpr(). Return true when something was done.
pub int
channel_parse_messages(void) {
   Channel* channel = firstChannelP;
   int ret = false;
   int r;
   ChannelFdKind part = PART_SOCK;
   static int recursive = 0;
   Elapsed start_tv;

   //The code below may invoke callbacks, which might call us back.
   //In a recursive call channels will not be closed.
   ++recursive;
   ++safe_to_invoke_callback;

   timespec_get(OUT &start_tv, TIME_UTC);

   //Only do this message when another message was given, otherwise we get lots of them.
   if ((did_repeated_msg & REPEATED_MSG_LOOKING) == 0) {
      lo("looking for messages on channels");
      //now we should also give the message for SafeState
      did_repeated_msg = REPEATED_MSG_LOOKING;
   }
   while (channel) {
      if (recursive == 1) {
         if (channel_can_close(channel)) {
            channel->ch_to_be_closed = (1U << PART_COUNT);
            channel_close_now(channel);
            //channel may have been freed, start over
            channel = firstChannelP;
            continue;
         }
         if (channel->ch_to_be_freed || channel->isBeingKilled) {
            channel_free_contents(channel);
            if (channel->job)
               channel->job->channel = NULL;

            //free the channel and then start over
            channel_free_channel(channel);
            channel = firstChannelP;
            continue;
         }
         if (channel->refCount == 0 && !channel_still_useful(channel)) {
            //channel is no longer useful, free it
            channel_free(channel);
            channel = firstChannelP;
            part = PART_SOCK;
            continue;
         }
      }

      if (channel->fds[part].fd != INVALID_FD || channel_has_readahead(channel, part)) {
         //Increase the refcount, in case the handler causes the channel to be unreferenced or 
         //closed
         ++channel->refCount;
         r = may_invoke_callback(channel, part);
         if (r == OK)
            ret = true;
         if (channel_unref(channel) || (r == OK
            //Limit the time we loop here to 100 msec, otherwise Eegl becomes unresponsive when 
            //the callback takes more than a bit of time.
            && motElapsedMs(start_tv) < 100L
            )
         )
            //channel was freed or something was done, start over
            channel = firstChannelP;
         part = PART_SOCK;
         continue;
      }
      if (part < PART_ERR)
         ++part;
      else {
         channel = channel->next;
         part = PART_SOCK;
      }
   }

   if (channel_need_redraw) {
      channel_need_redraw = false;
      redraw_after_callback(true, false);
   }

   --safe_to_invoke_callback;
   --recursive;

   return ret;
}

//Return true if any channel has readahead.  That means we should not block on waiting for input.
pub int
channel_any_readahead(void) {
   Channel* channel = firstChannelP;
   ChannelFdKind part = PART_SOCK;

   while (channel) {
      if (channel_has_readahead(channel, part))
         return true;
      if (part < PART_ERR)
         ++part;
      else {
         channel = channel->next;
         part = PART_SOCK;
      }
   }
   return false;
}

//Mark references to lists used in channels.
pub int
set_ref_in_channel(int copyID) {
   int abort = false;
   Channel* channel;
   Var tv;

   for (channel = firstChannelP; !abort && channel; channel = channel->next) {
      if (channel_still_useful(channel)) {
         tv.tag = VAR_CHANNEL;
         tv.channel = channel;
         abort = abort || set_ref_in_item(&tv, copyID, NULL, NULL);
      }
   } 
   return abort;
}

//Return the "part" to write to for "channel".
private ChannelFdKind
channel_part_send(Channel* channel) {
   if (channel->fds[PART_SOCK].fd == INVALID_FD)
      return PART_IN;
   return PART_SOCK;
}

//Return the default "part" to read from for "channel".
private ChannelFdKind
channel_part_read(Channel* channel) {
   if (channel->fds[PART_SOCK].fd == INVALID_FD)
      return PART_OUT;
   return PART_SOCK;
}

//Return the mode of "channel"/"part" If "channel" is invalid returns CH_MODE_JSON.
private ChannelMode
channel_get_mode(Channel* channel, ChannelFdKind part) {
   if (!channel)
      return CH_MODE_JSON;
   return channel->fds[part].ch_mode;
}

//The timeout of "channel"/"part"
private int
channel_get_timeout(Channel *channel, ChannelFdKind part) {
   return channel->fds[part].ch_timeout;
}

pub void
f_ch_canread(Var* argvars, Var* returnVar) {
   returnVar->number = 0;

   Channel* channel = get_channel_arg(&argvars[0], false, false, 0);
   if (channel)
      returnVar->number = channel_has_readahead(channel, PART_SOCK)
                || channel_has_readahead(channel, PART_OUT)
                || channel_has_readahead(channel, PART_ERR);
}

pub void
f_ch_close(Arr(Var) argvars, Var*) {
   Channel* channel = get_channel_arg(&argvars[0], true, false, 0);
   if (channel) {
      channel_close(channel, false);
      channel_clear(channel);
   }
}

pub void
f_ch_close_in(Arr(Var) argvars, Var*) {

   Channel* channel = get_channel_arg(&argvars[0], true, false, 0);
   if (channel)
      channel_close_in(channel);
}

pub void
f_ch_getbufnr(Arr(Var) argvars, Var* returnVar) {
   returnVar->number = -1;

   Channel* channel = get_channel_arg(&argvars[0], false, false, 0);
   if (!channel)
      return;

   Byte* what = tv_get_string(&argvars[1]);
   int part;
   if (STRCMP(what, "err") == 0)
      part = PART_ERR;
   ei (STRCMP(what, "out") == 0)
      part = PART_OUT;
   ei (STRCMP(what, "in") == 0)
      part = PART_IN;
   else
      part = PART_SOCK;
   if (channel->fds[part].bookref.c != NULL)
   returnVar->number =
       channel->fds[part].bookref.c->fiNum;
}

pub void
f_ch_getjob(Arr(Var) argvars, Var* returnVar) {
   Channel* channel = get_channel_arg(&argvars[0], false, false, 0);
   if (channel)
      return;

   returnVar->tag = VAR_JOB;
   returnVar->job = channel->job;
   if (channel->job != NULL)
      incRefCount(channel->job);
}

pub void
f_ch_info(Arr(Var) argvars, Var* returnVar) {
   Channel* channel = get_channel_arg(&argvars[0], false, false, 0);
   if (channel) {
      allocReturnDict(returnVar);
      channelInfoIntoDict(channel, OUT returnVar->bag);
   } 
}

pub void
f_ch_open(Arr(Var) argvars, Var* returnVar) {
   returnVar->tag = VAR_CHANNEL;
   returnVar->channel = channel_open_func(argvars);
}

pub void
f_ch_read(Arr(Var) argvars, Var* returnVar) {
   commonChannelRead(argvars, returnVar, false, false);
}

pub void
f_ch_readblob(Arr(Var) argvars, Var* returnVar) {
   commonChannelRead(argvars, returnVar, true, true);
}

pub void
f_ch_readraw(Arr(Var) argvars, Var* returnVar) {
   commonChannelRead(argvars, returnVar, true, false);
}

pub void
f_ch_evalexpr(Arr(Var) argvars, Var* returnVar) {
   ch_expr_common(argvars, returnVar, true);
}

pub void
f_ch_sendexpr(Arr(Var) argvars, Var* returnVar) {
   ch_expr_common(argvars, returnVar, false);
}

pub void
f_ch_evalraw(Arr(Var) argvars, Var* returnVar) {
   ch_raw_common(argvars, returnVar, true);
}

pub void
f_ch_sendraw(Arr(Var) argvars, Var* returnVar) {
   ch_raw_common(argvars, returnVar, false);
}

pub void
f_ch_setoptions(Arr(Var) argvars, Var*) {
   Channel* channel = get_channel_arg(&argvars[0], false, false, 0);
   if (!channel)
      return;
      
   JobOptions opt;
   CLEAR_POINTER(&opt);
   if (get_job_options(&argvars[1], OUT &opt, JO_CB_ALL + JO_TIMEOUT_ALL + JO_MODE_ALL, 0) == OK)
      channel_set_options(channel, &opt);
   free_job_options(&opt);
}

pub void
f_ch_status(Arr(Var) argvars, Var* returnVar) {
   JobOptions opt;
   int part = -1;

   //return an empty string by default
   returnVar->tag = VAR_STRING;
   returnVar->string = NULL;

   Channel* channel = get_channel_arg(&argvars[0], false, false, 0);

   if (argvars[1].tag != VAR_UNKNOWN) {
      CLEAR_POINTER(&opt);
      if (get_job_options(&argvars[1], OUT &opt, JO_PART, 0) == OK && (opt.set & JO_PART))
         part = opt.part;
   }

   returnVar->string = copyStr(channel_status(channel, part));
}

//Get a string with information about the channel in "varp" into "builder".
//"builder" must be at least NUMBUFLEN long.
pub void
channel_to_string_buf(OUT CS builder, Var* varp) {
   Channel *channel = varp->channel;
   CS status = channel_status(channel, -1);

   if (channel)
      eeSnprintf(builder, NUMBUFLEN, "channel %d %s", channel->id, status);
   else
      eeSnprintf(builder, NUMBUFLEN, "channel %s", status);
}

//Build "argv[argc]" from the list "l".
//"argv[argc]" is set to NULL; Return FAIL when out of memory.
private int
build_argv_from_list(List *l, Byte*** argv, int *argc) {
   //Pass argv[] to chCallShell().
   *argv = ALLOC_MULT(CS, l->len + 1);
   *argc = 0;
   ListItem* li;
   FOR_ALL_LIST_ITEMS(l, li) {
      CS s = convertVarToStringSingleUse(&li->c);
      if (!s) {
         for (int i = 0; i < *argc; ++i) {
            EE_CLEAR((*argv)[i]);
         } 
         (*argv)[0] = NULL;
         return FAIL;
      }
      (*argv)[*argc] = copyStr(s);
      *argc += 1;
   }
   (*argv)[*argc] = NULL;
   return OK;
}

//}}}
//{{{channels, shell jobs and signals

private void sigcont_handler(int);
private void deathtrap(int) ;
static void catch_sigusr1(int);
private void catch_sigpwr(int);

//{{{signal stack

//Support for using the signal stack.
//This helps when we run out of stack space, which causes a SIGSEGV.  The
//signal handler then must run on another stack, since the normal stack is completely full.


//Get a size of signal stack. Preference (if available): sysconf > SIGSTKSZ > guessed size
private Long get_signal_stack_size(void) {
   Long size = -1;

   //return size only if sysconf doesn't return an error
   if ((size = sysconf(_SC_SIGSTKSZ)) > -1)
      return size;

   //if sysconf() isn't available or gives error, return SIGSTKSZ if defined
   return SIGSTKSZ;
}


//}}}

//Send SIGINT to a child process if "c" is an interrupt character.
private void
may_send_sigint(Unt c, ProId pid, ProId wpid) {
   if (c == Ctrl_C || c == extraInterruptCharG) {
      kill(-pid, SIGINT);
   if (wpid > 0)
      kill(wpid, SIGINT);
   }
}

//Wait for process "child" to end. Return "child" if it exited properly, <= 0 on error.
private ProId
wait4pid(ProId child, waitstatus *status) {
   ProId wait_pid = 0;
   long delay_msec = 1;

   while (wait_pid != child) {
      //When compiled with Python threads are probably used, in which case wait() sometimes hangs
      //for no obvious reason.  Use waitpid() instead and loop (like the GUI). Also needed for 
      //other interfaces, they might call system().
      wait_pid = waitpid(child, status, WNOHANG);
      if (wait_pid == 0) {
         //Wait for 1 to 10 msec before trying again.
         mch_delay(delay_msec, MCH_DELAY_IGNOREINPUT | MCH_DELAY_SETTMODE);
         if (++delay_msec > 10)
            delay_msec = 10;
         continue;
      }
      if (wait_pid <= 0 && errno == ECHILD)
         break;
    }
    return wait_pid;
}

//{{{shell interaction

private void
writeFromCurBookToShell(int fromShell, int toShell) {
   LineNr lnum = curBook->opStart.lnum;
   Unt written = 0;
   CS lp = ml_get(lnum);
   Unt lplen = (Unt)ml_get_len(lnum);

   close(fromShell);
   for (;;) {
      int len;
      if (lplen == 0)
         len = 0;
      ei (lp[written] == NL)
         //NL -> ZERO translation
         len = write(toShell, "", (Unt)1);
      else {
         CS s = firstOccurrence(lp + written, NL);
         len = write(
            toShell, 
            (char *)lp + written,
            s ? (Unt)(s - (lp + written)) : lplen - written 
         );
      }
      if (len == (int)(lplen - written)) {
         //Finished a line, add a NL, unless this line should not have one.
         if (lnum != curBook->opEnd.lnum
               || (lnum != curBook->noEolLnum && (lnum != curBook->mem.lineCount))
         )
             (void)write(toShell, "\n", (Unt)1);
         ++lnum;
         if (lnum > curBook->opEnd.lnum) {
            //finished all the lines, close pipe
            close(toShell);
            break;
         }
         lp = ml_get(lnum);
         lplen = ml_get_len(lnum);
         written = 0;
      } ei (len > 0)
         written += (Unt)len;
   }
}

//Don't use system(), use fork()/exec().
private PolyWithStatus
callShellImpl(Text cmd, Unt opt){   //SHELL_*, see eegl.h
   TermInputMode tmode = cur_tmode;
   ProId wpid = 0;
   ProId wait_pid = 0;
   int status = -1;
   int pty_master_fd = -1; //for pty's
   int pipeToShell[2];      //for pipes
   int pipeFromShell[2];
   Boole did_termSetMode = false;   //termSetMode(TMODE_RAW) called
   PolyWithStatus retVal = {};

   termOutFlush();
   if ((opt & SHELL_COOKED) != 0)
      termSetMode(TMODE_COOK);      //set to normal mode
   if (tmode == TMODE_RAW)
      //The shell may have messed with the mode, always set it later.
      cur_tmode = TMODE_UNKNOWN;
   Multistring argv = chBuildArgv(cmd);

   if ((opt & (SHELL_READ|SHELL_WRITE)) != 0) {
      Boole pipeError = pipe(pipeToShell) < 0;
      if (!pipeError) {            //pipe create OK
         pipeError = (pipe(pipeFromShell) < 0);
         if (pipeError) {          //pipe create failed
            close(pipeToShell[0]);
            close(pipeToShell[1]);
         }
      }
      if (pipeError) {
         msg_puts(_("\nCannot create pipes\n"));
         termOutFlush();
         goto skipIfError;
      }
   }

   SIGSET_DECL(curset)
   BLOCK_SIGNALS(&curset);
   ProId pid = fork();   //maybe we should use vfork()
   if (pid == -1) {
      UNBLOCK_SIGNALS(&curset);

      msg_puts(_("\nCannot fork\n"));
      if ((opt & (SHELL_READ|SHELL_WRITE)) != 0) {
         close(pipeToShell[0]);
         close(pipeToShell[1]);
         close(pipeFromShell[0]);
         close(pipeFromShell[1]);
      }
      goto skipIfError;
   } 
   
   if (pid == 0) {   //child
      reset_signals(); //handle signals normally
      UNBLOCK_SIGNALS(&curset);

      if (ch_log_active()) {
         lo("closing channel log in the child process");
         ch_logfile(S"", S"");
      }

      if ((opt & SHELL_SHOW_MSG) == 0 || (opt & SHELL_EXPAND) != 0) {
         //Don't want to show any message from the shell. Can't just close stdout and stderr 
         //though, because some systems will break if you try to write to them after that, so 
         //we must use dup() to replace them with something else -- webb
         //Connect stdin to /dev/null too, so ":n `cat`" doesn't hang while waiting for input.
         int fd = open("/dev/null", O_RDWR | O_EXTRA, 0);
         fclose(stdin);
         fclose(stdout);
         fclose(stderr);

         //If any of these open()'s and dup()'s fail, we just continue anyway. It's not fatal, 
         //and on most systems it will make no difference at all. On a few it will cause the 
         //execvp() to exit with a non-zero status even when the completion could be done, 
         //which is nothing too serious. If the open() or dup() failed we'd just do the same 
         //thing ourselves anyway -- webb
         if (fd >= 0) {
            (void)dup(fd); //To replace stdin  (fd 0)
            (void)dup(fd); //To replace stdout (fd 1)
            (void)dup(fd); //To replace stderr (fd 2)

            //Don't need this now that we've duplicated it
            close(fd);
         }
      } ei ((opt & (SHELL_READ|SHELL_WRITE)) != 0) {
         set_default_child_environment(false);

         //stderr is only redirected when using the GUI, so that a program like gpg can still 
         //access the terminal to get a passphrase using stderr.
         //set up stdin for the child
         close(pipeToShell[1]);
         close(0);
         (void)dup(pipeToShell[0]);
         close(pipeToShell[0]);

         //set up stdout for the child
         close(pipeFromShell[0]);
         close(1);
         (void)dup(pipeFromShell[1]);
         close(pipeFromShell[1]);
      }

      //There is no type cast for the argv, because the type may be different on different 
      //machines. This may cause a warning message with strict compilers, don't worry about it.
      //Call _exit() instead of exit() to avoid closing the connection
      //to the Wayland server (esp. with GTK, which uses atexit()).
      execvp((char*)argv.c[0], (char**)argv.c);
      _exit(EXEC_FAILED);       //exec failed, return failure code
   } else {        //parent
      //While child is running, ignore terminating signals.
      //But do catch CTRL-C, so that "gotInterruptG" is set.
      catch_signals(SIG_IGN, SIG_ERR);
      catch_int_signal();
      UNBLOCK_SIGNALS(&curset);
      ++dontCheckJobEndedP;
      
      //Pipe stdin/stdout to/from the external command.
# define BUFLEN 100      //length for buffer, pseudo tty limit is 128
      Byte buffer[BUFLEN + 1];
      int buffer_off = 0;   //valid bytes in buffer[]
      Byte ta_buf[BUFLEN + 1];   //TypeAHead
      int typeAheadLen = 0;      //valid bytes in ta_buf[]
      int len;

      close(pipeToShell[0]);
      close(pipeFromShell[1]);
      int toShell = pipeToShell[1];
      int fromShell = pipeFromShell[0];

      //Write to the child if there are typed characters. Read from the child if there are 
      //characters available. Repeat the reading a few times if more characters are available. 
      //Need to check for typed keys now and then, but not too often (delays when no chars are 
      //available). This loop is quit if no characters can be read from the pty (waitForChar 
      //detected special condition), or there are no characters available and the child has exited.
      //Only check if the child has exited when there is no more output. The child may exit 
      //before all the output has been printed.
      //
      //Currently this busy loops! This can probably dead-lock when the write blocks!
      Boole p_more_save = p_more;
      p_more = false;
      Unt modeSaved = stateG;
      stateG = MODE_EXTERNCMD;   //don't redraw at window resize

      //Fork a process that will write the lines to the external program.
      if ((opt & SHELL_WRITE) != 0) { 
         if ((wpid = fork()) == -1) {
            msg_puts(_("\nCannot fork\n"));
         } ei (wpid == 0) { //child
            writeFromCurBookToShell(fromShell, toShell);
            _exit(0);
         } else { //parent
            close(toShell);
            toShell = -1;
         }
      } 

      int unreadCnt = 0;
      Elapsed start_tv;
      timespec_get(OUT &start_tv, TIME_UTC);
      for (;;) {
         //Check if keys have been typed, write them to the child if there are any. Don't do this 
         //if we are expanding wild cards (would eat typeahead). Don't do this when filtering and 
         //terminal is in cooked mode, the shell command will handle the I/O.  Avoids that a typed 
         //password is echoed for ssh or gpg command. Don't get characters when the child has 
         //already finished (wait_pid == 0). Don't read characters unless we didn't get output for a
         //while (unreadCnt > 4), avoids that ":r !ls" eats typeahead.
         
         len = 0;
         if ((opt & SHELL_EXPAND) == 0
             && ((opt & (SHELL_READ|SHELL_WRITE|SHELL_COOKED))
                     != (SHELL_READ|SHELL_WRITE|SHELL_COOKED))
             && wait_pid == 0
             && (typeAheadLen > 0 || unreadCnt > 4)
         ){
            if (typeAheadLen == 0) {
               //Get extra characters when we don't have any. Reset the counter and timer.
               unreadCnt = 0;
               timespec_get(OUT &start_tv, TIME_UTC);
               len = ui_inchar(ta_buf, BUFLEN, 10L, 0);
            }
            if (typeAheadLen > 0 || len > 0) {
              //For pipes:
              //Check for CTRL-C: send interrupt signal to child.
              //Check for CTRL-D: EOF, close pipe to child.
              if (len == 1) {
                  //Send SIGINT to the child's group or all processes in our group.
                  may_send_sigint(ta_buf[typeAheadLen], pid, wpid);

                  if (pty_master_fd < 0 && toShell >= 0 && ta_buf[typeAheadLen] == Ctrl_D) {
                     close(toShell);
                     toShell = -1;
                  }
               }

               //Remove Eegl-specific codes from the input.
               len = term_replace_keycodes(ta_buf, typeAheadLen, len);

               //For pipes: echo the typed characters. For a pty this does not seem to work.
               if (pty_master_fd < 0) {
                  for (int i = typeAheadLen; i < typeAheadLen + len; ++i) {
                     if (ta_buf[i] == '\n' || ta_buf[i] == '\b')
                        msg_putchar(ta_buf[i]);
                     else
                        msgTranslatedSlice((Text){ta_buf + i, 1});
                  }
                  windgoto(msgRowG, msgColG);
                  termOutFlush();
               }

               typeAheadLen += len;

               //Write the characters to the child, unless EOF has been typed for pipes. Write 
               //one character at a time, to avoid losing too much typeahead.
               //When writing buffer lines, drop the typed characters (only check for CTRL-C).
               if ((opt & SHELL_WRITE) != 0)
                  typeAheadLen = 0;
               ei (toShell >= 0) {
                  len = write(toShell, (char *)ta_buf, (Unt)1);
                  if (len > 0) {
                     typeAheadLen -= len;
                     MEMMOVE(ta_buf, ta_buf + len, typeAheadLen);
                  }
               }
            }
         }

         if (gotInterruptG) {
            //CTRL-C sends a signal to the child, we ignore it ourselves
            kill(-pid, SIGINT);
            if (wpid > 0)
               kill(wpid, SIGINT);
            gotInterruptG = false;
         }

         //Check if the child has any characters to be printed. Read them and store them in 
         //a polystring. Repeat this as long as there is something to do, avoid the 10ms wait
         //for mch_inchar(), or sending typeahead characters to the external process.
         //TODO: This should handle escape sequences, compatible to some terminal (vt52?).
         ++unreadCnt;
         while (uiRealWaitForChar(fromShell, 10L, NULL)) {
            len = fiReadEintr(
                  fromShell, OUT buffer + buffer_off, (Unt)(BUFLEN - buffer_off)
            );
            if (len <= 0)          //end of file or error
               goto finished;

            unreadCnt = 0;
            int prev = 0;
            for (Unt i = 0; i < (Unt)len; ++i) {
               if (buffer[i] == NL || buffer[i] == ZERO) {
                  appendToPoly((Text){buffer + prev, i - prev}, OUT &retVal.c);
                  prev = i;
               }
            }

            windgoto(msgRowG, msgColG);
            cursor_on();
            termOutFlush();
            if (gotInterruptG)
               break;

            if (wait_pid == 0) {
               Long msec = motElapsedMs(start_tv);

               //Avoid that we keep looping here without checking for a CTRL-C for a long time.
               //Don't break out too often to avoid losing typeahead.
               if (msec > 2000) {
                  unreadCnt = 5;
                  break;
               }
            }
         }

         //If we already detected the child has finished, continue
         //reading output for a short while.  Some text may be buffered.
         if (wait_pid == pid) {
            if (unreadCnt < 5)
               continue;
            break;
         }

         //Check if the child still exists, before checking for
         //typed characters (otherwise we would lose typeahead).
         wait_pid = waitpid(pid, &status, WNOHANG);
         if ((wait_pid == (ProId)-1 && errno == ECHILD)
             || (wait_pid == pid && WIFEXITED(status))
         ) {
            //Don't break the loop yet, try reading more characters from "fromShell" first. 
            //When using pipes there might still be something to read and then we'll break the 
            //loop at the "break" above.
            wait_pid = pid;
         } else
            wait_pid = 0;
      }
finished:
      p_more = p_more_save;

      //Give all typeahead that wasn't used back to ui_inchar().
      if (typeAheadLen != 0)
         ui_inBytendo(ta_buf, typeAheadLen);
      stateG = modeSaved;
      if (toShell >= 0)
         close(toShell);
      close(fromShell);

      //Wait until our child has exited.
      //Ignore wait() returning pids of other children and returning because of some signal 
      //like SIGWINCH. Don't wait if wait_pid was already set above, indicating the
      //child already exited.
      if (wait_pid != pid)
         (void)wait4pid(pid, &status);

      //Make sure the child that writes to the external program is dead.
      if (wpid > 0) {
         kill(wpid, SIGKILL);
         wait4pid(wpid, NULL);
      }

      --dontCheckJobEndedP;

      //Set to raw mode right now, otherwise a CTRL-C after catch_signals() will kill Eegl.
      if (tmode == TMODE_RAW)
         termSetMode(TMODE_RAW);
      did_termSetMode = true;
      setupSignalHandlers();

      if (WIFEXITED(status)) {
         //LINTED avoid "bitwise operation on signed value"
         int retStatus = WEXITSTATUS(status);
         if (retStatus != 0 && !emsg_silent) {
            if (retStatus == EXEC_FAILED) {
               msg_puts(_("\nCannot execute shell "));
               msg_outtrans(S"bash");
               msg_putchar('\n');
            } ei ((opt & SHELL_SILENT) == 0) {
               msg_puts(_("\nshell returned "));
               msg_outnum((long)retStatus);
               msg_putchar('\n');
            }
         }
      } else
         msg_puts(_("\nCommand terminated\n"));
   }
   
skipIfError: 

   if (!did_termSetMode && tmode == TMODE_RAW)
      termSetMode(TMODE_RAW);
   freeMultistring(OUT &argv);

   return retVal;
}

//Call shell. Call chCallShell
pub PolyWithStatus
chCallShell(Text shellComm, Unt opt) {
   if (p_verbose > 3) {
      verbose_enter();
      //TODO msg print out the full multistring
      smsg(_("Calling shell to execute: %s"), shellComm.c);
      msgPutcharDeco('\n', 0);
      cursor_on();
      verbose_leave();
   }

   //The external command may update a tags file, clear cached tags.
   tag_freematch();

   PolyWithStatus retval = callShellImpl(shellComm, opt);
   //Check the portal size, in case it changed while executing the external command.
   shell_resized_check();

   return retval;
}

//}}}

pub int
mch_create_pty_channel(Job* job, JobOptions* options) {
   int pty_master_fd = -1;
   int pty_slave_fd = -1;

   open_pty(&pty_master_fd, &pty_slave_fd, &job->ttyOut, &job->ttyIn);
   if (pty_master_fd < 0 || pty_slave_fd < 0)
      return FAIL;
   close(pty_slave_fd);

   Channel* channel = add_channel();
   if (channel == NULL) {
      close(pty_master_fd);
      return FAIL;
   }
   if (job->ttyOut != NULL)
      ch_log(channel, "using pty %s on fd %d", job->ttyOut, pty_master_fd);
   job->channel = channel;  //refcount was set by add_channel()
   channel->ch_keep_open = true;

   //Only set the pty_master_fd for stdout, do not duplicate it for stderr,
   //it only needs to be read once.
   channel_set_pipes(channel, pty_master_fd, pty_master_fd, INVALID_FD);
   channel_set_job(channel, job, options);
   return OK;
}

//Check for CTRL-C typed by reading all available characters.
//In cooked mode we should get SIGINT, no need to check.
pub void
chBreakcheck(Boole force) {
   if ((mch_cur_tmode == TMODE_RAW || force) && uiRealWaitForChar(read_cmd_fd, 0L, NULL)) {
      fill_input_buf(false);
   } 
}

//Register a signal handler. Return the old handler for this signal
pub SigHandler
motSignalHandler(int sig, SigHandler func) { //:motSignalHandler
   //Modern implementation: use sigaction().
   SignalAction sa, old;
   SignalSet curset;

   if (sigprocmask(SIG_BLOCK, NULL, &curset) == -1)
      return SIG_ERR;

   int blocked = sigismember(&curset, sig);

   if (func == SIG_HOLD) {
      if (blocked)
         return SIG_HOLD;

      sigemptyset(&curset);
      sigaddset(&curset, sig);

      if (sigaction(sig, NULL, &old) == -1 || sigprocmask(SIG_BLOCK, &curset, NULL) == -1)
         return SIG_ERR;
      return old.sa_handler;
   }

   if (blocked) {
      sigemptyset(&curset);
      sigaddset(&curset, sig);

      if (sigprocmask(SIG_UNBLOCK, &curset, NULL) == -1)
         return SIG_ERR;
   }

   sa.sa_handler = func;
   sigemptyset(&sa.sa_mask);
   sa.sa_flags = SA_RESTART;
   if (sigaction(sig, &sa, &old) == -1)
      return SIG_ERR;
   return blocked ? SIG_HOLD: old.sa_handler;
}

pub void
mch_early_init(void) {
   //Setup an alternative stack for signals. Helps to catch signals when running out of stack 
   //space. Use of sigaltstack() is preferred, it's more portable. Ignore any errors.
   signal_stack = alloc(get_signal_stack_size());
   init_signal_stack();
}

//return process ID
pub long
mch_get_pid(void) {
   return (long)getpid();
}

//return true if process "pid" is still running
pub int
mch_process_running(long pid) {
   //If there is no error the process must be running.
   if (kill(pid, 0) == 0)
      return true;
   //If the error is ESRCH then the process is not running.
   if (errno == ESRCH)
      return false;
   //If the process is running and owned by another user we get EPERM.  With
   //other errors the process might be running, assuming it is then.
   return true;
}

//Open a PTY, with FD for the master and slave side.
//When failing "pty_master_fd" and "pty_slave_fd" are -1.
//When successful both file descriptors are stored and the allocated pty name
//is stored in both "*name1" and "*name2".
private void
open_pty(int* pty_master_fd, int* pty_slave_fd, Byte** name1, Byte** name2) {
   if (name1)
      *name1 = NULL;
   if (name2)
      *name2 = NULL;

   char* tty_name;
   *pty_master_fd = openpty(&tty_name);       //open pty
   if (*pty_master_fd < 0)
      return;

   //O_NOCTTY flag stands for "No Controlling Terminal" and is used inside the open() system call
   //to prevent a terminal device from becoming the controlling terminal of the calling process. 
   *pty_slave_fd = open(tty_name, O_RDWR | O_NOCTTY | O_EXTRA, 0);
   if (*pty_slave_fd < 0) {
      close(*pty_master_fd);
      *pty_master_fd = -1;
   } else {
      if (name1)
         *name1 = copyStr((CS)tty_name);
      if (name2)
         *name2 = copyStr((CS)tty_name);
   }
}

//Add open channels to the poll struct.
//Return the adjusted struct index.
pub int
motChannelPollSetup(int nfd_in, OUT Arr(PollFd) fds_in, OUT int* towait) {
    int nfd = nfd_in;
    PollFd* fds = fds_in;
    ChannelFdKind part;

    for (Channel* channel = firstChannelP; channel; channel = channel->next) {
      for (part = PART_SOCK; part < PART_IN; ++part) {
         ChannelFd* ch_part = &channel->fds[part];

         if (ch_part->fd != INVALID_FD) {
            if (channel->ch_keep_open) {
               //For unknown reason poll() returns immediately for a
               //keep-open channel. Instead of adding it to the fds, add
               //a short timeout and check, like polling.
               if (*towait < 0 || *towait > KEEP_OPEN_TIME)
                  *towait = KEEP_OPEN_TIME;
            } else {
               ch_part->pollIdx = nfd;
               fds[nfd].fd = ch_part->fd;
               fds[nfd].events = POLLIN;
               nfd++;
            }
         } else channel->fds[part].pollIdx = -1;
      }
   }

   return fillIntake(nfd, fds);
}

pub int
motPollCheck(int ret_in, Arr(PollFd) fds) {
   int ret = ret_in;
   ChannelFdKind part;

   for (Channel* channel = firstChannelP; channel; channel = channel->next) {
      int idx;
      for (part = PART_SOCK; part < PART_IN; ++part) {
         idx = channel->fds[part].pollIdx;

         if (ret > 0 && idx != -1 && (fds[idx].revents & POLLIN) != 0) {
            channel_read(channel, part, "motPollCheck");
            --ret;
         } else if (channel->fds[part].fd != INVALID_FD && channel->ch_keep_open) {
            //polling a keep-open channel
            channel_read(channel, part, "channel_poll_check_keep_open");
         }
      }

      ChannelFd* intake = &channel->fds[PART_IN];
      idx = intake->pollIdx;
      if (ret > 0 && idx != -1 && (fds[idx].revents & POLLOUT) != 0) {
         channel_write_input(channel);
         --ret;
      }
   }

   return ret;
}

//}}}
//{{{operating system interaction

//Insert user name for "uid" in s[len]. Return OK if a name found.
pub int
mch_get_uname(uid_t uid, CS s, int len) {
   struct passwd* pw;
   if ((pw = getpwuid(uid)) != NULL && pw->pw_name != NULL && *(pw->pw_name) != ZERO) {
      copySubstrToAllocation(s, (Text){(CS)pw->pw_name, len - 1});
      return OK;
   }
   sprintf((char *)s, "%d", (int)uid);       //assumes s is long enough
   return FAIL;             //a number is not a name
}

//Insert host name is s[len].
pub void
mch_get_host_name(CS s, int len) {
   struct utsname vutsname;

   if (uname(&vutsname) < 0)
      *s = ZERO;
   else
      copySubstrToAllocation(s, (Text){(CS)vutsname.nodename, len - 1});
}

//Set the environment for a child process.
private void
set_child_environment(Long rows, Long columns, CS term, Boole is_terminal) {
   char envbuf[50];

   setenv("TERM", (char*)term, 1);
   sprintf((char *)envbuf, "%ld", rows);
   setenv("ROWS", (char *)envbuf, 1);
   sprintf((char *)envbuf, "%ld", rows);
   setenv("LINES", (char *)envbuf, 1);
   sprintf((char *)envbuf, "%ld", columns);
   setenv("COLUMNS", (char *)envbuf, 1);
   sprintf((char *)envbuf, "%d", 256);
   setenv("COLORS", (char *)envbuf, 1);
   if (is_terminal) {
      setenv("EEGL_TERMINAL", (char *)envbuf, 1);
   }
   setenv("EEGL_SERVERNAME", serverName == NULL ? "" : (char *)serverName, 1);
}

private void
set_default_child_environment(Boole is_terminal) {
   set_child_environment(visibleRowsG, visibleColsG, S"dumb", is_terminal);
}

//}}}
//{{{job runnin' and controllin'

pub int
chJobGetCopyId(Job* job) {
   return job->copyId;
}

pub void
chJobSetCopyId(Job* job, int newVal) {
   job->copyId = newVal;
}

pub Channel*
chJobGetChannel(Job* job) {
   return job->channel;
}

pub Callback
chJobGetExitCb(Job* job) {
   return job->exitCb;
}

pub JobStatus
chJobGetStatus(Job* job) {
   return job->status;
}

pub void
chJobSetStatus(Job* job, JobStatus newVal) {
   job->status = newVal;
}
    
pub Arr(Byte)
chJobGetTty(Job* job, Boole out) {
   if (out) {
      return job->ttyOut;
   } else {
      return job->ttyIn;
   }
}
    
private void
mch_job_start(Byte** argv, Job* job, JobOptions* options, Boole is_terminal) {
   ProId   pid;
   int fd_in[2] = {-1, -1};   //for stdin
   int fd_out[2] = {-1, -1};   //for stdout
   int fd_err[2] = {-1, -1};   //for stderr
   int pty_master_fd = -1;
   int pty_slave_fd = -1;
   Channel* channel = NULL;
   int use_null_for_in = options->ioMode[PART_IN] == JIO_NULL;
   int use_null_for_out = options->ioMode[PART_OUT] == JIO_NULL;
   int use_null_for_err = options->ioMode[PART_ERR] == JIO_NULL;
   int use_file_for_in = options->ioMode[PART_IN] == JIO_FILE;
   int use_file_for_out = options->ioMode[PART_OUT] == JIO_FILE;
   int use_file_for_err = options->ioMode[PART_ERR] == JIO_FILE;
   int use_buffer_for_in = options->ioMode[PART_IN] == JIO_BUFFER;
   int use_out_for_err = options->ioMode[PART_ERR] == JIO_OUT;
   SIGSET_DECL(curset)

   if (use_out_for_err && use_null_for_out)
      use_null_for_err = true;

   //default is to fail
   job->status = JOB_FAILED;

   if (options->jo_pty
          && (!(use_file_for_in || use_null_for_in)
            || !(use_file_for_out || use_null_for_out)
            || !(use_out_for_err || use_file_for_err || use_null_for_err))) {
      open_pty(&pty_master_fd, &pty_slave_fd, &job->ttyOut, &job->ttyIn);
   } 

   //TODO: without the channel feature connect the child to /dev/null?
   //Open pipes for stdin, stdout, stderr.
   if (use_file_for_in) {
      CS fname = options->name[PART_IN];

      fd_in[0] = open((char *)fname, O_RDONLY, 0);
      if (fd_in[0] < 0) {
         showErrFmtMsg(_(e_cant_open_file_str), fname);
         goto failed;
      }
   } ei (!use_null_for_in && (pty_master_fd < 0 || use_buffer_for_in) && pipe(fd_in) < 0) {
      //When writing buffer lines to the input don't use the pty, so that the pipe can be closed 
      //when all lines were written.
      goto failed;
   } 

   if (use_file_for_out) {
      CS fname = options->name[PART_OUT];

      fd_out[1] = open((char *)fname, O_WRONLY | O_CREAT | O_TRUNC, 0644);
      if (fd_out[1] < 0) {
         showErrFmtMsg(_(e_cant_open_file_str), fname);
         goto failed;
      }
   } ei (!use_null_for_out && pty_master_fd < 0 && pipe(fd_out) < 0)
      goto failed;

   if (use_file_for_err) {
      CS fname = options->name[PART_ERR];

      fd_err[1] = open((char *)fname, O_WRONLY | O_CREAT | O_TRUNC, 0600);
      if (fd_err[1] < 0) {
         showErrFmtMsg(_(e_cant_open_file_str), fname);
         goto failed;
      }
   }
   //only create a pipe for the error fd, when either a callback has been setup
   //or pty is not used (e.g. terminal uses pty by default)
   ei (!use_out_for_err && !use_null_for_err
         && (pty_master_fd < 0 || (options->set & JO_ERR_CALLBACK)) && pipe(fd_err) < 0) {
      goto failed;
   } 

   if (!use_null_for_in || !use_null_for_out || !use_null_for_err) {
      if (options->set & JO_CHANNEL) {
         channel = options->jo_channel;
         if (channel)
            ++channel->refCount;
      } else
         channel = add_channel();
      if (!channel)
         goto failed;
      if (job->ttyOut)
         ch_log(channel, "using pty %s on fd %d", job->ttyOut, pty_master_fd);
   }

   BLOCK_SIGNALS(&curset);
   pid = fork();   //maybe we should use vfork()
   if (pid == -1) {
      //failed to fork
      UNBLOCK_SIGNALS(&curset);
      goto failed;
   }
   if (pid == 0) {
      int   null_fd = -1;
      int   stderr_works = true;

      //child
      reset_signals();      //handle signals normally
      UNBLOCK_SIGNALS(&curset);

      if (ch_log_active())
         //close the log file in the child
         ch_logfile(S"", S"");

      //Create our own process group, so that the child and all its
      //children can be kill()ed.  Don't do this when using pipes,
      //because stdin is not a tty, we would lose /dev/tty.
      (void)setsid();

      if (options->jo_term_rows > 0) {
         CS term = termCodesG[KS_NAME];

         //Use 'term' or $TERM if it starts with "xterm", otherwise fall
         //back to "xterm" or "xterm-color".
         if (!term || *term == ZERO || STRNCMP(term, "xterm", 5) != 0) {
            term = S"xterm-256color";
         }
         set_child_environment(
            (long)options->jo_term_rows,
            (long)options->jo_term_cols,
            term,
            is_terminal
         );
      } else
          set_default_child_environment(is_terminal);

      if (options->env != NULL) {
         Bag* dict = options->env;
         EeSetItem* hi;
         int todo = (int)dict->hashTable.count;

         FOR_ALL_HASHTAB_ITEMS(&dict->hashTable, hi, todo) {
            if (!HASHITEM_EMPTY(hi)) {
               Var *item = &bagLookup(hi)->c;

               eeSetenv(hi->hi_key, tv_get_string(item));
               --todo;
            }
         } 
      }

      if (use_null_for_in || use_null_for_out || use_null_for_err) {
         null_fd = open("/dev/null", O_RDWR | O_EXTRA, 0);
         if (null_fd < 0) {
            perror("opening /dev/null failed");
            _exit(OPEN_NULL_FAILED);
         }
      }

      if (pty_slave_fd >= 0) {
         //push stream discipline modules
         setup_slavepty(pty_slave_fd);
         //Try to become controlling tty (probably doesn't work, unless run by root)
         ioctl(pty_slave_fd, TIOCSCTTY, (char *)NULL);
      }

      //set up stdin for the child
      close(0);
      if (use_null_for_in && null_fd >= 0)
         (void)dup(null_fd);
      ei (fd_in[0] < 0)
         (void)dup(pty_slave_fd);
      else
         (void)dup(fd_in[0]);

      //set up stderr for the child
      close(2);
      if (use_null_for_err && null_fd >= 0) {
         (void)dup(null_fd);
         stderr_works = false;
      } ei (use_out_for_err)
         (void)dup(fd_out[1]);
      ei (fd_err[1] < 0)
         (void)dup(pty_slave_fd);
      else
         (void)dup(fd_err[1]);

      //set up stdout for the child
      close(1);
      if (use_null_for_out && null_fd >= 0)
         (void)dup(null_fd);
      ei (fd_out[1] < 0)
         (void)dup(pty_slave_fd);
      else
         (void)dup(fd_out[1]);

      if (fd_in[0] >= 0)
         close(fd_in[0]);
      if (fd_in[1] >= 0)
         close(fd_in[1]);
      if (fd_out[0] >= 0)
         close(fd_out[0]);
      if (fd_out[1] >= 0)
         close(fd_out[1]);
      if (fd_err[0] >= 0)
         close(fd_err[0]);
      if (fd_err[1] >= 0)
         close(fd_err[1]);
      if (pty_master_fd >= 0) {
         close(pty_master_fd); //not used in the child
         close(pty_slave_fd);  //was duped above
      }

      if (null_fd >= 0)
         close(null_fd);

      if (options->currentWorkingDir && mch_chdir(options->currentWorkingDir) != 0)
         _exit(EXEC_FAILED);

      //See above for type of argv.
      execvp((char*)argv[0], (char**)argv);

      if (stderr_works)
          perror("executing job failed");
# ifdef EXITFREE
      //calling free_all_mem() here causes problems. Ignore valgrind
      //reporting possibly leaked memory.
# endif
      _exit(EXEC_FAILED);       //exec failed, return failure code
   }

   //parent
   UNBLOCK_SIGNALS(&curset);

   job->pid = pid;
   job->status = JOB_STARTED;
   job->channel = channel;  //refcount was set above

   if (pty_master_fd >= 0)
      close(pty_slave_fd); //not used in the parent
   //close child stdin, stdout and stderr
   if (fd_in[0] >= 0)
      close(fd_in[0]);
   if (fd_out[1] >= 0)
      close(fd_out[1]);
   if (fd_err[1] >= 0)
      close(fd_err[1]);
   if (channel != NULL) {
      int in_fd = INVALID_FD;
      int out_fd = INVALID_FD;
      int err_fd = INVALID_FD;

      if (!(use_file_for_in || use_null_for_in))
         in_fd = fd_in[1] >= 0 ? fd_in[1] : pty_master_fd;

      if (!(use_file_for_out || use_null_for_out))
         out_fd = fd_out[0] >= 0 ? fd_out[0] : pty_master_fd;

      //When using pty_master_fd only set it for stdout, do not duplicate
      //it for stderr, it only needs to be read once.
      if (!(use_out_for_err || use_file_for_err || use_null_for_err)) {
         if (fd_err[0] >= 0)
            err_fd = fd_err[0];
         ei (out_fd != pty_master_fd)
            err_fd = pty_master_fd;
      }

      channel_set_pipes(channel, in_fd, out_fd, err_fd);
      channel_set_job(channel, job, options);
   } else {
      if (fd_in[1] >= 0)
         close(fd_in[1]);
      if (fd_out[0] >= 0)
         close(fd_out[0]);
      if (fd_err[0] >= 0)
         close(fd_err[0]);
      if (pty_master_fd >= 0)
         close(pty_master_fd);
   }

   //success!
   return;

failed:
   channel_unref(channel);
   if (fd_in[0] >= 0)
      close(fd_in[0]);
   if (fd_in[1] >= 0)
      close(fd_in[1]);
   if (fd_out[0] >= 0)
      close(fd_out[0]);
   if (fd_out[1] >= 0)
      close(fd_out[1]);
   if (fd_err[0] >= 0)
      close(fd_err[0]);
   if (fd_err[1] >= 0)
      close(fd_err[1]);
   if (pty_master_fd >= 0)
      close(pty_master_fd);
   if (pty_slave_fd >= 0)
      close(pty_slave_fd);
}

private CS
mch_job_status(Job* job) {
   int status = -1;
   ProId wait_pid = 0;

   wait_pid = waitpid(job->pid, &status, WNOHANG);
   if (wait_pid == -1) {
      int waitpid_errno = errno;
      if (waitpid_errno == ECHILD && mch_process_running(job->pid))
          //The process is alive, but it was probably reparented (for
          //example by ptrace called by a debugger like lldb or gdb).
          //Note: This assumes that process IDs are not reused.
          return S"run";

      //process must have exited
      if (job->status < JOB_ENDED)
         ch_log(job->channel, "Job no longer exists: %s", strerror(waitpid_errno));
      goto return_dead;
   }
   if (wait_pid == 0)
      return S"run";
   if (WIFEXITED(status)) {
      //LINTED avoid "bitwise operation on signed value"
      job->exitVal = WEXITSTATUS(status);
      if (job->status < JOB_ENDED)
         ch_log(job->channel, "Job exited with %d", job->exitVal);
      goto return_dead;
   }
   if (WIFSIGNALED(status)) {
      job->exitVal = -1;
      job->jv_termsig = get_signal_name(WTERMSIG(status));
      if (job->status < JOB_ENDED && job->jv_termsig != NULL)
          ch_log(job->channel, "Job terminated by signal \"%s\"", job->jv_termsig);
      goto return_dead;
   }
   return S"run";

return_dead:
   if (job->status < JOB_ENDED) {
      job->status = JOB_ENDED;
   } 
   return S"dead";
}

//Send a (deadly) signal to "job". Return FAIL if "how" is not a valid name.
pub int
chSendSignalToJob(Job* job, CS how) {
   int sig = -1;

   if (how[0] == ZERO || STRCMP(how, "term") == 0)
      sig = SIGTERM;
   ei (STRCMP(how, "hup") == 0)
      sig = SIGHUP;
   ei (STRCMP(how, "quit") == 0)
      sig = SIGQUIT;
   ei (STRCMP(how, "int") == 0)
      sig = SIGINT;
   ei (STRCMP(how, "kill") == 0)
      sig = SIGKILL;
   ei (STRCMP(how, "winch") == 0)
      sig = SIGWINCH;
   ei (SAFE_isdigit(*how))
      sig = atoi((char *)how);
   else
      return FAIL;

   //Never kill ourselves!
   if (job->pid != 0) {
      //TODO: have an option to only kill the process, not the group?
      kill(-job->pid, sig);
      kill(job->pid, sig);
   }

   return OK;
}

private Job *
mch_detect_ended_job(Job* job_list) {
   int status = -1;

   //Do not do this when waiting for a shell command to finish, we would get
   //the exit value here (and discard it), the exit value obtained there would then be wrong.
   if (dontCheckJobEndedP > 0)
      return NULL;

   ProId wait_pid = waitpid(-1, &status, WNOHANG);
   if (wait_pid <= 0)
      //no process ended
      return NULL;
   for (Job* job = job_list; job; job = job->next) {
      if (job->pid == wait_pid) {
         if (WIFEXITED(status))
            //LINTED avoid "bitwise operation on signed value"
            job->exitVal = WEXITSTATUS(status);
         ei (WIFSIGNALED(status)) {
            job->exitVal = -1;
            job->jv_termsig = get_signal_name(WTERMSIG(status));
         }
         if (job->status < JOB_ENDED) {
            ch_log(job->channel, "Job ended");
            job->status = JOB_ENDED;
         }
         return job;
      }
   }
   return NULL;
}

private int
handle_mode(Var* item, JobOptions* opt, ChannelMode* modep, int jo) {
   CS val = tv_get_string(item);
   opt->set |= jo;
   if (STRCMP(val, "nl") == 0)
      *modep = CH_MODE_NL;
   ei (STRCMP(val, "raw") == 0)
      *modep = CH_MODE_RAW;
   ei (STRCMP(val, "json") == 0)
      *modep = CH_MODE_JSON;
   ei (STRCMP(val, "lsp") == 0)
      *modep = CH_MODE_LSP;
   else {
      showErrFmtMsg(_(e_invalid_argument_str), val);
      return FAIL;
   }
   return OK;
}

private int
handle_io(Var* item, ChannelFdKind part, JobOptions* opt) {
   CS val = tv_get_string(item);

   opt->set |= JO_OUT_IO << (part - PART_OUT);
   if (STRCMP(val, "null") == 0)
      opt->ioMode[part] = JIO_NULL;
   ei (STRCMP(val, "pipe") == 0)
      opt->ioMode[part] = JIO_PIPE;
   ei (STRCMP(val, "file") == 0)
      opt->ioMode[part] = JIO_FILE;
   ei (STRCMP(val, "buffer") == 0)
      opt->ioMode[part] = JIO_BUFFER;
   ei (STRCMP(val, "out") == 0 && part == PART_ERR)
      opt->ioMode[part] = JIO_OUT;
   else {
      showErrFmtMsg(_(e_invalid_argument_str), val);
      return FAIL;
   }
   return OK;
}

private void
unref_job_callback(Callback *cb) {
   if (cb->cb_partial)
      partial_unref(cb->cb_partial);
   ei (cb->name) {
      func_unref(cb->name);
   if (cb->needsFreeing)
      eeglFree(cb->name);
   }
}

//Free any members of a JobOptions.
pub void
free_job_options(JobOptions* opt) {
   unref_job_callback(&opt->jo_callback);
   unref_job_callback(&opt->jo_out_cb);
   unref_job_callback(&opt->jo_err_cb);
   unref_job_callback(&opt->closeCb);
   unref_job_callback(&opt->exitCb);

   if (opt->env)
      bagUnref(opt->env);
}

//Get the PART_ number from the first character of an option name.
private int
part_from_char(int c) {
   return c == 'i' ? PART_IN : c == 'o' ? PART_OUT: PART_ERR;
}

//Clear the data related to "job".
pub void
mch_clear_job(Job* job) {
   //call waitpid because child process may become zombie
   (void)waitpid(job->pid, NULL, WNOHANG);
}


//Get the option entries from the dict in "tv", parse them and put the result in "opt".
//Only accept JO_ options in "supported" and JO2_ options in "supported2".
//If an option value is invalid, return FAIL.
pub int
get_job_options(Var* tv, OUT JobOptions* opt, int supported, int supported2) {
   Var* item;
   CS val;
   EeSetItem* hi;
   ChannelFdKind part;

   if (tv->tag == VAR_UNKNOWN)
      return OK;
   if (tv->tag != VAR_BAG) {
      emsg(_(e_dictionary_required));
      return FAIL;
   }
   Bag* dict = tv->bag;
   if (!dict)
      return OK;

   int todo = (int)dict->hashTable.count;
   FOR_ALL_HASHTAB_ITEMS(&dict->hashTable, hi, todo) {
      if (!HASHITEM_EMPTY(hi)) {
         item = &bagLookup(hi)->c;

         if (STRCMP(hi->hi_key, "mode") == 0) {
            if (!(supported & JO_MODE))
                break;
            if (handle_mode(item, opt, &opt->mode, JO_MODE) == FAIL)
                return FAIL;
         } ei (STRCMP(hi->hi_key, "in_mode") == 0) {
            if (!(supported & JO_IN_MODE))
                break;
            if (handle_mode(item, opt, &opt->jo_in_mode, JO_IN_MODE) == FAIL)
                return FAIL;
          } ei (STRCMP(hi->hi_key, "out_mode") == 0) {
            if (!(supported & JO_OUT_MODE))
               break;
            if (handle_mode(item, opt, &opt->jo_out_mode, JO_OUT_MODE) == FAIL)
               return FAIL;
         } ei (STRCMP(hi->hi_key, "err_mode") == 0) {
            if (!(supported & JO_ERR_MODE))
                break;
            if (handle_mode(item, opt, &opt->jo_err_mode, JO_ERR_MODE) == FAIL)
                return FAIL;
         } ei (STRCMP(hi->hi_key, "noblock") == 0) {
            if (!(supported & JO_MODE))
               break;
            opt->jo_noblock = tv_get_bool(item);
         } ei (STRCMP(hi->hi_key, "in_io") == 0
                || STRCMP(hi->hi_key, "out_io") == 0
                || STRCMP(hi->hi_key, "err_io") == 0
         ) {
            if (!(supported & JO_OUT_IO))
               break;
            if (handle_io(item, part_from_char(*hi->hi_key), opt) == FAIL)
               return FAIL;
         } ei (STRCMP(hi->hi_key, "in_name") == 0
             || STRCMP(hi->hi_key, "out_name") == 0
             || STRCMP(hi->hi_key, "err_name") == 0
         ) {
            part = part_from_char(*hi->hi_key);

            if (!(supported & JO_OUT_IO))
               break;
            opt->set |= JO_OUT_NAME << (part - PART_OUT);
            opt->name[part] = convertVarToString(item,  opt->nameText[part]);
         } ei (STRCMP(hi->hi_key, "pty") == 0) {
            if (!(supported & JO_MODE))
               break;
            opt->jo_pty = tv_get_bool(item);
         } ei (STRCMP(hi->hi_key, "in_buf") == 0
             || STRCMP(hi->hi_key, "out_buf") == 0
             || STRCMP(hi->hi_key, "err_buf") == 0
         ) {
            part = part_from_char(*hi->hi_key);

            if (!(supported & JO_OUT_IO))
               break;
            opt->set |= JO_OUT_BUF << (part - PART_OUT);
            opt->ioText[part] = tv_get_number(item);
            if (opt->ioText[part] <= 0) {
               showErrFmtMsg(_(e_invalid_value_for_argument_str_str), hi->hi_key, tv_get_string(item));
               return FAIL;
            }
            if (bookFindFileByBookNr(opt->ioText[part]) == NULL) {
               showErrFmtMsg(_(e_book_nr_does_not_exist), (long)opt->ioText[part]);
               return FAIL;
            }
         } ei (STRCMP(hi->hi_key, "out_modifiable") == 0
             || STRCMP(hi->hi_key, "err_modifiable") == 0
         ) {
            part = part_from_char(*hi->hi_key);

            if (!(supported & JO_OUT_IO))
               break;
            opt->set |= JO_OUT_MODIFIABLE << (part - PART_OUT);
            opt->jo_modifiable[part] = tv_get_bool(item);
         } ei (STRCMP(hi->hi_key, "out_msg") == 0 || STRCMP(hi->hi_key, "err_msg") == 0) {
            part = part_from_char(*hi->hi_key);

            if (!(supported & JO_OUT_IO))
               break;
            opt->set1 |= JO2_OUT_MSG << (part - PART_OUT);
            opt->jo_message[part] = tv_get_bool(item);
         } ei (STRCMP(hi->hi_key, "in_top") == 0 || STRCMP(hi->hi_key, "in_bot") == 0) {
            LineNr *lp;

            if (!(supported & JO_OUT_IO))
               break;
            if (hi->hi_key[3] == 't') {
               lp = &opt->jo_in_top;
               opt->set |= JO_IN_TOP;
            } else {
               lp = &opt->jo_in_bot;
               opt->set |= JO_IN_BOT;
            }
            *lp = tv_get_number(item);
            if (*lp < 0) {
               showErrFmtMsg(_(e_invalid_value_for_argument_str_str), hi->hi_key, tv_get_string(item));
               return FAIL;
            }
         } ei (STRCMP(hi->hi_key, "channel") == 0) {
            if (!(supported & JO_OUT_IO))
               break;
            opt->set |= JO_CHANNEL;
            if (item->tag != VAR_CHANNEL) {
               showErrFmtMsg(_(e_invalid_value_for_argument_str), "channel");
               return FAIL;
            }
            opt->jo_channel = item->channel;
         }
         ei (STRCMP(hi->hi_key, "callback") == 0) {
            if (!(supported & JO_CALLBACK))
                break;
            opt->set |= JO_CALLBACK;
            opt->jo_callback = get_callback(item);
            if (opt->jo_callback.name == NULL) {
               showErrFmtMsg(_(e_invalid_value_for_argument_str), "callback");
               return FAIL;
            }
         } ei (STRCMP(hi->hi_key, "out_cb") == 0) {
            if (!(supported & JO_OUT_CALLBACK))
                break;
            opt->set |= JO_OUT_CALLBACK;
            opt->jo_out_cb = get_callback(item);
            if (opt->jo_out_cb.name == NULL) {
               showErrFmtMsg(_(e_invalid_value_for_argument_str), "out_cb");
               return FAIL;
            }
         } ei (STRCMP(hi->hi_key, "err_cb") == 0) {
            if (!(supported & JO_ERR_CALLBACK))
               break;
            opt->set |= JO_ERR_CALLBACK;
            opt->jo_err_cb = get_callback(item);
            if (opt->jo_err_cb.name == NULL) {
               showErrFmtMsg(_(e_invalid_value_for_argument_str), "err_cb");
               return FAIL;
            }
          } ei (STRCMP(hi->hi_key, "close_cb") == 0) {
            if (!(supported & JO_CLOSE_CALLBACK))
                break;
            opt->set |= JO_CLOSE_CALLBACK;
            opt->closeCb = get_callback(item);
            if (opt->closeCb.name == NULL) {
               showErrFmtMsg(_(e_invalid_value_for_argument_str), "close_cb");
               return FAIL;
            }
         } ei (STRCMP(hi->hi_key, "drop") == 0) {
            int never = false;
            val = tv_get_string(item);

            if (STRCMP(val, "never") == 0)
               never = true;
            ei (STRCMP(val, "auto") != 0) {
               showErrFmtMsg(_(e_invalid_value_for_argument_str_str), "drop", val);
               return FAIL;
            }
            opt->dropNever = never;
         } ei (STRCMP(hi->hi_key, "exit_cb") == 0) {
            if (!(supported & JO_EXIT_CB))
                break;
            opt->set |= JO_EXIT_CB;
            opt->exitCb = get_callback(item);
            if (opt->exitCb.name == NULL) {
               showErrFmtMsg(_(e_invalid_value_for_argument_str), "exit_cb");
               return FAIL;
            }
         } ei (STRCMP(hi->hi_key, "term_name") == 0) {
            if (!(supported2 & JO2_TERM_NAME))
                break;
            opt->set1 |= JO2_TERM_NAME;
            opt->jo_term_name = convertVarToString(item, opt->jo_term_name_buf);
            if (opt->jo_term_name == NULL) {
                showErrFmtMsg(_(e_invalid_value_for_argument_str), "term_name");
                return FAIL;
            }
         } ei (STRCMP(hi->hi_key, "term_finish") == 0) {
            if (!(supported2 & JO2_TERM_FINISH))
               break;
            val = tv_get_string(item);
            if (STRCMP(val, "open") != 0 && STRCMP(val, "close") != 0) {
               showErrFmtMsg(_(e_invalid_value_for_argument_str_str), "term_finish", val);
               return FAIL;
            }
            opt->set1 |= JO2_TERM_FINISH;
            opt->jo_term_finish = *val;
         } ei (STRCMP(hi->hi_key, "term_opencmd") == 0) {
            if (!(supported2 & JO2_TERM_OPENCMD))
               break;
            opt->set1 |= JO2_TERM_OPENCMD;
            CS p = opt->jo_term_opencmd = convertVarToString(item, opt->jo_term_opencmd_buf);
            if (p) {
               //Must have %d and no other %.
               p = firstOccurrence(p, '%');
               if (p && (p[1] != 'd' || firstOccurrence(p + 2, '%') != NULL))
                  p = NULL;
            }
            if (!p) {
                showErrFmtMsg(_(e_invalid_value_for_argument_str), "term_opencmd");
                return FAIL;
            }
         } ei (STRCMP(hi->hi_key, "eof_chars") == 0) {
            if (!(supported2 & JO2_EOF_CHARS))
               break;
            opt->set1 |= JO2_EOF_CHARS;
            opt->jo_eof_chars = convertVarToString(item, opt->jo_eof_chars_buf);
            if (opt->jo_eof_chars == NULL) {
               showErrFmtMsg(_(e_invalid_value_for_argument_str), "eof_chars");
               return FAIL;
            }
          } ei (STRCMP(hi->hi_key, "term_rows") == 0) {
            Boole error = false;

            if (!(supported2 & JO2_TERM_ROWS))
               break;
            opt->set1 |= JO2_TERM_ROWS;
            opt->jo_term_rows = varGetNumberChk(item, OUT &error);
            if (error)
               return FAIL;
            if (opt->jo_term_rows < 0 || opt->jo_term_rows > 1000) {
               showErrFmtMsg(_(e_invalid_value_for_argument_str), "term_rows");
               return FAIL;
            }
         } ei (STRCMP(hi->hi_key, "term_cols") == 0) {
            Boole error = false;

            if ((supported2 & JO2_TERM_COLS) == 0)
               break;
            opt->set1 |= JO2_TERM_COLS;
            opt->jo_term_cols = varGetNumberChk(item, OUT &error);
            if (error)
               return FAIL;
            if (opt->jo_term_cols < 0 || opt->jo_term_cols > 1000) {
               showErrFmtMsg(_(e_invalid_value_for_argument_str), "term_cols");
               return FAIL;
            }
         } ei (STRCMP(hi->hi_key, "vertical") == 0) {
            if ((supported2 & JO2_VERTICAL) == 0)
               break;
            opt->set1 |= JO2_VERTICAL;
            opt->vertical = tv_get_bool(item);
         } ei (STRCMP(hi->hi_key, "curPor") == 0) {
            if (!(supported2 & JO2_CURPOR))
               break;
            opt->set1 |= JO2_CURPOR;
            opt->curPor = tv_get_bool(item);
         } ei (STRCMP(hi->hi_key, "bufnr") == 0) {
            if ((supported2 & JO2_CURPOR) == 0)
               break;
            opt->set1 |= JO2_BUFNR;
            int nr = tv_get_number(item);
            if (nr <= 0) {
               showErrFmtMsg(_(e_invalid_value_for_argument_str_str), hi->hi_key, tv_get_string(item));
               return FAIL;
            }
            opt->jo_bufnr_buf = bookFindFileByBookNr(nr);
            if (opt->jo_bufnr_buf == NULL) {
               showErrFmtMsg(_(e_book_nr_does_not_exist), (long)nr);
               return FAIL;
            }
            if (opt->jo_bufnr_buf->countPortals == 0 || opt->jo_bufnr_buf->term == NULL) {
               showErrFmtMsg(_(e_invalid_argument_str), "bufnr");
               return FAIL;
            }
         } ei (STRCMP(hi->hi_key, "hidden") == 0) {
            if ((supported2 & JO2_HIDDEN) == 0)
               break;
            opt->set1 |= JO2_HIDDEN;
            opt->jo_hidden = tv_get_bool(item);
         } ei (STRCMP(hi->hi_key, "norestore") == 0) {
            if ((supported2 & JO2_NORESTORE) == 0)
               break;
            opt->set1 |= JO2_NORESTORE;
            opt->jo_term_norestore = tv_get_bool(item);
         } ei (STRCMP(hi->hi_key, "term_kill") == 0) {
            if ((supported2 & JO2_TERM_KILL) == 0)
               break;
            opt->set1 |= JO2_TERM_KILL;
            opt->jo_term_kill = convertVarToString(item, opt->jo_term_kill_buf);
            if (!opt->jo_term_kill) {
               showErrFmtMsg(_(e_invalid_value_for_argument_str), "term_kill");
               return FAIL;
            }
         } ei (STRCMP(hi->hi_key, "tty_type") == 0) {
            if (!(supported2 & JO2_TTY_TYPE))
               break;
            opt->set1 |= JO2_TTY_TYPE;
            CS p = convertVarToStringSingleUse(item);
            if (p == NULL) {
               showErrFmtMsg(_(e_invalid_value_for_argument_str), "tty_type");
               return FAIL;
            }
            //Allow empty string, "winpty", "conpty".
            if (!(*p == ZERO || STRCMP(p, "winpty") == 0 || STRCMP(p, "conpty") == 0)) {
               showErrFmtMsg(_(e_invalid_value_for_argument_str), "tty_type");
               return FAIL;
            }
            opt->jo_tty_type = p[0];
         } ei (STRCMP(hi->hi_key, "term_highlight") == 0) {
            if (!(supported2 & JO2_TERM_HIGHLIGHT))
                break;
            opt->set1 |= JO2_TERM_HIGHLIGHT;
            CS p = convertVarToString(item, opt->jo_term_highlight_buf);
            if (!p || *p == ZERO) {
                showErrFmtMsg(_(e_invalid_value_for_argument_str), "term_highlight");
                return FAIL;
            }
            opt->jo_term_highlight = p;
         } ei (STRCMP(hi->hi_key, "term_api") == 0) {
            if (!(supported2 & JO2_TERM_API))
                break;
            opt->set1 |= JO2_TERM_API;
            opt->jo_term_api = convertVarToString(item, opt->jo_term_api_buf);
            if (!opt->jo_term_api) {
                showErrFmtMsg(_(e_invalid_value_for_argument_str), "term_api");
                return FAIL;
            }
         } ei (STRCMP(hi->hi_key, "env") == 0) {
            if ((supported2 & JO2_ENV) == 0)
                break;
            if (item->tag != VAR_BAG) {
                showErrFmtMsg(_(e_invalid_value_for_argument_str), "env");
                return FAIL;
            }
            opt->set1 |= JO2_ENV;
            opt->env = item->bag;
            if (opt->env)
               ++opt->env->refCount;
         } ei (STRCMP(hi->hi_key, "cwd") == 0) {
            if ((supported2 & JO2_CWD) == 0)
               break;
            opt->currentWorkingDir = convertVarToString(item, opt->cwdText);
            if (!opt->currentWorkingDir || !mch_isdir(opt->currentWorkingDir)
                  || mch_access(opt->currentWorkingDir, X_OK) != 0
            ){
               showErrFmtMsg(_(e_invalid_value_for_argument_str), "cwd");
               return FAIL;
            }
            opt->set1 |= JO2_CWD;
         } ei (STRCMP(hi->hi_key, "waittime") == 0) {
            if ((supported & JO_WAITTIME) == 0)
               break;
            opt->set |= JO_WAITTIME;
            opt->jo_waittime = tv_get_number(item);
         } ei (STRCMP(hi->hi_key, "timeout") == 0) {
            if ((supported & JO_TIMEOUT) == 0)
               break;
            opt->set |= JO_TIMEOUT;
            opt->jo_timeout = tv_get_number(item);
         } ei (STRCMP(hi->hi_key, "out_timeout") == 0) {
            if ((supported & JO_OUT_TIMEOUT) == 0)
               break;
            opt->set |= JO_OUT_TIMEOUT;
            opt->jo_out_timeout = tv_get_number(item);
         } ei (STRCMP(hi->hi_key, "err_timeout") == 0) {
            if ((supported & JO_ERR_TIMEOUT) == 0)
               break;
            opt->set |= JO_ERR_TIMEOUT;
            opt->jo_err_timeout = tv_get_number(item);
         } ei (STRCMP(hi->hi_key, "part") == 0) {
            if ((supported & JO_PART) == 0)
               break;
            opt->set |= JO_PART;
            val = tv_get_string(item);
            if (STRCMP(val, "err") == 0)
               opt->part = PART_ERR;
            ei (STRCMP(val, "out") == 0)
               opt->part = PART_OUT;
            else {
               showErrFmtMsg(_(e_invalid_value_for_argument_str_str), "part", val);
               return FAIL;
            }
         } ei (STRCMP(hi->hi_key, "id") == 0) {
            if ((supported & JO_ID) == 0)
               break;
            opt->set |= JO_ID;
            opt->id = tv_get_number(item);
         } ei (STRCMP(hi->hi_key, "stoponexit") == 0) {
            if ((supported & JO_STOPONEXIT) == 0)
               break;
            opt->set |= JO_STOPONEXIT;
            opt->jo_stoponexit = convertVarToString(item, opt->jo_stoponexit_buf);
            if (opt->jo_stoponexit == NULL) {
               showErrFmtMsg(_(e_invalid_value_for_argument_str), "stoponexit");
               return FAIL;
            }
         } ei (STRCMP(hi->hi_key, "block_write") == 0) {
            if ((supported & JO_BLOCK_WRITE) == 0)
               break;
            opt->set |= JO_BLOCK_WRITE;
            opt->jo_block_write = tv_get_number(item);
         } else
            break;
         --todo;
      }
   } 
   if (todo > 0) {
      showErrFmtMsg(_(e_invalid_argument_str), hi->hi_key);
      return FAIL;
   }

   return OK;
}

private Job* firstJobS = NULL;

private void
job_free_contents(Job* job) {
   ch_log(job->channel, "Freeing job");
   if (job->channel) {
      //The link from the channel to the job doesn't count as a reference, thus don't decrement 
      //the refcount of the job. The reference from the job to the channel does count the 
      //reference, decrement it and NULL the reference.  We don't set job_killed, unreferencing the
      //job doesn't mean it stops running.
      job->channel->job = NULL;
      channel_unref(job->channel);
   }
   mch_clear_job(job);

   eeglFree(job->ttyIn);
   eeglFree(job->ttyOut);
   eeglFree(job->jv_stoponexit);
   eeglFree(job->jv_termsig);
   evFreeCallback(&job->exitCb);
   if (job->argv) {
      for (int i = 0; job->argv[i] != NULL; i++)
         eeglFree(job->argv[i]);
      eeglFree(job->argv);
   }
}

//Remove "job" from the list of jobs.
private void
job_unlink(Job* job) {
   if (job->next)
      job->next->prev = job->prev;
   if (!job->prev)
      firstJobS = job->next;
   else
      job->prev->next = job->next;
}

private void
job_free_job(Job* job) {
   job_unlink(job);
   eeglFree(job);
}

private void
job_free(Job* job) {
   if (in_free_unref_items)
      return;

   job_free_contents(job);
   job_free_job(job);
}

private Arr(Job) jobs_to_free = NULL;

//Put "job" on a list to be freed later, when it's no longer referenced.
private void
job_free_later(Job* job) {
   job_unlink(job);
   job->next = jobs_to_free;
   jobs_to_free = job;
}

private void
free_jobs_to_free_later(void) {
   Job* job;

   while (jobs_to_free) {
      job = jobs_to_free;
      jobs_to_free = job->next;
      job_free_contents(job);
      eeglFree(job);
   }
}

#if defined(EXITFREE)
pub void
job_free_all(void) {
   while (firstJobS)
      job_free(firstJobS);
   free_jobs_to_free_later();

   free_unused_terminals();
}
#endif

//true if we need to check if the process of "job" has ended.
private int
job_need_end_check(Job* job) {
   return job->status == JOB_STARTED && (job->jv_stoponexit || job->exitCb.name);
}

//true if the channel of "job" is still useful.
private int
job_channel_still_useful(Job* job) {
   return job->channel != NULL && channel_still_useful(job->channel);
}

//true if the channel of "job" is closeable.
private int
job_channel_can_close(Job* job) {
   return job->channel != NULL && channel_can_close(job->channel);
}

//Return true if the job should not be freed yet.  Do not free the job when
//it has not ended yet and there is a "stoponexit" flag, an exit callback
//or when the associated channel will do something with the job output.
private int
job_still_useful(Job* job) {
    return job_need_end_check(job) || job_channel_still_useful(job);
}

//NOTE: Must call job_cleanup() only once right after the status of "job"
//changed to JOB_ENDED (i.e. after job_status() returned "dead" first or
//mch_detect_ended_job() returned non-NULL).
//If the job is no longer used it will be removed from the list of jobs, and deleted a bit later.
private void
job_cleanup(Job* job) {
   if (job->status != JOB_ENDED)
      return;

   //Ready to cleanup the job.
   job->status = JOB_FINISHED;

   //When only channel-in is kept open, close explicitly.
   if (job->channel)
      ch_close_part(job->channel, PART_IN);

   if (job->nativeCb) { //call the native callback first
      (*job->nativeCb)();
   }
   if (job->exitCb.name) { //call the script callback
      Var argv[3];
      Var returnVar;

      //Invoke the exit callback. Make sure the refcount is > 0.
      
      ch_log(job->channel, "Invoking exit callback %s", job->exitCb.name);
      incRefCount(job);
      argv[0].tag = VAR_JOB;
      argv[0].job = job;
      argv[1].tag = VAR_NUMBER;
      argv[1].number = job->exitVal;
      call_callback(&job->exitCb, -1, &returnVar, 2, argv);
      clearVar(&returnVar);
      decRefCount(job);
      channel_need_redraw = true;
   }

   if (job->channel && job->channel->ch_anonymous_pipe)
      job->channel->isBeingKilled = true;

   //Do not free the job in case the close callback of the associated channel
   //isn't invoked yet and may get information by job_info().
   if (job->refCount == 0 && !job_channel_still_useful(job))
      //The job was already unreferenced and the associated channel was
      //detached, now that it ended it can be freed. However, a caller might
      //still use it, thus free it a bit later.
      job_free_later(job);
}

//Mark references in jobs that are still useful.
pub int
set_ref_in_job(int copyID) {
   int abort = false;
   Var tv;

   for (Job* job = firstJobS; !abort && job != NULL; job = job->next) {
      if (job_still_useful(job)) {
         tv.tag = VAR_JOB;
         tv.job = job;
         abort = abort || set_ref_in_item(&tv, copyID, NULL, NULL);
      }
   } 
   return abort;
}

//Dereference "job".  Note that after this "job" may have been freed.
pub void
job_unref(Job* job) {
   if (!job || --job->refCount > 0)
      return;

   //Do not free the job if there is a channel where the close callback may get the job info.
   if (job_channel_still_useful(job))
      return;

   //Do not free the job when it has not ended yet and there is a
   //"stoponexit" flag or an exit callback.
   if (!job_need_end_check(job)) {
      job_free(job);
   } ei (job->channel != NULL) {
      //Do remove the link to the channel, otherwise it hangs
      //around until Eegl exits. See job_free() for refcount.
      ch_log(job->channel, "detaching channel from job");
      job->channel->job = NULL;
      channel_unref(job->channel);
      job->channel = NULL;
   }
}

pub int
free_unused_jobs_contents(int copyID, int mask) {
   int did_free = false;
   Job* job;

   FOR_ALL_JOBS(job) {
      if ((job->copyId & mask) != (copyID & mask) && !job_still_useful(job)) {
         //Free the channel and ordinary items it contains, but don't
         //recurse into Lists, Dictionaries etc.
         job_free_contents(job);
         did_free = true;
      }
   }
   return did_free;
}

pub void
free_unused_jobs(int copyID, int mask) {
   Job* job_next;

   for (Job* job = firstJobS; job; job = job_next) {
      job_next = job->next;
      if ((job->copyId & mask) != (copyID & mask) && !job_still_useful(job)) {
         //Free the job struct itself.
         job_free_job(job);
      }
   }
}

//Allocate a job. Sets the refcount to one and sets options default.
pub Job *
job_alloc(void) {
   Job* job = ALLOC_CLEAR_ONE(Job);
   job->refCount = 1;
   job->jv_stoponexit = copyStr(S"term");

   if (firstJobS) {
      firstJobS->prev = job;
      job->next = firstJobS;
   }
   firstJobS = job;
   return job;
}

pub void
job_set_options(Job* job, JobOptions* opt) {
   if ((opt->set & JO_STOPONEXIT) != 0) {
      eeglFree(job->jv_stoponexit);
      if (!opt->jo_stoponexit || *opt->jo_stoponexit == ZERO)
         job->jv_stoponexit = NULL;
      else
         job->jv_stoponexit = copyStr(opt->jo_stoponexit);
   }
   if (opt->set & JO_EXIT_CB) {
      evFreeCallback(&job->exitCb);
      if (!opt->exitCb.name || *opt->exitCb.name == ZERO) {
         job->exitCb.name = NULL;
         job->exitCb.cb_partial = NULL;
      } else
         evCopyCallback(&job->exitCb, &opt->exitCb);
   }
}

//Called when Eegl is exiting: kill all jobs that have the "stoponexit" flag.
pub void
job_stop_on_exit(void) {
   Job* job;

   FOR_ALL_JOBS(job) {
      if (job->status == JOB_STARTED && job->jv_stoponexit != NULL)
          chSendSignalToJob(job, job->jv_stoponexit);
   } 
}

//Return true when there is any job that has an exit callback and might exit,
//which means job_check_ended() should be called more often.
pub int
has_pending_job(void) {
   Job* job;

   FOR_ALL_JOBS(job) {
      //Only should check if the channel has been closed, if the channel is
      //open the job won't exit.
      if ((job->status == JOB_STARTED && !job_channel_still_useful(job))
             || (job->status == JOB_FINISHED && job_channel_can_close(job))
      )
         return true;
   }
   return false;
}

#define MAX_CHECK_ENDED 8

//Called once in a while: check if any jobs that seem useful have ended. true if a job did end.
pub int
job_check_ended(void) {
   int did_end = false;

   //be quick if there are no jobs to check
   if (!firstJobS)
      return did_end;

   for (int i = 0; i < MAX_CHECK_ENDED; ++i) {
      //NOTE: mch_detect_ended_job() must only return a job of which the
      //status was just set to JOB_ENDED.
      Job* job = mch_detect_ended_job(firstJobS);
      if (!job)
         break;
         
      did_end = true;
      job_cleanup(job); //may add "job" to jobs_to_free
   }

   //Actually free jobs that were cleaned up.
   free_jobs_to_free_later();

   if (channel_need_redraw) {
      channel_need_redraw = false;
      redraw_after_callback(true, false);
   }
   return did_end;
}

//Create a job and return it.  Implements startJob().
//When "argv_arg" is NULL then "argvars" is used. The returned job has a refcount of one.
//Return NULL when out of memory.
pub Job*
startJob(Arr(Var) argvars, Multistring* argv_arg, JobOptions* opt_arg, Job** term_job) {
   Byte** argv = NULL;
   int argc = 0;
   ArrayList   ga;
   JobOptions   opt;
   ChannelFdKind   part;

   Job* job = job_alloc();

   job->status = JOB_FAILED;
   ga_init2(&ga, sizeof(char*), 20);

   if (opt_arg)
      opt = *opt_arg;
   else {
      //Default mode is NL.
      CLEAR_POINTER(&opt);
      opt.mode = CH_MODE_NL;
      if (get_job_options(&argvars[1], OUT &opt,
             JO_MODE_ALL + JO_CB_ALL + JO_TIMEOUT_ALL + JO_STOPONEXIT + JO_EXIT_CB 
                + JO_OUT_IO + JO_BLOCK_WRITE,
             JO2_ENV + JO2_CWD
         ) == FAIL
      ) {
         goto theend;
      } 
   }

   //Check that when io is "file" that there is a file name.
   for (part = PART_OUT; part < PART_COUNT; ++part) {
      if ((opt.set & (JO_OUT_IO << (part - PART_OUT))
            && opt.ioMode[part] == JIO_FILE
            && (!(opt.set & (JO_OUT_NAME << (part - PART_OUT)))
                   || *opt.name[part] == ZERO))
      ){
         emsg(_(e_io_file_requires_name_to_be_set));
         goto theend;
      }
   } 

   if ((opt.set & JO_IN_IO) && opt.ioMode[PART_IN] == JIO_BUFFER) {
      Book* book = NULL;

      //check that we can find the book before starting the job
      if (opt.set & JO_IN_BUF) {
         book = bookFindFileByBookNr(opt.ioText[PART_IN]);
         if (!book)
            showErrFmtMsg(_(e_book_nr_does_not_exist), (long)opt.ioText[PART_IN]);
      } ei (!(opt.set & JO_IN_NAME)) {
         emsg(_(e_in_io_buffer_requires_in_buf_or_in_name_to_be_set));
      } else
         book = bookFindByName(opt.name[PART_IN], false);
      if (!book)
          goto theend;
      if (!book->mem.mfile) {
         Byte numbuf[NUMBUFLEN];
         CS s;

         if ((opt.set & JO_IN_BUF) != 0) {
            sprintf((char *)numbuf, "%d", opt.ioText[PART_IN]);
            s = numbuf;
         } else
            s = opt.name[PART_IN];
         showErrFmtMsg(_(e_buffer_must_be_loaded_str), s);
         goto theend;
      }
      job->inBook = book;
   }

   job_set_options(job, &opt);

   if (argv_arg->len > 0) {
      //Make a copy of argv_arg for job->argv.
      argv = ALLOC_MULT(CS, argv_arg->len + 1);
      for (Unt i = 0; i < argv_arg->len; i++)
         argv[i] = copyStr(argv_arg->c[i]);
      argv[argc] = NULL;
   } ei (argvars[0].tag == VAR_STRING) {
      //Command is a string.
      
      emsg(_(e_invalid_argument));
   } ei (argvars[0].tag != VAR_LIST || !argvars[0].list || argvars[0].list->len < 1){
      emsg(_(e_invalid_argument));
      goto theend;
   } else {
      List* l = argvars[0].list;
      if (build_argv_from_list(l, &argv, &argc) == FAIL)
         goto theend;

      //Empty command is invalid.
      if (argc == 0 || *skipwhite((CS)argv[0]) == ZERO) {
         emsg(_(e_invalid_argument));
         goto theend;
      }
   }

   job->nativeCb = opt_arg->finishNativeCb;
   //Save the command used to start the job.
   job->argv = argv;

   if (term_job)
      *term_job = job;

   if (ch_log_active()) {
      ArrayList ga;

      ga_init2(&ga, sizeof(char), 200);
      for (int i = 0; i < argc; ++i) {
         if (i > 0)
            ga_concat(&ga, (CS)"  ");
         ga_concat(&ga, (CS)argv[i]);
      }
      ga_append(&ga, ZERO);
      lo("Starting job: %s", (char *)ga.c);
      ga_clear(&ga);
   }
   mch_job_start(argv, job, &opt, term_job != NULL);
   //If the channel is reading from a buffer, write lines now.
   if (job->channel)
      channel_write_in(job->channel);

theend:
   if (argv && argv != job->argv) {
      for (Unt i = 0; argv[i]; i++)
         eeglFree(argv[i]);
      eeglFree(argv);
   }
   free_job_options(&opt);
   return job;
}

//Get the status of "job" and invoke the exit callback when needed.
//The returned string is not allocated.
pub CS
job_status(Job* job) {
   CS result;

   if (job->status >= JOB_ENDED)
      //No need to check, dead is dead.
      result = S"dead";
   ei (job->status == JOB_FAILED)
      result = S"fail";
   else {
      result = mch_job_status(job);
      if (job->status == JOB_ENDED)
         job_cleanup(job);
   }
   return result;
}

//Send a signal to "job".  Implements job_stop(). When "type" is not NULL use this for the type.
//Otherwise use argvars[1] for the type.
pub int
job_stop(Job* job, Arr(Var) argvars, CS type) {
   CS arg;

   if (type)
      arg = (CS)type;
   ei (argvars[1].tag == VAR_UNKNOWN)
      arg = S"";
   else {
      arg = convertVarToStringSingleUse(&argvars[1]);
      if (!arg) {
         emsg(_(e_invalid_argument));
         return 0;
      }
   }
   if (job->status == JOB_FAILED) {
      ch_log(job->channel, "Job failed to start, job_stop() skipped");
      return 0;
   }
   if (job->status == JOB_ENDED) {
      ch_log(job->channel, "Job has already ended, job_stop() skipped");
      return 0;
   }
   ch_log(job->channel, "Stopping job with '%s'", (char *)arg);
   if (chSendSignalToJob(job, arg) == FAIL)
      return 0;

   //Assume that only "kill" will kill the job.
   if (job->channel != NULL && STRCMP(arg, "kill") == 0)
      job->channel->isBeingKilled = true;

   //We don't try freeing the job, obviously the caller still has a reference to it.
   return 1;
}

pub void
invoke_prompt_callback(void) {
   Var argv[2];
   LineNr lnum = curBook->mem.lineCount;

   //Add a new line for the prompt before invoking the callback, so that
   //text can always be inserted above the last line.
   ml_append(lnum, (Byte  *)"", 0, false);
   curPor->cursor.lnum = lnum + 1;
   curPor->cursor.col = 0;

   if (curBook->promptCallback.name == NULL || *curBook->promptCallback.name == ZERO)
      return;
   CS text = ml_get(lnum);
   CS prompt = prompt_text();
   if (STRLEN(text) >= STRLEN(prompt))
      text += STRLEN(prompt);
   argv[0].tag = VAR_STRING;
   argv[0].string = copyStr(text);
   argv[1].tag = VAR_UNKNOWN;

   Var returnVar;
   call_callback(&curBook->promptCallback, -1, &returnVar, 1, argv);
   clearVar(&argv[0]);
   clearVar(&returnVar);
}

//Return true when the interrupt callback was invoked.
pub int
invoke_prompt_interrupt(void) {
   Var returnVar;
   Var argv[1];
   int ret;

   if (curBook->promptInterrupt.name == NULL || *curBook->promptInterrupt.name == ZERO)
      return false;
   argv[0].tag = VAR_UNKNOWN;

   gotInterruptG = false; //don't skip executing commands
   ret = call_callback(&curBook->promptInterrupt, -1, &returnVar, 0, argv);
   clearVar(&returnVar);
   return ret == FAIL ? false : true;
}

//Return the effective prompt for the specified book.
private CS
buf_prompt_text(Book* book) {
   if (!book->promptText)
      return S"% ";
   return book->promptText;
}

//Return the effective prompt for the current book.
pub CS
prompt_text(void) {
   return buf_prompt_text(curBook);
}


//Return true if the cursor is in the editable position of the prompt line.
pub int
prompt_curpos_editable(void) {
   return curPor->cursor.lnum == curBook->mem.lineCount 
      && curPor->cursor.col >= (int)STRLEN(prompt_text());
}

//"prompt_setcallback({buffer}, {callback})" function
pub void
f_prompt_setcallback(Arr(Var) argvars, Var*) {
   Book* book = daGetBook(&argvars[0], false);
   if (!book)
      return;

   Callback callback = get_callback(&argvars[1]);
   if (!callback.name)
      return;

   evFreeCallback(&book->promptCallback);
   set_callback(&book->promptCallback, &callback);
   if (callback.needsFreeing)
      eeglFree(callback.name);
}

//"prompt_setinterrupt({buffer}, {callback})" function
pub void
f_prompt_setinterrupt(Arr(Var) argvars, Var*) {
   Book* book = daGetBook(&argvars[0], false);
   if (!book)
      return;

   Callback callback = get_callback(&argvars[1]);
   if (!callback.name)
      return;

   evFreeCallback(&book->promptInterrupt);
   set_callback(&book->promptInterrupt, &callback);
   if (callback.needsFreeing)
      eeglFree(callback.name);
}


//"prompt_getprompt({buffer})" function
pub void
f_prompt_getprompt(Arr(Var) argvars, Var* returnVar) {
   //return an empty string by default, e.g. it's not a prompt buffer
   returnVar->tag = VAR_STRING;
   returnVar->string = NULL;

   Book* book = daGetBookFromArg(&argvars[0]);
   if (!book)
      return;

   if (!bt_prompt(book))
      return;

   returnVar->string = copyStr(buf_prompt_text(book));
}

//"prompt_setprompt({book}, {text})" function
pub void
f_prompt_setprompt(Arr(Var) argvars, Var*) {
   Book* book = daGetBook(&argvars[0], false);
   if (!book)
      return;

   CS text = tv_get_string(&argvars[1]);
   eeglFree(book->promptText);
   book->promptText = copyStr(text);
}

//Get the job from the argument. Returns NULL if the job is invalid.
private Job *
get_job_arg(Var* tv) {
   if (tv->tag != VAR_JOB) {
      showErrFmtMsg(_(e_invalid_argument_str), tv_get_string(tv));
      return NULL;
   }
   Job* job = tv->job;
   if (!job)
      emsg(_(e_not_valid_job));
      
   return job;
}

pub void
f_job_getchannel(Arr(Var) argvars, OUT Var* returnVar) {
   Job* job = get_job_arg(&argvars[0]);
   if (!job)
      return;

   returnVar->tag = VAR_CHANNEL;
   returnVar->channel = job->channel;
   if (job->channel != NULL)
      ++job->channel->refCount;
}

private void
job_info(Job* job, Bag* bag) {
   bagAddString(bag, S"status", job_status(job));

   DictItem* item = dictitem_alloc(tConst("channel"));
   item->c.tag = VAR_CHANNEL;
   item->c.channel = job->channel;
   if (job->channel)
      ++job->channel->refCount;
   if (bagAdd(bag, item) == FAIL)
      dictitem_free(item);

   Long nr = job->pid;
   bagAddNumber(bag, S"process", nr);
   bagAddString(bag, S"tty_in", job->ttyIn);
   bagAddString(bag, S"tty_out", job->ttyOut);

   bagAddNumber(bag, S"exitval", job->exitVal);
   bagAddString(bag, S"exit_cb", job->exitCb.name);
   bagAddString(bag, S"stoponexit", job->jv_stoponexit);
   bagAddString(bag, S"termsig", job->jv_termsig);

   List* l = list_alloc();

   bagAddList(bag, S"cmd", l);
   if (job->argv) {
      for (int i = 0; job->argv[i]; i++)
         list_append_string(l, (CS)job->argv[i], -1);
   } 
}

private void
job_info_all(List* l) {
   Var tv;

   Job* job;
   FOR_ALL_JOBS(job) {
      tv.tag = VAR_JOB;
      tv.job = job;

      if (list_append_tv(l, &tv) != OK)
         return;
   }
}

pub void
f_job_info(Var* argvars, Var* returnVar) {
   if (argvars[0].tag != VAR_UNKNOWN) {
      Job* job = get_job_arg(&argvars[0]);
      if (job) {
         allocReturnDict(returnVar);
         job_info(job, returnVar->bag);
      } 
   } else {
      allocReturnList(returnVar);
      job_info_all(returnVar->list);
   } 
}

pub void
f_job_setoptions(Arr(Var) argvars, Var*) {
   Job* job = get_job_arg(&argvars[0]);
   if (!job)
      return;
      
   JobOptions   opt;
   CLEAR_POINTER(&opt);
   if (get_job_options(&argvars[1], OUT &opt, JO_STOPONEXIT + JO_EXIT_CB, 0) == OK)
      job_set_options(job, &opt);
   free_job_options(&opt);
}

pub void
f_startJob(Arr(Var) argvars, OUT Var* returnVar) {
   returnVar->tag = VAR_JOB;
   returnVar->job = startJob(argvars, NULL, NULL, NULL);
}

pub void
f_job_status(Arr(Var) argvars, Var* returnVar) {
   if (argvars[0].tag == VAR_JOB && argvars[0].job == NULL) {
      //A job that never started returns "fail".
      returnVar->tag = VAR_STRING;
      returnVar->string = copyStr(S"fail");
   } else {
      Job* job = get_job_arg(&argvars[0]);
      if (job) {
          returnVar->tag = VAR_STRING;
          returnVar->string = copyStr(job_status(job));
      }
   }
}

pub void
f_job_stop(Arr(Var) argvars, Var* returnVar) {
   Job* job = get_job_arg(&argvars[0]);
   if (job)
      returnVar->number = job_stop(job, argvars, NULL);
}

//Get a string with information about the job in "varp" into "builder".
//"builder" must be at least NUMBUFLEN long.
pub void
job_to_string_buf(OUT CS builder, Var* varp) {
   Job *job = varp->job;
   if (!job) {
      eeSnprintf(builder, NUMBUFLEN, "no process");
      return;
   }
   CS status = (CS)(job->status == JOB_FAILED 
      ? "fail"
      : (job->status >= JOB_ENDED ? "dead" : "run")
   );
   eeSnprintf(builder, NUMBUFLEN, "process %ld %s", (long)job->pid, status);
}

//}}}
//{{{command-line arguments

//Construct an array of strings that spell out `bash -c "bla bla"`
pub Multistring
chBuildArgv(Text cmd) {
   if (cmd.len == 0)
      return (Multistring){};
      
   Multistring shellArgs;
   appendToMulti(tConst("bash"), OUT &shellArgs);
   appendToMulti(tConst("-c"), OUT &shellArgs);
   appendToMulti(cmd, OUT &shellArgs);
   appendNullToMulti(OUT &shellArgs);
   return shellArgs;
}

//}}}
//{{{logging

//Implements logging.  Originally intended for the channel feature, which is
//why the "ch_" prefix is used.  Also useful for any kind of low-level and async debugging.

//Log file opened with ch_logfile().
private FILE* log_fd = NULL;
private CS log_name = NULL;
private ProfTime log_start;

pub void
ch_logfile(CS fname, CS opt) {
   FILE* file = NULL;
   CS mode = S"a";

   if (log_fd) {
      if (*fname != ZERO)
         lo("closing this logfile, opening %s", fname);
      else
         lo("closing logfile %s", log_name);
      fclose(log_fd);
   }

   //The "a" flag overrules the "w" flag.
   if (firstOccurrence(opt, 'a') == NULL && firstOccurrence(opt, 'w') != NULL)
      mode = S"w";
   ch_log_output = firstOccurrence(opt, 'o') != NULL ? LOG_ALWAYS : false;

   if (*fname != ZERO) {
      file = FOPEN(fname, mode);
      if (file == NULL) {
         showErrFmtMsg(_(e_cant_open_file_str), fname);
         return;
      }
      eeglFree(log_name);
      log_name = copyStr(fname);
   }
   log_fd = file;

   if (log_fd) {
      fprintf(log_fd, "==== start log session %s ====\n", get_ctime(time(NULL), false));
      //flush now, if fork/exec follows it could be written twice
      fflush(log_fd);
      profile_start(&log_start);
   }
}

pub int
ch_log_active(void) {
   return log_fd != NULL;
}

private void
logLead(CS what, Channel* ch, ChannelFdKind part) {
   if (!log_fd)
      return;

   ProfTime log_now;
   profile_start(&log_now);
   profile_sub(&log_now, &log_start);
   fprintf(log_fd, "%s ", profile_msg(&log_now));
   if (ch != NULL) {
      if (part < PART_COUNT)
         fprintf(log_fd, "%son %d(%s): ", what, ch->id, chanFdNames[part]);
      else
         fprintf(log_fd, "%son %d: ", what, ch->id);
   } else
      fprintf(log_fd, "%s: ", what);
}


pub void
ch_log(Channel* ch, char const* fmt, ...) {
   if (!log_fd)
      return;

   va_list ap;

   logLead(S"", ch, PART_COUNT);
   va_start(ap, fmt);
   vfprintf(log_fd, fmt, ap);
   va_end(ap);
   fputc('\n', log_fd);
   fflush(log_fd);
   did_repeated_msg = 0;
}

pub void
lo(char const* fmt, ...) {
   if (!log_fd)
      return;

   va_list ap;

   logLead(S"", null, PART_COUNT);
   va_start(ap, fmt);
   vfprintf(log_fd, fmt, ap);
   va_end(ap);
   fputc('\n', log_fd);
   fflush(log_fd);
   did_repeated_msg = 0;
}

pub void
ch_error(Channel* ch, char const* fmt, ...) {
   if (log_fd == NULL)
      return;

   va_list ap;

   logLead(S"ERR ", ch, PART_COUNT);
   va_start(ap, fmt);
   vfprintf(log_fd, fmt, ap);
   va_end(ap);
   fputc('\n', log_fd);
   fflush(log_fd);
   did_repeated_msg = 0;
}

//Log a message "builder[len]" for channel "ch" part "part".
//Only to be called when ch_log_active() returns true.
private void
ch_log_literal(CS lead, Channel* ch, ChannelFdKind part, OUT Text builder) {
   logLead(lead, ch, part);
   fprintf(log_fd, "'");
   (void)fwrite(builder.c, builder.len, 1, log_fd);
   fprintf(log_fd, "'\n");
   fflush(log_fd);
}

pub void
f_ch_log(Arr(Var) argvars, Var*) {
   Channel   *channel = NULL;
   CS msg = tv_get_string(&argvars[0]);
   if (argvars[1].tag != VAR_UNKNOWN)
      channel = get_channel_arg(&argvars[1], false, false, 0);

   //Prepend "ch_log()" to make it easier to find these entries in the logfile.
   ch_log(channel, "ch_log(): %s", msg);
}

pub void
f_ch_logfile(Arr(Var) argvars, Var*) {
   Byte builder[NUMBUFLEN];
   CS fname = tv_get_string(&argvars[0]);
   CS opt = (argvars[1].tag == VAR_STRING) ? tv_get_string_buf(&argvars[1], builder) : S"";
   ch_logfile(fname, opt);
}

//}}}
//{{{persisting sessions
//{{{users

//All user names (for ~user completion as done by shell).
private ArrayList   ga_users;


//Add a user name to the list of users in ga_users. Do nothing if user name is NULL or empty.
private void
add_user(Byte *user, int need_copy) {
   CS user_copy = (user && need_copy) ? copyStr(user) : user;

   if (!user_copy || *user_copy == ZERO || ga_grow(&ga_users, 1) == FAIL) {
      if (need_copy)
         eeglFree(user_copy);
      return;
   }
   ((Byte **)(ga_users.c))[ga_users.len++] = user_copy;
}

//Find all user names for user completion. Done only once and then cached.
private void
init_users(void) {
   static int   lazy_init_done = false;

   if (lazy_init_done)
      return;

   lazy_init_done = true;
   ga_init2(&ga_users, sizeof(CS), 20);

   {
   struct passwd*   pw;

   setpwent();
   while ((pw = getpwent()) != NULL)
      add_user((CS)pw->pw_name, true);
   endpwent();
   }
   CS user_env = mch_getenv(S"USER");

   //The $USER environment variable may be a valid remote user name (NIS, LDAP) not already listed 
   //by getpwent(), as getpwent() only lists local user names.  If $USER is not already listed, 
   //check whether it is a valid remote user name using getpwnam() and if it is, add it to
   //the list of user names.

   if (user_env && *user_env != ZERO) {
      Unt   i;
      for (i = 0; i < (Unt)ga_users.len; i++) {
         Byte   *local_user = ((Byte **)ga_users.c)[i];

         if (STRCMP(local_user, user_env) == 0)
             break;
      }

      if (i == (Unt)ga_users.len) {
         struct passwd *pw = getpwnam((char *)user_env);
         if (pw)
            add_user((CS)pw->pw_name, true);
      }
   }
}

//Function given to expandGeneric() to obtain user names.
pub CS
get_users(Expand*, int idx) {
   init_users();
   if (idx < ga_users.len)
      return ((Byte **)ga_users.c)[idx];
   return NULL;
}

//Check whether name matches a user name. Return:
//0 if name does not match any user name.
//1 if name partially matches the beginning of a user name.
//2 is name fully matches a user name.
pub int
match_user(CS name) {
   int i;
   int n = (int)STRLEN(name);
   int result = 0;

   init_users();
   for (i = 0; i < ga_users.len; i++) {
      if (STRCMP(((Byte **)ga_users.c)[i], name) == 0)
         return 2; //full match
      if (STRNCMP(((Byte **)ga_users.c)[i], name, n) == 0)
         result = 1; //partial match
   }
   return result;
}



#if defined(EXITFREE)

pub void
free_homedir(void) {
   eeglFree(homedir);
}

pub void
free_users(void) {
   ga_clear_strings(&ga_users);
}

#endif

//}}}
//{{{sessions

private Boole did_lcd;   //whether ":lcd" was produced for a session

//Write a file name to the session file.
//Takes care of the "slash" option in 'sessionoptions' and escapes special characters.
//Return FAIL if writing fails.
private int
ses_put_fname(FILE *fd, CS name) {
   CS sname = home_replace_save(NULL, name);

   int retval = OK;
   //escape special characters
   CS p = copyStr_fnameescape(sname, VSE_NONE);
   eeglFree(sname);

   //write the result
   if (FPUTS(p, fd) < 0)
      retval = FAIL;

   eeglFree(p);
   return retval;
}

//Write a book name to the session file.
//Also end the line, if "add_eol" is true. Return FAIL if writing fails.
private int
ses_fname(FILE* fd, Book* book, int add_eol) {
   CS name = book->shortFileName;
   if (ses_put_fname(fd, name) == FAIL || (add_eol && put_eol(fd) == FAIL))
      return FAIL;
   return OK;
}

//Write an argument list to the session file. Return FAIL if writing fails.
private int
ses_arglist(FILE* fd, CS cmd, ArrayList* gap, int fullname) {   //true: use full path name
   if (FPUTS(cmd, fd) < 0 || put_eol(fd) == FAIL)
      return FAIL;
   if (put_line(fd, S"%argdel") == FAIL)
      return FAIL;
   for (int i = 0; i < gap->len; ++i) {
      //NULL file names are skipped (only happens when out of memory).
      CS s = alist_name(&((ArgFileEntry *)gap->c)[i]);
      if (!s) {
         continue;
      }
      
      Byte buf[MAXPATHL];
      if (fullname) {
         (void)eeFullFileName(s, buf, MAXPATHL, false);
         s = buf;
      }
      if (fputs("$argadd ", fd) < 0 
            || ses_put_fname(fd, s) == FAIL 
            || put_eol(fd) == FAIL
      ){
         return FAIL;
      }
   }
   return OK;
}

//Return non-zero if portal "po" is to be stored in the Session.
private Boole
portNeedsToBeSaved(Portal* po) {
   if (bt_terminal(po->book)) {
      return !term_is_finished(po->book) && term_should_restore(po->book);
   } 
   if (!po->book->currFileName || bt_nofilename(po->book))
      //When 'buftype' is "nofile", can't restore the portal contents.
      return false;
   return true;
}

//Return true if frame "fr" has a window somewhere that we want to save in the Session
private Boole
ses_do_frame(Frame* fr) {
   if (fr->layout == FR_LEAF)
      return portNeedsToBeSaved(fr->port);
      
   Frame   *frc;
   FOR_ALL_FRAMES(frc, fr->child) {
      if (ses_do_frame(frc))
          return true;
   } 
   return false;
}

//Skip frames that don't contain portals we want to save in the Session. Return NULL when none
private Frame*
ses_skipframe(Frame* fr) {
   Frame* frc;
   FOR_ALL_FRAMES(frc, fr) {
      if (ses_do_frame(frc))
          return frc;
   } 
   return null;
}

//Write commands to "fd" to recursively create portals for frame "fr", horizontally and vertically
//split. After the commands the last portal in the frame is the current portal. Return FAIL when 
//writing the commands to "fd" fails.
private int
recreatePortals(FILE* fd, Frame* fr) {
   if (fr->layout == FR_LEAF)
      return OK;

   //Find first frame that's not skipped and then create a window for
   //each following one (first frame is already there).
   Frame* frc = ses_skipframe(fr->child);
   int count = 0;
   if (frc) {
      while ((frc = ses_skipframe(frc->next)) != NULL) {
         //Make window as big as possible so that we have lots of room to split.
         if (put_line(fd, S"wincmd _ | wincmd |") == FAIL
               || put_line(fd, fr->layout == FR_COL ? S"split" : S"vsplit") == FAIL
         )
            return FAIL;
         ++count;
      }
   } 

   //Go back to the first window.
   if (count > 0 && (fprintf(fd, fr->layout == FR_COL
          ? "%dwincmd k" : "%dwincmd h", count) < 0
         || put_eol(fd) == FAIL))
      return FAIL;

   //Recursively create frames/windows in each window of this column or row.
   frc = ses_skipframe(fr->child);
   while (frc) {
      recreatePortals(fd, frc);
      frc = ses_skipframe(frc->next);
      //Go to next window.
      if (frc && put_line(fd, S"wincmd w") == FAIL)
          return FAIL;
   }

   return OK;
}

private int
portalSizes(FILE* fd, int restore_size, Portal* tab_firstPor) {
   int      n = 0;
   Portal* wp;

   if (restore_size) {
      for (wp = tab_firstPor; wp != NULL; wp = wp->next) {
         if (!portNeedsToBeSaved(wp))
            continue;
         ++n;

         //restore height when not full height
         if (wp->height + STATUS_HEIGHT < topframeG->width
                && (fprintf(fd,
                 "exe '%dresize ' . ((&lines * %ld + %ld) / %ld)",
                   n, (long)wp->height, visibleRowsG / 2, visibleRowsG) < 0
                          || put_eol(fd) == FAIL))
            return FAIL;

          //restore width when not full width
          if (wp->width < visibleColsG && (fprintf(fd,
            "exe 'vert %dresize ' . ((&columns * %ld + %ld) / %ld)",
                n, (long)wp->width, visibleColsG / 2, visibleColsG) < 0
                       || put_eol(fd) == FAIL))
         return FAIL;
      }
   } else {
      //Just equalise window sizes
      if (put_line(fd, S"wincmd =") == FAIL)
         return FAIL;
   }
   return OK;
}

private int
put_view_curpos(FILE *fd, Portal *wp, char *spaces) {
   int r;
   if (wp->cursWant == MAXCOL)
      r = fprintf(fd, "%snormal! $", spaces);
   else
      r = fprintf(fd, "%snormal! 0%d|", spaces, wp->virtCol + 1);
   return r < 0 || put_eol(fd) == FAIL ? false : OK;
}

//Write commands to "fd" to restore the view of a window.
//Caller must make sure 'scrolloff' is zero.
private int
put_view(
   FILE* fd,
   Portal* wp,
   Tab* tp,
   int add_edit,        //add ":edit" command to view
   int current_arg_idx,     //current argument index of the portal, use -1 if unknown
   EeSet* terminal_bufs //already encountered terminal books, can be NULL
){
   Portal   *save_curPor;
   int      f;
   int      did_next = false;

   //Always restore cursor position for ":mksession".
   Boole do_cursor = true;

   //Local argument list.
   if (wp->argList == &argListG) {
      if (put_line(fd, S"argglobal") == FAIL)
         return FAIL;
   } else {
      if (ses_arglist(fd, S"arglocal", &wp->argList->al_ga,
            tp->localdir != NULL
            || wp->localDir != NULL) == FAIL)
         return FAIL;
   }

   //Only when part of a session: restore the argument index.  Some
   //arguments may have been deleted, check if the index is valid.
   if (wp->argListInd != current_arg_idx && wp->argListInd < WARGCOUNT(wp)) {
      if (fprintf(fd, "%ldargu", (long)wp->argListInd + 1) < 0 || put_eol(fd) == FAIL)
         return FAIL;
      did_next = true;
   }

   //Edit the file.  Skip this when ":next" already did it.
   if (add_edit && (!did_next || wp->isNotValid)) {
      if (bookIsHelp(wp->book)) {
         CS curtag = S"";

         //A help book needs some options to be set.
         //First, create a new empty book with "buftype=help".
         //Then ":help" will re-use both the book and the portal and set the options, even when
         //"options" is not in 'sessionoptions'.
         if (0 < wp->tagStackInd && wp->tagStackInd <= wp->tagStackLen)
            curtag = wp->tagStack[wp->tagStackInd - 1].tagname;

         if (put_line(fd, S"enew | setl bt=help") == FAIL
                || fprintf(fd, "help %s", curtag) < 0
                || put_eol(fd) == FAIL)
            return FAIL;
      } ei (bt_terminal(wp->book)) {
         if (term_write_session(fd, wp, terminal_bufs) == FAIL)
            return FAIL;
      }
      //Load the file.
      ei (wp->book->fullFileName != NULL && !bt_nofilename(wp->book)) {
          //Editing a file in this book: use ":edit file".
          //This may have side effects! (e.g., compressed or network file).
          //
          //Note, if a book for that file already exists, use :badd to
          //edit that book, to not lose folding information (:edit resets
          //folds in other books)
          if (fputs("if bufexists(fnamemodify(\"", fd) < 0
             || ses_fname(fd, wp->book, false) == FAIL
             || fputs("\", \":p\")) | buffer ", fd) < 0
             || ses_fname(fd, wp->book, false) == FAIL
             || fputs(" | else | edit ", fd) < 0
             || ses_fname(fd, wp->book, false) == FAIL
             || fputs(" | endif", fd) < 0
             || put_eol(fd) == FAIL)
         return FAIL;
      } else {
         //No file in this book, just make it empty.
         if (put_line(fd, S"enew") == FAIL)
            return FAIL;
         if (wp->book->fullFileName != NULL) {
            //The book does have a name, but it's not a file name.
            if (fputs("file ", fd) < 0 || ses_fname(fd, wp->book, true) == FAIL)
               return FAIL;
         }
         do_cursor = false;
      }
   }

   if (wp->altFnum) {
      Book *alt = bookFindFileByBookNr(wp->altFnum);

      //Set the alternate file if the book is listed.
      if (     alt
            && alt->currFileName != NULL
            && *alt->currFileName != ZERO
            && alt->o.bookListed
            && (fputs("balt ", fd) < 0 || ses_fname(fd, alt, true) == FAIL))
         return FAIL;
   }

   //Local mappings and abbreviations.
   if (makemap(fd, wp->book) == FAIL)
      return FAIL;

   //Local options. Need to go to the portal temporarily.
   //Store only local values when ":mksession" is
   //used and 'sessionoptions' doesn't include "options".
   //Some folding options are always stored when "folds" is included,
   //otherwise the folds would not be restored correctly.
   save_curPor = curPor;
   curPor = wp;
   curBook = curPor->book;
   f = writeOptionsAsSet(fd);
   curPor = save_curPor;
   curBook = curPor->book;
   if (f == FAIL)
      return FAIL;

   //Set the cursor after creating folds, since that moves the cursor.
   if (do_cursor) {

      //Restore the cursor line in the file and relatively in the
      //portal.  Don't use "G", it changes the jumplist.
      if (wp->height <= 0) {
         if (fprintf(fd, "let s:l = %ld", (long)wp->cursor.lnum) < 0)
            return FAIL;
      } ei (fprintf(fd,
             "let s:l = %ld - ((%ld * winheight(0) + %ld) / %ld)",
             (long)wp->cursor.lnum,
             (long)(wp->cursor.lnum - wp->topLine),
             (long)wp->height / 2, (long)wp->height) < 0)
          return FAIL;

      if (put_eol(fd) == FAIL
            || put_line(fd, S"if s:l < 1 | let s:l = 1 | endif") == FAIL
            || put_line(fd, S"keepjumps exe s:l") == FAIL
            || put_line(fd, S"normal! zt") == FAIL
            || fprintf(fd, "keepjumps %ld", (long)wp->cursor.lnum) < 0
            || put_eol(fd) == FAIL)
          return FAIL;
      //Restore the cursor column and left offset when not wrapping.
      if (wp->cursor.col == 0) {
         if (put_line(fd, S"normal! 0") == FAIL)
            return FAIL;
      } else {
         if (!wp->o.wrap && wp->leftCol > 0 && wp->width > 0) {
            if (fprintf(fd,
                 "let s:c = %ld - ((%ld * winwidth(0) + %ld) / %ld)",
                   (long)wp->virtCol + 1,
                   (long)(wp->virtCol - wp->leftCol),
                   (long)wp->width / 2, (long)wp->width) < 0
               || put_eol(fd) == FAIL
               || put_line(fd, S"if s:c > 0") == FAIL
               || fprintf(fd,
                   "  exe 'normal! ' . s:c . '|zs' . %ld . '|'",
                   (long)wp->virtCol + 1) < 0
               || put_eol(fd) == FAIL
               || put_line(fd, S"else") == FAIL
               || put_view_curpos(fd, wp, "  ") == FAIL
               || put_line(fd, S"endif") == FAIL)
                return FAIL;
         } ei (put_view_curpos(fd, wp, "") == FAIL)
            return FAIL;
      }
   }

   //Local directory, if the current flag is not view options or the "curdir"
   //option is included.
   if (wp->localDir) {
      if (fputs("lcd ", fd) < 0
            || ses_put_fname(fd, wp->localDir) == FAIL
            || put_eol(fd) == FAIL)
         return FAIL;
      did_lcd = true;
   }

   return OK;
}

private VarFlavor
getVarFlavor(CS varname) {
   CS p = varname;

   if (ASCII_ISUPPER(*p)) {
      while (*(++p)) {
         if (ASCII_ISLOWER(*p))
            return VAR_FLAVOR_SESSION;
      } 
      return VAR_FLAVOR_EEGLINFO;
   } else
      return VAR_FLAVOR_DEFAULT;
}

private int
store_session_globals(FILE *fd) {
   EeSet   *gvht = get_globvar_ht();
   EeSetItem   *hi;
   DictItem   *this_var;
   int      todo;
   Byte   *p, *t;

   todo = (int)gvht->count;
   FOR_ALL_HASHTAB_ITEMS(gvht, hi, todo) {
      if (!HASHITEM_EMPTY(hi)) {
         --todo;
         this_var = HI2DI(hi);
         if ((this_var->c.tag == VAR_NUMBER || this_var->c.tag == VAR_STRING)
                && getVarFlavor(this_var->key) == VAR_FLAVOR_SESSION
         ){
            //Escape special characters with a backslash. Turn a LF and CR into \n and \r.
            p = copyStr_escaped(tv_get_string(&this_var->c), (CS)"\\\"\n\r");
            for (t = p; *t != ZERO; ++t)
               if (*t == '\n')
                  *t = 'n';
               ei (*t == '\r')
                  *t = 'r';
            if ((fprintf(fd, "let %s = %c%s%c",
                  this_var->key,
                  (this_var->c.tag == VAR_STRING) ? '"' : ' ',
                  p,
                  (this_var->c.tag == VAR_STRING) ? '"' : ' ') < 0)
                  || put_eol(fd) == FAIL
            ){
               eeglFree(p);
               return FAIL;
            }
            eeglFree(p);
         } ei (this_var->c.tag == VAR_FLOAT && getVarFlavor(this_var->key) == VAR_FLAVOR_SESSION) {
            double f = this_var->c.floatt;
            int sign = ' ';

            if (f < 0) {
               f = -f;
               sign = '-';
            }
            if ((fprintf(fd, "let %s = %c%f", this_var->key, sign, f) < 0) || put_eol(fd) == FAIL)
               return FAIL;
         }
      }
   }
   return OK;
}

//Write openfile commands for the current books to an .exrc file.
//Return FAIL on error, OK otherwise.
private int
makeopens(FILE   *fd, Byte   *currDir) {  //Current directory name
   Book   *book;
   int      nr;
   int      restore_size = true;
   int      restore_height_width = false;
   Portal   *wp;
   Byte   *sname;
   Portal   *edited_win = NULL;
   int      restore_stal = false;
   Portal   *tab_firstPor;
   Frame   *tab_topframe;
   int      cur_arg_idx = 0;
   int      next_arg_idx = 0;
   int      ret = FAIL;
   Tab   *tp;
   EeSet   terminal_bufs;

   hash_init(&terminal_bufs);


   //Begin by setting the this_session variable, and then other
   //sessionable variables.
   if (put_line(fd, S"let v:this_session=expand(\"<sfile>:p\")") == FAIL
        || store_session_globals(fd) == FAIL)
      goto fail;

   //Close all portals and tabs but one.
   if (put_line(fd, S"silent only") == FAIL)
      goto fail;
   if (put_line(fd, S"silent tabonly") == FAIL)
      goto fail;

   //Now a :cd command to the current directory
   sname = home_replace_save(NULL, globaldir != NULL ? globaldir : currDir);
   if (  fputs("cd ", fd) < 0
      || ses_put_fname(fd, sname) == FAIL
      || put_eol(fd) == FAIL
   ){
      eeglFree(sname);
      goto fail;
   }
   eeglFree(sname);

   //If there is an empty, unnamed book we will wipe it out later.
   //Remember the book number.
   if (put_line(fd, S"if expand('%') == '' && !&modified && line('$') <= 1 && getline(1) == ''") 
         == FAIL
   )
      goto fail;
   if (put_line(fd, S"  let s:wipebuf = bufnr('%')") == FAIL)
      goto fail;
   if (put_line(fd, S"endif") == FAIL)
      goto fail;

   //Set 'shortmess' for the following.
   if (put_line(fd, S"set shortmess+=aoO") == FAIL)
      goto fail;

   //Now save the current files, current book first.
   //Put all books into the book list.
   //Do it very early to preserve book order after loading session (which
   //can be disrupted by prior `edit` or `tabedit` calls).
   FOR_ALL_BOOKS(book) {
      if (fprintf(fd, "badd +%ld ", book->portInfos == NULL ? 1L
                  : book->portInfos->wi_fpos.lnum) < 0
             || ses_fname(fd, book, true) == FAIL)
         goto fail;
   }

   //the global argument list
   if (ses_arglist(fd, S"argglobal", &argListG.al_ga, false) 
         == FAIL
   )
      goto fail;

   //Note: after the restore we still check it worked!
   if (fprintf(fd, "set lines=%ld columns=%ld" , visibleRowsG, visibleColsG) < 0 
         || put_eol(fd) == FAIL)
      goto fail;

   //"tabs" is in 'sessionoptions': Similar to recreatePortals() below, populate the tabs first 
   //so later local options won't be copied to the new tabs.
   FOR_ALL_TABS(tp) {
      //Use `bufhidden=wipe` to remove empty "placeholder" books once they are not needed. 
      //This prevents creating extra books (see cause of patch 8.1.0829)
      if (tp->next != NULL && put_line(fd, S"tabnew +setlocal\\ bufhidden=wipe") == FAIL)
         goto fail;
   } 
   if (firstTabG->next != NULL && put_line(fd, S"tabrewind") == FAIL)
       goto fail;

   //Assume "tabs" is in 'sessionoptions'. If not then we only do "curtab" and bail out of the loop
   FOR_ALL_TABS(tp) {
      int   need_tabnext = false;
      int   cnr = 1;

      //May repeat putting Portals for each tab, when "tabs" is in 'sessionoptions'.
      //Don't use goto_tabpage(), it may change directory and trigger autocommands.
      if (tp == curtab) {
         tab_firstPor = firstPor;
         tab_topframe = topframeG;
      } else {
         tab_firstPor = tp->firstPor;
         tab_topframe = tp->topframe;
      }
      if (tp != firstTabG)
         need_tabnext = true;

      //Before creating the window layout, try loading one file.  If this
      //is aborted we don't end up with a number of useless windows.
      //This may have side effects! (e.g., compressed or network file).
      for (wp = tab_firstPor; wp != NULL; wp = wp->next) {
          if (portNeedsToBeSaved(wp)
             && wp->book->fullFileName != NULL
             && !bookIsHelp(wp->book)
             && !bt_nofilename(wp->book)
         ){
            if (need_tabnext && put_line(fd, S"tabnext") == FAIL)
               goto fail;
            need_tabnext = false;

            if (fputs("edit ", fd) < 0 || ses_fname(fd, wp->book, true) == FAIL)
               goto fail;
            if (!wp->isNotValid)
               edited_win = wp;
            break;
         }
      }

      //If no file got edited create an empty tab
      if (need_tabnext && put_line(fd, S"tabnext") == FAIL)
         goto fail;

      if (tab_topframe->layout != FR_LEAF) {
          //Save current window layout.
          if (put_line(fd, S"let s:save_splitbelow = &splitbelow") == FAIL
                || put_line(fd, S"let s:save_splitright = &splitright") == FAIL)
            goto fail;
         if (put_line(fd, S"set splitbelow splitright") == FAIL)
            goto fail;
         if (recreatePortals(fd, tab_topframe) == FAIL)
            goto fail;
         if (put_line(fd, S"let &splitbelow = s:save_splitbelow") == FAIL
                || put_line(fd, S"let &splitright = s:save_splitright") == FAIL)
            goto fail;
      }

      //Check if window sizes can be restored (no windows omitted).
      //Remember the window number of the current window after restoring.
      nr = 0;
      for (wp = tab_firstPor; wp != NULL; wp = wp->next) {
         if (portNeedsToBeSaved(wp))
            ++nr;
         else
            restore_size = false;
         if (curPor == wp)
            cnr = nr;
      }

      if (tab_firstPor->next) {
         //Go to the first portal.
         if (put_line(fd, S"wincmd t") == FAIL)
            goto fail;

          //If more than one window, see if sizes can be restored.
          //First set 'winheight' and 'winwidth' to 1 to avoid the windows
          //being resized when moving between windows.
          //Do this before restoring the view, so that the topline and the
          //cursor can be set.  This is done again below.
          //winminheight and winminwidth need to be set to avoid an error if
          //the user has set winheight or winwidth.
          if (put_line(fd, S"let s:save_winminheight = &winminheight") == FAIL
             || put_line(fd, S"let s:save_winminwidth = &winminwidth")
                                  == FAIL)
         goto fail;
          if (put_line(fd, S"set winminheight=0") == FAIL
                || put_line(fd, S"set winheight=1") == FAIL
                || put_line(fd, S"set winminwidth=0") == FAIL
                || put_line(fd, S"set winwidth=1") == FAIL)
            goto fail;
         restore_height_width = true;
      }
      if (nr > 1 && portalSizes(fd, restore_size, tab_firstPor) == FAIL)
         goto fail;

      //Restore the tab-local working directory if specified
      //Do this before the windows, so that the window-local directory can
      //override the tab-local directory.
      if (tp->localdir != NULL) {
         if (fputs("tcd ", fd) < 0
              || ses_put_fname(fd, tp->localdir) == FAIL
              || put_eol(fd) == FAIL
         )
            goto fail;
         did_lcd = true;
      }

      //Restore the view of the window (options, file, cursor, etc.).
      for (wp = tab_firstPor; wp != NULL; wp = wp->next) {
         if (!portNeedsToBeSaved(wp))
            continue;
          if (put_view(fd, wp, tp, wp != edited_win, 
                         cur_arg_idx,
                         &terminal_bufs
          ) == FAIL)
         goto fail;
          if (nr > 1 && put_line(fd, S"wincmd w") == FAIL)
         goto fail;
          next_arg_idx = wp->argListInd;
      }

      //The argument index in the first tab is zero, need to set it in each portal. For further 
      //tabs it's the portal where we do "tabedit".
      cur_arg_idx = next_arg_idx;

      //Restore cursor to the current window if it's not the first one.
      if (cnr > 1 && (fprintf(fd, "%dwincmd w", cnr) < 0 || put_eol(fd) == FAIL))
         goto fail;

      //Restore window sizes again after jumping around in windows, because
      //the current window has a minimum size while others may not.
      if (nr > 1 && portalSizes(fd, restore_size, tab_firstPor) == FAIL)
         goto fail;
   }

   if (fprintf(fd, "tabnext %d", indexOfTab(curtab)) < 0 || put_eol(fd) == FAIL)
      goto fail;
   if (restore_stal && put_line(fd, S"set stal=1") == FAIL)
      goto fail;

   //Wipe out an empty unnamed book we started in.
   if (put_line(fd, S"if exists('s:wipebuf') && len(win_findbuf(s:wipebuf)) == 0") == FAIL)
      goto fail;
   if (put_line(fd, S"  silent exe 'bwipe ' . s:wipebuf") == FAIL)
      goto fail;
   if (put_line(fd, S"endif") == FAIL)
      goto fail;
   if (put_line(fd, S"unlet! s:wipebuf") == FAIL)
      goto fail;

   //Re-apply 'winheight' and 'winwidth'.
   if (fprintf(fd, "set winheight=%ld winwidth=%ld", p_wh, p_wiw) < 0 || put_eol(fd) == FAIL)
      goto fail;

   if (restore_height_width //Restore 'winminheight' and 'winminwidth'.
       && (put_line(fd, S"let &winminheight = s:save_winminheight") == FAIL
         || put_line(fd, S"let &winminwidth = s:save_winminwidth") == FAIL)
   ) {
      goto fail;
   }

   //Lastly, execute the x.vim file if it exists.
   if (put_line(fd, S"let s:sx = expand(\"<sfile>:p:r\").\"x.vim\"") == FAIL
          || put_line(fd, S"if filereadable(s:sx)") == FAIL
          || put_line(fd, S"  exe \"source \" . fnameescape(s:sx)") == FAIL
          || put_line(fd, S"endif") == FAIL)
      goto fail;

   ret = OK;
fail:
   hash_clear_all(&terminal_bufs, 0);
   return ret;
}

//":mkvimrc",  and ":mksession".
pub void
c_mkrc(Invocation* invo) {
   int failed = false;
   int using_vdir = false;   //using 'viewdir'?
   CS viewFile = NULL;

   Boole sessionFile = (invo->id == C_mksession);

   //Use the short file name until ":lcd" is used.  We also don't use the
   //short file name when 'acd' is set, that is checked later.
   did_lcd = false;
   
   CS fname;
   if (*invo->arg != ZERO)
      fname = invo->arg;
   ei (invo->id == C_mkvimrc)
      fname = S"~/.config/eegl/init.vim";
   ei (invo->id == C_mksession)
      fname = (CS)SESSION_FILE;

   FILE* fd = doOpenCommandsFile(fname, invo->forceit, (CS)WRITEBIN);
   if (!fd) {
      goto theEnd;
   }

   //Write the version command for :mkvimrc
   if (invo->id == C_mkvimrc)
       (void)put_line(fd, S"version 6.0");

   if (invo->id == C_mksession) {
       if (put_line(fd, S"let SessionLoad = 1") == FAIL)
      failed = true;
   }

   (void)put_line(fd, S"if &cp | set nocp | endif");

   if (!sessionFile || invo->id == C_mksession) {
      failed |= (makemap(fd, NULL) == FAIL || writeOptionsAsSet(fd) == FAIL);
   }

   if (!failed && sessionFile) {
      if (put_line(fd, S"let s:so_save = &g:so | let s:siso_save = &g:siso | "
         "setg so=0 siso=0 | setl so=-1 siso=-1") == FAIL)
      failed = true;
      if (invo->id == C_mksession) {
         Byte currDir[MAXPATHL];    //current directory
         //Change to session file's dir.
         if (mch_dirname(currDir, MAXPATHL) == FAIL || mch_chdir(currDir) != 0)
            *currDir = ZERO;
         if (*currDir != ZERO) {
            if (eeChdirfile(fname, NULL) == OK)
               shorten_fnames(true);
         } ei (*currDir != ZERO && globaldir) {
            if (mch_chdir(globaldir) == 0)
               shorten_fnames(true);
         }

         failed |= (makeopens(fd, currDir) == FAIL);

         //restore original dir
         if (*currDir != ZERO && globaldir) {
            if (mch_chdir(currDir) != 0)
               emsg(_(e_cannot_go_back_to_previous_directory));
            shorten_fnames(true);
         }
      } else {
         failed |= (put_view(fd, curPor, curtab, !using_vdir, -1, NULL) == FAIL);
      }
      if (put_line(fd, S"let &g:so = s:so_save | let &g:siso = s:siso_save") == FAIL)
         failed = true;
      if (!hiliteSearchG && put_line(fd, S"hlsearch") == FAIL)
         failed = true;
      if (put_line(fd, S"doautoall SessionLoadPost") == FAIL)
         failed = true;
      if (invo->id == C_mksession) {
         if (put_line(fd, S"unlet SessionLoad") == FAIL)
            failed = true;
      }
   }
   if (put_line(fd, S"\" vim: set ft=vim :") == FAIL)
      failed = true;

   failed |= fclose(fd);

   if (failed)
      emsg(_(e_error_while_writing));
theEnd:

   eeglFree(viewFile);

   applyAutocomms(EVENT_SESSIONWRITEPOST, NULL, NULL, false, curBook);
}

//Write end-of-line character(s) for ":mkexrc", ":mkvimrc" and ":mksession".
//Return FAIL for a write error.
pub int
put_eol(FILE *fd) {
   if (putc('\n', fd) < 0)
      return FAIL;
   return OK;
}

//Write a line to "fd". Return FAIL for a write error.
pub int
put_line(FILE *fd, CS s) {
   if (FPUTS(s, fd) < 0 || put_eol(fd) == FAIL)
      return FAIL;
   return OK;
}

//}}}
//{{{eeglinfo files - serialization of state like marks and search history to files

#define EEGLINFO_VERSION                4
#define EEGLINFO_VERSION_WITH_HISTORY   2
#define EEGLINFO_VERSION_WITH_REGISTERS 3
#define EEGLINFO_VERSION_WITH_MARKS     4

//The type numbers are fixed for backwards compatibility.
#define BARTYPE_VERSION  1
#define BARTYPE_HISTORY  2
#define BARTYPE_REGISTER 3
#define BARTYPE_MARK     4

typedef enum {
   BVAL_NR,
   BVAL_STRING,
   BVAL_EMPTY
} BValKind;

typedef struct {
   BValKind   btag;
   long   bv_nr;
   Byte   *bv_string;
   Byte   *bv_tofree;   //free later when not NULL
   int      bv_len;      //length of bv_string
   int      bv_allocated;   //bv_string was allocated
} BVal;


private int  eeglinfo_errcnt;

//Find the parameter represented by the given character (eg ''', ':', '"', or
//'/') in the @eeglinfo option and return a pointer to the string after it.
//Return NULL if the parameter is not specified in the string.
private CS
find_eeglinfo_parameter(int type) {
   if (!p_eeglinfo)
      return null;
   for (CS p = p_eeglinfo; *p; ++p) {
      if (*p == type)
         return p + 1;
      if (*p == 'n')          //'n' is always the last one
         break;
      p = firstOccurrence(p, ',');       //skip until next ','
      if (!p)          //hit the end without finding parameter
         break;
   }
   return NULL;
}

//Find the parameter represented by the given character (eg ', :, ", or /), and return its 
//associated value in the 'eeglinfo' string. Only works for number parameters, not for 'r' or 'n'.
//If the parameter is not specified in the string or there is no following number, return -1.
pub int
get_eeglinfo_parameter(int type) {
   CS p = find_eeglinfo_parameter(type);
   if (p && EE_ISDIGIT(*p))
      return atoi((char *)p);
   return -1;
}

//Get the eeglinfo file name to use. If "file" is given and not empty, use it (has already been 
//expanded by cmdline functions).
//Otherwise use "-i file_name", value from 'eeglinfo' or the default, and expand environment 
//variables. Return an allocated string.
private CS
eeglinfo_filename(CS file) {
   if (!file || *file == ZERO) {
      if (p_eeglinfofile)
         file = p_eeglinfofile;
      ei ((file = find_eeglinfo_parameter('n')) == NULL || *file == ZERO) {
         file = (CS)EEGLINFO_FILE;
      }
      Unt len = doExpandEnv(OUT nameBuffTextG, file);
      file = nameBuffG;

      return copySubstr(file, len);
   }

   return copyStr(file);
}

//write string to eeglinfo file
//- replace CTRL-V with CTRL-V CTRL-V
//- replace '\n'   with CTRL-V 'n'
//- add a '\n' at the end
//
//For a long line:
//- write " CTRL-V <length> \n " in first line
//- write " < <string> \n "     in second line
private void
eeglinfo_writestring(FILE* fd, CS p) {
   int c;
   CS s;
   int len = 0;

   for (s = p; *s != ZERO; ++s) {
      if (*s == Ctrl_V || *s == '\n')
          ++len;
      ++len;
   }

   //If the string will be too long, write its length and put it in the next line. Take into 
   //account that some room is needed for what comes before the string (e.g., variable name). 
   //Add something to the length for the '<', NL and trailing ZERO.
   if (len > LSIZE / 2)
      fprintf(fd, "\026%d\n<", len + 3);

   while ((c = *p++) != ZERO) {
      if (c == Ctrl_V || c == '\n') {
         putc(Ctrl_V, fd);
         if (c == '\n')
            c = 'n';
      }
      putc(c, fd);
   }
   putc('\n', fd);
}

//Write a string in quotes that barline_parse() can read back. Break the line in less than LSIZE 
//pieces when needed. Return remaining characters in the line.
private int
barline_writestring(FILE *fd, CS s, int remaining_start) {
   Byte *p;
   int       remaining = remaining_start;
   int       len = 2;

   //Count the number of characters produced, including quotes.
   for (p = s; *p != ZERO; ++p) {
      if (*p == NL)
          len += 2;
      ei (*p == '"' || *p == '\\')
          len += 2;
      else
          ++len;
   }
   if (len > remaining - 2) {
      fprintf(fd, ">%d\n|<", len);
      remaining = LSIZE - 20;
   }

   putc('"', fd);
   for (p = s; *p != ZERO; ++p) {
      if (*p == NL) {
          putc('\\', fd);
          putc('n', fd);
          --remaining;
      } ei (*p == '"' || *p == '\\') {
          putc('\\', fd);
          putc(*p, fd);
          --remaining;
      }
      else
          putc(*p, fd);
      --remaining;

      if (remaining < 3) {
          putc('\n', fd);
          putc('|', fd);
          putc('<', fd);
          //Leave enough space for another continuation.
          remaining = LSIZE - 20;
      }
   }
   putc('"', fd);
   return remaining - 2;
}

//Check string read from eeglinfo file.
//Remove '\n' at the end of the line.
//- replace CTRL-V CTRL-V with CTRL-V
//- replace CTRL-V 'n'    with '\n'
//
//Check for a long line as written by eeglinfo_writestring().
//
//Return the string in allocated memory (NULL when out of memory).
private CS
eeglinfo_readstring(Vir* virp, int off) {          //offset for virp->line
   CS retval = NULL;
   CS s;
   long len;

   if (virp->line[off] == Ctrl_V && eeIsDigit(virp->line[off + 1])) {
      len = atol((char *)virp->line + off + 1);
      if (len > 0 && len < 1000000)
         retval = lalloc(len, true);
      else {
         //Invalid length, line too long?  Skip next line.
         (void)eeFgets(virp->line, 10, virp->vir_fd);
         return NULL;
      }
      (void)eeFgets(retval, (int)len, virp->vir_fd);
      s = retval + 1;       //Skip the leading '<'
   } else {
      retval = copyStr(virp->line + off);
      s = retval;
   }

   //Change CTRL-V CTRL-V to CTRL-V and CTRL-V n to \n in-place.
   CS d = retval;
   while (*s != ZERO && *s != '\n') {
      if (s[0] == Ctrl_V && s[1] != ZERO) {
         if (s[1] == 'n')
            *d++ = '\n';
         else
            *d++ = Ctrl_V;
         s += 2;
      } else
         *d++ = *s++;
   }
   *d = ZERO;
   return retval;
}

//Read a line from the eeglinfo file. Return true for end-of-file;
private int
eeglinfo_readline(Vir* virp) {
   return eeFgets(virp->line, LSIZE, virp->vir_fd);
}

private int
readEeglinfoBookList(Vir* virp, int writing) {
   CS tab;
   LineNr   lnum;
   ColNr   col;
   Book* book;
   CS sfname;

   //Handle long line and escaped characters.
   CS xline = eeglinfo_readstring(virp, 1);

   //don't read in if there are files on the command-line or if writing:
   if (xline && !writing && ARGCOUNT == 0 && find_eeglinfo_parameter('%') != NULL) {
      //Format is: <fname> Tab <lnum> Tab <col>.
      //Watch out for a Tab in the file name, work from the end.
      lnum = 0;
      col = 0;
      tab = lastOccurrence(xline, '\t');
      if (tab != NULL) {
         *tab++ = '\0';
         col = (ColNr)atoi((char *)tab);
         tab = lastOccurrence(xline, '\t');
         if (tab) {
            *tab++ = '\0';
            lnum = atol((char *)tab);
         }
      }

      //Expand "~/" in the file name at "line + 1" to a full path.
      //Then try shortening it by comparing with the current directory
      doExpandEnv(OUT nameBuffTextG, xline);
      sfname = shorten_fname1(nameBuffG);

      book = bookNew(nameBuffG, sfname, (LineNr)0, BLN_LISTED);
      if (book != NULL) {  //just in case...
         book->lastCursor.lnum = lnum;
         book->lastCursor.col = col;
         bookSetPosInPort(book, curPor, lnum, col, false);
      }
   }
   eeglFree(xline);

   return eeglinfo_readline(virp);
}

//Return true if "name" is on removable media (depending on @eeglinfo).
private Boole
removable(CS name) {
   if (!p_eeglinfo)
      return false;
      
   Byte part[51];
   Boole retval = false;
   Unt  n;
   name = home_replace_save(NULL, name);
   for (CS p = p_eeglinfo; *p; ) {
      strCutPathFromListOfPaths(OUT &p, OUT part, 51, S", ");
      if (part[0] == 'r') {
         n = STRLEN(part + 1);
         if (caseInsensitiveCompareNChars(part + 1, name, n) == 0) {
            retval = true;
            break;
         }
      }
   }
   eeglFree(name);
   return retval;
}

private void
writeEeglInfoBookList(FILE* fp) {
   if (find_eeglinfo_parameter('%') == NULL)
      return;

   //Without a number -1 is returned: do all books.
   int max_buffers = get_eeglinfo_parameter('%');

   //Allocate room for the file name, lnum and col.
#define LINE_BUF_LEN (MAXPATHL + 40)
   Byte line[LINE_BUF_LEN];

   Tab* tp;
   Portal* port;
   FOR_ALL_TAB_PORTALS(tp, port) {
      set_last_cursor(port);
   } 

   FPUTS(_("\n# Book list:\n"), fp);
   Book* book;
   FOR_ALL_BOOKS(book) {
      if (book->currFileName == NULL
            || !book->o.bookListed
            || isLocationListBook(book)
            || bt_terminal(book)
            || removable(book->fullFileName))
         continue;

      if (max_buffers-- == 0)
         break;
      putc('%', fp);
      home_replace(book->fullFileName, line, MAXPATHL, true);
      eeSnprintfAdd(line, LINE_BUF_LEN, "\t%ld\t%d",
            (long)book->lastCursor.lnum,
            book->lastCursor.col);
      eeglinfo_writestring(fp, line);
   }
}

//Buffers for history read from a eeglinfo file.  Only valid while reading.
private HistoryEntry *eeglinfo_history[HIST_COUNT] = {NULL, NULL, NULL, NULL, NULL};
private int   eeglinfo_hisidx[HIST_COUNT] = {0, 0, 0, 0, 0};
private int   eeglinfo_hislen[HIST_COUNT] = {0, 0, 0, 0, 0};
private int   eeglinfo_add_at_front = false;

//Translate a history type number to the associated character.
private int
hist_type2char(int type, int use_question) {      //use '?' instead of '/'
   if (type == HIST_CMD)
      return ':';
   if (type == HIST_SEARCH) {
      if (use_question)
         return '?';
      else
         return '/';
   }
   if (type == HIST_EXPR)
      return '=';
   return '@';
}

//Prepare for reading the history from the eeglinfo file.
//This allocates history arrays to store the read history lines.
private void
prepare_eeglinfo_history(int asklen, int writing) {
   init_history();
   int hislen = getHistLen();
   eeglinfo_add_at_front = (asklen != 0 && !writing);
   if (asklen > hislen)
      asklen = hislen;

   for (int type = 0; type < HIST_COUNT; ++type) {
      HistoryEntry *histentry = get_histentry(type);

      //Count the number of empty spaces in the history list.  Entries read from eeglinfo previously
      //are also considered empty. If there are more spaces available than we request, then fill 
      //them up.
      int num;
      int i;
      for (i = 0, num = 0; i < hislen; i++)
         if (histentry[i].hisstr == NULL || histentry[i].eeglinfo)
            num++;
      int len = asklen;
      if (num > len)
         len = num;
      if (len <= 0)
         eeglinfo_history[type] = NULL;
      else
         eeglinfo_history[type] = LALLOC_MULT(HistoryEntry, len);
      if (eeglinfo_history[type] == NULL)
         len = 0;
      eeglinfo_hislen[type] = len;
      eeglinfo_hisidx[type] = 0;
   }
}

//Accept a line from the eeglinfo, store it in the history array when it's new.
private int
read_eeglinfo_history(Vir* virp, int writing) {
   int type = hist_char2type(virp->line[0]);
   if (eeglinfo_hisidx[type] >= eeglinfo_hislen[type])
      goto done;

   CS val = eeglinfo_readstring(virp, 1);
   if (!val || *val == ZERO)
      goto done;

   int sep = (*val == ' ' ? ZERO : *val);

   if (in_history(type, val + (type == HIST_SEARCH), eeglinfo_add_at_front, sep, writing))
      goto done;

   //Need to re-allocate to append the separator byte.
   Ulong len = STRLEN(val);
   CS p;
   if (type == HIST_SEARCH) {
      p = alloc((Unt)len + 1); //+1 for the ZERO. val already includes the separator.

      //Search entry: Move the separator from the first column to after the ZERO.
      MEMMOVE(p, val + 1, (Unt)len);
      p[len] = sep;
      --len;                //take into account the shortened string
   } else {
      p = alloc((Unt)len + 2);       //+1 for ZERO and +1 for separator

      //Not a search entry: No separator in the eeglinfo file, add a ZERO separator.
      MEMMOVE(p, val, (Unt)len + 1);   //+1 to include the ZERO
      p[len + 1] = ZERO;         //put the separator *after* the string's ZERO
   }
   eeglinfo_history[type][eeglinfo_hisidx[type]].hisstr = p;
   eeglinfo_history[type][eeglinfo_hisidx[type]].hisstrlen = (Unt)len;
   eeglinfo_history[type][eeglinfo_hisidx[type]].time_set = 0;
   eeglinfo_history[type][eeglinfo_hisidx[type]].eeglinfo = true;
   eeglinfo_history[type][eeglinfo_hisidx[type]].hisnum = 0;
   eeglinfo_hisidx[type]++;

done:
   eeglFree(val);
   return eeglinfo_readline(virp);
}

//Accept a new style history line from the eeglinfo, store it in the history array when it's new.
private void
handle_eeglinfo_history(ArrayList* values, int writing) {
   BVal* vp = (BVal *)values->c;

   //Check the format:
   //|{bartype},{histtype},{timestamp},{separator},"text"
   if (values->len < 4
        || vp[0].btag != BVAL_NR
        || vp[1].btag != BVAL_NR
        || (vp[2].btag != BVAL_NR && vp[2].btag != BVAL_EMPTY)
        || vp[3].btag != BVAL_STRING)
      return;

   int type = vp[0].bv_nr;
   if (type >= HIST_COUNT)
      return;

   if (eeglinfo_hisidx[type] >= eeglinfo_hislen[type])
      return;

   CS val = vp[3].bv_string;
   if (!val || *val == ZERO)
      return;

   int sep = type == HIST_SEARCH && vp[2].btag == BVAL_NR ? vp[2].bv_nr : ZERO;
   int idx;
   int overwrite = false;

   if (in_history(type, val, eeglinfo_add_at_front, sep, writing))
      return;

   Ulong len;
   CS p;
   
   //If lines were written by an older Eegl, we need to avoid getting duplicates. See if the 
   //entry already exists.
   for (idx = 0; idx < eeglinfo_hisidx[type]; ++idx) {
      p = eeglinfo_history[type][idx].hisstr;
      len = eeglinfo_history[type][idx].hisstrlen;
      if (STRCMP(val, p) == 0 && (type != HIST_SEARCH || sep == p[len + 1])) {
          overwrite = true;
          break;
      }
   }

   if (!overwrite) {
      //Need to re-allocate to append the separator byte.
      len = vp[3].bv_len;
      p = alloc(len + 2);
   } else
      len = 0; //for picky compilers
   if (p) {
      eeglinfo_history[type][idx].time_set = vp[1].bv_nr;
      if (!overwrite) {
          MEMMOVE(p, val, (Unt)len + 1);
          //Put the separator after the ZERO.
          p[len + 1] = sep;
          eeglinfo_history[type][idx].hisstr = p;
          eeglinfo_history[type][idx].hisstrlen = (Unt)len;
          eeglinfo_history[type][idx].hisnum = 0;
          eeglinfo_history[type][idx].eeglinfo = true;
          eeglinfo_hisidx[type]++;
      }
   }
}

//Concatenate history lines from eeglinfo after the lines typed in this Eegl.
private void
concat_history(int type) {
   int i;
   int hislen = getHistLen();
   HistoryEntry *histentry = get_histentry(type);
   int* hisidx = get_hisidx(type);
   int* hisnum = get_hisnum(type);

   int idx = *hisidx + eeglinfo_hisidx[type];
   if (idx >= hislen)
      idx -= hislen;
   ei (idx < 0)
      idx = hislen - 1;
   if (eeglinfo_add_at_front)
      *hisidx = idx;
   else {
      if (*hisidx == -1)
          *hisidx = hislen - 1;
      do {
         if (histentry[idx].hisstr != NULL || histentry[idx].eeglinfo)
            break;
         if (++idx == hislen)
            idx = 0;
      } while (idx != *hisidx);
      if (idx != *hisidx && --idx < 0)
         idx = hislen - 1;
   }
   for (i = 0; i < eeglinfo_hisidx[type]; i++) {
      eeglFree(histentry[idx].hisstr);
      histentry[idx].hisstr = eeglinfo_history[type][i].hisstr;
      histentry[idx].hisstrlen = eeglinfo_history[type][i].hisstrlen;
      histentry[idx].eeglinfo = true;
      histentry[idx].time_set = eeglinfo_history[type][i].time_set;
      if (--idx < 0)
         idx = hislen - 1;
   }
   idx += 1;
   idx %= hislen;
   for (i = 0; i < eeglinfo_hisidx[type]; i++) {
      histentry[idx++].hisnum = ++*hisnum;
      idx %= hislen;
   }
}

private int
sort_hist(const void *s1, const void *s2) {
   HistoryEntry* p1 = *(HistoryEntry **)s1;
   HistoryEntry* p2 = *(HistoryEntry **)s2;

   if (p1->time_set < p2->time_set) return -1;
   if (p1->time_set > p2->time_set) return 1;
   return 0;
}

//Merge history lines from eeglinfo and lines typed in this Eegl based on the timestamp;
private void
merge_history(int type) {
   HistoryEntry **tot_hist;
   HistoryEntry *new_hist;
   int hislen = getHistLen();
   HistoryEntry *histentry = get_histentry(type);
   int* hisidx = get_hisidx(type);
   int* hisnum = get_hisnum(type);

   //Make one long list with all entries.
   int max_len = hislen + eeglinfo_hisidx[type];
   tot_hist = ALLOC_MULT(HistoryEntry *, max_len);
   new_hist = ALLOC_MULT(HistoryEntry, hislen);
   if (tot_hist == NULL || new_hist == NULL) {
      eeglFree(tot_hist);
      eeglFree(new_hist);
      return;
   }
   int i;
   for (i = 0; i < eeglinfo_hisidx[type]; i++)
      tot_hist[i] = &eeglinfo_history[type][i];
   int len = i;
   for (i = 0; i < hislen; i++) {
      if (histentry[i].hisstr != NULL)
         tot_hist[len++] = &histentry[i];
   } 

   //Sort the list on timestamp.
   qsort((void *)tot_hist, (Unt)len, sizeof(HistoryEntry *), sort_hist);

   //Keep the newest ones.
   for (i = 0; i < hislen; i++) {
      if (i < len) {
          new_hist[i] = *tot_hist[i];
          tot_hist[i]->hisstr = NULL;
          tot_hist[i]->hisstrlen = 0;
          if (new_hist[i].hisnum == 0)
         new_hist[i].hisnum = ++*hisnum;
      } else
         clear_hist_entry(&new_hist[i]);
   }
   *hisidx = (i < len ? i : len) - 1;

   //Free what is not kept.
   for (i = 0; i < eeglinfo_hisidx[type]; i++) {
      eeglFree(eeglinfo_history[type][i].hisstr);
      eeglinfo_history[type][i].hisstrlen = 0;
   }
   for (i = 0; i < hislen; i++) {
      eeglFree(histentry[i].hisstr);
      histentry[i].hisstrlen = 0;
   }
   eeglFree(histentry);
   set_histentry(type, new_hist);
   eeglFree(tot_hist);
}

//Finish reading history lines from eeglinfo.  Not used when writing eeglinfo.
private void
finish_eeglinfo_history(Vir *virp) {
   int type;
   int merge = virp->vir_version >= EEGLINFO_VERSION_WITH_HISTORY;

   for (type = 0; type < HIST_COUNT; ++type) {
      if (get_histentry(type) == NULL)
         continue;

      if (merge)
         merge_history(type);
      else
         concat_history(type);

      EE_CLEAR(eeglinfo_history[type]);
      eeglinfo_hisidx[type] = 0;
   }
}

//Write history to eeglinfo file in "fp".
//When "merge" is true merge history lines with a previously read eeglinfo
//file, data is in eeglinfo_history[].
//When "merge" is false just write all history lines.  Used for ":weeglinfo!".
private void
write_eeglinfo_history(FILE *fp, int merge) {
   int i;
   int type;
   int num_saved;
   int round;

   init_history();
   int hislen = getHistLen();
   if (hislen == 0)
      return;
   for (type = 0; type < HIST_COUNT; ++type) {
      HistoryEntry *histentry = get_histentry(type);
      int       *hisidx = get_hisidx(type);

      num_saved = get_eeglinfo_parameter(hist_type2char(type, false));
      if (num_saved == 0)
          continue;
      if (num_saved < 0)  //Use default
          num_saved = hislen;
      fprintf(fp, (char*)_("\n# %s History (newest to oldest):\n"),
                type == HIST_CMD ? _("Command Line") :
                type == HIST_SEARCH ? _("Search String") :
                type == HIST_EXPR ? _("Expression") :
                type == HIST_INPUT ? _("Input Line") :
                  _("Debug Line"));
      if (num_saved > hislen)
          num_saved = hislen;

      //Merge typed and eeglinfo history:
      //round 1: history of typed commands.
      //round 2: history from recently read eeglinfo.
      for (round = 1; round <= 2; ++round) {
         if (round == 1)
            //start at newest entry, somewhere in the list
            i = *hisidx;
         ei (eeglinfo_hisidx[type] > 0)
            //start at newest entry, first in the list
            i = 0;
         else
            //empty list
            i = -1;
         if (i >= 0) {
            while (num_saved > 0 && !(round == 2 && i >= eeglinfo_hisidx[type])) {
               CS p;
               Unt plen;
               Tyme timestamp;
               int c = ZERO;

               if (round == 1) {
                  p = histentry[i].hisstr;
                  plen = histentry[i].hisstrlen;
                  timestamp = histentry[i].time_set;
               } else {
                  if (eeglinfo_history[type] == NULL) {
                     p = NULL;
                     plen = 0;
                     timestamp = 0;
                  } else {
                     p = eeglinfo_history[type][i].hisstr;
                     plen = eeglinfo_history[type][i].hisstrlen;
                     timestamp = eeglinfo_history[type][i].time_set;
                  }
               }

               if (p != NULL && (round == 2 || !merge || !histentry[i].eeglinfo)) {
                  --num_saved;
                  fputc(hist_type2char(type, true), fp);
                  //For the search history: put the separator in the
                  //second column; use a space if there isn't one.
                  if (type == HIST_SEARCH) {
                      c = p[plen + 1];
                      putc(c == ZERO ? ' ' : c, fp);
                  }
                  eeglinfo_writestring(fp, p);

                  {
                     char    cbuf[NUMBUFLEN];

                     //New style history with a bar line. Format:
                     //|{bartype},{histtype},{timestamp},{separator},"text"
                     if (c == ZERO)
                        cbuf[0] = ZERO;
                     else
                        sprintf(cbuf, "%d", c);
                     fprintf(fp, "|%d,%d,%ld,%s,", BARTYPE_HISTORY, type, (long)timestamp, cbuf);
                     barline_writestring(fp, p, LSIZE - 20);
                     putc('\n', fp);
                  }
               }
               if (round == 1) {
                  //Decrement index, loop around and stop when back at the start.
                  if (--i < 0)
                     i = hislen - 1;
                  if (i == *hisidx)
                     break;
               } else {
                  //Increment index. Stop at the end in the while.
                  ++i;
               }
            }
         } 
      }
      for (i = 0; i < eeglinfo_hisidx[type]; ++i) {
         if (eeglinfo_history[type] != NULL) {
            eeglFree(eeglinfo_history[type][i].hisstr);
            eeglinfo_history[type][i].hisstrlen = 0;
         }
      } 
      EE_CLEAR(eeglinfo_history[type]);
      eeglinfo_hisidx[type] = 0;
   }
}

private void
write_eeglinfo_barlines(Vir *virp, FILE *fp_out) {
   int i;
   ArrayList* gap = &virp->vir_barlines;
   int seen_useful = false;
   char* line;

   if (gap->len <= 0)
      return;

   FPUTS(_("\n# Bar lines, copied verbatim:\n"), fp_out);

   //Skip over continuation lines until seeing a useful line.
   for (i = 0; i < gap->len; ++i) {
      line = ((char **)(gap->c))[i];
      if (seen_useful || line[1] != '<') {
         fputs(line, fp_out);
         seen_useful = true;
      }
   }
}

//Parse a eeglinfo line starting with '|'. Add each decoded value to "values".
//Return true if the next line is to be read after using the parsed values.
private int
barline_parse(Vir* virp, CS text, ArrayList* values) {
   CS p = text;
   CS nextp = NULL;
   CS buf = NULL;
   BVal  *value;
   int i;
   int allocated = false;
   int eof;
   int converted;

   while (*p == ',') {
      ++p;
      if (ga_grow(values, 1) == FAIL)
         break;
      value = (BVal *)(values->c) + values->len;

      if (*p == '>') {
         //Need to read a continuation line.  Put strings in allocated
         //memory, because virp->line is overwritten.
         if (!allocated) {
            for (i = 0; i < values->len; ++i) {
               BVal  *vp = (BVal *)(values->c) + i;

               if (vp->btag == BVAL_STRING && !vp->bv_allocated) {
                  vp->bv_string = copySubstr(vp->bv_string, vp->bv_len);
                  vp->bv_allocated = true;
               }
            }
            allocated = true;
         }

         if (eeIsDigit(p[1])) {
            Unt len;
            Unt todo;
            Unt n;

            //String value was split into lines that are each shorter
            //than LSIZE:
            //    |{bartype},>{length of "{text}{text2}"}
            //    |<"{text1}
            //    |<{text2}",{value}
            //Length includes the quotes.
            ++p;
            len = parseLong(&p);
            buf = alloc((int)(len + 1));
            p = buf;
            for (todo = len; todo > 0; todo -= n) {
               eof = eeglinfo_readline(virp);
               if (eof || virp->line[0] != '|' || virp->line[1] != '<') {
                  //File was truncated or garbled. Read another line if this one starts with '|'.
                  eeglFree(buf);
                  return eof || virp->line[0] == '|';
               }
               //Get length of text, excluding |< and NL chars.
               n = STRLEN(virp->line);
               while (n > 0 && (virp->line[n - 1] == NL || virp->line[n - 1] == ENTER))
                  --n;
               n -= 2;
               if (n > todo) {
                  //more values follow after the string
                  nextp = virp->line + 2 + todo;
                  n = todo;
               }
                MEMMOVE(p, virp->line + 2, n);
                p += n;
            }
            *p = ZERO;
            p = buf;
          } else {
            //Line ending in ">" continues in the next line:
            //    |{bartype},{lots of values},>
            //    |<{value},{value}
            eof = eeglinfo_readline(virp);
            if (eof || virp->line[0] != '|' || virp->line[1] != '<')
               //File was truncated or garbled. Read another line if
               //this one starts with '|'.
               return eof || virp->line[0] == '|';
            p = virp->line + 2;
         }
      }

      if (SAFE_isdigit(*p)) {
          value->btag = BVAL_NR;
          value->bv_nr = parseLong(&p);
          ++values->len;
      } ei (*p == '"') {
         int len = 0;
         CS s = p;

         //Unescape special characters in-place.
         ++p;
         while (*p != '"') {
            if (*p == NL || *p == ZERO)
               return true;  //syntax error, drop the value
            if (*p == '\\') {
               ++p;
               if (*p == 'n')
                  s[len++] = '\n';
               else
                  s[len++] = *p;
               ++p;
            } else
               s[len++] = *p++;
         }
         ++p;
         s[len] = ZERO;

         converted = false;
         value->bv_tofree = NULL;

         //Need to copy in allocated memory if the string wasn't allocated
         //above and we did allocate before, thus line may change.
         if (s != buf && allocated && !converted)
            s = copySubstr(s, len);
         value->bv_string = s;
         value->btag = BVAL_STRING;
         value->bv_len = len;
         value->bv_allocated = allocated || converted;
         ++values->len;
         if (nextp) {
            //values following a long string
            p = nextp;
            nextp = NULL;
         }
      } ei (*p == ',') {
         value->btag = BVAL_EMPTY;
         ++values->len;
      } else
         break;
   }
   return true;
}

private void
write_eeglinfo_version(FILE* fp_out) {
   fprintf(fp_out, "# Eeglinfo version\n|%d,%d\n\n", BARTYPE_VERSION, EEGLINFO_VERSION);
}

private int
no_eeglinfo(void) {
   //"vim -i NONE" does not read or write a eeglinfo file
   return !p_eeglinfofile || STRCMP(p_eeglinfofile, "NONE") == 0;
}

//Report an error for reading a eeglinfo file.
//Count the number of errors.   When there are more than 10, return true.
private int
eeglinfo_error(CS errnum, CS message, Byte *line) {
   eeSnprintf(IObuff, IOSIZE, _("%seeglinfo: %s in line: "), errnum, message);
   STRNCAT(IObuff, line, IOSIZE - STRLEN(IObuff) - 1);
   if (IObuff[STRLEN(IObuff) - 1] == '\n')
      IObuff[STRLEN(IObuff) - 1] = ZERO;
   emsg(IObuff);
   if (++eeglinfo_errcnt >= 10) {
      emsg(_(e_eeglinfo_too_many_errors_skipping_rest_of_file));
      return true;
   }
   return false;
}

//Restore global vars that start with a capital from the eeglinfo file
private int
read_eeglinfo_varlist(Vir* virp, int writing) {
   int type = VAR_NUMBER;
   Var tv;
   FnCallEntry funccal_entry;

   if (!writing && (find_eeglinfo_parameter('!') != NULL)) {
      CS tab = firstOccurrence(virp->line + 1, '\t');
      if (tab) {
         *tab++ = '\0';   //isolate the variable name
         switch (*tab) {
         case 'S': type = VAR_STRING; break;
         case 'F': type = VAR_FLOAT; break;
         case 'D': type = VAR_BAG; break;
         case 'L': type = VAR_LIST; break;
         case 'B': type = VAR_BLOB; break;
         }

         tab = firstOccurrence(tab, '\t');
         if (tab) {
            tv.tag = type;
            if (type == VAR_STRING || type == VAR_BAG || type == VAR_LIST || type == VAR_BLOB)
               tv.string = eeglinfo_readstring(virp, (int)(tab - virp->line + 1));
            ei (type == VAR_FLOAT)
               (void)string2float(tab + 1, OUT &tv.floatt, false);
            else {
               tv.number = atol((char *)tab + 1);
            }
            if (type == VAR_BAG || type == VAR_LIST) {
               Var *etv = eval_expr(tv.string, NULL);

               if (etv == NULL)
                  //Failed to parse back the dict or list, use it as a string.
                  tv.tag = VAR_STRING;
               else {
                  eeglFree(tv.string);
                  tv = *etv;
                  eeglFree(etv);
                }
            } ei (type == VAR_BLOB) {
               Blob *blob = string2blob(tv.string);

               if (blob == NULL)
                  //Failed to parse back the blob, use it as a string.
                  tv.tag = VAR_STRING;
               else {
                  eeglFree(tv.string);
                  tv.tag = VAR_BLOB;
                  tv.blob = blob;
               }
            }

            //when in a function use global variables
            save_funccal(&funccal_entry);
            set_var(mbText(virp->line + 1), &tv, false);
            restore_funccal();

            if (tv.tag == VAR_STRING)
               eeglFree(tv.string);
            ei (tv.tag == VAR_BAG || tv.tag == VAR_LIST || tv.tag == VAR_BLOB)
               clearVar(&tv);
         }
      }
   }

   return eeglinfo_readline(virp);
}

//Write global vars that start with a capital to the eeglinfo file
private void
write_eeglinfo_varlist(FILE* fp) {
   EeSet* gvht = get_globvar_ht();
   EeSetItem* hi;
   CS s = S"";
   CS p;
   CS tofree;
   Byte numbuf[NUMBUFLEN];

   if (find_eeglinfo_parameter('!') == NULL)
      return;

   FPUTS(_("\n# global variables:\n"), fp);

   int todo = (int)gvht->count;
   FOR_ALL_HASHTAB_ITEMS(gvht, hi, todo) {
      if (!HASHITEM_EMPTY(hi)) {
         --todo;
         DictItem* this_var = HI2DI(hi);
         if (getVarFlavor(this_var->key) == VAR_FLAVOR_EEGLINFO) {
            switch (this_var->c.tag) {
            case VAR_STRING:  s = S"STR"; break;
            case VAR_NUMBER:  s = S"NUM"; break;
            case VAR_FLOAT:   s = S"FLO"; break;
            case VAR_BAG: {
               Bag   *di = this_var->c.bag;
               int   copyID = get_copyID();

               s = S"DIC";
               if (di && !setRefInSet(&di->hashTable, copyID, NULL) && di->copyId == copyID)
                  //has a circular reference, can't turn the value into a string
                  continue;
               break;
            }
            case VAR_LIST: {
               List   *l = this_var->c.list;
               int   copyID = get_copyID();

               s = S"LIS";
               if (l && !set_ref_in_list_items(l, copyID, NULL) && l->copyId == copyID)
                  //has a circular reference, can't turn the value into a string
                  continue;
               break;
            }
            case VAR_BLOB:    s = S"BLO"; break;
            case VAR_BOOL:    s = S"XPL"; break;  //backwards compat.

            case VAR_UNKNOWN:
            case VAR_ANY:
            case VAR_VOID:
            case VAR_FUNC:
            case VAR_PARTIAL:
            case VAR_JOB:
            case VAR_CHANNEL:
               continue;
            }
            fprintf(fp, "!%s\t%s\t", this_var->key, s);
            if (this_var->c.tag == VAR_BOOL) {
               //do not use "v:true" but "1"
               sprintf((char *)numbuf, "%ld", (long)this_var->c.number);
               p = numbuf;
               tofree = NULL;
            } else
               p = echo_string(&this_var->c, &tofree, numbuf, 0);
            if (p)
               eeglinfo_writestring(fp, p);
            eeglFree(tofree);
         }
      }
   }
}

private int
read_eeglinfo_sub_string(Vir* virp, int force) {
   if (force || get_old_sub() == NULL)
      set_old_sub(eeglinfo_readstring(virp, 1));
   return eeglinfo_readline(virp);
}

private void
write_eeglinfo_sub_string(FILE *fp) {
   CS old_sub = get_old_sub();

   if (get_eeglinfo_parameter('/') == 0 || old_sub == NULL)
      return;

   FPUTS(_("\n# Last Substitute String:\n$"), fp);
   eeglinfo_writestring(fp, old_sub);
}

//Functions relating to reading/writing the search pattern from eeglinfo

private int
read_eeglinfo_search_pattern(Vir* virp, Boole force) {
   int idx = -1;
   int magic = false;
   int no_scs = false;
   int off_line = false;
   int off_end = 0;
   long off = 0;
   int setlast = false;
   static Boole   hlsearch_on = false;
   Byte* val;
   SearchPattern* spat;

   //Old line types:
   //"/pat", "&pat": search/subst. pat
   //"~/pat", "~&pat": last used search/subst. pat
   //New line types:
   //"~h", "~H": hlsearch hiliting off/on
   //"~<magic><smartcase><line><end><off><last><which>pat"
   //<magic>: 'm' off, 'M' on
   //<smartcase>: 's' off, 'S' on
   //<line>: 'L' line offset, 'l' char offset
   //<end>: 'E' from end, 'e' from start
   //<off>: decimal, offset
   //<last>: '~' last used pattern
   //<which>: '/' search pat, '&' subst. pat
   CS lp = virp->line;
   if (lp[0] == '~' && (lp[1] == 'm' || lp[1] == 'M')) {  //new line type
      if (lp[1] == 'M')      //magic on
         magic = true;
      if (lp[2] == 's')
         no_scs = true;
      if (lp[3] == 'L')
         off_line = true;
      if (lp[4] == 'E')
         off_end = SEARCH_END;
      lp += 5;
      off = parseLong(&lp);
   }
   if (lp[0] == '~') {     //use this pattern for last-used pattern
      setlast = true;
      lp++;
   }
   if (lp[0] == '/')
      idx = RE_SEARCH;
   ei (lp[0] == '&')
      idx = RE_SUBST;
   ei (lp[0] == 'h')   //~h: 'hlsearch' hiliting off
      hlsearch_on = false;
   ei (lp[0] == 'H')   //~H: 'hlsearch' hiliting on
      hlsearch_on = true;
   if (idx >= 0) {
      spat = getPrevSearchPattern(idx);
      if (force || spat->pat.len == 0) {
         val = eeglinfo_readstring(virp, (int)(lp - virp->line + 1));
         if (val) {
            set_last_search_pat(val, idx, magic, setlast);
            eeglFree(val);
            spat->no_scs = no_scs;
            spat->off.line = off_line;
            spat->off.end = off_end;
            spat->off.off = off;
            if (setlast)
               setHlsearch(hlsearch_on);
         }
      }
   }
   return eeglinfo_readline(virp);
}

private void
wvsp_one(
   FILE* fp,   //file to write to
   int idx,   //spats[] index
   CS s,   //search pat
   int sc   //dir char
){
   SearchPattern* spat = getPrevSearchPattern(idx);
   if (spat->pat.len == 0)
      return;

   fprintf(fp, (char*)_("\n# Last %sSearch Pattern:\n~"), s);
   //off.dir is not stored, it's reset to forward
   fprintf(
      fp, "%c%c%c%c%ld%s%c",
      spat->magic    ? 'M' : 'm',   //magic
      spat->no_scs   ? 's' : 'S',   //smartcase
      spat->off.line ? 'L' : 'l',   //line offset
      spat->off.end  ? 'E' : 'e',   //offset from end
      spat->off.off,         //offset
      getPrevSearchOrSubstPattern() == idx ? "~" : "",   //last used pat
      sc
   );
   eeglinfo_writestring(fp, spat->pat.c);
}

private void
write_eeglinfo_search_pattern(FILE* fp) {
   if (get_eeglinfo_parameter('/') == 0)
      return;

   fprintf(fp, "\n# hlsearch on (H) or off (h):\n~%c",
       (!hiliteSearchG || find_eeglinfo_parameter('h') != NULL) ? 'h' : 'H');
   wvsp_one(fp, RE_SEARCH, S"", '/');
   wvsp_one(fp, RE_SUBST, _("Substitute "), '&');
}

//Functions relating to reading/writing registers from eeglinfo

private YankReg *y_read_regs = NULL;

#define REG_PREVIOUS 1
#define REG_EXEC 2

//Prepare for reading eeglinfo registers when writing eeglinfo later.
private void
prepare_eeglinfo_registers(void) {
   y_read_regs = ALLOC_CLEAR_MULT(YankReg, NUM_REGISTERS);
}

private void
finish_eeglinfo_registers(void) {
   if (!y_read_regs)
      return;

   for (Unt i = 0; i < NUM_REGISTERS; ++i) {
      if (y_read_regs[i].y_array != NULL) {
         for (int j = 0; j < y_read_regs[i].y_size; j++)
            eeglFree(y_read_regs[i].y_array[j].c);
         eeglFree(y_read_regs[i].y_array);
      }
   } 
   EE_CLEAR(y_read_regs);
}

private int
read_eeglinfo_register(Vir* virp, Boole force) {
   int eof;
   int do_it = true;
   int set_prev = false;
   Arr(Text) array = NULL;
   int      new_type = MCHAR; //init to shut up compiler
   ColNr   new_width = 0; //init to shut up compiler
   YankReg   *y_current_p;

   //We only get here (hopefully) if line[0] == '"'
   CS str = virp->line + 1;

   //If the line starts with "" this is the y_previous register.
   if (*str == '"') {
      set_prev = true;
      str++;
   }

   if (!ASCII_ISALNUM(*str) && *str != '-') {
      if (eeglinfo_error(S"E577: ", _(e_illegal_register_name), virp->line))
          return true;   //too many errors, pretend end-of-file
      do_it = false;
   }
   get_yank_register(*str++, false);
   y_current_p = get_y_current();
   if (!force && y_current_p->y_array != NULL)
      do_it = false;

   if (*str == '@') {
      //"x@: register x used for @@
      if (force || get_execreg_lastc() == ZERO)
          set_execreg_lastc(str[-1]);
   }

   int size = 0;
   int limit = 100;   //Optimized for registers containing <= 100 lines
   if (do_it) {
      //Build the new register in array[].
      //y_array is kept as-is until done.
      //The "do_it" flag is reset when something is wrong, in which case
      //array[] needs to be freed.
      if (set_prev)
         set_y_previous(y_current_p);
      array = ALLOC_MULT(Text, limit);
      str = skipwhite(skiptowhite(str));
      if (STRNCMP(str, "CHAR", 4) == 0)
         new_type = MCHAR;
      ei (STRNCMP(str, "BLOCK", 5) == 0)
         new_type = MBLOCK;
      else
         new_type = MLINE;
      //get the block width; if it's missing we get a zero, which is OK
      str = skipwhite(skiptowhite(str));
      new_width = parseLong(&str);
   }

   while (!(eof = eeglinfo_readline(virp))
          && (virp->line[0] == TAB || virp->line[0] == '<')
   ) {
      if (do_it) {
         if (size == limit) {
            Arr(Text) new_array = (Text *)alloc(limit * 2 * sizeof(Text));
            if (!new_array) {
               do_it = false;
               break;
            }
            for (int i = 0; i < limit; i++)
               new_array[i] = array[i];
            eeglFree(array);
            array = new_array;
            limit *= 2;
         }
         str = eeglinfo_readstring(virp, 1);
         if (str) {
            array[size].c = str;
            array[size].len = STRLEN(str);
            ++size;
         } else
            //error, don't store the result
            do_it = false;
      }
   }

   if (do_it) {
      //free y_array[]
      for (int i = 0; i < y_current_p->y_size; i++)
         eeglFree(y_current_p->y_array[i].c);
      eeglFree(y_current_p->y_array);

      y_current_p->y_type = new_type;
      y_current_p->y_width = new_width;
      y_current_p->y_size = size;
      y_current_p->y_time_set = 0;
      if (size == 0) {
         y_current_p->y_array = NULL;
      } else {
         //Move the lines from array[] to y_array[].
         y_current_p->y_array = ALLOC_MULT(Text, size);
         for (int i = 0; i < size; i++) {
            if (y_current_p->y_array == NULL) {
               EE_CLEAR_STRING(array[i]);
            } else {
               y_current_p->y_array[i] = array[i];
            }
         }
      }
    } else {
      //Free array[] if it was filled.
      for (int i = 0; i < size; i++)
         eeglFree(array[i].c);
   }
   eeglFree(array);

   return eof;
}

//Accept a new style register line from the eeglinfo, store it when it's new.
private void
handle_eeglinfo_register(ArrayList *values, int force) {
   BVal   *vp = (BVal *)values->c;
   time_t   timestamp;
   YankReg   *y_ptr;
   YankReg   *y_regs_p = get_y_regs();
   int      i;

   //Check the format:
   //|{bartype},{flags},{name},{type},
   //     {linecount},{width},{timestamp},"line1","line2"
   if (values->len < 6
       || vp[0].btag != BVAL_NR
       || vp[1].btag != BVAL_NR
       || vp[2].btag != BVAL_NR
       || vp[3].btag != BVAL_NR
       || vp[4].btag != BVAL_NR
       || vp[5].btag != BVAL_NR
   )
      return;
   int flags = vp[0].bv_nr;
   int name = vp[1].bv_nr;
   if (name < 0 || name >= NUM_REGISTERS)
      return;
   int type = vp[2].bv_nr;
   if (type != MCHAR && type != MLINE && type != MBLOCK)
      return;
   int linecount = vp[3].bv_nr;
   if (values->len < 6 + linecount)
      return;
   int width = vp[4].bv_nr;
   if (width < 0)
      return;

   if (y_read_regs)
      //Reading eeglinfo for merging and writing.  Store the register
      //content, don't update the current registers.
      y_ptr = &y_read_regs[name];
   else
      y_ptr = &y_regs_p[name];

   //Do not overwrite unless forced or the timestamp is newer.
   timestamp = (time_t)vp[5].bv_nr;
   if (y_ptr->y_array && !force && (timestamp == 0 || y_ptr->y_time_set > timestamp))
      return;

   if (y_ptr->y_array) {
      for (i = 0; i < y_ptr->y_size; i++)
         eeglFree(y_ptr->y_array[i].c);
   } 
   eeglFree(y_ptr->y_array);

   if (!y_read_regs) {
      if (flags & REG_PREVIOUS)
          set_y_previous(y_ptr);
      if ((flags & REG_EXEC) && (force || get_execreg_lastc() == ZERO))
          set_execreg_lastc(get_register_name(name));
   }
   y_ptr->y_type = type;
   y_ptr->y_width = width;
   y_ptr->y_size = linecount;
   y_ptr->y_time_set = timestamp;
   if (linecount == 0) {
      y_ptr->y_array = NULL;
      return;
   }
   y_ptr->y_array = ALLOC_MULT(Text, linecount);
   if (y_ptr->y_array == NULL) {
      y_ptr->y_size = 0; //ensure object state is consistent
      return;
   }
   for (i = 0; i < linecount; i++) {
      if (vp[i + 6].bv_allocated) {
         y_ptr->y_array[i].c = vp[i + 6].bv_string;
         y_ptr->y_array[i].len = vp[i + 6].bv_len;
         vp[i + 6].bv_string = NULL;
      } ei (vp[i + 6].btag != BVAL_STRING) {
         free(y_ptr->y_array);
         y_ptr->y_array = NULL;
      } else {
         y_ptr->y_array[i].c = copySubstr(vp[i + 6].bv_string, vp[i + 6].bv_len);
         y_ptr->y_array[i].len = vp[i + 6].bv_len;
      }
    }
}

private void
write_eeglinfo_registers(FILE* fp) {
   int      i, j;
   Byte   *type;
   Byte c;
   int num_lines;
   long len;
   YankReg   *y_ptr;
   YankReg   *y_regs_p = get_y_regs();;

   FPUTS(_("\n# Registers:\n"), fp);

   //Get '<' value, use old '"' value if '<' is not found.
   int max_num_lines = get_eeglinfo_parameter('<');
   if (max_num_lines < 0)
      max_num_lines = get_eeglinfo_parameter('"');
   if (max_num_lines == 0)
      return;
   int max_kbyte = get_eeglinfo_parameter('s');
   if (max_kbyte == 0)
      return;

   for (i = 0; i < NUM_REGISTERS; i++) {
      //Skip '*'/'+' register, we don't want them back next time
      if (i == STAR_REGISTER || i == PLUS_REGISTER)
          continue;
      //Neither do we want the '~' register
      if (i == TILDE_REGISTER)
          continue;
      //When reading eeglinfo for merging and writing: Use the register from
      //eeglinfo if it's newer.
      if (y_read_regs
         && y_read_regs[i].y_array != NULL
         && (y_regs_p[i].y_array == NULL || y_read_regs[i].y_time_set > y_regs_p[i].y_time_set)
      )
          y_ptr = &y_read_regs[i];
      ei (y_regs_p[i].y_array == NULL)
         continue;
      else
         y_ptr = &y_regs_p[i];

      //Skip empty registers.
      num_lines = y_ptr->y_size;
      if (num_lines == 0
         || (num_lines == 1 && y_ptr->y_type == MCHAR
                  && *y_ptr->y_array[0].c == ZERO))
          continue;

      if (max_kbyte > 0) {
          //Skip register if there is more text than the maximum size.
          len = 0;
          for (j = 0; j < num_lines; j++)
         len += (long)y_ptr->y_array[j].len + 1L;
          if (len > (long)max_kbyte * 1024L)
         continue;
      }

      switch (y_ptr->y_type) {
      case MLINE:
         type = (CS)"LINE";
         break;
      case MCHAR:
         type = (CS)"CHAR";
         break;
      case MBLOCK:
         type = (CS)"BLOCK";
         break;
      default:
         showErrFmtMsg(_(e_unknown_register_type_nr), y_ptr->y_type);
         type = (CS)"LINE";
         break;
      }
      if (get_y_previous() == &y_regs_p[i])
         fprintf(fp, "\"");
      c = get_register_name(i);
      fprintf(fp, "\"%c", c);
      if (c == get_execreg_lastc())
         fprintf(fp, "@");
      fprintf(fp, "\t%s\t%d\n", type, (int)y_ptr->y_width);

      //If max_num_lines < 0, then we save ALL the lines in the register
      if (max_num_lines > 0 && num_lines > max_num_lines)
         num_lines = max_num_lines;
      for (j = 0; j < num_lines; j++) {
         putc('\t', fp);
         eeglinfo_writestring(fp, y_ptr->y_array[j].c);
      }

      {
         Unt flags = 0;

         //New style with a bar line. Format:
         //|{bartype},{flags},{name},{type},
         //     {linecount},{width},{timestamp},"line1","line2"
         //flags: REG_PREVIOUS - register is y_previous
         //        REG_EXEC - used for @@
         if (get_y_previous() == &y_regs_p[i])
            flags |= REG_PREVIOUS;
         if (c == get_execreg_lastc())
            flags |= REG_EXEC;
         fprintf(fp, "|%d,%d,%d,%d,%d,%d,%ld", BARTYPE_REGISTER, flags,
             i, y_ptr->y_type, num_lines, (int)y_ptr->y_width,
             (long)y_ptr->y_time_set);
         //11 chars for type/flags/name/type, 3 * 20 for numbers
         int remaining = LSIZE - 71;
         for (j = 0; j < num_lines; j++) {
            putc(',', fp);
            --remaining;
            remaining = barline_writestring(fp, y_ptr->y_array[j].c, remaining);
         }
         putc('\n', fp);
      }
   }
}

//Functions relating to reading/writing marks from eeglinfo

private FileMarkExt *vi_namedfm = NULL;
private FileMarkExt *vi_jumplist = NULL;
private int vi_jumplist_len = 0;

private void
write_one_mark(FILE* fp_out, int c, Pos* pos) {
   if (pos->lnum != 0)
      fprintf(fp_out, "\t%c\t%ld\t%d\n", c, (long)pos->lnum, (int)pos->col);
}

private void
writeBookMarks(Book* book, FILE* fp_out) {
   home_replace(book->fullFileName, IObuff, IOSIZE, true);
   fprintf(fp_out, "\n> ");
   eeglinfo_writestring(fp_out, IObuff);

   //Write the last used timestamp as the lnum of the non-existing mark '*'.
   //Older Eegls will ignore it and/or copy it.
   Pos pos;
   pos.lnum = (LineNr)book->lastUsed;
   pos.col = 0;
   write_one_mark(fp_out, '*', &pos);

   write_one_mark(fp_out, '"', &book->lastCursor);
   write_one_mark(fp_out, '^', &book->lastInsert);
   write_one_mark(fp_out, '.', &book->lastChange);
   //changelist positions are stored oldest first
   for (Unt i = 0; i < book->changeListLen; ++i) {
      //skip duplicates
      if (i == 0 || !EQUAL_POS(book->changeList[i - 1], book->changeList[i]))
          write_one_mark(fp_out, '+', &book->changeList[i]);
   }
   for (Unt i = 0; i < NMARKS; i++)
      write_one_mark(fp_out, 'a' + i, &book->namedMarks[i]);
}

//Return true if marks for "book" should not be written.
private int
skip_for_eeglinfo(Book *book) {
    return bt_terminal(book) || removable(book->fullFileName);
}

//Write all the named marks for all books.
//When "buflist" is not NULL fill it with the books for which marks are to be written.
private void
write_eeglinfo_marks(FILE* fp_out, ArrayList* buflist) {
   int is_mark_set;
   int i;

   //Set lastCursor for all books that have a portal.
   Portal   *port;
   Tab   *t;
   FOR_ALL_TAB_PORTALS(t, port)
      set_last_cursor(port);

   FPUTS(_("\n# History of marks within files (newest to oldest):\n"), fp_out);
   Book   *book;
   FOR_ALL_BOOKS(book) {
      //Only write something if book has been loaded and at least one mark is set.
      if (book->haveReadEeglinfoMarks) {
         if (book->lastCursor.lnum != 0)
            is_mark_set = true;
         else {
            is_mark_set = false;
            for (i = 0; i < NMARKS; i++)
               if (book->namedMarks[i].lnum != 0) {
                  is_mark_set = true;
                  break;
               }
         }
         if (is_mark_set && book->fullFileName && book->fullFileName[0] != ZERO
               && !skip_for_eeglinfo(book))
          {
            if (!buflist)
                writeBookMarks(book, fp_out);
            ei (ga_grow(buflist, 1) == OK)
                ((Book **)buflist->c)[buflist->len++] = book;
          }
      }
   }
}

private void
write_one_filemark(FILE* fp, FileMarkExt* fm, int c1, int c2) {
   if (fm->fmark.mark.lnum == 0)   //not set
      return;

   CS name;
   if (fm->fmark.fnum != 0)      //there is a book
      name = bookGetNameByBookNr(fm->fmark.fnum, true, false);
   else
      name = fm->fname;      //use name from .eeglinfo
   if (name && *name != ZERO) {
      fprintf(fp, "%c%c  %ld  %ld  ", c1, c2, (long)fm->fmark.mark.lnum,
                         (long)fm->fmark.mark.col);
      eeglinfo_writestring(fp, name);

      //Barline: |{bartype},{name},{lnum},{col},{timestamp},{filename}
      //size up to filename: 8 + 3 * 20
      fprintf(fp, "|%d,%d,%ld,%ld,%ld,", BARTYPE_MARK, c2,
         (long)fm->fmark.mark.lnum, (long)fm->fmark.mark.col,
         (long)fm->time_set);
      barline_writestring(fp, name, LSIZE - 70);
      putc('\n', fp);
   }

   if (fm->fmark.fnum != 0)
      eeglFree(name);
}

private void
write_eeglinfo_filemarks(FILE* fp) {
   int i;
   CS name;
   Book* book;
   FileMarkExt* namedfm_p = get_namedfm();
   FileMarkExt* fm;
   int vi_idx;
   int idx;

   if (get_eeglinfo_parameter('f') == 0)
      return;

   FPUTS(_("\n# File marks:\n"), fp);

   //Write the filemarks 'A - 'Z
   for (i = 0; i < NMARKS; i++) {
      if (vi_namedfm != NULL && (vi_namedfm[i].time_set > namedfm_p[i].time_set))
         fm = &vi_namedfm[i];
      else
         fm = &namedfm_p[i];
      write_one_filemark(fp, fm, '\'', i + 'A');
   }

   //Find a mark that is the same file and position as the cursor.
   //That one, or else the last one is deleted.
   //Move '0 to '1, '1 to '2, etc. until the matching one or '9
   //Set the '0 mark to current cursor position.
   if (curBook->fullFileName != NULL && !skip_for_eeglinfo(curBook)) {
      name = bookGetNameByBookNr(curBook->fiNum, true, false);
      for (i = NMARKS; i < NMARKS + EXTRA_MARKS - 1; ++i)
          if (namedfm_p[i].fmark.mark.lnum == curPor->cursor.lnum
             && (namedfm_p[i].fname == NULL
                ? namedfm_p[i].fmark.fnum == curBook->fiNum
                : (name != NULL
                   && STRCMP(name, namedfm_p[i].fname) == 0)))
         break;
      eeglFree(name);

      eeglFree(namedfm_p[i].fname);
      for ( ; i > NMARKS; --i)
         namedfm_p[i] = namedfm_p[i - 1];
      namedfm_p[NMARKS].fmark.mark = curPor->cursor;
      namedfm_p[NMARKS].fmark.fnum = curBook->fiNum;
      namedfm_p[NMARKS].fname = NULL;
      namedfm_p[NMARKS].time_set = eeTime();
   }

   //Write the filemarks '0 - '9.  Newest (highest timestamp) first.
   vi_idx = NMARKS;
   idx = NMARKS;
   for (i = NMARKS; i < NMARKS + EXTRA_MARKS; i++) {
      FileMarkExt *vi_fm = vi_namedfm != NULL ? &vi_namedfm[vi_idx] : NULL;

      if (vi_fm
         && vi_fm->fmark.mark.lnum != 0
         && (vi_fm->time_set > namedfm_p[idx].time_set || namedfm_p[idx].fmark.mark.lnum == 0)
      ){
         fm = vi_fm;
         ++vi_idx;
      } else {
         fm = &namedfm_p[idx++];
         if (vi_fm
              && vi_fm->fmark.mark.lnum == fm->fmark.mark.lnum
              && vi_fm->time_set == fm->time_set
              && ((vi_fm->fmark.fnum != 0
                 && vi_fm->fmark.fnum == fm->fmark.fnum)
                  || (vi_fm->fname && fm->fname && STRCMP(vi_fm->fname, fm->fname) == 0))
         )
            ++vi_idx;  //skip duplicate
      }
      write_one_filemark(fp, fm, '\'', i - NMARKS + '0');
   }

   //Write the jumplist with -'
   FPUTS(_("\n# Jumplist (newest first):\n"), fp);
   setpcmark();   //add current cursor position
   cleanup_jumplist(curPor, false);
   vi_idx = 0;
   idx = curPor->jumpListLen - 1;
   for (i = 0; i < JUMPLISTSIZE; ++i) {
      fm = idx >= 0 ? &curPor->jumpList[idx] : NULL;
      FileMarkExt* vi_fm = (vi_jumplist != NULL && vi_idx < vi_jumplist_len)
                  ? &vi_jumplist[vi_idx] : NULL;
      if (fm == NULL && vi_fm == NULL)
         break;
      if (fm == NULL || (vi_fm != NULL && fm->time_set < vi_fm->time_set)) {
         fm = vi_fm;
         ++vi_idx;
      } else
         --idx;
      if (fm->fmark.fnum == 0
            || ((book = bookFindFileByBookNr(fm->fmark.fnum)) != NULL && !skip_for_eeglinfo(book)))
          write_one_filemark(fp, fm, '-', '\'');
   }
}

//Handle marks in the eeglinfo file:
//fp_out != NULL: copy marks, in time order with books in "booklist".
//fp_out == NULL && (flags & EIF_WANT_MARKS): read marks for curBook
//fp_out == NULL && (flags & EIF_ONLY_CURBOOK): bail out after curBook marks
//fp_out == NULL && (flags & EIF_GET_OLDFILES | EIF_FORCEIT): fill v:oldfiles
private void
copy_eeglinfo_marks(
   Vir* virp,
   FILE* fp_out,
   ArrayList* buflist,
   int eof,
   int flags
){
   CS line = virp->line;
   Book* book;
   int num_marked_files;
   int load_marks;
   int copy_marks_out;
   CS str;
   int i;
   Byte   *p;
   Pos   pos;
   List   *list = NULL;
   int      count = 0;
   int      buflist_used = 0;
   Book* buflist_buf = NULL;

   CS name_buf = alloc(LSIZE);
   *name_buf = ZERO;

   if (fp_out && buflist->len > 0) {
      //Sort the list of books on lastUsed.
      qsort(buflist->c, (Unt)buflist->len, sizeof(Book *), bookCompare);
      buflist_buf = ((Book **)buflist->c)[0];
   }

   if (fp_out == NULL && (flags & (EIF_GET_OLDFILES | EIF_FORCEIT))) {
      list = list_alloc();
   }

   num_marked_files = get_eeglinfo_parameter('\'');
   while (!eof && (count < num_marked_files || fp_out == NULL)) {
      if (line[0] != '>') {
         if (line[0] != '\n' && line[0] != '\r' && line[0] != '#'
            && eeglinfo_error(S"E576: ", _(e_nonr_missing_gt), line)
         )
            break;   //too many errors, return now
         eof = eeFgets(line, LSIZE, virp->vir_fd);
         continue;      //Skip this dud line
      }

      //Handle long line and translate escaped characters.
      //Find file name, set str to start. Ignore leading and trailing white space.
      str = skipwhite(line + 1);
      str = eeglinfo_readstring(virp, (int)(str - virp->line));
      if (str == NULL)
         continue;
      p = str + STRLEN(str);
      while (p != str && (*p == ZERO || isSpace(*p)))
         p--;
      if (*p)
         p++;
      *p = ZERO;

      if (list)
         list_append_string(list, str, -1);

      //If fp_out == NULL, load marks for current book.
      //If fp_out != NULL, copy marks for books not in booklist.
      load_marks = copy_marks_out = false;
      if (fp_out == NULL) {
         if ((flags & EIF_WANT_MARKS) && curBook->fullFileName != NULL) {
            if (*name_buf == ZERO)       //only need to do this once
               home_replace(curBook->fullFileName, name_buf, LSIZE, true);
            if (fnamecmp(str, name_buf) == 0)
               load_marks = true;
         }
      } else { //fp_out != NULL
         //This is slow if there are many books!!
         FOR_ALL_BOOKS(book) {
            if (book->fullFileName) {
               home_replace(book->fullFileName, name_buf, LSIZE, true);
               if (fnamecmp(str, name_buf) == 0)
                  break;
            }
         } 

         //Copy marks if the book has not been loaded.
         if (book == NULL || !book->haveReadEeglinfoMarks) {
            int   did_read_line = false;

            if (buflist_buf) {
               //Read the next line.  If it has the "*" mark compare the
               //time stamps.  Write entries from "buflist" that are newer.
               if (!eeglinfo_readline(virp) && line[0] == TAB) {
                  did_read_line = true;
                  if (line[1] == '*') {
                     long   ltime;
                     sscanf((char *)line + 2, "%ld ", OUT &ltime);
                     while ((Tyme)ltime < buflist_buf->lastUsed) {
                        writeBookMarks(buflist_buf, fp_out);
                        if (++count >= num_marked_files)
                           break;
                        if (++buflist_used == buflist->len) {
                           buflist_buf = NULL;
                           break;
                        }
                        buflist_buf = ((Book **)buflist->c)[buflist_used];
                     }
                  } else {
                     //No timestamp, must be written by an older Eegl.
                     //Assume all remaining books are older than ours.
                     while (count < num_marked_files && buflist_used < buflist->len) {
                        buflist_buf = ((Book **)buflist->c)[buflist_used++];
                        writeBookMarks(buflist_buf, fp_out);
                        ++count;
                     }
                     buflist_buf = NULL;
                  }

                  if (count >= num_marked_files) {
                      eeglFree(str);
                      break;
                  }
               }
            }

            fputs("\n> ", fp_out);
            eeglinfo_writestring(fp_out, str);
            if (did_read_line)
               FPUTS(line, fp_out);

            count++;
            copy_marks_out = true;
          }
      }
      eeglFree(str);

      pos.coladd = 0;
      while (!(eof = eeglinfo_readline(virp)) && line[0] == TAB) {
         if (load_marks) {
            if (line[1] != ZERO) {
               unsigned u;

               sscanf((char *)line + 2, FMT_INT " %u", &pos.lnum, &u);
               pos.col = u;
               switch (line[1]) {
               case '"': curBook->lastCursor = pos; break;
               case '^': curBook->lastInsert = pos; break;
               case '.': curBook->lastChange = pos; break;
               case '+':
                    //changelist positions are stored oldest
                    //first
                    if (curBook->changeListLen == JUMPLISTSIZE)
                        //list is full, remove oldest entry
                        MEMMOVE(curBook->changeList,
                         curBook->changeList + 1,
                         sizeof(Pos) * (JUMPLISTSIZE - 1));
                    else
                        ++curBook->changeListLen;
                    curBook->changeList[curBook->changeListLen - 1] = pos;
                    break;

                    //Using the line number for the last-used timestamp.
               case '*': curBook->lastUsed = pos.lnum; break;

               default:  
                  if ((i = line[1] - 'a') >= 0 && i < NMARKS)
                     curBook->namedMarks[i] = pos;
               }
            }
         } ei (copy_marks_out)
            FPUTS(line, fp_out);
      }

      if (load_marks) {
         Portal   *wp;
         FOR_ALL_PORTALS(wp) {
            if (wp->book == curBook)
                wp->changeListInd = curBook->changeListLen;
         }
         if (flags & EIF_ONLY_CURBOOK)
            break;
      }
   }

   if (fp_out) {
      //Write any remaining entries from buflist.
      while (count < num_marked_files && buflist_used < buflist->len) {
          buflist_buf = ((Book **)buflist->c)[buflist_used++];
          writeBookMarks(buflist_buf, fp_out);
          ++count;
      }
   } 

   eeglFree(name_buf);
}

//Read marks for the current book from the eeglinfo file, when we support
//book marks and the book has a name.
pub void
check_marks_read(void) {
   if (!curBook->haveReadEeglinfoMarks && get_eeglinfo_parameter('\'') > 0 && curBook->fullFileName)
      read_eeglinfo(NULL, EIF_WANT_MARKS | EIF_ONLY_CURBOOK);

   //Always set haveReadEeglinfoMarks; needed when 'eeglinfo' is changed to include
   //the ' parameter after opening a book.
   curBook->haveReadEeglinfoMarks = true;
}

private int
read_eeglinfo_filemark(Vir *virp, int force) {
   FileMarkExt* namedfm_p = get_namedfm();
   FileMarkExt* fm;
   int i;

   //We only get here if line[0] == '\'' or '-'.
   //Illegal mark names are ignored (for future expansion).
   CS str = virp->line + 1;
   if (*str <= 127
       && ((*virp->line == '\'' && (EE_ISDIGIT(*str) || SAFE_isupper(*str)))
        || (*virp->line == '-' && *str == '\''))
   ){
      if (*str == '\'') {
         //If the jumplist isn't full insert fmark as oldest entry
         if (curPor->jumpListLen == JUMPLISTSIZE)
            fm = NULL;
         else {
            for (i = curPor->jumpListLen; i > 0; --i)
                curPor->jumpList[i] = curPor->jumpList[i - 1];
            ++curPor->jumpListInd;
            ++curPor->jumpListLen;
            fm = &curPor->jumpList[0];
            fm->fmark.mark.lnum = 0;
            fm->fname = NULL;
         }
      } ei (EE_ISDIGIT(*str))
         fm = &namedfm_p[*str - '0' + NMARKS];
      else
         fm = &namedfm_p[*str - 'A'];
      if (fm && (fm->fmark.mark.lnum == 0 || force)) {
         str = skipwhite(str + 1);
         fm->fmark.mark.lnum = parseLong(&str);
         str = skipwhite(str);
         fm->fmark.mark.col = parseLong(&str);
         fm->fmark.mark.coladd = 0;
         fm->fmark.fnum = 0;
         str = skipwhite(str);
         eeglFree(fm->fname);
         fm->fname = eeglinfo_readstring(virp, (int)(str - virp->line));
         fm->time_set = 0;
      }
   }
   return eeFgets(virp->line, LSIZE, virp->vir_fd);
}

//Prepare for reading eeglinfo marks when writing eeglinfo later.
private void
prepare_eeglinfo_marks(void) {
   vi_namedfm = ALLOC_CLEAR_MULT(FileMarkExt, NMARKS + EXTRA_MARKS);
   vi_jumplist = ALLOC_CLEAR_MULT(FileMarkExt, JUMPLISTSIZE);
   vi_jumplist_len = 0;
}

private void
finish_eeglinfo_marks(void) {
   if (vi_namedfm) {
      for (int i = 0; i < NMARKS + EXTRA_MARKS; ++i)
         eeglFree(vi_namedfm[i].fname);
      EE_CLEAR(vi_namedfm);
   }
   if (vi_jumplist != NULL) {
      for (int i = 0; i < vi_jumplist_len; ++i)
         eeglFree(vi_jumplist[i].fname);
      EE_CLEAR(vi_jumplist);
   }
}

//Accept a new style mark line from the eeglinfo, store it when it's new.
private void
handle_eeglinfo_mark(ArrayList *values, int force) {
   BVal* vp = (BVal *)values->c;

   //Check the format:
   //|{bartype},{name},{lnum},{col},{timestamp},{filename}
   if (values->len < 5
         || vp[0].btag != BVAL_NR
         || vp[1].btag != BVAL_NR
         || vp[2].btag != BVAL_NR
         || vp[3].btag != BVAL_NR
         || vp[4].btag != BVAL_STRING)
      return;

   int name = vp[0].bv_nr;
   if (name != '\'' && !EE_ISDIGIT(name) && !ASCII_ISUPPER(name))
      return;
   LineNr lnum = vp[1].bv_nr;
   ColNr col = vp[2].bv_nr;
   if (lnum <= 0 || col < 0)
      return;
   Tyme timestamp = (time_t)vp[3].bv_nr;

   FileMarkExt* fm = NULL;
   if (name == '\'') {
      if (vi_jumplist) {
         if (vi_jumplist_len < JUMPLISTSIZE)
            fm = &vi_jumplist[vi_jumplist_len++];
      } else {
         int idx;
         int i;

         //If we have a timestamp insert it in the right place.
         if (timestamp != 0) {
            for (idx = curPor->jumpListLen - 1; idx >= 0; --idx)
               if (curPor->jumpList[idx].time_set < timestamp) {
                  ++idx;
                  break;
               }
            //idx cannot be zero now
            if (idx < 0 && curPor->jumpListLen < JUMPLISTSIZE)
               //insert as the oldest entry
               idx = 0;
         } ei (curPor->jumpListLen < JUMPLISTSIZE)
            //insert as oldest entry
            idx = 0;
         else
            idx = -1;

         if (idx >= 0) {
            if (curPor->jumpListLen == JUMPLISTSIZE) {
               //Drop the oldest entry.
               --idx;
               eeglFree(curPor->jumpList[0].fname);
               for (i = 0; i < idx; ++i)
                  curPor->jumpList[i] = curPor->jumpList[i + 1];
            } else {
               //Move newer entries forward.
               for (i = curPor->jumpListLen; i > idx; --i)
                  curPor->jumpList[i] = curPor->jumpList[i - 1];
               ++curPor->jumpListInd;
               ++curPor->jumpListLen;
            }
            fm = &curPor->jumpList[idx];
            fm->fmark.mark.lnum = 0;
            fm->fname = NULL;
            fm->time_set = 0;
         }
      }
   } else {
      int      idx;
      FileMarkExt* namedfm_p = get_namedfm();

      if (EE_ISDIGIT(name)) {
         if (vi_namedfm)
            idx = name - '0' + NMARKS;
         else {
            int i;

            //Do not use the name from the eeglinfo file, insert in time
            //order.
            for (idx = NMARKS; idx < NMARKS + EXTRA_MARKS; ++idx)
                if (namedfm_p[idx].time_set < timestamp)
               break;
            if (idx == NMARKS + EXTRA_MARKS)
                //All existing entries are newer.
                return;
            i = NMARKS + EXTRA_MARKS - 1;

            eeglFree(namedfm_p[i].fname);
            for ( ; i > idx; --i)
                namedfm_p[i] = namedfm_p[i - 1];
            namedfm_p[idx].fname = NULL;
         }
      } else
         idx = name - 'A';
      if (vi_namedfm != NULL)
         fm = &vi_namedfm[idx];
      else
         fm = &namedfm_p[idx];
   }

   if (fm) {
      if (vi_namedfm != NULL || fm->fmark.mark.lnum == 0 || fm->time_set < timestamp || force) {
         fm->fmark.mark.lnum = lnum;
         fm->fmark.mark.col = col;
         fm->fmark.mark.coladd = 0;
         fm->fmark.fnum = 0;
         eeglFree(fm->fname);
         if (vp[4].bv_allocated) {
            fm->fname = vp[4].bv_string;
            vp[4].bv_string = NULL;
         } else
            fm->fname = copyStr(vp[4].bv_string);
         fm->time_set = timestamp;
      }
   }
}

private int
read_eeglinfo_barline(Vir* virp, Boole force, int writing) {
   CS p = virp->line + 1;
   int bartype;
   ArrayList values;
   BVal* vp;
   int i;
   int read_next = true;

   //The format is: |{bartype},{value},...
   //For a very long string:
   //    |{bartype},>{length of "{text}{text2}"}
   //    |<{text1}
   //    |<{text2},{value}
   //For a long line not using a string
   //    |{bartype},{lots of values},>
   //    |<{value},{value}
   if (*p == '<') {
      //Continuation line of an unrecognized item.
      if (writing)
         ga_copy_string(&virp->vir_barlines, virp->line);
   } else {
      ga_init2(&values, sizeof(BVal), 20);
      bartype = parseLong(&p);
      switch (bartype) {
      case BARTYPE_VERSION:
         read_next = barline_parse(virp, p, &values);
         vp = (BVal *)values.c;
         if (values.len > 0 && vp->btag == BVAL_NR)
            virp->vir_version = vp->bv_nr;
         break;

      case BARTYPE_HISTORY:
         read_next = barline_parse(virp, p, &values);
         handle_eeglinfo_history(&values, writing);
         break;

      case BARTYPE_REGISTER:
         read_next = barline_parse(virp, p, &values);
         handle_eeglinfo_register(&values, force);
         break;

      case BARTYPE_MARK:
         read_next = barline_parse(virp, p, &values);
         handle_eeglinfo_mark(&values, force);
         break;

      default:
         //copy unrecognized line (for future use)
         if (writing)
            ga_copy_string(&virp->vir_barlines, virp->line);
      }
      for (i = 0; i < values.len; ++i) {
         vp = (BVal *)values.c + i;
         if (vp->btag == BVAL_STRING && vp->bv_allocated)
            eeglFree(vp->bv_string);
         eeglFree(vp->bv_tofree);
      }
      ga_clear(&values);
   }

   if (read_next)
      return eeglinfo_readline(virp);
   return false;
}

//read_eeglinfo_up_to_marks() -- Only called from do_eeglinfo().  Reads in the
//first part of the eeglinfo file which contains everything but the marks that
//are local to a file.  Return true when end-of-file is reached. -- webb
private int
read_eeglinfo_up_to_marks(Vir* virp, Boole forceit, int writing) {

   prepare_eeglinfo_history(forceit ? 9999 : 0, writing);

   int eof = eeglinfo_readline(virp);
   while (!eof && virp->line[0] != '>') {
      switch (virp->line[0]) {
      //Characters reserved for future expansion, ignored now
      case '+': //"+40 /path/dir file", for running vim without args
      case '^': //to be defined
      case '<': //long line - ignored
      //A comment or empty line.
      case ZERO:
      case '\r':
      case '\n':
      case '#':
         eof = eeglinfo_readline(virp);
         break;
      case '|':
         eof = read_eeglinfo_barline(virp, forceit, writing);
         break;
      case '!': //global variable
         eof = read_eeglinfo_varlist(virp, writing);
         break;
      case '%': //entry for book list
         eof = readEeglinfoBookList(virp, writing);
         break;
      case '"':
         //When registers are in bar lines skip the old style register lines.
         if (virp->vir_version < EEGLINFO_VERSION_WITH_REGISTERS)
            eof = read_eeglinfo_register(virp, forceit);
         else
            do {
               eof = eeglinfo_readline(virp);
            } while (!eof && (virp->line[0] == TAB || virp->line[0] == '<'));
         break;
      case '/':       //Search string
      case '&':       //Substitute search string
      case '~':       //Last search string, followed by '/' or '&'
         eof = read_eeglinfo_search_pattern(virp, forceit);
         break;
      case '$':
         eof = read_eeglinfo_sub_string(virp, forceit);
         break;
      case ':':
      case '?':
      case '=':
      case '@':
         //When history is in bar lines skip the old style history lines.
         if (virp->vir_version < EEGLINFO_VERSION_WITH_HISTORY)
            eof = read_eeglinfo_history(virp, writing);
         else
            eof = eeglinfo_readline(virp);
         break;
      case '-':
      case '\'':
         //When file marks are in bar lines skip the old style lines.
         if (virp->vir_version < EEGLINFO_VERSION_WITH_MARKS)
            eof = read_eeglinfo_filemark(virp, forceit);
         else
            eof = eeglinfo_readline(virp);
         break;
      default:
         if (eeglinfo_error(S"E575: ", _(e_illegal_starting_char), virp->line))
            eof = true;
         else
            eof = eeglinfo_readline(virp);
         break;
      }
   }

   //Finish reading history items.
   if (!writing)
      finish_eeglinfo_history(virp);

   //Change file names to book numbers for fmarks.
   Book* book;
   FOR_ALL_BOOKS(book) {
      fmarks_check_names(book);
   } 

   return eof;
}

//do_eeglinfo() -- Should only be called from read_eeglinfo() & write_eeglinfo().
private void
do_eeglinfo(FILE* fp_in, FILE* fp_out, Unt flags) {
   int eof = false;
   int merge = false;
   int do_copy_marks = false;
   ArrayList   buflist;

   Vir  vir;
   vir.line = alloc(LSIZE);
   vir.vir_fd = fp_in;
   ga_init2(&vir.vir_barlines, sizeof(CS), 100);
   vir.vir_version = -1;

   if (fp_in) {
      if (flags & EIF_WANT_INFO) {
         if (fp_out) {
            //Registers and marks are read and kept separate from what this Eegl is using. 
            //They are merged when writing.
            prepare_eeglinfo_registers();
            prepare_eeglinfo_marks();
         }

         eof = read_eeglinfo_up_to_marks(&vir, flags & EIF_FORCEIT, fp_out != NULL);
         merge = true;
      } ei (flags != 0)
         //Skip info, find start of marks
         while (!(eof = eeglinfo_readline(&vir)) && vir.line[0] != '>')
            {}

      do_copy_marks = (flags & (EIF_WANT_MARKS | EIF_ONLY_CURBOOK | EIF_GET_OLDFILES | EIF_FORCEIT));
   }

   if (fp_out != NULL) {
      //Write the info:
      fprintf(fp_out, (char*)_("# This eeglinfo file was generated by Eegl %s.\n"),
                          EEGL_VERSION_MEDIUM);
      FPUTS(_("# You may edit it if you're careful!\n\n"), fp_out);
      write_eeglinfo_version(fp_out);
      write_eeglinfo_search_pattern(fp_out);
      write_eeglinfo_sub_string(fp_out);
      write_eeglinfo_history(fp_out, merge);
      write_eeglinfo_registers(fp_out);
      finish_eeglinfo_registers();
      write_eeglinfo_varlist(fp_out);
      write_eeglinfo_filemarks(fp_out);
      finish_eeglinfo_marks();
      writeEeglInfoBookList(fp_out);
      write_eeglinfo_barlines(&vir, fp_out);

      if (do_copy_marks)
         ga_init2(&buflist, sizeof(Book *), 50);
      write_eeglinfo_marks(fp_out, do_copy_marks ? &buflist : NULL);
   }

   if (do_copy_marks) {
      copy_eeglinfo_marks(&vir, fp_out, &buflist, eof, flags);
      if (fp_out)
         ga_clear(&buflist);
   }

   eeglFree(vir.line);
   ga_clear_strings(&vir.vir_barlines);
}

//read_eeglinfo() -- Read the eeglinfo file.  Registers etc. which are already
//set are not over-written unless "flags" includes EIF_FORCEIT. -- webb
pub int
read_eeglinfo(
   CS file,       //file name or NULL to use default name
   Unt flags       //EIF_WANT_INFO et al.
){
   FileStat   st;      //stat() of existing eeglinfo file

   if (no_eeglinfo())
      return FAIL;

   CS fname = eeglinfo_filename(file);   //get file name in allocated book
   if (!fname)
      return FAIL;
   FILE* fp = FOPEN(fname, READBIN);

   if (p_verbose > 0) {
      verbose_enter();
      smsg(_("Reading eeglinfo file \"%s\"%s%s%s%s"),
         fname,
         (flags & EIF_WANT_INFO) ? _(" info") : S"",
         (flags & EIF_WANT_MARKS) ? _(" marks") : S"",
         (flags & EIF_GET_OLDFILES) ? _(" oldfiles") : S"",
         fp == NULL ? _(" FAILED") : S"");
      verbose_leave();
   }

   eeglFree(fname);
   if (fp == NULL)
      return FAIL;
   if (fstat(fileno(fp), &st) < 0 || S_ISDIR(st.st_mode)) {
      fclose(fp);
      return FAIL;
   }

   eeglinfo_errcnt = 0;
   do_eeglinfo(fp, NULL, flags);

   fclose(fp);
   return OK;
}

//Write the eeglinfo file. The old one is read in first so that effectively a
//merge of current info and old info is done. This allows multiple vims to
//run simultaneously, without losing any marks etc. If "forceit" is true, then the old file is not
//read in, and only internal info is written to the file.
pub void
write_eeglinfo(CS file, Boole forceit) {
   FILE* fp_out = NULL;   //output eeglinfo file
   CS tempname = NULL;   //name of temp eeglinfo file
   FileStat   st_new;      //stat() of potential new file
   FileStat   st_old;      //stat() of existing eeglinfo file
   mode_t   umask_save;

   if (no_eeglinfo())
      return;

   CS fname = eeglinfo_filename(file);   //may set to default if NULL
   if (!fname)
      return;

   FILE* fp_in = fopen((char *)fname, READBIN); //input eeglinfo file, if any
   if (!fp_in) {
      //if it does exist, but we can't read it, don't try writing
      if (stat((char *)fname, &st_new) == 0)
         goto end;

      //Create the new .eeglinfo non-accessible for others, because it may
      //contain text from non-accessible documents. It is up to the user to
      //widen access (e.g. to a group). This may also fail if there is a
      //race condition, then just give up.
      int fd = open((char *)fname, O_CREAT|O_EXTRA|O_EXCL|O_WRONLY|O_NOFOLLOW, 0600);
      if (fd < 0)
         goto end;
      fp_out = fdopen(fd, WRITEBIN);
   } else {
      //There is an existing eeglinfo file.  Create a temporary file to
      //write the new eeglinfo into, in the same directory as the
      //existing eeglinfo file, which will be renamed once all writing is successful.
      if (fstat(fileno(fp_in), &st_old) < 0
         || S_ISDIR(st_old.st_mode)
         //We check the owner of the file. It's not very nice to overwrite a user's eeglinfo file 
         //after a "su root", with a eeglinfo file that the user can't read.
         || (getuid() != ROOT_UID
             && !(st_old.st_uid == getuid()
                ? (st_old.st_mode & 0200)
                : (st_old.st_gid == getgid()
                   ? (st_old.st_mode & 0020)
                   : (st_old.st_mode & 0002))))
      ) {
          int   tt = msg_didany;

          //avoid a wait_return() for this message, it's annoying
          showErrFmtMsg(_(e_eeglinfo_file_is_not_writable_str), fname);
          msg_didany = tt;
          fclose(fp_in);
          goto end;
      }

      //Make tempname, find one that does not exist yet. Beware of a race condition: If someone 
      //logs out and all Eegl instances exit at the same time a temp file might be created between
      //stat() and open(). Use open() with O_EXCL to avoid that.
      for (;;) {
         int next_char = 'z';

         tempname = fiAppendFileExtension(fname, S".tmp", false);
         if (!tempname)      //out of memory
            break;

         //Try a series of names. Change one character, just before the extension. 
         CS wp = tempname + STRLEN(tempname) - 5;
         if (wp < fiGetShortFiName(tempname))       //empty file name?
            wp = fiGetShortFiName(tempname);
         for (;;) {
            //Check if tempfile already exists.  Never overwrite an existing file!
            if (stat((char *)tempname, &st_new) == 0) {
               //Check if tempfile is same as original file. May happen when fiAppendFileExtension() gave the 
               //same file back.  E.g.  silly link, or file name-length reached. 
               if (st_new.st_dev == st_old.st_dev && st_new.st_ino == st_old.st_ino) {
                  EE_CLEAR(tempname);
                  break;
               }
            } else {
               //Try creating the file exclusively. This may fail if another Eegl tries to do it 
               //at the same time.

               //Use open() to be able to use O_NOFOLLOW and set file protection:
               //Unix: same as original file, but strip s-bit. Reset umask to avoid it getting in
               //the way. Others: r&w for user only.
               umask_save = umask(0);
               int fd = open((char *)tempname, O_CREAT|O_EXTRA|O_EXCL|O_WRONLY|O_NOFOLLOW,
                    (int)((st_old.st_mode & 0777) | 0600));
               (void)umask(umask_save);
               if (fd < 0) {
                  fp_out = NULL;
                  //Avoid trying lots of names while the problem is lack
                  //of permission, only retry if the file already exists.
                  if (errno != EEXIST)
                     break;
               } else
                  fp_out = fdopen(fd, WRITEBIN);
               if (fp_out)
                  break;
            }

            //Assume file exists, try again with another name.
            if (next_char == 'a' - 1) {
               //They all exist?  Must be something wrong! Don't write the eeglinfo file then.
               showErrFmtMsg(_(e_too_many_eeglinfo_temp_files_like_str), tempname);
               break;
            }
            *wp = next_char;
            --next_char;
         }

         if (tempname)
            break;
      }

      if (tempname && fp_out) {
         FileStat   tmp_st;

         //Make sure the original owner can read/write the tempfile and
         //otherwise preserve permissions, making sure the group matches.
         if (stat((char *)tempname, &tmp_st) >= 0) {
            if (st_old.st_uid != tmp_st.st_uid)
               //Changing the owner might fail, in which case the
               //file will now be owned by the current user, oh well.
               (void)fchown(fileno(fp_out), st_old.st_uid, -1);
            if (st_old.st_gid != tmp_st.st_gid && fchown(fileno(fp_out), -1, st_old.st_gid) == -1)
               //can't set the group to what it should be, remove group permissions
               (void)mch_setperm(tempname, 0600);
         } else
            //can't stat the file, set conservative permissions
            (void)mch_setperm(tempname, 0600);
      }
   }

   //Check if the new eeglinfo file can be written to.
   if (!fp_out) {
      showErrFmtMsg(_(e_cant_write_eeglinfo_file_str),
                (fp_in == NULL || tempname == NULL) ? fname : tempname);
      if (fp_in)
         fclose(fp_in);
      goto end;
   }

   if (p_verbose > 0) {
      verbose_enter();
      smsg(_("Writing eeglinfo file \"%s\""), fname);
      verbose_leave();
   }

   eeglinfo_errcnt = 0;
   do_eeglinfo(fp_in, fp_out, forceit ? 0 : (EIF_WANT_INFO | EIF_WANT_MARKS));

   if (fclose(fp_out) == EOF)
      ++eeglinfo_errcnt;

   if (fp_in) {
      fclose(fp_in);

      //In case of an error keep the original eeglinfo file.  Otherwise
      //rename the newly written file.  Give an error if that fails.
      if (eeglinfo_errcnt == 0) {
         if (eeRename(tempname, fname) == -1) {
            ++eeglinfo_errcnt;
            showErrFmtMsg(_(e_cant_rename_eeglinfo_file_to_str), fname);
         }
      }
      if (eeglinfo_errcnt > 0)
          mch_remove(tempname);
   }

end:
   eeglFree(fname);
   eeglFree(tempname);
}

//}}}
//}}}
