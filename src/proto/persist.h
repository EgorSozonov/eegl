CS get_users(Expand *xp UNUSED, int idx);
int match_user(CS name);
void free_homedir(void);
void free_users(void);
int get_user_name(CS builder, int len);
int write_session_file(CS filename);
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
