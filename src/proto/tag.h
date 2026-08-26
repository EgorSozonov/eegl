CS did_set_tagfunc(OptionChange *cha);
void free_tagfunc_option(void);
int set_ref_in_tagfunc(int copyID UNUSED);
int do_tag(
   CS tag,      // tag (pattern) to jump to
   Unt type,
   int count,
   Boole forceit,   // :ta with !
   Boole verbose   // print "tag not found" message
);
void tag_freematch(void);
void do_tags(Invocation *eap UNUSED);
int find_tags(
   CS pat,         // pattern to search for
   Unt flags,
   int mincount,      // MAXCOL: find all matches. other: minimal number of matches
   CS buf_ffname,      // name of buffer for priority
   OUT ExpandMatch* matches
);
void free_tag_stuff(void);
int get_tagfname(
   TagName   *tnp,   // holds status info
   int      first,   // true when first file name is wanted
   OUT CS buf)   // pointer to buffer of MAXPATHL chars
;
void tagname_free(TagName *tnp);
void tagstack_clear_entry(Taggy* item);
int expand_tags(Boole expandTagNames, CS pat, OUT ExpandMatch* matches);
int get_tags(List* list, CS pat, CS buf_fname);
void get_tagstack(Portal* wp, Bag* retBag);
int set_tagstack(Portal *wp, Bag *d, Unt action);
CS get_cscope_name(Expand* xp UNUSED, int idx);
void set_context_in_cscope_cmd(Expand* xp, CS arg, CommIndex id);
void c_cscope(Invocation* invo);
void c_scscope(Invocation* invo);
void c_cstag(Invocation* invo);
int cs_fgets(CS buf, int size);
void cs_free_tags(void);
void cs_print_tags(void);
void cs_end(void);
void f_cscope_connection(Var *argvars UNUSED, Var *returnVar UNUSED);
