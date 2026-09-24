#define MAX_ARG_CMDS 10
typedef struct {
   int argc;
   Arr(Arr(char)) argv;

   CS fname;         // first file to edit

   CS altInitFile;      // alternative init file name from -u argument
   int clean;         // --clean argument

   int n_commands;                 //no. of commands from + or -c
   CS commands[MAX_ARG_CMDS];      //commands from + or -c arg.
   Byte cmds_tofree[MAX_ARG_CMDS]; //commands that need free()
   int n_pre_commands;             //no. of commands from --cmd
   CS pre_commands[MAX_ARG_CMDS];  //commands from --cmd argument

   int edit_type; //type of editing to do
   CS tagname;    //tag from -t argument
   CS use_ef;     //@errorfile from -q argument

   int want_full_screen;
   int not_a_term;      // no warning for missing term?
   int tty_fail;      // exit if not a tty
   CS term;         // specified terminal name
   int no_swap_file;      // "-n" argument used
   int use_debug_break_level;
   Unt portalCount;      // number of portals to use
   int portalLayout;     // 0, WIN_HOR, WIN_VER or WIN_TABS

   int serverArg;      // TRUE when argument for a server
   CS serverName_arg;  // cmdline arg for server name
   CS serverStr;       // remote server command
   CS servername;      // allocated name for our server
   int diff_mode;      // start with 'diff' set
} MainParams;
