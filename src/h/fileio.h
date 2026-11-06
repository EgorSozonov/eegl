#ifndef AVOID_DUPLICATION
ssize_t listxattr(const char*, char*, size_t);
int setxattr(const char*, const char*, const void*, size_t, int);
#define BAD_REPLACE   '?'   //replace it with '?' (default)
#define BAD_KEEP    1000   //leave it
#define BAD_DROP    1002   //erase it
declStruct(VisitedList);
declStruct (DirSearchStack);
#define FINDFILE_FILE  0  //only files
#define FINDFILE_DIR   1  //only directories
#define FINDFILE_BOTH  2  //files and directories
extern S eeTempDirG;
#endif

void init_homedir(void);
int file_is_readable(S fname);
void f_chdir(Var* argvars, Var* returnVar);
void f_delete(Var* argvars, Var* returnVar);
void f_executable(Arr(Var) argvars, Var* returnVar);
void f_exepath(Arr(Var) argvars, Var* returnVar);
void f_filereadable(Arr(Var) argvars, Var* returnVar);
void f_filewritable(Arr(Var) argvars, Var* returnVar);
void f_finddir(Arr(Var) argvars, Var* returnVar);
void f_findfile(Arr(Var) argvars, Var* returnVar);
void f_fnamemodify(Arr(Var) argvars, Var* returnVar);
void f_getcwd(Arr(Var) argvars, Var* returnVar);
S getfpermst(FileStat *st, S perm);
void f_getfperm(Arr(Var) argvars, Var* returnVar);
void f_getfsize(Arr(Var) argvars, Var* returnVar);
void f_getftime(Arr(Var) argvars, Var* returnVar);
S getftypest(FileStat *st);
void f_getftype(Arr(Var) argvars, Var* returnVar);
void f_glob(Arr(Var) argvars, Var* returnVar);
void f_glob2regpat(Arr(Var) argvars, Var* returnVar);
void f_globpath(Arr(Var) argvars, Var* returnVar);
void f_isdirectory(Arr(Var) argvars, Var* returnVar);
void f_isabsolutepath(Arr(Var) argvars, Var* returnVar);
void f_mkdir(Var* argvars, Var* returnVar);
void f_pathshorten(Arr(Var) argvars, Var* returnVar);
void f_readdir(Arr(Var) argvars, Var* returnVar);
void f_readdirex(Arr(Var) argvars, Var* returnVar);
void f_readblob(Var* argvars, Var* returnVar);
void f_readfile(Var* argvars, Var* returnVar);
void f_resolve(Arr(Var) argvars, Var* returnVar);
void f_tempname(Var*, Var* returnVar);
void f_writefile(Var* argvars, Var* returnVar);
void f_browse(Arr(Var), Var* returnVar);
void f_browsedir(Arr(Var), Var* returnVar);
void f_filecopy(Arr(Var) argvars, Var* returnVar);
S fiExpandAndCopy(NULLABLE S fname, int force);
Boole eeFexists(S fname);
int expand_wildcards_eval(
	Arr(S) pattern,      //pointer to input pattern
	Unt flags,  //EW_DIR, etc.
	OUT ExpandMatch* files
);
int expand_wildcards(
	int num_pat, //number of input patterns
	Arr(S) pat, //array of input patterns
	Unt flags,   //EW_DIR, etc.
	OUT ExpandMatch* files
);
int gen_expand_wildcards(
	int num_pat,   //number of input patterns
	Arr(S) pat,   //array of input patterns
	Unt flags,      //EW_* flags
	OUT ExpandMatch* matches
);
void addFile(OUT ExpandMatch* matches, S fName, Unt flags);
int eeFullFileName(S fname, OUT S buf, int len, Boole force);
int same_directory(S f1, S f2);
int fullpathcmp(
	S s1,
	S s2,
	int checkname,      //when both don't exist, check file names
	int expandenv
);
int mch_dirname(S buf, int len);
S home_replace_save(Book* book, S inputFname);
S homeReplaceA(Book* book, S inputFname, Arena* a);
int modify_fname(
	S src,         //string with modifiers
	int tilde_file, //"~" is a file name, not $HOME
	Unt* usedlen,   //characters after src that are used
	OUT S* fnamep, //file name so far
	OUT S* bufp,   //buffer for allocated file name or NULL
	Unt* fnamelen   //length of fnamep
);
void home_replace(
	S src, //input file name
	S dst, //where to put the result
	int dstlen,  //maximum length of the result
	Boole one      //if true, only replace one file name, include spaces and commas in the file name.
);
FileSearchCtx* eeFindFile_init(
	S path,
	Text filename,
	S stopdirs,
	int level,
	Boole free_visited,
	Unt find_what, //FINDFILE_DIR, FINDFILE_FILE or FINDFILE_BOTH for both.
	NULLABLE OUT FileSearchCtx* search_ctx_arg,
	Boole tagfile,   //expanding names of tags files
	S rel_fname   //file name to use for "."
);
int eeChdir(S new_dir);
int eeChdirfile(S fname, char *trigger_autocmd);
S eeFindFile_stopdir(S buf);
void eeFindFile_cleanup(FileSearchCtx* ctx);
S eeFindFile(FileSearchCtx* search_ctx_arg);
S findFileInPath(
	Text fname,
	Unt  options,
	Boole first,      //use count'th matching file name
	S rel_fname,   //file name searching relative to
	OUT Byte** file_to_find,   //modified copy of file name
	OUT FileSearchCtx** searchCtx   //state of the search
);
void free_findfile(void);
S grab_file_name(Long count, OUT LineNr* file_lnum);
S file_name_at_cursor(int options, Long count, OUT LineNr* file_lnum);
S file_name_in_line(
	S line,
	int col,
	int options,
	Long count,
	S rel_fname,   //file we are searching relative to
	OUT LineNr* file_lnum   //line number after the file name
);
S find_file_name_in_path(
	S ptr,
	int len,
	Unt options,
	Long count,
	S rel_fname   //file we are searching relative to
);
Unt simplify_filename(S filename);
int mch_has_exp_wildcard(S p);
int mch_has_wildcard(S p);
void f_simplify(Var* argvars, Var* returnVar);
void fiGlobpath(
	S path,
	S file,
	OUT ExpandMatch* matches,
	Unt expand_options,
	Boole onlyDirs
);
int mch_chdir(S path);
void filemess(Book* book, S name, S s, int attr);
int readfile(
	S fname,
	S sfname,
	LineNr from,
	LineNr lines_to_skip,
	LineNr lines_to_read,
	Invocation* invo,         //can be NULL!
	Unt flags
);
int read_blob(FILE* fd, Var* returnVar, FileOffset offset, FileOffset size_arg);
int write_blob(FILE* fd, Blob* blob);
int prep_exarg(Invocation* invo, Book* book);
void set_file_options(Invocation* invo);
int check_file_readonly(S fname, Unt perm);
int eeFsync(int fd);
int set_rw_fname(S fname, S sfname);
void msg_add_fname(Book* book, S fname);
void msg_add_lines(int insert_space, Long lnum, FileOffset nchars);
void msg_add_eol(void);
int time_differs(FileStat* st, Long mtime, Long mtime_ns);
S shorten_fname1(S full_path);
S shorten_fname(S full_path, S dir_name);
S fiAppendFileExtension(S fname, S ext, Boole prepend_dot);
int eeFgets(S buf, int size, FILE *fp);
int eeRename(S from, S to);
int check_timestamps(int focus);
void buf_reload(Book* book, int orig_mode, int reload_options);
void buf_store_time(Book* book, FileStat* st, S);
void write_lnum_adjust(LineNr offset);
int mch_isrealdir(S name);
Boole mch_isdir(S name);
int mch_nodetype(S name);
void eeDelTempDir(void);
S eeTempName(int, int);
int match_file_pat(
	S pattern,      //pattern to match with
	RegProg** prog,         //pre-compiled regprog or NULL
	S fname,         //full path of file name
	S sfname,      //short file name or NULL
	S tail,         //tail of path
	Boole allow_dirs      //allow matching with dir
);
int match_file_list(S list, S sfname, S ffname);
S file_pat_to_reg_pat(
	S pat,
	S pat_end,   //first char after pattern or NULL
	OUT Boole* allow_dirs   //Result passed back out in here
);
Long fiReadEintr(int fd, OUT void* buf, Unt bufsize);
Long write_eintr(int fd, void *buf, Unt bufsize);
S fiGetShellOutput(
	S cmd,
	NULLABLE S infile, //optional input file name
	Unt flags,          //can be SHELL_SILENT
	OUT int* ret_len
);
void f_system(Var* argvars, Var* returnVar);
void f_systemlist(Var* argvars, Var* returnVar);
Text fiInitSwapDir(S progName);
int filewritable(S fname);
int get2c(FILE* fd);
int get3c(FILE* fd);
int get4c(FILE* fd);
S read_string(FILE* fd, int cnt);
Long mch_getperm(S name);
int mch_setperm(S name, Long perm);
void mch_copy_xattr(S from_file, S to_file);
int mch_fsetperm(int fd, Long perm);
int mch_can_exe(S name, Arr(S) path, int use_path);
S fiBuildSwapOrUndoFname(S fname, Boole isUndo);
