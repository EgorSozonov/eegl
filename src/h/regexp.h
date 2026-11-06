#ifndef AVOID_DUPLICATION
#define REX_SET   1   //to allow \z\(...\),
#define REX_USE   2   //to allow \z\1 et al.
#define REX_ALL   (REX_SET | REX_USE)
#endif

void init_regexp_timeout(long msec);
void disable_regexp_timeout(void);
int re_multiline(RegProg* prog);
S skip_regexp(S startp, int delim, int magic);
S skip_regexp_err(S startp, int delim, int magic);
S skip_regexp_ex(
	S startp,
	int dirc,
	int magic,
	Byte** newp,
	int* dropped,
	Magic* magic_val
);
RegExternalMatch * ref_extmatch(RegExternalMatch *em);
void unref_extmatch(RegExternalMatch *em);
S regtilde(S source);
int eeRegsub(
	RegMatch* rmp,
	S source,
	Var* expr,
	Text dest,
	Unt flags
);
int eeRegsub_multi(
	RegMultilineMatch* rmp,
	LineNr lnum,
	S source,
	Text dest,
	Unt flags
);
void free_resub_eval_result(void);
Byte * reg_submatch(int no);
List * reg_submatch_list(int no);
void save_timeout_for_debugging(void);
void restore_timeout_for_debugging(void);
RegProg * compileRegexp(S expr_arg, Unt flags);
Boole regexContainsEol(RegProg* prog);
void eeRegFree(RegProg* prog);
void free_regexp_stuff(void);
Boole eeRegexec_prog(OUT RegProg** prog, Boole ignore_case, S line, ColNr   col);
Boole eeRegexec(RegMatch* rmp, Byte *line, ColNr col);
int eeRegexec_nl(RegMatch *rmp, Byte *line, ColNr col);
Long eeRegexec_multi(
	RegMultilineMatch *rmp,
	Portal* port, //portal in which to search or NULL
	Book* book,  //book in which to search
	LineNr lnum, //nr of line to start looking for match
	ColNr col,   //column to start looking for match
	int* timed_out //flag is set when timeout limit reached
);
