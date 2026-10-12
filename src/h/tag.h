#ifndef AVOID_DUPLICATION
#define TAG_HELP         1   //only search for help tags
#define TAG_NAMES        2   //only return name of tag
#define TAG_REGEXP       4   //use tag pattern as regexp
#define TAG_NOIC         8   //don't always ignore case
#define TAG_CSCOPE      16   //cscope tag
#define TAG_VERBOSE     32   //message verbosity
#define TAG_INS_COMP    64   //Currently doing insert completion
#define TAG_KEEP_LANG  128   //keep current language
#define TAG_NO_TAGFUNC 256   //do not use 'tagfunc'
#define TAG_MANY       300   //When finding many tags (for completion), find up to this many tags
#endif

CS did_set_tagfunc(OptionChange *cha);
void free_tagfunc_option(void);
int do_tag(
   CS tag,      //tag (pattern) to jump to
   Unt type,
   int count,
   Boole forceit,   //:ta with !
   Boole verbose   //print "tag not found" message
);
void tag_freematch(void);
void do_tags(Invocation*);
int find_tags(
   CS pat,         //pattern to search for
   Unt flags,
   int mincount,      //MAXCOL: find all matches. other: minimal number of matches
   CS buf_ffname,      //name of buffer for priority
   OUT ExpandMatch* matches
);
void free_tag_stuff(void);
void tagstack_clear_entry(Taggy* item);
int expand_tags(Boole expandTagNames, CS pat, OUT ExpandMatch* matches);
int get_tags(List* list, CS pat, CS buf_fname);
void get_tagstack(Portal* wp, Bag* retBag);
int set_tagstack(Portal *wp, Bag *d, Unt action);
void f_tagfiles(Arr(Var), Var* returnVar);
void f_taglist(Arr(Var) argvars, Var* returnVar);
CS get_cscope_name(Expand*, int idx);
void set_context_in_cscope_cmd(Expand* xp, CS arg, CommIndex id);
void c_cscope(Invocation* invo);
void c_scscope(Invocation* invo);
void c_cstag(Invocation* invo);
int cs_fgets(CS buf, int size);
void cs_free_tags(void);
void cs_print_tags(void);
void cs_end(void);
void f_cscope_connection(Arr(Var) argvars, Var *returnVar);
