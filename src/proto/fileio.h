ssize_t listxattr(const char*, char*, size_t);
int setxattr(const char*, const char*, const void*, size_t, int);
void init_homedir(void);
int file_is_readable(CS fname);
void f_chdir(Var* argvars, Var* returnVar);
void f_delete(Var* argvars, Var* returnVar);
void f_executable(Var *argvars, Var* returnVar);
void f_exepath(Var *argvars, Var* returnVar);
void f_filereadable(Var *argvars, Var* returnVar);
void f_filewritable(Var *argvars, Var* returnVar);
void f_finddir(Var *argvars, Var* returnVar);
void f_findfile(Var *argvars, Var* returnVar);
void f_fnamemodify(Var *argvars, Var* returnVar);
void f_getcwd(Var *argvars, Var* returnVar);
CS getfpermst(FileStat *st, CS perm);
void f_getfperm(Var *argvars, Var* returnVar);
void f_getfsize(Var *argvars, Var* returnVar);
void f_getftime(Var *argvars, Var* returnVar);
CS getftypest(FileStat *st);
void f_getftype(Var *argvars, Var* returnVar);
void f_glob(Var *argvars, Var* returnVar);
void f_glob2regpat(Var *argvars, Var* returnVar);
void f_globpath(Var *argvars, Var* returnVar);
void f_isdirectory(Var *argvars, Var* returnVar);
void f_isabsolutepath(Var *argvars, Var* returnVar);
void f_mkdir(Var* argvars, Var* returnVar);
void f_pathshorten(Var *argvars, Var* returnVar);
void f_readdir(Var *argvars, Var* returnVar);
void f_readdirex(Var *argvars, Var* returnVar);
void f_readblob(Var* argvars, Var* returnVar);
void f_readfile(Var* argvars, Var* returnVar);
void f_resolve(Var *argvars, Var* returnVar);
void f_tempname(Var *argvars UNUSED, Var* returnVar);
void f_writefile(Var* argvars, Var* returnVar);
void f_browse(Var *argvars UNUSED, Var* returnVar);
void f_browsedir(Var *argvars UNUSED, Var* returnVar);
void f_filecopy(Var *argvars, Var* returnVar);
CS fiExpandAndCopy(NULLABLE CS fname, int force);
int eeFexists(CS fname);
int expand_wildcards_eval(
   Arr(CS) pattern,      // pointer to input pattern
   Unt         flags,  // EW_DIR, etc.
   OUT ExpandMatch* files
);
int expand_wildcards(
   int num_pat, // number of input patterns
   Arr(CS) pat, // array of input patterns
   Unt flags,   // EW_DIR, etc.
   OUT ExpandMatch* files
);
int gen_expand_wildcards(
   int num_pat,   // number of input patterns
   Arr(CS) pat,   // array of input patterns
   Unt flags,      // EW_* flags
   OUT ExpandMatch* matches
);
void addFile(OUT ExpandMatch* matches, CS fName, Unt flags);
int eeFullFileName(CS fname, OUT CS buf, int len, Boole force);
int same_directory(CS f1, CS f2);
int fullpathcmp(
   CS s1,
   CS s2,
   int checkname,      // when both don't exist, check file names
   int expandenv
);
int mch_dirname(CS buf, int len);
CS home_replace_save(Book* book, CS inputFname);
CS homeReplaceA(Book* book, CS inputFname, Arena* a);
int modify_fname(
   CS src,      // string with modifiers
   int tilde_file,   // "~" is a file name, not $HOME
   Unt* usedlen,   // characters after src that are used
   OUT CS* fnamep,   // file name so far
   OUT CS* bufp,      // buffer for allocated file name or NULL
   Unt* fnamelen   // length of fnamep
);
void home_replace(
   CS src, //input file name
   CS dst, //where to put the result
   int dstlen,  //maximum length of the result
   Boole one      //if true, only replace one file name, include spaces and commas in the file name.
);
declStruct (DirSearchStack);
declStruct(VisitedList);
Byte * eeFindfirst(Byte *path, Byte *filename, int level);
CS eeFindnext(void);
FileSearchCtx* eeFindFile_init(
   CS path,
   Text filename,
   CS stopdirs,
   int level,
   Boole free_visited,
   Unt find_what, // FINDFILE_DIR, FINDFILE_FILE or FINDFILE_BOTH for both.
   NULLABLE OUT FileSearchCtx* search_ctx_arg,
   Boole tagfile,   // expanding names of tags files
   CS rel_fname   // file name to use for "."
);
int eeChdir(CS new_dir);
int eeChdirfile(CS fname, char *trigger_autocmd);
CS eeFindFile_stopdir(CS buf);
void eeFindFile_cleanup(FileSearchCtx* ctx);
CS eeFindFile(FileSearchCtx* search_ctx_arg);
CS findFileInPath(
   Text fname,
   Unt  options,
   Boole first,      // use count'th matching file name
   CS rel_fname,   // file name searching relative to
   OUT Byte** file_to_find,   // modified copy of file name
   OUT FileSearchCtx** searchCtx   // state of the search
);
void free_findfile(void);
CS grab_file_name(long count, OUT LineNr* file_lnum);
CS file_name_at_cursor(int options, long count, OUT LineNr* file_lnum);
CS file_name_in_line(
   CS line,
   int col,
   int options,
   long count,
   CS rel_fname,   // file we are searching relative to
   OUT LineNr* file_lnum   // line number after the file name
);
CS find_file_name_in_path(
   CS ptr,
   int len,
   Unt options,
   long count,
   CS rel_fname   // file we are searching relative to
);
Unt simplify_filename(CS filename);
int mch_has_exp_wildcard(CS p);
int mch_has_wildcard(CS p);
void f_simplify(Var* argvars, Var* returnVar);
void fiGlobpath(
   CS path,
   CS file,
   OUT ExpandMatch* matches,
   Unt expand_options,
   Boole onlyDirs
);
int mch_chdir(CS path);
void filemess(Book* book, CS name, CS s, int attr);
int readfile(
   CS fname,
   CS sfname,
   LineNr from,
   LineNr lines_to_skip,
   LineNr lines_to_read,
   Invocation* invo,         // can be NULL!
   Unt flags
);
int read_blob(FILE* fd, Var* returnVar, FileOffset offset, FileOffset size_arg);
int write_blob(FILE* fd, Blob* blob);
int is_dev_fd_file(CS fname);
int prep_exarg(Invocation* invo, Book* book);
void set_file_options(Invocation* invo);
int check_file_readonly(CS fname, Unt perm);
int eeFsync(int fd);
int set_rw_fname(CS fname, CS sfname);
void msg_add_fname(Book* book, CS fname);
void msg_add_lines(int insert_space, long lnum, FileOffset nchars);
void msg_add_eol(void);
int time_differs(FileStat* st, long mtime, long mtime_ns UNUSED);
CS shorten_fname1(CS full_path);
CS shorten_fname(CS full_path, CS dir_name);
void shorten_buf_fname(Book* book, CS dirname, int force);
void shorten_fnames(Boole force);
CS fiAppendFileExtension(CS fname, CS ext, Boole prepend_dot);
int eeFgets(CS buf, int size, FILE *fp);
int eeRename(CS from, CS to);
int check_timestamps( int      focus);
int fiCheckBookTimestamp(
   Book* book
);
void buf_reload(Book* book, int orig_mode, int reload_options);
void buf_store_time(Book *book, FileStat *st, CS fname UNUSED);
void write_lnum_adjust(LineNr offset);
int mch_isrealdir(CS name);
Boole mch_isdir(CS name);
int mch_nodetype(CS name);
void eeDelTempDir(void);
CS eeTempName(
   int extra_char UNUSED,  // char to use in the name instead of '?'
   int keep UNUSED
);
int match_file_pat(
   CS pattern,      // pattern to match with
   RegProg** prog,         // pre-compiled regprog or NULL
   CS fname,         // full path of file name
   CS sfname,      // short file name or NULL
   CS tail,         // tail of path
   Boole allow_dirs      // allow matching with dir
);
int match_file_list(CS list, CS sfname, CS ffname);
CS file_pat_to_reg_pat(
   CS pat,
   CS pat_end,   // first char after pattern or NULL
   OUT Boole* allow_dirs   // Result passed back out in here
);
Long fiReadEintr(int fd, OUT void* buf, Unt bufsize);
long write_eintr(int fd, void *buf, Unt bufsize);
CS fiGetShellOutput(
   CS cmd,
   NULLABLE CS infile, // optional input file name
   Unt flags,          // can be SHELL_SILENT
   OUT int* ret_len
);
void f_system(Var* argvars, Var* returnVar);
void f_systemlist(Var* argvars, Var* returnVar);
Text fiInitSwapDir(CS progName);
int filewritable(CS fname);
int get2c(FILE* fd);
int get3c(FILE* fd);
int get4c(FILE* fd);
CS read_string(FILE* fd, int cnt);
long mch_getperm(CS name);
int mch_setperm(CS name, long perm);
void mch_copy_xattr(CS from_file, CS to_file);
int mch_fsetperm(int fd, long perm);
int mch_can_exe(CS name, Arr(CS) path, int use_path);
CS fiBuildSwapOrUndoFname(CS fname, Boole isUndo);
int xxdMain(int argc, char* argv[]);
