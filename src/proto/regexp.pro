/* src/regexp.c */
void init_regexp_timeout(long msec);
void disable_regexp_timeout(void);
int re_multiline(RegProg *prog);
Byte *skip_regexp(Byte *startp, int delim, int magic);
Byte *skip_regexp_err(Byte *startp, int delim, int magic);
Byte *skip_regexp_ex(Byte *startp, int dirc, int magic, Byte **newp, int *dropped, Magic *magic_val);
RegExternalMatch *ref_extmatch(RegExternalMatch *em);
void unref_extmatch(RegExternalMatch *em);
CS regtilde(CS source);
int eeRegsub(RegMatch *rmp, Byte *source, Var *expr, Byte *dest, int destlen, int flags);
int eeRegsub_multi(RegMultilineMatch *rmp, LineNr lnum, Byte *source, Byte *dest, int destlen, int flags);
void free_resub_eval_result(void);
Byte *reg_submatch(int no);
List *reg_submatch_list(int no);
void save_timeout_for_debugging(void);
void restore_timeout_for_debugging(void);
RegProg *compileRegexp(CS expr_arg, Unt flags);
Boole regexContainsEol(RegProg *prog);
void eeRegFree(RegProg *prog);
void free_regexp_stuff(void);
Boole eeRegexec_prog(RegProg **prog, Boole ignore_case, CS line, ColNr col);
Boole eeRegexec(RegMatch *rmp, Byte *line, ColNr col);
int eeRegexec_nl(RegMatch *rmp, Byte *line, ColNr col);
long eeRegexec_multi(RegMultilineMatch *rmp, Portal *port, Book *book, LineNr lnum, ColNr col, int *timed_out);
/* eegl: set ft=c : */
