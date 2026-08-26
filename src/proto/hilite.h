int highlight_link_id(Short hiId);
void initHilite(int reset);
void doHilite(CS line, Boole forceit, Boole init);
Short hiliteGroupByName(Text name);
Decoration decosByHiliteName(CS name);
Boole hiliteExists(Text name);
CS syn_id2name(Unt id);
Byte decorationByHiliteId(Short hiId);
Byte syn_id2colors(Short hiId, OUT VTermColor* fgp, OUT VTermColor* bgp);
void setCompletionContextInHiliteCommand(OUT Expand* xp, CS arg);
Text getHiliteGroupName(Expand* xp UNUSED, int id);
CS getHiliteGroupNameAsCString(Expand *xp, int id);
int expandHiliteGroup(
   CS pattern,
   Expand* xp,
   RegMatch* rmp,
   OUT ExpandMatch* matches
);
void f_hlget(Var *argvars, Var *returnVar);
char getDecoFlags(Short hiId);
Decoration getFullDecoration(Short hiId);
Boole decoEq(Decoration a, Decoration b);
void syntaxStartLine(Portal *wp, LineNr lnum);
void synFreeBlock(SyntaxBlock *block);
void syn_stack_apply_changes(Book* book);
void syntax_end_parsing(Portal *wp, LineNr lnum);
int syntax_check_changed(LineNr lnum);
Decoration syntGetDeco(
   ColNr col,
   int keep_state   // keep state of char at "col"
);
void syntax_clear(SyntaxBlock *block);
void reset_synblock(Portal *wp);
Short syntaxClusterByName(Text line);
void c_syntax(Invocation* invo);
void c_ownsyntax(Invocation* invo);
int syntax_present(Portal* po);
void reset_expand_highlight(void);
void set_context_in_echohl_cmd(Expand *xp, CS arg);
void set_context_in_syntax_cmd(Expand *xp, CS arg);
CS get_syntax_name(Expand* xp, int idx);
int syn_get_id(
   Portal   *wp,
   long   lnum,
   ColNr   col,
   int      trans,      // remove transparency
   int      keep_state  // keep state of char at "col"
);
int get_syntax_info(int *seqnrp);
int syn_get_stack_item(int i);
int syn_get_foldlevel(Portal *po, long lnum);
void f_synIDtrans(Arr(Var) argvars UNUSED, Var* returnVar);
