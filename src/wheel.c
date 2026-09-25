//EEGL - the Extensible development Environment for GNU/Linux
//Licensed under GPLv3, see the LICENSE file (c) Egor Sozonov

//## wheel.c: code for actions in Normal, Insert and Visual modes. User's steering wheel

#include "eegl.h"
#include "h/data.types.h"
#include "h/data.h"
#include "h/book.h"
#include "h/input.types.h"
#include "h/input.h"
#include "h/channel.types.h"
#include "h/channel.h"
#include "h/diff.h"
#include "h/do.h"
#include "h/draw.types.h"
#include "h/draw.h"
#include "h/eval.h"
#include "h/fileio.h"
#include "h/hilite.types.h"
#include "h/hilite.h"
#include "h/location.types.h"
#include "h/location.h"
#include "h/message.h"
#include "h/motor.types.h"
#include "h/motor.h"
#include "h/option.h"
#include "h/portal.h"
#include "h/regexp.h"
#include "h/script.h"
#include "h/search.types.h"
#include "h/search.h"
#include "h/strings.h"
#include "h/tag.h"
#include "h/term.h"
#include "h/ui.h"
#include "h/wheel.h"
#include "h/window.h"

private int VIsual_mode_orig = ZERO;      // saved Visual mode

// nv_*(): functions are called to handle Normal and Visual mode actions.
// n_*(): functions are called to handle Normal mode actions.
// v_*(): functions are called to handle Visual mode actions.
//{{{types

typedef struct {
   LineNr lnum; // line number
   int fill;    // filler lines
   int height;  // height of added line
} LineOffset;

declStruct(InsertCompletion);

//state information used for getting the next set of insert completion matches.
typedef struct {
   CS e_cpt_copy;      // copy of 'complete'
   CS e_cpt;         // current entry in "e_cpt_copy"
   Book* scannedBook;      // book being scanned
   Pos* cur_match_pos;      // current match position
   Pos prev_match_pos;      // previous match position
   int set_match_pos;      // save first_match_pos/last_match_pos
   Pos first_match_pos;   // first match position
   Pos last_match_pos;      // last match position
   int found_all;      // found all matches of a certain type.
   CS dict;         // dictionary file to search
   int dict_f;         // "dict" is an exact file name or not
   Callback* func_cb;      // callback of function in 'cpt' option
} InsertionCompletionNext;

//}}}
//{{{@@@forward decls
private int cls(void);
private int skip_chars(int cclass, int dir);
private void back_in_line(void);
private void find_first_blank(Pos *posp);
private void findsent_forward(long count, int at_start_sent);
private int current_block(
   Operator* oper,
   long count,
   int include,   // true == include white space
   Unt what,      // '(', '{', etc.
   Unt other      // ')', '}', etc.
);
private int in_html_tag(int end_tag);
private int find_next_quote(
   CS line,
   int col,
   int  quotechar,
   Boole escapeWithBackslash   // does backslash escape the quote character?
);
private int find_prev_quote(
   CS line,
   int col_start,
   int quotechar,
   Boole escapeWithBackslash   // does backslash escape the quote character?
);
private int findAction(int actionChar);
private int checkTextLocked(Operator* oper);
private int normalCmdGetCount(
   ActionArg* aArg,
   Unt c,
   int toplevel,
   int set_prevcount,
   OUT int* ctrl_w,
   OUT int* need_flushbuf
);
private int needsMoreChars(ActionArg* aArg, Short cmd_flags);
private int getMoreChars(
   int idx_arg,
   ActionArg* aArg,
   int* need_flushbuf
);
private int needToWaitForMsg(ActionArg* aArg, Pos *old_pos);
private void waitForMsg(void);
private void setVCountPrevCount(ActionArg* aArg, int *set_prevcount);
private void callYankDoAutocmd(int regname);
private int checkIsBalloonItem(CS ptr, int* colp, int* bnp, int dir);
private void prepareForRedo(ActionArg* aArg);
private int checkclearop(Operator* oper);
private int checkclearopq(Operator* oper);
private void unshift_special(ActionArg* aArg);
private void del_from_showcmd(int len);
private void display_showcmd(void);
private Boole isIdent(CS line, int offset);
private int normal_search(
   ActionArg* aArg,
   int dir,
   Text pat,
   int opt,      // extra flags for do_search()
   int* wrapped
);
private void adjust_cursor(Operator *oper);
private void invokeEdit(
   ActionArg* aArg,
   int repl,      // "r" action
   int cmd,
   int startln
);
private void utf_find_illegal(void);
private Boole wasAnyBookChanged(void);
private void nv_ignore(ActionArg* aArg);
private void nv_nop(ActionArg*);
private void nvError(ActionArg* aArg);
private void nv_help(ActionArg* aArg);
private void nvAddSub(ActionArg* aArg);
private void nvPage(ActionArg* aArg);
private void nv_gd(Operator* oper, int nchar, int      thisblock);
private int appendDigitLong(OUT Long* value, int digit);
private int widthLeft(Portal* po);
private int nv_z_get_count(ActionArg* aArg, Unt* nchar_arg);
private void nv_zet(ActionArg* aArg);
private void nv_colon(ActionArg* aArg);
private void nv_ctrlg(ActionArg* aArg);
private void nv_ctrlh(ActionArg* aArg);
private void nv_clear(ActionArg* aArg);
private void nv_ctrlo(ActionArg* aArg);
private void nv_hat(ActionArg* aArg);
private void nv_Zet(ActionArg* aArg);
private int nv_K_getcmd(
   ActionArg* aArg,
   CS kp,
   int kp_help,
   int kp_ex,
   Byte** ptr_arg,
   int n,
   CS buffer,
   Unt bufsize,
   Unt* buflen
);
private void nv_ident(ActionArg* aArg);
private void nv_tagpop(ActionArg* aArg);
private void nv_scroll(ActionArg* aArg);
private void nv_right(ActionArg* aArg);
private void nv_left(ActionArg* aArg);
private void nv_up(ActionArg* aArg);
private void nv_down(ActionArg* aArg);
private void nv_gotofile(ActionArg* aArg);
private void nv_end(ActionArg* aArg);
private void nv_dollar(ActionArg* aArg);
private void nv_search(ActionArg* aArg);
private void nv_next(ActionArg* aArg);
private void nv_csearch(ActionArg* aArg);
private void nv_bracket_block(ActionArg* aArg, Pos* old_pos);
private void nv_brackets(ActionArg* aArg);
private void nv_percent(ActionArg* aArg);
private void nv_brace(ActionArg* aArg);
private void nv_mark(ActionArg* aArg);
private void nv_findpar(ActionArg* aArg);
private void nv_undo(ActionArg* aArg);
private void nv_kundo(ActionArg* aArg);
private void nv_replace(ActionArg* aArg);
private void nv_cursormark(ActionArg* aArg, int flag, Pos *pos);
private void nv_subst(ActionArg* aArg);
private void nv_abbrev(ActionArg* aArg);
private void nvOperatorAliases(ActionArg* aArg);
private void nv_gomark(ActionArg* aArg);
private void nv_pcmark(ActionArg* aArg);
private void nv_regname(ActionArg* aArg);
private void nv_visual(ActionArg* aArg);
private void nv_portal(ActionArg* aArg);
private void nv_suspend(ActionArg* aArg);
private void nv_gv_cmd(ActionArg*);
private void gUnderscoreAction(ActionArg* aArg);
private void nvGDollarAction(ActionArg* aArg);
private void nv_gi_cmd(ActionArg* aArg);
private void nv_g_cmd(ActionArg* aArg);
private void nvDot(ActionArg* aArg);
private void nv_redo_or_register(ActionArg* aArg);
private void nv_Undo(ActionArg* aArg);
private void nv_tilde(ActionArg* aArg);
private void set_op_var(int optype);
private void nv_operator(ActionArg* aArg);
private void nv_home(ActionArg* aArg);
private void nv_pipe(ActionArg* aArg);
private void nv_bck_word(ActionArg* aArg);
private void nv_wordcmd(ActionArg* aArg);
private void nv_beginline(ActionArg* aArg);
private void nv_goto(ActionArg* aArg);
private void nv_normal(ActionArg* aArg);
private void nv_esc(ActionArg* aArg);
private void nv_edit(ActionArg* aArg);
private void nvOpen(ActionArg* aArg);
private void nv_drop(ActionArg*);
private void nv_cursorhold(ActionArg* aArg);
private void nv_object(ActionArg* aArg);
private void nv_record(ActionArg* aArg);
private void nv_at(ActionArg* aArg);
private void nv_halfpage(ActionArg* aArg);
private void nvJoin(ActionArg* aArg);
private void nv_put(ActionArg* aArg);
private void nv_put_opt(ActionArg* aArg, int fix_indent);
private void n_swapchar(ActionArg* aArg);
private void nOpenAction(ActionArg* aArg);
private void n_start_visual_mode(int c);
private void vVisualOperators(ActionArg* aArg);
private void v_swap_corners(int cmdchar);
private int plines_correct_topline(Portal* po, LineNr lnum, int limit_winheight);
private void comp_botline(Portal* po);
private void redraw_for_cursorline(Portal* po);
private void redraw_for_cursorcolumn(Portal* po);
private int skipcol_from_plines(Portal* po, int plines_off);
private void reset_skipcol(void);
private int scrolljump_value(void);
private int check_top_offset(void);
private void update_curswant_force(void);
private void curs_rows(Portal* po);
private int virtcol2col(Portal* po, LineNr lnum, int vcol);
private void cursor_correct_sms(void);
private void topline_back_winheight(LineOffset* lp, int winheight);
private void topline_back(LineOffset *lp);
private void botline_forw(LineOffset* lp);
private void scroll_cursor_top(int min_scroll, int always);
private int get_scroll_overlap(int dir);
private int scrollSmoothly(int dir, long count, long *curscount);
private void validateMappingTable(void);
private void mapFree(MapBlock** mpp);
private CS mapModeToChars(int mode);
private void showMap(MapBlock* mp, int local);
private MapBlock * addToMap(
   MapBlock** map_table,
   MapBlock** abbr_table,
   CS keys,
   CS rhs,
   CS orig_rhs,
   Unt noremap,
   int nowait,
   int silent,
   int mode,
   int is_abbr,
   int expr,
   ScriptId sid,       // 0 to use scriptPosG
   LineNr lnum,
   int simplified
);
private void listMappings(
   int keyround,
   int abbrev,
   int haskey,
   CS keys,
   int keys_len,
   int mode,
   int* did_local
);
private int getMapMode(CS* cmdp, Boole forceit);
private void mapClear(CS cmdp, CS arg, Boole forceit, int abbr);
private int isMapLocked(void);
private CS translateMapping(CS str);
private void mapblock2dict(
   MapBlock* mp,
   Bag* bag,
   NULLABLE CS lhsrawalt,
   int bookLocal,   // false if not buffer local mapping
   int abbr       // true if abbreviation
);
private void getMapArg(Var* argvars, Var* returnVar, int exact);
private int getMapModeString(CS mode_string, int abbr);
private void setEntry(int from, int to);
private void mappingImpl(Invocation* invo, Boole isabbrev);
private int char_before_cursor(void);
private void redrawInInsertMode(Boole ready);
private void insertStartVisualBlockMode(void);
private int decodeModifyOtherKeys(int c);
private int del_char_after_col(int limit_col);
private void insertRegular(Unt c, Boole allow_modmask, Boole ctrlv);
private void redo_literal(int c);
private void start_arrow_with_change(NULLABLE Pos* end_insert_pos, int end_change);
private void start_arrow_common(NULLABLE Pos* end_insert_pos, int end_change);
private void check_spell_redraw(void);
private void stop_insert(
   Pos* end_insert_pos,
   int esc,         // called by ins_esc()
   int nomove       // <c-\><c-o>, don't move cursor
);
private Boole echeck_abbr(Unt c);
private void insertRegisterContents(void);
private void ins_ctrl_g(void);
private void ins_ctrl_hat(void);
private int ins_esc(long* count, int commChar, int nomove);
private int ins_start_select(int c);
private void ins_ctrl_o(void);
private void ins_shift(Unt c, int lastc);
private void ins_del(void);
private void ins_bs_one(void);
private int ins_bs(int c, int mode, int* inserted_space_p);
private void ins_left(void);
private void ins_home(Unt c);
private void ins_end(Unt c);
private void ins_s_left(void);
private void ins_right(void);
private void ins_s_right(void);
private void ins_up( int      startcol);
private void ins_pageup(void);
private void ins_down(int startcol);
private void ins_pagedown(void);
private void ins_drop(void);
private int ins_tab(void);
private int ins_eol(Unt c);
private Unt ins_ctrl_ey(Unt tc);
private int ctrl_x_mode_normal(void)    ;
private int ctrl_x_mode_scroll(void)    ;
private int ctrl_x_mode_files(void)    ;
private int ctrl_x_mode_tags(void)    ;
private int ctrl_x_mode_path_patterns(void)    ;
private int ctrl_x_mode_path_defines(void)    ;
private int ctrl_x_mode_dictionary(void)    ;
private int ctrl_x_mode_thesaurus(void)    ;
private int ctrl_x_mode_cmdline(void);
private int ctrl_x_mode_function(void)    ;
private int ctrl_x_mode_omni(void)    ;
private int ctrl_x_mode_eval(void)    ;
private int ctrl_x_mode_line_or_eval(void)    ;
private int ctrl_x_mode_register(void)    ;
private void compl_status_clear(void);
private int compl_dir_forward(void);
private int compl_shows_dir_forward(void);
private int compl_shows_dir_backward(void);
private int has_compl_option(int dict_opt);
private int match_at_original_text(InsertCompletion *match);
private int is_first_match(InsertCompletion *match);
private int ins_compl_accept_char(int c);
private CS ins_compl_infercase_gettext(
   CS str,
   int char_len,
   int compl_char_len,
   int min_len,
   OUT Byte** tofree
);
private int cfc_has_mode(void);
private int is_nearest_active(void);
private Unt addMatchToList(
   CS str,
   int len,
   CS fname,
   Byte** cptext,       // extra text for popup menu or NULL
   Var* user_data,  // "user_data" entry or NULL
   Unt cdir,
   Unt flags_arg,
   Boole adup,          // accept duplicate match
   Arr(Decoration) userDecos,           // user abbreviation/kind decorations
   int      score
);
private int ins_compl_equal(InsertCompletion *match, CS str, int len);
private void ins_compl_insert_bytes(CS p, int len);
private Boole ins_compl_has_multiple(void);
private void ins_compl_longest_match(InsertCompletion* match);
private void ins_compl_add_matches(OUT ExpandMatch* matches, int icase);
private int ins_compl_make_cyclic(void);
private int ins_compl_has_shown_match(void);
private int ins_compl_long_shown_match(void);
private void ins_compl_upd_pum(void);
private void ins_compl_del_pum(void);
private int pum_wanted(void);
private int pum_enough_matches(void);
private Bag * ins_compl_allocBag(InsertCompletion *match);
private void trigger_complete_changed_event(int cur);
private void* cp_get_next(void *node);
private void cp_set_next(void *node, void *next);
private void* cp_get_prev(void* node);
private void cp_set_prev(void* node, void* prev);
private int cp_compare_fuzzy(const void* a, const void* b);
private int cp_compare_nearest(const void* a, const void* b);
private Unt prepend_startcol_text(Text* dest, Text* src, int startcol);
private Text* get_leader_for_startcol(InsertCompletion* match, int cached);
private void set_fuzzy_score(void);
private void sort_compl_match_list(int (*compare)(const void *, const void *));
private int ins_compl_build_pum(void);
private Unt ins_compl_leader_len(void);
private void ins_compl_dictionaries(
   NULLABLE CS dict_start,
   CS pat,
   Unt flags,      // DICT_FIRST and/or DICT_EXACT
   int thesaurus   // Thesaurus completion
);
private Unt thesaurus_add_words_in_line(CS fname, OUT CS* buf_arg, Unt dir, CS skip_word);
private void filterFromFiles(
   OUT ExpandMatch files,
   int thesaurus,
   Unt flags,
   RegMatch* regmatch,
   CS buf,
   OUT Unt* dir
);
private void ins_compl_item_free(InsertCompletion* match);
private void ins_compl_free(void);
private void ins_compl_clear(void);
private Boole ins_compl_used_match(void);
private void ins_compl_init_get_longest(void);
private int ins_compl_enter_selects(void);
private ColNr ins_compl_col(void);
private int ins_compl_has_preinsert(void);
private int ins_compl_preinsert_effect(void);
private int ins_compl_bs(void);
private int ins_compl_need_restart(void);
private void ins_compl_new_leader(void);
private int get_compl_len(void);
private void ins_compl_addleader(int c);
private void ins_compl_restart(void);
private void ins_compl_set_original_text(CS str, Unt len);
private void ins_compl_addfrommatch(void);
private Boole set_ctrl_x_mode(Unt c);
private void trigger_complete_done_event(int mode, CS word);
private int ins_compl_stop(Unt c, int prev_mode, int retval);
private int ins_compl_cancel(void);
private Boole ins_compl_prep(Unt c);
private void ins_compl_fixRedoBufForLeader(CS ptr_arg);
private Book* ins_compl_next_buf(Book* book, Unt flag);
private Unt copyCompletionCbs(OUT Callback* dest, Callback* src);
private CS get_complete_funcname(int type);
private Callback* get_insert_callback(int type);
private void expand_by_function(int type, CS base, Callback* cb);
private inline Decoration getUserDecoration(CS hlname);
private int ins_compl_add_tv(Var* tv, Unt dir, int fast);
private void ins_compl_add_list(List* list);
private void ins_compl_add_dict(Bag* dict);
private void set_completion(ColNr startcol, List *list);
private Unt add_match_to_list( Var  *returnVar, CS str, int len, int pos);
private CS ins_compl_mode(void);
private void ins_compl_update_sequence_numbers(void);
private void fill_complete_info_dict(Bag *di, InsertCompletion *match, int add_match);
private void get_complete_info(List *what_list, Bag *retdict);
private int thesaurus_func_complete(int type);
private int may_advance_cpt_index(CS cpt);
private int process_next_cpt_value(
   OUT InsertionCompletionNext* st,
   OUT Unt* InsertCompletionype_arg,
   Pos* start_match_pos,
   int fuzzy_collect,
   OUT int* advance_cpt_idx
);
private void get_next_include_file_completion(Unt insertCompletionType);
private void get_next_dict_tsr_completion(int insertCompletionType, CS dict, int dict_f);
private void get_next_tag_completion(void);
private void ins_compl_longest_insert(CS prefix);
private void fuzzy_longest_match(void);
private void get_next_filename_completion(void);
private void get_next_cmdline_completion(void);
private CS ins_compl_get_next_word_or_line(
   Book* scannedBook,      // buffer being scanned
   Pos* cur_match_pos,      // current match position
   int* match_len,
   int* cont_s_ipos
);
private Unt get_next_default_completion(InsertionCompletionNext* st, Pos* start_pos);
private Callback * get_callback_if_cfn(CS p);
private void get_register_completion(void);
private Unt get_next_completion_match(int type, InsertionCompletionNext *st, Pos *ini);
private void strip_caret_numbers_in_place(CS str);
private int prepare_cpt_compl_funcs(void);
private void compl_source_start_timer(int source_idx);
private int advance_cpt_sources_index_safe(void);
private int ins_compl_get_exp(Pos* ini);
private void ins_compl_update_shown_match(void);
private void ins_compl_delete(void);
private void ins_compl_expand_multiple(CS str);
private void ins_compl_insert(int move_cursor);
private void ins_compl_show_filename(void);
private InsertCompletion * find_next_match_in_menu(void);
private Unt find_next_completion_match(
   int allow_get_expansion,
   int todo,      // repeat completion this many times
   int advance,
   int* num_matches
);
private int ins_compl_next(
   int allow_get_expansion,
   int count,      // repeat completion this many times; should be at least 1
   Boole doInsertMatch   // Insert the newly selected match
);
private void check_elapsed_time(void);
private Unt ins_compl_key2dir(Unt c);
private Boole ins_compl_pum_key(Unt c);
private int ins_compl_key2count(Unt c);
private Boole shouldNewCharInsertTheMatch(int c);
private Unt get_normal_compl_info(CS line, int startcol, ColNr curs_col);
private int get_wholeline_compl_info(CS line, ColNr curs_col);
private int get_filename_compl_info(CS line, int startcol, ColNr curs_col);
private int get_cmdline_compl_info(CS line, ColNr curs_col);
private int set_compl_globals(int startcol, ColNr curs_col, int is_cpt_compl);
private int get_userdefined_compl_info(ColNr curs_col, Callback* cb, int* startcol);
private Unt compl_get_info(CS line, int startcol, ColNr curs_col, OUT Boole* line_invalid);
private void ins_compl_continue_search(CS line);
private Unt ins_compl_start(void);
private void ins_compl_show_statusmsg(void);
private Unt ins_complete(Unt c, Boole enable_pum);
private Boole ins_compl_setup_autocompl(Unt c);
private void show_pum(int prev_cursorRow, int prev_leftCol);
private unsigned quote_meta(CS dest, CS src, int len);
private void cpt_sources_clear(void);
private Unt setup_cpt_sources(void);
private Boole is_cfn_refresh_always(void);
private void ins_compl_make_linear(void);
private InsertCompletion * remove_old_matches(void);
private void get_cfn_completion_matches(Callback* cb);
private void cpt_compl_refresh(void);
private void copyGlobalToBookLocalCb(Callback* globcb, Callback* bookCb);
private void pchar_cursor(int c);
private int paragraph_start(LineNr lnum);
//}}}

// Declare actions[].
#define DO_DECLARE_ACTIONS
#include "actions.h"
// The lookuptable generated by indices/createActionIndices.vim.
#include "indices/actions.h"

//{{{text objects (things like insides of parentheses)

private int cls(void);
private int skip_chars(int, int);

// Find the start of the next sentence, searching in the direction specified
// by the "dir" argument.  The cursor is positioned on the start of the next
// sentence when found.  If the next sentence is found, return OK.  Return FAIL
// otherwise.  See ":h sentence" for the precise definition of a "sentence" text object.
pub int
findsent(int dir, long count) {
   Pos   pos, tpos;
   Pos   prev_pos;
   int      c;
   int      (*func)(Pos *);
   int      startlnum;
   int      noskip = false;       // do not skip blanks
   int      found_dot;

   pos = curPor->cursor;
   if (dir == FORWARD)
      func = incl;
   else
      func = decl;

   while (count--) {
      prev_pos = pos;

      // if on an empty line, skip up to a non-empty line
      if (gchar_pos(&pos) == ZERO) {
         do {
            if ((*func)(&pos) == -1)
                break;
         } while (gchar_pos(&pos) == ZERO);
         if (dir == FORWARD)
            goto found;
      }
      // if on the start of a paragraph or a section and searching forward, go to the next line
      ei (dir == FORWARD && pos.col == 0 && startPS(pos.lnum, ZERO, false)) {
         if (pos.lnum == curBook->mem.lineCount)
            return FAIL;
         ++pos.lnum;
         goto found;
      } ei (dir == BACKWARD)
         decl(&pos);

      // go back to the previous non-white non-punctuation character
      found_dot = false;
      while (c = gchar_pos(&pos), SPACE_OR_TAB(c) || firstOccurrence(S".!?)]\"'", c) != NULL) {
         tpos = pos;
         if (decl(&tpos) == -1 || (LINEEMPTY(tpos.lnum) && dir == FORWARD))
            break;

         if (found_dot)
            break;
         if (firstOccurrence((CS) ".!?", c) != NULL)
            found_dot = true;

         if (firstOccurrence((CS) ")]\"'", c) != NULL
               && firstOccurrence((CS) ".!?)]\"'", gchar_pos(&tpos)) == NULL)
            break;

          decl(&pos);
      }

      // remember the line where the search started
      startlnum = pos.lnum;

      for (;;) {     // find end of sentence
          c = gchar_pos(&pos);
         if (c == ZERO || (pos.col == 0 && startPS(pos.lnum, ZERO, false))) {
            if (dir == BACKWARD && pos.lnum != startlnum)
               ++pos.lnum;
            break;
         }
         if (c == '.' || c == '!' || c == '?') {
            tpos = pos;
            do
                if ((c = inc(&tpos)) == -1)
               break;
            while (firstOccurrence((CS)")]\"'", c = gchar_pos(&tpos))
               != NULL);
            if (c == -1  || (c == ' ' || c == '\t') || c == ZERO) {
                pos = tpos;
                if (gchar_pos(&pos) == ZERO) // skip ZERO at EOL
               inc(&pos);
                break;
            }
          }
          if ((*func)(&pos) == -1) {
         if (count)
             return FAIL;
         noskip = true;
         break;
          }
      }
   found:
          // skip white space
      while (!noskip && ((c = gchar_pos(&pos)) == ' ' || c == '\t'))
          if (incl(&pos) == -1)
         break;

      if (EQUAL_POS(prev_pos, pos)) {
          // didn't actually move, advance one character and try again
          if ((*func)(&pos) == -1) {
         if (count)
             return FAIL;
         break;
          }
          ++count;
      }
   }

   setpcmark();
   curPor->cursor = pos;
   return OK;
}

// Find the next paragraph or section in direction 'dir'.
// Paragraphs are currently supposed to be separated by empty lines.
// If 'what' is ZERO we go to the next paragraph.
// If 'what' is '{' or '}' we go to the next section.
// If 'both' is true also stop at '}'.
// Return true if the next paragraph or section was found.
pub int
normFindNextParagraf(
   OUT Boole* pincl,       // Return: true if last char is to be included
   int dir,
   long count,
   int what,
   int both
){
   int did_skip;   // true after separating lines have been skipped
   int first;       // true on first line
   LineNr fold_first;   // first line of a closed fold
   LineNr fold_last;   // last line of a closed fold
   int fold_skipped;   // true if a closed fold was skipped this iteration

   LineNr curr = curPor->cursor.lnum;

   while (count--) {
      did_skip = false;
      for (first = true; ; first = false) {
         if (*ml_get(curr) != ZERO)
            did_skip = true;

         // skip folded lines
         fold_skipped = false;
         if (first && getFolds(curr, &fold_first, &fold_last)) {
            curr = ((dir > 0) ? fold_last : fold_first) + dir;
            fold_skipped = true;
         }

         if (!first && did_skip && (startPS(curr, what, both)))
            break;

         if (fold_skipped)
            curr -= dir;
         if ((curr += dir) < 1 || curr > curBook->mem.lineCount) {
            if (count)
               return false;
            curr -= dir;
            break;
         }
      }
   }
   setpcmark();
   if (both && *ml_get(curr) == '}')   // include line with '}'
      ++curr;
   curPor->cursor.lnum = curr;
   if (curr == curBook->mem.lineCount && what != '}' && dir == FORWARD) {
      CS line = ml_get(curr);

      // Put the cursor on the last character in the last line and make the motion inclusive.
      if ((curPor->cursor.col = ml_get_len(curr)) != 0) {
          --curPor->cursor.col;
          curPor->cursor.col -= (*mb_head_off)(line, line + curPor->cursor.col);
          *pincl = true;
      }
   } else
      curPor->cursor.col = 0;
   return true;
}

// startPS: return true if line 'lnum' is the start of a section or paragraph.
// If 'para' is '{' or '}' only check for sections.
// If 'both' is true also stop at '}'
pub int
startPS(LineNr lnum, int para, int both) {
   CS s = ml_get(lnum);
   return (*s == para || *s == '\f' || (both && *s == '}'));
}

// The following routines do the word searches performed by the 'w', 'W',
// 'b', 'B', 'e', and 'E' commands.

// To perform these searches, characters are placed into one of three
// classes, and transitions between classes determine word boundaries.
//
// The classes are:
//
// 0 - white space
// 1 - punctuation
// 2 or higher - keyword characters (letters, digits and underscore)

private int   cls_bigword;   // true for "W", "B" or "E"

// cls() - return the class of character at curPor->cursor
//
// If a 'W', 'B', or 'E' motion is being done (cls_bigword == true), chars from class 2 and higher
// are reported as class 1 since only white space boundaries are of interest.
private int
cls(void) {
   int c = gchar_cursor();
   if (c == ' ' || c == '\t' || c == ZERO)
      return 0;
   c = utf_class(c);
   if (c != 0 && cls_bigword)
       return 1;
   return c;

   // If cls_bigword is true, report all non-blanks as class 1.
   if (cls_bigword)
      return 1;

   if (eeIsWordc(c))
      return 2;
   return 1;
}


// fwd_word(count, type, eol) - move forward one word
//
// Return FAIL if the cursor was already at the end of the file.
// If eol is true, last word stops at end of line (for operators).
pub int
fwd_word(
   long   count,
   int      bigword,    // "W", "E" or "B"
   int      eol)
{
   int      sclass;       // starting class
   int      i;
   int      last_line;

   curPor->cursor.coladd = 0;
   cls_bigword = bigword;
   while (--count >= 0) {
      // When inside a range of folded lines, move to the last char of the last line.
      if (getFolds(curPor->cursor.lnum, NULL, &curPor->cursor.lnum))
          coladvance((ColNr)MAXCOL);
      sclass = cls();

      // We always move at least one character, unless on the last character in the book.
      last_line = (curPor->cursor.lnum == curBook->mem.lineCount);
      i = inc_cursor();
      if (i == -1 || (i >= 1 && last_line)) // started at last char in file
          return FAIL;
      if (i >= 1 && eol && count == 0)      // started at last char in line
          return OK;

      // Go one char past end of current word (if any)
      if (sclass != 0)
         while (cls() == sclass) {
            i = inc_cursor();
            if (i == -1 || (i >= 1 && eol && count == 0))
               return OK;
         }

      // go to next non-white
      while (cls() == 0) {
         //We'll stop if we land on a blank line
         if (curPor->cursor.col == 0 && *ml_get_curline() == ZERO)
            break;

         i = inc_cursor();
         if (i == -1 || (i >= 1 && eol && count == 0))
            return OK;
      }
   }
   return OK;
}

// bck_word() - move backward 'count' words
// If stop is true and we are already on the start of a word, move one less.
// Return FAIL if top of the file was reached.
pub int
bck_word(long count, int bigword, int stop) {
   int      sclass;       // starting class

   curPor->cursor.coladd = 0;
   cls_bigword = bigword;
   while (--count >= 0) {
      // When inside a range of folded lines, move to the first char of the first line.
      if (getFolds(curPor->cursor.lnum, &curPor->cursor.lnum, NULL))
         curPor->cursor.col = 0;
      sclass = cls();
      if (dec_cursor() == -1)      // started at start of file
         return FAIL;

      if (!stop || sclass == cls() || sclass == 0) {
         // Skip white space before the word. Stop on an empty line.
         while (cls() == 0) {
            if (curPor->cursor.col == 0 && LINEEMPTY(curPor->cursor.lnum))
               goto finished;
            if (dec_cursor() == -1) // hit start of file, stop here
               return OK;
         }

         // Move backward to start of this word.
         if (skip_chars(cls(), BACKWARD))
            return OK;
      }

      inc_cursor();         // overshot - forward one
   finished:
      stop = false;
   }
   adjust_skipcol();
   return OK;
}

// end_word() - move to the end of the word Return FAIL if end of the file was reached.
//
// If stop is true and we are already on the end of a word, move one less.
// If empty is true stop on an empty line.
pub int
end_word(long count, int bigword, int stop, int      empty) {
   int      sclass;       // starting class

   curPor->cursor.coladd = 0;
   cls_bigword = bigword;

   while (--count >= 0) {
      // When inside a range of folded lines, move to the last char of the last line.
      if (getFolds(curPor->cursor.lnum, NULL, &curPor->cursor.lnum))
          coladvance((ColNr)MAXCOL);
      sclass = cls();
      if (inc_cursor() == -1)
          return FAIL;

      // If we're in the middle of a word, we just have to move to the end of it.
      if (cls() == sclass && sclass != 0) {
         // Move forward to end of the current word
         if (skip_chars(sclass, FORWARD))
            return FAIL;
      } ei (!stop || sclass == 0) {
          // We were at the end of a word. Go to the end of the next word.
          // First skip white space, if 'empty' is true, stop at empty line.
          while (cls() == 0) {
         if (empty && curPor->cursor.col == 0 && LINEEMPTY(curPor->cursor.lnum))
             goto finished;
         if (inc_cursor() == -1)       // hit end of file, stop here
             return FAIL;
         }

         // Move forward to the end of this word.
         if (skip_chars(cls(), FORWARD))
            return FAIL;
      }
      dec_cursor();         // overshot - one char backward
   finished:
      stop = false;         // we move only one word less
   }
   return OK;
}

// Move back to the end of the word. Return FAIL if start of the file was reached.
pub int
bckend_word(
   long   count,
   int      bigword,    // true for "B"
   int      eol)       // true: stop at end of line.
{
   int      sclass;       // starting class
   int      i;

   curPor->cursor.coladd = 0;
   cls_bigword = bigword;
   while (--count >= 0) {
      sclass = cls();
      if ((i = dec_cursor()) == -1)
          return FAIL;
      if (eol && i == 1)
          return OK;

      // Move backward to before the start of this word.
      if (sclass != 0) {
         while (cls() == sclass) {
            if ((i = dec_cursor()) == -1 || (eol && i == 1))
               return OK;
         } 
      }

      // Move backward to end of the previous word
      while (cls() == 0) {
         if (curPor->cursor.col == 0 && LINEEMPTY(curPor->cursor.lnum))
            break;
         if ((i = dec_cursor()) == -1 || (eol && i == 1))
            return OK;
      }
   }
   adjust_skipcol();
   return OK;
}

// Skip a row of characters of the same class. Return true when end-of-file reached, false otherwise
private int
skip_chars(int cclass, int dir) {
   while (cls() == cclass) {
      if ((dir == FORWARD ? inc_cursor() : dec_cursor()) == -1)
         return true;
   } 
   return false;
}

// Go back to the start of the word or the start of white space
private void
back_in_line(void) {
   int sclass = cls(); // starting class
   for (;;) {
      if (curPor->cursor.col == 0)       // stop at start of line
         break;
      dec_cursor();
      if (cls() != sclass) {        // stop at start of word
         inc_cursor();
         break;
      }
   }
}

private void
find_first_blank(Pos *posp) {
   while (decl(posp) != -1) {
      int c = gchar_pos(posp);
      if (!SPACE_OR_TAB(c)) {
         incl(posp);
         break;
      }
   }
}

// Skip count/2 sentences and count/2 separating white spaces.
private void
findsent_forward(long count, int at_start_sent) {  // cursor is at start of sentence
   while (count--) {
      findsent(FORWARD, 1L);
      if (at_start_sent)
          find_first_blank(&curPor->cursor);
      if (count == 0 || at_start_sent)
          decl(&curPor->cursor);
      at_start_sent = !at_start_sent;
   }
}

// Find word under cursor, cursor at end.
// Used while an operator is pending, and in Visual mode.
pub int
current_word(
   Operator* oper,
   long   count,
   int      include,   // true: include word and white space
   int      bigword)   // false == word, true == WORD
{
   Pos   start_pos;
   Pos   pos;
   int      inclusive = true;
   int      include_white = false;

   cls_bigword = bigword;
   CLEAR_POS(&start_pos);

   // When Visual mode is not active, or when the VIsual area is only one
   // character, select the word and/or white space under the cursor.
   if (!VIsual_active || EQUAL_POS(curPor->cursor, VIsual)) {
      // Go to start of current word or white space.
      back_in_line();
      start_pos = curPor->cursor;

      // If the start is on white space, and white space should be included
      // ("   word"), or start is not on white space, and white space should
      // not be included ("word"), find end of word.
      if ((cls() == 0) == include) {
         if (end_word(1L, bigword, true, true) == FAIL)
            return FAIL;
      } else {
         // If the start is not on white space, and white space should be included ("word    "), 
         // or start is on white space and white space should not be included ("    "), find 
         // start of word. If we end up in the first column of the next line (single char
         // word) back up to end of the line.
         fwd_word(1L, bigword, true);
         if (curPor->cursor.col == 0)
            decl(&curPor->cursor);
         else
            oneleft();

         if (include)
            include_white = true;
      }

      if (VIsual_active) {
         // should do something when inclusive == false !
         VIsual = start_pos;
         drawCurBookLater(UPD_INVERTED);   // update the inversion
      } else {
         oper->start = start_pos;
         oper->motion_type = MCHAR;
      }
      --count;
   }

   // When count is still > 0, extend with more objects.
   while (count > 0) {
      inclusive = true;
      if (VIsual_active && LT_POS(curPor->cursor, VIsual)) {
         //In Visual mode, with cursor at start: move cursor back.
         if (decl(&curPor->cursor) == -1)
            return FAIL;
         if (include != (cls() != 0)) {
            if (bck_word(1L, bigword, true) == FAIL)
               return FAIL;
         } else {
            if (bckend_word(1L, bigword, true) == FAIL)
               return FAIL;
            (void)incl(&curPor->cursor);
         }
      } else {
         //Move cursor forward one word and/or white area.
         if (incl(&curPor->cursor) == -1)
            return FAIL;
         if (include != (cls() == 0)) {
            if (fwd_word(1L, bigword, true) == FAIL && count > 1)
               return FAIL;
            //If end is just past a new-line, we don't want to include
            //the first character on the line. Put cursor on last char of white.
            if (oneleft() == FAIL)
               inclusive = false;
         } else {
            if (end_word(1L, bigword, true, true) == FAIL)
               return FAIL;
         }
      }
      --count;
   }

   if (include_white && (cls() != 0 || (curPor->cursor.col == 0 && !inclusive))) {
      // If we don't include white space at the end, move the start to include some white space 
      // there. This makes "daw" work better on the last word in a sentence (and "2daw" on 
      // last-but-one word).  Also when "2daw" deletes "word." at the end of the line
      // (cursor is at start of next line). But don't delete white space at start of line (indent).
      pos = curPor->cursor;   // save cursor position
      curPor->cursor = start_pos;
      if (oneleft() == OK) {
         back_in_line();
         if (cls() == 0 && curPor->cursor.col > 0) {
            if (VIsual_active)
               VIsual = curPor->cursor;
            else
               oper->start = curPor->cursor;
         }
      }
      curPor->cursor = pos;   // put cursor back at end
   }

   if (VIsual_active) {
      if (VIsual_mode == 'V') {
         VIsual_mode = 'v';
         redrawCommlineG = true;      // show mode later
      }
   } else
      oper->inclusive = inclusive;

   return OK;
}

// Find sentence(s) under the cursor, cursor at end.
// When Visual active, extend it by one or more sentences.
pub int
current_sent(Operator *oper, long count, int include) {
   int      start_blank;
   int      c;
   int      at_start_sent;
   long   ncount;

   Pos start_pos = curPor->cursor;
   Pos pos = start_pos;
   findsent(FORWARD, 1L);   // Find start of next sentence.

   // When the Visual area is bigger than one character: Extend it.
   if (VIsual_active && !EQUAL_POS(start_pos, VIsual)) {
extend:
         if (LT_POS(start_pos, VIsual)) {
            // Cursor at start of Visual area.
            // Find out where we are:
            // - in the white space before a sentence
            // - in a sentence or just after it
            // - at the start of a sentence
            at_start_sent = true;
            decl(&pos);
            while (LT_POS(pos, curPor->cursor)) {
               c = gchar_pos(&pos);
               if (!SPACE_OR_TAB(c)) {
                  at_start_sent = false;
                  break;
               }
               incl(&pos);
            }
            if (!at_start_sent) {
               findsent(BACKWARD, 1L);
               if (EQUAL_POS(curPor->cursor, start_pos))
                  at_start_sent = true;  // exactly at start of sentence
               else
                  // inside a sentence, go to its end (start of next)
                  findsent(FORWARD, 1L);
            }
            if (include)   // "as" gets twice as much as "is"
               count *= 2;
            while (count--) {
               if (at_start_sent)
                  find_first_blank(&curPor->cursor);
               c = gchar_cursor();
               if (!at_start_sent || (!include && !SPACE_OR_TAB(c)))
                  findsent(BACKWARD, 1L);
               at_start_sent = !at_start_sent;
            }
         } else {
            // Cursor at end of Visual area.
            // Find out where we are:
            // - just before a sentence
            // - just before or in the white space before a sentence
            // - in a sentence
            incl(&pos);
            at_start_sent = true;
            // not just before a sentence
            if (!EQUAL_POS(pos, curPor->cursor)) {
               at_start_sent = false;
               while (LT_POS(pos, curPor->cursor)) {
                  c = gchar_pos(&pos);
                  if (!SPACE_OR_TAB(c)) {
                     at_start_sent = true;
                     break;
                  }
                  incl(&pos);
               }
               if (at_start_sent)   // in the sentence
                  findsent(BACKWARD, 1L);
               else      // in/before white before a sentence
                  curPor->cursor = start_pos;
            }

            if (include)   // "as" gets twice as much as "is"
               count *= 2;
            findsent_forward(count, at_start_sent);
         }
         return OK;
   }

   // If the cursor started on a blank, check if it is just before the start of the next sentence.
   while (c = gchar_pos(&pos), SPACE_OR_TAB(c))   // SPACE_OR_TAB() is a macro
      incl(&pos);
   if (EQUAL_POS(pos, curPor->cursor)) {
      start_blank = true;
      find_first_blank(&start_pos);   // go back to first blank
   } else {
      start_blank = false;
      findsent(BACKWARD, 1L);
      start_pos = curPor->cursor;
   }
   if (include)
      ncount = count * 2;
   else {
      ncount = count;
      if (start_blank)
         --ncount;
   }
   if (ncount > 0)
      findsent_forward(ncount, true);
   else
      decl(&curPor->cursor);

   if (include) {
      // If the blank in front of the sentence is included, exclude the
      // blanks at the end of the sentence, go back to the first blank.
      // If there are no trailing blanks, try to include leading blanks.
      if (start_blank) {
         find_first_blank(&curPor->cursor);
         c = gchar_pos(&curPor->cursor);   // SPACE_OR_TAB() is a macro
         if (SPACE_OR_TAB(c))
            decl(&curPor->cursor);
      } ei (c = gchar_cursor(), !SPACE_OR_TAB(c))
         find_first_blank(&start_pos);
   }

   if (VIsual_active) {
      // Avoid getting stuck with "is" on a single space before a sentence.
      if (EQUAL_POS(start_pos, curPor->cursor))
         goto extend;
      VIsual = start_pos;
      VIsual_mode = 'v';
      redrawCommlineG = true;      // show mode later
      drawCurBookLater(UPD_INVERTED);   // update the inversion
   } else {
      // include a newline after the sentence, if there is one
      if (incl(&curPor->cursor) == -1)
          oper->inclusive = true;
      else
          oper->inclusive = false;
      oper->start = start_pos;
      oper->motion_type = MCHAR;
   }
    return OK;
}

// Find block under the cursor, cursor at end. "what" and "other" are two matching 
// parentheses/braces/etc.
private int
current_block(
   Operator* oper,
   long count,
   int include,   // true == include white space
   Unt what,      // '(', '{', etc.
   Unt other      // ')', '}', etc.
){
   Pos* pos = NULL;
   Pos start_pos;
   Pos* end_pos;
   int sol = false;      // '{' at start of line

   Pos old_pos = curPor->cursor;
   Pos old_end = curPor->cursor;      // remember where we started
   Pos old_start = old_end;

   // If we start on '(', '{', ')', '}', etc., use the whole block inclusive.
   if (!VIsual_active || EQUAL_POS(VIsual, curPor->cursor)) {
      setpcmark();
      if (what == '{')      // ignore indent
          while (inindent(1))
         if (inc_cursor() != 0)
             break;
      if (gchar_cursor() == what)
          // cursor on '(' or '{', move cursor just after it
          ++curPor->cursor.col;
   } ei (LT_POS(VIsual, curPor->cursor)) {
      old_start = VIsual;
      curPor->cursor = VIsual;       // cursor at low end of Visual
   } else
      old_end = VIsual;

   // Search backwards for unclosed '(', '{', etc..
   // Put this position in start_pos. Ignore quotes here.
   if ((pos = findmatch(NULL, what)) != NULL) {
      while (count-- > 0) {
          if ((pos = findmatch(NULL, what)) == NULL)
         break;
          curPor->cursor = *pos;
          start_pos = *pos;   // the findmatch for end_pos will overwrite *pos
      }
   } else {
      while (count-- > 0) {
         if ((pos = findmatchlimit(NULL, what, FM_FORWARD, 0)) == NULL)
            break;
         curPor->cursor = *pos;
         start_pos = *pos;   // the findmatch for end_pos will overwrite *pos
      }
   }

   // Search for matching ')', '}', etc. Put this position in curPor->cursor.
   if (pos == NULL || (end_pos = findmatch(NULL, other)) == NULL) {
      curPor->cursor = old_pos;
      return FAIL;
   }
   curPor->cursor = *end_pos;

   //Try to exclude the '(', '{', ')', '}', etc. when "include" is false.
   //If the ending '}', ')' or ']' is only preceded by indent, skip that
   //indent.  But only if the resulting area is not smaller than what we started with.
   while (!include) {
      incl(&start_pos);
      sol = (curPor->cursor.col == 0);
      decl(&curPor->cursor);
      while (inindent(1)) {
         sol = true;
         if (decl(&curPor->cursor) != 0)
            break;
      }

      //In Visual mode, when resulting area is empty i.e. there is no inner block to select, abort.
      if (EQUAL_POS(start_pos, *end_pos) && VIsual_active) {
          curPor->cursor = old_pos;
          return FAIL;
      }

      //In Visual mode, when the resulting area is not bigger than what we
      //started with, extend it to the next block, and then exclude again.
      //Don't try to expand the area if the area is empty.
      if (!LT_POS(start_pos, old_start) && !LT_POS(old_end, curPor->cursor)
         && !EQUAL_POS(start_pos, curPor->cursor)
         && VIsual_active)
      {
          curPor->cursor = old_start;
          decl(&curPor->cursor);
          if ((pos = findmatch(NULL, what)) == NULL) {
         curPor->cursor = old_pos;
         return FAIL;
          }
          start_pos = *pos;
          curPor->cursor = *pos;
          if ((end_pos = findmatch(NULL, other)) == NULL) {
            curPor->cursor = old_pos;
            return FAIL;
          }
          curPor->cursor = *end_pos;
      } else
          break;
   }

   if (VIsual_active) {
      if (sol && gchar_cursor() != ZERO)
         inc(&curPor->cursor);   // include the line break
      VIsual = start_pos;
      VIsual_mode = 'v';
      drawCurBookLater(UPD_INVERTED);   // update the inversion
      showmode();
   } else {
      oper->start = start_pos;
      oper->motion_type = MCHAR;
      oper->inclusive = false;
      if (sol)
          incl(&curPor->cursor);
      ei (LTOREQ_POS(start_pos, curPor->cursor))
          // Include the character under the cursor.
          oper->inclusive = true;
      else
          // End is before the start (no text in between <>, [], etc.): don't
          // operate on any text.
          curPor->cursor = start_pos;
   }

    return OK;
}

// Return true if the cursor is on a "<aaa>" tag.  Ignore "<aaa/>".
// When "end_tag" is true return true if the cursor is on "</aaa>".
private int
in_html_tag(int end_tag) {
   CS line = ml_get_curline();
   CS p;
   int      c;
   int      lc = ZERO;

   for (p = line + curPor->cursor.col; p > line; ) {
      if (*p == '<')   // find '<' under/before cursor
         break;
      MB_PTR_BACK(line, p);
      if (*p == '>')   // find '>' before cursor
         break;
   }
   if (*p != '<')
      return false;

   Pos   pos;
   pos.lnum = curPor->cursor.lnum;
   pos.col = (ColNr)(p - line);

   MB_PTR_ADV(p);
   if (end_tag)
      // check that there is a '/' after the '<'
      return *p == '/';

   // check that there is no '/' after the '<'
   if (*p == '/')
      return false;

   // check that the matching '>' is not preceded by '/'
   for (;;) {
      if (inc(&pos) < 0)
          return false;
      c = *ml_get_pos(&pos);
      if (c == '>')
          break;
      lc = c;
   }
   return lc != '/';
}

// Find tag block under the cursor, cursor at end.
pub int
current_tagblock(Operator* oper, long count_arg, Boole includeWhiteSpace){
   long count = count_arg;
   long n;
   Pos old_pos;
   Pos start_pos;
   Pos end_pos;
   Pos old_start, old_end;
   CS cp;
   int len;
   int r;
   int do_include = includeWhiteSpace;
   int retval = FAIL;
   int is_inclusive = true;

   wrapSearchG = false;

   old_pos = curPor->cursor;
   old_end = curPor->cursor;          // remember where we started
   old_start = old_end;
   if (!VIsual_active)
      decl(&old_end);             // old_end is inclusive

   // If we start on "<aaa>" select that block.
   if (!VIsual_active || EQUAL_POS(VIsual, curPor->cursor)) {
      setpcmark();

      // ignore indent
      while (inindent(1)) {
         if (inc_cursor() != 0)
            break;
      } 

      if (in_html_tag(false)) {
         // cursor on start tag, move to its '>'
         while (*ml_get_cursor() != '>') {
            if (inc_cursor() < 0)
               break;
         } 
      } ei (in_html_tag(true)) {
         // cursor on end tag, move to just before it
         while (*ml_get_cursor() != '<') {
            if (dec_cursor() < 0)
               break;
         } 
         dec_cursor();
         old_end = curPor->cursor;
      }
   } ei (LT_POS(VIsual, curPor->cursor)) {
      old_start = VIsual;
      curPor->cursor = VIsual;       // cursor at low end of Visual
   } else
      old_end = VIsual;

again:
   // Search backwards for unclosed "<aaa>". Put this position in start_pos.
   for (n = 0; n < count; ++n) {
      if (do_searchpair((CS)"<[^ \t>/!]\\+\\%(\\_s\\_[^>]\\{-}[^/]>\\|$\\|\\_s\\=>\\)",
             (CS)"",
             (CS)"</[^>]*>", BACKWARD, NULL, 0, NULL, (LineNr)0, 0L) <= 0)
      {
          curPor->cursor = old_pos;
          goto theend;
      }
   }
   start_pos = curPor->cursor;

   // Search for matching "</aaa>".  First isolate the "aaa".
   inc_cursor();
   CS p = ml_get_cursor();
   for (cp = p; *cp != ZERO && *cp != '>' && !SPACE_OR_TAB(*cp); MB_PTR_ADV(cp))
      ;
   len = (int)(cp - p);
   if (len == 0) {
      curPor->cursor = old_pos;
      goto theend;
   }
   CS spat = alloc(len + 39);
   CS epat = alloc(len + 9);
   sprintf((char *)spat, "<%.*s\\>\\%%(\\_s\\_[^>]\\{-}\\_[^/]>\\|\\_s\\?>\\)\\c", len, p);
   sprintf((char *)epat, "</%.*s>\\c", len, p);

   r = do_searchpair(spat, (CS)"", epat, FORWARD, NULL, 0, NULL, (LineNr)0, 0L);

   eeglFree(spat);
   eeglFree(epat);

   if (r < 1 || LT_POS(curPor->cursor, old_end)) {
      // Can't find other end or it's before the previous end.  Could be a
      // HTML tag that doesn't have a matching end.  Search backwards for another starting tag.
      count = 1;
      curPor->cursor = start_pos;
      goto again;
   }

   if (do_include) {
   // Include up to the '>'.
   while (*ml_get_cursor() != '>')
       if (inc_cursor() < 0)
      break;
   } else {
      CS c = ml_get_cursor();

      // Exclude the '<' of the end tag.
      // If the closing tag is on new line, do not decrement cursor, but
      // make operation exclusive, so that the linefeed will be selected
      if (*c == '<' && !VIsual_active && curPor->cursor.col == 0)
          // do not decrement cursor
          is_inclusive = false;
      ei (*c == '<')
          dec_cursor();
   }
   end_pos = curPor->cursor;

   if (!do_include) {
      // Exclude the start tag,
      // but skip over '>' if it appears in quotes
      int in_quotes = false;
      curPor->cursor = start_pos;
      while (inc_cursor() >= 0) {
         p = ml_get_cursor();
         if (*p == '>' && !in_quotes) {
            inc_cursor();
            start_pos = curPor->cursor;
            break;
         } ei (*p == '"' || *p == '\'')
            in_quotes = !in_quotes;
      }
      curPor->cursor = end_pos;

      //If we are in Visual mode and now have the same text as before set "do_include" and try again
      if (VIsual_active && EQUAL_POS(start_pos, old_start) && EQUAL_POS(end_pos, old_end)) {
          do_include = true;
          curPor->cursor = old_start;
          count = count_arg;
          goto again;
      }
   }

   if (VIsual_active) {
      //If the end is before the start there is no text between tags, select
      //the char under the cursor.
      if (LT_POS(end_pos, start_pos))
          curPor->cursor = start_pos;
      VIsual = start_pos;
      VIsual_mode = 'v';
      drawCurBookLater(UPD_INVERTED);   // update the inversion
      showmode();
   } else {
   oper->start = start_pos;
   oper->motion_type = MCHAR;
   if (LT_POS(end_pos, start_pos)) {
      //End is before the start: there is no text between tags; operate
      //on an empty area.
      curPor->cursor = start_pos;
      oper->inclusive = false;
   } else
      oper->inclusive = is_inclusive;
   }
   retval = OK;

theend:
   wrapSearchG = true;
   return retval;
}

pub int
current_par(
   Operator   *oper,
   long   count,
   int      include,   // true == include white space
   int      type      // 'p' for paragraph, 'S' for section
){
   LineNr   end_lnum;
   int      white_in_front;
   int      dir;
   int      start_is_white;
   int      prev_start_is_white;
   int      retval = OK;
   int      do_white = false;
   int      t;
   int      i;

   if (type == 'S')       // not implemented yet
      return FAIL;

   LineNr start_lnum = curPor->cursor.lnum;

   // When visual area is more than one line: extend it.
   if (VIsual_active && start_lnum != VIsual.lnum) {
   extend:
      if (start_lnum < VIsual.lnum)
         dir = BACKWARD;
      else
         dir = FORWARD;
      for (i = count; --i >= 0; ) {
         if (start_lnum == (dir == BACKWARD ? 1 : curBook->mem.lineCount)) {
            retval = FAIL;
            break;
         }

         prev_start_is_white = -1;
         for (t = 0; t < 2; ++t) {
            start_lnum += dir;
            start_is_white = linewhite(start_lnum);
            if (prev_start_is_white == start_is_white) {
               start_lnum -= dir;
               break;
            }
            for (;;) {
               if (start_lnum == (dir == BACKWARD ? 1 : curBook->mem.lineCount))
                  break;
               if (start_is_white != linewhite(start_lnum + dir)
                   || (!start_is_white && startPS(start_lnum + (dir > 0 ? 1 : 0), 0, 0))
               )
                  break;
               start_lnum += dir;
            }
            if (!include)
               break;
            if (start_lnum == (dir == BACKWARD ? 1 : curBook->mem.lineCount))
               break;
            prev_start_is_white = start_is_white;
         }
      }
      curPor->cursor.lnum = start_lnum;
      curPor->cursor.col = 0;
      return retval;
   }

   //First move back to the start_lnum of the paragraph or white lines
   white_in_front = linewhite(start_lnum);
   while (start_lnum > 1) {
      if (white_in_front) {      // stop at first white line
         if (!linewhite(start_lnum - 1))
            break;
      } else {     // stop at first non-white line of start of paragraph
         if (linewhite(start_lnum - 1) || startPS(start_lnum, 0, 0))
            break;
      }
      --start_lnum;
   }

   // Move past the end of any white lines.
   end_lnum = start_lnum;
   while (end_lnum <= curBook->mem.lineCount && linewhite(end_lnum))
      ++end_lnum;

   --end_lnum;
   i = count;
   if (!include && white_in_front)
      --i;
   while (i--) {
      if (end_lnum == curBook->mem.lineCount)
         return FAIL;

      if (!include)
         do_white = linewhite(end_lnum + 1);

      if (include || !do_white) {
          ++end_lnum;
          //skip to end of paragraph
          while (end_lnum < curBook->mem.lineCount
             && !linewhite(end_lnum + 1)
             && !startPS(end_lnum + 1, 0, 0))
         ++end_lnum;
      }

      if (i == 0 && white_in_front && include)
         break;

      //skip to end of white lines after paragraph
      if (include || do_white) {
         while (end_lnum < curBook->mem.lineCount && linewhite(end_lnum + 1))
            ++end_lnum;
      } 
   }

   //If there are no empty lines at the end, try to find some empty lines at
   //the start (unless that has been done already).
   if (!white_in_front && !linewhite(end_lnum) && include) {
      while (start_lnum > 1 && linewhite(start_lnum - 1))
         --start_lnum;
   } 

   if (VIsual_active) {
      // Problem: when doing "Vipipip" nothing happens in a single white
      // line, we get stuck there.  Trap this here.
      if (VIsual_mode == 'V' && start_lnum == curPor->cursor.lnum)
         goto extend;
      if (VIsual.lnum != start_lnum) {
         VIsual.lnum = start_lnum;
         VIsual.col = 0;
      }
      VIsual_mode = 'V';
      drawCurBookLater(UPD_INVERTED);   // update the inversion
      showmode();
   } else {
      oper->start.lnum = start_lnum;
      oper->start.col = 0;
      oper->motion_type = MLINE;
   }
   curPor->cursor.lnum = end_lnum;
   curPor->cursor.col = 0;

   return OK;
}

// Search quote char from string line[col].
// Quote character escaped by one of the characters in "escape" is not counted as a quote.
// Return column number of "quotechar" or -1 when not found.
private int
find_next_quote(
   CS line,
   int col,
   int  quotechar,
   Boole escapeWithBackslash   // does backslash escape the quote character?
){
   for (;;) {
      int c = line[col];
      if (c == ZERO)
          return -1;
      ei (escapeWithBackslash && c == '\\') {
         ++col;
         if (line[col] == ZERO)
            return -1;
      } ei (c == quotechar)
          break;
      col += utfCharLen(line + col);
   }
   return col;
}

// Search backwards in "line" from column "col_start" to find "quotechar".
// Quote character escaped by one of the characters in "escape" is not counted as a quote.
// Return the found column or zero.
private int
find_prev_quote(
   CS line,
   int col_start,
   int quotechar,
   Boole escapeWithBackslash   // does backslash escape the quote character?
){
   while (col_start > 0) {
      --col_start;
      col_start -= (*mb_head_off)(line, line + col_start);
      int n = 0;
      if (escapeWithBackslash) {
          while (col_start > n && line[col_start - n - 1] == '\\')
             ++n;
      } 
      if (n & 1)
         col_start -= n;   // uneven number of escape chars, skip it
      ei (line[col_start] == quotechar)
         break;
   }
   return col_start;
}

// Find quote under the cursor, cursor at end. Return true if found, else false.
pub int
current_quote(
   Operator* oper,
   long count,
   int include,   // true == include quote char
   int quotechar   // Quote character
){
   CS line = ml_get_curline();
   int      col_end;
   int      col_start = curPor->cursor.col;
   int      inclusive = false;
   int      vis_empty = true;   // Visual selection <= 1 char
   int      vis_bef_curs = false;   // Visual starts before cursor
   int      inside_quotes = false;   // Looks like "i'" done before
   int      selected_quote = false;   // Has quote inside selection
   int      i;

   // When 'selection' is "exclusive" move the cursor to where it would be
   // with 'selection' "inclusive", so that the logic is the same for both.
   // The cursor then is moved forward after adjusting the area.
   if (VIsual_active) {
      // this only works within one line
      if (VIsual.lnum != curPor->cursor.lnum)
          return false;

      vis_bef_curs = LT_POS(VIsual, curPor->cursor);
      vis_empty = EQUAL_POS(VIsual, curPor->cursor);
   }

   if (!vis_empty) {
      // Check if the existing selection exactly spans the text inside quotes.
      if (vis_bef_curs) {
          inside_quotes = VIsual.col > 0
            && line[VIsual.col - 1] == quotechar
            && line[curPor->cursor.col] != ZERO
            && line[curPor->cursor.col + 1] == quotechar;
          i = VIsual.col;
          col_end = curPor->cursor.col;
      } else {
          inside_quotes = curPor->cursor.col > 0
            && line[curPor->cursor.col - 1] == quotechar
            && line[VIsual.col] != ZERO
            && line[VIsual.col + 1] == quotechar;
          i = curPor->cursor.col;
          col_end = VIsual.col;
      }

      // Find out if we have a quote in the selection.
      while (i <= col_end) {
         // check for going over the end of the line, which can happen if
         // the line was changed after the Visual area was selected.
         if (line[i] == ZERO)
            break;
         if (line[i++] == quotechar) {
            selected_quote = true;
            break;
         }
      }
   }

   if (!vis_empty && line[col_start] == quotechar) {
      // Already selecting something and on a quote character. Find the next quoted string.
      if (vis_bef_curs) {
         // Assume we are on a closing quote: move to after the next opening quote.
         col_start = find_next_quote(line, col_start + 1, quotechar, false);
         if (col_start < 0)
            goto abort_search;
         col_end = find_next_quote(line, col_start + 1, quotechar, true);
         if (col_end < 0) {
            // We were on a starting quote perhaps?
            col_end = col_start;
            col_start = curPor->cursor.col;
         }
      } else {
         col_end = find_prev_quote(line, col_start, quotechar, false);
         if (line[col_end] != quotechar)
            goto abort_search;
         col_start = find_prev_quote(line, col_end, quotechar, true);
         if (line[col_start] != quotechar) {
            // We were on an ending quote perhaps?
            col_start = col_end;
            col_end = curPor->cursor.col;
         }
      }
   } ei (line[col_start] == quotechar || !vis_empty) {
      int firstCol = col_start;
      if (!vis_empty) {
         if (vis_bef_curs)
            firstCol = find_next_quote(line, col_start, quotechar, false);
         else
            firstCol = find_prev_quote(line, col_start, quotechar, false);
      }

      // The cursor is on a quote, we don't know if it's the opening or
      // closing quote.  Search from the start of the line to find out.
      // Also do this when there is a Visual area, a' may leave the cursor
      // in between two strings.
      col_start = 0;
      for (;;) {
         // Find open quote character.
         col_start = find_next_quote(line, col_start, quotechar, false);
         if (col_start < 0 || col_start > firstCol)
            goto abort_search;
         // Find close quote character.
         col_end = find_next_quote(line, col_start + 1, quotechar, true);
         if (col_end < 0)
            goto abort_search;
         // If is cursor between start and end quote character, it is target text object.
         if (col_start <= firstCol && firstCol <= col_end)
            break;
         col_start = col_end + 1;
      }
   } else {
      //Search backward for a starting quote.
      col_start = find_prev_quote(line, col_start, quotechar, true);
      if (line[col_start] != quotechar) {
         // No quote before the cursor, look after the cursor.
         col_start = find_next_quote(line, col_start, quotechar, false);
         if (col_start < 0)
            goto abort_search;
      }

      // Find close quote character.
      col_end = find_next_quote(line, col_start + 1, quotechar, true);
      if (col_end < 0)
          goto abort_search;
   }

   // When "include" is true, include spaces after closing quote or before the starting quote.
   if (include) {
      if (SPACE_OR_TAB(line[col_end + 1])) {
         while (SPACE_OR_TAB(line[col_end + 1]))
            ++col_end;
      } else {
         while (col_start > 0 && SPACE_OR_TAB(line[col_start - 1]))
            --col_start;
      } 
   }

   //Set start position.  After vi" another i" must include the ".
   //For v2i" include the quotes.
   if (!include && count < 2 && (vis_empty || !inside_quotes))
      ++col_start;
   curPor->cursor.col = col_start;
   if (VIsual_active) {
      // Set the start of the Visual area when the Visual area was empty, we
      // were just inside quotes or the Visual area didn't start at a quote
      // and didn't include a quote.
      if (vis_empty
         || (vis_bef_curs
             && !selected_quote
             && (inside_quotes
                  || (line[VIsual.col] != quotechar
                      && (VIsual.col == 0 || line[VIsual.col - 1] != quotechar))
                )
            )
      ) {
          VIsual = curPor->cursor;
          drawCurBookLater(UPD_INVERTED);
      }
   } else {
      oper->start = curPor->cursor;
      oper->motion_type = MCHAR;
   }

   // Set end position.
   curPor->cursor.col = col_end;
   if ((include || count > 1 // After vi" another i" must include the ".
            || (!vis_empty && inside_quotes)
         ) && inc_cursor() == 2)
      inclusive = true;
   if (VIsual_active) {
      if (vis_empty || vis_bef_curs) {
         dec_cursor();
      } else {
          // Cursor is at start of Visual area. Set the end of the Visual
          // area when it was just inside quotes or it didn't end at a quote.
          if (inside_quotes
             || (!selected_quote
               && line[VIsual.col] != quotechar
               && (line[VIsual.col] == ZERO
                   || line[VIsual.col + 1] != quotechar))
          ){
             dec_cursor();
             VIsual = curPor->cursor;
          }
          curPor->cursor.col = col_start;
      }
      if (VIsual_mode == 'V') {
          VIsual_mode = 'v';
          redrawCommlineG = true;      // show mode later
      }
   } else {
      // Set inclusive and other oper's flags.
      oper->inclusive = inclusive;
   }

   return OK;

abort_search:
   return false;
}

//}}}
//{{{common code

//Return true if in the current mode we need to use virtual.
pub int
virtual_active(void) {
   // While an operator is being executed we return "virtual_op", because
   // VIsual_active has already been reset, thus we can't check for "block" being used.
   if (virtual_op != MAYBE)
      return virtual_op;
   return VIsual_active && VIsual_mode == Ctrl_V;
}

//Return the dictionary of v:event. Save and clear the value in case it already has items.
pub Bag *
get_v_event(SaveVEvent *sve) {
   Bag* v_event = get_EeglVar_dict(VV_EVENT);

   if (v_event->hashTable.count > 0) {
      // recursive use of v:event, save, make empty and restore later
      sve->sve_did_save = true;
      sve->sve_hashtab = v_event->hashTable;
      hash_init(&v_event->hashTable);
   } else
      sve->sve_did_save = false;
   return v_event;
}

pub void
restore_v_event(Bag* v_event, SaveVEvent* sve) {
   dict_free_contents(v_event);
   if (sve->sve_did_save)
      v_event->hashTable = sve->sve_hashtab;
   else
      hash_init(&v_event->hashTable);
}

//Return the current mode as a string in "buf[MODE_MAX_LENGTH]", ZERO terminated.
//The first character represents the major mode, the following ones the minor ones.
pub void
get_mode(CS buf) {
   int i = 0;

   if (time_for_testing == 93784) {
      // Testing the two-character code.
      buf[i] = 'x';
      i++;
      buf[i] = '!';
      i++; 
   } ei (term_use_loop()) {
      if (stateG & MODE_COMMLINE) {
          buf[i] = 'c';
          i++;
      } 
      buf[i] = 't';
      i++;
   } ei (stateG == MODE_HITRETURN || stateG == MODE_ASKMORE
         || stateG == MODE_SETWSIZE || stateG == MODE_CONFIRM
   ) {
      buf[i] = 'r';
      i++;
      if (stateG == MODE_ASKMORE) {
         buf[i] = 'm';
         i++;
      } ei (stateG == MODE_CONFIRM) {
         buf[i] = '?';
         i++; 
      } 
   } ei (stateG == MODE_EXTERNCMD) {
      buf[i] = '!';
      i++;
   } ei (stateG & MODE_INSERT) {
      buf[i] = 'i';
      i++;
      if (ins_compl_active()) {
         buf[i] = 'c';
         i++;
      } ei (ctrl_x_mode_not_defined_yet()) {
         buf[i] = 'x';
         i++; 
      } 
   } ei (stateG & MODE_COMMLINE) {
      buf[i] = 'c';
      if ((stateG & MODE_COMMLINE) && cmdline_overstrike()) {
         buf[i] = 'r';
         i++;
      } 
   } ei (VIsual_active) {
      buf[i] = VIsual_mode;
      i++;
   } else {
      buf[i] = 'n';
      i++;
      if (finish_op) {
         buf[i] = 'o';
         i++;
         // to be able to detect force-linewise/blockwise/characterwise operations
         buf[i] = motion_force;
         i++; 
      } ei (restart_edit == 'I' || restart_edit == 'R' || restart_edit == 'V') {
         buf[i] = 'i';
         i++;
         buf[i] = restart_edit;
         i++;
      } ei (term_in_normal_mode()) {
         buf[i] = 't';
         i++;
      } 
   }

   buf[i] = ZERO;
}

//Fire a ModeChanged autocmd event if appropriate.
pub void
may_trigger_modechanged(void) {
   Bag* v_event;
   SaveVEvent  save_v_event;
   Byte curr_mode[MODE_MAX_LENGTH];
   Byte pattern_buf[2 * MODE_MAX_LENGTH];

   // Skip this when gotInterruptG is set, the autocommand will not be executed.
   // Better trigger it next time.
   if (!has_modechanged() || gotInterruptG)
      return;

   get_mode(curr_mode);
   if (STRCMP(curr_mode, last_mode) == 0)
      return;

   v_event = get_v_event(&save_v_event);
   (void)bagAddString(v_event, S"new_mode", curr_mode);
   (void)bagAddString(v_event, S"old_mode", last_mode);
   bagSetItemsRo(v_event);

   // concatenate modes in format "old_mode:new_mode"
   eeSnprintf(pattern_buf, sizeof(pattern_buf), "%s:%s", last_mode, curr_mode);

   applyAutocomms(EVENT_MODECHANGED, pattern_buf, NULL, false, curBook);
   STRCPY(last_mode, curr_mode);

   restore_v_event(v_event, &save_v_event);
}

// MODE_VISUAL, MODE_OP_PENDING stateG are never set, they are
// equal to MODE_NORMAL stateG with a condition. Return the real stateG.
pub int
get_real_state(void) {
   if (stateG & MODE_NORMAL) {
      if (VIsual_active) {
         return MODE_VISUAL;
      } ei (finish_op)
         return MODE_OP_PENDING;
   }
   return stateG;
}

// Search for an action in the actions table. Return -1 for invalid command.
private int
findAction(int actionChar) {
   int      i;
   int      idx;
   int      top, bot;
   int      c;
   // A multi-byte character is never a command.
   if (actionChar >= 0x100)
      return -1;

   // We use the absolute value of the character.  Special keys have a
   // negative value, but are sorted on their absolute value.
   if (actionChar < 0)
      actionChar = -actionChar;

   // If the character is in the first part: The character is the index into actionIndices[].
   if (actionChar <= actionsMaxLinear)
      return actionIndices[actionChar];

   // Perform a binary search.
   bot = actionsMaxLinear + 1;
   top = ACTIONS_SIZE - 1;
   idx = -1;
   while (bot <= top) {
      i = (top + bot) / 2;
      c = actions[actionIndices[i]].actionChar;
      if (c < 0)
         c = -c;
      if (actionChar == c) {
         idx = actionIndices[i];
         break;
      }
      if (actionChar > c)
         bot = i + 1;
      else
         top = i - 1;
   }

   return idx;
}

// If currently editing a cmdline or text is locked: beep and give an error message, return true.
private int
checkTextLocked(Operator* oper) {
   if (!text_locked())
      return false;

   if (oper)
      clearopbeep(oper);
   text_locked_msg();
   return true;
}

// If text is locked, "curBookLock" or "allBookLock" is set:
// Give an error message, possibly beep and return true. "oper" may be NULL.
pub int
check_text_or_curbuf_locked(Operator *oper) {
   if (checkTextLocked(oper))
      return true;

   if (!curBookLocked())
      return false;

   if (oper != NULL)
      clearop(oper);
   return true;
}

// Handle the count before a normal command and set aArg->count0.
private int
normalCmdGetCount(
   ActionArg* aArg,
   Unt c,
   int toplevel,
   int set_prevcount,
   OUT int* ctrl_w,
   OUT int* need_flushbuf
) {
getcount:
   // Handle a count before a command and compute ca.count0.
   // Note that '0' is a command and not the start of a count, though it is
   // part of a count if after other digits.
   while ((c >= '1' && c <= '9')
      || (aArg->count0 != 0 && (c == K_DEL || c == K_KDEL || c == '0'))
   ) {
      if (c == K_DEL || c == K_KDEL) {
         aArg->count0 /= 10;
         del_from_showcmd(4);   // delete the digit and ~@%
      } ei (aArg->count0 > 99999999L) {
         aArg->count0 = 999999999L;
      } else {
         aArg->count0 = aArg->count0 * 10 + (c - '0');
      }
      // Set v:count here, when called from main() and not a stuffed
      // command, so that v:count can be used in an expression mapping
      // right after the count. Do set it for redo.
      if (toplevel && readbuf1_empty())
         setVCountPrevCount(aArg, &set_prevcount);
      if (*ctrl_w) {
         ++no_mapping;
         ++allow_keys;      // no mapping for nchar, but keys
      }
      ++isZeroJustANumberG;      // don't map zero here
      c = plain_vgetc();
      LANGMAP_ADJUST(c, true);
      --isZeroJustANumberG;
      if (*ctrl_w) {
         --no_mapping;
         --allow_keys;
      }
      *need_flushbuf |= add_to_showcmd(c);
   }

   // If we got CTRL-W there may be a/another count
   if (c == Ctrl_W && !*ctrl_w && aArg->oper->opTy == OP_NOP) {
      *ctrl_w = true;
      aArg->opcount = aArg->count0;   // remember first count
      aArg->count0 = 0;
      ++no_mapping;
      ++allow_keys;      // no mapping for nchar, but keys
      c = plain_vgetc();      // get next character
      LANGMAP_ADJUST(c, true);
      --no_mapping;
      --allow_keys;
      *need_flushbuf |= add_to_showcmd(c);
      goto getcount;      // jump back
   }

   if (c == K_CURSORHOLD) {
       // Save the count values so that ca.opcount and ca.count0 are exactly
       // the same when coming back here after handling K_CURSORHOLD.
       aArg->oper->prev_opcount = aArg->opcount;
       aArg->oper->prev_count0 = aArg->count0;
   } ei (aArg->opcount != 0) {
      // If we're in the middle of an operator (including after entering a
      // yank buffer with '"') AND we had a count before the operator, then
      // that count overrides the current value of ca.count0.
      // What this means effectively, is that actions like "3dw" get turned
      // into "d3w" which makes things fall into place pretty neatly.
      // If you give a count before AND after the operator, they are multiplied.
      if (aArg->count0) {
         if (aArg->opcount >= 999999999L / aArg->count0)
            aArg->count0 = 999999999L;
         else
            aArg->count0 *= aArg->opcount;
      } else
         aArg->count0 = aArg->opcount;
   }

   // Always remember the count.  It will be set to zero (on the next call,
   // above) when there is no pending operator.
   // When called from main(), save the count for use by the "count" built-in variable.
   aArg->opcount = aArg->count0;
   aArg->count1 = (aArg->count0 == 0 ? 1 : aArg->count0);

   // Only set v:count when called from main() and not a stuffed action. Do set it for redo.
   if (toplevel && readbuf1_empty())
       set_vcount(aArg->count0, aArg->count1, set_prevcount);

   return c;
}

// Return true if the normal action (aArg) needs a second character.
private int
needsMoreChars(ActionArg* aArg, Short cmd_flags) {
   return ((cmd_flags & NV_NCH)
       && (((cmd_flags & NV_NCH_NOP) == NV_NCH_NOP && aArg->oper->opTy == OP_NOP)
            || (cmd_flags & NV_NCH_ALW) == NV_NCH_ALW
            || (aArg->cmdchar == 'q'
                && aArg->oper->opTy == OP_NOP
                && reg_recording == 0
                && reg_executing == 0)
            || ((aArg->cmdchar == 'a' || aArg->cmdchar == 'i')
                && (aArg->oper->opTy != OP_NOP || VIsual_active))
         )
   );
}

// Get one or more additional characters for a normal action.
// Return the updated action index (if changed).
private int
getMoreChars(
   int idx_arg,
   ActionArg* aArg,
   int* need_flushbuf
) {
   int idx = idx_arg;
   Unt c;
   Unt* cp;
   int lit = false;   // get extra character literally
   int langmap_active = false;    // using :lmap mappings
   int lang;      // getting a text character

   ++no_mapping;
   ++allow_keys;      // no mapping for nchar, but allow key codes
   // Don't generate a CursorHold event here, most actions can't handle
   // it, e.g., nv_replace(), nv_csearch().
   did_cursorhold = true;
   if (aArg->cmdchar == 'g') {
      // For 'g' get the next character now, so that we can check for "g'" and "g`".
      aArg->nchar = plain_vgetc();
      LANGMAP_ADJUST(aArg->nchar, true);
      *need_flushbuf |= add_to_showcmd(aArg->nchar);
      if (aArg->nchar == '\'' || aArg->nchar == '`' || aArg->nchar == Ctrl_BSL) {
         cp = &aArg->extra_char;   // need to get a third character
         lit = true;         // get it literally
      } else
         cp = NULL;      // no third character needed
   } else {
      cp = &aArg->nchar;
   }
   lang = (actions[idx].cmd_flags & NV_LANG);

   // Get a second or third character.
   if (cp != NULL) {
      if (lang && curBook->o.b_p_iminsert == B_IMODE_LMAP) {
         // Allow mappings defined with ":lmap".
         --no_mapping;
         --allow_keys;
         stateG = MODE_LANGMAP;
         langmap_active = true;
      }
      *cp = plain_vgetc();

      if (langmap_active) {
         // Undo the decrement done above
         ++no_mapping;
         ++allow_keys;
         stateG = MODE_NORMAL_BUSY;
      }
      stateG = MODE_NORMAL_BUSY;
      *need_flushbuf |= add_to_showcmd(*cp);

      if (!lit) {
         // adjust chars > 127, except after "tTfFr" actions
         LANGMAP_ADJUST(*cp, !lang);
      }

      //When the next character is CTRL-\ a following CTRL-N means the
      //action is aborted and we go to Normal mode.
      if (cp == &aArg->extra_char
         && aArg->nchar == Ctrl_BSL
         && (aArg->extra_char == Ctrl_N || aArg->extra_char == Ctrl_G)
      ){
         aArg->cmdchar = Ctrl_BSL;
         aArg->nchar = aArg->extra_char;
         idx = findAction(aArg->cmdchar);
      } ei ((aArg->nchar == 'n' || aArg->nchar == 'N') && aArg->cmdchar == 'g')
         aArg->oper->opTy = get_op_type(*cp, ZERO);
      ei (*cp == Ctrl_BSL) {
         long towait = (p_ttm >= 0 ? p_ttm : p_tm);

         // There is a busy wait here when typing "f<C-\>" and then
         // something different from CTRL-N.  Can't be avoided.
         while ((c = vpeekc()) <= 0 && towait > 0L) {
            do_sleep(towait > 50L ? 50L : towait, false);
            towait -= 50L;
          }
         if (c > 0) {
            c = plain_vgetc();
            if (c != Ctrl_N && c != Ctrl_G)
               vungetc(c);
            else {
               aArg->cmdchar = Ctrl_BSL;
               aArg->nchar = c;
               idx = findAction(aArg->cmdchar);
            }
          }
      }

      if (lang) {
          // When getting a text character and the next character is a
          // multi-byte character, it could be a composing character.
          // However, don't wait for it to arrive. Also, do enable mapping,
          // because if it's put back with vungetc() it's too late to apply mapping.
         --no_mapping;
         while ((c = vpeekc()) > 0 && (c >= 0x100 || utf8CharLens[vpeekc()] > 1)) {
            c = plain_vgetc();
            if (!utf_iscomposing(c)) {
               vungetc(c);      // it wasn't, put it back
               break;
            } ei (aArg->ncharC1 == 0)
               aArg->ncharC1 = c;
            else
               aArg->ncharC2 = c;
         }
         ++no_mapping;
         //Eegl may be in a different mode when the user types the next key, but when replaying 
         //a recording the next key is already in the typeahead buffer, so record an <Ignore> 
         //before that to prevent the vpeekc() above from applying wrong mappings when replaying.
         ++no_u_sync;
         gotchars_ignore();
         --no_u_sync;
      }
   }
   --no_mapping;
   --allow_keys;

   return idx;
}

// Return true if after processing a normal mode action, need to wait for a moment when a message
// is displayed that will be overwritten by the mode message.
private int
needToWaitForMsg(ActionArg* aArg, Pos *old_pos) {
   // In Visual mode and with "^O" in Insert mode, a short message will be
   // overwritten by the mode message.  Wait a bit, until a key is hit.
   // In Visual mode, it's more important to keep the Visual area updated
   // than keeping a message (e.g. from a /pat search).
   // Only do this if the command was typed, not from a mapping.
   // Don't wait when emsg_silent is non-zero.
   // Also wait a bit after an error message, e.g. for "^O:".
   // Don't redraw the screen, it would remove the message.
   return (       ((p_smd
          && msg_silent == 0
          && (restart_edit != 0
         || (VIsual_active
             && old_pos->lnum == curPor->cursor.lnum
             && old_pos->col == curPor->cursor.col)
             )
          && (mustClearCommlineG || redrawCommlineG)
          && (msg_didout || (msg_didany && msg_scroll))
          && !msg_nowait
          && keyWasTypedG)
      || (restart_edit != 0
          && !VIsual_active
          && (msg_scroll
         || emsg_on_display)))
       && aArg->oper->regname == 0
       && !(aArg->retval & CA_COMMAND_BUSY)
       && stuff_empty()
       && typebuf_typed()
       && emsg_silent == 0
       && !in_assert_fails
       && !did_wait_return
       && aArg->oper->opTy == OP_NOP);
}

// After processing a normal mode command, wait for a moment when a message is
// displayed that will be overwritten by the mode message.
private void
waitForMsg(void) {
   int   save_State = stateG;

   // Draw the cursor with the right shape here
   if (restart_edit != 0)
      stateG = MODE_INSERT;

   // If need to redraw, and there is a "msgAfterRedrawG", redraw before the delay
   if (mustRedrawG && msgAfterRedrawG != NULL && !emsg_on_display) {
      CS kmsg = msgAfterRedrawG;
      msgAfterRedrawG = NULL;
      // Showmode() will clear msgAfterRedrawG, but we want to use it anyway. First update topLine
      setcursor();
      drawUpdateScreen(0);
      // now reset it, otherwise it's put in the history again
      msgAfterRedrawG = kmsg;

      kmsg = copyStr(msgAfterRedrawG);
      msgDeco(kmsg, decoAfterRedrawG);
      eeglFree(kmsg);
   }
   setcursor();
   ui_cursor_shape();      // may show different cursor shape
   cursor_on();
   out_flush();
   if (msg_scroll || emsg_on_display)
      ui_delay(1003L, true);   // wait at least one second
   ui_delay(3003L, false);      // wait up to three seconds
   stateG = save_State;

   msg_scroll = false;
   emsg_on_display = false;
}

//Execute an action in Normal mode.
pub void
normalAction(Operator* oper, Boole toplevel) { // true when called from main()
   ActionArg action;
   Unt c;
   int ctrl_w = false;      // got CTRL-W command
   int old_col = curPor->cursWant;
   int need_flushbuf = false;   // need to call out_flush()
   Pos old_pos;      // cursor position before command
   int mapped_len;
   static int   old_mapped_len = 0;
   int idx;
   int set_prevcount = false;
   int save_did_cursorhold = did_cursorhold;

   CLEAR_FIELD(action);   // also resets action.retval
   action.oper = oper;

   // Use a count remembered from before entering an operator.  After typing
   // "3d" we return from normalAction() and come back here, the "3" is remembered in "opcount".
   action.opcount = opcount;

   // If there is an operator pending, then the command we take this time
   // will terminate it. Finish_op tells us to finish the operation before
   // returning this time (unless the operation was cancelled).
   Boole finishOpSaved = finish_op;
   finish_op = (oper->opTy != OP_NOP);
   if (finish_op != finishOpSaved) {
      ui_cursor_shape();      // may show different cursor shape
   }
   may_trigger_modechanged();

   // When not finishing an operator and no register name typed, reset the count.
   if (!finish_op && !oper->regname) {
      action.opcount = 0;
      set_prevcount = true;
   }

   // Restore counts from before receiving K_CURSORHOLD.  This means after
   // typing "3", handling K_CURSORHOLD and then typing "2" we get "32", not "3 * 2".
   if (oper->prev_opcount > 0 || oper->prev_count0 > 0) {
      action.opcount = oper->prev_opcount;
      action.count0 = oper->prev_count0;
      oper->prev_opcount = 0;
      oper->prev_count0 = 0;
   }

   mapped_len = typebuf_maplen();

   stateG = MODE_NORMAL_BUSY;

  // Set v:count here, when called from main() and not a stuffed command, so that v:count can be
  // used in an expression mapping  when there is no count. Do set it for redo.
  if (toplevel && readbuf1_empty())
      setVCountPrevCount(&action, &set_prevcount);

   // Get the command character from the user.
   c = safe_vgetc();
   LANGMAP_ADJUST(c, true);

   // If a mapping was started in Visual or Select mode, remember the length
   // of the mapping.  This is used below to not return to Insert mode for as
   // long as the mapping is being executed.
   if (restart_edit == 0)
      old_mapped_len = 0;
   ei (old_mapped_len || (VIsual_active && mapped_len == 0 && typebuf_maplen() > 0))
      old_mapped_len = typebuf_maplen();

   if (c == ZERO)
      c = K_ZERO;

   // If the portal was made so small that nothing shows, make it at least one
   // line and one column when typing a command.
   if (keyWasTypedG && !keyWasStuffedG)
      portEnsureSize();

   need_flushbuf = add_to_showcmd(c);

   // Get the command count
   c = normalCmdGetCount(&action, c, toplevel, set_prevcount, &ctrl_w, &need_flushbuf);

   // Find the command character in the table of actions.
   // For CTRL-W we already got nchar when looking for a count.
   if (ctrl_w) {
      action.nchar = c;
      action.cmdchar = Ctrl_W;
   } else
      action.cmdchar = c;

   idx = findAction(action.cmdchar);

   if (idx < 0) {
      // Not a known command: beep.
      clearopbeep(oper);
      goto normal_end;
   }

   if ((actions[idx].cmd_flags & NV_NCW) && check_text_or_curbuf_locked(oper))
       // this command is not allowed now
       goto normal_end;

   // In Visual/Select mode, a few keys are handled in a special way.
   if (VIsual_active) {
         // may stop Select/Visual mode
         if ((actions[idx].cmd_flags & NV_STS) != 0 && !(modMaskG & MOD_MASK_SHIFT)) {
            end_visual_mode();
            drawCurBookLater(UPD_INVERTED);
         }

         // Keys that work different when 'keymodel' contains "startsel"
         if ((actions[idx].cmd_flags & NV_SS) != 0) {
            unshift_special(&action);
            idx = findAction(action.cmdchar);
            if (idx < 0) {
               // Just in case
               clearopbeep(oper);
               goto normal_end;
            }
         } ei ((actions[idx].cmd_flags & NV_SSS) && (modMaskG & MOD_MASK_SHIFT))
         { modMaskG &= ~MOD_MASK_SHIFT; }
   }

   // Get additional characters if we need them.
   if (needsMoreChars(&action, actions[idx].cmd_flags))
      idx = getMoreChars(idx, &action, &need_flushbuf);

   // Flush the showcmd characters onto the screen so we can see them while
   // the command is being executed.  Only do this when the shown command was
   // actually displayed, otherwise this will slow down a lot when executing mappings.
   if (need_flushbuf)
       out_flush();
   if (action.cmdchar != K_IGNORE) {
       if (ex_normal_busy)
         did_cursorhold = save_did_cursorhold;
       else
         did_cursorhold = false;
   }

   stateG = MODE_NORMAL;
    
   if (action.nchar == ESC || action.extra_char == ESC) {
      clearop(oper);
      goto normal_end;
   }

   if (action.cmdchar != K_IGNORE) {
      msg_didout = false;    // don't scroll screen up for normal command
      msgColG = 0;
   }

   old_pos = curPor->cursor;      // remember where the cursor was

   // Some keys start Select/Visual mode.
   if (!VIsual_active) {
      if ((actions[idx].cmd_flags & NV_SS) != 0) {
         start_selection();
         unshift_special(&action);
         idx = findAction(action.cmdchar);
      } ei ((actions[idx].cmd_flags & NV_SSS) && (modMaskG & MOD_MASK_SHIFT)) {
         start_selection();
         modMaskG &= ~MOD_MASK_SHIFT;
      }
   }

   // Execute the action! Call the action function found in the actions table.
   action.arg = actions[idx].cmd_arg;
   (actions[idx].fn)(&action);

   // If we didn't start or finish an operator, reset oper->regname, unless we need it later. 
   if (!finish_op && !oper->opTy && (idx < 0 || !(actions[idx].cmd_flags & NV_KEEPREG))) {
      clearop(oper);
      reset_reg_var();
   }

   // Get the length of mapped chars again after typing a count, second character or "z333<cr>".
   if (old_mapped_len > 0)
      old_mapped_len = typebuf_maplen();

   // If an operation is pending, handle it.  But not for K_IGNORE or K_MOUSEMOVE.
   if (action.cmdchar != K_IGNORE && action.cmdchar != K_MOUSEMOVE)
      doExecuteVisualOperator(&action, old_col, false);

   // Wait for a moment when a message is displayed that will be overwritten by the mode message.
   if (needToWaitForMsg(&action, &old_pos))
      waitForMsg();

   // Finish up after executing a Normal mode action.
normal_end:

   msg_nowait = false;

   if (finish_op)
      reset_reg_var();

   int prev_finish_op = finish_op;
   if (oper->opTy == OP_NOP) {
       // Reset finish_op, in case it was set
       finish_op = false;
       may_trigger_modechanged();
   }
   // Redraw the cursor with another shape, if we were in Operator-pending
   // mode or did a replace action.
   if (prev_finish_op || action.cmdchar == 'r' || (action.cmdchar == 'g' && action.nchar == 'r')) {
      ui_cursor_shape();      // may show different cursor shape
   }

   if (oper->opTy == OP_NOP && oper->regname == 0 && action.cmdchar != K_CURSORHOLD)
      clear_showcmd();

   checkpcmark();      // check if we moved since setting pcmark
   eeglFree(action.searchbuf);

   mb_adjust_cursor();

   if (curPor->o.diff && toplevel) {
      validate_cursor();   // may need to update leftCol
      normPostProcessScrollbind(true);
      validate_cursor();   // may need to update leftCol
      do_check_cursorbind();
   }

   // don't go to Insert mode if a terminal has a running job
   if (term_job_running(curBook->term))
      restart_edit = 0;

   // May restart edit(), if we got here with CTRL-O in Insert mode (but not
   // if still inside a mapping that started in Visual mode).
   // May switch from Visual to Select mode after CTRL-O action.
   if (oper->opTy == OP_NOP
       && (restart_edit != 0 && !VIsual_active && old_mapped_len == 0)
       && !(action.retval & CA_COMMAND_BUSY)
       && stuff_empty()
       && oper->regname == 0
   ) {
      if (restart_edit != 0 && !VIsual_active && old_mapped_len == 0)
         (void)edit(restart_edit, false, 1L);
   }

   // Save count before an operator for next time.
   opcount = action.opcount;
}

// Set v:count and v:count1 according to "aArg".
// Set v:prevcount only when "set_prevcount" is true.
private void
setVCountPrevCount(ActionArg* aArg, int *set_prevcount) {
   long count = aArg->count0;

   // multiply with aArg->opcount the same way as above
   if (aArg->opcount != 0)
      count = aArg->opcount * (count == 0 ? 1 : count);
   set_vcount(count, count == 0 ? 1 : count, *set_prevcount);
   *set_prevcount = false;  // only set v:prevcount once
}

// Check if highlighting for Visual mode is possible, give a warning message if not.
pub void
check_visual_highlight(void) {
   static Boole did_check = false;
   if (fullScreenG) {
      if (!did_check && getDecoFlags(HLF_V) == 0)
         msg(_("Warning: terminal cannot highlight"));
      did_check = true;
   }
}

// Call yank_do_autocmd() for "regname".
private void
callYankDoAutocmd(int regname) {
   Operator   oper;
   doClearOpArg(&oper);
   oper.regname = regname;
   oper.opTy = OP_YANK;
   oper.is_VIsual = true;
   YankReg* reg = get_register(regname, true);
   yank_do_autocmd(&oper, reg);
   free_register(reg);
}

// End Visual mode.
// This or next function should ALWAYS be called to end Visual mode, except from doExecuteVisualOperator()
pub void
end_visual_mode(void) {
   VIsual_select_exclu_adj = false;
   end_visual_mode_keep_button();
   reset_held_button();
}

pub void
end_visual_mode_keep_button(void) {
   // Emit a TextYankPost for the automatic copy of the selection into the star and/or plus register
   if (has_textyankpost()) {
       callYankDoAutocmd('*');
   }

   VIsual_active = false;
   setmouse();
   mouseDraggingG = 0;

   // Save the current VIsual area for '< and '> marks, and "gv"
   curBook->visual = (VisualInfo){
      .vi_mode = VIsual_mode, .vi_start = VIsual, .vi_end = curPor->cursor, 
      .vi_curswant = curPor->cursWant, .kind = VIsual_mode
   };
   if (!virtual_active())
      curPor->cursor.coladd = 0;
   may_clear_cmdline();

   adjust_cursor_eol();
   may_trigger_modechanged();
}

// Reset VIsual_active and VIsual_reselect.
pub void
reset_VIsual_and_resel(void) {
   if (VIsual_active) {
      end_visual_mode();
      drawCurBookLater(UPD_INVERTED);   // delete the inversion later
   }
   VIsual_reselect = false;
}

// Reset VIsual_active and VIsual_reselect if it's set.
pub void
reset_VIsual(void) {
   if (VIsual_active) {
      end_visual_mode();
      drawCurBookLater(UPD_INVERTED);   // delete the inversion later
      VIsual_reselect = false;
   }
}

pub void
restore_visual_mode(void) {
   if (VIsual_mode_orig != ZERO) {
       curBook->visual.vi_mode = VIsual_mode_orig;
       VIsual_mode_orig = ZERO;
   }
}

//Check for a balloon-eval special item to include when searching for an identifier.  When "dir" 
//is BACKWARD "ptr[-1]" must be valid! Return true if the character at "*ptr" should be included.
//"dir" is FORWARD or BACKWARD, the direction of searching. "*colp" is in/decremented if 
//"ptr[-dir]" should also be included. "bnp" points to a counter for square brackets.
private int
checkIsBalloonItem(CS ptr, int* colp, int* bnp, int dir){
   // Accept everything inside [].
   if ((*ptr == ']' && dir == BACKWARD) || (*ptr == '[' && dir == FORWARD))
      ++*bnp;
   if (*bnp > 0) {
      if ((*ptr == '[' && dir == BACKWARD) || (*ptr == ']' && dir == FORWARD))
         --*bnp;
      return true;
   }

   // skip over "s.var"
   if (*ptr == '.')
      return true;

   // two-character item: s->var
   if (ptr[dir == BACKWARD ? 0 : 1] == '>' && ptr[dir == BACKWARD ? -1 : 0] == '-') {
      *colp += dir;
      return true;
   }
   return false;
}


pub
#define FIND_IDENT   1 //find identifier (keyword)
#define FIND_STRING  2 //find any non-whitespace text
#define FIND_EVAL    4 //include "->", "[]" and "." (useful for C program debugging)
#define FIND_NOERROR 8 //no error when no word found

// Find the identifier under or to the right of the cursor.
// "find_type" can have one of three values:
// FIND_IDENT:   find an identifier (keyword)
// FIND_STRING:  find any non-white text
// FIND_IDENT + FIND_STRING: find any non-white text, identifier preferred.
// FIND_EVAL:    find text useful for C program debugging
//
// There are three steps:
// 1. Search forward for the start of an identifier/text.  Doesn't move if
//    already on one.
// 2. Search backward for the start of this identifier/text.
//    This doesn't match the real Vi but I like it a little better and it
//    shouldn't bother anyone.
// 3. Search forward to the end of this identifier/text.
//    When FIND_IDENT isn't defined, we backup until a blank.
//
// Return the length of the text, or zero if no text is found.
// If text is found, a pointer to the text is put in "*text".  This
// points into the current buffer line and is not always ZERO terminated.
pub int
find_ident_under_cursor(OUT CS* text, int find_type) {
    return find_ident_at_pos(curPor, curPor->cursor.lnum,
            curPor->cursor.col, text, NULL, find_type);
}

// Like find_ident_under_cursor(), but for any portal and any position.
// However: Uses @iskeyword from the current portal.
pub int
find_ident_at_pos(
   Portal* po,
   LineNr lnum,
   ColNr startcol,
   OUT CS* text,
   int* textcol,   // column where "text" starts, can be NULL
   int find_type
) {
   int col = 0;   // init to shut up GCC
   int i;
   int this_class = 0;
   int prev_class;
   int prevcol;
   int bn = 0;      // bracket nesting

   // if i == 0: try to find an identifier
   // if i == 1: try to find any non-white text
   CS ptr = memGetLine(po->book, lnum, false);
   for (i = (find_type & FIND_IDENT) ? 0 : 1;   i < 2; ++i) {
      // 1. skip to start of identifier/text
      col = startcol;
      while (ptr[col] != ZERO) {
         // Stop at a ']' to evaluate "a[x]".
         if ((find_type & FIND_EVAL) && ptr[col] == ']')
            break;
         this_class = mb_get_class(ptr + col);
         if (this_class != 0 && (i == 1 || this_class != 1))
            break;
         col += utfCharLen(ptr + col);
      }

      // When starting on a ']' count it, so that we include the '['.
      bn = ptr[col] == ']';

      // 2. Back up to start of identifier/text.
      // Remember class of character under cursor.
      if ((find_type & FIND_EVAL) && ptr[col] == ']')
         this_class = mb_get_class((CS)"a");
      else
         this_class = mb_get_class(ptr + col);
      while (col > 0 && this_class != 0) {
         prevcol = col - 1 - (*mb_head_off)(ptr, ptr + col - 1);
         prev_class = mb_get_class(ptr + prevcol);
         if (this_class != prev_class
            && (i == 0
                || prev_class == 0
                || (find_type & FIND_IDENT))
            && (!(find_type & FIND_EVAL)
                || prevcol == 0
                || !checkIsBalloonItem(ptr + prevcol, &prevcol, &bn, BACKWARD))
         )
            break;
         col = prevcol;
      }

      // If we don't want just any old text, or we've found an identifier, stop searching.
      if (this_class > 2)
         this_class = 2;
      if (!(find_type & FIND_STRING) || this_class == 2)
         break;
   }

   if (ptr[col] == ZERO || (i == 0 && (this_class != 2 ))) {
      // didn't find an identifier or text
      if ((find_type & FIND_NOERROR) == 0) {
         if (find_type & FIND_STRING)
            emsg(_(e_no_string_under_cursor));
         else
            emsg(_(e_no_identifier_under_cursor));
      }
      return 0;
   }
   ptr += col;
   *text = ptr;
   if (textcol != NULL)
      *textcol = col;

   //3. Find the end if the identifier/text.
   bn = 0;
   startcol -= col;
   col = 0;
   // Search for point of changing multibyte character class.
   this_class = mb_get_class(ptr);
   while (ptr[col] != ZERO
	  && ((i == 0 ? mb_get_class(ptr + col) == this_class
          : mb_get_class(ptr + col) != 0)
            || ((find_type & FIND_EVAL)
           && col <= (int)startcol
           && checkIsBalloonItem(ptr + col, &col, &bn, FORWARD))
	  )
   ) {
	   col += utfCharLen(ptr + col);
   }
   return col;
}

// Prepare for redo of a normal action.
private void
prepareForRedo(ActionArg* aArg) {
   prep_redo(aArg->oper->regname, aArg->count0, ZERO, aArg->cmdchar, ZERO, ZERO, aArg->nchar);
}

// Prepare for redo of any action.
// Note that only the last argument can be a multi-byte char.
pub void
prep_redo(
   int regname,
   long num,
   int cmd1,
   int cmd2,
   int cmd3,
   int cmd4,
   int cmd5
){
   prep_redo_num2(regname, num, cmd1, cmd2, 0L, cmd3, cmd4, cmd5);
}

// Prepare for redo of any action with extra count after "cmd2".
pub void
prep_redo_num2(
    int       regname,
    long    num1,
    int       cmd1,
    int       cmd2,
    long    num2,
    int       cmd3,
    int       cmd4,
    int       cmd5)
{
   ResetRedobuff();

   // Put info about a mapping in the redo buffer, so that "." will use the
   // same script context.
   may_add_last_used_map_to_redobuff();

   if (regname != 0) {  // yank from specified buffer
      AppendCharToRedobuff('"');
      AppendCharToRedobuff(regname);
   }
   if (num1 != 0)
      inpAppendNumberToRedoBuff(num1);
   if (cmd1 != ZERO)
      AppendCharToRedobuff(cmd1);
   if (cmd2 != ZERO)
      AppendCharToRedobuff(cmd2);
   if (num2 != 0)
      inpAppendNumberToRedoBuff(num2);
   if (cmd3 != ZERO)
      AppendCharToRedobuff(cmd3);
   if (cmd4 != ZERO)
      AppendCharToRedobuff(cmd4);
   if (cmd5 != ZERO)
      AppendCharToRedobuff(cmd5);
}

// Check for operator active and clear it.
private int
checkclearop(Operator* oper) {
   if (oper->opTy == OP_NOP)
      return false;
   clearopbeep(oper);
      return true;
}

// Check for operator or Visual active.  Clear active operator.
private int
checkclearopq(Operator* oper) {
   if (oper->opTy == OP_NOP && !VIsual_active)
      return false;
   clearopbeep(oper);
   return true;
}

pub void
clearop(Operator *oper) {
   oper->opTy = OP_NOP;
   oper->regname = 0;
   oper->motion_force = ZERO;
   oper->use_reg_one = false;
   motion_force = ZERO;
}

pub void
clearopbeep(Operator *oper) {
   clearop(oper);
   inpFlushIfNotSilent();
}

//Remove the shift modifier from a special key.
private void
unshift_special(ActionArg* aArg) {
   switch (aArg->cmdchar) {
   case K_S_RIGHT:   aArg->cmdchar = K_RIGHT; break;
   case K_S_LEFT:   aArg->cmdchar = K_LEFT; break;
   case K_S_UP:   aArg->cmdchar = K_UP; break;
   case K_S_DOWN:   aArg->cmdchar = K_DOWN; break;
   case K_S_HOME:   aArg->cmdchar = K_HOME; break;
   case K_S_END:   aArg->cmdchar = K_END; break;
   }
   aArg->cmdchar = simplify_key(aArg->cmdchar, &modMaskG);
}

// If the mode is currently displayed clear the command line or update the command displayed.
pub void
may_clear_cmdline(void) {
   if (isModeDisplayedG)
      mustClearCommlineG = true;   // unshow visual mode later
   else
      clear_showcmd();
}

// Routines for displaying a partly typed command

private Byte old_showcmd_buf[SHOWCMD_BUFLEN];  // For push_showcmd()
private int showcmd_is_clear = true;
private int showcmd_visual = false;

private void display_showcmd(void);

pub void
clear_showcmd(void) {
   if (VIsual_active && !char_avail()) {
      int      cursor_bot = LT_POS(VIsual, curPor->cursor);
      long      lines;
      ColNr      leftcol, rightcol;
      LineNr   top, bot;

      // Show the size of the Visual area.
      if (cursor_bot) {
         top = VIsual.lnum;
         bot = curPor->cursor.lnum;
      } else {
         top = curPor->cursor.lnum;
         bot = VIsual.lnum;
      }
      // Include closed folds as a whole.
      (void)getFolds(top, &top, NULL);
      (void)getFolds(bot, NULL, &bot);
      lines = bot - top + 1;

      if (VIsual_mode == Ctrl_V) {
         CS saved_sbr = p_sbr;

         // Make 'sbr' empty for a moment to get the correct size.
         p_sbr = null;
         getvcols(curPor, &curPor->cursor, &VIsual, &leftcol, &rightcol);
         p_sbr = saved_sbr;
         sprintf((char *)showcmd_buf, "%ldx%ld", lines, (long)(rightcol - leftcol + 1));
      } ei (VIsual_mode == 'V' || VIsual.lnum != curPor->cursor.lnum)
         sprintf((char *)showcmd_buf, "%ld", lines);
      else {
         int       l;
         int       bytes = 0;
         int       chars = 0;

         CS s;
         CS e;
         if (cursor_bot) {
            s = ml_get_pos(&VIsual);
            e = ml_get_cursor();
         } else {
            s = ml_get_cursor();
            e = ml_get_pos(&VIsual);
         }
         while (s <= e) {
            l = utfCharLen(s);
            if (l == 0) {
               ++bytes;
               ++chars;
               break;  // end of line
            }
            bytes += l;
            ++chars;
            s += l;
         }
         if (bytes == chars)
            sprintf((char *)showcmd_buf, "%d", chars);
         else
            sprintf((char *)showcmd_buf, "%d-%d", chars, bytes);
      }
      showcmd_buf[SHOWCMD_COLS] = ZERO;   // truncate
      showcmd_visual = true;
   } else {
      showcmd_buf[0] = ZERO;
      showcmd_visual = false;

      // Don't actually display something if there is nothing to clear.
      if (showcmd_is_clear)
         return;
   }

   display_showcmd();
}

// Add 'c' to string of shown action chars.
// Return true if output has been written (and setcursor() has been called).
pub int
add_to_showcmd(Unt c) {
   CS p;
   int old_len;
   int extra_len;
   int overflow;
   int i;
   Byte mbyte_buf[MB_MAXBYTES];
   static Unt ignore[] = {
      K_IGNORE, K_PS,
      K_LEFTMOUSE, K_LEFTDRAG, K_LEFTRELEASE, K_MOUSEMOVE,
      K_MIDDLEMOUSE, K_MIDDLEDRAG, K_MIDDLERELEASE,
      K_RIGHTMOUSE, K_RIGHTDRAG, K_RIGHTRELEASE,
      K_MOUSEDOWN, K_MOUSEUP, K_MOUSELEFT, K_MOUSERIGHT,
      K_X1MOUSE, K_X1DRAG, K_X1RELEASE, K_X2MOUSE, K_X2DRAG, K_X2RELEASE,
      K_CURSORHOLD,
      0
   };

   if (msg_silent != 0)
      return false;

   if (showcmd_visual) {
      showcmd_buf[0] = ZERO;
      showcmd_visual = false;
   }

   // Ignore keys that are scrollbar updates and mouse clicks
   if (IS_SPECIAL(c)) {
      for (i = 0; ignore[i] != 0; ++i) {
         if (ignore[i] == c)
            return false;
      } 
   }

   if (c <= 0x7f || !bookIsCharPrintable(c)) {
      p = transchar(c);
      if (*p == ' ')
          STRCPY(p, "<20>");
   } else {
      mbyte_buf[(*mb_char2bytes)(c, mbyte_buf)] = ZERO;
      p = mbyte_buf;
   }
   old_len = (int)STRLEN(showcmd_buf);
   extra_len = (int)STRLEN(p);
   overflow = old_len + extra_len - SHOWCMD_COLS;
   if (overflow > 0)
      MEMMOVE(showcmd_buf, showcmd_buf + overflow, old_len - overflow + 1);
   STRCAT(showcmd_buf, p);

   if (char_avail())
      return false;

   display_showcmd();

   return true;
}

pub void
add_to_showcmd_c(Unt c) {
   if (!add_to_showcmd(c))
   setcursor();
}

// Delete 'len' characters from the end of the shown action.
private void
del_from_showcmd(int len) {
   int old_len = (int)STRLEN(showcmd_buf);
   if (len > old_len)
      len = old_len;
   showcmd_buf[old_len - len] = ZERO;

   if (!char_avail())
      display_showcmd();
}

//push_showcmd() and pop_showcmd() are used when waiting for the user to type
//something and there is a partial mapping.
pub void
push_showcmd(void) {
   STRCPY(old_showcmd_buf, showcmd_buf);
}

pub void
pop_showcmd(void) {
   STRCPY(showcmd_buf, old_showcmd_buf);
   display_showcmd();
}

private void
display_showcmd(void) {
   int len = eeglStrSize(showcmd_buf);

   showcmd_is_clear = (len == 0);
   cursor_off();

   if (p_sloc == SHOW_COMM_STATUSLINE) {
      if (showcmd_is_clear)
         curPor->statusLineNeedsRedraw = true;
      else
         redrawPortalStatusLine(curPor, false);
   } else { // @showcmdloc is "last" or empty
      if (!showcmd_is_clear)
         drawText(showcmd_buf, (int)visibleRowsG - 1, shownCommandColG, 0);

      //clear the rest of an old message by outputting up to SHOWCMD_COLS spaces
      drawText(S"          " + len, (int)visibleRowsG - 1, shownCommandColG + len, 0);
   }

    setcursor();       // put cursor back where it belongs
}

// When "check" is false, prepare for actions that scroll the portal.
// When "check" is true, take care of scroll-binding after the portal has
// scrolled.  Called from normal_cmd() and edit().
pub void
normPostProcessScrollbind(int check) {
   static Portal* old_curPor = NULL;
   static LineNr old_topline = 0;
   static int old_topfill = 0;
   static Book* old_buf = NULL;
   static ColNr old_leftcol = 0;

   if (check && curPor->o.diff) {
      // If the ":syncbind" command was just used, don't scroll, only reset the values.
      if (did_syncbind)
          did_syncbind = false;
      ei (curPor == old_curPor) {
         //Synchronize other portals, as necessary according to
         //@diff. Don't do this after an ":edit" command, except when @diff is set.
         if ((curPor->book == old_buf || curPor->o.diff)
           && (curPor->topLine != old_topline
              || curPor->topFill != old_topfill
              || curPor->leftCol != old_leftcol)
         ) {
            check_scrollbind(curPor->topLine - old_topline, (long)(curPor->leftCol - old_leftcol));
         }
      } ei ((p_sbo & SCR_JUMP) != 0) {// jump flag set in @scrollopt
          //When switching between portals, make sure that the relative vertical offset is valid 
          //for the new portal. The relative offset is invalid whenever another scrollbound portal
          //has scrolled to a point that would force the current portal to scroll past the 
          //beginning or end of its buffer. When the resync is performed, some of the other 
          //scrollbound portals may need to jump so that the current portal's relative position is
          //visible on-screen.
          check_scrollbind(curPor->topLine - curPor->scbindPos, 0L);
      }
      curPor->scbindPos = curPor->topLine;
   }

   old_curPor = curPor;
   old_topline = curPor->topLine;
   old_topfill = curPor->topFill;
   old_buf = curPor->book;
   old_leftcol = curPor->leftCol;
}

// Synchronize any portals that have diff set, based on the
// number of rows by which the current portal has changed
// (1998-11-02 16:21:01  R. Edward Ralston <eralston@computer.org>)
pub void
check_scrollbind(LineNr topline_diff, long leftcol_diff) {
   Portal   *old_curPor = curPor;
   Book   *oldCurBook = curBook;
   int      old_VIsual_active = VIsual_active;
   ColNr   tgt_leftcol = curPor->leftCol;
   long   topline;
   long   y;

   // check @scrollopt string for vertical and horizontal scroll options
   Boole want_ver = ((p_sbo & SCR_VER) != 0 && topline_diff != 0);
   want_ver |= old_curPor->o.diff;
   Boole want_hor = ((p_sbo & SCR_HOR) != 0 && (leftcol_diff || topline_diff != 0));

   // loop through the scrollbound portals and scroll accordingly
   VIsual_active = 0;
   FOR_ALL_PORTALS(curPor) {
      curBook = curPor->book;
      // skip original portal and portals without scrollbind
      if (curPor == old_curPor || !curPor->o.diff)
          continue;

      // do the vertical scroll
      if (want_ver)    {
         if (old_curPor->o.diff && curPor->o.diff) {
            diff_set_topline(old_curPor, curPor);
         } else {
            curPor->scbindPos += topline_diff;
            topline = curPor->scbindPos;
            if (topline > curBook->mem.lineCount)
               topline = curBook->mem.lineCount;
            if (topline < 1)
               topline = 1;

            y = topline - curPor->topLine;
            if (y > 0)
               scrollup(y, false);
            else
               scrolldown(-y, false);
         }

         redraw_later(UPD_VALID);
         cursor_correct();
         curPor->statusLineNeedsRedraw = true;
      }

      // do the horizontal scroll
      if (want_hor)
         (void)set_leftcol(tgt_leftcol);
   }

   // reset current portal
   VIsual_active = old_VIsual_active;
   curPor = old_curPor;
   curBook = oldCurBook;
}


// Return true if line[offset] is not inside a C-style comment or string, false otherwise.
private Boole
isIdent(CS line, int offset) {
   Boole incomment = false;
   int   instring = 0;
   int   prev = 0;

   for (int i = 0; i < offset && line[i] != ZERO; i++) {
      if (instring != 0) {
         if (prev != '\\' && line[i] == instring)
            instring = 0;
      } ei ((line[i] == '"' || line[i] == '\'') && !incomment) {
          instring = line[i];
      } else {
         if (incomment) {
            if (prev == '*' && line[i] == '/')
               incomment = false;
         } ei (prev == '/' && line[i] == '*') {
            incomment = true;
         } ei (prev == '/' && line[i] == '/') {
            return false;
         }
      }

      prev = line[i];
   }

   return incomment == false && instring == 0;
}

//Search for variable declaration of "ptr[len]".
//When "locally" is true in the current function ("gd"), otherwise in the
//current file ("gD").
//When "thisblock" is true check the {} block scope. Return FAIL when not found.
pub int
find_decl(
   CS ptr,
   int len,
   int locally,
   int thisblock,
   Unt flags_arg   // flags passed to searchit()
){
   Pos old_pos;
   Pos par_pos;
   Pos found_pos;
   int t;
   int save_p_scs;
   int retval = OK;
   Unt searchflags = flags_arg;

   CS pat = alloc(len + 7);

   // Put "\V" before the pattern to avoid that the special meaning of "."
   // and "~" causes trouble.
   Unt patlen = eeSnprintf(pat, len + 7, eeIsWordPtr(ptr) ? "\\V\\<%.*s\\>" : "\\V%.*s", len, ptr);

   old_pos = curPor->cursor;
   save_p_scs = p_scs;
   wrapSearchG = false;   // don't wrap around end of file now
   p_scs = false;   // don't switch ignorecase off now

   //With "gD" go to line 1.
   //With "gd" Search back for the start of the current function, then go
   //back until a blank line.  If this fails go to line 1.
   Boole incll;
   if (!locally || !normFindNextParagraf(OUT &incll, BACKWARD, 1L, '{', false)) {
      setpcmark();         // Set in normFindNextParagraf() otherwise
      curPor->cursor.lnum = 1;
      par_pos = curPor->cursor;
   } else {
      par_pos = curPor->cursor;
      while (curPor->cursor.lnum > 1 && *skipwhite(ml_get_curline()) != ZERO)
         --curPor->cursor.lnum;
   }
   curPor->cursor.col = 0;

   // Search forward for the identifier, ignore comment lines.
   CLEAR_POS(&found_pos);
   for (;;) {
      t = searchit(curPor, curBook, &curPor->cursor, NULL, FORWARD,
                    (Text){pat, patlen}, 1L, searchflags, RE_LAST, NULL);
      if (curPor->cursor.lnum >= old_pos.lnum)
          t = FAIL;   // match after start is failure too

      if (thisblock && t != FAIL) {
          Pos   *pos;

          // Check that the block the match is in doesn't end before the
          // position where we started the search from.
          if ((pos = findmatchlimit(NULL, '}', FM_FORWARD,
              (int)(old_pos.lnum - curPor->cursor.lnum + 1))) != NULL
             && pos->lnum < old_pos.lnum)
          {
         // There can't be a useful match before the end of this block.
         // Skip to the end.
         curPor->cursor = *pos;
         continue;
          }
      }

      if (t == FAIL) {
         // If we previously found a valid position, use it.
         if (found_pos.lnum != 0) {
            curPor->cursor = found_pos;
            t = OK;
         }
         break;
      }
      if (get_leader_len(ml_get_curline(), NULL, false, true) > 0) {
         // Ignore this line, continue at start of next line.
         ++curPor->cursor.lnum;
         curPor->cursor.col = 0;
         continue;
      }
      Boole valid = isIdent(ml_get_curline(), curPor->cursor.col);

      // If the current position is not a valid identifier and a previous
      // match is present, favor that one instead.
      if (!valid && found_pos.lnum != 0) {
         curPor->cursor = found_pos;
         break;
      }

      // Global search: use first valid match found
      if (valid && !locally)
         break;
      if (valid && curPor->cursor.lnum >= par_pos.lnum) {
         // If we previously found a valid position, use it.
         if (found_pos.lnum != 0)
            curPor->cursor = found_pos;
         break;
      }

      //For finding a local variable and the match is before the "{" or
      //inside a comment, continue searching.  For K&R style function
      //declarations this skips the function header without types.
      if (!valid)
         CLEAR_POS(&found_pos);
      else
         found_pos = curPor->cursor;
      //Remove SEARCH_START from flags to avoid getting stuck at one position.
      searchflags &= ~SEARCH_START;
   }

   if (t == FAIL) {
      retval = FAIL;
      curPor->cursor = old_pos;
   } else {
      curPor->setCursWant = true;
      // "n" searches forward now
      reset_search_dir();
   }

   eeglFree(pat);
   wrapSearchG = true;
   p_scs = save_p_scs;

   return retval;
}

// Get visually selected text, within one line only. Return FAIL if more than one line selected.
pub int
get_visual_text(
   ActionArg* aArg,
   OUT CS* pp,       // return: start of selected text
   OUT int* lenp       // return: length of selected text
){
   if (VIsual.lnum != curPor->cursor.lnum) {
      if (aArg)
         clearopbeep(aArg->oper);
      return FAIL;
   }
   if (VIsual_mode == 'V') {
      *pp = ml_get_curline();
      *lenp = (int)ml_get_curline_len();
   } else {
      if (LT_POS(curPor->cursor, VIsual)) {
         *pp = ml_get_pos(&curPor->cursor);
         *lenp = VIsual.col - curPor->cursor.col + 1;
      } else {
         *pp = ml_get_pos(&VIsual);
         *lenp = curPor->cursor.col - VIsual.col + 1;
      }
      if (**pp == ZERO)
         *lenp = 0;
      if (*lenp > 0) {
         // Correct the length to include all bytes of the last
         // character.
         *lenp += utfCharLen(*pp + (*lenp - 1)) - 1;
      }
   }
   reset_VIsual_and_resel();
   return OK;
}

// Search for "pat" in direction "dir" ('/' or '?', 0 for repeat).
// Uses only aArg->count1 and aArg->oper from "aArg".
// Return 0 for failure, 1 for found, 2 for found and line offset added.
private int
normal_search(
   ActionArg* aArg,
   int dir,
   Text pat,
   int opt,      // extra flags for do_search()
   int* wrapped
) {
   int      i;
   SearchitArg sia;
   Pos   prev_cursor = curPor->cursor;

   aArg->oper->motion_type = MCHAR;
   aArg->oper->inclusive = false;
   aArg->oper->use_reg_one = true;
   curPor->setCursWant = true;

   CLEAR_FIELD(sia);
   i = do_search(aArg->oper, dir, dir, pat, aArg->count1,
             opt | SEARCH_OPT | SEARCH_ECHO | SEARCH_MSG, &sia);
   if (wrapped != NULL)
      *wrapped = sia.sa_wrapped;
   if (i == 0)
      clearop(aArg->oper);
   else {
      if (i == 2)
         aArg->oper->motion_type = MLINE;
      curPor->cursor.coladd = 0;
      if (aArg->oper->opTy == OP_NOP && (p_fdo & FDO_SEARCH) && keyWasTypedG)
         foldOpenCursor();
   }
   // Redraw the portal to refresh the hilited matches.
   if (!EQUAL_POS(curPor->cursor, prev_cursor) && p_hls && hiliteSearchG)
      redraw_later(UPD_SOME_VALID);

    // "/$" will put the cursor after the end of the line, may need to
    // correct that here
    check_cursor();

    return i;
}

// Start selection for Shift-movement keys.
pub void
start_selection(void) {
   // if 'selectmode' contains "key", start Select mode
   n_start_visual_mode('v');
}

// Used after a movement action: If the cursor ends up on the ZERO after the end of the line,
// may move it back to the last character and make the motion inclusive.
private void
adjust_cursor(Operator *oper) {
   // The cursor cannot remain on the ZERO when:
   // - the column is > 0
   // - not in Visual mode or 'selection' is "o"
   // - 'virtualedit' is not "all" and not "onemore".
   if (curPor->cursor.col > 0 && gchar_cursor() == ZERO && (!VIsual_active) && !virtual_active()) {
      --curPor->cursor.col;
      // prevent cursor from moving on the trail byte
      mb_adjust_cursor();
      oper->inclusive = true;
   }
}

//Move position "*pp" back one character for 'selection' == "exclusive".
//Return true when backed up to the previous line.
pub int
unadjust_for_sel_inner(Pos *pp) {
   ColNr   cs, ce;
   VIsual_select_exclu_adj = false;

   if (pp->coladd > 0)
      --pp->coladd;
   ei (pp->col > 0)  {
      --pp->col;
      mb_adjustpos(curBook, pp);
      if (virtual_active()) {
         getvcol(curPor, pp, &cs, NULL, &ce);
         pp->coladd = ce - cs;
      }
   } ei (pp->lnum > 1) {
      --pp->lnum;
      pp->col = ml_get_len(pp->lnum);
      return true;
   }

   return false;
}

// Move the cursor for the "A" action.
pub void
set_cursor_for_append_to_line(void) {
   curPor->setCursWant = true;
   curPor->cursor.col += (ColNr)STRLEN(ml_get_cursor());
}

// Invoke edit() and take care of "restart_edit" and the return value.
private void
invokeEdit(
   ActionArg* aArg,
   int repl,      // "r" action
   int cmd,
   int startln
){
   //Complicated: When the user types "a<C-O>a" we don't want to do Insert
   //mode recursively.  But when doing "a<C-O>." or "a<C-O>rx" we do allow it.
   int restartEditSaved = (repl || !stuff_empty()) ? restart_edit : 0;

   // Always reset "restart_edit", this is not a restarted edit.
   restart_edit = 0;

   //Reset Changedtick_i, so that TextChangedI will only be triggered for stuff
   //from insert mode, for 'o/O' this has already been done in nOpenAction
   if (aArg->cmdchar != 'O' && aArg->cmdchar != 'o')
      curBook->lastChangeTickInsert = CHANGEDTICK(curBook);
   if (edit(cmd, startln, aArg->count1))
      aArg->retval |= CA_COMMAND_BUSY;

   if (restart_edit == 0)
      restart_edit = restartEditSaved;
}

// Find the next illegal byte sequence.
private void
utf_find_illegal(void) {
   Pos pos = curPor->cursor;
   CS p;
   CS tofree = NULL;

   curPor->cursor.coladd = 0;
   for (;;) {
      p = ml_get_cursor();
      while (*p != ZERO){
         // Illegal means that there are not enough trail bytes (checked by
         // utf_ptr2len()) or too many of them (overlong sequence).
         Unt len = utf_ptr2len(p);
         if (*p >= 0x80 && (len == 1 || mb_char2len(mb_ptr2char(p)) != len)) {
            curPor->cursor.col += (ColNr)(p - ml_get_cursor());
            goto theend;
         }
         p += len;
      }
      if (curPor->cursor.lnum == curBook->mem.lineCount)
         break;
      ++curPor->cursor.lnum;
      curPor->cursor.col = 0;
   }

   // didn't find it: don't move and beep
   curPor->cursor = pos;
   inpFlushIfNotSilent();

theend:
   eeglFree(tofree);
}

//Return true if any book has changes. Also books that haven't been written.
private Boole
wasAnyBookChanged(void) {
   Book* book;
   FOR_ALL_BOOKS(book) {
      if (bookWasChanged(book))
         return true;
   } 
   return false;
}


//}}}
//{{{normal and visual mode actions

// Command character that's ignored.
// Used for CTRL-Q and CTRL-S to avoid problems with terminals that use xon/xoff.
private void
nv_ignore(ActionArg* aArg) {
   aArg->retval |= CA_COMMAND_BUSY;   // don't call edit() now
}

// Action character that doesn't do anything, but unlike nv_ignore() does
// start edit().  Used for "startinsert" executed while starting up.
private void
nv_nop(ActionArg*) {
}

// Action character doesn't exist.
private void
nvError(ActionArg* aArg) {
   clearopbeep(aArg->oper);
}

// <Help> and <F1> actions.
private void
nv_help(ActionArg* aArg) {
   if (!checkclearopq(aArg->oper))
      c_help(NULL);
}

// CTRL-A and CTRL-X: Add or subtract from letter or number under cursor.
private void
nvAddSub(ActionArg* aArg) {
   if (bt_prompt(curBook) && !prompt_curpos_editable())
       clearopbeep(aArg->oper);
   ei (!VIsual_active && aArg->oper->opTy == OP_NOP) {
      prepareForRedo(aArg);
      aArg->oper->opTy = aArg->cmdchar == Ctrl_A ?  OP_ADD : OP_SUB;
      op_addsub(aArg->oper, aArg->count1, aArg->arg);
      aArg->oper->opTy = OP_NOP;
   } ei (VIsual_active)
      nv_operator(aArg);
   else
      clearop(aArg->oper);
}

// CTRL-F, CTRL-B, etc: Scroll page up or down.
private void
nvPage(ActionArg* aArg) {
   if (checkclearop(aArg->oper))
       return;

   if (modMaskG & MOD_MASK_CTRL) {
      // <C-PageUp>: tab page back; <C-PageDown>: tab page forward
      if (aArg->arg == BACKWARD)
         gotoTabById(-(int)aArg->count1);
      else
         gotoTabById((int)aArg->count0);
   } else {
       (void)pagescroll(aArg->arg, aArg->count1, false);
   } 
}

// Implementation of "gd" and "gD" action.
private void
nv_gd(Operator* oper, int nchar, int      thisblock) {  // 1 for "1gd" and "1gD"
   int len;
   CS ptr;

   if ((len = find_ident_under_cursor(OUT &ptr, FIND_IDENT)) == 0
       || find_decl(ptr, len, nchar == 'd', thisblock, SEARCH_START) == FAIL
   ) {
      clearopbeep(oper);
      return;
   }

   if ((p_fdo & FDO_SEARCH) && keyWasTypedG && oper->opTy == OP_NOP)
      foldOpenCursor();
   // clear any search statistics
   if (messaging() && !msg_silent)
      mustClearCommlineG = true;
}

// Move 'dist' lines in direction 'dir', counting lines by *screen*
// lines rather than lines in the file.
// 'dist' must be positive.
//
// Return OK if able to move cursor, FAIL otherwise.
pub int
nv_screengo(Operator* oper, Unt dir, long dist) {
   int linelen = linetabsize_no_outer(curPor, curPor->cursor.lnum);
   int retval = OK;
   int atend = false;

   oper->motion_type = MCHAR;
   oper->inclusive = (curPor->cursWant == MAXCOL);

   int col_off1 = normalPortalColumnOffset(curPor); //margin offset for first screen line
   int width1 = curPor->width - col_off1; //text width for first screen line
   int width2 = curPor->width - col_off1; //text width for wrapped screen line
   if (width2 == 0)
      width2 = 1; // avoid divide by zero

   if (curPor->width != 0) {
      // Instead of sticking at the last character of the buffer line we
      // try to stick in the last column of the screen.
      if (curPor->cursWant == MAXCOL) {
         atend = true;
         validate_virtcol();
         if (width1 <= 0)
             curPor->cursWant = 0;
         else {
            curPor->cursWant = width1 - 1;
            if (curPor->virtCol > curPor->cursWant)
               curPor->cursWant += ((curPor->virtCol - curPor->cursWant - 1) / width2 + 1) * width2;
         }
      } else {
         int n = (linelen > width1) 
            ? ((linelen - width1 - 1) / width2 + 1) * width2 + width1 :  width1;
         if (curPor->cursWant >= (ColNr)n)
            curPor->cursWant = n - 1;
      }

      while (dist--) {
         if (dir == BACKWARD)    {
            if ((long)curPor->cursWant >= width1
                && !getFolds(curPor->cursor.lnum, NULL, NULL)
            )
               // Move back within the line. This can give a negative value
               // for cursWant if width1 < width2 (with cpoptions+=n),
               // which will get clipped to column 0.
               curPor->cursWant -= width2;
            else {
               // to previous line
               if (curPor->cursor.lnum <= 1) {
                  retval = FAIL;
                  break;
               }
               cursor_up_inner(curPor, 1);

               linelen = linetabsize_no_outer(curPor, curPor->cursor.lnum);
               if (linelen > width1)
                  curPor->cursWant += (((linelen - width1 - 1) / width2) + 1) * width2;
            }
         } else { // dir == FORWARD
            int n = (linelen > width1) 
               ? ((linelen - width1 - 1) / width2 + 1) * width2 + width1 : width1;
            if (curPor->cursWant + width2 < (ColNr)n && !getFolds(curPor->cursor.lnum, NULL, NULL))
               // move forward within line
               curPor->cursWant += width2;
            else {
               // to next line
               if (curPor->cursor.lnum >= curPor->book->mem.lineCount) {
                  retval = FAIL;
                  break;
               }
               cursor_down_inner(curPor, 1);
               curPor->cursWant %= width2;

               //Check if the cursor has moved below the number display when width1 < width2 
               //(with cpoptions+=n). Subtract width2 to get a negative value for cursWant, which 
               //will get clipped to column 0.
               if (curPor->cursWant >= width1)
                  curPor->cursWant -= width2;
               linelen = linetabsize_no_outer(curPor, curPor->cursor.lnum);
            }
         }
      }
   }

   if (virtual_active() && atend)
      coladvance(MAXCOL);
   else
      coladvance(curPor->cursWant);

   if (curPor->cursor.col > 0 && curPor->o.wrap) {
      //Check for landing on a character that got split at the end of the
      //last line. We want to advance a screenline, not end up in the same
      //screenline or move two screenlines.
      validate_virtcol();
      ColNr virtcol = curPor->virtCol;
      if (virtcol > (ColNr)width1 && p_sbr)
         virtcol -= eeglStrSize(p_sbr);

      Unt c = mb_ptr2char(ml_get_cursor());
      if (dir == FORWARD && virtcol < curPor->cursWant
            && (curPor->cursWant <= (ColNr)width1)
            && !bookIsCharPrintable(c) && c > 255
      )
         oneright();

      if (virtcol > curPor->cursWant
         && (curPor->cursWant < (ColNr)width1
             ? (curPor->cursWant > (ColNr)width1 / 2)
             : ((curPor->cursWant - width1) % width2 > (ColNr)width2 / 2))
      )
         --curPor->cursor.col;
   }

   if (atend)
      curPor->cursWant = MAXCOL;       // stick in the last column
   adjust_skipcol();

   return retval;
}

// Handle CTRL-E and CTRL-Y actions: scroll a line up or down. aArg->arg must be true for CTRL-E.
pub void
nv_scroll_line(ActionArg* aArg) {
   if (!checkclearop(aArg->oper))
      scroll_redraw(aArg->arg, aArg->count1);
}

// For overflow detection, add a digit safely to a long value.
private int
appendDigitLong(OUT Long* value, int digit) {
   Long x = *value;
   if (x > (Long)((LONG_MAX - (long)digit) / 10))
      return FAIL;
   *value = x * 10 + (long)digit;
   return OK;
}

private int
widthLeft(Portal* po) {
   return po->width - normalPortalColumnOffset(po);
} 

// Get the count specified after a 'z' action. Only the 'z<CR>', 'zl', 'zh', 'z<Left>', and 
// 'z<Right>' commands accept a count after 'z'. Return true to process the 'z' command and 
// false to skip it.
private int
nv_z_get_count(ActionArg* aArg, Unt* nchar_arg) {
   // "z123{nchar}": edit the count before obtaining {nchar}
   if (checkclearop(aArg->oper))
      return false;
      
   Unt nchar = *nchar_arg;
   Long n = (Long)nchar - '0';

   for (;;) {
      ++no_mapping;
      ++allow_keys;   // no mapping for nchar, but allow key codes
      nchar = plain_vgetc();
      LANGMAP_ADJUST(nchar, true);
      --no_mapping;
      --allow_keys;
      (void)add_to_showcmd(nchar);

      if (nchar == K_DEL || nchar == K_KDEL)
         n /= 10;
      ei (EE_ISDIGIT(nchar)) {
         if (appendDigitLong(OUT &n, nchar - '0') == FAIL) {
            clearopbeep(aArg->oper);
            break;
         }
      } ei (nchar == ENTER)  {
         portSetHeight((int)n, curPor);
         break;
      } ei (nchar == 'l' || nchar == 'h' || nchar == K_LEFT || nchar == K_RIGHT) {
         aArg->count1 = n ? n * aArg->count1 : aArg->count1;
         *nchar_arg = nchar;
         return true;
      } else {
          clearopbeep(aArg->oper);
          break;
      }
   }
   aArg->oper->opTy = OP_NOP;
   return false;
}

// Actions that start with "z".
private void
nv_zet(ActionArg* aArg) {
   long   n;
   ColNr   col;
   Unt      nchar = aArg->nchar;
   long   old_fdl = curPor->o.foldLevel;
   int      old_fen = curPor->o.foldEnable;
   long   siso = get_sidescrolloff_value();

   if (EE_ISDIGIT(nchar) && !nv_z_get_count(aArg, &nchar))
       return;

   if (
          // "zf" and "zF" are always an operator, "zd", "zo", "zO", "zc" and "zC" only in Visual 
          // mode. "zj" and "zk" are motion actions.
          aArg->nchar != 'f' && aArg->nchar != 'F'
          && !(VIsual_active && firstOccurrence((CS)"dcCoO", aArg->nchar))
          && aArg->nchar != 'j' && aArg->nchar != 'k'
          &&
          checkclearop(aArg->oper))
      return;

    // For "z+", "z<CR>", "zt", "z.", "zz", "z^", "z-", "zb":
    // If line number given, set cursor.
   if ((firstOccurrence((CS)"+\r\nt.z^-b", nchar) != NULL)
       && aArg->count0
       && aArg->count0 != curPor->cursor.lnum
   ) {
      setpcmark();
      if (aArg->count0 > curBook->mem.lineCount)
         curPor->cursor.lnum = curBook->mem.lineCount;
      else
         curPor->cursor.lnum = aArg->count0;
      check_cursor_col();
   }

   switch (nchar) {
   // "z+", "z<CR>" and "zt": put cursor at top of screen
   case '+':
      if (aArg->count0 == 0) {
         // No count given: put cursor at the line below screen
         validate_botline();   // make sure bottomLine is valid
         if (curPor->bottomLine > curBook->mem.lineCount)
            curPor->cursor.lnum = curBook->mem.lineCount;
         else
            curPor->cursor.lnum = curPor->bottomLine;
      }
      // FALLTHROUGH
   case NL:
   case ENTER:
   case K_KENTER:
      beginline(BL_WHITE | BL_FIX);
      // FALLTHROUGH

   case 't':  
      scroll_cursor_top(0, true);
      redraw_later(UPD_VALID);
      set_fraction(curPor);
      break;

   // "z." and "zz": put cursor in middle of screen
   case '.':   beginline(BL_WHITE | BL_FIX);
      // FALLTHROUGH

   case 'z':   scroll_cursor_halfway(true, false);
      redraw_later(UPD_VALID);
      set_fraction(curPor);
      break;

   // "z^", "z-" and "zb": put cursor at bottom of screen
   case '^':   // Strange Vi behavior: <count>z^ finds line at top of portal
      // when <count> is at bottom of portal, and puts that one at bottom of portal.
      if (aArg->count0 != 0) {
         scroll_cursor_bot(0, true);
         curPor->cursor.lnum = curPor->topLine;
      }
      ei (curPor->topLine == 1)
         curPor->cursor.lnum = 1;
      else
         curPor->cursor.lnum = curPor->topLine - 1;
   // FALLTHROUGH
   case '-':
      beginline(BL_WHITE | BL_FIX);
      // FALLTHROUGH

   case 'b':   scroll_cursor_bot(0, true);
      redraw_later(UPD_VALID);
      set_fraction(curPor);
      break;

   // "zH" - scroll screen right half-page
   case 'H':
      aArg->count1 *= curPor->width / 2;
      // FALLTHROUGH

   // "zh" - scroll screen to the right
   case 'h':
   case K_LEFT:
      if (!curPor->o.wrap)
          (void)set_leftcol((ColNr)aArg->count1 > curPor->leftCol
                ? 0 : curPor->leftCol - (ColNr)aArg->count1);
      break;

   // "zL" - scroll portal left half-page
   case 'L':   aArg->count1 *= curPor->width / 2;
      // FALLTHROUGH

   // "zl" - scroll portal to the left if not wrapping
   case 'l':
   case K_RIGHT:
      if (!curPor->o.wrap)
         (void)set_leftcol(curPor->leftCol + (ColNr)aArg->count1);
      break;

   // "zs" - scroll screen, cursor at the start
   case 's':   
      if (!curPor->o.wrap)       {
         if (getFolds(curPor->cursor.lnum, NULL, NULL))
            col = 0;   // like the cursor is in col 0
         else
            getvcol(curPor, &curPor->cursor, &col, NULL, NULL);
         if ((long)col > siso)
            col -= siso;
         else
            col = 0;
         if (curPor->leftCol != col) {
            curPor->leftCol = col;
            redraw_later(UPD_NOT_VALID);
         }
      }
      break;

      // "ze" - scroll screen, cursor at the end
    case 'e':   
      if (!curPor->o.wrap) {
         if (getFolds(curPor->cursor.lnum, NULL, NULL))
            col = 0;   // like the cursor is in col 0
         else
            getvcol(curPor, &curPor->cursor, NULL, NULL, &col);
         n = widthLeft(curPor);
         if ((long)col + siso < n)
            col = 0;
         else
            col = col + siso - n + 1;
         if (curPor->leftCol != col) {
            curPor->leftCol = col;
            redraw_later(UPD_NOT_VALID);
         }
      }
      break;

      // "zp", "zP" in block mode put without adding trailing spaces
    case 'P':
    case 'p':  
      nv_put(aArg);
      break;
      // "zy" Yank without trailing spaces
    case 'y':  nv_operator(aArg);
          break;
      // "zF": create fold action
      // "zf": create fold operator
   case 'F':
   case 'f':   
      if (foldManualAllowed(true)) {
         aArg->nchar = 'f';
         nv_operator(aArg);
         curPor->o.foldEnable = true;

         // "zF" is like "zfzf"
         if (nchar == 'F' && aArg->oper->opTy == OP_FOLD) {
            nv_operator(aArg);
            finish_op = true;
         }
      }
      else
          clearopbeep(aArg->oper);
      break;

      // "zd": delete fold at cursor
      // "zD": delete fold at cursor recursively
   case 'd':
   case 'D':   
      if (foldManualAllowed(false)) {
         if (VIsual_active)
            nv_operator(aArg);
         else
            deleteFold(curPor->cursor.lnum, curPor->cursor.lnum, nchar == 'D', false);
      }
      break;

   // "zE": erase all folds
   case 'E':   
      if (curPor->o.foldMethod == FOLD_MARKER && curPor->o.foldMarker)
         deleteFold((LineNr)1, curBook->mem.lineCount, true, false);
      else
         emsg(_(e_cannot_erase_folds_with_current_foldmethod));
      break;

   // "zn": fold none: reset @foldenable
   case 'n':   
      curPor->o.foldEnable = false;
      break;

   // "zN": fold Normal: set @foldenable
   case 'N':   curPor->o.foldEnable = true;
      break;

   // "zi": invert folding: toggle @foldenable
   case 'i':   curPor->o.foldEnable = !curPor->o.foldEnable;
      break;

   // "za": open closed fold or close open fold at cursor
   case 'a':   
      if (getFolds(curPor->cursor.lnum, NULL, NULL))
         openFold(curPor->cursor.lnum, aArg->count1);
      else {
         closeFold(curPor->cursor.lnum, aArg->count1);
         curPor->o.foldEnable = true;
      }
      break;

      // "zA": open fold at cursor recursively
   case 'A':   
      if (getFolds(curPor->cursor.lnum, NULL, NULL))
         openFoldRecurse(curPor->cursor.lnum);
      else {
         closeFoldRecurse(curPor->cursor.lnum);
         curPor->o.foldEnable = true;
      }
      break;

      // "zo": open fold at cursor or Visual area
    case 'o':   
      if (VIsual_active)
         nv_operator(aArg);
      else
         openFold(curPor->cursor.lnum, aArg->count1);
      break;

      // "zO": open fold recursively
   case 'O':   
      if (VIsual_active)
         nv_operator(aArg);
      else
         openFoldRecurse(curPor->cursor.lnum);
      break;

      // "zc": close fold at cursor or Visual area
    case 'c':   
      if (VIsual_active)
         nv_operator(aArg);
      else
         closeFold(curPor->cursor.lnum, aArg->count1);
      curPor->o.foldEnable = true;
      break;

      // "zC": close fold recursively
    case 'C':   
      if (VIsual_active)
         nv_operator(aArg);
      else
         closeFoldRecurse(curPor->cursor.lnum);
      curPor->o.foldEnable = true;
      break;

      // "zv": open folds at the cursor
    case 'v':   
      foldOpenCursor();
      break;

      // "zx": re-apply 'foldlevel' and open folds at the cursor
    case 'x':   
      curPor->o.foldEnable = true;
      curPor->foldNeedsRecomputation = true;   // recompute folds
      newFoldLevel();         // update right now
      foldOpenCursor();
      break;

   // "zX": undo manual opens/closes, re-apply 'foldlevel'
   case 'X':   
      curPor->o.foldEnable = true;
      curPor->foldNeedsRecomputation = true;   // recompute folds
      old_fdl = -1;         // force an update
      break;

   // "zm": fold more
   case 'm':  
      if (curPor->o.foldLevel > 0) {
         curPor->o.foldLevel -= aArg->count1;
         if (curPor->o.foldLevel < 0)
            curPor->o.foldLevel = 0;
      }
      old_fdl = -1;      // force an update
      curPor->o.foldEnable = true;
      break;

   // "zM": close all folds
   case 'M':   curPor->o.foldLevel = 0;
      old_fdl = -1;      // force an update
      curPor->o.foldEnable = true;
      break;

      // "zr": reduce folding
   case 'r':   {
         curPor->o.foldLevel += aArg->count1;
         int d = getDeepestNesting();

         if (curPor->o.foldLevel >= d)
            curPor->o.foldLevel = d;
      }
      break;

      // "zR": open all folds
   case 'R':   curPor->o.foldLevel = getDeepestNesting();
      old_fdl = -1;      // force an update
      break;

   case 'j':   // "zj" move to next fold downwards
   case 'k':   // "zk" move to next fold upwards
      if (foldMoveTo(true, nchar == 'j' ? FORWARD : BACKWARD, aArg->count1) == FAIL)
         clearopbeep(aArg->oper);
      break;

   default:   clearopbeep(aArg->oper);
   }

   // Redraw when 'foldenable' changed
   if (old_fen != curPor->o.foldEnable)     {
      Portal* po;

      if (curPor->o.foldMethod == FOLD_DIFF && curPor->o.diff) {
         // Adjust @foldenable in diff-synced portals.
         FOR_ALL_PORTALS(po) {
            if (po != curPor && po->o.foldMethod == FOLD_DIFF && po->o.diff) {
                po->o.foldEnable = curPor->o.foldEnable;
                didChangePortalSetting(po);
            }
         }
      }
      didChangePortalSettingCurPor();
   }

   // Redraw when @foldlevel changed.
   if (old_fdl != curPor->o.foldLevel)
      newFoldLevel();
}

// Handle a ":" action.
private void
nv_colon(ActionArg* aArg) {
   Boole isCmdkey = aArg->cmdchar == K_COMMAND || aArg->cmdchar == K_SCRIPT_COMMAND;

   if (VIsual_active && !isCmdkey) {
      nv_operator(aArg);
      return;
   }

   if (aArg->oper->opTy != OP_NOP) {
      // Using ":" as a movement is characterwise exclusive.
      aArg->oper->motion_type = MCHAR;
      aArg->oper->inclusive = false;
   } ei (aArg->count0 && !isCmdkey) {
      // translate "count:" into ":.,.+(count - 1)"
      stuffcharReadbuff('.');
      if (aArg->count0 > 1) {
         stuffReadbuff((CS)",.+");
         stuffnumReadbuff((long)aArg->count0 - 1L);
      }
   }

   // When typing, don't type below an old message
   if (keyWasTypedG)
      compute_cmdrow();

   // get a command line and execute it
   int flags = aArg->oper->opTy != OP_NOP ? DOCMD_KEEPLINE : 0;
   
   int commResult = (isCmdkey) 
      ? do_cmdkey_command(aArg->cmdchar, flags) : doCommand(NULL, scrGetTypedCommand, NULL, flags);

   if (commResult == FAIL) {
      // The command failed, do not execute the operator.
      clearop(aArg->oper);
   } ei (aArg->oper->opTy != OP_NOP
          && (aArg->oper->start.lnum > curBook->mem.lineCount
            || aArg->oper->start.col > ml_get_len(aArg->oper->start.lnum)
            || anyEmsgG)
   ) {
      // The start of the operator has become invalid by the command.
      clearopbeep(aArg->oper);
   } 
}

// Handle CTRL-G action.
private void
nv_ctrlg(ActionArg* aArg) {
   if (VIsual_active) {   // toggle Selection/Visual mode
   } ei (!checkclearop(aArg->oper)) {
       // print full name if count given or :cd used
       fileinfo((int)aArg->count0, false, true);
   } 
}

// Handle CTRL-H <Backspace> action.
private void
nv_ctrlh(ActionArg* aArg) {
   nv_left(aArg);
}

// CTRL-L: clear screen and redraw.
private void
nv_clear(ActionArg* aArg) {
   if (checkclearop(aArg->oper))
      return;

   // Clear all syntax states to force resyncing.
   synFreeBlock(curPor->ownSyntax);
   {
   Portal *po;
   FOR_ALL_PORTALS(po)
      po->ownSyntax->redrawTime = false;
   }
   redraw_later(UPD_CLEAR);
}

//CTRL-O: In Select mode: switch to Visual mode for one action. Otherwise: Go to older pcmark.
private void
nv_ctrlo(ActionArg* aArg) {
   aArg->count1 = -aArg->count1;
   nv_pcmark(aArg);
}

//CTRL-^ command, short for ":e #".  Works even when the alternate book is not named.
private void
nv_hat(ActionArg* aArg) {
   if (!checkclearopq(aArg->oper)) {
      (void)booklistGetFile((int)aArg->count0, (LineNr)0, GETF_SETMARK|GETF_ALT, false);
   }
}

// "Z" commands.
private void
nv_Zet(ActionArg* aArg) {
   if (checkclearopq(aArg->oper))
      return;

   switch (aArg->nchar) {
   // "ZZ": equivalent to ":x".
   case 'Z':   
      executeCommLine(S"x");
      break;

   // "ZQ": equivalent to ":q!" (Elvis compatible).
   case 'Q':   
      executeCommLine((CS)"q!");
      break;

   default:   
      clearopbeep(aArg->oper);
   }
}

// Call nv_ident() as if "c1" was used, with "c2" as next character.
pub void
do_nv_ident(int c1, int c2) {
    Operator   oa;
    ActionArg   ca;

    doClearOpArg(&oa);
    CLEAR_FIELD(ca);
    ca.oper = &oa;
    ca.cmdchar = c1;
    ca.nchar = c2;
    nv_ident(&ca);
}

// 'K' normal-mode command. Get the command to lookup the keyword under the cursor.
private int
nv_K_getcmd(
   ActionArg* aArg,
   CS kp,
   int kp_help,
   int kp_ex,
   Byte** ptr_arg,
   int n,
   CS buffer,
   Unt bufsize,
   Unt* buflen
) {
   CS ptr = *ptr_arg;
   int isman;
   int isman_s;

   if (kp_help) {
      // in the help buffer
      STRCPY(buffer, "he! ");
      *buflen = STRLEN_LITERAL("he! ");
      return n;
   }

   if (kp_ex) {
      // @keywordprog is a command
      if (aArg->count0 != 0)
         *buflen = eeSnprintf(buffer, bufsize, "%s %ld ", kp, aArg->count0);
      else
         *buflen = eeSnprintf(buffer, bufsize, "%s ", kp);
      return n;
   }

   // An external command will probably use an argument starting
   // with "-" as an option.  To avoid trouble we skip the "-".
   while (*ptr == '-' && n > 0) {
      ++ptr;
      --n;
   }
   if (n == 0) {
      // found dashes only
      emsg(_(e_no_identifier_under_cursor));
      eeglFree(buffer);
      *ptr_arg = ptr;
      return 0;
   }

   //When a count is given, turn it into a range.  Is this really what we want?
   isman = (STRCMP(kp, "man") == 0);
   isman_s = (STRCMP(kp, "man -s") == 0);
   if (aArg->count0 != 0 && !(isman || isman_s))
      *buflen = eeSnprintf(buffer, bufsize, ".,.+%ld! ", aArg->count0 - 1);
   else
      *buflen = eeSnprintf(buffer, bufsize, "! ");

   if (aArg->count0 == 0 && isman_s)
      *buflen += eeSnprintf(buffer + *buflen, bufsize - *buflen, "man ");
   else
      *buflen += eeSnprintf(buffer + *buflen, bufsize - *buflen, "%s ", kp);
   if (aArg->count0 != 0 && (isman || isman_s))
      *buflen += eeSnprintf(buffer + *buflen, bufsize - *buflen, "%ld ", aArg->count0);

   *ptr_arg = ptr;
   return n;
}

// Handle the commands that use the word under the cursor.
// [g] CTRL-]   :ta to current identifier
// [g] 'K'   run program for current identifier
// [g] '*'   / to current identifier or string
// [g] '#'   ? to current identifier or string
//  g  ']'   :tselect for current identifier
private void
nv_ident(ActionArg* aArg) {
   CS ptr = NULL;
   CS buffer;
   Unt bufsize;
   Unt buflen;
   CS newbuf;
   CS p;
   CS kp;      // value of 'keywordprg'
   int kp_help;   // 'keywordprg' is ":he"
   int kp_ex;      // 'keywordprg' starts with ":"
   int n = 0;      // init for GCC
   int cmdchar;
   int g_cmd;      // "g" command
   int tag_cmd = false;
   CS aux_ptr;

   if (aArg->cmdchar == 'g') {   // "g*", "g#", "g]" and "gCTRL-]"
      cmdchar = aArg->nchar;
      g_cmd = true;
   } else {
      cmdchar = aArg->cmdchar;
      g_cmd = false;
   }

   if (cmdchar == POUND)   // the pound sign, '#' for English keyboards
      cmdchar = '#';

   // The "]", "CTRL-]" and "K" commands accept an argument in Visual mode.
   if (cmdchar == ']' || cmdchar == Ctrl_RSB || cmdchar == 'K') {
      if (VIsual_active && get_visual_text(aArg, OUT &ptr, OUT &n) == FAIL)
          return;
      if (checkclearopq(aArg->oper))
          return;
   }

   if (!ptr 
         && (n = find_ident_under_cursor(
               &ptr, 
               (cmdchar == '*' || cmdchar == '#') ? FIND_IDENT|FIND_STRING : FIND_IDENT)
            ) == 0
   ) {
      clearop(aArg->oper);
      return;
   }

   //Allocate buffer to put the command in. Inserting backslashes can
   //double the length of the word. curBook->o.keywordProg could be added and some numbers.
   kp = curBook->o.keywordProg;
   kp_help = (!kp || STRCMP(kp, ":he") == 0 || STRCMP(kp, ":help") == 0);
   if (kp_help && *skipwhite(ptr) == ZERO) {
       emsg(_(e_no_identifier_under_cursor));    // found white space only
       return;
   }
   kp_ex = (*kp == ':');
   bufsize = (Unt)(n * 2 + 30 + STRLEN(kp));
   buffer = alloc(bufsize);
   buffer[0] = ZERO;
   buflen = 0;

   switch (cmdchar) {
   case '*':
   case '#':
      // Put cursor at start of word, makes search skip the word under the cursor.
      // Call setpcmark() first, so "*``" puts the cursor back where it was.
      setpcmark();
      curPor->cursor.col = (ColNr) (ptr - ml_get_curline());

      if (!g_cmd && eeIsWordPtr(ptr)) {
         STRCPY(buffer, "\\<");
         buflen = STRLEN_LITERAL("\\<");
      }
      no_smartcase = true;   // don't use 'smartcase' now
      break;

   case 'K':
      n = nv_K_getcmd(aArg, kp, kp_help, kp_ex, &ptr, n, buffer, bufsize, &buflen);
      if (n == 0)
         return;
      break;

   case ']':
      tag_cmd = true;
      if (p_cst) {
         STRCPY(buffer, "cstag ");
         buflen = STRLEN_LITERAL("cstag ");
      } else {
         STRCPY(buffer, "ts ");
         buflen = STRLEN_LITERAL("ts ");
      }
      break;

   default:
      tag_cmd = true;
      if (curBook->kind == BOOK_HELP) {
         STRCPY(buffer, "he! ");
         buflen = STRLEN_LITERAL("he! ");
      } else {
         if (g_cmd) {
            STRCPY(buffer, "tj ");
            buflen = STRLEN_LITERAL("tj ");
         } ei (aArg->count0 == 0) {
            STRCPY(buffer, "ta ");
            buflen = STRLEN_LITERAL("ta ");
         } else
            buflen = eeSnprintf(buffer, bufsize, ":%ldta ", aArg->count0);
      }
   }

   // Now grab the chars in the identifier
   if (cmdchar == 'K' && !kp_help) {

      ptr = copySubstr(ptr, n);
      if (kp_ex)
         // Escape the argument properly for a command
         p = copyStr_fnameescape(ptr, VSE_NONE);
      else
         // Escape the argument properly for a shell command
         p = copyStr_shellescape(ptr, true, true);
      eeglFree(ptr);
      Unt plen = STRLEN(p);
      newbuf = eeRealloc(buffer, buflen + plen + 1);
      buffer = newbuf;
      STRCPY(buffer + buflen, p);
      buflen += plen;
      eeglFree(p);
   } else {
      if (cmdchar == '*')
         aux_ptr = S"/.*~[^$\\";
      ei (cmdchar == '#')
         aux_ptr = S"/?.*~[^$\\";
      ei (tag_cmd) {
         if (curBook->kind == BOOK_HELP)
            // ":help" handles unescaped argument
            aux_ptr = S"";
         else
            aux_ptr = S"\\|\"\n[";
      } else
         aux_ptr = S"\\|\"\n*?[";

      p = buffer + buflen;
      while (n-- > 0) {
         // put a backslash before \ and some others
         if (firstOccurrence(aux_ptr, *ptr) != NULL)
            *p++ = '\\';

         // When current byte is a part of multibyte character, copy all bytes of that character.
         int i;
         int len = utfCharLen(ptr) - 1;

         for (i = 0; i < len && n >= 1; ++i, --n)
            *p++ = *ptr++;
          *p++ = *ptr++;
      }
      *p = ZERO;
      buflen = p - buffer;
   }

   // Execute the command.
   if (cmdchar == '*' || cmdchar == '#') {
      if (!g_cmd && (eeIsWordPtr(mb_prevptr(ml_get_curline(), ptr)))) {
         STRCPY(buffer + buflen, "\\>");
         buflen += STRLEN_LITERAL("\\>");
      }

      // put pattern in search history
      init_history();
      scrAddToHistory(HIST_SEARCH, (Text){buffer, buflen}, true, ZERO);

      (void)normal_search(aArg, cmdchar == '*' ? '/' : '?', (Text){buffer, buflen}, 0, NULL);
   } else {
      g_tag_at_cursor = true;
      executeCommLine(buffer);
      g_tag_at_cursor = false;
   }

   eeglFree(buffer);
}

// CTRL-T: backwards in tag stack
private void
nv_tagpop(ActionArg* aArg) {
   if (!checkclearopq(aArg->oper))
      do_tag(S"", DT_POP, (int)aArg->count1, false, true);
}

// Handle scrolling command 'H', 'L' and 'M'.
private void
nv_scroll(ActionArg* aArg) {
   long n;
   aArg->oper->motion_type = MLINE;
   setpcmark();

   if (aArg->cmdchar == 'L') {
      validate_botline();       // make sure curPor->bottomLine is valid
      curPor->cursor.lnum = curPor->bottomLine - 1;
      if (aArg->count1 - 1 >= curPor->cursor.lnum)
          curPor->cursor.lnum = 1;
      else {
         if (hasAnyFolding(curPor)) {
            // Count a fold for one screen line.
            for (n = aArg->count1 - 1; n > 0 && curPor->cursor.lnum > curPor->topLine; --n) {
               (void)getFolds(curPor->cursor.lnum, &curPor->cursor.lnum, NULL);
               if (curPor->cursor.lnum > curPor->topLine)
                  --curPor->cursor.lnum;
            }
         } else
            curPor->cursor.lnum -= aArg->count1 - 1;
      }
   } else {
      LineNr lnum;
      int used = 0;
      if (aArg->cmdchar == 'M') {
         // Don't count filler lines above the portal.
         used -= diff_check_fill(curPor, curPor->topLine) - curPor->topFill;
         validate_botline();       // make sure emptyRowCount is valid
         int half = (curPor->height - curPor->emptyRowCount + 1) / 2;
         for (n = 0; curPor->topLine + n < curBook->mem.lineCount; ++n) {
           // Count half he number of filler lines to be "below this
           // line" and half to be "above the next line".
           if (n > 0 && used + diff_check_fill(curPor, curPor->topLine
                     + n) / 2 >= half)       {
               --n;
               break;
           }
           used += plines(curPor->topLine + n);
           if (used >= half)
               break;
           if (getFolds(curPor->topLine + n, NULL, &lnum))
               n = lnum - curPor->topLine;
         }
         if (n > 0 && used > (int)curPor->height)
            --n;
      } else { // (aArg->cmdchar == 'H')
         n = aArg->count1 - 1;
         if (hasAnyFolding(curPor)) {
            // Count a fold for one screen line.
            lnum = curPor->topLine;
            while (n-- > 0 && lnum < curPor->bottomLine - 1) {
               (void)getFolds(lnum, NULL, &lnum);
               ++lnum;
            }
            n = lnum - curPor->topLine;
         }
      }
      curPor->cursor.lnum = curPor->topLine + n;
      if (curPor->cursor.lnum > curBook->mem.lineCount)
         curPor->cursor.lnum = curBook->mem.lineCount;
   }

   // Correct for 'so', except when an operator is pending.
   if (aArg->oper->opTy == OP_NOP)
      cursor_correct();
   beginline(BL_SOL | BL_FIX);
}

// Cursor right commands.
private void
nv_right(ActionArg* aArg) {
   long   n;
   int      past_line;

   if (modMaskG & (MOD_MASK_SHIFT | MOD_MASK_CTRL)) {
      // <C-Right> and <S-Right> move a word or WORD right
      if (modMaskG & MOD_MASK_CTRL)
          aArg->arg = true;
      nv_wordcmd(aArg);
      return;
   }

   aArg->oper->motion_type = MCHAR;
   aArg->oper->inclusive = false;
   past_line = VIsual_active;

   //In virtual edit mode, there's no such thing as "past_line", as lines
   //are (theoretically) infinitely long.
   if (virtual_active())
      past_line = 0;

   for (n = aArg->count1; n > 0; --n) {
      if ((!past_line && oneright() == FAIL) || (past_line && *ml_get_cursor() == ZERO)) {
         //     <Space> wraps to next line if 'whichwrap' has 's'.
         //         'l' wraps to next line if 'whichwrap' has 'l'.
         // CURS_RIGHT wraps to next line if 'whichwrap' has '>'.
         if (p_ww && ((aArg->cmdchar == ' ' && firstOccurrence(p_ww, 's') != NULL)
               || (aArg->cmdchar == 'l' && firstOccurrence(p_ww, 'l') != NULL)
               || (aArg->cmdchar == K_RIGHT && firstOccurrence(p_ww, '>') != NULL)
             )
             && curPor->cursor.lnum < curBook->mem.lineCount
         ) {
            // When deleting we also count the NL as a character.
            // Set aArg->oper->inclusive when last char in the line is
            // included, move to next line after that
            if (      aArg->oper->opTy != OP_NOP
               && !aArg->oper->inclusive
               && !LINEEMPTY(curPor->cursor.lnum))
                aArg->oper->inclusive = true;
            else {
                ++curPor->cursor.lnum;
                curPor->cursor.col = 0;
                curPor->cursor.coladd = 0;
                curPor->setCursWant = true;
                aArg->oper->inclusive = false;
            }
            continue;
         }
         if (aArg->oper->opTy == OP_NOP) {
            // Only beep and flush if not moved at all
            if (n == aArg->count1)
                inpFlushIfNotSilent();
         } else {
            if (!LINEEMPTY(curPor->cursor.lnum))
                aArg->oper->inclusive = true;
         }
         break;
      } ei (past_line) {
         curPor->setCursWant = true;
         if (virtual_active())
            oneright();
         else {
            curPor->cursor.col += utfCharLen(ml_get_cursor());
         }
      }
   }
   if (n != aArg->count1 && (p_fdo & FDO_HOR) && keyWasTypedG && aArg->oper->opTy == OP_NOP)
      foldOpenCursor();
}

// Cursor left commands. Return true when operator end should not be adjusted.
private void
nv_left(ActionArg* aArg) {
   long   n;

   if (modMaskG & (MOD_MASK_SHIFT | MOD_MASK_CTRL)) {
      // <C-Left> and <S-Left> move a word or WORD left
      if (modMaskG & MOD_MASK_CTRL)
         aArg->arg = 1;
      nv_bck_word(aArg);
      return;
   }

   aArg->oper->motion_type = MCHAR;
   aArg->oper->inclusive = false;
   for (n = aArg->count1; n > 0; --n) {
      if (oneleft() == FAIL) {
          // <BS> and <Del> wrap to previous line if 'whichwrap' has 'b'.
          //       'h' wraps to previous line if 'whichwrap' has 'h'.
          //      CURS_LEFT wraps to previous line if 'whichwrap' has '<'.
          if (p_ww 
                && (((aArg->cmdchar == K_BS || aArg->cmdchar == Ctrl_H)
                      && firstOccurrence(p_ww, 'b') != NULL)
                     || (aArg->cmdchar == 'h' && firstOccurrence(p_ww, 'h') != NULL)
                     || (aArg->cmdchar == K_LEFT && firstOccurrence(p_ww, '<') != NULL)
                 ) && curPor->cursor.lnum > 1
         ) {
            --(curPor->cursor.lnum);
            coladvance((ColNr)MAXCOL);
            curPor->setCursWant = true;

            // When the NL before the first char has to be deleted we
            // put the cursor on the ZERO after the previous line.
            // This is a very special case, be careful!
            // Don't adjust op_end now, otherwise it won't work.
            if ((aArg->oper->opTy == OP_DELETE
                   || aArg->oper->opTy == OP_CHANGE)
                     && !LINEEMPTY(curPor->cursor.lnum)
            ){
               CS cp = ml_get_cursor();
               if (*cp != ZERO) {
                  curPor->cursor.col += utfCharLen(cp);
               }
               aArg->retval |= CA_NO_ADJ_OP_END;
            }
            continue;
         }
         // Only beep and flush if not moved at all
         ei (aArg->oper->opTy == OP_NOP && n == aArg->count1)
            inpFlushIfNotSilent();
         break;
      }
   }
   if (n != aArg->count1 && (p_fdo & FDO_HOR) && keyWasTypedG && aArg->oper->opTy == OP_NOP)
      foldOpenCursor();
}

// Cursor up commands. aArg->arg is true for "-": Move cursor to first non-blank.
private void
nv_up(ActionArg* aArg) {
   if (modMaskG & MOD_MASK_SHIFT) {
      // <Shift-Up> is page up
      aArg->arg = BACKWARD;
      nvPage(aArg);
      return;
   }

   aArg->oper->motion_type = MLINE;
   if (cursor_up(aArg->count1, aArg->oper->opTy == OP_NOP) == FAIL)
      clearopbeep(aArg->oper);
   ei (aArg->arg)
      beginline(BL_WHITE | BL_FIX);
}

// Cursor down commands. aArg->arg is true for CR and "+": Move cursor to first non-blank.
private void
nv_down(ActionArg* aArg) {
   if (modMaskG & MOD_MASK_SHIFT) {
      // <S-Down> is page down
      aArg->arg = FORWARD;
      nvPage(aArg);
   }
   // Location portal only: view the result under the cursor.
   ei (isLocationListBook(curBook) && aArg->cmdchar == ENTER)
      llViewLocation(false);
   else {
   // In the commline portal a <CR> executes the command.
   if (commPortTypeG != 0 && aArg->cmdchar == ENTER)
       commPortResultG = ENTER;
   else
   // In a prompt book a <CR> in the last line invokes the callback.
   if (bt_prompt(curBook) && aArg->cmdchar == ENTER
             && curPor->cursor.lnum == curBook->mem.lineCount) {
       invoke_prompt_callback();
       if (restart_edit == 0)
      restart_edit = 'a';
   } else {
      aArg->oper->motion_type = MLINE;
      if (cursor_down(aArg->count1, aArg->oper->opTy == OP_NOP) == FAIL)
         clearopbeep(aArg->oper);
      ei (aArg->arg)
         beginline(BL_WHITE | BL_FIX);
   }
   }
}

// Grab the file name under the cursor and edit it.
private void
nv_gotofile(ActionArg* aArg) {
   LineNr lnum = -1;

   if (check_text_or_curbuf_locked(aArg->oper))
      return;

   if (portErrorIfTermPopup())
      return;

   if (!portCheckCanSetCurBookDisabled())
      return;

   CS ptr = grab_file_name(aArg->count1, OUT &lnum);

   if (ptr) {
      setpcmark();
      if (startEditingFile(0, ptr, NULL, NULL, ECMD_LAST, ECMD_HIDE, curPor) == OK
         && aArg->nchar == 'F' && lnum >= 0
      ) {
         curPor->cursor.lnum = lnum;
         check_cursor_lnum();
         beginline(BL_SOL | BL_FIX);
      }
      eeglFree(ptr);
   } else
      clearop(aArg->oper);
}

// <End> command: to end of current line or last line.
private void
nv_end(ActionArg* aArg) {
   if (aArg->arg || (modMaskG & MOD_MASK_CTRL)) {  // CTRL-END = goto last line
      aArg->arg = true;
      nv_goto(aArg);
      aArg->count1 = 1;      // to end of current line
   }
   nv_dollar(aArg);
}

// Handle the "$" command.
private void
nv_dollar(ActionArg* aArg) {
   aArg->oper->motion_type = MCHAR;
   aArg->oper->inclusive = true;
   // In virtual mode when off the edge of a line and an operator
   // is pending (whew!) keep the cursor where it is. Otherwise, send it to the end of the line.
   if (!virtual_active() || gchar_cursor() != ZERO || aArg->oper->opTy == OP_NOP)
      curPor->cursWant = MAXCOL;   // so we stay at the end
   if (cursor_down((long)(aArg->count1 - 1), aArg->oper->opTy == OP_NOP) == FAIL)
      clearopbeep(aArg->oper);
   ei ((p_fdo & FDO_HOR) && keyWasTypedG && aArg->oper->opTy == OP_NOP)
      foldOpenCursor();
}

// Implementation of '?' and '/' commands. If aArg->arg is true don't set PC mark.
private void
nv_search(ActionArg* aArg) {
   Operator* oper = aArg->oper;
   Pos save_cursor = curPor->cursor;

   if (aArg->cmdchar == '?' && aArg->oper->opTy == OP_ROT13) {
      // Translate "g??" to "g?g?"
      aArg->cmdchar = 'g';
      aArg->nchar = '?';
      nv_operator(aArg);
      return;
   }

    // When using 'incsearch' the cursor may be moved to set a different search
    // start position.
    aArg->searchbuf = getCommline(aArg->cmdchar, aArg->count1, 0, 0);

   if (aArg->searchbuf == NULL) {
      clearop(oper);
      return;
   }

   (void)normal_search(aArg, aArg->cmdchar, text(aArg->searchbuf),
         (aArg->arg || !EQUAL_POS(save_cursor, curPor->cursor))
                        ? 0 : SEARCH_MARK, NULL);
}


// Handle "N" and "n" commands. aArg->arg is SEARCH_REV for "N", 0 for "n".
private void
nv_next(ActionArg* aArg) {
   Pos old = curPor->cursor;
   int wrapped = false;
   int i = normal_search(aArg, 0, (Text){NULL, 0}, SEARCH_MARK | aArg->arg, &wrapped);

   if (i == 1 && !wrapped && EQUAL_POS(old, curPor->cursor)) {
      // Avoid getting stuck on the current cursor position, which can
      // happen when an offset is given and the cursor is on the last char
      // in the book: Repeat with count + 1.
      aArg->count1 += 1;
      (void)normal_search(aArg, 0, (Text){NULL, 0}, SEARCH_MARK | aArg->arg, NULL);
      aArg->count1 -= 1;
   }

   // Redraw the portal to refresh the hilited matches.
   if (i > 0 && p_hls && hiliteSearchG)
      redraw_later(UPD_SOME_VALID);
}


// Character search commands.
// aArg->arg is BACKWARD for 'F' and 'T', FORWARD for 'f' and 't', true for
// ',' and false for ';'.
// aArg->nchar is ZERO for ',' and ';' (repeat the search)
private void
nv_csearch(ActionArg* aArg) {
   Boole t_cmd = (aArg->cmdchar == 't' || aArg->cmdchar == 'T');

   aArg->oper->motion_type = MCHAR;
   if (IS_SPECIAL(aArg->nchar) || searchc(aArg, t_cmd) == FAIL) {
      clearopbeep(aArg->oper);
      return;
   }

   curPor->setCursWant = true;
   // Include a Tab for "tx" and for "dfx".
   if (gchar_cursor() == TAB && virtual_active() && aArg->arg == FORWARD
       && (t_cmd || aArg->oper->opTy != OP_NOP)
   ) {
      ColNr   scol, ecol;

      getvcol(curPor, &curPor->cursor, OUT &scol, NULL, OUT &ecol);
      curPor->cursor.coladd = ecol - scol;
   } else
      curPor->cursor.coladd = 0;
   if ((p_fdo & FDO_HOR) && keyWasTypedG && aArg->oper->opTy == OP_NOP)
      foldOpenCursor();
}

// "[{", "[(", "]}" or "])": go to Nth unclosed '{', '(', '}' or ')'
// "[#", "]#": go to start/end of Nth innermost #if..#endif construct.
// "[/", "[*", "]/", "]*": go to Nth comment start/end.
// "[m" or "]m" search for prev/next start of (Java) method.
// "[M" or "]M" search for prev/next end of (Java) method.
private void
nv_bracket_block(ActionArg* aArg, Pos* old_pos) {
   Pos new_pos = {0, 0, 0};
   Pos* pos = NULL;       // init for GCC
   Pos prev_pos;
   long n;
   int findc;
   int c;

   if (aArg->nchar == '*')
      aArg->nchar = '/';
   prev_pos.lnum = 0;
   if (aArg->nchar == 'm' || aArg->nchar == 'M') {
      if (aArg->cmdchar == '[')
         findc = '{';
      else
         findc = '}';
      n = 9999;
   } else {
      findc = aArg->nchar;
      n = aArg->count1;
   }
   for ( ; n > 0; --n) {
      if ((pos = findmatchlimit(aArg->oper, findc,
            (aArg->cmdchar == '[') ? FM_BACKWARD : FM_FORWARD, 0)) == NULL)
      {
         if (new_pos.lnum == 0) {  // nothing found
            if (aArg->nchar != 'm' && aArg->nchar != 'M')
               clearopbeep(aArg->oper);
         } else
            pos = &new_pos;   // use last one found
         break;
      }
      prev_pos = new_pos;
      curPor->cursor = *pos;
      new_pos = *pos;
   }
   curPor->cursor = *old_pos;

   // Handle "[m", "]m", "[M" and "[M".  The findmatchlimit() only
   // brought us to the match for "[m" and "]M" when inside a method.
   // Try finding the '{' or '}' we want to be at.
   // Also repeat for the given count.
   if (aArg->nchar == 'm' || aArg->nchar == 'M') {
      // norm is true for "]M" and "[m"
      int norm = ((findc == '{') == (aArg->nchar == 'm'));

      n = aArg->count1;
      // found a match: we were inside a method
      if (prev_pos.lnum != 0) {
         pos = &prev_pos;
         curPor->cursor = prev_pos;
         if (norm)
            --n;
      } else
          pos = NULL;
      while (n > 0) {
         for (;;) {
            if ((findc == '{' ? dec_cursor() : inc_cursor()) < 0) {
               // if not found anything, that's an error
               if (pos == NULL)
                  clearopbeep(aArg->oper);
               n = 0;
               break;
            }
            c = gchar_cursor();
            if (c == '{' || c == '}') {
               // Must have found end/start of class: use it. Or found the place to be at.
               if ((c == findc && norm) || (n == 1 && !norm)) {
                  new_pos = curPor->cursor;
                  pos = &new_pos;
                  n = 0;
               }
               // if no match found at all, we started outside of the
               // class and we're inside now.  Just go on.
               ei (new_pos.lnum == 0) {
                  new_pos = curPor->cursor;
                  pos = &new_pos;
               }
               // found start/end of other method: go to match
               ei ((pos = findmatchlimit(
                           aArg->oper, findc, (aArg->cmdchar == '[') ? FM_BACKWARD : FM_FORWARD, 0)
                          ) == NULL)
                  n = 0;
               else
                  curPor->cursor = *pos;
               break;
            }
         }
         --n;
      }
      curPor->cursor = *old_pos;
      if (!pos && new_pos.lnum != 0)
         clearopbeep(aArg->oper);
   }
   if (pos) {
      setpcmark();
      curPor->cursor = *pos;
      curPor->setCursWant = true;
      if ((p_fdo & FDO_BLOCK) && keyWasTypedG && aArg->oper->opTy == OP_NOP)
         foldOpenCursor();
   }
}

// "[" and "]" commands.
// aArg->arg is BACKWARD for "[" and FORWARD for "]".
private void
nv_brackets(ActionArg* aArg) {
   Pos prev_pos;
   Pos* pos = NULL;       // init for GCC
   Unt flag;
   long n;

   aArg->oper->motion_type = MCHAR;
   aArg->oper->inclusive = false;
   Pos old_pos = curPor->cursor; // cursor position before command
   curPor->cursor.coladd = 0;    // TODO: don't do this for an error.

   // "[f" or "]f" : Edit file under the cursor (same as "gf")
   if (aArg->nchar == 'f')
      nv_gotofile(aArg);
   else

    // Find the occurrence(s) of the identifier or define under cursor
    // in current and included files or jump to the first occurrence.
    //
    //         search        list       jump
    //            fwd   bwd    fwd    bwd    fwd   bwd
    // identifier     "]i"  "[i"   "]I"  "[I"   "]^I"  "[^I"
    // define         "]d"  "[d"   "]D"  "[D"   "]^D"  "[^D"
   if (firstOccurrence((CS)"iI\011dD\004", aArg->nchar) != NULL) {
      CS ptr;
      int   len;

      if ((len = find_ident_under_cursor(&ptr, FIND_IDENT)) == 0)
         clearop(aArg->oper);
      else {
         // Make a copy, if the line was changed it will be freed.
         ptr = copySubstr(ptr, len);

         find_pattern_in_path(
            ptr, 0, len, true,
            aArg->count0 == 0 ? !SAFE_isupper(aArg->nchar) : false,
            ((aArg->nchar & 0xf) == ('d' & 0xf)) ?  FIND_DEFINE : FIND_ANY,
            aArg->count1,
            SAFE_isupper(aArg->nchar) ? ACTION_SHOW_ALL :
                   SAFE_islower(aArg->nchar) ? ACTION_SHOW : ACTION_GOTO,
            aArg->cmdchar == ']' ? curPor->cursor.lnum + 1 : (LineNr)1,
            (LineNr)MAXLNUM,
            false, false
         );
         eeglFree(ptr);
         curPor->setCursWant = true;
      }
   } else

      // "[{", "[(", "]}" or "])": go to Nth unclosed '{', '(', '}' or ')'
      // "[#", "]#": go to start/end of Nth innermost #if..#endif construct.
      // "[/", "[*", "]/", "]*": go to Nth comment start/end.
      // "[m" or "]m" search for prev/next start of (Java) method.
      // "[M" or "]M" search for prev/next end of (Java) method.
      if (  (aArg->cmdchar == '['
         && firstOccurrence((CS)"{(*/#mM", aArg->nchar) != NULL)
          || (aArg->cmdchar == ']'
         && firstOccurrence((CS)"})*/#mM", aArg->nchar) != NULL)
      )
         nv_bracket_block(aArg, &old_pos);

   // "[[", "[]", "]]" and "][": move to start or end of function
   ei (aArg->nchar == '[' || aArg->nchar == ']') {
      if (aArg->nchar == aArg->cmdchar)          // "]]" or "[["
         flag = '{';
      else
         flag = '}';          // "][" or "[]"

      curPor->setCursWant = true;
      // Imitate strange Vi behaviour: When using "]]" with an operator
      // we also stop at '}'.
      if (!normFindNextParagraf(
            OUT &aArg->oper->inclusive, aArg->arg, aArg->count1, flag, 
            (aArg->oper->opTy != OP_NOP && aArg->arg == FORWARD && flag == '{')
           )
      )
         clearopbeep(aArg->oper);
      else    {
         if (aArg->oper->opTy == OP_NOP)
            beginline(BL_WHITE | BL_FIX);
         if ((p_fdo & FDO_BLOCK) && keyWasTypedG && aArg->oper->opTy == OP_NOP)
            foldOpenCursor();
      }
   }

   // "[p", "[P", "]P" and "]p": put with indent adjustment
   ei (aArg->nchar == 'p' || aArg->nchar == 'P') {
      nv_put_opt(aArg, true);
   }

   // "['", "[`", "]'" and "]`": jump to next mark
   ei (aArg->nchar == '\'' || aArg->nchar == '`') {
      pos = &curPor->cursor;
      for (n = aArg->count1; n > 0; --n) {
         prev_pos = *pos;
         pos = getnextmark(pos, aArg->cmdchar == '[' ? BACKWARD : FORWARD, aArg->nchar == '\'');
         if (pos == NULL)
            break;
      }
      if (pos == NULL)
          pos = &prev_pos;
      nv_cursormark(aArg, aArg->nchar == '\'', pos);
   }
   // [ or ] followed by a middle mouse click: put selected text with
   // indent adjustment.  Any other button just does as usual.
   ei (aArg->nchar >= K_RIGHTRELEASE && aArg->nchar <= K_LEFTMOUSE)     {
      (void)do_mouse(aArg->oper, aArg->nchar,
                (aArg->cmdchar == ']') ? FORWARD : BACKWARD,
                aArg->count1, PUT_FIXINDENT);
   }
   // "[z" and "]z": move to start or end of open fold.
   ei (aArg->nchar == 'z') {
      if (foldMoveTo(false, aArg->cmdchar == ']' ? FORWARD : BACKWARD, aArg->count1) == FAIL)
         clearopbeep(aArg->oper);
   }

   // "[c" and "]c": move to next or previous diff-change.
   ei (aArg->nchar == 'c') {
      if (diff_move_to(aArg->cmdchar == ']' ? FORWARD : BACKWARD, aArg->count1) == FAIL)
         clearopbeep(aArg->oper);
   }
   // Not a valid aArg->nchar.
   else
      clearopbeep(aArg->oper);
}

// Handle Normal mode "%" command.
private void
nv_percent(ActionArg* aArg) {
   LineNr   lnum = curPor->cursor.lnum;

   aArg->oper->inclusive = true;
      if (aArg->count0) {      // {cnt}% : goto {cnt} percentage in file
      if (aArg->count0 > 100)
         clearopbeep(aArg->oper);
      else {
         aArg->oper->motion_type = MLINE;
         setpcmark();
         // Round up, so 'normal 100%' always jumps at the line line.
         // Beyond 21474836 lines, (lineCount * 100 + 99) would
         // overflow on 32-bits, so use a formula with less accuracy
         // to avoid overflows.
         if (curBook->mem.lineCount >= 21474836)
            curPor->cursor.lnum = (curBook->mem.lineCount + 99L) / 100L * aArg->count0;
         else
            curPor->cursor.lnum = (curBook->mem.lineCount * aArg->count0 + 99L) / 100L;
         if (curPor->cursor.lnum < 1)
            curPor->cursor.lnum = 1;
         if (curPor->cursor.lnum > curBook->mem.lineCount)
            curPor->cursor.lnum = curBook->mem.lineCount;
         beginline(BL_SOL | BL_FIX);
      }
   } else {         // "%" : go to matching paren
      aArg->oper->motion_type = MCHAR;
      aArg->oper->use_reg_one = true;
      Pos* pos;
      if ((pos = findmatch(aArg->oper, ZERO)) == NULL)
         clearopbeep(aArg->oper);
      else {
         setpcmark();
         curPor->cursor = *pos;
         curPor->setCursWant = true;
         curPor->cursor.coladd = 0;
      }
   }
   if (aArg->oper->opTy == OP_NOP
       && lnum != curPor->cursor.lnum
       && (p_fdo & FDO_PERCENT)
       && keyWasTypedG
   )
      foldOpenCursor();
}

// Handle "(" and ")" commands. aArg->arg is BACKWARD for "(" and FORWARD for ")".
private void
nv_brace(ActionArg* aArg) {
   aArg->oper->motion_type = MCHAR;
   aArg->oper->use_reg_one = true;
   // The motion used to be inclusive for "(", but that is not what Vi does.
   aArg->oper->inclusive = false;
   curPor->setCursWant = true;

   if (findsent(aArg->arg, aArg->count1) == FAIL) {
      clearopbeep(aArg->oper);
      return;
   }

   // Don't leave the cursor on the ZERO past end of line.
   adjust_cursor(aArg->oper);
   curPor->cursor.coladd = 0;
   if ((p_fdo & FDO_BLOCK) && keyWasTypedG && aArg->oper->opTy == OP_NOP)
      foldOpenCursor();
}

// "m" command: Mark a position.
private void
nv_mark(ActionArg* aArg) {
   if (checkclearop(aArg->oper))
      return;

   if (setmark(aArg->nchar) == FAIL)
      clearopbeep(aArg->oper);
}

// "{" and "}" commands.
// cmd->arg is BACKWARD for "{" and FORWARD for "}".
private void
nv_findpar(ActionArg* aArg) {
   aArg->oper->motion_type = MCHAR;
   aArg->oper->inclusive = false;
   aArg->oper->use_reg_one = true;
   curPor->setCursWant = true;
   if (!normFindNextParagraf(OUT &aArg->oper->inclusive, aArg->arg, aArg->count1, ZERO, false)) {
      clearopbeep(aArg->oper);
      return;
   }

   curPor->cursor.coladd = 0;
   if ((p_fdo & FDO_BLOCK) && keyWasTypedG && aArg->oper->opTy == OP_NOP)
      foldOpenCursor();
}

// "u" command: Undo or make lower case.
private void
nv_undo(ActionArg* aArg) {
   if (aArg->oper->opTy == OP_LOWER || VIsual_active) {
      // translate "<Visual>u" to "<Visual>gu" and "guu" to "gugu"
      aArg->cmdchar = 'g';
      aArg->nchar = 'u';
      nv_operator(aArg);
   } else
      nv_kundo(aArg);
}

// <Undo> command.
private void
nv_kundo(ActionArg* aArg) {
   if (checkclearopq(aArg->oper))
      return;

   if (bt_prompt(curBook)) {
      clearopbeep(aArg->oper);
      return;
   }
   u_undo((int)aArg->count1);
   curPor->setCursWant = true;
}

//Handle the "r" action
private void
nv_replace(ActionArg* aArg) {
   int had_ctrl_v;
   long n;

   if (checkclearop(aArg->oper))
      return;
   if (bt_prompt(curBook) && !prompt_curpos_editable()) {
      clearopbeep(aArg->oper);
      return;
   }

   // get another character
   if (aArg->nchar == Ctrl_V || aArg->nchar == Ctrl_Q) {
      had_ctrl_v = Ctrl_V;
      aArg->nchar = get_literal(false);
      // Don't redo a multibyte character with CTRL-V.
      if (aArg->nchar > DEL)
         had_ctrl_v = ZERO;
   } else
      had_ctrl_v = ZERO;

   // Abort if the character is a special key.
   if (IS_SPECIAL(aArg->nchar)) {
      clearopbeep(aArg->oper);
      return;
   }

   // Visual mode "r"
   if (VIsual_active) {
      if (gotInterruptG)
         gotInterruptG = false;
      if (had_ctrl_v) {
         // Use a special (negative) number to make a difference between a
         // literal CR or NL and a line break.
         if (aArg->nchar == ENTER)
            aArg->nchar = REPLACE_CR_NCHAR;
         ei (aArg->nchar == NL)
            aArg->nchar = REPLACE_NL_NCHAR;
      }
      nv_operator(aArg);
      return;
   }

   // Break tabs, etc.
   if (virtual_active()) {
      if (u_save_cursor() == FAIL)
         return;
      if (gchar_cursor() == ZERO) {
         // Add extra space and put the cursor on the first one.
         coladvance_force((ColNr)(getviscol() + aArg->count1));
         curPor->cursor.col -= aArg->count1;
      } ei (gchar_cursor() == TAB)
         coladvance_force(getviscol());
   }

   // Abort if not enough characters to replace.
   if ((Unt)ml_get_cursor_len() < (unsigned)aArg->count1
          || (mb_charlen(ml_get_cursor()) < aArg->count1)
   ) {
      clearopbeep(aArg->oper);
      return;
   }

   //Replacing with a TAB is done by edit() when it is complicated because
   //@expandtab' is set. CTRL-V TAB inserts a literal TAB.
   //Other characters are done below to avoid problems with things like
   //CTRL-V 048 (for edit() this would be R CTRL-V 0 ESC).
   if (had_ctrl_v != Ctrl_V && aArg->nchar == '\t' && curBook->o.expandTab) {
      stuffnumReadbuff(aArg->count1);
      stuffcharReadbuff('R');
      stuffcharReadbuff('\t');
      stuffcharReadbuff(ESC);
      return;
   }

   //save line for undo
   if (u_save_cursor() == FAIL)
      return;

   if (had_ctrl_v != Ctrl_V && (aArg->nchar == '\r' || aArg->nchar == '\n')) {
      //Replace character(s) by a single newline. Strange vi behaviour: Only one newline is 
      //inserted. Delete the characters here.
      //Insert the newline with an insert command, takes care of autoindent. 
      //The insert command depends on being on the last character of a line or not.
      (void)del_chars(aArg->count1, false);   // delete the characters
      stuffcharReadbuff('\r');
      stuffcharReadbuff(ESC);

      // Give 'r' to edit(), to get the redo command right.
      invokeEdit(aArg, true, 'r', false);
   } else {
      prep_redo(aArg->oper->regname, aArg->count1, ZERO, 'r', ZERO, had_ctrl_v, aArg->nchar);

      curBook->opStart = curPor->cursor;

      if (aArg->ncharC1 != 0)
         AppendCharToRedobuff(aArg->ncharC1);
      if (aArg->ncharC2 != 0)
         AppendCharToRedobuff(aArg->ncharC2);

      //This is slow, but it handles replacing a single-byte with a multi-byte and the other 
      //way around. Also handles adding composing characters for utf-8.
      for (n = aArg->count1; n > 0; --n) {
         if (aArg->nchar == Ctrl_E || aArg->nchar == Ctrl_Y) {
            int c = ins_copychar(curPor->cursor.lnum + (aArg->nchar == Ctrl_Y ? -1 : 1));
            if (c != ZERO)
               replaceChar(c);
            else
               //will be decremented further down
               ++curPor->cursor.col;
         } else
            replaceChar(aArg->nchar);
         if (aArg->ncharC1 != 0)
            insertChar(aArg->ncharC1);
         if (aArg->ncharC2 != 0)
            insertChar(aArg->ncharC2);
      }
      --curPor->cursor.col;       // cursor on the last replaced char
      // if the character on the left of the current cursor is a multi-byte
      // character, move two characters left
      mb_adjust_cursor();
      curBook->opEnd = curPor->cursor;
      curPor->setCursWant = true;
      set_last_insert(aArg->nchar);
   }
}


// Move cursor to mark.
private void
nv_cursormark(ActionArg* aArg, int flag, Pos *pos) {
   if (check_mark(pos) == FAIL)
      clearop(aArg->oper);
   else {
      if (aArg->cmdchar == '\''
         || aArg->cmdchar == '`'
         || aArg->cmdchar == '['
         || aArg->cmdchar == ']'
      )
         setpcmark();
      curPor->cursor = *pos;
      if (flag)
         beginline(BL_WHITE | BL_FIX);
      else
         check_cursor();
   }
   aArg->oper->motion_type = flag ? MLINE : MCHAR;
   if (aArg->cmdchar == '`')
      aArg->oper->use_reg_one = true;
   aArg->oper->inclusive = false;      // ignored if not MCHAR
   curPor->setCursWant = true;
}

// "s" and "S" commands. TODO
private void
nv_subst(ActionArg* aArg) {
   // When showing output of term_dumpdiff() swap the top and bottom.
   if (term_swap_diff() == OK)
   return;
   if (bt_prompt(curBook) && !prompt_curpos_editable()) {
      clearopbeep(aArg->oper);
      return;
   }
   if (VIsual_active) { // "vs" and "vS" are the same as "vc"
      if (aArg->cmdchar == 'S') {
         VIsual_mode_orig = VIsual_mode;
         VIsual_mode = 'V';
      }
      aArg->cmdchar = 'c';
      nv_operator(aArg);
   } else
      nvOperatorAliases(aArg);
}

// Abbreviated commands.
private void
nv_abbrev(ActionArg* aArg) {
   if (aArg->cmdchar == K_DEL || aArg->cmdchar == K_KDEL)
      aArg->cmdchar = 'x';      // DEL key behaves like 'x'

   // in Visual mode these commands are operators
   if (VIsual_active)
      vVisualOperators(aArg);
   else
      nvOperatorAliases(aArg);
}

// Translate a command into another command.
private void
nvOperatorAliases(ActionArg* aArg) {
   static CS aliases = S"DCsSY&";
   static CS replacements[8] = {SMAP((CS),
         "d$", "c$", "cl", "cc", "y$", ":s\r"
   )};

   if (!checkclearopq(aArg->oper)) {
       if (aArg->count0)
          stuffnumReadbuff(aArg->count0);
       stuffReadbuff(replacements[(int)(firstOccurrence(aliases, aArg->cmdchar) - aliases)]);
   }
   aArg->opcount = 0;
}

// "'" and "`" commands.  Also for "g'" and "g`". aArg->arg is true for "'" and "g'".
private void
nv_gomark(ActionArg* aArg) {
   Pos old_cursor = curPor->cursor;
   int old_keyWasTypedG = keyWasTypedG;    // getting file may reset it

   int c = (aArg->cmdchar == 'g') ? aArg->extra_char : aArg->nchar;
   Pos* pos = getmark(c, (aArg->oper->opTy == OP_NOP));
   if (pos == (Pos *)-1) {      // jumped to other file
      if (aArg->arg) {
         check_cursor_lnum();
         beginline(BL_WHITE | BL_FIX);
      } else
         check_cursor();
   } else
      nv_cursormark(aArg, aArg->arg, pos);

   // May need to clear the coladd that a mark includes.
   if (!virtual_active())
      curPor->cursor.coladd = 0;
   check_cursor_col();
   if (aArg->oper->opTy == OP_NOP
          && pos
          && (pos == (Pos *)-1 || !EQUAL_POS(old_cursor, *pos))
          && (p_fdo & FDO_MARK) != 0
          && old_keyWasTypedG
   )
      foldOpenCursor();
}

// Handle CTRL-O, CTRL-I, "g;", "g," and "CTRL-Tab" commands.
private void
nv_pcmark(ActionArg* aArg) {
   LineNr lnum = curPor->cursor.lnum;
   int old_keyWasTypedG = keyWasTypedG;    // getting file may reset it

   if (checkclearopq(aArg->oper))
      return;

   if (aArg->cmdchar == TAB && modMaskG == MOD_MASK_CTRL) {
      if (goto_tabpage_lastused() == FAIL)
         clearopbeep(aArg->oper);
      return;
   }
   Pos* pos = (aArg->cmdchar == 'g') 
      ? movechangelist((int)aArg->count1) : movemark((int)aArg->count1);
   if (pos == (Pos *)-1) {     // jump to other file
      curPor->setCursWant = true;
      check_cursor();
   } ei (pos)          // can jump
      nv_cursormark(aArg, false, pos);
   ei (aArg->cmdchar == 'g') {
      if (curBook->changeListLen == 0)
          emsg(_(e_changelist_is_empty));
      ei (aArg->count1 < 0)
          emsg(_(e_at_start_of_changelist));
      else
          emsg(_(e_at_end_of_changelist));
   } else
      clearopbeep(aArg->oper);
   if (aArg->oper->opTy == OP_NOP
         && (pos == (Pos *)-1 || lnum != curPor->cursor.lnum)
         && (p_fdo & FDO_MARK)
         && old_keyWasTypedG)
      foldOpenCursor();
}

// Handle '"' command.
private void
nv_regname(ActionArg* aArg) {
   if (checkclearop(aArg->oper))
      return;
   if (aArg->nchar == '=')
      aArg->nchar = get_expr_register();
   if (aArg->nchar != ZERO && valid_yank_reg(aArg->nchar, false)) {
      aArg->oper->regname = aArg->nchar;
      aArg->opcount = aArg->count0;   // remember count before '"'
      set_reg_var(aArg->oper->regname);
   } else
      clearopbeep(aArg->oper);
}

// Handle "v", "V", "Ctrl-q" and "Ctrl-v" commands.
// Also for "gh", "gH" and "g^H" commands: Always start Select mode, aArg->arg is true.
private void
nv_visual(ActionArg* aArg) {
   if (aArg->cmdchar == Ctrl_Q)
      aArg->cmdchar = Ctrl_V;

   // 'v', 'V' and CTRL-V can be used while an operator is pending to make it
   // characterwise, linewise, or blockwise.
   if (aArg->oper->opTy != OP_NOP) {
      motion_force = aArg->oper->motion_force = aArg->cmdchar;
      finish_op = false;   // operator doesn't finish now but later
      return;
   }

   if (VIsual_active)  {     // change Visual mode
      if (VIsual_mode == aArg->cmdchar)    // stop visual mode
         end_visual_mode();
      else {               // toggle char/block mode or char/line mode
         VIsual_mode = aArg->cmdchar;
         showmode();
         may_trigger_modechanged();
      }
      drawCurBookLater(UPD_INVERTED);       // update the inversion
   } else  {        // start Visual mode
      check_visual_highlight();
      if (aArg->count0 > 0 && resel_VIsual_mode != ZERO) {
         // use previously selected part
         VIsual = curPor->cursor;

         VIsual_active = true;
         VIsual_reselect = true;
         setmouse();
         if (p_smd && msg_silent == 0)
            redrawCommlineG = true;       // show visual mode later
         // For V and ^V, we multiply the number of lines even if there
         // was only one -- webb
         if (resel_VIsual_mode != 'v' || resel_VIsual_line_count > 1) {
            curPor->cursor.lnum += resel_VIsual_line_count * aArg->count0 - 1;
            check_cursor();
         }
         VIsual_mode = resel_VIsual_mode;
         if (VIsual_mode == 'v') {
            if (resel_VIsual_line_count <= 1) {
               update_curswant_force();
               curPor->cursWant += resel_VIsual_vcol * aArg->count0;
               --curPor->cursWant;
            } else
                curPor->cursWant = resel_VIsual_vcol;
            coladvance(curPor->cursWant);
         }
         if (resel_VIsual_vcol == MAXCOL) {
            curPor->cursWant = MAXCOL;
            coladvance((ColNr)MAXCOL);
         } ei (VIsual_mode == Ctrl_V) {
            // Update curswant on the original line, that is where "col" is valid.
            LineNr lnum = curPor->cursor.lnum;
            curPor->cursor.lnum = VIsual.lnum;
            update_curswant_force();
            curPor->cursWant += resel_VIsual_vcol * aArg->count0 - 1;
            curPor->cursor.lnum = lnum;
            coladvance(curPor->cursWant);
         } else
            curPor->setCursWant = true;
         drawCurBookLater(UPD_INVERTED);   // show the inversion
      } else {
         n_start_visual_mode(aArg->cmdchar);
         VIsual_select_exclu_adj = false;
         if (aArg->count0 > 0 && --aArg->count1 > 0) {
            // With a count select that many characters or lines.
            if (VIsual_mode == 'v' || VIsual_mode == Ctrl_V)
               nv_right(aArg);
            ei (VIsual_mode == 'V')
               nv_down(aArg);
          }
      }
   }
}

// CTRL-W: Portal commands
private void
nv_portal(ActionArg* aArg) {
   if (aArg->nchar == ':') {
      // "CTRL-W :" is the same as typing ":"; useful in a terminal portal
      aArg->cmdchar = ':';
      aArg->nchar = ZERO;
      nv_colon(aArg);
   } ei (!checkclearop(aArg->oper))
      doPortal(aArg->nchar, aArg->count0, ZERO); // everything is in window.c
}

// CTRL-Z: Suspend
private void
nv_suspend(ActionArg* aArg) {
   clearop(aArg->oper);
   if (VIsual_active)
      end_visual_mode();      // stop Visual mode
   executeCommLine((CS)"stop");
}

// "gv": Reselect the previous Visual area.  If Visual already active, exchange previous and 
// current Visual area.
private void
nv_gv_cmd(ActionArg*) {
   int i;

   if (curBook->visual.vi_start.lnum == 0
       || curBook->visual.vi_start.lnum > curBook->mem.lineCount
       || curBook->visual.vi_end.lnum == 0)
    {
      inpFlushIfNotSilent();
      return;
   }

   // set cursor to the start of the Visual area, tpos to the end
   Pos tpos;
   if (VIsual_active) {
      i = VIsual_mode;
      VIsual_mode = curBook->visual.vi_mode;
      curBook->visual.vi_mode = i;
      curBook->visual.kind = i;
      i = curPor->cursWant;
      curPor->cursWant = curBook->visual.vi_curswant;
      curBook->visual.vi_curswant = i;

      tpos = curBook->visual.vi_end;
      curBook->visual.vi_end = curPor->cursor;
      curPor->cursor = curBook->visual.vi_start;
      curBook->visual.vi_start = VIsual;
   } else {
      VIsual_mode = curBook->visual.vi_mode;
      curPor->cursWant = curBook->visual.vi_curswant;
      tpos = curBook->visual.vi_end;
      curPor->cursor = curBook->visual.vi_start;
   }

   VIsual_active = true;
   VIsual_reselect = true;

   // Set Visual to the start and cursor to the end of the Visual
   // area.  Make sure they are on an existing character.
   check_cursor();
   VIsual = curPor->cursor;
   curPor->cursor = tpos;
   check_cursor();
   update_topline();

   setmouse();
   drawCurBookLater(UPD_INVERTED);
   showmode();
}

// "g0", "g^" : Like "0" and "^" but for screen lines. "gm": middle of "g0" and "g$".
pub void
nv_g_home_m_cmd(ActionArg* aArg) {
   int i;
   Boole flag = false;

   if (aArg->nchar == '^')
      flag = true;

   aArg->oper->motion_type = MCHAR;
   aArg->oper->inclusive = false;
   if (curPor->o.wrap && curPor->width != 0) {
      int width1 = widthLeft(curPor);
      int virtcol;

      validate_virtcol();
      virtcol = curPor->virtCol - curPor->virtColFirstChar;
      i = 0;
      if (virtcol >= (ColNr)width1 && width1 > 0)
          i = (virtcol - width1) / width1 * width1 + width1;

      // When ending up below 'smoothscroll' marker, move just beyond it so
      // that skipcol is not adjusted later.
      if (curPor->skipCol > 0 && curPor->cursor.lnum == curPor->topLine) {
          int overlap = sms_marker_overlap(curPor, curPor->width - width1);
          if (overlap > 0 && i == curPor->skipCol)
         i += overlap;
      }
   } else
      i = curPor->leftCol;
   // Go to the middle of the screen line.  When 'number' or
   // 'relativenumber' is on and lines are wrapping the middle can be more
   // to the left.
   if (aArg->nchar == 'm')
      i += widthLeft(curPor) / 2;
   coladvance((ColNr)i);
   if (flag) {
      do {
         i = gchar_cursor();
      } while (SPACE_OR_TAB(i) && oneright() == OK);
      curPor->cacheState &= ~VALID_WCOL;
   }
   curPor->setCursWant = true;
   if (hasAnyFolding(curPor)) {
      validate_cheight();
      if (curPor->isCursorLineFolded)
         update_curswant_force();
   }
   adjust_skipcol();
}

// "g_": to the last non-blank character in the line or <count> lines downward.
private void
gUnderscoreAction(ActionArg* aArg) {
   aArg->oper->motion_type = MCHAR;
   aArg->oper->inclusive = true;
   curPor->cursWant = MAXCOL;
   if (cursor_down((long)(aArg->count1 - 1), aArg->oper->opTy == OP_NOP) == FAIL) {
      clearopbeep(aArg->oper);
      return;
   }

   CS ptr = ml_get_curline();

   // In Visual mode we may end up after the line.
   if (curPor->cursor.col > 0 && ptr[curPor->cursor.col] == ZERO)
      --curPor->cursor.col;

   // Decrease the cursor column until it's on a non-blank.
   while (curPor->cursor.col > 0 && SPACE_OR_TAB(ptr[curPor->cursor.col]))
      --curPor->cursor.col;
   curPor->setCursWant = true;
}

// "g$" : Like "$" but for screen lines.
private void
nvGDollarAction(ActionArg* aArg) {
   Operator* oper = aArg->oper;
   int i;
   int col_off = normalPortalColumnOffset(curPor);
   Boole flag = false;

   if (aArg->nchar == K_END || aArg->nchar == K_KEND)
      flag = true;

   oper->motion_type = MCHAR;
   oper->inclusive = true;
   if (curPor->o.wrap && curPor->width != 0) {
   curPor->cursWant = MAXCOL;    // so we stay at the end
   if (aArg->count1 == 1) {
      int      width1 = curPor->width - col_off;
      int      virtcol;

      validate_virtcol();
      virtcol = curPor->virtCol - curPor->virtColFirstChar ;
      i = width1 - 1;
      if (virtcol >= (ColNr)width1)
         i += ((virtcol - width1) / width1 + 1) * width1;
      coladvance((ColNr)i);

      // Make sure we stick in this column.
      update_curswant_force();
      if (curPor->cursor.col > 0 && curPor->o.wrap) {
         // Check for landing on a character that got split at
         // the end of the line.  We do not want to advance to the next screen line.
         if (curPor->virtCol - curPor->virtColFirstChar > (ColNr)i)
            --curPor->cursor.col;
       }
   }
   ei (nv_screengo(oper, FORWARD, aArg->count1 - 1) == FAIL)
      clearopbeep(oper);
   } else {
      if (aArg->count1 > 1)
          // if it fails, let the cursor still move to the last char
          (void)cursor_down(aArg->count1 - 1, false);

      i = curPor->leftCol + curPor->width - col_off - 1;
      coladvance((ColNr)i);

      // if the character doesn't fit move one back
      if (curPor->cursor.col > 0 && (*mb_ptr2cells)(ml_get_cursor()) > 1) {
         ColNr vcol;

         bookGetVirtualColInVirtualMode(curPor, &curPor->cursor, NULL, NULL, &vcol);
         if (vcol >= curPor->leftCol + (int)curPor->width - col_off)
            --curPor->cursor.col;
      }

      // Make sure we stick in this column.
      update_curswant_force();
   }
   if (flag) {
      do {
          i = gchar_cursor();
      } while (SPACE_OR_TAB(i) && oneleft() == OK);
      curPor->cacheState &= ~VALID_WCOL;
   }
}

// "gi": start Insert at the last position.
private void
nv_gi_cmd(ActionArg* aArg) {
   int i;

   if (curBook->lastInsert.lnum != 0) {
      curPor->cursor = curBook->lastInsert;
      check_cursor_lnum();
      i = (int)ml_get_curline_len();
      if (curPor->cursor.col > (ColNr)i) {
         if (virtual_active())
            curPor->cursor.coladd += curPor->cursor.col - i;
         curPor->cursor.col = i;
      }
   }
   aArg->cmdchar = 'i';
   nv_edit(aArg);
}


// Commands starting with "g".
private void
nv_g_cmd(ActionArg* aArg) {
   Operator   *oper = aArg->oper;
   int i;

   switch (aArg->nchar) {
   case Ctrl_A:
   case Ctrl_X:
#ifdef MEM_PROFILE
   // "g^A": dump log of used memory.
   if (!VIsual_active && aArg->nchar == Ctrl_A)
       eeMemProfileDump();
   else
#endif
   // "g^A/g^X": sequentially increment visually selected region
   if (VIsual_active) {
      aArg->arg = true;
      aArg->cmdchar = aArg->nchar;
      aArg->nchar = ZERO;
      nvAddSub(aArg);
   } else
      clearopbeep(oper);
   break;

   case '&':
      executeCommLine((CS)"%s//~/&");
      break;

   // "gv": Reselect the previous Visual area.  If Visual already active,
   // exchange previous and current Visual area.
   case 'v':
      nv_gv_cmd(aArg);
      break;

   // "gV": Don't reselect the previous Visual area after a Select mode
   // mapping of menu.
   case 'V':
      VIsual_reselect = false;
      break;

   // "gh":  start Select mode.
   // "gH":  start Select line mode.
   // "g^H": start Select block mode.
   case K_BS:
      aArg->nchar = Ctrl_H;
      // FALLTHROUGH

   // "gn", "gN" visually select next/previous search match
   // "gn" selects next match
   // "gN" selects previous match
   case 'N':
   case 'n':
      if (!current_search(aArg->count1, aArg->nchar == 'n'))
         clearopbeep(oper);
      break;

   // "gj" and "gk" two new funny movement keys -- up and down
   // movement based on *screen* line rather than *file* line.
   case 'j':
   case K_DOWN:
      // with 'nowrap' it works just like the normal "j" command.
      if (!curPor->o.wrap) {
          oper->motion_type = MLINE;
          i = cursor_down(aArg->count1, oper->opTy == OP_NOP);
      } else
         i = nv_screengo(oper, FORWARD, aArg->count1);
      if (i == FAIL)
         clearopbeep(oper);
      break;

   case 'k':
   case K_UP:
      // with 'nowrap' it works just like the normal "k" command.
      if (!curPor->o.wrap) {
          oper->motion_type = MLINE;
          i = cursor_up(aArg->count1, oper->opTy == OP_NOP);
      } else
          i = nv_screengo(oper, BACKWARD, aArg->count1);
      if (i == FAIL)
          clearopbeep(oper);
      break;

   // "gJ": join two lines without inserting a space.
   case 'J':
       nvJoin(aArg);
   break;

   // "g0", "g^" : Like "0" and "^" but for screen lines.
   // "gm": middle of "g0" and "g$".
   case '^':
   case '0':
   case 'm':
   case K_HOME:
   case K_KHOME:
       nv_g_home_m_cmd(aArg);
       break;

   case 'M': {
      oper->motion_type = MCHAR;
      oper->inclusive = false;
      i = linetabsize_no_outer(curPor, curPor->cursor.lnum);
      if (aArg->count0 > 0 && aArg->count0 <= 100)
         coladvance((ColNr)(i * aArg->count0 / 100));
      else
         coladvance((ColNr)(i / 2));
      curPor->setCursWant = true;
   }
   break;

   // "g_": to the last non-blank character in the line or <count> lines
   // downward.
   case '_':
      gUnderscoreAction(aArg);
      break;

   // "g$" : Like "$" but for screen lines.
   case '$':
   case K_END:
   case K_KEND:
       nvGDollarAction(aArg);
       break;

   // "g*" and "g#", like "*" and "#" but without using "\<" and "\>"
   case '*':
   case '#':
#if POUND != '#'
   case POUND:      // pound sign (sometimes equal to '#')
#endif
   case Ctrl_RSB:      // :tag or :tselect for current identifier
   case ']':         // :tselect for current identifier
      nv_ident(aArg);
      break;

   // ge and gE: go back to end of word
   case 'e':
   case 'E':
      oper->motion_type = MCHAR;
      curPor->setCursWant = true;
      oper->inclusive = true;
      if (bckend_word(aArg->count1, aArg->nchar == 'E', false) == FAIL)
          clearopbeep(oper);
      break;

   // "g CTRL-G": display info about cursor position
   case Ctrl_G:
      cursor_pos_info(NULL);
      break;

   // "gi": start Insert at the last position.
   case 'i':
      nv_gi_cmd(aArg);
      break;

   // "gI": Start insert in column 1.
   case 'I':
      beginline(0);
      if (!checkclearopq(oper))
          invokeEdit(aArg, false, 'g', false);
      break;

   // "gf": goto file, edit file under cursor
   // "]f" and "[f": can also be used.
   case 'f':
   case 'F':
      nv_gotofile(aArg);
      break;

   // "g'm" and "g`m": jump to mark without setting pcmark
   case '\'':
      aArg->arg = true;
      // FALLTHROUGH
   case '`':
      nv_gomark(aArg);
      break;

   // "gs": Goto sleep.
   case 's':
      do_sleep(aArg->count1 * 1000L, false);
      break;

   // "ga": Display the ascii value of the character under the
   // cursor.   It is displayed in decimal, hex. -- webb
   case 'a':
      do_ascii(NULL);
      break;

   // "g8": Display the bytes used for the UTF-8 character under the
   // cursor.   It is displayed in hex. "8g8" finds illegal byte sequence.
   case '8':
      if (aArg->count0 == 8)
         utf_find_illegal();
      else
         show_utf8();
      break;

   // "g<": show scrollback text
   case '<':
      show_sb_text();
      break;

   // "gg": Goto the first line in file.  With a count it goes to
   // that line number like for "G". -- webb
   case 'g':
      aArg->arg = false;
      nv_goto(aArg);
      break;

   //    Two-character operators:
   //    "gq"       Format text
   //    "gw"       Format text and keep cursor position
   //    "g~"       Toggle the case of the text.
   //    "gu"       Change text to lower case.
   //    "gU"       Change text to upper case.
   //   "g?"       rot13 encoding
   //   "g@"       call 'operatorfunc'
   case 'q':
   case 'w':
      oper->cursor_start = curPor->cursor;
      // FALLTHROUGH
   case '~':
   case 'u':
   case 'U':
   case '?':
   case '@':
      nv_operator(aArg);
      break;

   // "gd": Find first occurrence of pattern under the cursor in the
   //    current function
   // "gD": idem, but in the current file.
   case 'd':
   case 'D':
      nv_gd(oper, aArg->nchar, (int)aArg->count0);
      break;

   // g<*Mouse> : <C-*mouse>
   case K_MIDDLEMOUSE:
   case K_MIDDLEDRAG:
   case K_MIDDLERELEASE:
   case K_LEFTMOUSE:
   case K_LEFTDRAG:
   case K_LEFTRELEASE:
   case K_MOUSEMOVE:
   case K_RIGHTMOUSE:
   case K_RIGHTDRAG:
   case K_RIGHTRELEASE:
   case K_X1MOUSE:
   case K_X1DRAG:
   case K_X1RELEASE:
   case K_X2MOUSE:
   case K_X2DRAG:
   case K_X2RELEASE:
      modMaskG = MOD_MASK_CTRL;
      (void)do_mouse(oper, aArg->nchar, BACKWARD, aArg->count1, 0);
      break;

   case K_IGNORE:
      break;

   // "gP" and "gp": same as "P" and "p" but leave cursor just after new text
   case 'p':
   case 'P':
   nv_put(aArg);
   break;

   // "go": goto byte count from start of buffer
   case 'o':
      oper->inclusive = false;
      goto_byte(aArg->count0);
      break;

   case ',':
      nv_pcmark(aArg);
      break;

   case ';':
      aArg->count1 = -aArg->count1;
      nv_pcmark(aArg);
      break;

   case 't':
      if (!checkclearop(oper))
          gotoTabById((int)aArg->count0);
      break;
   case 'T':
      if (!checkclearop(oper))
         gotoTabById(-(int)aArg->count1);
      break;

   case TAB:
      if (!checkclearop(oper) && goto_tabpage_lastused() == FAIL)
          clearopbeep(oper);
      break;

   case '+':
   case '-': // "g+" and "g-": undo or redo along the timeline
      if (!checkclearopq(oper))
         undo_time(aArg->nchar == '-' ? -aArg->count1 : aArg->count1,
                         false, false, false);
      break;

   default:
      clearopbeep(oper);
      break;
   }
}


// "." action: redo last change.
private void
nvDot(ActionArg* aArg) {
   if (checkclearopq(aArg->oper))
      return;

   // If "restart_edit" is true, the last but one command is repeated
   // instead of the last command (inserting text). This is used for
   // CTRL-O <.> in insert mode.
   if (start_redo(aArg->count0, restart_edit != 0 && !arrow_used) == FAIL)
      clearopbeep(aArg->oper);
}

// CTRL-R: undo undo or specify register in select mode
private void
nv_redo_or_register(ActionArg* aArg) {
   if (checkclearopq(aArg->oper))
      return;

   u_redo((int)aArg->count1);
   curPor->setCursWant = true;
}

// Handle "U" action.
private void
nv_Undo(ActionArg* aArg) {
   // In Visual mode and typing "gUU" triggers an operator
   if (aArg->oper->opTy == OP_UPPER || VIsual_active) {
      // translate "gUU" to "gUgU"
      aArg->cmdchar = 'g';
      aArg->nchar = 'U';
      nv_operator(aArg);
      return;
   }

   if (checkclearopq(aArg->oper))
      return;

    u_undoline();
    curPor->setCursWant = true;
}

// '~' action: If tilde is not an operator and Visual is off: swap case of a single character.
private void
nv_tilde(ActionArg* aArg) {
   if (!VIsual_active && aArg->oper->opTy != OP_TILDE) {
      if (bt_prompt(curBook) && !prompt_curpos_editable()) {
         clearopbeep(aArg->oper);
         return;
      }
      n_swapchar(aArg);
   } else
      nv_operator(aArg);
}

// Set v:operator to the characters for "optype".
private void
set_op_var(int optype) {

   if (optype == OP_NOP)
      set_EeglVar_string(VV_OP, NULL, 0);
   else {
      Byte opchars[3] = { get_op_char(optype), get_extra_op_char(optype), ZERO};
      set_EeglVar_string(VV_OP, opchars, -1);
   }
}

//Handle an operator action. The actual work is done by doExecuteVisualOperator().
private void
nv_operator(ActionArg* aArg) { //:nv_operator
   Unt opTy = get_op_type(aArg->cmdchar, aArg->nchar);
   if (bt_prompt(curBook) && op_is_change(opTy) && !prompt_curpos_editable()) {
      clearopbeep(aArg->oper);
      return;
   }

   if (opTy == aArg->oper->opTy)       // double operator works on lines
      a_linewiseOperator(aArg);
   ei (!checkclearop(aArg->oper)) {
      aArg->oper->start = curPor->cursor;
      aArg->oper->opTy = opTy;
      set_op_var(opTy);
   }
}

// Handle linewise operator "dd", "yy", etc.
//
// "_" is is a strange motion command that helps make operators more logical. It is actually 
// implemented, but not documented in the real Vi. This motion command actually refers to 
// "the current line". Commands like "dd" and "yy" are really an alternate form of "d_" and "y_".
// It does accept a count, so "d3_" works to delete 3 lines.
pub void
a_linewiseOperator(ActionArg* aArg) {
   aArg->oper->motion_type = MLINE;
   if (cursor_down(aArg->count1 - 1L, aArg->oper->opTy == OP_NOP) == FAIL)
      clearopbeep(aArg->oper);
   ei (  (aArg->oper->opTy == OP_DELETE // only with linewise motions
         && aArg->oper->motion_force != 'v'
         && aArg->oper->motion_force != Ctrl_V)
          || aArg->oper->opTy == OP_LSHIFT
          || aArg->oper->opTy == OP_RSHIFT)
      beginline(BL_SOL | BL_FIX);
   ei (aArg->oper->opTy != OP_YANK)   // 'Y' does not move cursor
      beginline(BL_WHITE | BL_FIX);
}

// <Home> action
private void
nv_home(ActionArg* aArg) {
   // CTRL-HOME is like "gg"
   if (modMaskG & MOD_MASK_CTRL)
      nv_goto(aArg);
   else {
      aArg->count0 = 1;
      nv_pipe(aArg);
   }
   ins_at_eol = false;       // Don't move cursor past eol (only necessary in a one-character line).
}

// "|" action
private void
nv_pipe(ActionArg* aArg) {
   aArg->oper->motion_type = MCHAR;
   aArg->oper->inclusive = false;
   beginline(0);
   if (aArg->count0 > 0) {
      coladvance((ColNr)(aArg->count0 - 1));
      curPor->cursWant = (ColNr)(aArg->count0 - 1);
   } else
      curPor->cursWant = 0;
   // keep curswant at the column where we wanted to go, not where
   // we ended; differs if line is too short
   curPor->setCursWant = false;
}

// Handle back-word action "b" and "B". aArg->arg is 1 for "B"
private void
nv_bck_word(ActionArg* aArg) {
   aArg->oper->motion_type = MCHAR;
   aArg->oper->inclusive = false;
   curPor->setCursWant = true;
   if (bck_word(aArg->count1, aArg->arg, false) == FAIL)
      clearopbeep(aArg->oper);
   ei ((p_fdo & FDO_HOR) && keyWasTypedG && aArg->oper->opTy == OP_NOP)
      foldOpenCursor();
}

// Handle word motion commands "e", "E", "w" and "W". aArg->arg is true for "E" and "W".
private void
nv_wordcmd(ActionArg* aArg) {
   int      n;
   Boole flag = false;
   Pos   startpos = curPor->cursor;

   // Set inclusive for the "E" and "e" command.
   Boole wordEnd = (aArg->cmdchar == 'e' || aArg->cmdchar == 'E');
   aArg->oper->inclusive = wordEnd;

   // "cw" and "cW" are a special case.
   if (!wordEnd && aArg->oper->opTy == OP_CHANGE) {
      n = gchar_cursor();
      if (n != ZERO) {        // not an empty line
         if (SPACE_OR_TAB(n)) {
         } else {
            aArg->oper->inclusive = true;
            wordEnd = true;
            flag = true;
         }
      }
   }

   aArg->oper->motion_type = MCHAR;
   curPor->setCursWant = true;
   if (wordEnd)
       n = end_word(aArg->count1, aArg->arg, flag, false);
   else
       n = fwd_word(aArg->count1, aArg->arg, aArg->oper->opTy != OP_NOP);

   // Don't leave the cursor on the ZERO past the end of line. Unless we didn't move it forward.
   if (LT_POS(startpos, curPor->cursor))
      adjust_cursor(aArg->oper);

   if (n == FAIL && aArg->oper->opTy == OP_NOP)
       clearopbeep(aArg->oper);
   else {
      if ((p_fdo & FDO_HOR) && keyWasTypedG && aArg->oper->opTy == OP_NOP)
         foldOpenCursor();
   }
}


// "0" and "^" actions. aArg->arg is the argument for beginline().
private void
nv_beginline(ActionArg* aArg) {
   aArg->oper->motion_type = MCHAR;
   aArg->oper->inclusive = false;
   beginline(aArg->arg);
   if ((p_fdo & FDO_HOR) && keyWasTypedG && aArg->oper->opTy == OP_NOP)
      foldOpenCursor();
   ins_at_eol = false; // Don't move cursor past eol (only necessary in a one-character line).
}


// "G", "gg", CTRL-END, CTRL-HOME. aArg->arg is true for "G".
private void
nv_goto(ActionArg* aArg) {
   LineNr lnum = (aArg->arg) ? curBook->mem.lineCount : 1L;
   aArg->oper->motion_type = MLINE;
   setpcmark();

   // When a count is given, use it instead of the default lnum
   if (aArg->count0 != 0)
      lnum = aArg->count0;
   if (lnum < 1L)
      lnum = 1L;
   ei (lnum > curBook->mem.lineCount)
      lnum = curBook->mem.lineCount;
   curPor->cursor.lnum = lnum;
   beginline(BL_SOL | BL_FIX);
   if ((p_fdo & FDO_JUMP) && keyWasTypedG && aArg->oper->opTy == OP_NOP)
      foldOpenCursor();
}

// CTRL-\ in Normal mode.
private void
nv_normal(ActionArg* aArg) {
   if (aArg->nchar == Ctrl_N || aArg->nchar == Ctrl_G)     {
      clearop(aArg->oper);
      if (restart_edit != 0 && isModeDisplayedG)
         mustClearCommlineG = true;      // unshow mode later
      restart_edit = 0;
      if (commPortTypeG != 0)
         commPortResultG = Ctrl_C;
      if (VIsual_active)    {
          end_visual_mode();      // stop Visual
          drawCurBookLater(UPD_INVERTED);
      } 
   } else 
      clearopbeep(aArg->oper);
}

// ESC in Normal mode: beep, but don't flush buffers.
private void
nv_esc(ActionArg* aArg) {
   int      no_reason;

   no_reason = (aArg->oper->opTy == OP_NOP
      && aArg->opcount == 0
      && aArg->count0 == 0
      && aArg->oper->regname == 0
   );

   if (aArg->arg) {      // true for CTRL-C
      if (restart_edit == 0 && commPortTypeG == 0 && !VIsual_active && no_reason) {
         int   out_redir = !stdout_isatty && !is_not_a_term_or_gui();

         //The user may accidentally do "eegl file | grep word" and then
         //CTRL-C doesn't show anything.  With a changed buffer give the
         //message on stderr.  Without any changes might as well exit.
         if (wasAnyBookChanged()) {
            CS ms = _("Type  :qa!  and press <Enter> to abandon all changes and exit Eegl");

            if (out_redir)
               mch_errmsg(ms);
            else
               msg(ms);
         } else {
            if (out_redir) {
               gotInterruptG = false;
               executeCommLine((CS)"qa");
            } else
               msg(_("Type  :qa  and press <Enter> to exit Eegl"));
         }
      }

      if (restart_edit != 0)
         redrawModeG = true;  // remove "-- (insert) --"

      restart_edit = 0;
      if (commPortTypeG != 0)    {
         commPortResultG = K_IGNORE;
         gotInterruptG = false;   // don't stop executing autocommands et al.
         return;
      }
   } ei (commPortTypeG != 0 && ex_normal_busy && typebuf_was_empty) {
      //When :normal runs out of characters while in the command line portal
      //vgetorpeek() will repeatedly return ESC.  Exit the commline portal to break the loop.
      commPortResultG = K_IGNORE;
      return;
   }

   if (VIsual_active) {
      end_visual_mode();   // stop Visual
      check_cursor_col();   // make sure cursor is not beyond EOL
      curPor->setCursWant = true;
      drawCurBookLater(UPD_INVERTED);
   } ei (no_reason) {
   if (!aArg->arg && popup_message_win_visible())
       popup_hide_messagePort();
   }
   clearop(aArg->oper);
}

// Handle "A", "a", "I", "i" and <Insert> commands. Also handle K_PS, start bracketed paste.
private void
nv_edit(ActionArg* aArg) {
   //<Insert> is equal to "i"
   if (aArg->cmdchar == K_INS || aArg->cmdchar == K_KINS)
      aArg->cmdchar = 'i';

   // in Visual mode "A" and "I" are an operator
   if (VIsual_active && (aArg->cmdchar == 'A' || aArg->cmdchar == 'I')) {
      if (term_in_normal_mode()) {
         end_visual_mode();
         clearop(aArg->oper);
         term_enter_job_mode();
         return;
      }
      vVisualOperators(aArg);
   }

   // in Visual mode and after an operator "a" and "i" are for text objects
   ei ((aArg->cmdchar == 'a' || aArg->cmdchar == 'i')
         && (aArg->oper->opTy != OP_NOP || VIsual_active)) {
      nv_object(aArg);
   } ei (term_in_normal_mode()) {
      clearop(aArg->oper);
      term_enter_job_mode();
      return;
   } ei (IMMUTABLE) {
      emsg(_(e_cannot_make_changes_modifiable_is_off));
      clearop(aArg->oper);
      if (aArg->cmdchar == K_PS)
         // drop the pasted text
         bracketed_paste(PASTE_INSERT, true, NULL);
   } ei (aArg->cmdchar == K_PS && VIsual_active) {
      Pos old_pos = curPor->cursor;
      Pos old_visual = VIsual;
      int old_visual_mode = VIsual_mode;

      // In Visual mode the selected text is deleted.
      if (VIsual_mode == 'V' || curPor->cursor.lnum != VIsual.lnum)    {
         shift_delete_registers();
         aArg->oper->regname = '1';
      } else
         aArg->oper->regname = '-';
      aArg->cmdchar = 'd';
      aArg->nchar = ZERO;
      nv_operator(aArg);
      doExecuteVisualOperator(aArg, 0, false);
      aArg->cmdchar = K_PS;

      if (*ml_get_cursor() != ZERO) {
         if (old_visual_mode == 'V') {
            //In linewise Visual mode insert before the beginning of the next line. When the last 
            //line in the book was deleted then create a new line, otherwise there is not need to 
            //move cursor. Detect this by checking if cursor moved above Visual area.
            if (curPor->cursor.lnum < old_pos.lnum && curPor->cursor.lnum < old_visual.lnum
                  && u_save_cursor() == OK
            ) {
               ml_append(curPor->cursor.lnum, S"", 0, false);
               appended_lines(curPor->cursor.lnum++, 1L);
            }
         }
         //When the last char in the line was deleted then append.
         //Detect this by checking if cursor moved before Visual area.
         ei (curPor->cursor.col < old_pos.col && curPor->cursor.col < old_visual.col)
            inc_cursor();
      }

      // Insert to replace the deleted text with the pasted text.
      invokeEdit(aArg, false, aArg->cmdchar, false);
   } ei (!checkclearopq(aArg->oper)) {
      switch (aArg->cmdchar) {
      case 'A':   // "A"ppend after the line
         set_cursor_for_append_to_line();
         break;

      case 'I':   // "I"nsert before the first non-blank
         beginline(BL_WHITE);
         break;

      case K_PS:
         // Bracketed paste works like "a"ppend, unless the cursor is in
         // the first column, then it inserts.
         if (curPor->cursor.col == 0)
             break;
         // FALLTHROUGH

      case 'a':   // "a"ppend is like "i"nsert on the next character.
         // increment coladd when in virtual space, increment the
         // column otherwise, also to append after an unprintable char
         if (virtual_active()
                && (curPor->cursor.coladd > 0
               || *ml_get_cursor() == ZERO
               || *ml_get_cursor() == TAB)) {
            curPor->cursor.coladd++;
         } ei (*ml_get_cursor() != ZERO)
            inc_cursor();
         break;
      }

      if (curPor->cursor.coladd && aArg->cmdchar != 'A') {
         int save_State = stateG;

         // Pretend Insert mode here to allow the cursor on the character past the end of the line
         stateG = MODE_INSERT;
         coladvance(getviscol());
         stateG = save_State;
      }

      invokeEdit(aArg, false, aArg->cmdchar, false);
   } ei (aArg->cmdchar == K_PS) {
      // drop the pasted text
      bracketed_paste(PASTE_INSERT, true, NULL);
   } 
}

// "o" and "O" commands.
private void
nvOpen(ActionArg* aArg) {
   // "do" is ":diffget"
   if (aArg->oper->opTy == OP_DELETE && aArg->cmdchar == 'o') {
      clearop(aArg->oper);
      nvDiffGetPut(false, aArg->opcount);
   } else {
      if (VIsual_active)  // switch start and end of visual
         v_swap_corners(aArg->cmdchar);
      ei (bt_prompt(curBook))
         clearopbeep(aArg->oper);
      else
         nOpenAction(aArg);
   }
}

private void
nv_drop(ActionArg*) {
   do_put('~', NULL, BACKWARD, 1L, PUT_CURSEND);
}

// Trigger CursorHold event.
// When waiting for a character for 'updatetime' K_CURSORHOLD is put in the
// input buffer.  "did_cursorhold" is set to avoid retriggering.
private void
nv_cursorhold(ActionArg* aArg) {
   applyAutocomms(EVENT_CURSORHOLD, NULL, NULL, false, curBook);
   did_cursorhold = true;
   aArg->retval |= CA_COMMAND_BUSY;   // don't call edit() now
}

// "a" or "i" while an operator is pending or in Visual mode: object motion.
private void
nv_object(ActionArg* aArg) {
   int flag;
   Boole include;

   if (aArg->cmdchar == 'i')
      include = false;    // "ix" = inner object: exclude white space
   else
      include = true;       // "ax" = an object: include white space

   // Make sure (), [], {} and <> are in 'matchpairs'
   CS mps_save = curBook->o.matchPairs;
   curBook->o.matchPairs = S"(:),{:},[:],<:>";

   switch (aArg->nchar) {
   case 'w': // "aw" = a word
      flag = current_word(aArg->oper, aArg->count1, include, false);
      break;
   case 'W': // "aW" = a WORD
      flag = current_word(aArg->oper, aArg->count1, include, true);
      break;
   case 'b': // "ab" = a braces block
   case '(':
   case ')':
      flag = current_block(aArg->oper, aArg->count1, include, '(', ')');
      break;
   case 'B': // "aB" = a Brackets block
   case '{':
   case '}':
      flag = current_block(aArg->oper, aArg->count1, include, '{', '}');
      break;
   case '[': // "a[" = a [] block
   case ']':
      flag = current_block(aArg->oper, aArg->count1, include, '[', ']');
      break;
   case '<': // "a<" = a <> block
   case '>':
      flag = current_block(aArg->oper, aArg->count1, include, '<', '>');
      break;
   case 't': // "at" = a tag block (xml and html)
      // Do not adjust oper->end in doExecuteVisualOperator()
      // otherwise there are different results for 'dit'
      // (note leading whitespace in last line):
      // 1) <b>      2) <b>
      //    foobar      foobar
      //    </b>            </b>
      aArg->retval |= CA_NO_ADJ_OP_END;
      flag = current_tagblock(aArg->oper, aArg->count1, include);
      break;
   case 'p': // "ap" = a paragraph
      flag = current_par(aArg->oper, aArg->count1, include, 'p');
      break;
   case 's': // "as" = a sentence
      flag = current_sent(aArg->oper, aArg->count1, include);
      break;
   case '"': // "a"" = a double quoted string
   case '\'': // "a'" = a single quoted string
   case '`': // "a`" = a backtick quoted string
      flag = current_quote(aArg->oper, aArg->count1, include, aArg->nchar);
      break;
#if 0   // TODO
   case 'S': // "aS" = a section
   case 'f': // "af" = a filename
   case 'u': // "au" = a URL
#endif
   default:
      flag = FAIL;
      break;
   }

   curBook->o.matchPairs = mps_save;
   if (flag == FAIL)
      clearopbeep(aArg->oper);
   adjust_cursor_col();
   curPor->setCursWant = true;
}

// "q" action: Start/stop recording.
// "q:", "q/", "q?": edit command-line in command-line portal.
private void
nv_record(ActionArg* aArg) {
   if (aArg->oper->opTy == OP_FORMAT) {
      // "gqq" is the same as "gqgq": format line
      aArg->cmdchar = 'g';
      aArg->nchar = 'q';
      nv_operator(aArg);
      return;
   }

   if (checkclearop(aArg->oper))
      return;

   if (aArg->nchar == ':' || aArg->nchar == '/' || aArg->nchar == '?')     {
      if (commPortTypeG != 0)    {
         emsg(_(e_cmdline_window_already_open));
         return;
      }
      stuffcharReadbuff(aArg->nchar);
      stuffcharReadbuff(K_COMMPORT);
   } else {
      // (stop) recording into a named register, unless executing a
      // register
      if (reg_executing == 0 && do_record(aArg->nchar) == FAIL)
         clearopbeep(aArg->oper);
   }
}

// Handle the "@r" action.
private void
nv_at(ActionArg* aArg) {
   if (checkclearop(aArg->oper) || (aArg->nchar == '=' && get_expr_register() == ZERO))
      return;
   while (aArg->count1-- && !gotInterruptG)     {
      if (do_execreg(aArg->nchar, false, false, false) == FAIL) {
         clearopbeep(aArg->oper);
         break;
      }
      line_breakcheck();
   }
}

// Handle the CTRL-U and CTRL-D actions.
private void
nv_halfpage(ActionArg* aArg) {
   int   dir = aArg->cmdchar == Ctrl_D ? FORWARD : BACKWARD;
   if (!checkclearop(aArg->oper))
      pagescroll(dir, aArg->count0, true);
}

// Handle "J" or "gJ" action.
private void
nvJoin(ActionArg* aArg) {
   if (VIsual_active) {  // join the visual lines
      nv_operator(aArg);
      return;
   }

   if (checkclearop(aArg->oper))
      return;

   if (aArg->count0 <= 1)
      aArg->count0 = 2;       // default for join is two lines!
   if (curPor->cursor.lnum + aArg->count0 - 1 > curBook->mem.lineCount) {
      // can't join when on the last line
      if (aArg->count0 <= 2)  {
         clearopbeep(aArg->oper);
         return;
      }
      aArg->count0 = curBook->mem.lineCount - curPor->cursor.lnum + 1;
   }

   prep_redo(aArg->oper->regname, aArg->count0, ZERO, aArg->cmdchar, ZERO, ZERO, aArg->nchar);
   (void)doJoinLinesUnderCursor(aArg->count0, aArg->nchar == ZERO, true, true, true);
}

// "P", "gP", "p" and "gp" actions.
private void
nv_put(ActionArg* aArg) {
   nv_put_opt(aArg, false);
}

// "P", "gP", "p" and "gp" actions.
// "fix_indent" is true for "[p", "[P", "]p" and "]P".
private void
nv_put_opt(ActionArg* aArg, int fix_indent) {
   int regname = 0;
   void* reg1 = NULL;
   int empty = false;
   int was_visual = false;
   int dir;
   Unt flags = 0;
   int save_fen = curPor->o.foldEnable;

   if (aArg->oper->opTy != OP_NOP) {
      // "dp" is ":diffput"
      if (aArg->oper->opTy == OP_DELETE && aArg->cmdchar == 'p') {
         clearop(aArg->oper);
         nvDiffGetPut(true, aArg->opcount);
      } else
         clearopbeep(aArg->oper);
      return;
   }

   if (bt_prompt(curBook) && !prompt_curpos_editable()) {
      clearopbeep(aArg->oper);
      return;
   }

   if (fix_indent) {
      dir = (aArg->cmdchar == ']' && aArg->nchar == 'p') ? FORWARD : BACKWARD;
      flags |= PUT_FIXINDENT;
   } else {
      dir = (aArg->cmdchar == 'P' 
            || ((aArg->cmdchar == 'g' || aArg->cmdchar == 'z') && aArg->nchar == 'P')) 
	   ? BACKWARD : FORWARD;
   } 
   prepareForRedo(aArg);
   if (aArg->cmdchar == 'g')
      flags |= PUT_CURSEND;
   ei (aArg->cmdchar == 'z')
      flags |= PUT_BLOCK_INNER;

   if (VIsual_active) {
      // Putting in Visual mode: The put text replaces the selected text. First delete the selected 
      // text, then put the new text. Need to save and restore the registers that the delete
      // overwrites if the old contents is being put.
      was_visual = true;
      regname = aArg->oper->regname;
      clipGetDefaultRegister(&regname);
      if (regname == 0 || regname == '"'
          || EE_ISDIGIT(regname) || regname == '-'
          || (regname == '*' || regname == '+')
      ) {
           // The delete is going to overwrite the register we want to put, save it first.
           //reg1 = get_register(regname, true);
      }

      // Temporarily disable folding, as deleting a fold marker may cause
      // the cursor to be included in a fold.
      curPor->o.foldEnable = false;

      // Now delete the selected text. Avoid messages here.
      aArg->cmdchar = 'd';
      aArg->nchar = ZERO;
      aArg->oper->regname = '_';
      ++msg_silent;
      
      nv_operator(aArg);
      doExecuteVisualOperator(aArg, 0, false);
      
      empty = (curBook->mem.flags & ML_EMPTY);
      --msg_silent;
      
      // delete PUT_LINE_BACKWARD;
      aArg->oper->regname = regname;

      if (reg1 != NULL) {
          // Then put back what was in the register before the delete.
          //put_register(regname, reg1);
      }

      // When deleted a linewise Visual area, put the register as
      // lines to avoid it joined with the next line.  When deletion was
      // characterwise, split a line when putting lines.
      if (VIsual_mode == 'V')
         flags |= PUT_LINE;
      ei (VIsual_mode == 'v')
         flags |= PUT_LINE_SPLIT;
      if (VIsual_mode == Ctrl_V && dir == FORWARD)
         flags |= PUT_LINE_FORWARD;
      dir = BACKWARD;
      if ((VIsual_mode != 'V' && curPor->cursor.col < curBook->opStart.col)
            || (VIsual_mode == 'V' && curPor->cursor.lnum < curBook->opStart.lnum))
         // cursor is at the end of the line or end of file, put forward.
         dir = FORWARD;
      // May have been reset in do_put().
      VIsual_active = true;
   }
   do_put(aArg->oper->regname, NULL, dir, aArg->count1, flags);

   if (was_visual) {
      if (save_fen)
         curPor->o.foldEnable = true;
      // What to reselect with "gv"?  Selecting the just put text seems to be the most useful, 
      // since the original text was removed.
      curBook->visual.vi_start = curBook->opStart;
      curBook->visual.vi_end = curBook->opEnd;
   }

   // When all lines were selected and deleted do_put() leaves an empty
   // line that needs to be deleted now.
   if (empty && *ml_get(curBook->mem.lineCount) == ZERO) {
      ml_delete_flags(curBook->mem.lineCount, ML_DEL_MESSAGE);
      deleted_lines(curBook->mem.lineCount + 1, 1);

      // If the cursor was in that line, move it to the end of the last line.
      if (curPor->cursor.lnum > curBook->mem.lineCount) {
         curPor->cursor.lnum = curBook->mem.lineCount;
         coladvance((ColNr)MAXCOL);
      }
   }
   auto_format(false, true);
}

//}}}
//{{{normal mode only actions

// Swap case for "~" command, when it does not work like an operator.
private void
n_swapchar(ActionArg* aArg) {
   long   n;
   Pos   startpos;
   int      did_change = 0;

   if (checkclearopq(aArg->oper))
      return;

   if (LINEEMPTY(curPor->cursor.lnum) && p_ww && firstOccurrence(p_ww, '~') == NULL) {
      clearopbeep(aArg->oper);
      return;
   }

    prepareForRedo(aArg);

   if (u_save_cursor() == FAIL)
   return;

   startpos = curPor->cursor;
   for (n = aArg->count1; n > 0; --n) {
      did_change |= swapchar(aArg->oper->opTy, &curPor->cursor);
      inc_cursor();
      if (gchar_cursor() == ZERO) {
         if (p_ww && firstOccurrence(p_ww, '~') != NULL 
               && curPor->cursor.lnum < curBook->mem.lineCount
         ) {
            ++curPor->cursor.lnum;
            curPor->cursor.col = 0;
            if (n > 1) {
               if (u_savesub(curPor->cursor.lnum) == FAIL)
                  break;
               u_clearline();
            }
         } else
            break;
      }
   }

   check_cursor();
   curPor->setCursWant = true;
   if (did_change) {
      doChangedLines(startpos.lnum, startpos.col, curPor->cursor.lnum + 1, 0L);
      curBook->opStart = startpos;
      curBook->opEnd = curPor->cursor;
      if (curBook->opEnd.col > 0)
          --curBook->opEnd.col;
   }
}

// Handle "o" and "O" actions in normal mode.
private void
nOpenAction(ActionArg* aArg) {
   if (checkclearopq(aArg->oper)) {
      return;
   }
   if (aArg->cmdchar == 'O') {
      // Open above the first line of a folded sequence of lines
      (void)getFolds(curPor->cursor.lnum, OUT &curPor->cursor.lnum, NULL);
   } else {
      // Open below the last line of a folded sequence of lines
      (void)getFolds(curPor->cursor.lnum, NULL, OUT &curPor->cursor.lnum);
   }
   // trigger TextChangedI for the 'o/O' command
   curBook->lastChangeTickInsert = CHANGEDTICK(curBook);
   if (u_save(
             (LineNr)(curPor->cursor.lnum - (aArg->cmdchar == 'O' ? 1 : 0)),
             (LineNr)(curPor->cursor.lnum + (aArg->cmdchar == 'o' ? 1 : 0))
        ) == OK
        && insertLine(aArg->cmdchar == 'O' ? BACKWARD : FORWARD) == OK
   ) {
      if (curPor->o.cursorLine) {
         // force redraw of cursorline
         curPor->cacheState &= ~VALID_CROW;
      }
   }
}

// Start Visual mode "c".
private void
n_start_visual_mode(int c) {
   VIsual_mode = c;
   VIsual_active = true;
   VIsual_reselect = true;

   // Corner case: the 0 position in a tab may change when going into
   // virtualedit.  Recalculate curPor->cursor to avoid bad highlighting.
   if (c == Ctrl_V && gchar_cursor() == TAB) {
      validate_virtcol();
      coladvance(curPor->virtCol);
   }
   VIsual = curPor->cursor;

   foldAdjustVisual();

   may_trigger_modechanged();
   setmouse();

   if (p_smd && msg_silent == 0)
       redrawCommlineG = true;   // show visual mode later

   // Only need to redraw this line, unless still need to redraw an old
   // Visual area (when 'lazyredraw' is set).
   if (curPor->redrawType < UPD_INVERTED) {
      curPor->prevVisualEnd = curPor->cursor.lnum;
      curPor->oldVisualLnum = curPor->cursor.lnum;
   }
}

//}}}
//{{{visual mode only actions

// Handle actions that are operators in Visual mode.
private void
vVisualOperators(ActionArg* aArg) {
   static Byte trans[] = "YyDdCcxdXdAAIIrr"; // Y => y, D => d etc

   //Uppercase means linewise, except in block mode, then "D" deletes till
   //the end of the line, and "C" replaces till EOL
   if (SAFE_isupper(aArg->cmdchar)) {
      if (VIsual_mode != Ctrl_V) {
         VIsual_mode_orig = VIsual_mode;
         VIsual_mode = 'V';
      } ei (aArg->cmdchar == 'C' || aArg->cmdchar == 'D')
         curPor->cursWant = MAXCOL;
   }
   aArg->cmdchar = *(firstOccurrence(trans, aArg->cmdchar) + 1);
   nv_operator(aArg);
}

// 'o': Exchange start and end of Visual area.
// 'O': same, but in block mode exchange left and right corners.
private void
v_swap_corners(int cmdchar) {
   Pos   old_cursor;
   ColNr   left, right;

   if (cmdchar == 'O' && VIsual_mode == Ctrl_V) {
      old_cursor = curPor->cursor;
      getvcols(curPor, &old_cursor, &VIsual, &left, &right);
      curPor->cursor.lnum = VIsual.lnum;
      coladvance(left);
      VIsual = curPor->cursor;

      curPor->cursor.lnum = old_cursor.lnum;
      curPor->cursWant = right;
      coladvance(curPor->cursWant);
      if (curPor->cursor.col == old_cursor.col
            && (!virtual_active() || curPor->cursor.coladd == old_cursor.coladd)) {
         curPor->cursor.lnum = VIsual.lnum;
         coladvance(right);
         VIsual = curPor->cursor;

         curPor->cursor.lnum = old_cursor.lnum;
         coladvance(left);
         curPor->cursWant = left;
      }
   } else {
      old_cursor = curPor->cursor;
      curPor->cursor = VIsual;
      VIsual = old_cursor;
      curPor->setCursWant = true;
   }
}

//}}}
//{{{moving around and scrolling in normal mode

// There are two ways to move the cursor:
// 1. Move the cursor directly, the text is scrolled to keep the cursor in the portal.
// 2. Scroll the text, the cursor is moved into the text visible in the portal.
// The 'scrolloff' option makes this a bit complicated.

// Get the number of screen lines skipped with "po->skipCol".
pub int
adjust_plines_for_skipcol(Portal *po) {
   if (po->skipCol == 0)
      return 0;

   int width = widthLeft(po);
   int w2 = width;
   if (po->skipCol >= width && w2 > 0)
      return (po->skipCol - width) / w2 + 1;

   return 0;
}

// Return how many lines "lnum" will take on the screen, taking into account
// whether it is the first line, whether skipCol is non-zero and limiting to the portal height.
private int
plines_correct_topline(Portal* po, LineNr lnum, int limit_winheight) {
   int n;
   if (lnum == po->topLine)
      n = plines_win_nofill(po, lnum, false) + po->topFill;
   else
      n = plines_win(po, lnum, false);
   if (lnum == po->topLine)
      n -= adjust_plines_for_skipcol(po);
   if (limit_winheight && n > (int)po->height)
      n = po->height;
   return n;
}

// Compute po->bottomLine for the current po->topLine.  Can be called after po->topLine changed.
private void
comp_botline(Portal* po) {
   int      n;
   LineNr   lnum;
   int      done;
   LineNr    last;
   int      folded;

   // If cursorLineRow is valid, start there. Otherwise have to start at topLine.
   check_cursor_moved(po);
   if (po->cacheState & VALID_CROW) {
      lnum = po->cursor.lnum;
      done = po->cursorLineRow;
   } else {
      lnum = po->topLine;
      done = 0;
   }

   for ( ; lnum <= po->book->mem.lineCount; ++lnum) {
      last = lnum;
      folded = false;
      if (getFoldsPortal(po, lnum, NULL, OUT &last, true, NULL)) {
         n = 1;
         folded = true;
      } else {
         n = plines_correct_topline(po, lnum, true);
      }
      if ( lnum <= po->cursor.lnum && last >= po->cursor.lnum) {
         po->cursorLineRow = done;
         po->cursorLineHeight = n;
         po->isCursorLineFolded = folded;
         redraw_for_cursorline(po);
         po->cacheState |= (VALID_CROW|VALID_CHEIGHT);
      }
      if (done + n > (int)po->height)
         break;
      done += n;
      lnum = last;
   }

   // po->bottomLine is the line that is just below the portal
   po->bottomLine = lnum;
   po->cacheState |= VALID_BOTLINE|VALID_BOTLINE_AP;

   normSetEmptyRowCount(po, done);
}

// Redraw when cursorLineRow changes and 'relativenumber' or 'cursorline' is set.
private void
redraw_for_cursorline(Portal* po) {
   if ((po->o.relativeNumber || po->o.cursorLine )
       && (po->cacheState & VALID_CROW) == 0
       && !pum_visible()
   ) {
      // drawLineOnScreen() will redraw the number column and cursorline only.
      redrawPortLater(po, UPD_VALID);
   }
}

// Redraw when virtCol changes and 'cursorcolumn' is set or 'cursorlineopt' contains "screenline".
private void
redraw_for_cursorcolumn(Portal* po) {
   if ((po->cacheState & VALID_VIRTCOL) == 0 && !pum_visible()) {
      // When 'cursorlineopt' contains "screenline", need to redraw with UPD_VALID.
      if (po->o.cursorLine)
          redrawPortLater(po, UPD_VALID);
   }
}

//Calculate how much the @listchars "precedes" or 'smoothscroll' "<<<" marker overlaps with 
//buffer text for portal "po".
//Parameter "extra2" should be the padding on the 2nd line, not the first line. When "extra2" 
//is -1 calculate the padding.
//Return the number of columns of overlap with buffer text, excluding the extra padding on the 
//ledge.
pub int
sms_marker_overlap(Portal* po, int extra2) {
   if (extra2 == -1)
      extra2 = normalPortalColumnOffset(po);
   //There is no marker overlap when in showbreak mode, thus no need to
   //account for it. See wlv_screen_line().
   if (p_sbr)
      return 0;
   // Overlap when 'list' and @listchars "precedes" are set is 1.
   if (po->o.list && listCharsG.prec)
      return 1;

   return extra2 > 3 ? 0 : 3 - extra2;
}

// Calculate the skipcol offset for portal "po" given how many physical lines we want to scroll down
private int
skipcol_from_plines(Portal* po, int plines_off) {
   int width1 = widthLeft(po);
   int skipcol = 0;
   if (plines_off > 0)
      skipcol += width1;
   if (plines_off > 1)
      skipcol += width1 * (plines_off - 1);
   return skipcol;
}

// Set curPor->skipCol to 0 and redraw later if needed.
private void
reset_skipcol(void) {
   if (curPor->skipCol == 0)
      return;

   curPor->skipCol = 0;

   // Should use the least expensive way that displays all that changed.
   // UPD_NOT_VALID is too expensive, UPD_REDRAW_TOP does not redraw
   // enough when the top line gets another screen line.
   redraw_later(UPD_SOME_VALID);
}

// Update curPor->topLine and redraw if necessary. Used to update the screen before printing a 
// message
pub void
update_topline_redraw(void) {
   update_topline();
   if (mustRedrawG)
      drawUpdateScreen(0);
}

// Update curPor->topLine to move the cursor onto the screen.
pub void
update_topline(void) {
   // Cursor is updated instead when this is true for 'splitkeep'.
   if (skipUpdateToplineG)
      return;

   int n;
   Boole check_topline = false;
   Boole check_botline = false;
   long* scrollOff = &curPor->o.scrollOff;
   int save_so = *scrollOff;

   // If there is no valid screen and when the portal height is zero, just use the cursor line.
   if (!screen_valid(true) || curPor->height == 0) {
      check_cursor_lnum();
      curPor->topLine = curPor->cursor.lnum;
      curPor->bottomLine = curPor->topLine;
      curPor->scbindPos = 1;
      return;
   }

   check_cursor_moved(curPor);
   if (curPor->cacheState & VALID_TOPLINE)
      return;

   // When dragging with the mouse, don't scroll that quickly
   if (mouseDraggingG > 0)
      *scrollOff = mouseDraggingG - 1;

   LineNr old_topline = curPor->topLine;
   int old_topfill = curPor->topFill;

   // If the book is empty, always set topline to 1.
   if (CURBOOK_EMPTY()) {     // special case - file is empty
      if (curPor->topLine != 1)
          redraw_later(UPD_NOT_VALID);
      curPor->topLine = 1;
      curPor->bottomLine = 2;
      curPor->skipCol = 0;
      curPor->cacheState |= VALID_BOTLINE|VALID_BOTLINE_AP;
      curPor->scbindPos = 1;
   }
   // If the cursor is above or near the top of the portal, scroll the portal
   // to show the line the cursor is in, with 'scrolloff' context.
   else {
      if (curPor->topLine > 1 || curPor->skipCol > 0) {
         // If the cursor is above topline, scrolling is always needed.
         // If the cursor is far below topline and there is no folding,
         // scrolling down is never needed.
         if (curPor->cursor.lnum < curPor->topLine)
            check_topline = true;
         ei (check_top_offset())
            check_topline = true;
         ei (curPor->skipCol > 0 && curPor->cursor.lnum == curPor->topLine) {
            ColNr vcol;
            int overlap;

            // Check that the cursor position is visible.  Add columns for
            // the marker displayed in the top-left if needed.
            bookGetVirtualColInVirtualMode(curPor, &curPor->cursor, &vcol, NULL, NULL);
            overlap = sms_marker_overlap(curPor, -1);
            if (curPor->skipCol + overlap > vcol)
               check_topline = true;
         }
      }
      // Check if there are more filler lines than allowed.
      if (!check_topline && curPor->topFill > diff_check_fill(curPor, curPor->topLine))
         check_topline = true;

      if (check_topline) {
         int halfheight = curPor->height / 2 - 1;
         if (halfheight < 2)
            halfheight = 2;

         if (hasAnyFolding(curPor)) {
            // Count the number of logical lines between the cursor and
            // topline + scrolloff (approximation of how much will be scrolled).
            n = 0;
            for (LineNr lnum = curPor->cursor.lnum; lnum < curPor->topLine + *scrollOff; ++lnum) {
               ++n;
               // stop at end of file or when we know we are far off
               if (lnum >= curBook->mem.lineCount || n >= halfheight)
                  break;
               (void)getFolds(lnum, NULL, OUT &lnum);
            }
         } else
            n = curPor->topLine + *scrollOff - curPor->cursor.lnum;

         // If we weren't very close to begin with, we scroll to put the cursor in the middle of 
         // the portal. Otherwise put the cursor near the top of the portal.
         if (n >= halfheight)
            scroll_cursor_halfway(false, false);
         else {
            scroll_cursor_top(scrolljump_value(), false);
            check_botline = true;
         }
      } else {
         // Make sure topline is the first line of a fold.
         (void)getFolds(curPor->topLine, &curPor->topLine, NULL);
         check_botline = true;
      }
   }

   // If the cursor is below the bottom of the portal, scroll the portal to put the cursor on the 
   // portal. When bottomLine is invalid, recompute it first, to avoid a redraw later.
   // If bottomLine was approximated, we might need a redraw later in a few cases, but we don't
   // want to spend (a lot of) time recomputing bottomLine for every small change.
   if (check_botline) {
      if (!(curPor->cacheState & VALID_BOTLINE_AP))
         validate_botline();

      if (curPor->bottomLine <= curBook->mem.lineCount) {
         if (curPor->cursor.lnum < curPor->bottomLine) {
            if ((long)curPor->cursor.lnum >= (long)curPor->bottomLine - *scrollOff 
                  || hasAnyFolding(curPor)
            ) {
               // Cursor is (a few lines) above botline, check if there are 'scrolloff' portal 
               // lines below the cursor.  If not, need to scroll.
               n = curPor->emptyRowCount;
               LineOffset   loff;
               loff.lnum = curPor->cursor.lnum;
               // In a fold go to its last line.
               (void)getFolds(loff.lnum, NULL, OUT &loff.lnum);
               loff.fill = 0;
               n += curPor->fillerRowCount;
               loff.height = 0;
               while (loff.lnum < curPor->bottomLine
                  && (loff.lnum + 1 < curPor->bottomLine || loff.fill == 0)
               ){
                  n += loff.height;
                  if (n >= *scrollOff)
                     break;
                  botline_forw(&loff);
               }
               if (n >= *scrollOff)
                  // sufficient context, no need to scroll
                  check_botline = false;
           } else
              // sufficient context, no need to scroll
              check_botline = false;
         }
         if (check_botline) {
            long lineCount; 
            if (hasAnyFolding(curPor)) {
               // Count the number of logical lines between the cursor and botline - scrolloff 
               // (approximation of how much will be scrolled).
               lineCount = 0;
               for (LineNr lnum = curPor->cursor.lnum; 
                    lnum >= curPor->bottomLine - *scrollOff; 
                    --lnum
               ) {
                  ++lineCount;
                  // stop at end of file or when we know we are far off
                  if (lnum <= 0 || lineCount > curPor->height + 1)
                      break;
                  (void)getFolds(lnum, &lnum, NULL);
               }
            } else
               lineCount = curPor->cursor.lnum - curPor->bottomLine + 1 + *scrollOff;
               
            if (lineCount <= curPor->height + 1)
               scroll_cursor_bot(scrolljump_value(), false);
            else
               scroll_cursor_halfway(false, false);
         }
      }
   }
   curPor->cacheState |= VALID_TOPLINE;

   // Need to redraw when topline changed.
   if (curPor->topLine != old_topline || curPor->topFill != old_topfill) {
      redraw_later(UPD_VALID);

      // When 'smoothscroll' is not set, should reset skipCol.
      if (!curPor->o.smoothScroll)
        reset_skipcol();
      ei (curPor->skipCol != 0)
        redraw_later(UPD_SOME_VALID);

      // May need to set skipCol when cursor in topLine.
      if (curPor->cursor.lnum == curPor->topLine)
        validate_cursor();
   }

   *scrollOff = save_so;
}

// Return the scrolljump value to use for the current portal.
// When 'scrolljump' is positive use it as-is.
// When 'scrolljump' is negative use it as a percentage of the portal height.
private int
scrolljump_value(void) {
   if (p_sj >= 0)
      return (int)p_sj;
   return (curPor->height * -p_sj) / 100;
}

// Return true when there are not 'scrolloff' lines above the cursor for the current portal.
private int
check_top_offset(void) {
   LineOffset   loff;
   int      n;
   long   so = curPor->o.scrollOff;

   if (curPor->cursor.lnum < curPor->topLine + so || hasAnyFolding(curPor)) {
      loff.lnum = curPor->cursor.lnum;
      loff.fill = 0;
      n = curPor->topFill;       // always have this context
      // Count the visible screen lines above the cursor line.
      while (n < so) {
          topline_back(&loff);
          // Stop when included a line above the portal.
          if (loff.lnum < curPor->topLine
             || (loff.lnum == curPor->topLine && loff.fill > 0)
             )
         break;
          n += loff.height;
      }
      if (n < so)
          return true;
   }
   return false;
}

// Update cursWant.
private void
update_curswant_force(void) {
   validate_virtcol();
   curPor->cursWant = curPor->virtCol - curPor->virtColFirstChar;
   curPor->setCursWant = false;
}

// Update cursWant if setCursWant is set.
pub void
update_curswant(void) {
   if (curPor->setCursWant)
      update_curswant_force();
}

// Check if the cursor has moved.  Set the cacheState flag accordingly.
pub void
check_cursor_moved(Portal *po) {
   if (po->cursor.lnum != po->lastKnownCursor.lnum) {
      po->cacheState &= ~(
         VALID_WROW|VALID_WCOL|VALID_VIRTCOL|VALID_CHEIGHT|VALID_CROW|VALID_TOPLINE
               |VALID_BOTLINE|VALID_BOTLINE_AP
      );
      po->lastKnownCursor = po->cursor;
      po->lastKnownLeftCol = po->leftCol;
      po->lastKnownSkipCol = po->skipCol;
   } ei (po->skipCol != po->lastKnownSkipCol) {
      po->cacheState &= ~(
         VALID_WROW|VALID_WCOL|VALID_VIRTCOL|VALID_CHEIGHT|VALID_CROW|VALID_BOTLINE|VALID_BOTLINE_AP
      );
      po->lastKnownCursor = po->cursor;
      po->lastKnownLeftCol = po->leftCol;
      po->lastKnownSkipCol = po->skipCol;
   } ei (po->cursor.col != po->lastKnownCursor.col
        || po->leftCol != po->lastKnownLeftCol
        || po->cursor.coladd != po->lastKnownCursor.coladd
   ) {
      po->cacheState &= ~(VALID_WROW|VALID_WCOL|VALID_VIRTCOL);
      po->lastKnownCursor.col = po->cursor.col;
      po->lastKnownLeftCol = po->leftCol;
      po->lastKnownCursor.coladd = po->cursor.coladd;
   }
}

// Call didChangePortalSetting() for every portal containing "buf".
pub void
didChangePortalSettingBuf(Book* book) {
   Tab* t;
   Portal* po;
   FOR_ALL_TAB_PORTALS(t, po) {
      if (po->book == book)
          didChangePortalSetting(po);
   } 
}

// Call didChangePortalSetting() for every portal.
pub void
didChangePortalSettingAll(void) {
   Tab* t;
   Portal* po;
   FOR_ALL_TAB_PORTALS(t, po)
      didChangePortalSetting(po);
}

// Set po->topLine to a certain number.
pub void
set_topline(Portal* po, LineNr lnum) {
   LineNr prev_topline = po->topLine;

   // go to first of folded lines
   (void)getFoldsPortal(po, lnum, OUT &lnum, NULL, true, NULL);
   // Approximate the value of bottomLine
   po->bottomLine += lnum - po->topLine;
   if (po->bottomLine > po->book->mem.lineCount + 1)
      po->bottomLine = po->book->mem.lineCount + 1;
   po->topLine = lnum;
   po->wasTopLineSet = true;
   if (lnum != prev_topline)
      // Keep the filler lines when the topline didn't change.
      po->topFill = 0;
   po->cacheState &= ~(VALID_WROW|VALID_CROW|VALID_BOTLINE|VALID_TOPLINE);
   // Don't set VALID_TOPLINE here, 'scrolloff' needs to be checked.
   redraw_later(UPD_VALID);
}

// Call this function when the length of the cursor line (in screen characters) has changed, and 
// the change is before the cursor. If the line length changed the number of screen lines might 
// change, requiring updating topLine.  That may also invalidate w_crow. Need to take care of 
// bottomLine separately!
pub void
changed_cline_bef_curs(void) {
   curPor->cacheState &= ~(
          VALID_WROW|VALID_WCOL|VALID_VIRTCOL|VALID_CROW |VALID_CHEIGHT|VALID_TOPLINE
   );
}

pub void
changed_cline_bef_curs_win(Portal *po) {
   po->cacheState &= ~(
          VALID_WROW|VALID_WCOL|VALID_VIRTCOL|VALID_CROW |VALID_CHEIGHT|VALID_TOPLINE
   );
}

// Call this function when the length of a line (in screen characters) above the cursor have 
// changed. Need to take care of bottomLine separately!
pub void
changed_line_abv_curs(void) {
    curPor->cacheState &= 
       ~(VALID_WROW|VALID_WCOL|VALID_VIRTCOL|VALID_CROW |VALID_CHEIGHT|VALID_TOPLINE);
}

pub void
changed_line_abv_curs_win(Portal *po) {
    po->cacheState &= ~(VALID_WROW|VALID_WCOL|VALID_VIRTCOL|VALID_CROW|VALID_CHEIGHT|VALID_TOPLINE);
}

// Display of line has changed for "book", invalidate cursor position and bottomLine.
pub void
normInvalidateDisplayOfChangedBookLine(Book* book) {
   Portal *po;
   FOR_ALL_PORTALS(po) {
      if (po->book == book) {
         po->cacheState &= ~(
             VALID_WROW|VALID_WCOL|VALID_VIRTCOL|VALID_CROW|VALID_CHEIGHT|VALID_TOPLINE
             |VALID_BOTLINE|VALID_BOTLINE_AP
         );
      } 
   } 
}

// Make sure the value of curPor->bottomLine is valid.
pub void
validate_botline(void) {
   validate_botline_win(curPor);
}

// Make sure the value of po->bottomLine is valid.
pub void
validate_botline_win(Portal *po) {
   if (!(po->cacheState & VALID_BOTLINE))
      comp_botline(po);
}

// Mark curPor->bottomLine as invalid (because of some change in the book).
pub void
invalidate_botline(void) {
   curPor->cacheState &= ~(VALID_BOTLINE|VALID_BOTLINE_AP);
}

pub void
invalidate_botline_win(Portal *po) {
   po->cacheState &= ~(VALID_BOTLINE|VALID_BOTLINE_AP);
}

pub void
approximate_botline_win( Portal   *po) {
   po->cacheState &= ~VALID_BOTLINE;
}

// true if curPor->cursorRow and curPor->cursorCol are valid.
pub int
cursor_valid(void) {
   check_cursor_moved(curPor);
   return ((curPor->cacheState & (VALID_WROW|VALID_WCOL)) == (VALID_WROW|VALID_WCOL));
}

// Validate cursor position.  Makes sure cursorRow and cursorCol are valid.
// topLine must be valid, you may need to call update_topline() first!
pub void
validate_cursor(void) {
   check_cursor_lnum();
   check_cursor_moved(curPor);
   if ((curPor->cacheState & (VALID_WCOL|VALID_WROW)) != (VALID_WCOL|VALID_WROW))
      curs_columns(true);
}

// Compute po->cursorLineRow and po->cursorLineHeight, based on the current value of po->topLine.
private void
curs_rows(Portal* po) {
   // Check if po->lines[].height is invalid
   int all_invalid = (!redrawing()
        || po->validLines == 0
        || po->lines[0].bookLnum > po->topLine);
   int i = 0;
   po->cursorLineRow = 0;
   for (LineNr lnum = po->topLine; lnum < po->cursor.lnum; ++i) {
      Boole valid = false;
      if (!all_invalid && i < po->validLines) {
         if (po->lines[i].bookLnum < lnum || !po->lines[i].isValid)
            continue;      // skip changed or deleted lines
         if (po->lines[i].bookLnum == lnum) {
            // Check for newly inserted lines below this row, in which
            // case we need to check for folded lines.
            if (!po->book->needsRedraw
               || po->lines[i].lastBookLnum < po->cursor.lnum
               || po->book->needsRedrawTop > po->lines[i].lastBookLnum + 1)
            valid = true;
         } ei (po->lines[i].bookLnum > lnum)
            --i;         // hold at inserted lines
      }
      if (valid && (lnum != po->topLine || (po->skipCol == 0 && !po->o.diff))) {
          lnum = po->lines[i].lastBookLnum + 1;
          // Cursor inside folded lines, don't count this row
          if (lnum > po->cursor.lnum)
         break;
          po->cursorLineRow += po->lines[i].height;
      } else {
         long fold_count = foldedCount(po, lnum, NULL);
         if (fold_count) {
            lnum += fold_count;
            if (lnum > po->cursor.lnum)
               break;
            ++po->cursorLineRow;
         } else {
            po->cursorLineRow += plines_correct_topline(po, lnum, true);
            ++lnum;
         }
      }
   }

   check_cursor_moved(po);
   if (!(po->cacheState & VALID_CHEIGHT)) {
      if (all_invalid
         || i == po->validLines
         || (i < po->validLines
             && (!po->lines[i].isValid
            || po->lines[i].bookLnum != po->cursor.lnum))
      ) {
         if (po->cursor.lnum == po->topLine)
            po->cursorLineHeight = plines_win_nofill(po, po->cursor.lnum, true) + po->topFill;
         else
            po->cursorLineHeight = plines_win(po, po->cursor.lnum, true);
         po->isCursorLineFolded = getFoldsPortal(po, po->cursor.lnum, NULL, NULL, true, NULL);
      } ei (i > po->validLines) {
          // a line that is too long to fit on the last screen line
          po->cursorLineHeight = 0;
          po->isCursorLineFolded = getFoldsPortal(po, po->cursor.lnum, NULL, NULL, true, NULL);
      } else {
          po->cursorLineHeight = po->lines[i].height;
          po->isCursorLineFolded = po->lines[i].isFolded;
      }
   }

   redraw_for_cursorline(curPor);
   po->cacheState |= VALID_CROW|VALID_CHEIGHT;
}

// Validate curPor->virtCol only.
pub void
validate_virtcol(void) {
   validate_virtcol_win(curPor);
}

// Validate po->virtCol only.
pub void
validate_virtcol_win(Portal* po) {
   check_cursor_moved(po);

   if ((po->cacheState & VALID_VIRTCOL) != 0)
      return;

   po->virtColFirstChar = 0;
   bookGetVirtualColInVirtualMode(po, &po->cursor, NULL, &(po->virtCol), NULL);
   redraw_for_cursorcolumn(po);
   po->cacheState |= VALID_VIRTCOL;
}

// Validate curPor->cursorLineHeight only.
pub void
validate_cheight(void) {
   check_cursor_moved(curPor);

   if ((curPor->cacheState & VALID_CHEIGHT) != 0)
      return;

   if (curPor->cursor.lnum == curPor->topLine)
      curPor->cursorLineHeight = plines_nofill(curPor->cursor.lnum) + curPor->topFill;
   else
      curPor->cursorLineHeight = plines(curPor->cursor.lnum);
   curPor->isCursorLineFolded = getFolds(curPor->cursor.lnum, NULL, NULL);
   curPor->cacheState |= VALID_CHEIGHT;
}

// Validate cursorCol and virtCol only.
pub void
validate_cursor_col(void) {
   validate_virtcol();

   if ((curPor->cacheState & VALID_WCOL) != 0)
      return;

   ColNr col = curPor->virtCol;
   ColNr off = normalPortalColumnOffset(curPor);
   col += off;
   int width = curPor->width - off;

   // long line wrapping, adjust curPor->cursorRow
   if (curPor->o.wrap && col >= (ColNr)curPor->width && width > 0)
      // use same formula as what is used in curs_columns()
      col -= ((col - curPor->width) / width + 1) * width;
   if (col > (int)curPor->leftCol)
      col -= curPor->leftCol;
   else
      col = 0;
   curPor->cursorCol = col;

   curPor->cacheState |= VALID_WCOL;
   curPor->flags &= ~WFLAG_WCOL_OFF_ADDED;
}

// Compute offset of a portal, occupied by absolute or relative line number,
// fold column and sign column (these don't move when scrolling horizontally).
pub int
normalPortalColumnOffset(Portal *po) {
    return number_width(po) + 1 + (po != commPortPortG ? 0 : 1) + (isSigncolumnOn(po) ? 2 : 0);
                         // ^ for the line number column
}

// Compute curPor->cursorCol and curPor->virtCol.
// Also update curPor->cursorRow and curPor->cursorLineRow. Also update curPor->leftCol.
pub void
curs_columns(int may_scroll) { // when true, may scroll horizontally
   int diff;
   int off_left, off_right;
   int n;
   int width2 = 0;   // text width for second and later screen line
   ColNr startcol;
   ColNr endcol;
   ColNr prev_skipcol;
   long so = curPor->o.scrollOff;
   long siso = get_sidescrolloff_value();
   int did_sub_skipcol = false;

   // First make sure that topLine is valid (after moving the cursor).
   update_topline();

   // Next make sure that cursorLineRow is valid.
   if (!(curPor->cacheState & VALID_CROW))
      curs_rows(curPor);

   // will be set by bufGetVirtualColInVirtualMode() but not reset
   curPor->virtColFirstChar = 0;

   // Compute the number of virtual columns.
   if (curPor->isCursorLineFolded)
      // In a folded line the cursor is always in the first column
      startcol = curPor->virtCol = endcol = curPor->leftCol;
   else
      bookGetVirtualColInVirtualMode(curPor, &curPor->cursor, &startcol, &(curPor->virtCol), &endcol);

   int extra = normalPortalColumnOffset(curPor);  // offset for first screen line
   curPor->cursorCol = curPor->virtCol + extra;
   endcol += extra;

   // Now compute cursorRow, counting screen lines from cursorLineRow.
   curPor->cursorRow = curPor->cursorLineRow;
   // text width for first screen line
   int width1 = curPor->width - extra;
   if (width1 <= 0) {
      // No room for text, put cursor in last char of portal.
      // If not wrapping, the last non-empty line.
      curPor->cursorCol = curPor->width - 1;
      if (curPor->o.wrap)
         curPor->cursorRow = curPor->height - 1;
      else
         curPor->cursorRow = curPor->height - 1 - curPor->emptyRowCount;
   } ei (curPor->o.wrap && curPor->width != 0) {

      // skip columns that are not visible
      if (curPor->cursor.lnum == curPor->topLine
         && curPor->skipCol > 0
         && curPor->cursorCol >= curPor->skipCol)
      {
         // Deduct by multiples of width2.  This allows the long line
         // wrapping formula below to correctly calculate the cursorCol value
         // when wrapping.
         if (curPor->skipCol <= width1)
            curPor->cursorCol -= width2;
         else
            curPor->cursorCol -= width2 * (((curPor->skipCol - width1) / width2) + 1);

         did_sub_skipcol = true;
      }

      // long line wrapping, adjust curPor->cursorRow
      if (curPor->cursorCol >= (int)curPor->width) {
         // this same formula is used in validate_cursor_col()
         n = (curPor->cursorCol - curPor->width) / width2 + 1;
         curPor->cursorCol -= n * width2;
         curPor->cursorRow += n;
      }
   }

   // No line wrapping: compute curPor->leftCol if scrolling is on and line is not folded.
   // If scrolling is off, curPor->leftCol is assumed to be 0
   ei (may_scroll && !curPor->isCursorLineFolded) {
      if (curPor->virtColFirstChar > 0) {
         int cols = (curPor->width - extra);
         int rows = cols > 0 ? curPor->virtColFirstChar / cols : 1;

         // each "above" text prop shifts the text one row down
         curPor->cursorRow += rows;
         curPor->cursorCol -= rows * cols;
         endcol -= rows * cols;
         curPor->cursorLineHeight = rows + 1;
      }
      //If Cursor is left of the screen, scroll rightwards.
      //If Cursor is right of the screen, scroll leftwards
      //If we get closer to the edge than 'sidescrolloff', scroll a little extra
      off_left = (int)startcol - (int)curPor->leftCol - siso;
      off_right = (int)endcol - (int)(curPor->leftCol + curPor->width
                           - siso) + 1;
      if (off_left < 0 || off_right > 0) {
         if (off_left < 0)
            diff = -off_left;
         else
            diff = off_right;

         // When far off or not enough room on either side, put cursor in middle of portal.
         
         int neleftCol;
         if (p_ss == 0 || diff >= width1 / 2 || off_right >= off_left)
            neleftCol = curPor->cursorCol - extra - width1 / 2;
         else {
            if (diff < p_ss)
               diff = p_ss;
            neleftCol = (off_left < 0) ? curPor->leftCol - diff : curPor->leftCol + diff;
         }
         if (neleftCol < 0)
            neleftCol = 0;
         if (neleftCol != (int)curPor->leftCol) {
            curPor->leftCol = neleftCol;
            // screen has to be redrawn with new curPor->leftCol
            redraw_later(UPD_NOT_VALID);
         }
      }
      curPor->cursorCol -= curPor->leftCol;
   } ei (curPor->cursorCol > (int)curPor->leftCol)
      curPor->cursorCol -= curPor->leftCol;
   else
      curPor->cursorCol = 0;

   // Skip over filler lines.  At the top use topFill, there
   // may be some filler lines above the portal.
   if (curPor->cursor.lnum == curPor->topLine)
      curPor->cursorRow += curPor->topFill;
   else
      curPor->cursorRow += diff_check_fill(curPor, curPor->cursor.lnum);

   prev_skipcol = curPor->skipCol;

   int pLines = 0;

   if ((curPor->cursorRow >= (int)curPor->height
      || ((prev_skipcol > 0 || curPor->cursorRow + so >= curPor->height)
          && (pLines = plines_win_nofill(curPor, curPor->cursor.lnum, false)) - 1 
             >= (int)curPor->height
         )
       )
       && curPor->height != 0
       && curPor->cursor.lnum == curPor->topLine
       && width2 > 0
       && curPor->width != 0
   ) {
      // Cursor past end of screen.  Happens with a single line that does
      // not fit on screen.  Find a skipcol to show the text around the
      // cursor.  Avoid scrolling all the time. compute value of "extra":
      // 1: Less than 'scrolloff' lines above
      // 2: Less than 'scrolloff' lines below
      // 3: both of them
      extra = 0;
      if (curPor->skipCol + so * width2 > curPor->virtCol)
         extra = 1;
      // Compute last display line of the book line that we want at the bottom of the portal.
      if (pLines == 0)
         pLines = plines_win(curPor, curPor->cursor.lnum, false);
      --pLines;
      if (pLines > curPor->cursorRow + so)
         n = curPor->cursorRow + so;
      else
         n = pLines;
      if ((ColNr)n >= curPor->height + curPor->skipCol / width2 - so)
         extra += 2;

      if (extra == 3 || curPor->height <= so * 2) {
         // not enough room for 'scrolloff', put cursor in the middle
         n = curPor->virtCol / width2;
         if (n > (int)curPor->height / 2)
            n -= curPor->height / 2;
         else
            n = 0;
         // don't skip more than necessary
         if (n > pLines - (int)curPor->height + 1)
            n = pLines - (int)curPor->height + 1;
         if (n > 0)
            curPor->skipCol = width1 + (n - 1) * width2;
         else
            curPor->skipCol = 0;
      } ei (extra == 1) {
          // less than 'scrolloff' lines above, decrease skipcol
          extra = (curPor->skipCol + so * width2 - curPor->virtCol + width2 - 1) / width2;
          if (extra > 0) {
            if ((ColNr)(extra * width2) > curPor->skipCol)
                extra = curPor->skipCol / width2;
            curPor->skipCol -= extra * width2;
          }
      } ei (extra == 2) {
         // less than 'scrolloff' lines below, increase skipcol
         endcol = (n - curPor->height + 1) * width2;
         while (endcol > curPor->virtCol)
            endcol -= width2;
         if (endcol > curPor->skipCol)
            curPor->skipCol = endcol;
      }

      // adjust cursorRow for the changed skipCol
      if (did_sub_skipcol)
         curPor->cursorRow -= (curPor->skipCol - prev_skipcol) / width2;
      else
         curPor->cursorRow -= curPor->skipCol / width2;

      if (curPor->cursorRow >= (int)curPor->height) {
         // small portal, make sure cursor is in it
         extra = curPor->cursorRow - curPor->height + 1;
         curPor->skipCol += extra * width2;
         curPor->cursorRow -= extra;
      }

      extra = ((int)prev_skipcol - (int)curPor->skipCol) / width2;
      if (extra > 0)
         insertLinesIntoPortal(curPor, 0, extra, false, false);
      ei (extra < 0)
         deleteLinesFromPortal(curPor, 0, -extra, false, false, 0);
   } ei (!curPor->o.smoothScroll)
      curPor->skipCol = 0;
   if (prev_skipcol != curPor->skipCol)
      redraw_later(UPD_SOME_VALID);

   redraw_for_cursorcolumn(curPor);
   if (portalIsPopup(curPor) && curBook->term != NULL) {
      curPor->cursorRow += popup_top_extra(curPor);
      curPor->cursorCol += popup_left_extra(curPor);
      curPor->flags |= WFLAG_WCOL_OFF_ADDED + WFLAG_WROW_OFF_ADDED;
   } else
      curPor->flags &= ~(WFLAG_WCOL_OFF_ADDED + WFLAG_WROW_OFF_ADDED);

   // now leftCol and skipCol are valid, avoid check_cursor_moved()
   // thinking otherwise
   curPor->lastKnownLeftCol = curPor->leftCol;
   curPor->lastKnownSkipCol = curPor->skipCol;

   curPor->cacheState |= VALID_WCOL|VALID_WROW|VALID_VIRTCOL;
}

// Compute the screen position of text character at "pos" in portal "po"
// The resulting values are one-based, 0 when character is not visible.
pub void
textpos2screenpos(
   Portal* po,
   Pos* pos,
   int* rowp,   // screen row
   int* scolp,   // start screen column
   int* ccolp,   // cursor screen column
   int* ecolp   // end screen column
){
   ColNr   scol = 0, ccol = 0, ecol = 0;
   int      row = 0;
   ColNr   coloff = 0;

   if (pos->lnum >= po->topLine && pos->lnum <= po->bottomLine) {
      ColNr col;
      LineNr lnum = pos->lnum;
      Boole is_folded = getFoldsPortal(po, lnum, OUT &lnum, NULL, true, NULL);
      row = plines_m_win(po, po->topLine, lnum - 1, INT_MAX);
      // "row" should be the screen line where line "lnum" begins, which can
      // be negative if "lnum" is "topLine" and "skipCol" is non-zero.
      row -= adjust_plines_for_skipcol(po);

      // Add filler lines above this book line.
      row += lnum == po->topLine ? po->topFill : diff_check_fill(po, lnum);

      ColNr off = normalPortalColumnOffset(po);
      if (is_folded) {
         row += po->windowRow + 1;
         coloff = po->windowCol + 1 + off;
      } else {
         getvcol(po, pos, &scol, &ccol, &ecol);

         // similar to what is done in validate_cursor_col()
         col = scol;
         col += off;
         int width = po->width - off;

         // long line wrapping, adjust row
         if (po->o.wrap && col >= (ColNr)po->width && width > 0) {
            // use same formula as what is used in curs_columns()
            int rowoff = ((col - po->width) / width + 1);
            col -= rowoff * width;
            row += rowoff;
         }
         col -= po->leftCol;
         if (col >= (int)po->width)
            col = -1;
         if (col >= 0 && row >= 0 && row < (int)po->height) {
            coloff = col - scol + po->windowCol + 1;
            row += po->windowRow + 1;
         } else
            // character is out of the portal
            row = scol = ccol = ecol = 0;
      }
   }
   *rowp = row;
   *scolp = scol + coloff;
   *ccolp = ccol + coloff;
   *ecolp = ecol + coloff;
}

// "screenpos({winid}, {lnum}, {col})" function
pub void
f_screenpos(Var* argvars, Var* returnVar) {
   Pos   pos;
   int      row = 0;
   int      scol = 0, ccol = 0, ecol = 0;

   allocReturnDict(returnVar);
   Bag* bag = returnVar->bag;

   Portal* po = portFindByNrOrId(argvars);
   if (!po)
      return;

   pos.lnum = tv_get_number(&argvars[1]);
   if (pos.lnum > po->book->mem.lineCount) {
      showErrFmtMsg(_(e_invalid_line_number_nr), pos.lnum);
      return;
   }
   pos.col = tv_get_number(&argvars[2]) - 1;
   if (pos.col < 0)
      pos.col = 0;
   pos.coladd = 0;
   textpos2screenpos(po, &pos, &row, &scol, &ccol, &ecol);

   bagAddNumber(bag, S"row", row);
   bagAddNumber(bag, S"col", scol);
   bagAddNumber(bag, S"curscol", ccol);
   bagAddNumber(bag, S"endcol", ecol);
}

// Convert a virtual (screen) column to a character column.  The first column
// is one.  For a multibyte character, the column number of the first byte is returned.
private int
virtcol2col(Portal* po, LineNr lnum, int vcol) {
   int offset = vcol2col(po, lnum, vcol - 1, NULL);
   CS line = memGetLine(po->book, lnum, false);
   CS p = line + offset;
   if (*p == ZERO) {
      if (p == line)
         return 0;
      // Move to the first byte of the last char.
      MB_PTR_BACK(line, p);
   }
   return (int)(offset + 1);
}

// "virtcol2col({winid}, {lnum}, {col})" function
pub void
f_virtcol2col(Var* argvars, Var* returnVar) {
   returnVar->number = -1;

   if (check_for_number_arg(argvars, 0) == FAIL
          || check_for_number_arg(argvars, 1) == FAIL
          || check_for_number_arg(argvars, 2) == FAIL)
      return;

   Portal* po = portFindByNrOrId(argvars);
   if (!po)
      return;

   Boole error = false;
   LineNr lnum = varGetNumberChk(argvars + 1, OUT &error);
   if (error || lnum < 0 || lnum > po->book->mem.lineCount)
      return;

   int screencol = varGetNumberChk(argvars + 2, &error);
   if (error || screencol < 0)
      return;

   returnVar->number = virtcol2col(po, lnum, screencol);
}

// Make sure the cursor is in the visible part of the topline after scrolling
// the screen with 'smoothscroll'.
private void cursor_correct_sms(void) {
   if (!curPor->o.smoothScroll || !curPor->o.wrap
         || curPor->cursor.lnum != curPor->topLine)
      return;

   long scrollOff = curPor->o.scrollOff;
   int width1 = widthLeft(curPor);
   int so_cols = scrollOff == 0 ? 0 : (scrollOff * width1);
   int space_cols = (curPor->height - 1) * width1;
   int overlap, top, bot;
   int size = scrollOff == 0 ? 0 : linetabsize_eol(curPor, curPor->topLine);

   if (curPor->topLine == 1 && curPor->skipCol == 0)
      so_cols = 0;               // Ignore 'scrolloff' at top of book.
   ei (so_cols > space_cols / 2)
      so_cols = space_cols / 2;  // Not enough room: put cursor in the middle.

   // Not enough screen lines in topline: ignore 'scrolloff'.
   while (so_cols > size && so_cols - width1 >= width1 && width1 > 0)
      so_cols -= width1;
   if (so_cols >= width1 && so_cols > size)
      so_cols -= width1;

   overlap = curPor->skipCol == 0 ? 0 : sms_marker_overlap(curPor, curPor->width - width1);
   // If we have non-zero scrolloff, ignore marker overlap.
   top = curPor->skipCol + (so_cols != 0 ? so_cols : overlap);
   bot = curPor->skipCol + width1 + (curPor->height - 1) * width1 - so_cols;
   validate_virtcol();
   ColNr col = curPor->virtCol;

   if (col < top) {
      if (col < width1)
         col += width1;
      while (width1 > 0 && col < top) {
         col += width1;
      }
   } else {
      while (width1 > 0 && col >= bot) {
         col -= width1;
      }
   }

   if (col != curPor->virtCol) {
      int rc;

      curPor->cursWant = col;
      rc = coladvance(curPor->cursWant);
      // validate_virtcol() marked various things as valid, but after
      // moving the cursor they need to be recomputed
      curPor->cacheState &=
          ~(VALID_WROW|VALID_WCOL|VALID_CHEIGHT|VALID_CROW|VALID_VIRTCOL);
      if (rc == FAIL && curPor->skipCol > 0
         && curPor->cursor.lnum < curBook->mem.lineCount)
      {
         validate_virtcol();
         if (curPor->virtCol < curPor->skipCol + overlap) {
            // Cursor still not visible: move it to the next line instead.
            curPor->cursor.lnum++;
            curPor->cursor.col = 0;
            curPor->cursor.coladd = 0;
            curPor->cursWant = 0;
            curPor->cacheState &= ~VALID_VIRTCOL;
         }
      }
   }
}

// Scroll "count" lines up or down, and redraw.
pub void
scroll_redraw(int up, long count) {
   LineNr prev_topline = curPor->topLine;
   int prev_skipcol = curPor->skipCol;
   int prev_topfill = curPor->topFill;
   LineNr prev_lnum = curPor->cursor.lnum;
   if (up) {
      scrollup(count, true);
   } else {
      scrolldown(count, true);
   }
   if (curPor->o.scrollOff > 0) {
      // Adjust the cursor position for 'scrolloff'.  Mark topLine as
      // valid, otherwise the screen jumps back at the end of the file.
      cursor_correct();
      check_cursor_moved(curPor);
      curPor->cacheState |= VALID_TOPLINE;

      //If moved back to where we were, at least move the cursor, otherwise we get stuck at one 
      //position. Don't move the cursor up if the first line of the book is already on the screen
      while (curPor->topLine == prev_topline
         && curPor->skipCol == prev_skipcol
         && curPor->topFill == prev_topfill
      ) {
         if (up) {
            if (curPor->cursor.lnum > prev_lnum || cursor_down(1L, false) == FAIL)
               break;
         } else {
            if (curPor->cursor.lnum < prev_lnum 
                  || prev_topline == 1L 
                  || cursor_up(1L, false) == FAIL
            )
               break;
         }
         //Mark topLine as valid, otherwise the screen jumps back at the end of the file.
         check_cursor_moved(curPor);
         curPor->cacheState |= VALID_TOPLINE;
      }
   }

   cursor_correct_sms();
   if (curPor->cursor.lnum != prev_lnum)
      coladvance(curPor->cursWant);
   redraw_later(UPD_VALID);
}

// Scroll the current portal down by "line_count" logical lines.  "CTRL-Y"
pub void
scrolldown(long line_count, int byfold) {  // true: count a closed fold as one line
   long done = 0;   // total # of physical lines done
   int wrow;
   int moved = false;
   int doSmoothly = curPor->o.wrap && curPor->o.smoothScroll;
   int width1 = 0;

   if (doSmoothly) {
      width1 = widthLeft(curPor);
   }

   LineNr   first;

   // Make sure topLine is at the first of a sequence of folded lines.
   (void)getFolds(curPor->topLine, &curPor->topLine, NULL);
   validate_cursor();      // cursorRow needs to be valid
   for (int todo = line_count; todo > 0; --todo) {
      if (curPor->topFill < diff_check_fill(curPor, curPor->topLine)
         && curPor->topFill < (int)curPor->height - 1
      ) {
          ++curPor->topFill;
          ++done;
      } else {
         // break when at the very top
         if (curPor->topLine == 1 && (!doSmoothly || curPor->skipCol < width1))
            break;
         if (doSmoothly && curPor->skipCol >= width1) {
            // scroll a screen line down
            if (curPor->skipCol >= 2*width1)
                curPor->skipCol -= width1;
            else
                curPor->skipCol -= width1;
            redraw_later(UPD_NOT_VALID);
            ++done;
         } else {
            // scroll a text line down
            --curPor->topLine;
            curPor->skipCol = 0;
            curPor->topFill = 0;
            // A sequence of folded lines only counts for one logical line
            if (getFolds(curPor->topLine, &first, NULL)) {
               ++done;
               if (!byfold)
                  todo -= curPor->topLine - first - 1;
               curPor->bottomLine -= curPor->topLine - first;
               curPor->topLine = first;
            } ei (doSmoothly) {
               int size = linetabsize_eol(curPor, curPor->topLine);
               if (size > width1) {
                  curPor->skipCol = width1;
                  size -= width1;
                  redraw_later(UPD_NOT_VALID);
               }
               while (size > width1) {
                  curPor->skipCol += width1;
                  size -= width1;
               }
               ++done;
            } else
               done += PLINES_NOFILL(curPor->topLine);
         }
      }
      --curPor->bottomLine;      // approximate bottomLine
      invalidate_botline();
   }
   curPor->cursorRow += done;      // keep cursorRow updated
   curPor->cursorLineRow += done;   // keep cursorLineRow updated

   if (curPor->cursor.lnum == curPor->topLine)
      curPor->cursorLineRow = 0;
   check_topfill(curPor, true);

   //Compute the row number of the last row of the cursor line
   //and move the cursor onto the displayed part of the portal.
   wrow = curPor->cursorRow;
   if (curPor->o.wrap && curPor->width != 0) {
      validate_virtcol();
      validate_cheight();
      wrow += curPor->cursorLineHeight - 1 - curPor->virtCol / curPor->width;
   }
    
   while (wrow >= (int)curPor->height && curPor->cursor.lnum > 1) {
      if (getFolds(curPor->cursor.lnum, &first, NULL)) {
         --wrow;
         if (first == 1)
            curPor->cursor.lnum = 1;
         else
            curPor->cursor.lnum = first - 1;
      } else
         wrow -= plines(curPor->cursor.lnum--);
      curPor->cacheState &= ~(VALID_WROW|VALID_WCOL|VALID_CHEIGHT|VALID_CROW|VALID_VIRTCOL);
      moved = true;
   }
    
   if (moved) {
      // Move cursor to first line of closed fold.
      foldAdjustCursor();
      coladvance(curPor->cursWant);
   }
   if (curPor->cursor.lnum < curPor->topLine)
      curPor->cursor.lnum = curPor->topLine;
}

// Scroll the current portal up by "line_count" logical lines.  "CTRL-E"
pub void
scrollup(long line_count, int byfold) {  // true: count a closed fold as one line
   int      doSmoothly = curPor->o.wrap && curPor->o.smoothScroll;

   if (doSmoothly
       || (byfold && hasAnyFolding(curPor))
       || (curPor->o.diff && !curPor->o.wrap)
   ) {
      int width1 = widthLeft(curPor);
      int size = 0;
      ColNr prev_skipcol = curPor->skipCol;

      if (doSmoothly)
          size = linetabsize_eol(curPor, curPor->topLine);

      // diff mode: first consume "topfill"
      // 'smoothscroll': increase "skipCol" until it goes over the end of
      // the line, then advance to the next line.
      // folding: count each sequence of folded lines as one logical line.
      for (int todo = line_count; todo > 0; --todo) {
         if (curPor->topFill > 0)
            --curPor->topFill;
         else {
            LineNr lnum = curPor->topLine;

            if (byfold)
               // for a closed fold: go to the last line in the fold
               (void)getFolds(lnum, NULL, &lnum);
            if (lnum == curPor->topLine && doSmoothly) {
               // 'smoothscroll': increase "skipCol" until it goes over
               // the end of the line, then advance to the next line.
               curPor->skipCol += width1;
               if (curPor->skipCol >= size) {
                  if (lnum == curBook->mem.lineCount) {
                     // at the last screen line, can't scroll further
                     curPor->skipCol -= width1;
                     break;
                  }
                  ++lnum;
               }
            } else {
                if (lnum >= curBook->mem.lineCount)
               break;
                ++lnum;
            }

            if (lnum > curPor->topLine) {
                // approximate bottomLine
                curPor->bottomLine += lnum - curPor->topLine;
                curPor->topLine = lnum;
                curPor->topFill = diff_check_fill(curPor, lnum);
                curPor->skipCol = 0;
                if (todo > 1 && doSmoothly)
               size = linetabsize_eol(curPor, curPor->topLine);
            }
         }
      }

      if (prev_skipcol > 0 || curPor->skipCol > 0)
          // need to redraw more, because height of the (new) topline may
          // now be invalid
          redraw_later(UPD_NOT_VALID);
   } else {
      curPor->topLine += line_count;
      curPor->bottomLine += line_count;   // approximate bottomLine
   }

   if (curPor->topLine > curBook->mem.lineCount)
      curPor->topLine = curBook->mem.lineCount;
   if (curPor->bottomLine > curBook->mem.lineCount + 1)
      curPor->bottomLine = curBook->mem.lineCount + 1;

   check_topfill(curPor, false);

   if (hasAnyFolding(curPor))
      // Make sure topLine is at the first of a sequence of folded lines.
      (void)getFolds(curPor->topLine, &curPor->topLine, NULL);

   curPor->cacheState &= ~(VALID_WROW|VALID_CROW|VALID_BOTLINE);
   if (curPor->cursor.lnum < curPor->topLine) {
      curPor->cursor.lnum = curPor->topLine;
      curPor->cacheState &=
            ~(VALID_WROW|VALID_WCOL|VALID_CHEIGHT|VALID_CROW|VALID_VIRTCOL);
      coladvance(curPor->cursWant);
   }
}

//After changing the cursor column: make sure that curPor->skipCol is valid for 'smoothscroll'
pub void
adjust_skipcol(void) {
   if (!curPor->o.wrap
         || !curPor->o.smoothScroll
         || curPor->cursor.lnum != curPor->topLine
   )
      return;

   int width1 = widthLeft(curPor);
   if (width1 <= 0)
      return;  // no text will be displayed

   long so = curPor->o.scrollOff;
   int scrolloff_cols = so == 0 ? 0 : (so * width1);
   int scrolled = false;
   int row = 0;
   int overlap, col;

   validate_cheight();
   if (curPor->cursorLineHeight == curPor->height
       // cursorLineHeight may be capped at w_height, check there aren't
       // actually more lines.
       && plines_win(curPor, curPor->cursor.lnum, false) <= (int)curPor->height
   ) {
      // the line just fits in the portal, don't scroll
      reset_skipcol();
      return;
   }

   validate_virtcol();
   overlap = sms_marker_overlap(curPor, curPor->width - width1);
   while (curPor->skipCol > 0 && curPor->virtCol < curPor->skipCol + overlap + scrolloff_cols) {
      // scroll a screen line down
      if (curPor->skipCol >= width1 + width1)
         curPor->skipCol -= width1;
      else
         curPor->skipCol -= width1;
      scrolled = true;
   }
   if (scrolled) {
      validate_virtcol();
      redraw_later(UPD_NOT_VALID);
      return;  // don't scroll in the other direction now
   }

   col = curPor->virtCol + scrolloff_cols;

   // Avoid adjusting for 'scrolloff' beyond the text line height.
   if (scrolloff_cols > 0) {
      int size = linetabsize_eol(curPor, curPor->topLine);
      size = width1 + width1 * ((size - 1) / width1);
      while (col > size)
         col -= width1;
   }
   col -= curPor->skipCol;

   if (col >= width1) {
      col -= width1;
      ++row;
   }
   if (col > width1) {
      row += col / width1;
      // col may no longer be used, but make
      // sure it is correct anyhow, just in case
      col = col % width1;
   }
   if (row >= (int)curPor->height) {
      if (curPor->skipCol == 0) {
         curPor->skipCol += width1;
         --row;
      }
      if (row >= (int)curPor->height)
         curPor->skipCol += (row - curPor->height) * width1;
      redraw_later(UPD_NOT_VALID);
   }
}

// Don't end up with too many filler lines in the portal.
pub void
check_topfill(Portal* po, int down) {  // when true scroll down when not enough space
   if (po->topFill <= 0)
      return;

   int n = plines_win_nofill(po, po->topLine, true);
   if (po->topFill + n > (int)po->height) {
      if (down && po->topLine > 1) {
          --po->topLine;
          po->topFill = 0;
      } else {
          po->topFill = po->height - n;
          if (po->topFill < 0)
         po->topFill = 0;
      }
   }
}

// Scroll the screen one line down, but don't do it if it would move the cursor off the screen.
pub void
scrolldown_clamp(void) {
   int end_row;
   int can_fill = (curPor->topFill < diff_check_fill(curPor, curPor->topLine));

   if (curPor->topLine <= 1 && !can_fill)
      return;

   validate_cursor();       // cursorRow needs to be valid

   //Compute the row number of the last row of the cursor line
   //and make sure it doesn't go off the screen. Make sure the cursor
   //doesn't go past 'scrolloff' lines from the screen end.
   end_row = curPor->cursorRow;
   if (can_fill)
      ++end_row;
   else
      end_row += plines_nofill(curPor->topLine - 1);
   if (curPor->o.wrap && curPor->width != 0) {
      validate_cheight();
      validate_virtcol();
      end_row += curPor->cursorLineHeight - 1 - curPor->virtCol / curPor->width;
   }
   if (end_row < curPor->height - curPor->o.scrollOff) {
      if (can_fill) {
         ++curPor->topFill;
         check_topfill(curPor, true);
      } else {
         --curPor->topLine;
         curPor->topFill = 0;
      }
      (void)getFolds(curPor->topLine, &curPor->topLine, NULL);
      --curPor->bottomLine;       // approximate bottomLine
      curPor->cacheState &= ~(VALID_WROW|VALID_CROW|VALID_BOTLINE);
   }
}

// Scroll the screen one line up, but don't do it if it would move the cursor off the screen.
pub void
scrollup_clamp(void) {
   if (curPor->topLine == curBook->mem.lineCount && curPor->topFill == 0)
      return;

   validate_cursor();       // cursorRow needs to be valid

   //Compute the row number of the first row of the cursor line
   //and make sure it doesn't go off the screen. Make sure the cursor
   //doesn't go before 'scrolloff' lines from the screen start.
   int start_row = curPor->cursorRow - plines_nofill(curPor->topLine) - curPor->topFill;
   if (curPor->o.wrap && curPor->width != 0) {
      validate_virtcol();
      start_row -= curPor->virtCol / curPor->width;
   }
   if (start_row >= curPor->o.scrollOff) {
      if (curPor->topFill > 0)
         --curPor->topFill;
      else {
         (void)getFolds(curPor->topLine, NULL, &curPor->topLine);
         ++curPor->topLine;
      }
      ++curPor->bottomLine;      // approximate bottomLine
      curPor->cacheState &= ~(VALID_WROW|VALID_CROW|VALID_BOTLINE);
   }
}

// Add one line above "lp->lnum".  This can be a filler line, a closed fold or
// a (wrapped) text line.  Uses and sets "lp->fill". Return the height of the added line in 
// "lp->height". Lines above the first one are incredibly high: MAXCOL.
private void
topline_back_winheight(LineOffset* lp, int winheight) {  //when true, limit to portal height
   if (lp->fill < diff_check_fill(curPor, lp->lnum)) {
      // Add a filler line.
      ++lp->fill;
      lp->height = 1;
   } else {
      --lp->lnum;
      lp->fill = 0;
      if (lp->lnum < 1) {
          lp->height = MAXCOL;
      } ei (getFolds(lp->lnum, &lp->lnum, NULL)) {
          // Add a closed fold
          lp->height = 1;
      } else {
          lp->height = PLINES_WIN_NOFILL(curPor, lp->lnum, winheight);
      }
   }
}

private void
topline_back(LineOffset *lp) {
   topline_back_winheight(lp, true);
}


//Add one line below "lp->lnum".  This can be a filler line, a closed fold or a (wrapped) text line.
//Uses and sets "lp->fill". Return the height of the added line in "lp->height". Lines below the 
//last one are incredibly high.
private void
botline_forw(LineOffset* lp) {
   if (lp->fill < diff_check_fill(curPor, lp->lnum + 1)) {
      // Add a filler line.
      ++lp->fill;
      lp->height = 1;
   } else {
      ++lp->lnum;
      lp->fill = 0;
      if (lp->lnum > curBook->mem.lineCount)
         lp->height = MAXCOL;
      else
         if (getFolds(lp->lnum, NULL, &lp->lnum))
            // Add a closed fold
            lp->height = 1;
      else
         lp->height = PLINES_NOFILL(lp->lnum);
   }
}

// Recompute topline to put the cursor at the top of the portal. Scroll at least "min_scroll" lines.
// If "always" is true, always set topline (for "zt").
private void
scroll_cursor_top(int min_scroll, int always) {
   int      scrolled = 0;
   int      extra = 0;
   int      i;
   LineNr   old_topline = curPor->topLine;
   int      old_skipcol = curPor->skipCol;
   LineNr   old_topfill = curPor->topFill;
   int      off = curPor->o.scrollOff;

   if (mouseDraggingG > 0)
      off = mouseDraggingG - 1;

   //Decrease topline until:
   //- it has become 1
   //- (part of) the cursor line is moved off the screen or
   //- moved at least 'scrolljump' lines and
   //- at least 'scrolloff' lines above and below the cursor
   validate_cheight();
   int used = curPor->cursorLineHeight; // includes filler lines above
   if (curPor->cursor.lnum < curPor->topLine)
      scrolled = used;

   LineNr   top;      //just above displayed lines
   LineNr   bot;      //just below displayed lines
   if (getFolds(curPor->cursor.lnum, OUT &top, OUT &bot)) {
      --top;
      ++bot;
   } else {
      top = curPor->cursor.lnum - 1;
      bot = curPor->cursor.lnum + 1;
   }
   LineNr netopLine = top + 1;

   // "used" already contains the number of filler lines above, don't add it again.
   // Hide filler lines above cursor line by adding them to "extra".
   extra += diff_check_fill(curPor, curPor->cursor.lnum);

   //Check if the lines from "top" to "bot" fit in the portal. If they do,
   //set netopLine and advance "top" and "bot" to include more lines.
   while (top > 0) {
      if (getFolds(top, &top, NULL))
         // count one logical line for a sequence of folded lines
         i = 1;
      else
         i = PLINES_NOFILL(top);
      if (top < curPor->topLine)
         scrolled += i;

      // If scrolling is needed, scroll at least 'sj' lines.
      if ((netopLine >= curPor->topLine || scrolled > min_scroll) && extra >= off)
         break;

      used += i;
      if (extra + i <= off && bot < curBook->mem.lineCount) {
          if (getFolds(bot, NULL, &bot))
         // count one logical line for a sequence of folded lines
         ++used;
          else
         used += plines(bot);
      }
      if (used > (int)curPor->height)
          break;

      extra += i;
      netopLine = top;
      --top;
      ++bot;
   }

   //If we don't have enough space, put cursor in the middle.
   //This makes sure we get the same position when using "k" and "j" in a small portal.
   if (used > (int)curPor->height)
      scroll_cursor_halfway(false, false);
   else {
      // If "always" is false, only adjust topline to a lower value, higher
      // value may happen with wrapping lines.
      if (netopLine < curPor->topLine || always)
         curPor->topLine = netopLine;
      if (curPor->topLine > curPor->cursor.lnum)
         curPor->topLine = curPor->cursor.lnum;
      curPor->topFill = diff_check_fill(curPor, curPor->topLine);
      if (curPor->topFill > 0 && extra > off) {
         curPor->topFill -= extra - off;
         if (curPor->topFill < 0)
            curPor->topFill = 0;
      }
      check_topfill(curPor, false);
      if (curPor->topLine != old_topline)
         reset_skipcol();
      ei (curPor->topLine == curPor->cursor.lnum) {
         validate_virtcol();
         if (curPor->skipCol >= curPor->virtCol)
            // TODO: if the line doesn't fit may optimize skipCol instead
            // of making it zero
            reset_skipcol();
      }
      if (curPor->topLine != old_topline
         || curPor->skipCol != old_skipcol
         || curPor->topFill != old_topfill
      ) {
         curPor->cacheState &= ~(VALID_WROW|VALID_CROW|VALID_BOTLINE|VALID_BOTLINE_AP);
      } 
      curPor->cacheState |= VALID_TOPLINE;
   }
}

// Set emptyRowCount and fillerRowCount for portal "po", having used up "used"
// screen lines for text lines.
pub void
normSetEmptyRowCount(Portal* po, int used) {
   po->fillerRowCount = 0;
   if (used == 0)
      po->emptyRowCount = 0;   // single line that doesn't fit
   else {
      po->emptyRowCount = po->height - used;
      if (po->bottomLine <= po->book->mem.lineCount) {
         po->fillerRowCount = diff_check_fill(po, po->bottomLine);
         if (po->emptyRowCount > po->fillerRowCount)
            po->emptyRowCount -= po->fillerRowCount;
         else {
            po->fillerRowCount = po->emptyRowCount;
            po->emptyRowCount = 0;
         }
      }
   }
}

// Recompute topline to put the cursor at the bottom of the portal.
// When scrolling scroll at least "min_scroll" lines.
// If "set_topbot" is true, set topline and botline first (for "zb"). This is messy stuff!!!
pub void
scroll_cursor_bot(int min_scroll, int set_topbot) {
   int      used;
   int      scrolled = 0;
   int      extra = 0;
   int      i;
   LineNr   line_count;
   LineNr   old_topline = curPor->topLine;
   int      old_skipcol = curPor->skipCol;
   LineOffset   loff;
   LineOffset   boff;
   int      old_topfill = curPor->topFill;
   LineNr   old_botline = curPor->bottomLine;
   LineNr   old_valid = curPor->cacheState;
   int      old_empty_rows = curPor->emptyRowCount;
   LineNr   cln;          // Cursor Line Number
   long   so = curPor->o.scrollOff;
   int      doSmoothly = curPor->o.wrap && curPor->o.smoothScroll;

   cln = curPor->cursor.lnum;
   if (set_topbot) {
      used = 0;
      curPor->bottomLine = cln + 1;
      loff.lnum = cln + 1;
      loff.fill = 0;
      while (true) {
         topline_back_winheight(&loff, false);
         if (loff.height == MAXCOL)
            break;
         if (used + loff.height > (int)curPor->height) {
            if (doSmoothly) {
               // 'smoothscroll' and 'wrap' are set.  The above line is
               // too long to show in its entirety, so we show just a part of it.
               if (used < (int)curPor->height) {
                  int plines_offset = used + loff.height - curPor->height;
                  used = curPor->height;
                  curPor->topFill = loff.fill;
                  curPor->topLine = loff.lnum;
                  curPor->skipCol = skipcol_from_plines( curPor, plines_offset);
               }
            }
            break;
         }
         curPor->topFill = loff.fill;
         curPor->topLine = loff.lnum;
         used += loff.height;
      }

      normSetEmptyRowCount(curPor, used);
      curPor->cacheState |= VALID_BOTLINE|VALID_BOTLINE_AP;
      if (curPor->topLine != old_topline
         || curPor->topFill != old_topfill
         || curPor->skipCol != old_skipcol
         || curPor->skipCol != 0)
      {
         curPor->cacheState &= ~(VALID_WROW|VALID_CROW);
         if (curPor->skipCol != old_skipcol)
            redraw_later(UPD_NOT_VALID);
         else
            reset_skipcol();
      }
   } else
      validate_botline();

   // The lines of the cursor line itself are always used.
   used = plines_nofill(cln);

   // If the cursor is on or below botline, we will at least scroll by the height of the cursor 
   // line, which is "used". Correct for empty lines, which are really part of botline.
   if (cln >= curPor->bottomLine) {
      scrolled = used;
      if (cln == curPor->bottomLine)
         scrolled -= curPor->emptyRowCount;
      if (doSmoothly) {
         // 'smoothscroll' and 'wrap' are set.
         // Calculate how many screen lines the current top line of portal occupies. If it is 
         // occupying more than the entire portal, we need to scroll the additional clipped 
         // lines to scroll past the top line before we can move on to the other lines.
         int top_plines = plines_win_nofill (curPor, curPor->topLine, false);
         int width1 = widthLeft(curPor);

         if (width1 > 0) {
            int skip_lines = 0;

            // A similar formula is used in curs_columns().
            if (curPor->skipCol > width1)
                skip_lines += (curPor->skipCol - width1) / width1 + 1;
            ei (curPor->skipCol > 0)
                skip_lines = 1;

            top_plines -= skip_lines;
            if (top_plines > (int)curPor->height) {
               scrolled += (top_plines - curPor->height);
            }
         }
      }
   }

   //Stop counting lines to scroll when
   //- hitting start of the file
   //- scrolled nothing or at least 'sj' lines
   //- at least 'scrolloff' lines below the cursor
   //- lines between botline and cursor have been counted
   if (!getFolds(curPor->cursor.lnum, &loff.lnum, &boff.lnum)) {
      loff.lnum = cln;
      boff.lnum = cln;
   }
   loff.fill = 0;
   boff.fill = 0;
   int fillBelowPortal = diff_check_fill(curPor, curPor->bottomLine) - curPor->fillerRowCount;

   while (loff.lnum > 1) {
      // Stop when scrolled nothing or at least "min_scroll", found "extra"
      // context for 'scrolloff' and counted all lines below the portal.
      if ((((scrolled <= 0 || scrolled >= min_scroll)
             && extra >= (mouseDraggingG > 0 ? mouseDraggingG - 1 : so))
             || boff.lnum + 1 > curBook->mem.lineCount)
         && loff.lnum <= curPor->bottomLine
         && (loff.lnum < curPor->bottomLine
             || loff.fill >= fillBelowPortal)
         )
          break;

      // Add one line above
      topline_back(&loff);
      if (loff.height == MAXCOL)
          used = MAXCOL;
      else
          used += loff.height;
      if (used > (int)curPor->height)
          break;
      if (loff.lnum >= curPor->bottomLine
         && (loff.lnum > curPor->bottomLine || loff.fill <= fillBelowPortal)
      ) {
         // Count screen lines that are below the portal.
         scrolled += loff.height;
         if (loff.lnum == curPor->bottomLine && loff.fill == 0)
            scrolled -= curPor->emptyRowCount;
      }

      if (boff.lnum < curBook->mem.lineCount) {
         // Add one line below
         botline_forw(&boff);
         used += boff.height;
         if (used > (int)curPor->height)
         break;
         if (extra < ( mouseDraggingG > 0 ? mouseDraggingG - 1 : so) || scrolled < min_scroll) {
            extra += boff.height;
            if (boff.lnum >= curPor->bottomLine
               || (boff.lnum + 1 == curPor->bottomLine && boff.fill > curPor->fillerRowCount)
               )
            {
                // Count screen lines that are below the portal.
                scrolled += boff.height;
                if (boff.lnum == curPor->bottomLine
                   && boff.fill == 0
                   )
               scrolled -= curPor->emptyRowCount;
            }
         }
      }
   }

   // curPor->emptyRowCount is larger, no need to scroll
   if (scrolled <= 0)
      line_count = 0;
   // more than a screenfull, don't scroll but redraw
   ei (used > (int)curPor->height)
      line_count = used;
   // scroll minimal number of lines
   else {
      line_count = 0;
      boff.fill = curPor->topFill;
      boff.lnum = curPor->topLine - 1;
      for (i = 0; i < scrolled && boff.lnum < curPor->bottomLine; ) {
         botline_forw(&boff);
         i += boff.height;
         ++line_count;
      }
      if (i < scrolled)   // below curPor->bottomLine, don't scroll
         line_count = 9999;
   }

   //Scroll up if the cursor is off the bottom of the screen a bit.
   //Otherwise put it at 1/2 of the screen.
   if (line_count >= (int)curPor->height && line_count > min_scroll)
      scroll_cursor_halfway(false, true);
   ei (line_count > 0) {
      if (doSmoothly)
         scrollup(scrolled, true);  // TODO
      else
         scrollup(line_count, true);
   }

   //If topline didn't change we need to restore bottomLine and emptyRowCount (we changed them).
   //If topline did change, drawUpdateScreen() will set botline.
   if (curPor->topLine == old_topline && curPor->skipCol == old_skipcol && set_topbot) {
      curPor->bottomLine = old_botline;
      curPor->emptyRowCount = old_empty_rows;
      curPor->cacheState = old_valid;
   }
   curPor->cacheState |= VALID_TOPLINE;

   // Make sure cursor is still visible after adjusting skipcol for "zb".
   if (set_topbot)
      cursor_correct_sms();
}

// Recompute topline to put the cursor halfway the portal
// If "atend" is true, also put it halfway at the end of the file.
pub void
scroll_cursor_halfway(int atend, int prefer_above) {
   int above = 0;
   ColNr skipcol = 0;
   int topfill = 0;
   int below = 0;
   LineNr   old_topline = curPor->topLine;

   // if the width changed this needs to be updated first
   may_update_popup_position();
   LineOffset loff;
   LineOffset boff;
   loff.lnum = curPor->cursor.lnum;
   boff.lnum = curPor->cursor.lnum;
   (void)getFolds(loff.lnum, OUT &loff.lnum, OUT &boff.lnum);
   int used = plines_nofill(loff.lnum);
   loff.fill = 0;
   boff.fill = 0;
   LineNr topline = loff.lnum;

   int want_height;
   int doSmoothly = curPor->o.wrap && curPor->o.smoothScroll;
   if (doSmoothly) {
      // @smoothscroll and @wrap are set
      if (atend) {
         want_height = (curPor->height - used) / 2;
         used = 0;
      } else
         want_height = curPor->height;
   }

   while (topline > 1) {
   // If using smoothscroll, we can precisely scroll to the
   // exact point where the cursor is halfway down the screen.
   if (doSmoothly) {
      topline_back_winheight(&loff, false);
      if (loff.height == MAXCOL)
         break;
      used += loff.height;
      if (!atend && boff.lnum < curBook->mem.lineCount) {
         botline_forw(&boff);
         used += boff.height;
      }
      if (used > want_height) {
         if (used - loff.height < want_height) {
            topline = loff.lnum;
            topfill = loff.fill;
            skipcol = skipcol_from_plines(curPor, used - want_height);
         }
         break;
      }
      topline = loff.lnum;
      topfill = loff.fill;
      continue;
   }

   // If not using smoothscroll, we have to iteratively find how many lines to scroll down to 
   // roughly fit the cursor. This may not be right in the middle if the lines' physical height > 1
   // (e.g. 'wrap' is on).
   // Depending on "prefer_above" we add a line above or below first.
   // Loop twice to avoid duplicating code.
   int done = false;
   for (int round = 1; round <= 2; ++round) {
      if (prefer_above ? (round == 2 && below < above) : (round == 1 && below <= above)) {
         // add a line below the cursor
         if (boff.lnum < curBook->mem.lineCount) {
            botline_forw(&boff);
            used += boff.height;
            if (used > (int)curPor->height) {
               done = true;
               break;
            }
            below += boff.height;
         } else {
            ++below;       // count a "~" line
            if (atend)
               ++used;
         }
      }

      if (prefer_above ? (round == 1 && below >= above) : (round == 1 && below > above)) {
         // add a line above the cursor
         topline_back(&loff);
         if (loff.height == MAXCOL)
            used = MAXCOL;
         else
            used += loff.height;
         if (used > (int)curPor->height) {
            done = true;
            break;
         }
         above += loff.height;
         topline = loff.lnum;
         topfill = loff.fill;
      }
   }
   if (done)
      break;
   }

   if (!getFolds(topline, &curPor->topLine, NULL)) {
      if (curPor->topLine != topline || skipcol != 0 || curPor->skipCol != 0) {
         curPor->topLine = topline;
         if (skipcol != 0) {
            curPor->skipCol = skipcol;
            redraw_later(UPD_NOT_VALID);
         } ei (doSmoothly)
            reset_skipcol();
      }
   }
   curPor->topFill = topfill;
   if (old_topline > (int)(curPor->topLine + curPor->height))
      curPor->bottFill = false;
   check_topfill(curPor, false);
   curPor->cacheState &= ~(VALID_WROW|VALID_CROW|VALID_BOTLINE|VALID_BOTLINE_AP);
   curPor->cacheState |= VALID_TOPLINE;
}

// Correct the cursor position so that it is in a part of the screen at least 'scrolloff' lines 
// from the top and bottom, if possible. If not possible, put it at the same position as 
// scroll_cursor_halfway(). When called topline must be valid!
pub void
cursor_correct(void) {
   int      above = 0;       // screen lines above topline
   LineNr   topline;
   int      below = 0;       // screen lines below botline
   LineNr   botline;
   int      above_wanted, below_wanted;
   LineNr   cln;          // Cursor Line Number
   int      max_off;
   long   so = curPor->o.scrollOff;

   //How many lines we would like to have above/below the cursor depends on
   //whether the first/last line of the file is on screen.
   above_wanted = so;
   below_wanted = so;
   if (mouseDraggingG > 0) {
      above_wanted = mouseDraggingG - 1;
      below_wanted = mouseDraggingG - 1;
   }
   if (curPor->topLine == 1) {
      above_wanted = 0;
      max_off = curPor->height / 2;
      if (below_wanted > max_off)
          below_wanted = max_off;
   }
   validate_botline();
   if (curPor->bottomLine == curBook->mem.lineCount + 1 && mouseDraggingG == 0) {
      below_wanted = 0;
      max_off = (curPor->height - 1) / 2;
      if (above_wanted > max_off)
          above_wanted = max_off;
   }

   // If there are sufficient file-lines above and below the cursor, we can return now.
   cln = curPor->cursor.lnum;
   if (cln >= curPor->topLine + above_wanted
       && cln < curPor->bottomLine - below_wanted
       && !hasAnyFolding(curPor)
   ) {
      return;
   }

   if (curPor->o.smoothScroll && !curPor->o.wrap) {
      // @smoothscroll is active
      if (curPor->cursorLineHeight == curPor->height) {
          // The cursor line just fits in the portal, don't scroll.
          reset_skipcol();
          return;
      }
      // TODO: If the cursor line doesn't fit in the portal then only adjust skipCol.
   }

   //Narrow down the area where the cursor can be put by taking lines from
   //the top and the bottom until:
   //- the desired context lines are found
   //- the lines from the top is past the lines from the bottom
   topline = curPor->topLine;
   botline = curPor->bottomLine - 1;
   // count filler lines as context
   above = curPor->topFill;
   below = curPor->fillerRowCount;
   while ((above < above_wanted || below < below_wanted) && topline < botline) {
      if (below < below_wanted && (below <= above || above >= above_wanted)) {
         if (getFolds(botline, &botline, NULL))
            ++below;
         else
            below += plines(botline);
         --botline;
      }
      if (above < above_wanted && (above < below || below >= below_wanted)) {
         if (getFolds(topline, NULL, &topline))
            ++above;
         else
            above += PLINES_NOFILL(topline);
         // Count filler lines below this line as context.
         if (topline < botline)
            above += diff_check_fill(curPor, topline + 1);
         ++topline;
      }
   }
   if (topline == botline || botline == 0)
      curPor->cursor.lnum = topline;
   ei (topline > botline)
      curPor->cursor.lnum = botline;
   else {
      if (cln < topline && curPor->topLine > 1) {
          curPor->cursor.lnum = topline;
          curPor->cacheState &= ~(VALID_WROW|VALID_WCOL|VALID_CHEIGHT|VALID_CROW);
      }
      if (cln > botline && curPor->bottomLine <= curBook->mem.lineCount) {
          curPor->cursor.lnum = botline;
          curPor->cacheState &= ~(VALID_WROW|VALID_WCOL|VALID_CHEIGHT|VALID_CROW);
      }
   }
   check_cursor_moved(curPor);
   curPor->cacheState |= VALID_TOPLINE;
}

// Decide how much overlap to use for page-up or page-down scrolling.
// This is symmetric, so that doing both keeps the same lines displayed.
// Three lines are examined:
//
//  before CTRL-F          after CTRL-F / before CTRL-B
//     etc.                    l0
//  l0 last but one line       ------------
//  l1 last text line          l1 top text line
//  -------------              l2 second text line
//  l2                            etc.
private int
get_scroll_overlap(int dir) {
   LineOffset loff;
   int       min_height = curPor->height - 2;

   validate_botline();
   if ((dir == BACKWARD && curPor->topLine == 1)
         || (dir == FORWARD && curPor->bottomLine > curBook->mem.lineCount))
      return min_height + 2;  // no overlap, still handle 'smoothscroll'

   loff.lnum = dir == FORWARD ? curPor->bottomLine : curPor->topLine - 1;
   loff.fill = diff_check_fill( curPor, loff.lnum + (dir == BACKWARD)) 
               - (dir == FORWARD ? curPor->fillerRowCount : curPor->topFill);
   loff.height = loff.fill > 0 ? 1 : plines_nofill(loff.lnum);

   int h1 = loff.height;
   if (h1 > min_height)
      return min_height + 2;  // no overlap
   if (dir == FORWARD)
      topline_back(&loff);
   else
      botline_forw(&loff);

   int h2 = loff.height;
   if (h2 == MAXCOL || h2 + h1 > min_height)
      return min_height + 2;  // no overlap
   if (dir == FORWARD)
      topline_back(&loff);
   else
      botline_forw(&loff);

   int h3 = loff.height;
   if (h3 == MAXCOL || h3 + h2 > min_height)
      return min_height + 2;  // no overlap
   if (dir == FORWARD)
      topline_back(&loff);
   else
      botline_forw(&loff);

   int h4 = loff.height;
   if (h4 == MAXCOL || h4 + h3 + h2 > min_height || h3 + h2 + h1 > min_height)
   return min_height + 1;  // 1 line overlap
    else
   return min_height;      // 2 lines overlap
}

// Scroll "count" lines with 'smoothscroll' in direction "dir". Return true
// when scrolling happened. Adjust "curscount" for scrolling different amount of
// lines when 'smoothscroll' is disabled.
private int
scrollSmoothly(int dir, long count, long *curscount) {
   int      prev_sms = curPor->o.smoothScroll;
   ColNr   prev_skipcol = curPor->skipCol;
   LineNr   prev_topline = curPor->topLine;
   int      prev_topfill = curPor->topFill;

   curPor->o.smoothScroll = true;
   scroll_redraw(dir == FORWARD, count);

   // Not actually smoothscrolling but ended up with partially visible line.
   // Continue scrolling until skipcol is zero.
   if (!prev_sms && curPor->skipCol > 0) {
      int fixdir = dir;
      // Reverse the scroll direction when topline already changed. One line
      // extra for scrolling backward so that consuming skipcol is symmetric.
      if (labs(curPor->topLine - prev_topline) > (dir == BACKWARD))
          fixdir = dir * -1;

      int width1 = widthLeft(curPor);
      count = 1 + (curPor->skipCol - width1 - 1) / width1;
      if (fixdir == FORWARD) {
          count = 1 + (linetabsize_eol(curPor, curPor->topLine) - curPor->skipCol - 1)/width1;
      }

      scroll_redraw(fixdir == FORWARD, count);
      *curscount += count * (fixdir == dir ? 1 : -1);
   }
   curPor->o.smoothScroll = prev_sms;

   return curPor->topLine == prev_topline
      && curPor->topFill == prev_topfill
      && curPor->skipCol == prev_skipcol;
}

// Move screen "count" (half) pages up ("dir" is BACKWARD) or down ("dir" is
// FORWARD) and update the screen. Handle moving the cursor and not scrolling
// to reveal end of book lines for half-page scrolling with CTRL-D and CTRL-U.
//
// Return FAIL for failure, OK otherwise.
pub int
pagescroll(int dir, long count, int half) {
   int      nochange = true;
   int      buflen = curBook->mem.lineCount;
   ColNr   prev_col = curPor->cursor.col;
   ColNr   prev_curswant = curPor->cursWant;
   LineNr   prev_lnum = curPor->cursor.lnum;
   Operator   oa = { 0 };
   ActionArg   ca = { 0 };
   ca.oper = &oa;

   if (half) {
      // Scroll [count], @scroll or current poral height lines.
      if (count != 0)
         curPor->scroll = MIN(curPor->height, count);
      count = MIN((int)curPor->height, curPor->scroll);

      long curscount = count;
      // Adjust count so as to not reveal end of book lines.
      if (dir == FORWARD
             && (curPor->topLine + curPor->height + count > buflen
                        || hasAnyFolding(curPor)
         )
      ) {
         int n = plines_correct_topline(curPor, curPor->topLine, false);
         if (n - count < curPor->height && curPor->topLine < buflen)
            n += plines_m_win(curPor, curPor->topLine + 1, buflen, curPor->height + count);
         if (n < curPor->height + count)
            count = n - curPor->height;
      }

      // (Try to) scroll the portal unless already at the end of the book.
      
      if (count > 0) {
         nochange = scrollSmoothly(dir, count, &curscount);
         curPor->cursor.lnum = prev_lnum;
         curPor->cursor.col = prev_col;
         curPor->cursWant = prev_curswant;
      }

      // Move the cursor the same amount of screen lines.
      if (curPor->o.wrap)
          nv_screengo(&oa, dir, curscount);
      ei (dir == FORWARD)
          cursor_down_inner(curPor, curscount);
      else
          cursor_up_inner(curPor, curscount);
   } else {
       
      // Scroll [count] times @window or current portal height lines.
      count *= get_scroll_overlap(dir);

      nochange = scrollSmoothly(dir, count, &count);
          
      if (!nochange) {
         // Place cursor at top or bottom of portal.
         validate_botline();
         LineNr lnum = (dir == FORWARD ? curPor->topLine : curPor->bottomLine - 1);
         // In silent Ex mode the value of bottomLine - 1 may be 0,
         // but cursor lnum needs to be at least 1.
         curPor->cursor.lnum = MAX(lnum, 1);
      }
   }

   if (curPor->o.scrollOff > 0)
      cursor_correct();
   // Move cursor to first line of closed fold.
   foldAdjustCursor();
    
   nochange = nochange
      && prev_col == curPor->cursor.col
      && prev_lnum == curPor->cursor.lnum;

   // Error if both the viewport and cursor did not change.
   if (nochange)
      inpFlushIfNotSilent();
   ei (!curPor->o.smoothScroll)
      beginline(BL_SOL | BL_FIX);
   ei (p_sol)
      nv_g_home_m_cmd(&ca);

   return nochange;
}

pub void
do_check_cursorbind(void) {
   static Portal* prev_curPor = NULL;
   static Pos   prev_cursor = {0, 0, 0};

   if (curPor == prev_curPor && EQUAL_POS(curPor->cursor, prev_cursor))
      return;
   prev_curPor = curPor;
   prev_cursor = curPor->cursor;

   LineNr   line = curPor->cursor.lnum;
   ColNr   col = curPor->cursor.col;
   ColNr   coladd = curPor->cursor.coladd;
   ColNr   curswant = curPor->cursWant;
   int      set_curswant = curPor->setCursWant;
   Portal   *old_curPor = curPor;
   Book* oldCurBook = curBook;
   int      old_VIsual_active = VIsual_active;

   // loop through the cursorbound portals
   VIsual_active = 0;
   FOR_ALL_PORTALS(curPor) {
      curBook = curPor->book;
      // skip original portal and portals without @diff
      if (curPor != old_curPor && curPor->o.diff) {
         curPor->cursor.lnum = diff_get_corresponding_line(oldCurBook, line);
         curPor->cursor.col = col;
         curPor->cursor.coladd = coladd;
         curPor->cursWant = curswant;
         curPor->setCursWant = set_curswant;

         // Make sure the cursor is in a valid position.  Temporarily set
         // "restart_edit" to allow the cursor to be beyond the EOL.
         int restart_edit_save = restart_edit;
         restart_edit = 'a';
         check_cursor();

         // Avoid a scroll here for the cursor position, scrollbinding is more important.
         if (!curPor->o.diff)
            validate_cursor();

         restart_edit = restart_edit_save;
         // Correct cursor for multi-byte character.
         mb_adjust_cursor();
         redraw_later(UPD_VALID);

         // Only scroll when @diff hasn't done this.
         if (!curPor->o.diff)
            update_topline();
         curPor->statusLineNeedsRedraw = true;
      }
   }

   // reset current-portal
   VIsual_active = old_VIsual_active;
   curPor = old_curPor;
   curBook = oldCurBook;
}

//}}}
//{{{map: code for mappings and abbreviations.

// Used for the first argument of do_map()
#define MAPTYPE_MAP       0
#define MAPTYPE_UNMAP     1
#define MAPTYPE_NOREMAP   2
#define MAPTYPE_UNMAP_LHS 3


//List used for abbreviations.
private MapBlock* first_abbr = NULL; // first entry in abbrlist

// Each mapping is put in one of the 256 hash lists, to speed up finding it.
private MapBlock* (mappingTable[256]);
private int      mappingTable_valid = false;

// When non-zero then no mappings can be added or removed. Prevents mappings
// to change while listing them.
private int      map_locked = 0;

// Make a hash value for a mapping.
// "mode" is the lower 4 bits of the stateG for the mapping.
// "c1" is the first character of the "lhs".
// Returns a value between 0 and 255, index in mappingTable.
// Put Normal/Visual mode mappings mostly separately from Insert/Cmdline mode.
#define MAP_HASH(mode, c1) \
   (((mode) & (MODE_NORMAL | MODE_VISUAL | MODE_OP_PENDING | MODE_TERMINAL)) \
      ? (c1) : ((c1) ^ 0x80))

//Get the start of the hashed map list for "state" and first character "c".
pub MapBlock*
getMappingTableList(int state, int c) {
   return mappingTable[MAP_HASH(state, c)];
}

// Get the book-local hashed map list for "state" and first character "c".
pub MapBlock *
getBufMappingTableList(int state, int c) {
   return curBook->localMappings[MAP_HASH(state, c)];
}

pub int
isMappingTableValid(void) {
   return mappingTable_valid;
}

// Initialize mappingTable[] for first use.
private void
validateMappingTable(void) {
   if (mappingTable_valid) {
      return;
   }

   CLEAR_FIELD(mappingTable);
   mappingTable_valid = true;
}

//Delete an entry from the abbrlist or mappingTable[].
//"mpp" is a pointer to the "next" field of the PREVIOUS entry!
private void
mapFree(MapBlock** mpp) {
   MapBlock* mp = *mpp;
   eeglFree(mp->lhs);
   if (mp->alt != NULL) {
      mp->alt->alt = NULL;
   }
   eeglFree(mp->rhs);
   eeglFree(mp->origRhs);
   *mpp = mp->next;
   reset_last_used_map(mp);
   eeglFree(mp);
}

//Return characters to represent the map mode in an allocated string. NULL when out of memory.
private CS
mapModeToChars(int mode) {
   ArrayList    mapmode;

   ga_init2(&mapmode, 1, 7);

   if ((mode & (MODE_INSERT | MODE_COMMLINE)) == (MODE_INSERT | MODE_COMMLINE))
      ga_append(&mapmode, '!');         // :map!
   ei (mode & MODE_INSERT)
      ga_append(&mapmode, 'i');         // :imap
   ei (mode & MODE_LANGMAP)
      ga_append(&mapmode, 'l');         // :lmap
   ei (mode & MODE_COMMLINE)
      ga_append(&mapmode, 'c');         // :cmap
   ei ( mode == (MODE_NORMAL | MODE_VISUAL | MODE_OP_PENDING)) {
      ga_append(&mapmode, ' ');         // :map
   } else {
       if (mode & MODE_NORMAL)
          ga_append(&mapmode, 'n');         // :nmap
       if (mode & MODE_OP_PENDING)
          ga_append(&mapmode, 'o');         // :omap
       if (mode & MODE_TERMINAL)
          ga_append(&mapmode, 't');         // :tmap
       if ((mode & MODE_VISUAL) == MODE_VISUAL)
          ga_append(&mapmode, 'v');         // :vmap
       else {
          if (mode & MODE_VISUAL)
             ga_append(&mapmode, 'x');      // :xmap
       }
   }

   ga_append(&mapmode, ZERO);
   return (CS)mapmode.c;
}

//Output a line for one mapping.
private void
showMap(MapBlock* mp, int local) {      // true for book-local map
   int len = 1;

   if (message_filtered(mp->lhs) && message_filtered(mp->rhs))
      return;

   // Prevent mappings to be cleared while at the more prompt.
   // Must jump to "theend" instead of returning.
   ++map_locked;

   if (msg_didout || msg_silent != 0) {
       msg_putchar('\n');
       if (gotInterruptG)       // 'q' typed at MORE prompt
          { goto theend; }
   }

   CS mapchars = mapModeToChars(mp->mode);
   if (mapchars) {
      msg_puts(mapchars);
      len = (int)STRLEN(mapchars);
      eeglFree(mapchars);
   }

   while (++len <= 3) {
      msg_putchar(' ');
   }

   // Display the LHS.  Get length of what we write.
   len = msg_outtrans_special(mp->lhs, true, 0);
   do {
      msg_putchar(' ');      // pad with blanks
      ++len;
   } while (len < 12);

   if (mp->noremap == REMAP_NONE)
      msgPutsDeco((CS)"*", getDecoFlags(HLF_8));
   ei (mp->noremap == REMAP_SCRIPT)
      msgPutsDeco((CS)"&", getDecoFlags(HLF_8));
   else
      msg_putchar(' ');

   if (local) {
      msg_putchar('@');
   } else {
      msg_putchar(' ');
   }

   // Use false below if we only want things like <Up> to show up as such on
   // the rhs, and not M-x etc, true gets both -- webb
   if (*mp->rhs == ZERO)
      msgPutsDeco((CS)"<Nop>", getDecoFlags(HLF_8));
   else
      msg_outtrans_special(mp->rhs, false, 0);
   if (p_verbose > 0)
      lastSetMsg(mp->scriptCtx);
   msg_clr_eos();
   out_flush();         // show one line at a time

theend:
   --map_locked;
}

private MapBlock *
addToMap(
   MapBlock** map_table,
   MapBlock** abbr_table,
   CS keys,
   CS rhs,
   CS orig_rhs,
   Unt noremap,
   int nowait,
   int silent,
   int mode,
   int is_abbr,
   int expr,
   ScriptId sid,       // 0 to use scriptPosG
   LineNr lnum,
   int simplified
){
   MapBlock   *mp = ALLOC_CLEAR_ONE(MapBlock);
   if (!mp)
      return NULL;

   // If CTRL-C has been mapped, don't always use it for Interrupting.
   if (*keys == Ctrl_C) {
      if (map_table == curBook->localMappings)
         curBook->mappedCtrlC |= mode;
      else
         mapped_ctrl_c |= mode;
   }

   mp->lhs = copyStr(keys);
   mp->rhs = copyStr(rhs);
   mp->origRhs = copyStr(orig_rhs);
   mp->keylen = (int)STRLEN(mp->lhs);
   mp->noremap = noremap;
   mp->nowait = nowait;
   mp->silent = silent;
   mp->mode = mode;
   //log("ccc simpl map.c 242 left [%s] rig [%s]", mp->lhs, mp->rhs);
   mp->simplified = simplified;
   mp->expr = expr;
   if (sid != 0) {
      mp->scriptCtx.sid = sid;
      mp->scriptCtx.lineNr = lnum;
   } else {
      mp->scriptCtx = scriptPosG;
      mp->scriptCtx.lineNr += SOURCING_LNUM;
   }

   // add the new entry in front of the abbrlist or mappingTable[] list
    
   if (is_abbr) {
      mp->next = *abbr_table;
      *abbr_table = mp;
   } else {
      int n = MAP_HASH(mp->mode, mp->lhs[0]);

      mp->next = map_table[n];
      map_table[n] = mp;
   }
   return mp;
}

//List mappings. When "haskey" is false all mappings, otherwise mappings that match "keys[keys_len]"
private void
listMappings(
   int keyround,
   int abbrev,
   int haskey,
   CS keys,
   int keys_len,
   int mode,
   int* did_local
) {
   // Prevent mappings to be cleared while at the more prompt.
   ++map_locked;

   if (p_verbose > 0 && keyround == 1) {
      if (seenModifyOtherKeys)
          msg_puts(_("Seen modifyOtherKeys: true\n"));

      if (modify_otherkeys_state != MOKS_INITIAL) {
          CS name;
          switch (modify_otherkeys_state) {
          case MOKS_INITIAL: break;
          case MOKS_OFF: name = _("Off"); break;
          case MOKS_ENABLED: name = _("On"); break;
          case MOKS_DISABLED: name = _("Disabled"); break;
          case MOKS_AFTER_T_TE: name = _("Cleared"); break;
          default: name = _("Unknown");
          }

          Byte buffer[200];
          eeSnprintf(buffer, sizeof(buffer), _("modifyOtherKeys detected: %s\n"), name);
          msg_puts(buffer);
      }

      CS name = _("On");
      Byte buffer[200];
      eeSnprintf(buffer, sizeof(buffer), _("Kitty keyboard protocol: %s\n"), name);
      msg_puts(buffer);
   }

   // need to loop over all global hash lists
   for (int hash = 0; hash < 256 && !gotInterruptG; ++hash) {
      MapBlock   *mp;
      if (abbrev) {
         if (hash != 0)   // there is only one abbreviation list
            break;
         mp = curBook->firstAbbr;
      } else
         mp = curBook->localMappings[hash];
      for ( ; mp && !gotInterruptG; mp = mp->next) {
         // check entries with the same mode
         if (!mp->simplified && (mp->mode & mode) != 0) {
            if (!haskey) {          // show all entries
               showMap(mp, true);
               *did_local = true;
            } else {
               int n = mp->keylen;
               if (STRNCMP(mp->lhs, keys, (Unt)(n < keys_len ? n : keys_len)) == 0) {
                  showMap(mp, true);
                  *did_local = true;
               }
            }
         }
      }
   }

   --map_locked;
}

//map[!]          : show all key mappings
//map[!] {lhs}          : show key mapping for {lhs}
//map[!] {lhs} {rhs}       : set key mapping for {lhs} to {rhs}
//noremap[!] {lhs} {rhs}   : same, but no remapping for {rhs}
//unmap[!] {lhs}       : remove key mapping for {lhs}
//abbr             : show all abbreviations
//abbr {lhs}          : show abbreviations for {lhs}
//abbr {lhs} {rhs}       : set abbreviation for {lhs} to {rhs}
//noreabbr {lhs} {rhs}       : same, but no remapping for {rhs}
//unabbr {lhs}          : remove abbreviation for {lhs}
//
//maptype: MAPTYPE_MAP for :map or :abbr
//      MAPTYPE_UNMAP for :unmap or :unabbr
//      MAPTYPE_NOREMAP for :noremap or :noreabbr
//      MAPTYPE_UNMAP_LHS is like MAPTYPE_UNMAP, but doesn't try to match
//      with {rhs} if there is no match with {lhs}.
//
//arg is pointer to any arguments. Note: arg cannot be a read-only string,
//it will be modified.
//
//for :map   mode is MODE_NORMAL | MODE_VISUAL | MODE_OP_PENDING
//for :map!  mode is MODE_INSERT | MODE_COMMLINE
//for :cmap  mode is MODE_COMMLINE
//for :imap  mode is MODE_INSERT
//for :lmap  mode is MODE_LANGMAP
//for :nmap  mode is MODE_NORMAL
//for :vmap  mode is MODE_VISUAL
//for :xmap  mode is MODE_VISUAL
//for :omap  mode is MODE_OP_PENDING
//for :tmap  mode is MODE_TERMINAL
//
//for :abbr  mode is MODE_INSERT | MODE_COMMLINE
//for :iabbr mode is MODE_INSERT
//for :cabbr mode is MODE_COMMLINE
//
//Return 0 for success
//    1 for invalid arguments
//    2 for no match
//    4 for out of mem
//    5 for entry not unique
pub int
do_map(int maptype, CS arg, Unt mode, int abbrev){ // not a mapping but an abbreviation
   CS keys;
   MapBlock* foundMapping;
   MapBlock** mpp;
   MapBlock* mp_result[2] = {NULL, NULL};
   int n;
   int len = 0;   // init for GCC
   int hasarg;
   int haskey;
   int do_print;
   CS keysBuffer = NULL;
   CS alt_keysBuffer = NULL;
   CS arg_buf = NULL;
   int      retval = 0;
   Boole unique = false;
   Boole nowait = false;
   Boole silent = false;
   Boole special = false;
   Boole expr = false;
   Boole didSimplify = false;
   int      unmap_lhs_only = false;
   CS orig_rhs;
   keys = arg;
   MapBlock** map_table = mappingTable;
   MapBlock** abbr_table = &first_abbr;

   if (maptype == MAPTYPE_UNMAP_LHS) {
      unmap_lhs_only = true;
      maptype = MAPTYPE_UNMAP;
   }

   // For ":noremap" don't remap, otherwise do remap.
   Unt noremap = (maptype == MAPTYPE_NOREMAP) ? REMAP_NONE : REMAP_YES;

   //{{{ Accept <book>, <nowait>, <silent>, <expr> <script> and <unique> in any order.
   for (;;) {
      // Check for "<book>": mapping local to book.
      if (STRNCMP(keys, "<book>", 8) == 0) {
          keys = skipwhite(keys + 8);
          map_table = curBook->localMappings;
          abbr_table = &curBook->firstAbbr;
          continue;
      }

      // Check for "<nowait>": don't wait for more characters.
      if (STRNCMP(keys, "<nowait>", 8) == 0) {
         keys = skipwhite(keys + 8);
         nowait = true;
         continue;
      }

      // Check for "<silent>": don't echo commands.
      if (STRNCMP(keys, "<silent>", 8) == 0) {
         keys = skipwhite(keys + 8);
         silent = true;
         continue;
      }

      // Check for "<special>": accept special keys in <>
      if (STRNCMP(keys, "<special>", 9) == 0) {
         keys = skipwhite(keys + 9);
         special = true;
         continue;
      }

      // Check for "<script>": remap script-local mappings only
      if (STRNCMP(keys, "<script>", 8) == 0) {
         keys = skipwhite(keys + 8);
         noremap = REMAP_SCRIPT;
         continue;
      }

      // Check for "<expr>": {rhs} is an expression.
      if (STRNCMP(keys, "<expr>", 6) == 0) {
         keys = skipwhite(keys + 6);
         expr = true;
         continue;
      }
      // Check for "<unique>": don't overwrite an existing mapping.
      if (STRNCMP(keys, "<unique>", 8) == 0) {
         keys = skipwhite(keys + 8);
         unique = true;
         continue;
      }
      break;
   } //}}}

   validateMappingTable();

   // Find end of keys and skip CTRL-Vs (and backslashes) in it.
   // Accept backslash like CTRL-V
   // with :unmap white space is included in the keys, no argument possible.
   CS p = keys;
   while (*p && (maptype == MAPTYPE_UNMAP || !SPACE_OR_TAB(*p))) {
      if ((p[0] == Ctrl_V) && p[1] != ZERO) {
         ++p;      // skip CTRL-V or backslash
      }
      ++p;
   }
   if (*p != ZERO) {
      *p++ = ZERO;
   }

   p = skipwhite(p);
   CS rhs = p;
   hasarg = (*rhs != ZERO);
   haskey = (*keys != ZERO);
   do_print = !haskey || (maptype != MAPTYPE_UNMAP && !hasarg);

   // check for :unmap without argument
   if (maptype == MAPTYPE_UNMAP && !haskey) {
      retval = 1;
      goto theend;
   }

   //If mapping has been given as ^V<C_UP> say, then replace the term codes with the appropriate 
   //2 bytes. If it is a shifted special key, unshift it too, giving another two bytes.
   //replace_termcodes() may move the result to allocated memory, which needs to be freed later 
   //(*keysBuffer and *arg_buf).
   //replace_termcodes() also removes CTRL-Vs and sometimes backslashes. If something like <C-H> 
   //is simplified to 0x08 then mark it as simplified and also add an entry with a modifier, 
   //which will work when using a key protocol.
   if (haskey) {
      Unt flags = REPTERM_FROM_PART | REPTERM_DO_LT;

      if (special)
         flags |= REPTERM_SPECIAL;
      CS new_keys = replace_termcodes(keys, &keysBuffer, 0, flags, OUT &didSimplify, false);
      if (didSimplify) {
         (void)replace_termcodes(
               keys, &alt_keysBuffer, 0, flags | REPTERM_NO_SIMPLIFY, NULL, false
         );
      }
      keys = new_keys;
   }
   orig_rhs = rhs;
   if (hasarg) {
      if (caseInsensitiveCompare(rhs, "<nop>") == 0) { // "<Nop>" means nothing
         rhs = S"";
      } else {
         rhs = replace_termcodes(
            rhs, &arg_buf, 0, REPTERM_DO_LT | (special ? REPTERM_SPECIAL : 0), NULL, false
         );
      }
   }

   // The following is done twice if we have two versions of keys: "alt_keysBuffer" is not NULL
   for (int keyround = 1; keyround <= 2; ++keyround) {
      int did_it = false;
      int did_local = false;
      int keyroundIs1AndDidSimplify = keyround == 1 && didSimplify;
      int round;
      int num_rounds;

      if (keyround == 2) {
         if (!alt_keysBuffer) {
            break;
         }
         keys = alt_keysBuffer;
      } ei (alt_keysBuffer && do_print) {
         // when printing always use the not-simplified map
         keys = alt_keysBuffer;
      }
      // check arguments and translate function keys
      if (haskey) {
         len = (int)STRLEN(keys);
         if (len > MAXMAPLEN) {   // maximum length of MAXMAPLEN chars
            retval = 1;
            goto theend;
         }

         if (abbrev && maptype != MAPTYPE_UNMAP) {
            // If an abbreviation ends in a keyword character, the
            // rest must be all keyword-char or all non-keyword-char.
            // Otherwise we won't be able to find the start of it in a vi-compatible way.
            int   first, last;
            int   same = -1;

            first = eeIsWordPtr(keys);
            last = first;
            p = keys + utfCharLen(keys);
            n = 1;
            while (p < keys + len) {
               ++n;         // nr of (multi-byte) chars
               last = eeIsWordPtr(p);   // type of last char
               if (same == -1 && last != first) {
                  same = n - 1;   // count of same char type
               }
               p += utfCharLen(p);
            }
            if (last && n > 2 && same >= 0 && same < n - 1) {
               retval = 1;
               goto theend;
            }
            // An abbreviation cannot contain white space.
            for (n = 0; n < len; ++n) {
               if (SPACE_OR_TAB(keys[n])) {
                  retval = 1;
                  goto theend;
               }
            }
         }
      }

      if (haskey && hasarg && abbrev) {   // if we will add an abbreviation
         no_abbr = false;
      }

      if (do_print) {
         msg_start();
      }

      // Check if a new local mapping wasn't yet defined globally.
      if (
         unique && map_table == curBook->localMappings && haskey && hasarg && maptype != MAPTYPE_UNMAP
      ) {
          // need to loop over all global hash lists
          for (int hash = 0; hash < 256 && !gotInterruptG; ++hash) {
             if (abbrev) {
                 if (hash != 0)   // there is only one abbreviation list
                    break;
                 foundMapping = first_abbr;
             } else {
                 foundMapping = mappingTable[hash];
             }
             for ( ; foundMapping != NULL && !gotInterruptG; foundMapping = foundMapping->next) {
                // check entries with the same mode
                if ((foundMapping->mode & mode) != 0
                   && foundMapping->keylen == len
                   && STRNCMP(foundMapping->lhs, keys, (Unt)len) == 0
                ) {
                  if (abbrev)
                     showErrFmtMsg((e_global_abbreviation_already_exists_for_str), foundMapping->lhs);
                  else
                     showErrFmtMsg(_(e_global_mapping_already_exists_for_str), foundMapping->lhs);
                  retval = 5;
                  goto theend;
                }
            }
         }
      }

      // When listing global mappings, also list book-local ones here.
      if (map_table != curBook->localMappings && !hasarg && maptype != MAPTYPE_UNMAP) {
         listMappings(keyround, abbrev, haskey, keys, len, mode, &did_local);
      }

      // Find an entry in the mappingTable[] list that matches.
      // For :unmap we may loop two times: once to try to unmap an entry with
      // a matching 'from' part, a second time, if the first fails, to unmap
      // an entry with a matching 'to' part. This was done to allow
      // ":ab foo bar" to be unmapped by typing ":unab foo", where "foo" will
      // be replaced by "bar" because of the abbreviation.
      num_rounds = maptype == MAPTYPE_UNMAP && !unmap_lhs_only ? 2 : 1;
      for (round = 0; round < num_rounds && !did_it && !gotInterruptG; ++round) {
         // need to loop over all hash lists
         for (int hash = 0; hash < 256 && !gotInterruptG; ++hash) {
            if (abbrev) {
               if (hash > 0)   // there is only one abbreviation list
                  break;
               mpp = abbr_table;
            } else
               mpp = &(map_table[hash]);
            for (foundMapping = *mpp; foundMapping != NULL && !gotInterruptG; foundMapping = *mpp) {
               if ((foundMapping->mode & mode) == 0) {
                  // skip entries with wrong mode
                  mpp = &(foundMapping->next);
                  continue;
               }
               if (!haskey) {  // show all entries
                  if (!foundMapping->simplified) {
                     showMap(foundMapping, map_table != mappingTable);
                     did_it = true;
                  }
               } else {  // do we have a match?
                  if (round) {  // second round: Try unmap "rhs" string
                     n = (int)STRLEN(foundMapping->rhs);
                     p = foundMapping->rhs;
                  } else {
                     n = foundMapping->keylen;
                     p = foundMapping->lhs;
                  }
                  if (STRNCMP(p, keys, (Unt)(n < len ? n : len)) == 0) {
                     if (maptype == MAPTYPE_UNMAP) {
                        // Delete entry.
                        // Only accept a full match.  For abbreviations we ignore trailing space 
                        // when matching with the "lhs", since an abbreviation can't have
                        // trailing space.
                        if (n != len 
                           && (!abbrev || round || n > len || *skipwhite(keys + n) != ZERO)
                        ) {
                           mpp = &(foundMapping->next);
                           continue;
                        }
                        //In keyround for simplified keys, don't unmap a mapping without 
                        //simplified flag
                        if (keyroundIs1AndDidSimplify && !foundMapping->simplified)
                           { break; }
                        // We reset the indicated mode bits. If nothing
                        // is left the entry is deleted below.
                        foundMapping->mode &= ~mode;
                        did_it = true;   // remember we did something
                     } ei (!hasarg) {  // show matching entry
                        if (!foundMapping->simplified) {
                           showMap(foundMapping, map_table != mappingTable);
                           did_it = true;
                        }
                     } ei (n != len) {  // new entry is ambiguous
                        mpp = &(foundMapping->next);
                        continue;
                     } ei (unique) {
                        if (abbrev)
                           showErrFmtMsg(_(e_abbreviation_already_exists_for_str), p);
                        else
                           showErrFmtMsg(_(e_mapping_already_exists_for_str), p);
                        retval = 5;
                        goto theend;
                     } else {
                        // new rhs for existing entry
                        foundMapping->mode &= ~mode;   // remove mode bits
                        if (foundMapping->mode == 0 && !did_it) {// reuse entry
                           CS newstr = copyStr(rhs);

                           if (foundMapping->alt != NULL)
                              { foundMapping->alt = foundMapping->alt->alt = NULL; }
                           eeglFree(foundMapping->rhs);
                           foundMapping->rhs = newstr;
                           eeglFree(foundMapping->origRhs);
                           foundMapping->origRhs = copyStr(orig_rhs);
                           foundMapping->noremap = noremap;
                           foundMapping->nowait = nowait;
                           foundMapping->silent = silent;
                           foundMapping->mode = mode;
                           //log("ccc simpl map.c 735 arg [%s] val %d", arg, keyroundIs1AndDidSimplify);
                           foundMapping->simplified = keyroundIs1AndDidSimplify;
                           foundMapping->expr = expr;
                           foundMapping->scriptCtx = scriptPosG;
                           foundMapping->scriptCtx.lineNr += SOURCING_LNUM;
                           mp_result[keyround - 1] = foundMapping;
                           did_it = true;
                        }
                     }
                     if (foundMapping->mode == 0) { // entry can be deleted
                        mapFree(mpp);
                        continue;   // continue with *mpp
                     }

                     // May need to put this entry into another hash list.
                     int newHash = MAP_HASH(foundMapping->mode, foundMapping->lhs[0]);
                     if (!abbrev && newHash != hash) {
                          *mpp = foundMapping->next;
                          foundMapping->next = map_table[newHash];
                          map_table[newHash] = foundMapping;

                          continue;   // continue with *mpp
                     }
                  }
               }
               mpp = &(foundMapping->next);
            }
         }
      }

      if (maptype == MAPTYPE_UNMAP) {
         // delete entry
         if (!did_it)   {
            if (!keyroundIs1AndDidSimplify)
               retval = 2;      // no match
         } ei (*keys == Ctrl_C)   {
            // If CTRL-C has been unmapped, reuse it for Interrupting.
            if (map_table == curBook->localMappings)
               curBook->mappedCtrlC &= ~mode;
            else
               mapped_ctrl_c &= ~mode;
         }
         continue;
      }

      if (!haskey || !hasarg) {
         // print entries
         if (!did_it && !did_local) {
            if (abbrev)
               msg(_("No abbreviation found"));
            else
               msg(_("No mapping found"));
         }
         goto theend;    // listing finished
      }

      if (did_it)
         continue;   // have added the new entry already

      // Get here when adding a new entry to the mappingTable[] list or abbrlist.
      mp_result[keyround - 1] = addToMap(
         map_table, abbr_table, keys,
         rhs, orig_rhs, noremap, nowait, silent, mode, abbrev,
         expr, /* sid */ 0, /* lnum */ 0,
         keyroundIs1AndDidSimplify
      );
      if (mp_result[keyround - 1] == NULL) {
         retval = 4;       // no mem
         goto theend;
      }
   }

   if (mp_result[0] != NULL && mp_result[1] != NULL) {
      mp_result[0]->alt = mp_result[1];
      mp_result[1]->alt = mp_result[0];
   }

theend:
   eeglFree(keysBuffer);
   eeglFree(alt_keysBuffer);
   eeglFree(arg_buf);
   return retval;
}

// Get the mapping mode from the command name.
private int
getMapMode(CS* cmdp, Boole forceit) {
   int      mode;

   CS p = *cmdp;
   int modec = *p++;
   if (modec == 'i')
       mode = MODE_INSERT;            // :imap
   ei (modec == 'l')
       mode = MODE_LANGMAP;            // :lmap
   ei (modec == 'c')
       mode = MODE_COMMLINE;            // :cmap
   ei (modec == 'n' && *p != 'o')          // avoid :noremap
       mode = MODE_NORMAL;            // :nmap
   ei (modec == 'v')
       mode = MODE_VISUAL;      // :vmap
   ei (modec == 'x')
      mode = MODE_VISUAL;            // :xmap
   ei (modec == 'o')
      mode = MODE_OP_PENDING;         // :omap
   ei (modec == 't')
      mode = MODE_TERMINAL;         // :tmap
   else {
      --p;
      if (forceit)
         mode = MODE_INSERT | MODE_COMMLINE;      // :map !
      else
         mode = MODE_VISUAL | MODE_NORMAL | MODE_OP_PENDING; // :map
   }
   *cmdp = p;
   return mode;
}

//Clear all mappings (":mapclear") or abbreviations (":abclear").
//"abbr" should be false for mappings, true for abbreviations.
private void
mapClear(CS cmdp, CS arg, Boole forceit, int abbr) {
   int local = (STRCMP(arg, "<book>") == 0);
   if (!local && *arg != ZERO) {
      emsg(_(e_invalid_argument));
      return;
   }

   int mode = getMapMode(&cmdp, forceit);
   mapClearAllMappingsInMode(curBook, mode, local, abbr);
}

//If "map_locked" is set then give an error and return true.
//Otherwise return false.
private int
isMapLocked(void) {
   if (map_locked > 0) {
      emsg(_(e_cannot_change_mappings_while_listing));
      return true;
   }
   return false;
}

pub void
mapClearAllMappingsInMode(
   Book* book,      // book for local mappings
   int      modeClearFrom,      // mode in which to delete
   Boole      localOnly,      // true for buffer-local mappings
   Boole      abbr      // true for abbreviations
){
   MapBlock** mpp;
   int      hash;
   int      newHash;

   if (isMapLocked())
      return;

   validateMappingTable();

   for (hash = 0; hash < 256; ++hash) {
      if (abbr) {
         if (hash > 0)   // there is only one abbrlist
            break;
         if (localOnly)
            mpp = &book->firstAbbr;
         else
            mpp = &first_abbr;
      } ei (localOnly)
         mpp = &book->localMappings[hash];
      else
         mpp = &mappingTable[hash];
      while (*mpp != NULL) {
         MapBlock* mapping = *mpp;
         if (mapping->mode & modeClearFrom) {
            mapping->mode &= ~modeClearFrom;
            if (mapping->mode == 0) { // entry can be deleted
               mapFree(mpp);
               continue;
            }
            // May need to put this entry into another hash list.
            newHash = MAP_HASH(mapping->mode, mapping->lhs[0]);
            if (!abbr && newHash != hash) {
               *mpp = mapping->next;
               if (localOnly) {
                  mapping->next = book->localMappings[newHash];
                  book->localMappings[newHash] = mapping;
               } else {
                  mapping->next = mappingTable[newHash];
                  mappingTable[newHash] = mapping;
               }
               continue;      // continue with *mpp
            }
         }
         mpp = &(mapping->next);
      }
   }
}

pub int
mode_str2flags(CS modechars) {
   int      mode = 0;

   if (firstOccurrence(modechars, 'n') != NULL)
      mode |= MODE_NORMAL;
   if (firstOccurrence(modechars, 'v') != NULL)
      mode |= MODE_VISUAL;
   if (firstOccurrence(modechars, 'x') != NULL)
      mode |= MODE_VISUAL;
   if (firstOccurrence(modechars, 'o') != NULL)
      mode |= MODE_OP_PENDING;
   if (firstOccurrence(modechars, 'i') != NULL)
      mode |= MODE_INSERT;
   if (firstOccurrence(modechars, 'l') != NULL)
      mode |= MODE_LANGMAP;
   if (firstOccurrence(modechars, 'c') != NULL)
      mode |= MODE_COMMLINE;

   return mode;
}

//Return true if a map exists that has "str" in the rhs for mode "modechars".
//Recognize termcap codes in "str". Also check mappings local to the current book.
pub int
map_to_exists(CS str, CS modechars, int abbr) {
    CS buffer;
    CS rhs = replace_termcodes(str, OUT &buffer, 0, REPTERM_DO_LT, NULL, false);
    int retval = map_to_exists_mode(rhs, mode_str2flags(modechars), abbr);
    eeglFree(buffer);
    return retval;
}

//Return true if a map exists that has "str" in the rhs for mode "mode".
//Also checks mappings local to the current book.
pub Boole
map_to_exists_mode(CS rhs, int mode, int abbr) {
   MapBlock   *mp;
   int      hash;
   Boole isExpandBook = false;

   validateMappingTable();

   // Do it twice: once for global maps and once for local maps.
   for (;;) {
      for (hash = 0; hash < 256; ++hash) {
         if (abbr) {
            if (hash > 0)      // there is only one abbr list
               break;
            if (isExpandBook)
               mp = curBook->firstAbbr;
            else
               mp = first_abbr;
         } ei (isExpandBook)
            mp = curBook->localMappings[hash];
         else
            mp = mappingTable[hash];
         for (; mp; mp = mp->next) {
            if ((mp->mode & mode) && STRSTR(mp->rhs, rhs) != NULL)
               return true;
         }
      }
      if (isExpandBook)
         break;
      isExpandBook = true;
   }

   return false;
}

// Used below when expanding mapping/abbreviation names.
private int expand_mapmodes = 0;
private int expand_isabbrev = 0;
private int expand_buffer = false;

//Translate an internal mapping/abbreviation representation into the
//corresponding external one recognized by :map/:abbrev commands.
//
//This function is called when expanding mappings/abbreviations on the command-line.
//
//It uses an ArrayList to build the translation string since the latter can be
//wider than the original description. The caller has to free the string afterwards.
//
//Return NULL when there is a problem.
private CS
translateMapping(CS str) {
   ArrayList   ga;
   Unt      c;
   int      modifiers;
   ga_init(&ga);
   ga.ga_itemsize = 1;
   ga.ga_growsize = 40;


   for (; *str; ++str) {
      c = *str;
      if (c == K_SPECIAL && str[1] != ZERO && str[2] != ZERO) {
         modifiers = 0;
         if (str[1] == KS_MODIFIER) {
            str++;
            modifiers = *++str;
            c = *++str;
         }
         if (c == K_SPECIAL && str[1] != ZERO && str[2] != ZERO) {
            c = TO_SPECIAL(str[1], str[2]);
            if (c == K_ZERO)   // display <ZERO> as ^@
                c = ZERO;
            str += 2;
         }
         if (IS_SPECIAL(c) || modifiers) {  // special key
            ga_concat(&ga, get_special_key_name(c, modifiers));
            continue; // for (str)
         }
      }
      if (c == ' ' || c == '\t' || c == Ctrl_J || c == Ctrl_V || (c == '<'))
         ga_append(&ga, Ctrl_V);
      if (c)
         ga_append(&ga, c);
   }
   ga_append(&ga, ZERO);
   return (CS)(ga.c);
}

//Work out what to complete when doing command line completion of mapping or abbreviation names.
pub CS
set_context_in_map_cmd(
   Expand* xp,
   CS cmd,
   CS arg,
   Boole forceit,   // true if '!' given
   Boole isabbrev,   // true if abbreviation
   Boole isunmap,   // true if unmap/unabbrev command
   CommIndex   id
) {
   if (forceit && id != C_map && id != C_unmap)
      xp->context = EXPAND_NOTHING;
   else {
      if (isunmap)
         expand_mapmodes = getMapMode(&cmd, forceit || isabbrev);
      else {
         expand_mapmodes = MODE_INSERT | MODE_COMMLINE;
         if (!isabbrev)
           expand_mapmodes += MODE_VISUAL | MODE_NORMAL | MODE_OP_PENDING;
      }
      expand_isabbrev = isabbrev;
      xp->context = EXPAND_MAPPINGS;
      expand_buffer = false;
      for (;;) {
         if (STRNCMP(arg, "<book>", 8) == 0) {
            expand_buffer = true;
            arg = skipwhite(arg + 8);
            continue;
         }
         if (STRNCMP(arg, "<unique>", 8) == 0) {
            arg = skipwhite(arg + 8);
            continue;
         }
         if (STRNCMP(arg, "<nowait>", 8) == 0) {
            arg = skipwhite(arg + 8);
            continue;
         }
         if (STRNCMP(arg, "<silent>", 8) == 0) {
            arg = skipwhite(arg + 8);
            continue;
         }
         if (STRNCMP(arg, "<special>", 9) == 0) {
            arg = skipwhite(arg + 9);
            continue;
         }
         if (STRNCMP(arg, "<script>", 8) == 0) {
            arg = skipwhite(arg + 8);
            continue;
         }
         if (STRNCMP(arg, "<expr>", 6) == 0) { 
            arg = skipwhite(arg + 6);
            continue;
         }
         break;
      }
      xp->input = mbText(arg);
   }

   return NULL;
}

//Find all mapping/abbreviation names that match regexp "regmatch"'.
//For command line expansion of ":[un]map" and ":[un]abbrev" in all modes.
//Return OK if matches found, FAIL otherwise.
pub int
expandMappings(
   CS pat,
   RegMatch* regmatch,
   OUT ExpandMatch* matches
) {
   MapBlock   *mp;
   int hash;
   CS p;
   int i;
   int score = 0;

   Boole doFuzzy = scrIsCommlineFuzzyCompletable(pat);

   validateMappingTable();
   Fuzzy fuzzy = {.cap = 0, .len = 0, .a = matches->a};
   
   Boole match;

   // First search in map modifier arguments
   for (i = 0; i < 7; ++i) {
      switch (i) {
      case 0: p = S"<silent>"; break;
      case 1: p = S"<unique>"; break;
      case 2: p = S"<script>"; break;
      case 3: p = S"<expr>"; break;
      case 4: p = S"<book>"; break;
      case 5: p = S"<nowait>"; break;
      case 6: p = S"<special>"; break;
      case 7: continue; 
      }

      if (doFuzzy) {
         score = fuzzyMatchStr(p, pat);
         match = (score != FUZZY_SCORE_NONE);
      } else {
         match = eeRegexec(regmatch, p, (ColNr)0);
      }

      if (!match)
         continue;

      if (doFuzzy) {
         addFuzzyMatch((FuzzyMatch){.str = copyStrA(p, matches->a), .score = score}, OUT &fuzzy);
      } else
         addExpandMatch(copyStrA(p, matches->a), OUT matches);
   }

   for (hash = 0; hash < 256; ++hash) {
      if (expand_isabbrev) {
         if (hash > 0)   // only one abbrev list
            break; // for (hash)
         mp = first_abbr;
      } ei (expand_buffer)
         mp = curBook->localMappings[hash];
      else
         mp = mappingTable[hash];
      for (; mp; mp = mp->next) {
         if (mp->simplified || !(mp->mode & expand_mapmodes))
            continue;

         p = translateMapping(mp->lhs);
         if (!p)
            continue;

         if (doFuzzy) {
            score = fuzzyMatchStr(p, pat);
            match = (score != FUZZY_SCORE_NONE);
         } else {
            match = eeRegexec(regmatch, p, (ColNr)0);
         }

         if (!match) {
            eeglFree(p);
            continue;
         }

         if (doFuzzy) {
            addFuzzyMatch((FuzzyMatch){.str = p, .score = score}, OUT &fuzzy);
         } else
            addExpandMatch(p, OUT matches);
      } // for (mp)
   } // for (hash)
   
   int count = matches->len + fuzzy.len;

   if (count)
      return FAIL;

   if (doFuzzy && defuzz(OUT matches, fuzzy, false) == FAIL)
      return FAIL;

   if (count > 1) {
      //Sort the matches. Fuzzy matching already sorts the matches
      if (!doFuzzy)
         sortStrings(matches->c, matches->len);

      //Remove multiple entries
      CS* ptr1 = matches->c;
      CS* ptr2 = ptr1 + 1;
      CS* ptr3 = matches->c + matches->len;

      //TODO write/find function that removes runs of duplicate strings in-place
      while (ptr2 < ptr3) {
         if (!eq(*ptr1, *ptr2)) {
            ptr1++;
            *ptr1 = *ptr2;
            ptr2++;
         } else {
            eeglFree(*ptr2++);
            count--;
         }
      }
   }

   return (matches->len == 0 ? FAIL : OK);
}

//Check for an abbreviation.
//Cursor is at ptr[col].
//When inserting, mincol is where insert started.
//For the command line, mincol is what is to be skipped over.
//"c" is the character typed before check_abbr was called.  It may have
//ABBR_OFF added to avoid prepending a CTRL-V to it.
//
//Historic vi practice: The last character of an abbreviation must be an id
//character ([a-zA-Z0-9_]). The characters in front of it must be all id
//characters or all non-id characters. This allows for abbr. "#i" to "#include".
//
//Eegl addition: Allow for abbreviations that end in a non-keyword character.
//Then there must be white space before the abbr.
//
//return true if there is an abbreviation, false if not
pub Boole
check_abbr(Unt c, CS ptr, int col, int mincol) {
   int len;
   int scol;      // starting column of the abbr.
   int j;
   CS s;
   Byte tb[MB_MAXBYTES + 4];
   MapBlock   *mp;
   MapBlock   *mp2;
   int      clen = 0;   // length in characters
   Boole is_id = true;
   Boole eeglAbbr;

   if (typeBufG.noAbbrCnt)   // abbrev. are not recursive
       return false;

   // no remapping implies no abbreviation, except for CTRL-]
   if (noremap_keys() && c != Ctrl_RSB)
       return false;

   //Check for word before the cursor: If it ends in a keyword char all
   //chars before it must be keyword chars or non-keyword chars, but not
   //white space. If it ends in a non-keyword char we accept any characters
   //before it except white space.
   if (col == 0)            // cannot be an abbr.
      return false;

   CS p = mb_prevptr(ptr, ptr + col);
   if (!eeIsWordPtr(p)) {
      eeglAbbr = true;         // Eegl added abbr.
   } else {
      eeglAbbr = false;        // vi compatible abbr.
      if (p > ptr)
         is_id = eeIsWordPtr(mb_prevptr(ptr, p));
   }
   clen = 1;
   while (p > ptr + mincol) {
       p = mb_prevptr(ptr, p);
       if (isSpace(*p) || (!eeglAbbr && is_id != eeIsWordPtr(p))) {
          p += utfCharLen(p);
          break;
       }
       ++clen;
   }
   scol = (int)(p - ptr);

   if (scol < mincol)
      scol = mincol;
   if (scol < col) {     // there is a word in front of the cursor
      ptr += scol;
      len = col - scol;
      mp = curBook->firstAbbr;
      mp2 = first_abbr;
      if (mp == NULL) {
         mp = mp2;
         mp2 = NULL;
      }
      for ( ; mp; mp->next == NULL ? (mp = mp2, mp2 = NULL) : (mp = mp->next)) {
          int      qlen = mp->keylen;
          CS q = mp->lhs;
          int      match;

          if (eeStrbyte(mp->lhs, K_SPECIAL) != NULL) {
             CS qe = copyStr(mp->lhs);

             // might have CSI escaped mp->lhs
             q = qe;
             eeUnescapeCsi(q);
             qlen = (int)STRLEN(q);
         }

         // find entries with right mode and keys
         match =  (mp->mode & stateG) && qlen == len && !STRNCMP(q, ptr, (Unt)len);
         if (q != mp->lhs)
            eeglFree(q);
         if (match)
            break;
      }
      if (mp) {
         Unt   noremap;
         int silent;
         int expr;

         // Found a match:
         // Insert the rest of the abbreviation in typeBufG.c[]. This goes from end to start.
         //
         // Characters 0x000 - 0x100: normal chars, may need CTRL-V,
         // except K_SPECIAL: Becomes K_SPECIAL KS_SPECIAL KE_FILLER
         // Characters where IS_SPECIAL() == true: key codes, need
         // K_SPECIAL. Other characters (with ABBR_OFF): don't use CTRL-V.
         //
         // Character CTRL-] is treated specially - it completes the abbreviation, but is not 
         // inserted into the input stream.
         j = 0;
         if (c != Ctrl_RSB) {
           // special key code, split up
           if (IS_SPECIAL(c) || c == K_SPECIAL) {
              tb[j++] = K_SPECIAL;
              tb[j++] = K_SECOND(c);
              tb[j++] = K_THIRD(c);
           } else {
               if (c < ABBR_OFF && (c < ' ' || c > '~'))
                  tb[j++] = Ctrl_V;   // special char needs CTRL-V
               int   newlen;

               // if ABBR_OFF has been added, remove it here
               if (c >= ABBR_OFF)
                  c -= ABBR_OFF;
               newlen = (*mb_char2bytes)(c, tb + j);
               tb[j + newlen] = ZERO;
               // Need to escape K_SPECIAL.
               CS escaped = copyStr_escape_csi(tb + j);
               newlen = (int)STRLEN(escaped);
               MEMMOVE(tb + j, escaped, newlen);
               j += newlen;
               eeglFree(escaped);
            }
            tb[j] = ZERO; // insert the last typed char
            (void)insertIntoTypebuf(tb, 1, 0, true, mp->silent);
         }

         // copy values here, calling eval_map_expr() may make "mp" invalid!
         noremap = mp->noremap;
         silent = mp->silent;
         expr = mp->expr;

         if (expr)
            s = eval_map_expr(mp, c);
         else
            s = mp->rhs;
         if (s != NULL) {
            // insert the to string
            (void)insertIntoTypebuf(s, noremap, 0, true, silent);
            // no abbrev. for these chars
            typeBufG.noAbbrCnt += (int)STRLEN(s) + j + 1;
            if (expr)
               eeglFree(s);
         }

         tb[0] = Ctrl_H;
         tb[1] = ZERO;
         len = clen;   // Delete characters instead of bytes
         while (len-- > 0)      // delete the from string
            (void)insertIntoTypebuf(tb, 1, 0, true, silent);
         return true;
      }
   }
   return false;
}

//Evaluate the RHS of a mapping or abbreviations and take care of escaping special characters.
//Careful: after this "mp" will be invalid if the mapping was deleted.
pub CS
eval_map_expr(MapBlock   *mp, int c) { // ZERO or typed character for abbreviation
   ScriptId   save_sctx_sid = scriptPosG.sid;

   // Remove escaping of CSI, because "str" is in a format to be used as typeahead.
   CS expr = copyStr(mp->rhs);
   eeUnescapeCsi(expr);

   // Forbid changing text or using ":normal" to avoid most of the bad side
   // effects.  Also restore the cursor position.
   ++textlock;
   ++ex_normal_lock;
   set_EeglVar_char(c);  // set v:char to the typed character
   Pos save_cursor = curPor->cursor;
   int saveMsgCol = msgColG;
   int msgRowSaved = msgRowG;

   // Note: the evaluation may make "mp" invalid.
   CS p = eval_to_string(expr, false, false);

   --textlock;
   --ex_normal_lock;
   curPor->cursor = save_cursor;
   msgColG = saveMsgCol;
   msgRowG = msgRowSaved;
   scriptPosG.sid = save_sctx_sid;

   eeglFree(expr);

   if (p == NULL)
      return NULL;
   // Escape CSI in the result to be able to use the string as typeahead.
   CS res = copyStr_escape_csi(p);
   eeglFree(p);

   return res;
}

//Copy "p" to allocated memory, escaping K_SPECIAL and CSI so that the result
//can be put in the typeahead buffer.
pub CS
copyStr_escape_csi(CS p) {
   //Need a buffer to hold up to 3 times as much. 4 in case of an illegal utf-8 byte:
   //0xc0 -> 0xc3 0x80 -> 0xc3 K_SPECIAL KS_SPECIAL KE_FILLER
   CS res = alloc(STRLEN(p) * 4 + 1);
   CS d = res;
   for (CS s = p; *s != ZERO; ) {
      if ((s[0] == K_SPECIAL) && s[1] != ZERO && s[2] != ZERO) {
         // Copy special key unmodified.
         *d++ = *s++;
         *d++ = *s++;
         *d++ = *s++;
      } else {
         // Add character, possibly multi-byte to destination, escaping
         // CSI and K_SPECIAL. Be careful, it can be an illegal byte!
         d = add_char2buf(mb_ptr2char(s), d);
         s += MB_CPTR2LEN(s);
      }
   }
   *d = ZERO;

   return res;
}

//Remove escaping from CSI and K_SPECIAL characters. Reverse of copyStr_escape_csi(). 
//Work in-place.
pub void
eeUnescapeCsi(CS p) {
   CS s = p;
   CS d = p;

   while (*s != ZERO) {
      if (s[0] == K_SPECIAL && s[1] == KS_SPECIAL && s[2] == KE_FILLER) {
         *d++ = K_SPECIAL;
         s += 3;
      } ei ((s[0] == K_SPECIAL || s[0] == CSI) && s[1] == KS_EXTRA && s[2] == (int)KE_CSI) {
         *d++ = CSI;
         s += 3;
      } else
         *d++ = *s++;
   }
   *d = ZERO;
}

//Write map commands for the current mappings to an .exrc file.
//Return FAIL on error, OK otherwise.
pub int
makemap(FILE* fd, NULLABLE Book* book) {     // the book for local mappings or NULL
   MapBlock* mp;
   Byte c1, c2, c3;
   CS p;
   char   *cmd;
   int      abbr;
   int      hash;

   validateMappingTable();

   // Do the loop twice: Once for mappings, once for abbreviations.
   // Then loop over all map hash lists.
   for (abbr = 0; abbr < 2; ++abbr) {
      for (hash = 0; hash < 256; ++hash) {
          if (abbr) {
             if (hash > 0)      // there is only one abbr list
                break;
             mp = book ? book->firstAbbr : first_abbr;
         } else {
            mp = book ? book->localMappings[hash] : mappingTable[hash];
         }

         for ( ; mp; mp = mp->next) {
            // skip script-local mappings
            if (mp->noremap == REMAP_SCRIPT)
               continue;

            // skip mappings that contain a <SNR> (script-local thing),
            // they probably don't work when loaded again
            for (p = mp->rhs; *p != ZERO; ++p)
               if (p[0] == K_SPECIAL && p[1] == KS_EXTRA && p[2] == (int)KE_SNR)
               break;
            if (*p != ZERO)
                continue;

           // It's possible to create a mapping and then ":unmap" certain
           // modes. We recreate this here by mapping the individual
           // modes, which requires up to three of them.
           c1 = ZERO;
           c2 = ZERO;
           c3 = ZERO;
           if (abbr)
              cmd = "abbr";
           else
              cmd = "map";
           switch (mp->mode) {
           case MODE_NORMAL | MODE_VISUAL | MODE_OP_PENDING:
              break;
           case MODE_NORMAL:
              c1 = 'n';
              break;
           case MODE_VISUAL:
              c1 = 'v';
              break;
           case MODE_OP_PENDING:
              c1 = 'o';
              break;
           case MODE_NORMAL | MODE_VISUAL:
              c1 = 'n';
              c2 = 'v';
              break;
            case MODE_NORMAL | MODE_OP_PENDING:
               c1 = 'n';
               c2 = 'o';
               break;
            case MODE_VISUAL | MODE_OP_PENDING:
               c1 = 'v';
               c2 = 'o';
               break;
            case MODE_COMMLINE | MODE_INSERT:
               if (!abbr)
                  cmd = "map!";
               break;
            case MODE_COMMLINE:
               c1 = 'c';
               break;
            case MODE_INSERT:
               c1 = 'i';
               break;
            case MODE_LANGMAP:
               c1 = 'l';
               break;
            case MODE_TERMINAL:
               c1 = 't';
               break;
            default:
               internalErrMsg(e_makemap_illegal_mode);
               return FAIL;
            }
            do {  // do this twice if c2 is set, 3 times with c3
               if (c1 && putc(c1, fd) < 0)
                  return FAIL;
               if (mp->noremap != REMAP_YES && fprintf(fd, "nore") < 0)
                  return FAIL;
               if (fputs(cmd, fd) < 0)
                  return FAIL;
               if (book && fputs(" <book>", fd) < 0)
                  return FAIL;
               if (mp->nowait && fputs(" <nowait>", fd) < 0)
                  return FAIL;
               if (mp->silent && fputs(" <silent>", fd) < 0)
                  return FAIL;
               if (mp->noremap == REMAP_SCRIPT && fputs("<script>", fd) < 0)
                  return FAIL;
               if (mp->expr && fputs(" <expr>", fd) < 0)
                  return FAIL;

               if (      putc(' ', fd) < 0
                      || put_escstr(fd, mp->lhs, 0) == FAIL
                      || putc(' ', fd) < 0
                      || put_escstr(fd, mp->rhs, 1) == FAIL
                      || put_eol(fd) < 0)
                  return FAIL;
                c1 = c2;
                c2 = c3;
                c3 = ZERO;
            } while (c1 != ZERO);
         }
      }
   }

   return OK;
}

//write escape string to file. "what": 0 for :map lhs, 1 for :map rhs, 2 for :set
//return FAIL for failure, OK otherwise
pub int
put_escstr(FILE* fd, CS strstart, int what) {
   CS str = strstart;
   Unt      c;
   int modifiers;

   // :map xx <Nop>
   if (*str == ZERO && what == 1) {
      if (fprintf(fd, "<Nop>") < 0)
         return FAIL;
      return OK;
   }

   for ( ; *str != ZERO; ++str) {
         // Check for a multi-byte character, which may contain escaped K_SPECIAL and CSI bytes
         CS p = mb_unescape(&str);
         if (p) {
            while (*p != ZERO) {
               if (fputc(*p++, fd) < 0)
                  return FAIL;
            }
            --str;
            continue;
         }

         c = *str;
         //Special key codes have to be translated to be able to make sense when they are read back.
         if (c == K_SPECIAL && what != 2) {
            modifiers = 0;
            if (str[1] == KS_MODIFIER) {
               modifiers = str[2];
               str += 3;

               // Modifiers can be applied too to multi-byte characters.
               p = mb_unescape(&str);

               if (p == NULL) {
                  c = *str;
               } else {
                  // retrieve codepoint (character number) from unescaped string
                  c = (*mb_ptr2char)(p);
                  --str;
               }
            }
            if (c == K_SPECIAL) {
               c = TO_SPECIAL(str[1], str[2]);
               str += 2;
            }
            if (IS_SPECIAL(c) || modifiers) {  // special key
               if (fputs((char *)get_special_key_name(c, modifiers), fd) < 0)
                  return FAIL;
               continue;
            }
         }

         // A '\n' in a map command should be written as <NL>.
         // A '\n' in a set command should be written as \^V^J.
         if (c == NL) {
           if (what == 2) {
               if (fprintf(fd, "\\\026\n") < 0)
                  return FAIL;
           } else {
              if (fprintf(fd, "<NL>") < 0)
                  return FAIL;
           }
           continue;
        }

        // Some characters have to be escaped with CTRL-V to
        // prevent them from misinterpreted in DoOneCmd().
        // A space, Tab and '"' has to be escaped with a backslash to
        // prevent it to be misinterpreted in do_set().
        // A space has to be escaped with a CTRL-V when it's at the start of a
        // ":map" rhs.
        // A '<' has to be escaped with a CTRL-V to prevent it being
        // interpreted as the start of a special key name.
        // A space in the lhs of a :map needs a CTRL-V.
        if (what == 2 && (SPACE_OR_TAB(c) || c == '"' || c == '\\')) {
            if (putc('\\', fd) < 0)
               return FAIL;
        } ei (c < ' ' || c > '~' || c == '|'
           || (what == 0 && c == ' ')
           || (what == 1 && str == strstart && c == ' ')
           || (what != 2 && c == '<')
        ){
           if (putc(Ctrl_V, fd) < 0)
              return FAIL;
        }
        if (putc(c, fd) < 0)
           return FAIL;
   }
   return OK;
}

// Check all mappings for the presence of special key codes. Used after ":set term=xxx".
pub void
check_map_keycodes(void) {
   MapBlock   *mp;
   CS p;
   int i;
   Byte buffer[3];
   int abbr;
   int hash;
   Book   *bp;
   ESTACK_CHECK_DECLARATION;

   validateMappingTable();
   // avoids giving error messages
   estack_push(ETYPE_INTERNAL, (CS)"mappings", 0);
   ESTACK_CHECK_SETUP;

   //Do this once for each book, and then once for global mappings/abbreviations with bp == NULL
   for (bp = firstBook; ; bp = bp->next) {
      // Do the loop twice: Once for mappings, once for abbreviations.
      // Then loop over all map hash lists.
      for (abbr = 0; abbr <= 1; ++abbr) {
          for (hash = 0; hash < 256; ++hash) {
               if (abbr) {
                   if (hash)       // there is only one abbr list
                     break;
                   if (bp != NULL)
                     mp = bp->firstAbbr;
                   else
                     mp = first_abbr;
               } else {
                   if (bp != NULL)
                     mp = bp->localMappings[hash];
                   else
                     mp = mappingTable[hash];
               }
               for ( ; mp != NULL; mp = mp->next) {
                   for (i = 0; i <= 1; ++i) {  // do this twice
                        if (i == 0)
                            p = mp->lhs;   // once for the "from" part
                        else
                            p = mp->rhs;   // and once for the "to" part
                        for (; *p; p++) {
                            if (*p == K_SPECIAL) {
                              ++p;
                              if (*p < 128) {  // for "normal" tcap entries
                                  buffer[0] = p[0];
                                  buffer[1] = p[1];
                                  buffer[2] = ZERO;
                                  (void)add_termcap_entry(buffer, false);
                              }
                              ++p;
                            }
                        }
                   }
               }
          }
      }
      if (bp == NULL)
         break;
   }
   ESTACK_CHECK_NOW;
   estack_pop();
}

//Check the string "keys" against the lhs of all mappings. Return pointer to rhs of mapping 
//(mapblock->m_str). NULL when no mapping found.
pub CS
norCheckMapping(
   CS keys,
   int mode,
   int exact,      // require exact match
   int ign_mod,   // ignore preceding modifier
   int abbr,      // do abbreviations
   OUT MapBlock** mp_ptr,   // return: pointer to mapblock or NULL
   OUT int* local_ptr   // return: buffer-local mapping or NULL
){
   int      hash;
   MapBlock   *mp;
   CS s;

   validateMappingTable();

   int len = (int)STRLEN(keys);
   for (int local = 1; local >= 0; --local) {
      // loop over all hash lists
      for (hash = 0; hash < 256; ++hash) {
         if (abbr) {
            if (hash > 0)      // there is only one list.
               break;
            if (local)
               mp = curBook->firstAbbr;
            else
               mp = first_abbr;
         } ei (local)
            mp = curBook->localMappings[hash];
         else
            mp = mappingTable[hash];
         for ( ; mp != NULL; mp = mp->next) {
            // skip entries with wrong mode, wrong length and not matching ones
            if ((mp->mode & mode) && (!exact || mp->keylen == len)) {
               int minlen;
               if (len > mp->keylen)
                  minlen = mp->keylen;
               else
                  minlen = len;
               s = mp->lhs;
               if (ign_mod && s[0] == K_SPECIAL && s[1] == KS_MODIFIER && s[2] != ZERO) {
                  s += 3;
                  if (len > mp->keylen - 3)
                      minlen = mp->keylen - 3;
               }
               if (STRNCMP(s, keys, minlen) == 0) {
                  if (mp_ptr != NULL)
                     *mp_ptr = mp;
                  if (local_ptr != NULL)
                     *local_ptr = local;
                  return mp->rhs;
               }
            }
         }
      }
   } 

   return NULL;
}

// "hasmapto()" function
pub void
f_hasmapto(Var* argvars, Var* returnVar) {
   CS mode;
   Byte buffer[NUMBUFLEN];
   int  abbr = false;

   CS name = tv_get_string(&argvars[0]);
   if (argvars[1].tag == VAR_UNKNOWN)
      mode = S"nvo";
   else {
      mode = tv_get_string_buf(&argvars[1], buffer);
      if (argvars[2].tag != VAR_UNKNOWN)
         abbr = (int)tv_get_bool(&argvars[2]);
   }

   if (map_to_exists(name, mode, abbr))
      returnVar->number = true;
   else
      returnVar->number = false;
}

// Fill in the empty dictionary with items as defined by maparg builtin.
private void
mapblock2dict(
   MapBlock* mp,
   Bag* bag,
   NULLABLE CS lhsrawalt,
   int bookLocal,   // false if not buffer local mapping
   int abbr       // true if abbreviation
){
   CS lhs = str2special_save(mp->lhs, true, false);
   CS mapmode = mapModeToChars(mp->mode);

   bagAddString(bag, S"lhs", lhs);
   eeglFree(lhs);
   bagAddString(bag, S"lhsraw", mp->lhs);
   if (lhsrawalt) {
      // Also add the value for the simplified entry.
      bagAddString(bag, S"lhsrawalt", lhsrawalt);
   }
   bagAddString(bag, S"rhs", mp->origRhs);
   bagAddNumber(bag, S"noremap", mp->noremap ? 1L : 0L);
   bagAddNumber(bag, S"script", mp->noremap == REMAP_SCRIPT ? 1L : 0L);
   bagAddNumber(bag, S"expr", mp->expr ? 1L : 0L);
   bagAddNumber(bag, S"silent", mp->silent ? 1L : 0L);
   bagAddNumber(bag, S"sid", (long)mp->scriptCtx.sid);
   bagAddNumber(bag, S"lnum", (long)mp->scriptCtx.lineNr);
   bagAddNumber(bag, S"buffer", (long)bookLocal);
   bagAddNumber(bag, S"nowait", mp->nowait ? 1L : 0L);
   bagAddString(bag, S"mode", mapmode);
   bagAddNumber(bag, S"abbr", abbr ? 1L : 0L);
   bagAddNumber(bag, S"mode_bits", mp->mode);

   eeglFree(mapmode);
}

private void
getMapArg(Var* argvars, Var* returnVar, int exact) {
   // return empty string for failure
   returnVar->tag = VAR_STRING;
   returnVar->string = NULL;

   CS keys = tv_get_string(&argvars[0]);
   if (*keys == ZERO)
      return;

   Byte buffer[NUMBUFLEN];
   Unt flags = REPTERM_FROM_PART | REPTERM_DO_LT;
   CS which;
   Boole abbr = false;
   int get_dict = false;
   if (argvars[1].tag != VAR_UNKNOWN) {
      which = convertVarToString(&argvars[1], buffer);
      if (argvars[2].tag != VAR_UNKNOWN) {
         abbr = (int)tv_get_bool(&argvars[2]);
         if (argvars[3].tag != VAR_UNKNOWN)
            get_dict = (int)tv_get_bool(&argvars[3]);
      }
   } else
      which = S"";
   if (!which)
      return;

   int mode = getMapMode(&which, 0);

   Boole didSimplify = false;
   CS keysBuffer = NULL;
   CS simplifiedKeys = 
      replace_termcodes(keys, &keysBuffer, 0, flags, OUT &didSimplify, false);
   int bookLocal;
   MapBlock* mp = NULL;
   CS rhs = norCheckMapping(simplifiedKeys, mode, exact, false, abbr, OUT &mp, OUT &bookLocal);
   CS alt_keysBuffer = NULL;
   if (didSimplify) {
      // When the lhs is being simplified the not-simplified keys are
      // preferred for printing, like in do_map().
      (void)replace_termcodes(keys, &alt_keysBuffer, 0, flags | REPTERM_NO_SIMPLIFY, NULL, false);
      rhs = norCheckMapping(alt_keysBuffer, mode, exact, false, abbr, &mp, &bookLocal);
   }

   if (!get_dict) {
      // Return a string.
      if (rhs) {
         if (*rhs == ZERO)
            returnVar->string = copyStr(S"<Nop>");
         else
            returnVar->string = str2special_save(rhs, false, false);
      }

   } else {
      allocReturnDict(returnVar);
      if (rhs) {
         mapblock2dict(
               mp, returnVar->bag, didSimplify ? simplifiedKeys : NULL, bookLocal, abbr
         );
      }
   } 

   eeglFree(keysBuffer);
   eeglFree(alt_keysBuffer);
}

pub void
f_maplist(Arr(Var) argvars, Var* returnVar) {
   Bag* d;
   MapBlock* mp;
   Unt const flags = REPTERM_FROM_PART | REPTERM_DO_LT;
   int abbr = false;

   if (argvars[0].tag != VAR_UNKNOWN)
      abbr = tv_get_bool(&argvars[0]);

   allocReturnList(returnVar);

   validateMappingTable();

   // Do it twice: once for global maps and once for local maps.
   for (int bookLocal = 0; bookLocal <= 1; ++bookLocal)  {
      for (Unt hash = 0; hash < 256; ++hash) {
         if (abbr) {
            if (hash > 0)      // there is only one abbr list
               break;
            if (bookLocal)
               mp = curBook->firstAbbr;
            else
               mp = first_abbr;
         } ei (bookLocal) {
            mp = curBook->localMappings[hash];
         } else
            mp = mappingTable[hash];
         for (; mp; mp = mp->next) {
            if (mp->simplified)
               continue;
            d = allocBag();
            if (listAppendBag(returnVar->list, d) == FAIL) {
               bagUnref(d);
               return;
            }

            CS keysBuffer = NULL;
            Boole didSimplify = false;

            CS lhs = str2special_save(mp->lhs, true, false);
            (void)replace_termcodes(lhs, &keysBuffer, 0, flags, OUT &didSimplify, false);
            eeglFree(lhs);

            mapblock2dict( mp, d, didSimplify ? keysBuffer : NULL, bookLocal, abbr);
            eeglFree(keysBuffer);
         }
      }
   }
}

pub void
f_maparg(Var* argvars, Var* returnVar) {
   getMapArg(argvars, returnVar, true);
}

pub void
f_mapcheck(Arr(Var) argvars, Var* returnVar) {
   getMapArg(argvars, returnVar, false);
}

//Get the mapping mode from the mode string.
//It may contain multiple characters, eg "nox", or "!", or ' '. Return 0 if there is an error
private int
getMapModeString(CS mode_string, int abbr) {
   CS p = mode_string;
   int mode = 0;
   int tmode;
   int modec;
   const int MASK_V = MODE_VISUAL;
   const int MASK_MAP = MODE_VISUAL | MODE_NORMAL | MODE_OP_PENDING;
   const int MASK_BANG = MODE_INSERT | MODE_COMMLINE;

   if (*p == ZERO)
      p = S" ";   // compatibility
   while ((modec = *p++)) {
      switch (modec) {
      case 'i': tmode = MODE_INSERT;   break;
      case 'l': tmode = MODE_LANGMAP;   break;
      case 'c': tmode = MODE_COMMLINE;   break;
      case 'n': tmode = MODE_NORMAL;   break;
      case 'x': tmode = MODE_VISUAL;   break;
      case 'o': tmode = MODE_OP_PENDING;   break;
      case 't': tmode = MODE_TERMINAL;   break;
      case 'v': tmode = MASK_V;      break;
      case '!': tmode = MASK_BANG;   break;
      case ' ': tmode = MASK_MAP;      break;
      default: return 0; // error, unknown mode character
      }
      mode |= tmode;
   }
   if ((abbr && (mode & ~MASK_BANG) != 0)
        || (!abbr && (mode & (mode-1)) != 0 // more than one bit set
               && (// false if multiple bits set in mode and mode is fully contained in one mask
                    !(((mode & MASK_BANG) != 0 && (mode & ~MASK_BANG) == 0)
                        || ((mode & MASK_MAP) != 0 && (mode & ~MASK_MAP) == 0))
                  )
           )
   ) {
      return 0;
   }

   return mode;
}

// "mapset()" function
pub void
f_mapset(Arr(Var) argvars, Var*) {
   CS which;
   Byte buffer[NUMBUFLEN];
   int is_abbr;
   Bag* bag;
   MapBlock** map_table = mappingTable;
   MapBlock** abbr_table = &first_abbr;
   CS arg;
   MapBlock* mp_result[2] = {NULL, NULL};
   // If first arg is a dict, then that's the only arg permitted.
   Boole bagOnly = argvars[0].tag == VAR_BAG;

   if (bagOnly) {
       bag = argvars[0].bag;
       which = bagGetString(bag, tConst("mode"), false);
       is_abbr = bagGetBool(bag, tConst("abbr"), false);
       if (which == NULL || is_abbr < 0) {
          emsg(_(e_entries_missing_in_mapset_dict_argument));
          return;
       }
   } else {
       which = convertVarToString(&argvars[0], buffer);
       if (!which)
          return;
       is_abbr = (int)tv_get_bool(&argvars[1]);

       if (check_for_dict_arg(argvars, 2) == FAIL)
          return;
       bag = argvars[2].bag;
   }
   int mode = getMapModeString(which, is_abbr);
   if (mode == 0) {
      showErrFmtMsg(_(e_illegal_map_mode_string_str), which);
      return;
   }

   // Get the values in the same order as above in getMapArg().
   CS lhs = bagGetString(bag, tConst("lhs"), false);
   CS lhsraw = bagGetString(bag, tConst("lhsraw"), false);
   CS lhsrawalt = bagGetString(bag, tConst("lhsrawalt"), false);
   CS rhs = bagGetString(bag, tConst("rhs"), false);
   if (!lhs || !lhsraw || !rhs) {
      emsg(_(e_entries_missing_in_mapset_dict_argument));
      return;
   }
   CS orig_rhs = rhs;

   Unt noremap = bagGetNumber(bag, tConst("noremap")) ? REMAP_NONE: 0;
   if (bagGetNumber(bag, tConst("script")) != 0)
      noremap = REMAP_SCRIPT;
   int expr = bagGetNumber(bag, tConst("expr")) != 0;
   int silent = bagGetNumber(bag, tConst("silent")) != 0;
   ScriptId sid = bagGetNumber(bag,tConst("sid"));
   LineNr lnum = bagGetNumber(bag, tConst("lnum"));
   int isBook = bagGetNumber(bag, tConst("buffer"));
   int nowait = bagGetNumber(bag, tConst("nowait")) != 0;
   // mode from the dict is not used

   CS arg_buf = NULL;
   if (caseInsensitiveCompare(rhs, "<nop>") == 0)   // "<Nop>" means nothing
      rhs = S"";
   else
      rhs = replace_termcodes(rhs, &arg_buf, sid, REPTERM_DO_LT | REPTERM_SPECIAL, NULL, false);

   if (isBook) {
      map_table = curBook->localMappings;
      abbr_table = &curBook->firstAbbr;
   }

   // Delete any existing mapping for this lhs and mode.
   if (isBook) {
       arg = alloc(STRLEN(lhs) + STRLEN("<book>") + 1);
       STRCPY(arg, "<book>");
       STRCPY(arg + 8, lhs);
   } else {
      arg = copyStr(lhs);
   }
   do_map(MAPTYPE_UNMAP_LHS, arg, mode, is_abbr);
   eeglFree(arg);

   mp_result[0] = addToMap(map_table, abbr_table, lhsraw, rhs, orig_rhs,
            noremap, nowait, silent, mode, is_abbr, expr, sid,
            lnum, 0
   );
   if (lhsrawalt)
      //log("ccc simpl map.c 2373 left [%s] rig [%s]", lhs, rhs);
      mp_result[1] = addToMap(map_table, abbr_table, lhsrawalt, rhs, orig_rhs,
            noremap, nowait, silent, mode, is_abbr, expr, sid,
            lnum, 1
      );

   if (mp_result[0] != NULL && mp_result[1] != NULL) {
      mp_result[0]->alt = mp_result[1];
      mp_result[1]->alt = mp_result[0];
   }

   eeglFree(arg_buf);
}

//Add a mapping "map" for mode "mode". When "nore" is true use MAPTYPE_NOREMAP.
//Need to put string in allocated memory, because do_map() will modify it.
pub void
add_map(CS map, int mode, int nore) {
   CS s = copyStr(map);
   (void)do_map(nore ? MAPTYPE_NOREMAP : MAPTYPE_MAP, s, mode, false);
   eeglFree(s);
}

//Any character has an equivalent 'langmap' character. This is used for keyboards that have a 
//special language mode that sends characters above 128 (although other characters can be 
//translated too). The "to" field is a Eegl command character.  This avoids having to switch the 
//keyboard back to ASCII mode when leaving Insert mode.
//
//langmap_mapchar[] maps any of 256 chars to an ASCII char used for Eegl commands.
//langmapTable.c is a sorted table of LangmapEntry. This does the
//same as langmap_mapchar[] for characters >= 256.
//
//Use arraylist for 'langmap' chars >= 256
typedef struct {
   int from;
   int to;
} LangmapEntry;

private ArrayList langmapTable;

//Search for an entry in "langmapTable" for "from".  If found set the "to"
//field.  If not found insert a new entry at the appropriate location.
private void
setEntry(int from, int to) {
   LangmapEntry *entries = (LangmapEntry *)(langmapTable.c);
   int a = 0;
   int b = langmapTable.len;

   // Do a binary search for an existing entry.
   while (a != b) {
      int i = (a + b) / 2;
      int d = entries[i].from - from;

      if (d == 0) {
         entries[i].to = to;
         return;
      }
      if (d < 0)
         a = i + 1;
      else
         b = i;
   }

   if (ga_grow(&langmapTable, 1) == FAIL)
      return;  // out of memory

   // insert new entry at position "a"
   entries = (LangmapEntry *)(langmapTable.c) + a;
   MEMMOVE(entries + 1, entries, (langmapTable.len - a) * sizeof(LangmapEntry));
   ++langmapTable.len;
   entries[0].from = from;
   entries[0].to = to;
}

// Apply @langmap' to multi-byte character "c" and return the result.
pub int
langmap_adjust_mb(int c) {
   LangmapEntry *entries = (LangmapEntry *)(langmapTable.c);
   int a = 0;
   int b = langmapTable.len;

   while (a != b) {
      int i = (a + b) / 2;
      int d = entries[i].from - c;

      if (d == 0)
         return entries[i].to;  // found matching entry
      if (d < 0)
         a = i + 1;
      else
         b = i;
   }
   return c;  // no entry found, return "c" unmodified
}

pub void
langmap_init(void) {
   for (int i = 0; i < 256; i++)
      langmap_mapchar[i] = i;    // we init with a one-to-one map
   ga_init2(&langmapTable, sizeof(LangmapEntry), 8);
}

// Called when langmap option is set; the language map can be changed at any time!
pub CS
setLangmap(OptionChange* args) {
   CS new = args->newVal.string;
   if (!new) {
      p_langmap = null;
      return null;
   }
   CS p2;

   for (CS p = new; p[0] != ZERO; ) {
      for (p2 = p; p2[0] != ZERO && p2[0] != ',' && p2[0] != ';'; MB_PTR_ADV(p2)) {
         if (p2[0] == '\\' && p2[1] != ZERO)
            ++p2;
      }
      if (p2[0] == ';')
         ++p2;       // abcd;ABCD form, p2 points to A
      else
         p2 = NULL;       // aAbBcCdD form, p2 is NULL
      while (p[0]) {
         if (p[0] == ',') {
            ++p;
            break;
         }
         if (p[0] == '\\' && p[1] != ZERO) {
            ++p;
         }
         Unt from = mb_ptr2char(p);
         Unt to = ZERO;
         if (!p2) {
             MB_PTR_ADV(p);
             if (p[0] != ',') {
                if (p[0] == '\\')
                   ++p;
                to = mb_ptr2char(p);
             }
         } else {
             if (p2[0] != ',') {
                if (p2[0] == '\\')
                   ++p2;
                to = mb_ptr2char(p2);
             }
         }
         if (to == ZERO) {
            return _(e_langmap_matching_character_missing_for_str);
            // TODO growable string 
            // template:   (char*)_(e_langmap_matching_character_missing_for_str),
            // value:  transchar(from)
         }

         if (from >= 256) {
            setEntry(from, to);
         } else {
            langmap_mapchar[from & 255] = to;
         }

         // Advance to next pair
         MB_PTR_ADV(p);
         if (p2) {
            MB_PTR_ADV(p2);
            if (*p == ';') {
               p = p2;
               if (p[0] != ZERO) {
                  if (p[0] != ',') {
                     // TODO growable string 
                     // template:   _(e_langmap_extra_characters_after_semicolon_str),
                     // value:  p
                     return _(e_langmap_extra_characters_after_semicolon_str);
                  }
                  ++p;
               }
               break;
            }
         }
      }
   }
   p_langmap = new;
   ga_clear(&langmapTable);          // clear the previous map first
   langmap_init();             // back to one-to-one map
   return NULL;
}

private void
mappingImpl(Invocation* invo, Boole isabbrev) {
   CS cmdp = invo->comm;
   int mode = getMapMode(&cmdp, invo->forceit || isabbrev);

   switch (do_map(
            *cmdp == 'n' 
               ? MAPTYPE_NOREMAP
               : (*cmdp == 'u' ? MAPTYPE_UNMAP : MAPTYPE_MAP), invo->arg, mode, isabbrev
           )
   ) {
   case 1: emsg(_(e_invalid_argument));
      break;
   case 2: emsg((isabbrev ? _(e_no_such_abbreviation) : _(e_no_such_mapping)));
      break;
   }
}

// ":abbreviate" and friends.
pub void
c_abbreviate(Invocation* invo) {
   mappingImpl(invo, true);   // almost the same as mapping
}

// ":map" and friends.
pub void
c_map(Invocation* invo) {
   mappingImpl(invo, false);
}

// ":unmap" and friends.
pub void
c_unmap(Invocation* invo) {
   mappingImpl(invo, false);
}

// ":mapclear" and friends.
pub void
c_mapclear(Invocation* invo) {
   mapClear(invo->comm, invo->arg, invo->forceit, false);
}

// ":abclear" and friends.
pub void
c_abclear(Invocation* invo) {
   mapClear(invo->comm, invo->arg, true, true);
}

//}}}
//{{{insert mode

#define BACKSPACE_CHAR           1
#define BACKSPACE_WORD           2
#define BACKSPACE_WORD_NOT_SPACE 3
#define BACKSPACE_LINE           4

//Set when doing something for completion that may call edit() recursively, which is not allowed.
private Boole isCompletionBusyS = false;

private ColNr insertStartG_textlen;   // length of line when insert started
private ColNr insertStartG_blank_vcol;   // vcol for first inserted blank
private Boole update_insertStartOrigS = true; // set insertStartOrigG to insertStartG

private Text lastInsertP = {null, 0}; //text of the previous insert, K_SPECIAL and CSI are escaped
private int last_insert_skip; // nr of chars in front of previous insert
private int new_insert_skip;  // nr of chars in front of current insert
private int did_restart_edit; // "restart_edit" when calling edit()

private int can_cindent; // may do cindenting on this line

private Boole needUndoS; // call u_save() before inserting a char. Set when edit() is called.
                             // after that, arrow_used is used.

private int   dont_sync_undo = false;   // CTRL-G U prevents syncing undo for
                                       // the next left/right cursor key
               
//{{{Editing. Actual input character handling in Insert mode

// Return the character immediately before the cursor.
private int
char_before_cursor(void) {
   if (curPor->cursor.col == 0)
      return -1;

   CS line = ml_get_curline();

   CS p = line + curPor->cursor.col;
   int prev_len = mb_head_off(line, p - 1) + 1;
   return mb_ptr2char(p - prev_len);
}

//Prepare for prompt mode: Make sure the last line has the prompt text.
//Move the cursor to this line.
pub void
init_prompt(int cmdchar_todo) {
   CS prompt = prompt_text();
   curPor->cursor.lnum = curBook->mem.lineCount;
   CS text = ml_get_curline();
   if (STRNCMP(text, prompt, STRLEN(prompt)) != 0) {
      // prompt is missing, insert it or append a line with it
      if (*text == ZERO)
         ml_replace(curBook->mem.lineCount, prompt, true);
      else
         ml_append(curBook->mem.lineCount, prompt, 0, false);
      curPor->cursor.lnum = curBook->mem.lineCount;
      coladvance((ColNr)MAXCOL);
      changed_bytes(curBook->mem.lineCount, 0);
   }

   // Insert always starts after the prompt, allow editing text after it.
   if (insertStartOrigG.lnum != curPor->cursor.lnum
               || insertStartOrigG.col != (int)STRLEN(prompt))
      set_insstart(curPor->cursor.lnum, (int)STRLEN(prompt));

   if (cmdchar_todo == 'A')
      coladvance((ColNr)MAXCOL);
   if (curPor->cursor.col < (int)STRLEN(prompt))
      curPor->cursor.col = (int)STRLEN(prompt);
   // Make sure the cursor is in a valid position.
   check_cursor();
}

//edit(): Start inserting text.
//
//"commChar" can be:
//'i'   normal insert command
//'a'   normal append command
//K_PS bracketed paste
//'r'   "r<CR>" command: insert one <CR>.  Note: count can be > 1, for redo,
//  but still only one <CR> is inserted.  The <Esc> is not used for redo.
//'g'   "gI" command.
//
//This function is not called recursively.  For CTRL-O commands, it returns
//and lets the caller handle the Normal-mode command.
//
//Return true if a CTRL-O command caused the return (insert mode pending).
pub int
edit(Unt commChar, int startln, long count){
                   // if set, insert at start of line
   Unt c = 0;
   CS ptr;
   int lastc = 0;
   int mincol;
   static LineNr o_lnum = 0;
   int i;
   int did_backspace = true;       // previous char was backspace
   LineNr   old_topline = 0;       // topline before insertion
   int old_topfill = -1;
   int inserted_space = false;     // just inserted a space
   int nomove = false;          // don't move cursor on return
   int commChar_todo = commChar;

   // Remember whether editing was restarted after CTRL-O.
   did_restart_edit = restart_edit;

   // sleep before redrawing, needed for "CTRL-O :" that results in an error message
   check_for_delay(true);

   // set insertStartOrigG to insertStartG
   update_insertStartOrigS = true;

   // Don't allow changes in the book while editing the commline.  The
   // caller of getCommline() may get confused.
   // Don't allow recursive insert mode when busy with completion.
   if (textlock != 0 || ins_compl_active() || isCompletionBusyS || pum_visible()) {
      emsg(_(e_not_allowed_to_change_text_or_change_portal));
      return false;
   }
   ins_compl_clear();       // clear stuff for CTRL-X mode

   // Trigger InsertEnter autocommands.  Do not do this for "r<CR>" or "grx".
   if (commChar != 'r' && commChar != 'v') {
      Pos save_cursor = curPor->cursor;

      if (commChar == 'R')
         ptr = S"r";
      ei (commChar == 'V')
         ptr = S"v";
      else
         ptr = S"i";
      set_EeglVar_string(VV_INSERTMODE, ptr, 1);
      set_EeglVar_string(VV_CHAR, NULL, -1);  // clear v:char
      ins_applyAutocomms(EVENT_INSERTENTER);


      //Make sure the cursor didn't move. Do call check_cursor_col() in case the text was modified.
      //Since Insert mode was not started yet a call to check_cursor_col() may move the cursor, 
      //especially with the "A" command, thus set stateG to avoid that. Also check that the
      //line number is still valid (lines may have been deleted).
      //Do not restore if v:char was set to a non-empty string.
      if (!EQUAL_POS(curPor->cursor, save_cursor)
         && *get_EeglVar_str(VV_CHAR) == ZERO
         && save_cursor.lnum <= curBook->mem.lineCount
      ) {
         int saveState = stateG;

         curPor->cursor = save_cursor;
         stateG = MODE_INSERT;
         check_cursor_col();
         stateG = saveState;
      }
   }

   // When doing a paste with the middle mouse button, insertStartG is set to where the paste started.
   if (where_paste_started.lnum != 0)
      insertStartG = where_paste_started;
   else {
      insertStartG = curPor->cursor;
      if (startln)
         insertStartG.col = 0;
   }
   insertStartG_textlen = (ColNr)linetabsize_str(ml_get_curline());
   insertStartG_blank_vcol = MAXCOL;
   if (!didAindentG)
      ai_col = 0;

   if (commChar != ZERO && restart_edit == 0) {
      ResetRedobuff();
      inpAppendNumberToRedoBuff(count);
      if (commChar == 'V' || commChar == 'v') {
         // "gR" or "gr" command
         AppendCharToRedobuff('g');
         AppendCharToRedobuff((commChar == 'v') ? 'r' : 'R');
      } else {
         if (commChar == K_PS)
            AppendCharToRedobuff('a');
         else
            AppendCharToRedobuff(commChar);
         if (commChar == 'g')          // "gI" command
            AppendCharToRedobuff('I');
         ei (commChar == 'r')       // "r<CR>" command
            count = 1;          // insert only one <CR>
      }
   }

   stateG = MODE_INSERT;

   may_trigger_modechanged();
   stop_insert_mode = false;

   // Need to position cursor again when on a TAB and when on a char with virtual text.
   if (gchar_cursor() == TAB || curBook->hasTextprop)
      curPor->cacheState &= ~(VALID_WROW|VALID_WCOL|VALID_VIRTCOL);

   //Enable langmap or IME, indicated by 'iminsert'.
   //Note that IME may enabled/disabled without us noticing here, thus the
   //'iminsert' value may not reflect what is actually used. It is updated when hitting <Esc>.
   if (curBook->o.b_p_iminsert == B_IMODE_LMAP)
      stateG |= MODE_LANGMAP;

   setmouse();
   clear_showcmd();

   //Handle restarting Insert mode. Don't do this for "CTRL-O ." (repeat an insert): In 
   //that case we get here with something in the stuff buffer.
   if (restart_edit != 0 && stuff_empty()) {
      //After a paste we consider text typed to be part of the insert for
      //the pasted text. You can backspace over the pasted text too.
      if (where_paste_started.lnum)
         arrow_used = false;
      else
         arrow_used = true;
      restart_edit = 0;

      //If the cursor was after the end-of-line before the CTRL-O and it is now at the end-of-line,
      //put it after the end-of-line (this is not correct in very rare cases).
      //Also do this if curswant is greater than the current virtual
      //column.  Eg after "^O$" or "^O80|".
      validate_virtcol();
      update_curswant();
      if (((ins_at_eol && curPor->cursor.lnum == o_lnum)
             || curPor->cursWant > curPor->virtCol)
            && *(ptr = ml_get_curline() + curPor->cursor.col) != ZERO) {
         if (ptr[1] == ZERO)
            ++curPor->cursor.col;
         else {
            i = utfCharLen(ptr);
            if (ptr[i] == ZERO)
               curPor->cursor.col += i;
         } 
      }
      ins_at_eol = false;
   } else
      arrow_used = false;


   // Need to save the line for undo before inserting the first char.
   needUndoS = true;

   where_paste_started.lnum = 0;
   can_cindent = true;
   // The cursor line is not in a closed fold
   if (did_restart_edit == 0)
      foldOpenCursor();

   //If 'showmode' is set, show the current (insert/replace/..) mode.
   //A warning message for changing a readonly file is given here, before
   //actually changing anything.  It's put after the mode, if any.
   i = 0;
   if (p_smd && msg_silent == 0)
      i = showmode();

   if (did_restart_edit == 0)
      change_warning(i == 0 ? 0 : i + 1);

    ui_cursor_shape();      // may show different cursor shape

   //Get the current length of the redo buffer, those characters have to be
   //skipped if we want to get to the inserted characters.
   Text inserted = get_inserted();
   new_insert_skip = (int)inserted.len;
   if (inserted.c != NULL)
      eeglFree(inserted.c);

   old_indent = 0;

   // Main loop in Insert mode: repeat until Insert mode is left.
   for (;;) {
      if (arrow_used)       // don't repeat insert when arrow key used
         count = 0;

      if (update_insertStartOrigS)
         insertStartOrigG = insertStartG;

      if (stop_insert_mode && !ins_compl_active()) {
         // ":stopinsert" used or 'insertmode' reset
         count = 0;
         goto doESCkey;
      }

      // set curPor->cursWant for next K_DOWN or K_UP
      if (!arrow_used)
         curPor->setCursWant = true;

      // If there is no typeahead may check for timestamps (e.g., for when a
      // menu invoked a shell command).
      if (stuff_empty()) {
         did_check_timestamps = false;
         if (need_check_timestamps)
            check_timestamps(false);
      }

      // When emsg() was called msg_scroll will have been set.
      msg_scroll = false;

      // Open fold at the cursor line, according to 'foldopen'.
      if (p_fdo & FDO_INSERT)
         foldOpenCursor();
      // Close folds where the cursor isn't, according to 'foldclose'
      if (!char_avail())
         foldCheckClose();

      if (bt_prompt(curBook)) {
         init_prompt(commChar_todo);
         commChar_todo = ZERO;
      }

      //If we inserted a character at the last position of the last line in the portal, scroll 
      //the portal one line up. This avoids an extra redraw.
      //This is detected when the cursor column is smaller after inserting something.
      //Don't do this when the topline changed already, it has already been adjusted 
      //(by insertchar() calling openLine())). Also don't do this when @smoothscroll is set, as 
      //the portal should then be scrolled by screen lines.
      if (curBook->needsRedraw
            && curPor->o.wrap
            && !curPor->o.smoothScroll
            && !did_backspace
            && curPor->topLine == old_topline
            && curPor->topFill == old_topfill
            && count <= 1
      ){
         mincol = curPor->cursorCol;
         validate_cursor_col();

         if (
            (int)curPor->cursorCol < mincol - curBook->o.shiftWidth
             && curPor->cursorRow == curPor->height - 1 - curPor->o.scrollOff
             && (curPor->cursor.lnum != curPor->topLine || curPor->topFill > 0)
         ) {
            if (curPor->topFill > 0)
               --curPor->topFill;
            ei (getFolds(curPor->topLine, NULL, OUT &old_topline))
               set_topline(curPor, old_topline + 1);
            else
               set_topline(curPor, curPor->topLine + 1);
         }
      }

      // May need to adjust topLine to show the cursor.
      if (count <= 1)
         update_topline();

      did_backspace = false;

      if (count <= 1)
         validate_cursor();      // may set mustRedrawG

      //Redraw the display when no characters are waiting.
      //Also shows mode, ruler and positions cursor.
      redrawInInsertMode(true);

      if (curPor->o.diff) {
         normPostProcessScrollbind(true);
         do_check_cursorbind();
      } 
      if (count <= 1)
         update_curswant();
      old_topline = curPor->topLine;
      old_topfill = curPor->topFill;

      // May request the keyboard protocol state now.
      may_send_t_RK();

      //Get a character for Insert mode.  Ignore K_IGNORE and K_NOP.
      if (c != K_CURSORHOLD)
         lastc = c;      // remember the previous char for CTRL-D

      // After using CTRL-G U the next cursor key will not break undo.
      if (dont_sync_undo == MAYBE)
         dont_sync_undo = true;
      else
         dont_sync_undo = false;
      if (commChar == K_PS)
         // Got here from normal mode when bracketed paste started.
         c = K_PS;
      else {
         do {
            c = safe_vgetc();

            if (stop_insert_mode || (c == K_IGNORE && term_use_loop())) {
               // Insert mode ended, possibly from a callback, or a timer
               // must have opened a terminal portal.
               if (c != K_IGNORE && c != K_NOP)
                  vungetc(c);
               count = 0;

               if (!bt_prompt(curPor->book) && !bt_terminal(curPor->book) && stop_insert_mode)
                  // :stopinsert command via callback or via server command
                  nomove = false;
               else
                  nomove = true;
               ins_compl_prep(ESC);
               goto doESCkey;
            }
         } while (c == K_IGNORE || c == K_NOP);
      } 

      // Don't want K_CURSORHOLD for the second key, e.g., after CTRL-V.
      did_cursorhold = true;

      // If the window was made so small that nothing shows, make it at least
      // one line and one column when typing.
      if (keyWasTypedG && !keyWasStuffedG)
         portEnsureSize();

      //Special handling of keys while the popup menu is visible or wanted and the cursor is still
      //in the completed word.  Only when there is a match, skip this when no matches were found.
      if (ins_compl_active() && curPor->cursor.col >= ins_compl_col()
            && ins_compl_has_shown_match() && pum_wanted()
      ) {
         // BS: Delete one character from "compl_leader".
         if ((c == K_BS || c == Ctrl_H)
               && curPor->cursor.col > ins_compl_col()
               && (c = ins_compl_bs()) == ZERO)
            continue;

         // When no match was selected or it was edited.
         if (!ins_compl_used_match()) {
            // CTRL-L: Add one character from the current match to
            // "compl_leader".  Except when at the original match and
            // there is nothing to add, CTRL-L works like CTRL-P then.
            if (c == Ctrl_L && (!ctrl_x_mode_line_or_eval() || ins_compl_long_shown_match())) {
               ins_compl_addfrommatch();
               continue;
            }

            // A non-white character that fits in with the current completion: add to "compl_leader"
            if (ins_compl_accept_char(c)) {
               ins_compl_addleader(c);
               continue;
            }

            // Pressing CTRL-Y selects the current match.  When
            // ins_compl_enter_selects() is set the Enter key does the same.
            if ((c == Ctrl_Y || (ins_compl_enter_selects()
                      && (c == ENTER || c == K_KENTER || c == NL)))
               && stop_arrow() == OK
            ){
               ins_compl_delete();
               ins_compl_insert(false);
            }
            // Delete preinserted text when typing special chars
            ei (IS_WHITE_NL_OR_ZERO(c) && ins_compl_preinsert_effect())
               ins_compl_delete();
          }
      }

      //Prepare for or stop CTRL-X mode.  This doesn't do completion, but
      //it does fix up the text when finishing completion.
      ins_compl_init_get_longest();
      if (ins_compl_prep(c))
         continue;

      // CTRL-\ CTRL-N goes to Normal mode,
      // CTRL-\ CTRL-G goes to mode selected with 'insertmode',
      // CTRL-\ CTRL-O is like CTRL-O but without moving the cursor.
      if (c == Ctrl_BSL) {
         // may need to redraw when no more chars available now
         redrawInInsertMode(false);
         ++no_mapping;
         ++allow_keys;
         c = plain_vgetc();
         --no_mapping;
         --allow_keys;
         if (c != Ctrl_N && c != Ctrl_G && c != Ctrl_O) {
            // it's something else
            vungetc(c);
            c = Ctrl_BSL;
         } else {
            if (c == Ctrl_O) {
               ins_ctrl_o();
               ins_at_eol = false;   // cursor keeps its column
               nomove = true;
            }
            count = 0;
            goto doESCkey;
          }
      }

      if ((c == Ctrl_V || c == Ctrl_Q) && ctrl_x_mode_cmdline())
         goto docomplete;
      if (c == Ctrl_V || c == Ctrl_Q) {
         insertStartVisualBlockMode();
         c = Ctrl_V;   // pretend CTRL-V is last typed character
         continue;
      }

      //If @keymodel contains "startsel", may start selection.  If it
      //does, a CTRL-O and c will be stuffed, we need to get these characters.
      if (ins_start_select(c))
         continue;

      //The big switch to handle a character in insert mode.
      switch (c) {
      case ESC:   // End input mode
         if (echeck_abbr(ESC + ABBR_OFF))
            break;
         // FALLTHROUGH

      case Ctrl_C:   // End input mode
         if (c == Ctrl_C && commPortTypeG != 0) {
            // Close the cmdline window.
            commPortResultG = K_IGNORE;
            gotInterruptG = false; // don't stop executing autocommands et al.
            nomove = true;
            goto doESCkey;
         }
         if (c == Ctrl_C && bt_prompt(curBook) && invoke_prompt_interrupt()) {
            if (!bt_prompt(curBook))
               // book changed to a non-prompt book, get out of Insert mode
               goto doESCkey;
            break;
         }

   do_intr:
   doESCkey:
         //This is the ONLY return from edit()!
         //Always update o_lnum, so that a "CTRL-O ." that adds a line
         //still puts the cursor back after the inserted text.
         if (ins_at_eol && gchar_cursor() == ZERO)
            o_lnum = curPor->cursor.lnum;

         if (ins_esc(&count, commChar, nomove)) {
            // When CTRL-C was typed gotInterruptG will be set, with the result
            // that the autocommands won't be executed. When mapped gotInterruptG
            // is not set, but let's keep the behavior the same.
            if (commChar != 'r' && commChar != 'v' && c != Ctrl_C)
                ins_applyAutocomms(EVENT_INSERTLEAVE);
            did_cursorhold = false;

            if (!char_avail() && curBook->lastChangeTickInsert == CHANGEDTICK(curBook))
                curBook->lastChangeTick = CHANGEDTICK(curBook);
            return (c == Ctrl_O);
         }
         continue;

      case Ctrl_Z:   // suspend when 'insertmode' set
         goto normalchar;   // insert CTRL-Z as normal char

      case Ctrl_O:   // execute one command
         if (ctrl_x_mode_omni())
            goto docomplete;
         if (echeck_abbr(Ctrl_O + ABBR_OFF))
            break;
         ins_ctrl_o();

         count = 0;
         goto doESCkey;

      case K_HELP:   // Help key works like <ESC> <Help>
      case K_F1:
      case K_XF1:
         stuffcharReadbuff(K_HELP);
         goto doESCkey;

      case K_ZERO:   // Insert the previously inserted text.
      case ZERO:
      case Ctrl_A:
         // For ^@ the trailing ESC will end the insert, unless there is an error.
         if (stuff_inserted(ZERO, 1L, (c == Ctrl_A)) == FAIL && c != Ctrl_A)
            goto doESCkey;      // quit insert mode
         inserted_space = false;
         break;

      case Ctrl_R:   // insert the contents of a register
         if (ctrl_x_mode_register() && !ins_compl_active())
            goto docomplete;
         insertRegisterContents();
         auto_format(false, true);
         inserted_space = false;
         break;

      case Ctrl_G:   // commands starting with CTRL-G
         ins_ctrl_g();
         break;

      case Ctrl_HAT:   // switch input mode and/or langmap
         ins_ctrl_hat();
         break;


      case Ctrl_D:   // Make indent one shiftwidth smaller.
         if (ctrl_x_mode_path_defines())
            goto docomplete;
         // FALLTHROUGH

      case Ctrl_T:   // Make indent one shiftwidth greater.
         if (c == Ctrl_T && ctrl_x_mode_thesaurus()) {
            if (has_compl_option(false))
               goto docomplete;
            break;
         }

         ins_shift(c, lastc);
         auto_format(false, true);
         inserted_space = false;
         break;

      case K_DEL:   // delete character under the cursor
      case K_KDEL:
         ins_del();
         auto_format(false, true);
         break;

      case K_BS:   // delete character before the cursor
      case K_S_BS:
      case Ctrl_H:
         did_backspace = ins_bs(c, BACKSPACE_CHAR, &inserted_space);
         auto_format(false, true);
         if (did_backspace && p_ac && !char_avail() && curPor->cursor.col > 0) {
            c = char_before_cursor();
            if (ins_compl_setup_autocompl(c)) {
                drawUpdateScreen(UPD_VALID); // Show char deletion immediately
                out_flush();
                goto docomplete; // Trigger autocompletion
            }
         }
         break;

      case Ctrl_W:   // delete word before the cursor
         if (bt_prompt(curBook) && (modMaskG & MOD_MASK_SHIFT) == 0) {
            // In a prompt window CTRL-W is used for window commands.
            // Use Shift-CTRL-W to delete a word.
            stuffcharReadbuff(Ctrl_W);
            restart_edit = 'A';
            nomove = true;
            count = 0;
            goto doESCkey;
         }
         did_backspace = ins_bs(c, BACKSPACE_WORD, &inserted_space);
         auto_format(false, true);
         break;

      case Ctrl_U:   // delete all inserted text in current line
         // CTRL-X CTRL-U completes with 'completefunc'.
         if (ctrl_x_mode_function())
            goto docomplete;
         did_backspace = ins_bs(c, BACKSPACE_LINE, &inserted_space);
         auto_format(false, true);
         inserted_space = false;
         break;

      case K_LEFTMOUSE:   // mouse keys
      case K_LEFTMOUSE_NM:
      case K_LEFTDRAG:
      case K_LEFTRELEASE:
      case K_LEFTRELEASE_NM:
      case K_MOUSEMOVE:
      case K_MIDDLEMOUSE:
      case K_MIDDLEDRAG:
      case K_MIDDLERELEASE:
      case K_RIGHTMOUSE:
      case K_RIGHTDRAG:
      case K_RIGHTRELEASE:
      case K_X1MOUSE:
      case K_X1DRAG:
      case K_X1RELEASE:
      case K_X2MOUSE:
      case K_X2DRAG:
      case K_X2RELEASE:
         ins_mouse(c);
         break;

      case K_MOUSEDOWN: // Default action for scroll wheel up: scroll up
         ins_mousescroll(MSCR_DOWN);
         break;

      case K_MOUSEUP:   // Default action for scroll wheel down: scroll down
         ins_mousescroll(MSCR_UP);
         break;

      case K_MOUSELEFT: // Scroll wheel left
         ins_mousescroll(MSCR_LEFT);
         break;

      case K_MOUSERIGHT: // Scroll wheel right
         ins_mousescroll(MSCR_RIGHT);
         break;

      case K_PS:
         bracketed_paste(PASTE_INSERT, false, NULL);
         if (commChar == K_PS)
            // invoked from normal mode, bail out
            goto doESCkey;
         break;
      case K_PE:
         // Got K_PE without K_PS, ignore.
         break;

      case K_IGNORE:   // Something mapped to nothing
         break;

      case K_COMMAND:          // <Cmd>command<CR>
      case K_SCRIPT_COMMAND: {      // <ScriptCmd>command<CR>
         do_cmdkey_command(c, 0);

         if (term_use_loop())
            // Started a terminal that gets the input, exit Insert mode.
            goto doESCkey;
         if (curBook->undo.synced)
            // The command caused undo to be synced.  Need to save the
            // line for undo before inserting the next char.
            needUndoS = true;
         }
         break;

      case K_CURSORHOLD:   // Didn't type something for a while.
         ins_applyAutocomms(EVENT_CURSORHOLDI);
         did_cursorhold = true;
         // If CTRL-G U was used apply it to the next typed key.
         if (dont_sync_undo == true)
            dont_sync_undo = MAYBE;
         break;

      case K_HOME:   // <Home>
      case K_KHOME:
      case K_S_HOME:
      case K_C_HOME:
         ins_home(c);
         break;

      case K_END:   // <End>
      case K_KEND:
      case K_S_END:
      case K_C_END:
         ins_end(c);
         break;

      case K_LEFT:   // <Left>
         if (modMaskG & (MOD_MASK_SHIFT|MOD_MASK_CTRL))
            ins_s_left();
         else
            ins_left();
         break;

      case K_S_LEFT:   // <S-Left>
      case K_C_LEFT:
         ins_s_left();
         break;

      case K_RIGHT:   // <Right>
         if (modMaskG & (MOD_MASK_SHIFT|MOD_MASK_CTRL))
            ins_s_right();
         else
            ins_right();
         break;

      case K_S_RIGHT:   // <S-Right>
      case K_C_RIGHT:
          ins_s_right();
          break;

      case K_UP:   // <Up>
         if (pum_visible())
            goto docomplete;
         if (modMaskG & MOD_MASK_SHIFT)
            ins_pageup();
         else
            ins_up(false);
         break;

      case K_S_UP:   // <S-Up>
      case K_PAGEUP:
      case K_KPAGEUP:
         if (pum_visible())
            goto docomplete;
         ins_pageup();
         break;

      case K_DOWN:   // <Down>
         if (pum_visible())
            goto docomplete;
         if (modMaskG & MOD_MASK_SHIFT)
            ins_pagedown();
         else
            ins_down(false);
         break;

      case K_S_DOWN:   // <S-Down>
      case K_PAGEDOWN:
      case K_KPAGEDOWN:
         if (pum_visible())
            goto docomplete;
         ins_pagedown();
         break;

      case K_DROP:   // drag-n-drop event
         ins_drop();
         break;

      case K_S_TAB:   // When not mapped, use like a normal TAB
         c = TAB;
         // FALLTHROUGH

      case TAB:   // TAB or Complete patterns along path
         if (modMaskG & (MOD_MASK_SHIFT|MOD_MASK_CTRL)) {
            if (ins_tab())
               goto normalchar;   // insert TAB as a normal char
         } ei (ctrl_x_mode_path_patterns()) 
            goto docomplete;
         else {
            // go to normal mode
            goto doESCkey;
         }
         
         
         inserted_space = false;
         auto_format(false, true);
         break;

      case K_KENTER:   // <Enter>
         c = ENTER;
         // FALLTHROUGH
      case ENTER:
      case NL:
         // In a quickfix window a <CR> jumps to the error under the cursor.
         if (isLocationListBook(curBook) && c == ENTER) {
            if (curPor->locationStackRef == NULL)    // quickfix window
               executeCommLine(S".mc");
            else                // location list portal
               executeCommLine(S".ll");
            break;
         }
         if (commPortTypeG != 0) {
            // Execute the command in the commline portal
            commPortResultG = ENTER;
            goto doESCkey;
         }
         if (bt_prompt(curBook)) {
            invoke_prompt_callback();
            if (!bt_prompt(curBook))
               // book changed to a non-prompt book, get out of Insert mode
               goto doESCkey;
            break;
         }
         if (ins_eol(c) == FAIL)
            goto doESCkey;       // out of memory
         auto_format(false, false);
         inserted_space = false;
         break;

      case Ctrl_K:       // digraph or keyword completion
         if (ctrl_x_mode_dictionary()) {
            if (has_compl_option(true))
               goto docomplete;
            break;
         }
         goto normalchar;

      case Ctrl_X:   // Enter CTRL-X mode
          ins_ctrl_x();
          break;

      case Ctrl_RSB:   // Tag name completion after ^X
         if (!ctrl_x_mode_tags())
            goto normalchar;
         goto docomplete;

      case Ctrl_F:   // File name completion after ^X
         if (!ctrl_x_mode_files())
            goto normalchar;
         goto docomplete;
      case Ctrl_L:   // Whole line completion after ^X
         if (!ctrl_x_mode_whole_line()) {
            goto normalchar;
         }
         // FALLTHROUGH

      case Ctrl_P:   // Do previous/next pattern completion
      case Ctrl_N:
         //if @complete is empty then plain ^P is no longer special, but it is under other ^X modes
         if (!curBook->o.complete
                && (ctrl_x_mode_normal() || ctrl_x_mode_whole_line())
                && !compl_status_local()
         )
            goto normalchar;

   docomplete:
         isCompletionBusyS = true;
         disable_fold_update++;  // don't redraw folds here
         if (ins_complete(c, true) == FAIL)
            compl_status_clear();
         disable_fold_update--;
         isCompletionBusyS = false;
         can_si = may_do_si(); // allow smartindenting
         break;

      case Ctrl_Y:   // copy from previous line or scroll down
      case Ctrl_E:   // copy from next line      or scroll up
         c = ins_ctrl_ey(c);
         break;

      default:
         if (c == extraInterruptCharG)      // special interrupt char
            goto do_intr;

   normalchar:
         //Insert a normal character. If the new value is already inserted or an empty string
         // then don't insert any character.
         if (c == ZERO)
             break;
         // Try to perform smart-indenting.
         doTrySmartIndent(c);

         if (c == ' ') {
            inserted_space = true;
         if (inindent(0))
            can_cindent = false;
         if (insertStartG_blank_vcol == MAXCOL && curPor->cursor.lnum == insertStartG.lnum)
            insertStartG_blank_vcol = get_nolist_virtcol();
         }

         //Insert a normal character and check for abbreviations on a
         //special character.  Let CTRL-] expand abbreviations without inserting it.
         if (eeIsWordc(c) 
               || (!echeck_abbr((c >= 0x100) ? (c + ABBR_OFF) : c)
                  // Add ABBR_OFF for characters above 0x100, this is what check_abbr() expects.
                  && c != Ctrl_RSB)
         ) {
            insertRegular(c, false, false);
         }

         auto_format(false, true);

         // When inserting a character the cursor line must never be in a closed fold.
         foldOpenCursor();
         // Trigger autocompletion
         if (p_ac && !char_avail() && ins_compl_setup_autocompl(c)) {
            drawUpdateScreen(UPD_VALID); // Show character immediately
            out_flush();
            goto docomplete;
         }

         break;
      }   // end of switch (c)

      // If typed something may trigger CursorHoldI again.
      if (c != K_CURSORHOLD 
         // but not in CTRL-X mode, a script can't restore the state
         && ctrl_x_mode_normal()
      ) {
         did_cursorhold = false;
      } 

      // Check if we need to cancel completion mode because the portal or tab was changed
      if (ins_compl_active() && !ins_compl_win_active(curPor))
         ins_compl_cancel();

      // If the cursor was moved we didn't just insert a space
      if (arrow_used)
         inserted_space = false;

   }   // for (;;)
   // NOTREACHED
}

//Redraw for Insert mode. This is postponed until getting the next character to make '$' in the 
//'cpo' option work correctly. Only redraw when there are no characters available. This speeds up
//inserting sequences of characters (e.g., for CTRL-R).
private void
redrawInInsertMode(Boole ready) {      // not busy with something
   if (char_avail())
      return;

   // Trigger CursorMoved if the cursor moved.  Not when the popup menu is
   // visible, the command might delete it.
   if (ready && popup_visible && !EQUAL_POS(last_cursormoved, curPor->cursor) && !pum_visible()) {
      //Need to update the screen first, to make sure syntax highlighting is correct after making 
      //a change (e.g., inserting a "(".  The autocommand may also require a redraw, so it's done
      //again below, unfortunately.
      if (syntax_present(curPor) && mustRedrawG)
         drawUpdateScreen(0);
      if (popup_visible)
         popup_check_cursor_pos();
      last_cursormoved = curPor->cursor;
   }

   if (ready)
      may_trigger_win_scrolled_resized();

   // Trigger SafeState if nothing is pending.
   may_trigger_safestate(ready && !ins_compl_active() && !pum_visible());

   if (mustRedrawG)
      drawUpdateScreen(0);
   ei (mustClearCommlineG || redrawCommlineG)
      showmode();      // clear cmdline and show mode
   showruler(false);
   setcursor();
   emsg_on_display = false;   // may remove error message now
}

//Handle a CTRL-V or CTRL-Q typed in Insert mode.
private void
insertStartVisualBlockMode(void) {
   Boole did_putchar = false;

   // may need to redraw when no more chars available now
   redrawInInsertMode(false);

   if (redrawing() && !char_avail()) {
      edit_putchar('^', true);
      did_putchar = true;
   }
   AppendToRedobuff((CS)CTRL_V_STR);   // CTRL-V

   add_to_showcmd_c(Ctrl_V);

   // Do not change any modifyOtherKeys ESC sequence to a normal key for CTRL-SHIFT-V.
   Unt c = get_literal(modMaskG & MOD_MASK_SHIFT);
   if (did_putchar)
      // when the line fits in 'columns' the '^' is at the start of the next
      // line and will not removed by the redraw
      edit_unputchar();
   clear_showcmd();

   insertRegular(c, false, true);
}

//After getting an ESC or CSI for a literal key: If the typeahead buffer
//contains a modifyOtherKeys sequence then decode it and return the result.
//Otherwise return "c". Note that this doesn't wait for characters, they must be in the typeahead
//buffer already.
private int
decodeModifyOtherKeys(int c) {
   CS p = typeBufG.c + typeBufG.currPos;
   int idx;
   int form = 0;
   int argidx = 0;
   int arg[2] = {0, 0};

   // Recognize:
   // form 0: {lead}{key};{modifier}u
   // form 1: {lead}27;{modifier};{key}~
   if (typeBufG.validLen >= 4 && (c == CSI || (c == ESC && *p == '['))) {
      idx = (*p == '[');
      while (idx < typeBufG.validLen && argidx < 2) {
         if (p[idx] == ';')
            ++argidx;
         ei (EE_ISDIGIT(p[idx]))
            arg[argidx] = arg[argidx] * 10 + (p[idx] - '0');
         else
            break;
         ++idx;
      }
      int kitty_no_mods = argidx == 0;
      if (idx < typeBufG.validLen
         && p[idx] == (form == 1 ? '~' : 'u')
         && (argidx == 1 || kitty_no_mods)
      ){
         // Match, consume the code.
         typeBufG.currPos += idx + 1;
         typeBufG.validLen -= idx + 1;
         if (typeBufG.validLen == 0)
            typebuf_was_filled = false;

         modMaskG = kitty_no_mods ? 0 : decode_modifiers(arg[!form]);
         c = mergeModifierKey(arg[form], &modMaskG);
      }
   }

   return c;
}

//}}}
//{{{Putting characters on the screen

// Put a character directly onto the screen.  It's not stored in a buffer.
// Used while handling CTRL-K, CTRL-V, etc. in Insert mode.
private int  pc_status;
#define PC_STATUS_UNSET  0   // pc_bytes was not set
#define PC_STATUS_RIGHT  1   // right half of double-wide char
#define PC_STATUS_LEFT   2   // left half of double-wide char
#define PC_STATUS_SET    3   // pc_bytes was filled
private Byte pc_bytes[MB_MAXBYTES + 1]; // saved bytes
private Byte charDecoFlagsP;
private int  pc_row;
private int  pc_col;

pub void
edit_putchar(int c, Boole needDoHilite) {
   if (!drawHasLines())
      return;

   update_topline();   // just in case topLine isn't valid
   validate_cursor();
   char decoFl = needDoHilite ? getDecoFlags(HLF_8) : 0;
   pc_row = curPor->windowRow + curPor->cursorRow;
   pc_col = curPor->windowCol;
   pc_status = PC_STATUS_UNSET;
   pc_col += curPor->cursorCol;

   // save the character to be able to put it back
   if (pc_status == PC_STATUS_UNSET) {
      screen_getbytes(pc_row, pc_col, pc_bytes, OUT &charDecoFlagsP);
      pc_status = PC_STATUS_SET;
   }
   screen_putchar(c, pc_row, pc_col, decoFl);
}

// Set the insert start position for when using a prompt book.
pub void
set_insstart(LineNr lnum, int col) {
   insertStartG.lnum = lnum;
   insertStartG.col = col;
   insertStartOrigG = insertStartG;
   insertStartG_textlen = insertStartG.col;
   insertStartG_blank_vcol = MAXCOL;
   arrow_used = false;
}

// Undo the previous edit_putchar().
pub void
edit_unputchar(void) {
   if (pc_status != PC_STATUS_UNSET && pc_row >= msg_scrolled) {
      if (pc_status == PC_STATUS_RIGHT)
         ++curPor->cursorCol;
      if (pc_status == PC_STATUS_RIGHT || pc_status == PC_STATUS_LEFT)
         drawPortLineLater(curPor, curPor->cursor.lnum);
      else
         drawText(pc_bytes, pc_row - msg_scrolled, pc_col, charDecoFlagsP);
   }
}

// Truncate the space at the end of a line.  This is to be used only in insert mode
pub void
truncate_spaces(CS line, Unt len) {
   // find start of trailing white space
   for (int i = (int)len - 1; i >= 0 && SPACE_OR_TAB(line[i]); i--) {
      line[i + 1] = ZERO;
   } 
}

// Backspace the cursor until the given column. May also be used when not in insert mode at all.
// Will attempt not to go before "col" even when there is a composing character.
pub void
backspace_until_column(int col) {
   while ((int)curPor->cursor.col > col) {
      curPor->cursor.col--;
      if (!del_char_after_col(col))
         break;
   }
}

// Like del_char(), but make sure not to go before column "limit_col".
// Only matters when there are composing characters. Return true when something was deleted.
private int
del_char_after_col(int limit_col) {
   if (limit_col >= 0) {
      ColNr ecol = curPor->cursor.col + 1;

      // Make sure the cursor is at the start of a character, but
      // skip forward again when going too far back because of a composing character.
      mb_adjust_cursor();
      while (curPor->cursor.col < (ColNr)limit_col) {
         int l = utf_ptr2len(ml_get_cursor());
         if (l == 0)  // end of line
            break;
         curPor->cursor.col += l;
      }
      if (*ml_get_cursor() == ZERO || curPor->cursor.col == ecol)
         return false;
      del_bytes((long)((int)ecol - curPor->cursor.col), false, true);
   } else
      (void)del_char(false);
   return true;
}

//Next character is interpreted literally.
//A one, two or three digit decimal number is interpreted as its byte value.
//If one or two digits are entered, the next character is given to vungetc().
//For Unicode a character > 255 may be returned.
//If "noReduceKeys" is true do not change any modifyOtherKeys ESC sequence into a normal key, 
//return ESC.
pub int
get_literal(int noReduceKeys) {
   Unt nc;
   int hex = false;
   int unicode = 0;

   if (gotInterruptG)
      return Ctrl_C;

   ++no_mapping;      // don't map the next key hits
   int cc = 0;
   int i = 0;
   for (;;) {
      nc = plain_vgetc();
      if ((nc == ESC || nc == CSI) && !noReduceKeys)
         nc = decodeModifyOtherKeys(nc);

      if ((modMaskG & ~MOD_MASK_SHIFT) != 0)
         //A character with non-Shift modifiers should not be a valid character for i_CTRL-V_digit.
         break;

      if ((stateG & MODE_COMMLINE) == 0 && MB_BYTE2LEN_CHECK(nc) == 1)
         add_to_showcmd(nc);
      if (nc == 'x' || nc == 'X')
         hex = true;
      ei (nc == 'u' || nc == 'U')
         unicode = nc;
      else {
         if (hex || unicode != 0) {
            if (!eeIsXDigit(nc))
               break;
            cc = cc * 16 + hex2nr(nc);
         } else {
            if (!EE_ISDIGIT(nc))
                break;
            cc = cc * 10 + nc - '0';
         }

         ++i;
      }

      if (cc > 255 && unicode == 0)
         cc = 255;      // limit range to 0-255
      nc = 0;

      if (hex) {     // hex: up to two chars
         if (i >= 2)
            break;
      } ei (unicode) {  // Unicode: up to four or eight chars
         if ((unicode == 'u' && i >= 4) || (unicode == 'U' && i >= 8))
            break;
      } ei (i >= 3)   // decimal: up to three chars
         break;
   }
   if (i == 0) {      // no number entered
      if (nc == K_ZERO) {  // ZERO is stored as NL
         cc = '\n';
         nc = 0;
      } else {
         cc = nc;
         nc = 0;
      }
   }

   if (cc == 0)   // ZERO is stored as NL
      cc = '\n';

   --no_mapping;
   if (nc) {
      vungetc(nc);
      // A character typed with i_CTRL-V_digit cannot have modifiers.
      modMaskG = 0;
   }
   gotInterruptG = false;       // CTRL-C typed after CTRL-V is not an interrupt
   return cc;
}

// flags for insertchar()
pub
#define INSCHAR_FORMAT    1   //force formatting
#define INSCHAR_DO_COM    2   //format comments
#define INSCHAR_CTRLV     4   //char typed just after CTRL-V
#define INSCHAR_NO_FEX    8   //don't use 'formatexpr'
#define INSCHAR_COM_LIST 16   //format comments with list/2nd line indent

// Insert character, taking care of special keys and modMaskG
private void
insertRegular(Unt c, Boole allow_modmask, Boole ctrlv) {       // c was typed after CTRL-V
   //Special function key, translate into "<Key>". Up to the last '>' is inserted with ins_str(), 
   //so as not to replace characters in replace mode. Only use modMaskG for special keys, to 
   //avoid things like <S-Space>, unless 'allow_modmask' is true.
   if (IS_SPECIAL(c) || (modMaskG && allow_modmask)) {
      CS p = get_special_key_name(c, modMaskG);
      int len = (int)STRLEN(p);
      c = p[len - 1];
      if (len > 2) {
         if (stop_arrow() == FAIL)
            return;
         p[len - 1] = ZERO;
         ins_str(p, len - 1);
         AppendToRedobuffLit(p, -1);
         ctrlv = false;
      }
   }
   if (stop_arrow() == OK)
      insertchar0(c, ctrlv ? INSCHAR_CTRLV : 0, -1);
}

//Special characters in this context are those that need processing other than the simple 
//insertion that can be performed here. This includes ESC which terminates the insert, and CR/NL
//which need special processing to open up a new line. This routine tries to optimize insertions 
//performed by the "redo", "undo" or "put" commands, so it needs to know when it should
//stop and defer processing to the "normal" mechanism. '0' and '^' are special, because they can
//be followed by CTRL-D.
#define ISSPECIAL(c)   ((c) < ' ' || (c) >= DEL || (c) == '0' || (c) == '^')


//"flags": INSCHAR_FORMAT - force formatting
//      INSCHAR_CTRLV  - char typed just after CTRL-V
//      INSCHAR_NO_FEX - don't use 'formatexpr'
//
//  NOTE: passes the flags value straight through to internal_format() which,
//     beside INSCHAR_FORMAT (above), is also looking for these:
//      INSCHAR_DO_COM   - format comments
//      INSCHAR_COM_LIST - format comments with num list or 2nd line indent
pub void
insertchar0(
   Unt c,         // character to insert or ZERO
   Unt flags,         // INSCHAR_FORMAT, etc.
   int second_indent      // indent for second line if >= 0
){
   CS p;
   int force_format = flags & INSCHAR_FORMAT;

   int textwidth = comp_textwidth(force_format);
   int fo_ins_blank = has_format_option(FO_INS_BLANK);

   //Try to break the line in two or more pieces when:
   //- Always do this if we have been called to do formatting only.
   //- Always do this when 'formatoptions' has the 'a' flag and the line
   //  ends in white space.
   //- Otherwise:
   //   - Don't do this if inserting a blank
   //   - Don't do this if an existing character is being replaced, unless
   //     we're in MODE_VREPLACE state.
   //   - Do this if the cursor is not on the line where insert started
   //   or - 'formatoptions' doesn't have 'l' or the line was not too long
   //         before the insert.
   //      - 'formatoptions' doesn't have 'b' or a blank was inserted at or
   //        before 'textwidth'
   if (textwidth > 0
       && (force_format
            || (!SPACE_OR_TAB(c)
                && (curPor->cursor.lnum != insertStartG.lnum
                  || ((!has_format_option(FO_INS_LONG) || insertStartG_textlen <= (ColNr)textwidth)
                      && (!fo_ins_blank || insertStartG_blank_vcol <= (ColNr)textwidth)
                     ))
               )
          )
   ) {
      // Format with @formatexpr when it's set.  Use internal formatting
      // when @formatexpr isn't set or it returns non-zero.
      Boole do_internal = true;
      ColNr virtcol = get_nolist_virtcol() + bookChar2Cells(c != ZERO ? c : gchar_cursor());

      if (curBook->o.formatExpr && (flags & INSCHAR_NO_FEX) == 0
         && (force_format || virtcol > (ColNr)textwidth)
      ) {
         do_internal = (fex_format(curPor->cursor.lnum, 1L, c) != 0);
         // It may be required to save for undo again, e.g. when setline() was called.
         needUndoS = true;
      }
      if (do_internal)
         internal_format(textwidth, second_indent, flags, c == ZERO, c);
   }

   if (c == ZERO)       // only formatting was wanted
      return;

   // Check whether this character should end a comment.
   if (didAindentG && c == end_comment_pending) {
      CS line;
      Byte lead_end[COM_MAX_LEN];       // end-comment string
      int middle_len, end_len;

      //Need to remove existing (middle) comment leader and insert end
      //comment leader.  First, check what comment leader we can find.
      int i = get_leader_len(line = ml_get_curline(), &p, false, true);
      if (i > 0 && firstOccurrence(p, COM_MIDDLE) != NULL) {  // Just checking
         // Skip middle-comment string
         while (*p && p[-1] != ':')   // find end of middle flags
            ++p;
         middle_len = strCutPathFromListOfPaths(OUT &p, OUT lead_end, COM_MAX_LEN, S",");
         // Don't count trailing white space for middle_len
         while (middle_len > 0 && SPACE_OR_TAB(lead_end[middle_len - 1]))
            --middle_len;

         // Find the end-comment string
         while (*p && p[-1] != ':')   // find end of end flags
            ++p;
         end_len = strCutPathFromListOfPaths(OUT &p, OUT lead_end, COM_MAX_LEN, S",");

         // Skip white space before the cursor
         i = curPor->cursor.col;
         while (--i >= 0 && SPACE_OR_TAB(line[i]))
            {}
         i++;

         // Skip to before the middle leader
         i -= middle_len;

         // Check some expected things before we go on
         if (i >= 0 && end_len > 0 && lead_end[end_len - 1] == end_comment_pending) {
            // Backspace over all the stuff we want to replace
            backspace_until_column(i);

            // Insert the end-comment string, except for the last
            // character, which will get inserted as normal later.
            ins_bytes_len(lead_end, end_len - 1);
         }
      }
   }
   end_comment_pending = ZERO;

   didAindentG = false;
   didSindentG = false;
   can_si = false;
   can_si_back = false;

   //If there's any pending input, grab up to INPUT_BUFLEN at once. This speeds up normal text 
   //input considerably. Don't do this when 'cindent' or 'indentexpr' is set, because we might
   //need to re-indent at a ':', or any other character (but not what 'paste' is set)..
   //Don't do this when there an InsertCharPre autocommand is defined, because we need to fire 
   //the event for every character. Do the check for InsertCharPre before the call to vpeekc() 
   //because the InsertCharPre autocommand could change the input buffer.

   if (!ISSPECIAL(c)
       && (mb_char2len(c) == 1)
       // Skip typeahead if test_override("char_avail", 1) was called.
       && !disable_char_avail_for_testing
       && vpeekc() != ZERO
       && !doIsIndentationExpressionBased()
   ) {
#define INPUT_BUFLEN 100
      Byte buf[INPUT_BUFLEN + 1];
      ColNr virtcol = 0;

      buf[0] = c;
      int i = 1;
      if (textwidth > 0)
         virtcol = get_nolist_virtcol();
      //Stop the string when:
      //- no more chars available
      //- finding a special character (command key)
      //- buffer is full
      //- running into the 'textwidth' boundary
      //- need to check for abbreviation: A non-word char after a word-char
      while (      (c = vpeekc()) != ZERO
         && !ISSPECIAL(c)
         && (MB_BYTE2LEN_CHECK(c) == 1)
         && i < INPUT_BUFLEN
         && (textwidth == 0
             || (virtcol += byte2cells(buf[i - 1])) < (ColNr)textwidth)
         && !(!no_abbr && !eeIsWordc(c) && eeIsWordc(buf[i - 1])))
      {
         buf[i] = vgetc();
         i++;
      }

      buf[i] = ZERO;
      ins_str(buf, i);
      if (flags & INSCHAR_CTRLV) {
         redo_literal(*buf);
         i = 1;
      } else
         i = 0;
      if (buf[i] != ZERO)
         AppendToRedobuffLit(buf + i, -1);
   } else {
      int cc;

      if ((cc = mb_char2len(c)) > 1) {
         Byte buf[MB_MAXBYTES + 1];
         mb_char2bytes(c, buf);
         buf[cc] = ZERO;
         opInsertCharBytes(buf, cc, false);
         AppendCharToRedobuff(c);
      } else {
         insertChar(c);
         if (flags & INSCHAR_CTRLV)
            redo_literal(c);
         else
            AppendCharToRedobuff(c);
      }
    }
}

//Put a character in the redo buffer, for when just after a CTRL-V.
private void
redo_literal(int c) {
   Byte buf[10];

   // Only digits need special treatment.  Translate them into a string of three digits.
   if (EE_ISDIGIT(c)) {
      eeSnprintf(buf, sizeof(buf), "%03d", c);
      AppendToRedobuff(buf);
   } else
      AppendCharToRedobuff(c);
}

//start_arrow() is called when an arrow key is used in insert mode.
//For undo/redo it resembles hitting the <ESC> key.
pub void
start_arrow(Pos* end_insert_pos) {    // can be NULL
   start_arrow_common(end_insert_pos, true);
}

//Like start_arrow() but with end_change argument.
//Will prepare for redo of CTRL-G U if "end_change" is false.
private void
start_arrow_with_change(NULLABLE Pos* end_insert_pos, int end_change) { //end undoable change
   start_arrow_common(end_insert_pos, end_change);
   if (!end_change) {
      AppendCharToRedobuff(Ctrl_G);
      AppendCharToRedobuff('U');
   }
}

private void
start_arrow_common(NULLABLE Pos* end_insert_pos, int end_change) {     // end undoable change
   if (!arrow_used && end_change) {  // something has been inserted
      AppendToRedobuff(ESC_STR);
      stop_insert(end_insert_pos, false, false);
      arrow_used = true;   // this means we stopped the current insert
   }
   check_spell_redraw();
}

//If we skipped highlighting word at cursor, do it now.
//It may be skipped again, thus reset spell_redraw_lnum first.
private void
check_spell_redraw(void) {
   if (spell_redraw_lnum != 0) {
      LineNr   lnum = spell_redraw_lnum;
      spell_redraw_lnum = 0;
      drawPortLineLater(curPor, lnum);
   }
}

//stop_arrow() is called before a change is made in insert mode.
//If an arrow key has been used, start a new insertion. Return FAIL if undo is impossible, 
//shouldn't insert then.
pub int
stop_arrow(void) {
   if (arrow_used) {
      insertStartG = curPor->cursor;   // new insertion starts here
      if (insertStartG.col > insertStartOrigG.col && !needUndoS)
         // Don't update the original insert position when moved to the
         // right, except when nothing was inserted yet.
         update_insertStartOrigS = false;
      insertStartG_textlen = (ColNr)linetabsize_str(ml_get_curline());

      if (u_save_cursor() == OK) {
          arrow_used = false;
          needUndoS = false;
      }

      ai_col = 0;
      ResetRedobuff();
      AppendToRedobuff((CS)"1i");   // pretend we start an insertion
      new_insert_skip = 2;
   } ei (needUndoS) {
      if (u_save_cursor() == OK)
         needUndoS = false;
   }

   // Always open fold at the cursor line when inserting something.
   foldOpenCursor();

   return (arrow_used || needUndoS ? FAIL : OK);
}

//Do a few things to stop inserting. "end_insert_pos" is where insert ended. It is NULL when 
//we already jumped to another portal/book.
private void
stop_insert(
   Pos* end_insert_pos,
   int esc,         // called by ins_esc()
   int nomove       // <c-\><c-o>, don't move cursor
){
   int cc;
   stop_redo_ins();

   //Save the inserted text for later redo with ^@ and CTRL-A.
   //Don't do it when "restart_edit" was set and nothing was inserted,
   //otherwise CTRL-O w and then <Left> will clear "lastInsertP".
   Text inserted = get_inserted();
   int added = inserted.c == NULL ? 0 : (int)inserted.len - new_insert_skip;
   if (did_restart_edit == 0 || added > 0) {
      eeglFree(lastInsertP.c);
      lastInsertP = inserted;             // structure copy
      last_insert_skip = added < 0 ? 0 : new_insert_skip;
   } else
      eeglFree(inserted.c);

   if (!arrow_used && end_insert_pos != NULL) {
      //Auto-format now.  It may seem strange to do this when stopping an
      //insertion (or moving the cursor), but it's required when appending
      //a line and having it end in a space.  But only do it when something
      //was actually inserted, otherwise undo won't work.
      if (!needUndoS && has_format_option(FO_AUTO)) {
         Pos tpos = curPor->cursor;

         //When the cursor is at the end of the line after a space the
         //formatting will move it to the following word.  Avoid that by
         //moving the cursor onto the space.
         cc = 'x';
         if (curPor->cursor.col > 0 && gchar_cursor() == ZERO) {
            dec_cursor();
            cc = gchar_cursor();
            if (!SPACE_OR_TAB(cc))
                curPor->cursor = tpos;
         }

         auto_format(true, false);

         if (SPACE_OR_TAB(cc)) {
            if (gchar_cursor() != ZERO)
                inc_cursor();
            // If the cursor is still at the same character, also keep the "coladd".
            if (gchar_cursor() == ZERO
               && curPor->cursor.lnum == tpos.lnum
               && curPor->cursor.col == tpos.col)
                curPor->cursor.coladd = tpos.coladd;
         }
      }

      // If a space was inserted for auto-formatting, remove it now.
      check_auto_format(true);

      // If we just did an auto-indent, remove the white space from the end
      // of the line, and put the cursor back. Do this when ESC was used or moving the cursor 
      // up/down. Check for the old position still being valid, just in case the text
      // got changed unexpectedly.
      if (!nomove && didAindentG && (esc || curPor->cursor.lnum != end_insert_pos->lnum)
         && end_insert_pos->lnum <= curBook->mem.lineCount
      ) {
         Pos   tpos = curPor->cursor;
         ColNr   prev_col = end_insert_pos->col;

         curPor->cursor = *end_insert_pos;
         check_cursor_col();  // make sure it is not past the line
         for (;;) {
         if (gchar_cursor() == ZERO && curPor->cursor.col > 0)
            --curPor->cursor.col;
         cc = gchar_cursor();
         if (!SPACE_OR_TAB(cc))
             break;
         if (del_char(true) == FAIL)
             break;  // should not happen
         }
         if (curPor->cursor.lnum != tpos.lnum)
            curPor->cursor = tpos;
         ei (curPor->cursor.col < prev_col) {
            // reset tpos, could have been invalidated in the loop above
            tpos = curPor->cursor;
            tpos.col++;
            if (cc != ZERO && gchar_pos(&tpos) == ZERO)
               ++curPor->cursor.col;   // put cursor back on the ZERO
         }

         // <C-S-Right> may have started Visual mode, adjust the position for
         // deleted characters.
         if (VIsual_active)
            check_visual_pos();
      }
   }
   didAindentG = false;
   didSindentG = false;
   can_si = false;
   can_si_back = false;

   //Set '[ and '] to the inserted text.  When end_insert_pos is NULL we are
   //now in a different book.
   if (end_insert_pos) {
      curBook->opStart = insertStartG;
      curBook->opStartOrig = insertStartOrigG;
      curBook->opEnd = *end_insert_pos;
   }
}

//Set the last inserted text to a single character. Used for the replace command.
pub void
set_last_insert(Unt c) {
   eeglFree(lastInsertP.c);
   lastInsertP.c = alloc(MB_MAXBYTES * 3 + 5);

   CS s = lastInsertP.c;
   // Use the CTRL-V only when entering a special char
   if (c < ' ' || c == DEL)
      *s++ = Ctrl_V;
   s = add_char2buf(c, s);
   *s++ = ESC;
   *s = ZERO;
   lastInsertP.len = (Unt)(s - lastInsertP.c);
   
   last_insert_skip = 0;
}

#if defined(EXITFREE)
pub void
free_last_insert(void) {
   EE_CLEAR_STRING(lastInsertP);
}
#endif

//Add character "c" to buffer "s". Escape the special meaning of K_SPECIAL
//and CSI.  Handle multi-byte characters. Return a pointer to after the added bytes.
pub CS
add_char2buf(Unt c, CS s) {
   Byte temp[MB_MAXBYTES + 1];

   int len = mb_char2bytes(c, temp);
   for (int i = 0; i < len; ++i) {
      c = temp[i];
      // Need to escape K_SPECIAL and CSI like in the typeahead buffer.
      if (c == K_SPECIAL) {
         *s++ = K_SPECIAL;
         *s++ = KS_SPECIAL;
         *s++ = KE_FILLER;
      } else
         *s++ = c;
   }
   return s;
}

//move cursor to start of line
//if flags & BL_WHITE   move to first non-white
//if flags & BL_SOL   move to first non-white if startofline is set,
//            otherwise keep "curswant" column
//if flags & BL_FIX   don't leave the cursor on a ZERO.
pub void
beginline(Unt flags) {
   if ((flags & BL_SOL) != 0 && !p_sol)
      coladvance(curPor->cursWant);
   else {
      curPor->cursor.col = 0;
      curPor->cursor.coladd = 0;

      if ((flags & (BL_WHITE | BL_SOL)) != 0) {
         for (CS ptr = ml_get_curline(); 
              SPACE_OR_TAB(*ptr) && !((flags & BL_FIX) && ptr[1] == ZERO); 
              ++ptr
         )
            ++curPor->cursor.col;
      }
      curPor->setCursWant = true;
   }
   adjust_skipcol();
}

//}}}
//{{{inserting special characters

//oneright oneleft cursor_down cursor_up
//
//Move one char {right,left,down,up}.
//Doesn't move onto the ZERO past the end of the line, unless it is allowed.
//Return OK when successful, FAIL when we hit a line of file boundary.
pub int
oneright(void) {
   if (virtual_active()) {
      Pos prevpos = curPor->cursor;

      // Adjust for multi-wide char (excluding TAB)
      CS ptr = ml_get_cursor();
      coladvance(getviscol() + ((*ptr != TAB
                    && bookIsCharPrintable((*mb_ptr2char)(ptr)))
             ? bookPtr2Cells(ptr) : 1));
      curPor->setCursWant = true;
      // Return OK if the cursor moved, FAIL otherwise (at window edge).
      return (prevpos.col != curPor->cursor.col
             || prevpos.coladd != curPor->cursor.coladd) ? OK : FAIL;
   }

   CS ptr = ml_get_cursor();
   if (*ptr == ZERO)
      return FAIL;       // already at the very end

   int l = utfCharLen(ptr);

   //move "l" bytes right, but don't end up on the ZERO, unless 'virtualedit'
   //contains "onemore".
   if (ptr[l] == ZERO)
      return FAIL;
   curPor->cursor.col += l;

   curPor->setCursWant = true;
   adjust_skipcol();
   return OK;
}

pub int
oneleft(void) {
   if (virtual_active()) {
      int v = getviscol();
      if (v == 0)
         return FAIL;

      // We might get stuck on 'showbreak', skip over it.
      int width = 1;
      for (;;) {
         coladvance(v - width);
         // getviscol() is slow, skip it when 'showbreak' is empty,
         // 'breakindent' is not set and there are no multi-byte characters
         if (getviscol() < v)
            break;
         ++width;
      }

      if (curPor->cursor.coladd == 1) {
         // Adjust for multi-wide char (not a TAB)
         CS ptr = ml_get_cursor();
         if (*ptr != TAB && bookIsCharPrintable((*mb_ptr2char)(ptr)) && bookPtr2Cells(ptr) > 1)
            curPor->cursor.coladd = 0;
      }

      curPor->setCursWant = true;
      adjust_skipcol();
      return OK;
   }

   if (curPor->cursor.col == 0)
      return FAIL;

   curPor->setCursWant = true;
   --curPor->cursor.col;

   //if the character on the left of the current cursor is a multi-byte
   //character, move to its first byte
   mb_adjust_cursor();
   adjust_skipcol();
   return OK;
}

//Move the cursor up "n" lines in portal "wp". Take care of closed folds.
pub void
cursor_up_inner(Portal* po, long n) {
   LineNr lnum = po->cursor.lnum;

   if (n >= lnum)
      lnum = 1;
   ei (hasAnyFolding(po)) {
      //Count each sequence of folded lines as one logical line.
      // go to the start of the current fold
      (void)getFoldsPortal(po, lnum, OUT &lnum, NULL, true, NULL);

      while (n--) {
         // move up one line
         --lnum;
         if (lnum <= 1)
            break;
         // If we entered a fold, move to the beginning, unless in
         // Insert mode or when 'foldopen' contains "all": it will open
         // in a moment.
         if (n > 0 || !((stateG & MODE_INSERT) || (p_fdo & FDO_ALL)))
            (void)getFoldsPortal(po, lnum, OUT &lnum, NULL, true, NULL);
      }
      if (lnum < 1)
          lnum = 1;
   } else
      lnum -= n;
   po->cursor.lnum = lnum;
}

pub int
cursor_up(long   n, Boole upd_topline){       // When true: update topline
   // This fails if the cursor is already in the first line or the count is
   // larger than the line number and '-' is in 'cpoptions'
   LineNr lnum = curPor->cursor.lnum;
   if (n > 0 && lnum <= 1)
       return FAIL;
   cursor_up_inner(curPor, n);

   // try to advance to the column we want to be at
   coladvance(curPor->cursWant);

   if (upd_topline)
      update_topline();   // make sure curPor->topLine is valid

   return OK;
}

//Move the cursor down "n" lines in window "wp". Take care of closed folds.
pub void
cursor_down_inner(Portal* wp, long n) {
   LineNr lnum = wp->cursor.lnum;
   LineNr line_count = wp->book->mem.lineCount;

   if (lnum + n >= line_count)
      lnum = line_count;
   ei (hasAnyFolding(wp)) {
      LineNr   last;

      // count each sequence of folded lines as one logical line
      while (n--) {
         if (getFoldsPortal(wp, lnum, NULL, OUT &last, true, NULL))
            lnum = last + 1;
         else
            ++lnum;
         if (lnum >= line_count)
            break;
      }
      if (lnum > line_count)
         lnum = line_count;
   } else
      lnum += n;

   wp->cursor.lnum = lnum;
}

//Cursor down a number of logical lines.
pub int
cursor_down(long n, int upd_topline) {      // When true: update topline
   LineNr lnum = curPor->cursor.lnum;
   LineNr line_count = curPor->book->mem.lineCount;
   // This fails if the cursor is already in the last (folded) line, or would
   // move beyond the last line and '-' is in 'cpoptions'.
   getFoldsPortal(curPor, lnum, NULL, OUT &lnum, true, NULL);
   if (n > 0 && lnum >= line_count)
      return FAIL;
   cursor_down_inner(curPor, n);

   // try to advance to the column we want to be at
   coladvance(curPor->cursWant);

   if (upd_topline)
      update_topline();   // make sure curPor->topLine is valid

   return OK;
}

//Stuff the last inserted text in the read buffer. lastInsertP actually is a copy of the redo 
//buffer, so we first have to remove the command.
pub int
stuff_inserted(
   Unt c,      // Command character to be inserted
   Long count,   // Repeat this many times
   int no_esc   // Don't add an ESC at the end
){
   Byte last = ' ';

   Text insert = get_last_insert();// text to be inserted
   if (insert.c == NULL) {
      emsg(_(e_no_inserted_text_yet));
      return FAIL;
   }

   // may want to stuff the command character, to start Insert mode
   if (c != ZERO)
      stuffcharReadbuff(c);

   if (insert.len > 0) {
      // look for the last ESC in 'insert'
      for (CS p = insert.c + insert.len - 1; p >= insert.c; --p) {
         if (*p == ESC) {
            insert.len = (Unt)(p - insert.c);
            break;
         }
      }
   }

   if (insert.len > 0) {
      CS p = insert.c + insert.len - 1;

      // when the last char is either "0" or "^" it will be quoted if no ESC
      // comes after it OR if it will insert more than once and "ptr" starts with ^D.   -- Acevedo
      if ((*p == '0' || *p == '^') && (no_esc || (*insert.c == Ctrl_D && count > 1))) {
          last = *p;
          --insert.len;
      }
   }

   do {
      stuffReadbuffLen(insert.c, (long)insert.len);
      // a trailing "0" is inserted as "<C-V>048", "^" as "<C-V>^"
      switch (last) {
      case '0':
#define TEXT_TO_INSERT "\026\060\064\070"
          stuffReadbuffLen((CS)TEXT_TO_INSERT, STRLEN_LITERAL(TEXT_TO_INSERT));
#undef TEXT_TO_INSERT
          break;

      case '^':
#define TEXT_TO_INSERT "\026^"
          stuffReadbuffLen((CS)TEXT_TO_INSERT, STRLEN_LITERAL(TEXT_TO_INSERT));
#undef TEXT_TO_INSERT
          break;

      default:
          break;
      }
   } while (--count > 0);

   // may want to stuff a trailing ESC, to get out of Insert mode
   if (!no_esc)
      stuffcharReadbuff(ESC);

   return OK;
}

pub Text
get_last_insert(void){
   Text insert = {null, 0};

   if (lastInsertP.c) {
      insert.c = lastInsertP.c + last_insert_skip;
      insert.len = (Unt)(lastInsertP.len - last_insert_skip);
   }

   return insert;
}

//Get last inserted string, and remove trailing <Esc>. Return pointer to allocated memory 
//(must be freed) or NULL.
pub CS
get_last_insert_save(void){
   Text   insert = get_last_insert();

   if (!insert.c)
      return S"";
   CS s = copySubstr(insert.c, insert.len);
   if (!s)
      return S"";

   if (insert.len > 0 && s[insert.len - 1] == ESC)   // remove trailing ESC
      s[--insert.len] = ZERO;
   return s;
}

//Check the word in front of the cursor for an abbreviation. Called when the non-id character "c" 
//has been entered. When an abbreviation is recognized it is removed from the text and the 
//replacement string is inserted in typeBufG.c[], followed by "c".
private Boole
echeck_abbr(Unt c) {
   // Don't check for abbreviation in paste mode, when disabled and just
   // after moving around with cursor keys.
   if (no_abbr || arrow_used)
      return false;

   return check_abbr(c, ml_get_curline(), curPor->cursor.col,
      curPor->cursor.lnum == insertStartG.lnum ? insertStartG.col : 0);
}

private void
insertRegisterContents(void) {
   int      need_redraw = false;
   Unt      regname;
   int      literally = 0;
   int      vis_active = VIsual_active;

   //If we are going to wait for a character, show a '"'.
   pc_status = PC_STATUS_UNSET;
   if (redrawing() && !char_avail()) {
      // may need to redraw when no more chars available now
      redrawInInsertMode(false);

      edit_putchar('"', true);
      add_to_showcmd_c(Ctrl_R);
   }

   //Don't map the register name. This also prevents the mode message to be deleted when ESC is hit
   ++no_mapping;
   ++allow_keys;
   regname = plain_vgetc();
   LANGMAP_ADJUST(regname, true);
   if (regname == Ctrl_R || regname == Ctrl_O || regname == Ctrl_P)    {
      // Get a third key for literal register insertion
      literally = regname;
      add_to_showcmd_c(literally);
      regname = plain_vgetc();
      LANGMAP_ADJUST(regname, true);
   }
   --no_mapping;
   --allow_keys;

   // Don't call u_sync() while typing the expression or giving an error
   // message for it. Only call it explicitly.
   ++no_u_sync;
   if (regname == '=')     {
      Pos   curpos = curPor->cursor;
      // Sync undo when evaluating the expression calls setline() or
      // append(), so that it can be undone separately.
      u_sync_once = 2;

      regname = get_expr_register();

      // Cursor may be moved back a column.
      curPor->cursor = curpos;
      check_cursor();
   }
   if (regname == ZERO || !valid_yank_reg(regname, false)) {
      need_redraw = true;   // remove the '"'
   } else {
      if (literally == Ctrl_O || literally == Ctrl_P)   {
         // Append the command to the redo buffer.
         AppendCharToRedobuff(Ctrl_R);
         AppendCharToRedobuff(literally);
         AppendCharToRedobuff(regname);

         do_put(regname, NULL, BACKWARD, 1L,
         (literally == Ctrl_P ? PUT_FIXINDENT : 0) | PUT_CURSEND);
      } ei (insert_reg(regname, literally) == FAIL) {
         need_redraw = true;   // remove the '"'
      }
      ei (stop_insert_mode)
         // When the '=' register was used and a function was invoked that
         // did ":stopinsert" then stuff_empty() returns false but we won't
         // insert anything, need to remove the '"'
         need_redraw = true;
   }
   --no_u_sync;
   if (u_sync_once == 1)
      needUndoS = true;
   u_sync_once = 0;
   clear_showcmd();

   // If the inserted register is empty, we need to remove the '"'
   if (need_redraw || stuff_empty())
      edit_unputchar();

   // Disallow starting Visual mode here, would get a weird mode.
   if (!vis_active && VIsual_active)
      end_visual_mode();
}

// CTRL-G commands in Insert mode.
private void
ins_ctrl_g(void) {
   // Right after CTRL-X the cursor will be after the ruler.
   setcursor();

   //Don't map the second key. This also prevents the mode message to be deleted when ESC is hit.
   ++no_mapping;
   ++allow_keys;
   int c = plain_vgetc();
   --no_mapping;
   --allow_keys;
   switch (c) {
   // CTRL-G k and CTRL-G <Up>: cursor up to insertStartG.col
   case K_UP:
   case Ctrl_K:
   case 'k': 
      ins_up(true);
      break;

   // CTRL-G j and CTRL-G <Down>: cursor down to insertStartG.col
   case K_DOWN:
   case Ctrl_J:
   case 'j': ins_down(true);
      break;

   // CTRL-G u: start new undoable edit
   case 'u': u_sync(true);
      needUndoS = true;

      //Need to reset insertStartG, esp. because a BS that joins
      //a line to the previous one must save for undo.
      update_insertStartOrigS = false;
      insertStartG = curPor->cursor;
      break;

   // CTRL-G U: do not break undo with the next char
   case 'U':
      // Allow one left/right cursor movement with the next char, without breaking undo.
      dont_sync_undo = MAYBE;
      break;

   case ESC:
      // Esc after CTRL-G cancels it.
      break;
   }
}

//CTRL-^ in Insert mode.
private void
ins_ctrl_hat(void) {
   if (map_to_exists_mode((CS)"", MODE_LANGMAP, false)) {
      // ":lmap" mappings exists, Toggle use of ":lmap" mappings.
      if (stateG & MODE_LANGMAP) {
         curBook->o.b_p_iminsert = B_IMODE_NONE;
         stateG &= ~MODE_LANGMAP;
      } else {
         curBook->o.b_p_iminsert = B_IMODE_LMAP;
         stateG |= MODE_LANGMAP;
      }
   }
   showmode();
   // Show/unshow value of 'keymap' in status lines.
   drawAllStatusLinesOfCurBookLater();
}

//Handle ESC in insert mode.
//Return true when leaving insert mode, false when going to repeat the insert.
private int
ins_esc(long* count, int commChar, int nomove) {      // don't move cursor
   static int   disabled_redraw = false;

   check_spell_redraw();

   int temp = curPor->cursor.col;
   if (disabled_redraw) {
      if (isRedrawingDisabledG > 0)
         --isRedrawingDisabledG;
      disabled_redraw = false;
   }
   if (!arrow_used) {
      //Don't append the ESC for "r<CR>" and "grx".
      //When 'insertmode' is set only CTRL-L stops Insert mode. Needed for when "count" is non-zero
      if (commChar != 'r' && commChar != 'v')
          AppendToRedobuff(ESC_STR);

      // Repeating insert may take a long time.  Check for interrupt now and then.
      if (*count > 0) {
          line_breakcheck();
          if (gotInterruptG)
         *count = 0;
      }

      if (--*count > 0)   {// repeat what was typed
          (void)start_redo_ins();
          if (commChar == 'r' || commChar == 'v')
         stuffRedoReadbuff(ESC_STR);   // no ESC in redo buffer
          ++isRedrawingDisabledG;
          disabled_redraw = true;
          return false;   // repeat the insert
      }
      stop_insert(&curPor->cursor, true, nomove);
   }

   if (commChar != 'r' && commChar != 'v') 
      ins_applyAutocomms(EVENT_INSERTLEAVEPRE);

   // When an autoindent was removed, curswant stays after the indent
   if (restart_edit == ZERO && (ColNr)temp == curPor->cursor.col)
      curPor->setCursWant = true;

   // Remember the last Insert position in the '^ mark.
   if ((commModifierG.cmod_flags & CMOD_KEEPJUMPS) == 0)
      curBook->lastInsert = curPor->cursor;

   //The cursor should end up on the last inserted character.
   //Don't do it for CTRL-O, unless past the end of the line.
   if (!nomove
       && (curPor->cursor.col != 0 || curPor->cursor.coladd > 0)
       && (restart_edit == ZERO || (gchar_cursor() == ZERO && !VIsual_active))
   ) {
      if (curPor->cursor.coladd > 0) {
         oneleft();
         if (restart_edit != ZERO)
            ++curPor->cursor.coladd;
      } else {
         --curPor->cursor.col;
         curPor->cacheState &= ~(VALID_WCOL|VALID_VIRTCOL);
         // Correct cursor for multi-byte character.
         mb_adjust_cursor();
      }
   }

   stateG = MODE_NORMAL;
   may_trigger_modechanged();
   // need to position cursor again when on a TAB and when on a char with virtual text.
   if (gchar_cursor() == TAB || curBook->hasTextprop )
      curPor->cacheState &= ~(VALID_WROW|VALID_WCOL|VALID_VIRTCOL);

   setmouse();
   ui_cursor_shape();      // may show different cursor shape

   // When recording or for CTRL-O, need to display the new mode.
   // Otherwise remove the mode message.
   if (reg_recording != 0 || restart_edit != ZERO)
      showmode();
   ei (p_smd && (gotInterruptG || !skip_showmode()))
      msg(S"");

   return true;       // exit Insert mode
}

//If @keymodel contains "startsel", may start selection.
//Return true when a CTRL-O and other keys stuffed.
private int
ins_start_select(int c) {
   if (!km_startsel)
      return false;
   switch (c) {
   case K_KHOME:
   case K_KEND:
   case K_PAGEUP:
   case K_KPAGEUP:
   case K_PAGEDOWN:
   case K_KPAGEDOWN:
      if (!(modMaskG & MOD_MASK_SHIFT))
         break;
      // FALLTHROUGH
   case K_S_LEFT:
   case K_S_RIGHT:
   case K_S_UP:
   case K_S_DOWN:
   case K_S_END:
   case K_S_HOME:
      //Start selection right away, the cursor can move with CTRL-O when beyond the end of the line
      start_selection();

      // Execute the key in (insert) Select mode.
      stuffcharReadbuff(Ctrl_O);
      if (modMaskG) {
         Byte buf[4] = {K_SPECIAL, KS_MODIFIER, modMaskG, ZERO};
         stuffReadbuffLen(buf, 3L);
      }
      stuffcharReadbuff(c);
      return true;
   }
   return false;
}


//Pressed CTRL-O in Insert mode.
private void
ins_ctrl_o(void) {
   restart_edit = 'I';
   if (virtual_active())
      ins_at_eol = false;   // cursor always keeps its column
   else
      ins_at_eol = (gchar_cursor() == ZERO);
}

//If the cursor is on an indent, ^T/^D insert/delete one shiftwidth.  Otherwise ^T/^D behave 
//like a "<<" or ">>". Always round the indent to 'shiftwidth'.
private void
ins_shift(Unt c, int lastc) {
   if (stop_arrow() == FAIL)
      return;
   AppendCharToRedobuff(c);

   //0^D and ^^D: remove all indent.
   if (c == Ctrl_D && (lastc == '0' || lastc == '^') && curPor->cursor.col > 0) {
      --curPor->cursor.col;
      (void)del_char(false);        // delete the '^' or '0'
      if (lastc == '^')
         old_indent = get_indent(); // remember curr. indent
      opChangeIndent(INDENT_SET, 0, 0, true);
   } else
      opChangeIndent(c == Ctrl_D ? INDENT_DEC : INDENT_INC, 0, 0, true);

   if (didAindentG && *skipwhite(ml_get_curline()) != ZERO)
      didAindentG = false;
   didSindentG = false;
   can_si = false;
   can_si_back = false;
   can_cindent = false;   // no cindenting after ^D or ^T
}

// "Delete" key
private void
ins_del(void) {
   int temp;

   if (stop_arrow() == FAIL)
      return;
   if (gchar_cursor() == ZERO) {     // delete newline
      temp = curPor->cursor.col;
      if (doJoinLinesUnderCursor(2, false, true, false, false) == FAIL) {
      } else {
         curPor->cursor.col = temp;
      }
   } else {
      del_char(false);  // delete char under cursor
   }
   didAindentG = false;
   didSindentG = false;
   can_si = false;
   can_si_back = false;
   AppendCharToRedobuff(K_DEL);
}

// Delete one character for ins_bs().
private void
ins_bs_one(void) {
   dec_cursor();
   (void)del_char(false);
}

//Handle Backspace, delete-word and delete-line in Insert mode.
//Return true when backspace was actually used.
private int
ins_bs(int c, int mode, int* inserted_space_p) {
   LineNr   lnum;
   int      cc;
   int      temp = 0;       // init for GCC
   ColNr   save_col;
   ColNr   mincol;
   int      did_backspace = false;
   int      cpc[MAX_COMBINED_SYMBOLS];       // composing characters
   int      call_fix_indent = false;

   //can't delete anything in an empty file
   //can't backup past first character in buffer
   //can't backup past starting point unless 'backspace' > 1
   //can backup to a previous line if 'backspace' == 0
   if (CURBOOK_EMPTY() || ( curPor->cursor.lnum == 1 && curPor->cursor.col == 0)) {
      return false;
   }

   if (stop_arrow() == FAIL)
      return false;
   int in_indent = inindent(0);
   if (in_indent)
      can_cindent = false;
   end_comment_pending = ZERO;   // After BS, don't auto-end comment

   // Virtualedit:
   //   BACKSPACE_CHAR eats a virtual space
   //   BACKSPACE_WORD eats all coladd
   //   BACKSPACE_LINE eats all coladd and keeps going
   if (curPor->cursor.coladd > 0) {
      if (mode == BACKSPACE_CHAR) {
          --curPor->cursor.coladd;
          return true;
      }
      if (mode == BACKSPACE_WORD) {
          curPor->cursor.coladd = 0;
          return true;
      }
      curPor->cursor.coladd = 0;
   }

   // Delete newline!
   if (curPor->cursor.col == 0) {
      lnum = insertStartG.lnum;
      if (curPor->cursor.lnum == lnum) {
         if (u_save((LineNr)(curPor->cursor.lnum - 2), (LineNr)(curPor->cursor.lnum + 1)) == FAIL)
            return false;
         --insertStartG.lnum;
         insertStartG.col = ml_get_len(insertStartG.lnum);
      }
      //In replace mode:
      //cc < 0: NL was inserted, delete it
      //cc >= 0: NL was replaced, put original characters back
      cc = -1;
      temp = gchar_cursor();   // remember current char
      --curPor->cursor.lnum;

      // When "aw" is in 'formatoptions' we must delete the space at
      // the end of the line, otherwise the line will be broken again when auto-formatting.
      if (has_format_option(FO_AUTO) && has_format_option(FO_WHITE_PAR)) {
         CS ptr = memGetLine(curBook, curPor->cursor.lnum, false);
         int len = ml_get_curline_len();

         if (len > 0 && ptr[len - 1] == ' ') {
            CS newp = alloc(curBook->mem.lineLen - 1);

            MEMMOVE(newp, ptr, len - 1);
            newp[len - 1] = ZERO;
            if (curBook->mem.lineLen > len + 1)
               MEMMOVE(newp + len, ptr + len + 1, curBook->mem.lineLen - len - 1);

            if ((curBook->mem.flags & ML_LINE_DIRTY) != 0)
               eeglFree(curBook->mem.cachedLine);
            curBook->mem.cachedLine = newp;
            curBook->mem.lineLen--;
            curBook->mem.lineTextLen--;
            curBook->mem.flags |= ML_LINE_DIRTY;
         }
      }
      (void)doJoinLinesUnderCursor(2, false, false, false, false);
      if (temp == ZERO && gchar_cursor() != ZERO)
         inc_cursor();

      didAindentG = false;
   } else {
      //Delete character(s) before the cursor.
      mincol = 0; // keep indent
      if (mode == BACKSPACE_LINE && (curBook->o.autoIndent || doIsIndentationExpressionBased())) {
         save_col = curPor->cursor.col;
         beginline(BL_WHITE);
         if (curPor->cursor.col < save_col) {
            mincol = curPor->cursor.col;
            // should now fix the indent to match with the previous line
            call_fix_indent = true;
         }
         curPor->cursor.col = save_col;
      }

      //Handle deleting one 'shiftwidth' or 'softtabstop'.
      if (mode == BACKSPACE_CHAR
         && ((get_sw_value(curBook) != 0) && curPor->cursor.col > 0
               && (*(ml_get_cursor() - 1) == TAB || (*(ml_get_cursor() - 1) == ' '
                     && (!*inserted_space_p || arrow_used)))
            )
      ) {
         ColNr   vcol = 0;
         ColNr   want_vcol;
         CS line;
         CS ptr;
         CS cursor_ptr;
         CS space_ptr;
         ColNr   space_vcol = 0;
         int      prev_space = false;
         ColNr   want_col;

         *inserted_space_p = false;

         space_ptr = ptr = line = ml_get_curline();
         cursor_ptr = line + curPor->cursor.col;

         // Compute virtual column of cursor position, and find the last
         // whitespace before cursor that is preceded by non-whitespace.
         // Use chartabsize() so that virtual text and wrapping are ignored.
         while (ptr < cursor_ptr) {
            int   cur_space = SPACE_OR_TAB(*ptr);

            if (!prev_space && cur_space) {
               space_ptr = ptr;
               space_vcol = vcol;
            }
            vcol += chartabsize(ptr, vcol);
            MB_PTR_ADV(ptr);
            prev_space = cur_space;
         }

         // Compute the virtual column where we want to be.
         want_vcol = vcol > 0 ? vcol - 1 : 0;
         want_vcol -= want_vcol % (int)get_sw_value(curBook);

         // Find the position to stop backspacing.
         // Use chartabsize() so that virtual text and wrapping are ignored.
         while (true) {
            int size = chartabsize(space_ptr, space_vcol);

            if (space_vcol + size > want_vcol)
               break;
            space_vcol += size;
            MB_PTR_ADV(space_ptr);
         }
         want_col = space_ptr - line;

         // Delete characters until we are at or before want_col.
         while (curPor->cursor.col > want_col)
            ins_bs_one();

         // Insert extra spaces until we are at want_vcol.
         for (; space_vcol < want_vcol; space_vcol++) {
            // Remember the first char we inserted
            if (curPor->cursor.lnum == insertStartOrigG.lnum
                     && curPor->cursor.col < insertStartOrigG.col
            )
               insertStartOrigG.col = curPor->cursor.col;
            ins_str(S" ", 1);
         }
      }

      //Delete up to starting point, start of line or previous word.
      else {
         int cclass = mb_get_class(ml_get_cursor());
         do {
            dec_cursor();

            cc = gchar_cursor();
            // look multi-byte character class
            int prev_cclass = cclass;
            cclass = mb_get_class(ml_get_cursor());

            // start of word?
            if (mode == BACKSPACE_WORD && !isSpace(cc)) {
               mode = BACKSPACE_WORD_NOT_SPACE;
               temp = eeIsWordc(cc);
            }
            // end of word?
            ei (mode == BACKSPACE_WORD_NOT_SPACE
               && ((isSpace(cc) || eeIsWordc(cc) != temp) || prev_cclass != cclass)
            ){
               inc_cursor();
               break;
            }
            if (p_delcomb)
               (void)utfc_ptr2char(ml_get_cursor(), cpc);
            (void)del_char(false);
            //If there are combining characters and 'delcombine' is set
            //move the cursor back.  Don't back up before the base character.
            if (p_delcomb && cpc[0] != ZERO)
               inc_cursor();
            // Just a single backspace?:
            if (mode == BACKSPACE_CHAR)
               break;
          } while ( (curPor->cursor.col > mincol));
      }
      did_backspace = true;
   }
   didSindentG = false;
   can_si = false;
   can_si_back = false;
   if (curPor->cursor.col <= 1)
      didAindentG = false;

   if (call_fix_indent)
      fix_indent();

   //It's a little strange to put backspaces into the redo
   //buffer, but it makes auto-indent a lot easier to deal with.
   AppendCharToRedobuff(c);

   // If deleted before the insertion point, adjust it
   if (curPor->cursor.lnum == insertStartOrigG.lnum && curPor->cursor.col < insertStartOrigG.col)
      insertStartOrigG.col = curPor->cursor.col;

   // When deleting a char the cursor line must never be in a closed fold.
   // E.g., when 'foldmethod' is indent and deleting the first non-white char before a Tab.
   if (did_backspace)
      foldOpenCursor();

   return did_backspace;
}

//Handle receiving P_PS: start paste mode. Insert the following text up to P_PE literally.
//When "drop" is true then consume the text and drop it.
pub int
bracketed_paste(PasteMode mode, int drop, ArrayList *gap) {
   Unt c;
   Byte buf[NUMBUFLEN + MB_MAXBYTES];
   int idx = 0;
   CS end = find_termcode((CS)"PE");
   int ret_char = -1;
   int save_allow_keys = allow_keys;

   // If the end code is too long we can't detect it, read everything.
   if (end && STRLEN(end) >= NUMBUFLEN)
      end = null;
   ++no_mapping;
   allow_keys = 0;

   for (;;) {
      // When the end is not defined read everything there is.
      if (end == NULL && vpeekc() == ZERO)
          break;
      do
          c = vgetc();
      while (c == K_IGNORE || c == K_VER_SCROLLBAR || c == K_HOR_SCROLLBAR);

      if (c == ZERO || gotInterruptG || (ex_normal_busy > 0 && c == Ctrl_C))
          // When CTRL-C was encountered the typeahead will be flushed and we
          // won't get the end sequence.  Except when using ":normal".
          break;

      idx += (*mb_char2bytes)(c, buf + idx);
      buf[idx] = ZERO;
      if (end != NULL && STRNCMP(buf, end, idx) == 0) {
         if (end[idx] == ZERO)
            break; // Found the end of paste code.
         continue;
      }
      if (!drop) {
         switch (mode) {
         case PASTE_CMDLINE:
            put_on_cmdline(buf, idx, true);
            break;

         case PASTE_EX:
            // add one for the ZERO that is going to be appended
            if (gap != NULL && ga_grow(gap, idx + 1) == OK) {
               MEMMOVE((char *)gap->c + gap->len, buf, (Unt)idx);
               gap->len += idx;
            }
            break;

         case PASTE_INSERT:
            if (stop_arrow() == OK) {
               c = buf[0];
               if (idx == 1 && (c == ENTER || c == K_KENTER || c == NL))
                  ins_eol(c);
               else {
                  opInsertCharBytes(buf, idx, false);
                  AppendToRedobuffLit(buf, idx);
               }
            }
            break;

         case PASTE_ONE_CHAR:
            if (ret_char == -1) {
               ret_char = (*mb_ptr2char)(buf);
            }
            break;
         }
      }
      idx = 0;
   }

   --no_mapping;
   allow_keys = save_allow_keys;

   return ret_char;
}

private void
ins_left(void) {
   int      end_change = dont_sync_undo == false; // end undoable change

   if ((p_fdo & FDO_HOR) != 0 && keyWasTypedG)
      foldOpenCursor();
   Pos tpos = curPor->cursor;
   if (oneleft() == OK) {
      start_arrow_with_change(&tpos, end_change);
      if (!end_change)
          AppendCharToRedobuff(K_LEFT);
   }

   // if 'whichwrap' set for cursor in insert mode may go to previous line
   ei (p_ww && firstOccurrence(p_ww, '[') != NULL && curPor->cursor.lnum > 1) {
      // always break undo when moving upwards/downwards, else undo may break
      start_arrow(&tpos);
      --(curPor->cursor.lnum);
      coladvance((ColNr)MAXCOL);
      curPor->setCursWant = true;   // so we stay at the end
   }
   dont_sync_undo = false;
}

private void
ins_home(Unt c) {
   if ((p_fdo & FDO_HOR) && keyWasTypedG)
      foldOpenCursor();
   Pos tpos = curPor->cursor;
   if (c == K_C_HOME)
      curPor->cursor.lnum = 1;
   curPor->cursor.col = 0;
   curPor->cursor.coladd = 0;
   curPor->cursWant = 0;
   start_arrow(&tpos);
}

private void
ins_end(Unt c) {
   if ((p_fdo & FDO_HOR) && keyWasTypedG)
      foldOpenCursor();
   Pos tpos = curPor->cursor;
   if (c == K_C_END)
      curPor->cursor.lnum = curBook->mem.lineCount;
   coladvance((ColNr)MAXCOL);
   curPor->cursWant = MAXCOL;

   start_arrow(&tpos);
}

private void
ins_s_left(void) {
   int end_change = dont_sync_undo == false; // end undoable change
   if ((p_fdo & FDO_HOR) && keyWasTypedG)
      foldOpenCursor();
   if (curPor->cursor.lnum > 1 || curPor->cursor.col > 0) {
      start_arrow_with_change(&curPor->cursor, end_change);
      if (!end_change)
          AppendCharToRedobuff(K_S_LEFT);
      (void)bck_word(1L, false, false);
      curPor->setCursWant = true;
   }
   dont_sync_undo = false;
}

private void
ins_right(void) {
   int end_change = dont_sync_undo == false; // end undoable change

   if ((p_fdo & FDO_HOR) && keyWasTypedG)
      foldOpenCursor();
   if (gchar_cursor() != ZERO || virtual_active()) {
      start_arrow_with_change(&curPor->cursor, end_change);
      if (!end_change)
         AppendCharToRedobuff(K_RIGHT);
      curPor->setCursWant = true;
      if (virtual_active())
          oneright();
      else {
         curPor->cursor.col += utfCharLen(ml_get_cursor());
      }
   }
   // if 'whichwrap' set for cursor in insert mode, may move the cursor to the next line
   ei (p_ww && firstOccurrence(p_ww, ']') != NULL && curPor->cursor.lnum < curBook->mem.lineCount) {
       start_arrow(&curPor->cursor);
       curPor->setCursWant = true;
       ++curPor->cursor.lnum;
       curPor->cursor.col = 0;
   }
   dont_sync_undo = false;
}

private void
ins_s_right(void) {
   int end_change = dont_sync_undo == false; // end undoable change
   if ((p_fdo & FDO_HOR) && keyWasTypedG)
      foldOpenCursor();
   if (curPor->cursor.lnum < curBook->mem.lineCount || gchar_cursor() != ZERO) {
      start_arrow_with_change(&curPor->cursor, end_change);
      if (!end_change)
         AppendCharToRedobuff(K_S_RIGHT);
      (void)fwd_word(1L, false, 0);
      curPor->setCursWant = true;
   }
   dont_sync_undo = false;
}

private void
ins_up( int      startcol) {  // when true move to insertStartG.col
   LineNr   old_topline = curPor->topLine;
   int      old_topfill = curPor->topFill;
   Pos tpos = curPor->cursor;
   if (cursor_up(1L, true) == OK) {
      if (startcol)
          coladvance(getvcol_nolist(&insertStartG));
      if (old_topline != curPor->topLine || old_topfill != curPor->topFill)
          redraw_later(UPD_VALID);
      start_arrow(&tpos);
      can_cindent = true;
   }
}

private void
ins_pageup(void) {
   if ((modMaskG & MOD_MASK_CTRL) != 0) {
      // <C-PageUp>: tab back
      if (firstTabG->next != NULL) {
         start_arrow(&curPor->cursor);
         gotoTabById(-1);
      }
      return;
   }

   Pos tpos = curPor->cursor;
   if (pagescroll(BACKWARD, 1L, false) == OK) {
      start_arrow(&tpos);
      can_cindent = true;
   }
}

private void
ins_down(int startcol) {  // when true move to insertStartG.col
   LineNr old_topline = curPor->topLine;
   int old_topfill = curPor->topFill;

   Pos tpos = curPor->cursor;
   if (cursor_down(1L, true) == OK) {
      if (startcol)
         coladvance(getvcol_nolist(&insertStartG));
      if (old_topline != curPor->topLine || old_topfill != curPor->topFill)
         redraw_later(UPD_VALID);
      start_arrow(&tpos);
      can_cindent = true;
   }
}

private void
ins_pagedown(void) {
   if (modMaskG & MOD_MASK_CTRL)  {
      // <C-PageDown>: tab forward
      if (firstTabG->next != NULL)   {
         start_arrow(&curPor->cursor);
         gotoTabById(0);
      }
      return;
   }

   Pos tpos = curPor->cursor;
   if (pagescroll(FORWARD, 1L, false) == OK) {
      start_arrow(&tpos);
      can_cindent = true;
   }
}

private void
ins_drop(void) {
   do_put('~', NULL, BACKWARD, 1L, PUT_CURSEND);
}

//Handle TAB in Insert mode.
//Return true when the TAB needs to be inserted like a normal character.
private int
ins_tab(void) {
   int      i;
   int      temp;

   if (insertStartG_blank_vcol == MAXCOL && curPor->cursor.lnum == insertStartG.lnum)
      insertStartG_blank_vcol = get_nolist_virtcol();
   if (echeck_abbr(TAB + ABBR_OFF))
      return false;

   int ind = inindent(0);
   if (ind)
      can_cindent = false;

   //When nothing special, insert TAB like a normal character.
   if (!curBook->o.expandTab && get_sw_value(curBook) == 0)
      return true;

   if (stop_arrow() == FAIL)
      return true;

   didAindentG = false;
   didSindentG = false;
   can_si = false;
   can_si_back = false;
   AppendToRedobuff((CS)"\t");

   temp = (int)get_sw_value(curBook);
   temp -= get_nolist_virtcol() % temp;

   //Insert the first space with insertChar(). Insert the rest with ins_str(); it will not delete
   //any chars.  For MODE_VREPLACE state, we use ins_char() for all characters.
   insertChar(' ');
   while (--temp > 0) {
      ins_str((CS)" ", 1);
   }

   //When 'expandtab' not set: Replace spaces by TABs where possible.
   if (!curBook->o.expandTab) {
      Pos* cursor;
      ColNr want_vcol, vcol;
      int change_col = -1;
      int save_list = curPor->o.list;
      CS tab = S"\t";
      CharTableSize   cts;

      //Get the current line.
      CS ptr = ml_get_cursor();
      cursor = &curPor->cursor;

      // When 'L' is not in 'cpoptions' a tab always takes up 'ts' spaces.
      curPor->o.list = false;

      // Find first white before the cursor
      Pos fpos = curPor->cursor;
      while (fpos.col > 0 && SPACE_OR_TAB(ptr[-1])) {
         --fpos.col;
         --ptr;
      }

      // compute virtual column numbers of first white and cursor
      getvcol(curPor, &fpos, &vcol, NULL, NULL);
      getvcol(curPor, cursor, &want_vcol, NULL, NULL);

      bookInitCharsForKeywordsSizeArg(&cts, curPor, 0, vcol, tab, tab);

      // Use as many TABs as possible.  Beware of 'breakindent', 'showbreak'
      // and 'linebreak' adding extra virtual columns.
      while (SPACE_OR_TAB(*ptr)) {
         i = lbr_chartabsize(&cts);
         if (cts.cts_vcol + i > want_vcol)
            break;
         if (*ptr != TAB) {
            *ptr = TAB;
            if (change_col < 0) {
               change_col = fpos.col;  // Column of first change
               // May have to adjust insertStartG
               if (fpos.lnum == insertStartG.lnum && fpos.col < insertStartG.col)
                  insertStartG.col = fpos.col;
            }
         }
         ++fpos.col;
         ++ptr;
         cts.cts_vcol += i;
      }
      vcol = cts.cts_vcol;
      clear_chartabsize_arg(&cts);

      if (change_col >= 0) {
         int repl_off = 0;

         // Skip over the spaces we need.
         bookInitCharsForKeywordsSizeArg(&cts, curPor, 0, vcol, ptr, ptr);
         while (cts.cts_vcol < want_vcol && *cts.cts_ptr == ' ') {
            cts.cts_vcol += lbr_chartabsize(&cts);
            ++cts.cts_ptr;
            ++repl_off;
         }
         ptr = cts.cts_ptr;
         vcol = cts.cts_vcol;
         clear_chartabsize_arg(&cts);

         if (vcol > want_vcol) {
            // Must have a char with 'showbreak' just before it.
            --ptr;
            --repl_off;
         }
         fpos.col += repl_off;

         // Delete following spaces.
         i = cursor->col - fpos.col;
         if (i > 0) {
            CS newp = alloc(curBook->mem.lineLen - i);

            int col = ptr - curBook->mem.cachedLine;
            if (col > 0)
               MEMMOVE(newp, ptr - col, col);
            MEMMOVE(newp + col, ptr + i, curBook->mem.lineLen - col - i);

            if ((curBook->mem.flags & ML_LINE_DIRTY) != 0)
               eeglFree(curBook->mem.cachedLine);
            curBook->mem.cachedLine = newp;
            curBook->mem.lineLen -= i;
            curBook->mem.lineTextLen = 0;
            curBook->mem.flags = (curBook->mem.flags | ML_LINE_DIRTY) & ~ML_EMPTY;
            cursor->col -= i;
         }
      }
      curPor->o.list = save_list;
   }
   return false;
}

//Handle CR or NL in insert mode. Return FAIL when out of memory or can't undo.
private int
ins_eol(Unt c) {
   if (echeck_abbr(c + ABBR_OFF))
      return OK;
   if (stop_arrow() == FAIL)
      return FAIL;

   //In MODE_VREPLACE state, a NL replaces the rest of the line, and starts
   //replacing the next line, so we push all of the characters left on the
   //line onto the replace stack.  This is not done here though, it is done in openLine().

   // Put cursor on ZERO if on the last char and coladd is 1 (happens after CTRL-O).
   if (virtual_active() && curPor->cursor.coladd > 0)
      coladvance(getviscol());

   AppendToRedobuff(NL_STR);
   int i = openLine(has_format_option(FO_RET_COMS) ? OPENLINE_DO_COM : 0, old_indent);
   old_indent = 0;
   can_cindent = true;
   // When inserting a line the cursor line must never be in a closed fold.
   foldOpenCursor();

   return i;
}

//Handle CTRL-E and CTRL-Y in Insert mode: copy char from other line.
 //Return the char to be inserted, or ZERO if none found.
pub int
ins_copychar(LineNr lnum) {
   if (lnum < 1 || lnum > curBook->mem.lineCount) {
      return ZERO;
   }

   // try to advance to the cursor column
   validate_virtcol();
   CS line = ml_get(lnum);
   CS prev_ptr = line;
   CharTableSize cts;
   bookInitCharsForKeywordsSizeArg(&cts, curPor, lnum, 0, line, line);
   while (cts.cts_vcol < curPor->virtCol && *cts.cts_ptr != ZERO) {
      prev_ptr = cts.cts_ptr;
      cts.cts_vcol += lbr_chartabsize_adv(&cts);
   }
   CS ptr = (cts.cts_vcol > curPor->virtCol) ? prev_ptr : cts.cts_ptr;
   clear_chartabsize_arg(&cts);

   return mb_ptr2char(ptr);
}

// CTRL-Y or CTRL-E typed in Insert mode.
private Unt
ins_ctrl_ey(Unt tc) {
   Unt c = tc;

   if (ctrl_x_mode_scroll()) {
      if (c == Ctrl_Y)
         scrolldown_clamp();
      else
         scrollup_clamp();
      redraw_later(UPD_VALID);
   } else {
      c = ins_copychar(curPor->cursor.lnum + (c == Ctrl_Y ? -1 : 1));
      if (c != ZERO) {
         long   tw_save;

         // The character must be taken literally, insert like it was typed after a CTRL-V, and 
         // pretend 'textwidth' wasn't set.  Digits, 'o' and 'x' are special after a
         // CTRL-V, don't use it for these.
         if (c < 256 && !SAFE_isalnum(c))
            AppendToRedobuff((CS)CTRL_V_STR);   // CTRL-V
         tw_save = curBook->o.textWidth;
         curBook->o.textWidth = -1;
         insertRegular(c, true, false);
         curBook->o.textWidth = tw_save;
         c = Ctrl_V;   // pretend CTRL-V is last character
         auto_format(false, true);
      }
   }
   return c;
}

// Get the value that virtCol would have when 'list' is off.
pub ColNr
get_nolist_virtcol(void) {
   // check validity of cursor in current book
   if (!curPor->book
         || !curPor->book->mem.mfile
         || curPor->cursor.lnum > curPor->book->mem.lineCount)
      return 0;
   if (curPor->o.list)
      return getvcol_nolist(&curPor->cursor);
   validate_virtcol();
   return curPor->virtCol;
}

pub void
set_can_cindent(int val) {
    can_cindent = val;
}

// Trigger "event" and take care of fixing undo.
pub int
ins_applyAutocomms(AutoEvent event) {
   Long   tick = CHANGEDTICK(curBook);
   int r = applyAutocomms(event, NULL, NULL, false, curBook);

   // If u_savesub() was called then we are not prepared to start a new line. Call u_save() with no
   // contents to fix that. Except when leaving Insert mode.
   if (event != EVENT_INSERTLEAVE && tick != CHANGEDTICK(curBook))
      u_save(curPor->cursor.lnum, (LineNr)(curPor->cursor.lnum + 1));

   return r;
}

//}}}
//{{{completion (Ctrl-X) mode

private Callback completeFnS;  // 'completefunc' callback function
private Callback omniFnS;      // 'omnifunc' callback function
private Callback thesaurusCbS; // 'thesaurusfunc' callback function
private Callback customCompleteFnS; 

#define CFC_KEYWORD         0x001
#define CFC_FILES           0x002
#define CFC_WHOLELINE       0x004

//Definitions used for CTRL-X submode.
//Note: If you change CTRL-X submode, you must also maintain ctrl_x_msgs[] and 
//ctrl_x_mode_names[] below
#define CTRL_X_WANT_IDENT   0x100

#define CTRL_X_NORMAL            0  // CTRL-N CTRL-P completion, default
#define CTRL_X_NOT_DEFINED_YET   1
#define CTRL_X_SCROLL            2
#define CTRL_X_WHOLE_LINE        3
#define CTRL_X_FILES             4
#define CTRL_X_TAGS             (5 + CTRL_X_WANT_IDENT)
#define CTRL_X_PATH_PATTERNS    (6 + CTRL_X_WANT_IDENT)
#define CTRL_X_PATH_DEFINES     (7 + CTRL_X_WANT_IDENT)
#define CTRL_X_FINISHED          8
#define CTRL_X_DICTIONARY       (9 + CTRL_X_WANT_IDENT)
#define CTRL_X_THESAURUS        (10 + CTRL_X_WANT_IDENT)
#define CTRL_X_CMDLINE          11
#define CTRL_X_FUNCTION         12
#define CTRL_X_OMNI             13
#define CTRL_X_LOCAL_MSG        15   //only used in "ctrl_x_msgs"
#define CTRL_X_EVAL             16   //for builtin function complete()
#define CTRL_X_CMDLINE_CTRL_X   17   //CTRL-X typed in CTRL_X_CMDLINE
#define CTRL_X_REGISTER         18   //complete words from registers

#define CTRL_X_MSG(i) ctrl_x_msgs[(i) & ~CTRL_X_WANT_IDENT]

// Message for CTRL-X mode, index is ctrl_x_mode.
private CS ctrl_x_msgs[] = {
   N_(" Keyword completion (^N^P)"), // CTRL_X_NORMAL, ^P/^N compl.
   N_(" ^X mode (^]^D^E^F^I^K^L^N^O^P^Rs^U^V^Y)"),
   NULL, // CTRL_X_SCROLL: depends on state
   N_(" Whole line completion (^L^N^P)"),
   N_(" File name completion (^F^N^P)"),
   N_(" Tag completion (^]^N^P)"),
   N_(" Path pattern completion (^N^P)"),
   N_(" Definition completion (^D^N^P)"),
   NULL, // CTRL_X_FINISHED
   N_(" Dictionary completion (^K^N^P)"),
   N_(" Thesaurus completion (^T^N^P)"),
   N_(" Command-line completion (^V^N^P)"),
   N_(" User defined completion (^U^N^P)"),
   N_(" Omni completion (^O^N^P)"),
   N_(" Spelling suggestion (^S^N^P)"),
   N_(" Keyword Local completion (^N^P)"),
   NULL,   // CTRL_X_EVAL doesn't use msg.
   N_(" Command-line completion (^V^N^P)"),
   N_(" Register completion (^N^P)"),
};

private CS ctrl_x_mode_names[] = {SMAP((CS),
   "keyword",
   "ctrl_x",
   "scroll",
   "whole_line",
   "files",
   "tags",
   "path_patterns",
   "path_defines",
   "unknown",          // CTRL_X_FINISHED
   "dictionary",
   "thesaurus",
   "cmdline",
   "function",
   "omni",
   "spell"),
   NULL,          // CTRL_X_LOCAL_MSG only used in "ctrl_x_msgs"
   SMAP((CS),
   "eval",
   "cmdline",
   "register"
)};

// Structure used to store one match for insert completion.
struct InsertCompletion {
   InsertCompletion* next;
   InsertCompletion* prev;
   InsertCompletion* nextMatch;      // matched next InsertCompletion
   Text   cp_str;         // matched text
   CS cp_text[CPT_COUNT];   // text for the menu
   Var   userData;
   CS fName; // file containing the match, allocated when flags has CP_FREE_FNAME
   Unt flags;      // CP_ values
   int cp_number;      // sequence number
   int cp_score;      // fuzzy match score or proximity score
   int cp_in_match_array;   // collected by displayedCompletionsS
   Decoration abbrDeco;   // hilite decoration for abbr
   Decoration kindDeco;   // hilite decoration for kind
   int indexOfSourceInCpt;   // index of this match's source in 'cpt' option
};

// values for flags
#define CP_ORIGINAL_TEXT  1   // the original text when the expansion begun
#define CP_FREE_FNAME     2   // fName is allocated
#define CP_CONT_S_IPOS    4   // use CONT_S_IPOS for compl_cont_status
#define CP_EQUAL          8   // ins_compl_equal() always returns true
#define CP_ICASE         16   // ins_compl_equal() ignores case
#define CP_FAST          32   // use fast_breakcheck instead of ui_breakcheck

//All the current matches are stored in a list.
//"compl_first_match" points to the start of the list.
//"compl_curr_match" points to the currently selected entry.
//"compl_shown_match" is different from compl_curr_match during
//ins_compl_get_exp(), when new matches are added to the list.
//"compl_old_match" points to previous "compl_curr_match".
private InsertCompletion* compl_first_match = NULL;
private InsertCompletion* compl_curr_match = NULL;
private InsertCompletion* compl_shown_match = NULL;
private InsertCompletion* compl_old_match = NULL;

// list used to store the InsertCompletion which have the max score
// used for completefuzzycollect
private InsertCompletion** compl_best_matches = NULL;
private int complCountBestS = 0;
// inserted a longest when completefuzzycollect enabled
private int compl_cfc_longest_ins = false;

// After using a cursor key <Enter> selects a match in the popup menu,
// otherwise it inserts a line break.
private int compl_enter_selects = false;

// When "compl_leader" is not NULL only matches that start with this string are used.
private Text compl_leader = {NULL, 0};

private int compl_get_longest = false; // put longest common string in compl_leader

// Selected one of the matches. When false, the match was edited or using the longest common string
private Boole complUsedMatchS;

// didn't finish finding completions.
private int compl_was_interrupted = false;

// Set when character typed while looking for matches and it means we should
// stop looking for matches.
private int compl_interrupted = false;

private int compl_restarting = false;   // don't insert match

// When the first completion is done "compl_started" is set.  When it's
// false the word to be completed must be located.
private int compl_started = false;

// Which Ctrl-X mode are we in?
private Unt ctrl_x_mode = CTRL_X_NORMAL;

private int compl_matches = 0;       // number of completion matches
private Text compl_pattern = {NULL, 0};    // search pattern for matching items
private Text cpt_compl_pattern = {NULL, 0}; // pattern returned by func in 'cpt'
private Unt compl_direction = FORWARD;
private Unt compl_shows_dir = FORWARD;
private int compl_pending = 0;       // > 1 for postponed CTRL-N
private Pos compl_startpos;
// Length in bytes of the text being completed (this is deleted to be replaced by the match)
private int     compl_length = 0;
private LineNr     compl_lnum = 0;           // lnum where the completion start
private ColNr compl_col = 0; // column where the text starts that is being completed
private ColNr compl_ins_end_col = 0;
private Text compl_orig_text = {NULL, 0};  // text as it was before completion started
private Unt compl_cont_mode = 0;
private Expand compl_xp;

private Portal* compl_curr_win = NULL;  // win where completion is active
private Book* compl_curr_buf = NULL;  // buf where completion is active

#define COMPL_INITIAL_TIMEOUT_MS    80
// Autocomplete uses a decaying timeout: starting from COMPL_INITIAL_TIMEOUT_MS, if the current 
// source exceeds its timeout, it is interrupted and the next begins with half the time. A small 
// minimum timeout ensures every source gets at least a brief chance.
private int compl_autocomplete = false;       // whether autocompletion is active
private int InsertCompletionimeout_ms = COMPL_INITIAL_TIMEOUT_MS;
private int InsertCompletionime_slice_expired = false; // time budget exceeded for current source
private int compl_from_nonkeyword = false;    // completion started from non-keyword

// Halve the current completion timeout, simulating exponential decay.
#define COMPL_MIN_TIMEOUT_MS   5
#define DECAY_InsertCompletionIMEOUT() \
    do { \
   if (InsertCompletionimeout_ms > COMPL_MIN_TIMEOUT_MS) \
       InsertCompletionimeout_ms /= 2; \
    } while (0)

// List of flags for method of completion.
private int     compl_cont_status = 0;
#define CONT_ADDING 1   // "normal" or "adding" expansion
#define CONT_INTRPT (2 + 4) // a ^X interrupted the current expansion. Set only iff N_ADDS is set
#define CONT_N_ADDS 4 // next ^X<> will add-new or expand-current
#define CONT_S_IPOS 8 // next ^X<> will set initial_pos? if so, word-wise-expansion will set SOL
#define CONT_SOL   16 // pattern includes start of line, just for word-wise expansion, 
                       // not set for ^X^L
#define CONT_LOCAL 32 // for ctrl_x_mode 0, ^X^P/^X^N do a local expansion, (eg use complete=.)

private int compl_opt_refresh_always = false;
private int compl_opt_suppress_empty = false;

private int compl_selected_item = -1;

private int* compl_fuzzy_scores;

// Define the structure for completion source (in 'cpt' option) information
typedef struct CompletionSource {
   int   refreshAlways;  // Whether 'refresh:always' is set for func
   int   startCol;       // Start column returned by func
   int   maxMatches;       // Max items to display from this source
   Elapsed   matchCollectionStart;       // Timestamp when match collection starts
} CompletionSource;

private CompletionSource *cpt_sources_array; // Pointer to the array of completion sources
private int cpt_sources_count;  // Total number of completion sources specified in the 'cpt' option
private int cpt_sources_index = -1;  // Index of the current completion source being expanded

// "displayedCompletionsS" points the currently displayed list of entries in the
// popup menu. It is NULL when there is no popup menu.
private Arr(PopupItem) displayedCompletionsS = NULL;
private int displayedCompletionsSsize;

private Unt addMatchToList(
   CS str, int len, CS fname, CS* cptext, Var *user_data, Unt cdir, Unt flags, 
   Boole adup, Arr(Decoration) userDecos, int score
);
private void ins_compl_longest_match(InsertCompletion *match);
private void ins_compl_del_pum(void);
private void filterFromFiles(
   ExpandMatch files, int thesaurus, Unt flags, RegMatch *regmatch, CS buf, OUT Unt *dir
);
private void ins_compl_free(void);
private int  ins_compl_need_restart(void);
private void ins_compl_new_leader(void);
private int  get_compl_len(void);
private void ins_compl_restart(void);
private void ins_compl_set_original_text(CS str, Unt len);
private void ins_compl_fixRedoBufForLeader(CS ptr_arg);
private void ins_compl_add_list(List *list);
private void ins_compl_add_dict(Bag *bag);
private int get_userdefined_compl_info(ColNr curs_col, Callback *cb, int *startcol);
private void get_cfn_completion_matches(Callback *cb);
private Callback *get_callback_if_cfn(CS p);
private Unt setup_cpt_sources(void);
private Boole is_cfn_refresh_always(void);
private void cpt_sources_clear(void);
private void cpt_compl_refresh(void);
private Unt  ins_compl_key2dir(Unt c);
private Boole ins_compl_pum_key(Unt c);
private int  ins_compl_key2count(Unt c);
private void show_pum(int prev_cursorRow, int prev_leftCol);
private unsigned  quote_meta(CS dest, CS str, int len);
private Boole ins_compl_has_multiple(void);
private void ins_compl_expand_multiple(CS str);
private void ins_compl_longest_insert(CS prefix);
private void ins_compl_make_linear(void);
private int ins_compl_make_cyclic(void);


// CTRL-X pressed in Insert mode.
pub void
ins_ctrl_x(void) {
   if (!ctrl_x_mode_cmdline()) {
      // if the next ^X<> won't ADD nothing, then reset compl_cont_status
      if ((compl_cont_status & CONT_N_ADDS) && !p_ac)
         compl_cont_status |= CONT_INTRPT;
      else
         compl_cont_status = 0;
      // We're not sure which CTRL-X mode it will be yet
      ctrl_x_mode = CTRL_X_NOT_DEFINED_YET;
      editSubmodeMsgG = (CS)_(CTRL_X_MSG(ctrl_x_mode));
      editSubmodePreMsgG = NULL;
      showmode();
   } else
      // CTRL-X in CTRL-X CTRL-V mode behaves differently to make CTRL-X
      // CTRL-V look like CTRL-N
      ctrl_x_mode = CTRL_X_CMDLINE_CTRL_X;

   may_trigger_modechanged();
}

// Functions to check the current CTRL-X mode.
private int ctrl_x_mode_normal(void)
    { return ctrl_x_mode == CTRL_X_NORMAL; }
private int ctrl_x_mode_scroll(void)
    { return ctrl_x_mode == CTRL_X_SCROLL; }
pub int ctrl_x_mode_whole_line(void)
    { return ctrl_x_mode == CTRL_X_WHOLE_LINE; }
private int ctrl_x_mode_files(void)
    { return ctrl_x_mode == CTRL_X_FILES; }
private int ctrl_x_mode_tags(void)
    { return ctrl_x_mode == CTRL_X_TAGS; }
private int ctrl_x_mode_path_patterns(void)
    { return ctrl_x_mode == CTRL_X_PATH_PATTERNS; }
private int ctrl_x_mode_path_defines(void)
    { return ctrl_x_mode == CTRL_X_PATH_DEFINES; }
private int ctrl_x_mode_dictionary(void)
    { return ctrl_x_mode == CTRL_X_DICTIONARY; }
private int ctrl_x_mode_thesaurus(void)
    { return ctrl_x_mode == CTRL_X_THESAURUS; }
private int ctrl_x_mode_cmdline(void) { 
   return ctrl_x_mode == CTRL_X_CMDLINE || ctrl_x_mode == CTRL_X_CMDLINE_CTRL_X; 
}
private int ctrl_x_mode_function(void)
    { return ctrl_x_mode == CTRL_X_FUNCTION; }
private int ctrl_x_mode_omni(void)
    { return ctrl_x_mode == CTRL_X_OMNI; }
private int ctrl_x_mode_eval(void)
    { return ctrl_x_mode == CTRL_X_EVAL; }
private int ctrl_x_mode_line_or_eval(void)
    { return ctrl_x_mode == CTRL_X_WHOLE_LINE || ctrl_x_mode == CTRL_X_EVAL; }
private int ctrl_x_mode_register(void)
    { return ctrl_x_mode == CTRL_X_REGISTER; }

// Whether other than default completion has been selected.
pub int
ctrl_x_mode_not_default(void) {
   return ctrl_x_mode != CTRL_X_NORMAL;
}

// Whether CTRL-X was typed without a following character, not including when in CTRL-X CTRL-V mode
pub int
ctrl_x_mode_not_defined_yet(void) {
   return ctrl_x_mode == CTRL_X_NOT_DEFINED_YET;
}

// Return true if currently in "normal" or "adding" insert completion matches state
pub int
compl_status_adding(void) {
   return compl_cont_status & CONT_ADDING;
}

// Return true if the completion pattern includes start of line, just for word-wise expansion
pub int
compl_status_sol(void) {
   return compl_cont_status & CONT_SOL;
}

// Return true if ^X^P/^X^N will do a local completion (i.e. use complete=.)
pub int
compl_status_local(void) {
   return compl_cont_status & CONT_LOCAL;
}

// Clear the completion status flags
private void
compl_status_clear(void) {
   compl_cont_status = 0;
}

// true if completion is using the forward direction matches
private int
compl_dir_forward(void) {
   return compl_direction == FORWARD;
}

// true if currently showing forward completion matches
private int
compl_shows_dir_forward(void) {
   return compl_shows_dir == FORWARD;
}

// true if currently showing backward completion matches
private int
compl_shows_dir_backward(void) {
   return compl_shows_dir == BACKWARD;
}

// Return true if the 'dictionary' or 'thesaurus' option can be used.
private int
has_compl_option(int dict_opt) {
   if (dict_opt ? (!curBook->o.dictionary) : (!curBook->o.thesaurus && !curBook->o.thesaurusFn)) {
      ctrl_x_mode = CTRL_X_NORMAL;
      editSubmodeMsgG = NULL;
      msgDeco(dict_opt ? _("'dictionary' option is empty")
              : _("'thesaurus' option is empty"), getDecoFlags(HLF_E));
      if (emsg_silent == 0 && !in_assert_fails)    {
         setcursor();
         out_flush();
         if (!get_EeglVar_nr(VV_TESTING))
            ui_delay(2004L, false);
      }
      return false;
   }
   return true;
}

// Is the character "c" a valid key to go to or keep us in CTRL-X mode? Depends on the current mode
pub int
eeIsCtrlXKey(Unt c) {
   // Always allow ^R - let its results then be checked
   if (c == Ctrl_R && ctrl_x_mode != CTRL_X_REGISTER)
      return true;

   // Accept <PageUp> and <PageDown> if the popup menu is visible.
   if (ins_compl_pum_key(c))
      return true;

   switch (ctrl_x_mode) {
   case 0:          // Not in any CTRL-X mode
      return (c == Ctrl_N || c == Ctrl_P || c == Ctrl_X);
   case CTRL_X_NOT_DEFINED_YET:
   case CTRL_X_CMDLINE_CTRL_X:
      return (   c == Ctrl_X || c == Ctrl_Y || c == Ctrl_E
          || c == Ctrl_L || c == Ctrl_F || c == Ctrl_RSB
          || c == Ctrl_I || c == Ctrl_D || c == Ctrl_P
          || c == Ctrl_N || c == Ctrl_T || c == Ctrl_V
          || c == Ctrl_Q || c == Ctrl_U || c == Ctrl_O
          || c == Ctrl_S || c == Ctrl_K || c == 's'
          || c == Ctrl_Z || c == Ctrl_R);
   case CTRL_X_SCROLL:
      return (c == Ctrl_Y || c == Ctrl_E);
   case CTRL_X_WHOLE_LINE:
      return (c == Ctrl_L || c == Ctrl_P || c == Ctrl_N);
   case CTRL_X_FILES:
      return (c == Ctrl_F || c == Ctrl_P || c == Ctrl_N);
   case CTRL_X_DICTIONARY:
      return (c == Ctrl_K || c == Ctrl_P || c == Ctrl_N);
   case CTRL_X_THESAURUS:
      return (c == Ctrl_T || c == Ctrl_P || c == Ctrl_N);
   case CTRL_X_TAGS:
      return (c == Ctrl_RSB || c == Ctrl_P || c == Ctrl_N);
   case CTRL_X_PATH_PATTERNS:
      return (c == Ctrl_P || c == Ctrl_N);
   case CTRL_X_PATH_DEFINES:
      return (c == Ctrl_D || c == Ctrl_P || c == Ctrl_N);
   case CTRL_X_CMDLINE:
      return (c == Ctrl_V || c == Ctrl_Q || c == Ctrl_P || c == Ctrl_N || c == Ctrl_X);
   case CTRL_X_FUNCTION:
      return (c == Ctrl_U || c == Ctrl_P || c == Ctrl_N);
   case CTRL_X_OMNI:
      return (c == Ctrl_O || c == Ctrl_P || c == Ctrl_N);
   case CTRL_X_EVAL:
      return (c == Ctrl_P || c == Ctrl_N);
   case CTRL_X_REGISTER:
      return (c == Ctrl_R || c == Ctrl_P || c == Ctrl_N);
   }
   internal_error(S"eeIsCtrlXKey()");
   return false;
}

// true if "match" is the original text when the completion began.
private int
match_at_original_text(InsertCompletion *match) {
   return match->flags & CP_ORIGINAL_TEXT;
}

// Returns true if "match" is the first match in the completion list.
private int
is_first_match(InsertCompletion *match) {
   return match == compl_first_match;
}

// true when character "c" is part of the item currently being completed. Used to decide 
// whether to abandon complete mode when the menu is visible.
private int
ins_compl_accept_char(int c) {
   if (compl_autocomplete && compl_from_nonkeyword)
      return false;

   if (ctrl_x_mode & CTRL_X_WANT_IDENT)
      // When expanding an identifier only accept identifier chars.
      return eeIsIdentifierChar(c);

   switch (ctrl_x_mode) {
   case CTRL_X_FILES:
      // When expanding file name only accept file name chars. But not path separators, so that
      // "proto/<Tab>" expands files in "proto", not "proto/" as a whole
      return eeIsFnameChar(c) && c != '/';

   case CTRL_X_CMDLINE:
   case CTRL_X_CMDLINE_CTRL_X:
   case CTRL_X_OMNI:
      // Command line and Omni completion can work with just about any
      // printable character, but do stop at white space.
      return bookIsCharPrintable(c) && !SPACE_OR_TAB(c);

   case CTRL_X_WHOLE_LINE:
      // For while line completion a space can be part of the line.
      return bookIsCharPrintable(c);
   }
   return eeIsWordc(c);
}

//Get the completed text by inferring the case of the originally typed text.
//If the result is in allocated memory "tofree" is set to it.
private CS
ins_compl_infercase_gettext(
   CS str,
   int char_len,
   int compl_char_len,
   int min_len,
   OUT Byte** tofree
) {
   int i, c;
   int has_lower = false;
   int was_letter = false;
   ArrayList   gap;

   IObuff[0] = ZERO;

   // Allocate wide character array for the completion and fill it.
   Arr(int) wideChars = ALLOC_MULT(int, char_len);

   CS p = str;
   for (i = 0; i < char_len; ++i) {
      wideChars[i] = strAdvanceMultibyte(&p);
   }

   // Rule 1: Were any chars converted to lower?
   p = compl_orig_text.c;
   for (i = 0; i < min_len; ++i) {
      c = strAdvanceMultibyte(&p);
      if (MB_ISLOWER(c)) {
         has_lower = true;
         if (MB_ISUPPER(wideChars[i])) {
            // Rule 1 is satisfied.
            for (i = compl_char_len; i < char_len; ++i)
                wideChars[i] = MB_TOLOWER(wideChars[i]);
            break;
         }
      }
   }

   //Rule 2: No lower case, 2nd consecutive letter converted to upper case.
   if (!has_lower) {
      p = compl_orig_text.c;
      for (i = 0; i < min_len; ++i) {
         c = strAdvanceMultibyte(&p);
         if (was_letter && MB_ISUPPER(c) && MB_ISLOWER(wideChars[i])) {
            // Rule 2 is satisfied.
            for (i = compl_char_len; i < char_len; ++i)
                wideChars[i] = MB_TOUPPER(wideChars[i]);
            break;
         }
         was_letter = MB_ISLOWER(c) || MB_ISUPPER(c);
      }
   }

   // Copy the original case of the part we typed.
   p = compl_orig_text.c;
   for (i = 0; i < min_len; ++i) {
      c = strAdvanceMultibyte(&p);
      if (MB_ISLOWER(c))
          wideChars[i] = MB_TOLOWER(wideChars[i]);
      ei (MB_ISUPPER(c))
          wideChars[i] = MB_TOUPPER(wideChars[i]);
   }

   // Generate encoding specific output from wide character array.
   p = IObuff;
   i = 0;
   ga_init2(&gap, 1, 500);
   while (i < char_len) {
      if (gap.c != NULL) {
         if (ga_grow(&gap, 10) == FAIL) {
            ga_clear(&gap);
            return (CS)"[failed]";
         }
         p = (CS)gap.c + gap.len;
         gap.len += (*mb_char2bytes)(wideChars[i], p);
         i++;
      } ei ((p - IObuff) + 6 >= IOSIZE) {
         // Multi-byte characters can occupy up to five bytes more than ASCII characters, and we 
         // also need one byte for ZERO, so when getting to six bytes from the edge of IObuff
         // switch to using a growarray. Add the character in the next round.
         if (ga_grow(&gap, IOSIZE) == FAIL) {
            eeglFree(wideChars);
            return (CS)"[failed]";
         }
         *p = ZERO;
         STRCPY(gap.c, IObuff);
         gap.len = (int)(p - IObuff);
      } else
         p += (*mb_char2bytes)(wideChars[i], p);
      i++;
   }
   eeglFree(wideChars);

   if (gap.c) {
      *tofree = gap.c;
      return gap.c;
   }

   *p = ZERO;
   return IObuff;
}

// This is like addMatchToList(), but if 'ic' and 'inf' are set, then the case of the originally 
// typed text is used, and the case of the completed text is inferred, ie this tries to work out
// what case you probably wanted the rest of the word to be in -- webb
pub Unt
ins_compl_add_infercase(
   CS str_arg,
   int len,
   int icase,
   CS fname,
   Unt dir,
   int cont_s_ipos,  // next ^X<> will set initial_pos
   int score
) {
   CS str = str_arg;
   CS p;
   int char_len;      // count multi-byte characters
   int compl_char_len;
   int min_len;
   Unt flags = 0;
   CS tofree = NULL;

   if (p_ic && curBook->o.inferCase && len > 0) {
      // Infer case of completed part. Find actual length of completion.
      p = str;
      char_len = 0;
      while (*p != ZERO) {
         MB_PTR_ADV(p);
         ++char_len;
      }

      // Find actual length of original text.
      p = compl_orig_text.c;
      compl_char_len = 0;
      while (*p != ZERO) {
         MB_PTR_ADV(p);
         ++compl_char_len;
      }

      // "char_len" may be smaller than "compl_char_len" when using
      // thesaurus, only use the minimum when comparing.
      min_len = MIN(char_len, compl_char_len);
      str = ins_compl_infercase_gettext(str, char_len, compl_char_len, min_len, &tofree);
   }
   if (cont_s_ipos)
      flags |= CP_CONT_S_IPOS;
   if (icase)
      flags |= CP_ICASE;

   Unt res = addMatchToList(str, len, fname, NULL, NULL, dir, flags, false, NULL, score);
   eeglFree(tofree);
   return res;
}

// Check if ctrl_x_mode has been configured in 'completefuzzycollect'
private int
cfc_has_mode(void) {
   if (ctrl_x_mode_normal() || ctrl_x_mode_dictionary())
      return (cfc_flags & CFC_KEYWORD) != 0;
   ei (ctrl_x_mode_files())
      return (cfc_flags & CFC_FILES) != 0;
   ei (ctrl_x_mode_whole_line())
      return (cfc_flags & CFC_WHOLELINE) != 0;
   return false;
}

// Returns true if matches should be sorted based on proximity to the cursor.
private int
is_nearest_active(void) {
   Unt flags = curBook->o.completeOpt;
   return (compl_autocomplete || (flags & COT_NEAREST)) && !(flags & COT_FUZZY);
}

//Add a match to the list of matches. The arguments are:
//    str       - text of the match to add
//    len       - length of "str". If -1, then the length of "str" is computed.
//    fname     - file name to associate with this match.
//    cptext    - list of strings to use with this match (for abbr, menu, info and kind)
//    user_data - user supplied data (any Eegl type) for this match
//    cdir    - match direction. If 0, use "compl_direction".
//    flags_arg - match flags (flags)
//    adup    - accept this match even if it is already present.
//    *userDecos  - list of 2 extra hilite decorations for abbr kind.
//If "cdir" is FORWARD, then the match is added after the current match.
//Otherwise, it is added before the current match.
//
//If the given string is already in the list of completions, then return
//NOTDONE, otherwise add it to the list and return OK. If there is an error,
//maybe because alloc() returns NULL, then FAIL is returned.
private Unt
addMatchToList(
   CS str,
   int len,
   CS fname,
   Byte** cptext,       // extra text for popup menu or NULL
   Var* user_data,  // "user_data" entry or NULL
   Unt cdir,
   Unt flags_arg,
   Boole adup,          // accept duplicate match
   Arr(Decoration) userDecos,           // user abbreviation/kind decorations
   int      score
) {
   InsertCompletion   *match, *current, *prev;
   Unt dir = (cdir == 0 ? compl_direction : cdir);
   Unt flags = flags_arg;
   int inserted = false;

   if (flags & CP_FAST)
      fast_breakcheck();
   else
      ui_breakcheck();
   if (gotInterruptG)
      return FAIL;
   if (len < 0)
      len = (int)STRLEN(str);

   // If the same match is already present, don't add it.
   if (compl_first_match != NULL && !adup) {
      match = compl_first_match;
      do {
         if (!match_at_original_text(match)
             && STRNCMP(match->cp_str.c, str, len) == 0
             && ((int)match->cp_str.len <= len || match->cp_str.c[len] == ZERO)
          ){
            if (is_nearest_active() && score > 0 && score < match->cp_score)
               match->cp_score = score;
            return NOTDONE;
         }
         match = match->next;
      } while (match != NULL && !is_first_match(match));
   }

   // Remove any popup menu before changing the list of matches.
   ins_compl_del_pum();

   // Allocate a new match structure. Copy the values to the new match structure.
   match = ALLOC_CLEAR_ONE(InsertCompletion);
   match->cp_number = flags & CP_ORIGINAL_TEXT ? 0 : -1;
   match->cp_str.c = copySubstr(str, len);
   match->cp_str.len = len;

   // match-fname is:
   // - compl_curr_match->fName if it is a string equal to fname.
   // - a copy of fname, CP_FREE_FNAME is set to free later THE allocated mem.
   // - NULL otherwise.   --Acevedo
   if (fname
          && compl_curr_match && compl_curr_match->fName
          && STRCMP(fname, compl_curr_match->fName) == 0
   )
      match->fName = compl_curr_match->fName;
   ei (fname) {
      match->fName = copyStr(fname);
      flags |= CP_FREE_FNAME;
   } else
      match->fName = NULL;
   match->flags = flags;
   match->abbrDeco = userDecos ? userDecos[0] : EMPTY_DECO;
   match->kindDeco = userDecos ? userDecos[1] : EMPTY_DECO;
   match->cp_score = score;
   match->indexOfSourceInCpt = cpt_sources_index;

   if (cptext) {
      for (int i = 0; i < CPT_COUNT; ++i) {
         if (cptext[i] != NULL && *cptext[i] != ZERO)
            match->cp_text[i] = copyStr(cptext[i]);
      }
   }
   if (user_data)
      match->userData = *user_data;

   // Link the new match structure after (FORWARD) or before (BACKWARD) the
   // current match in the list of matches .
   if (!compl_first_match)
      match->next = match->prev = NULL;
   ei (cfc_has_mode() && score != FUZZY_SCORE_NONE && compl_get_longest) {
      current = compl_first_match->next;
      prev = compl_first_match;
      inserted = false;
      // The direction is ignored when using longest and completefuzzycollect, because matches are 
      // inserted and sorted by score.
      while (current != NULL && current != compl_first_match) {
         if (current->cp_score < score) {
            match->next = current;
            match->prev = current->prev;
            if (current->prev)
               current->prev->next = match;
            current->prev = match;
            inserted = true;
            break;
         }
         prev = current;
         current = current->next;
      }
      if (!inserted) {
         prev->next = match;
         match->prev = prev;
         match->next = compl_first_match;
         compl_first_match->prev = match;
      }
   } ei (dir == FORWARD) {
      match->next = compl_curr_match->next;
      match->prev = compl_curr_match;
   } else {  // BACKWARD
      match->next = compl_curr_match;
      match->prev = compl_curr_match->prev;
   }
   if (match->next)
      match->next->prev = match;
   if (match->prev)
      match->prev->next = match;
   else   // if there's nothing before, it is the first match
      compl_first_match = match;
   compl_curr_match = match;

   // Find the longest common string if still doing that.
   if (compl_get_longest && (flags & CP_ORIGINAL_TEXT) == 0 && !cfc_has_mode())
      ins_compl_longest_match(match);

   return OK;
}

// Return true if "str[len]" matches with match->cp_str, considering match->flags.
private int
ins_compl_equal(InsertCompletion *match, CS str, int len) {
   if ((match->flags & CP_EQUAL) != 0)
      return true;
   if ((match->flags & CP_ICASE) != 0)
      return STRNICMP(match->cp_str.c, str, (Unt)len) == 0;
   return STRNCMP(match->cp_str.c, str, (Unt)len) == 0;
}

// when len is -1 mean use whole length of p otherwise part of p
private void
ins_compl_insert_bytes(CS p, int len) {
   if (len == -1)
      len = (int)STRLEN(p);
   ins_bytes_len(p, len);
   compl_ins_end_col = curPor->cursor.col;
}

//Check if the column is within the currently inserted completion text
//column range. If it is, return a special hilite decoration. -1 means normal item.
pub Decoration
getDecorationIfColumnIsWithinCompletion(LineNr lnum, int col) {
   if (curBook->o.completeOpt & COT_FUZZY)
      return (Decoration){.hiId = SHORT};
   Decoration deco = decosByHiliteName((CS)"ComplMatchIns");
   if (deco.hiId == SHORT)
      return (Decoration){.hiId = SHORT};

   int start_col = compl_col + (int)ins_compl_leader_len();
   if (!ins_compl_has_multiple())
      return (col >= start_col && col < compl_ins_end_col) ? deco : (Decoration){.hiId = SHORT};

   // Multiple lines
   if ((lnum == compl_lnum && col >= start_col && col < MAXCOL) ||
         (lnum > compl_lnum && lnum < curPor->cursor.lnum) ||
         (lnum == curPor->cursor.lnum && col <= compl_ins_end_col)
   )
      return deco;

   return (Decoration){.hiId = SHORT};
}

// Return true if the current completion string contains newline characters,
// indicating it's a multi-line completion.
private Boole
ins_compl_has_multiple(void) {
   return firstOccurrence(compl_shown_match->cp_str.c, '\n') != NULL;
}

//Return true if the given line number falls within the range of a multi-line completion, i.e. 
//between the starting line (compl_lnum) and current cursor line. Always return false for 
//single-line completions.
pub int
ins_compl_lnum_in_range(LineNr lnum) {
   if (!ins_compl_has_multiple())
      return false;
   return lnum >= compl_lnum && lnum <= curPor->cursor.lnum;
}

// Reduce the longest common string for match "match".
private void
ins_compl_longest_match(InsertCompletion* match) {
   int c1, c2;
   int had_match;

   if (compl_leader.c == NULL) {
      // First match, use it as a whole.
      compl_leader.c = copySubstr(match->cp_str.c, match->cp_str.len);
      if (compl_leader.c == NULL)
         return;

      compl_leader.len = match->cp_str.len;
      had_match = (curPor->cursor.col > compl_col);
      ins_compl_longest_insert(compl_leader.c);

      // When the match isn't there (to avoid matching itself) remove it
      // again after redrawing.
      if (!had_match)
         ins_compl_delete();
      complUsedMatchS = false;

      return;
   }

   // Reduce the text if this match differs from compl_leader.
   CS p = compl_leader.c;
   CS s = match->cp_str.c;
   while (*p != ZERO) {
      c1 = mb_ptr2char(p);
      c2 = mb_ptr2char(s);
      if ((match->flags & CP_ICASE) ? (MB_TOLOWER(c1) != MB_TOLOWER(c2)) : (c1 != c2))
         break;
      MB_PTR_ADV(p);
      MB_PTR_ADV(s);
   }

   if (*p != ZERO) {
      //Leader was shortened, need to change the inserted text.
      *p = ZERO;
      compl_leader.len = (Unt)(p - compl_leader.c);

      had_match = (curPor->cursor.col > compl_col);
      ins_compl_longest_insert(compl_leader.c);

      //When the match isn't there (to avoid matching itself) remove it again after redrawing.
      if (!had_match)
         ins_compl_delete();
   }

   complUsedMatchS = false;
}

// Add an array of matches to the list of matches. Frees matches[].
private void
ins_compl_add_matches(OUT ExpandMatch* matches, int icase) {
   Unt add_r = OK;
   Unt dir = compl_direction;

   for (Unt i = 0; i < matches->len && add_r != FAIL; i++) {
      add_r = addMatchToList(
         matches->c[i], -1, NULL, NULL, NULL, dir, CP_FAST | (icase ? CP_ICASE : 0), false, NULL,
         FUZZY_SCORE_NONE
      );
      if (add_r == OK)
         // if dir was BACKWARD then honor it just once
         dir = FORWARD;
   }
}

//Make the completion list cyclic. Return the number of matches (excluding the original).
private int
ins_compl_make_cyclic(void) {
   InsertCompletion *match;
   int count = 0;

   if (compl_first_match == NULL)
      return 0;

   // Find the end of the list.
   match = compl_first_match;
   // there's always an entry for the compl_orig_text, it doesn't count.
   while (match->next != NULL && !is_first_match(match->next)) {
      match = match->next;
      ++count;
   }
   match->next = compl_first_match;
   compl_first_match->prev = match;

   return count;
}

// Whether there currently is a shown match.
private int
ins_compl_has_shown_match(void) {
   return compl_shown_match == NULL || compl_shown_match != compl_shown_match->next;
}

// Whether the shown match is long enough.
private int
ins_compl_long_shown_match(void) {
   return (int)compl_shown_match->cp_str.len > curPor->cursor.col - compl_col;
}

// Update the screen and when there is any scrolling remove the popup menu.
private void
ins_compl_upd_pum(void) {
   if (!displayedCompletionsS)
      return;

   Unt h = curPor->cursorLineHeight;
   // Update the screen later, before drawing the popup menu over it.
   pum_callUpdateScreen();
   if (h != curPor->cursorLineHeight)
      ins_compl_del_pum();
}

// Remove any popup menu.
private void
ins_compl_del_pum(void) {
   if (!displayedCompletionsS)
      return;

   pum_undisplay();
   EE_CLEAR(displayedCompletionsS);
}

// Return true if the popup menu should be displayed.
private int
pum_wanted(void) {
   // @completeopt must contain "menu" or "menuone"
   if ((curBook->o.completeOpt & COT_ANY_MENU) == 0 && !compl_autocomplete)
      return false;
   return true;
}

//Return true if there are two or more matches to be shown in the popup menu.
//One if 'completopt' contains "menuone".
private int
pum_enough_matches(void) {
   int i = 0;

   // Don't display the popup menu if there are no matches or there is only
   // one (ignoring the original text).
   InsertCompletion* compl = compl_first_match;
   do {
      if (compl == NULL || (!match_at_original_text(compl) && ++i == 2))
          break;
      compl = compl->next;
   } while (!is_first_match(compl));

   if ((curBook->o.completeOpt & COT_MENUONE) || compl_autocomplete)
      return (i >= 1);
   return (i >= 2);
}

// Allocate Bag for the completed item. { word, abbr, menu, kind, info }
private Bag *
ins_compl_allocBag(InsertCompletion *match) {
   Bag* dict = allocBag_lock(VAR_FIXED);

   bagAddString(dict, S"word", match->cp_str.c);
   bagAddString(dict, S"abbr", match->cp_text[CPT_ABBR]);
   bagAddString(dict, S"menu", match->cp_text[CPT_MENU]);
   bagAddString(dict, S"kind", match->cp_text[CPT_KIND]);
   bagAddString(dict, S"info", match->cp_text[CPT_INFO]);
   if (match->userData.tag == VAR_UNKNOWN)
      bagAddString(dict, S"user_data", (CS)"");
   else
      bagAddVar(dict, S"user_data", &match->userData);

   return dict;
}

// Trigger the CompleteChanged event. Invoked each time the Insert mode completion menu is changed
private void
trigger_complete_changed_event(int cur) {
   static Boole recursive = false;
   SaveVEvent save_v_event;

   if (recursive)
      return;

   Bag* item = cur < 0 ? allocBag() : ins_compl_allocBag(compl_curr_match);
   if (!item)
      return;
   Bag* v_event = get_v_event(&save_v_event);
   bagAddBag(v_event, S"completed_item", item);
   pum_set_event_info(v_event);
   bagSetItemsRo(v_event);

   recursive = true;
   textlock++;
   applyAutocomms(EVENT_COMPLETECHANGED, NULL, NULL, false, curBook);
   textlock--;
   recursive = false;

   restore_v_event(v_event, &save_v_event);
}

// Helper functions for mergesort_list().
private void*
cp_get_next(void *node) {
   return ((InsertCompletion*)node)->next;
}

private void
cp_set_next(void *node, void *next) {
   ((InsertCompletion*)node)->next = (InsertCompletion*)next;
}

private void*
cp_get_prev(void* node) {
   return ((InsertCompletion*)node)->prev;
}

private void
cp_set_prev(void* node, void* prev) {
   ((InsertCompletion*)node)->prev = (InsertCompletion*)prev;
}

private int
cp_compare_fuzzy(const void* a, const void* b) {
   int score_a = ((InsertCompletion*)a)->cp_score;
   int score_b = ((InsertCompletion*)b)->cp_score;
   return (score_b > score_a) ? 1 : (score_b < score_a) ? -1 : 0;
}

private int
cp_compare_nearest(const void* a, const void* b) {
   int score_a = ((InsertCompletion*)a)->cp_score;
   int score_b = ((InsertCompletion*)b)->cp_score;
   if (score_a == FUZZY_SCORE_NONE || score_b == FUZZY_SCORE_NONE)
      return 0;
   return (score_a > score_b) ? 1 : (score_a < score_b) ? -1 : 0;
}

// Constructs a new string by prepending text from the current line (from startcol to compl_col) to 
// the given source string. Stores the result in dest. Returns OK or FAIL.
private Unt
prepend_startcol_text(Text* dest, Text* src, int startcol) {
   int prepend_len = compl_col - startcol;
   int new_length = prepend_len + (int)src->len;

   dest->len = (Unt)new_length;
   dest->c = alloc(new_length + 1);  // +1 for ZERO
   CS line = ml_get(curPor->cursor.lnum);

   MEMMOVE(dest->c, line + startcol, prepend_len);
   MEMMOVE(dest->c + prepend_len, src->c, src->len);
   dest->c[new_length] = ZERO;
   return OK;
}

//Return the completion leader string adjusted for a specific source's
//startcol. If the source's startcol is before compl_col, prepends text from
//the buffer line to the original compl_leader.
private Text*
get_leader_for_startcol(InsertCompletion* match, int cached) {
   static Text adjusted_leader = {null, 0};

   if (!match) {
      EE_CLEAR_STRING(adjusted_leader);
      return NULL;
   }

   if (!cpt_sources_array || !compl_leader.c)
      goto theend;

   int   cpt_idx = match->indexOfSourceInCpt;
   if (cpt_idx < 0 || compl_col <= 0)
      goto theend;
   int startcol = cpt_sources_array[cpt_idx].startCol;

   if (startcol >= 0 && startcol < compl_col) {
      int prepend_len = compl_col - startcol;
      int new_length = prepend_len + (int)compl_leader.len;
      if (cached && (Unt)new_length == adjusted_leader.len && adjusted_leader.c != NULL)
         return &adjusted_leader;

      EE_CLEAR_STRING(adjusted_leader);
      if (prepend_startcol_text(&adjusted_leader, &compl_leader, startcol) != OK)
          goto theend;

      return &adjusted_leader;
   }
theend:
    return &compl_leader;
}

// Set fuzzy score.
private void
set_fuzzy_score(void) {
   InsertCompletion *compl;

   if (!compl_first_match || !compl_leader.c || compl_leader.len == 0)
      return;

   (void)get_leader_for_startcol(NULL, true); // Clear the cache

   compl = compl_first_match;
   do {
      compl->cp_score = fuzzyMatchStr(compl->cp_str.c, get_leader_for_startcol(compl, true)->c);
      compl = compl->next;
   } while (compl != NULL && !is_first_match(compl));
}

// Sort completion matches, excluding the node that contains the leader.
private void
sort_compl_match_list(int (*compare)(const void *, const void *)) {
   InsertCompletion     *compl;

   if (!compl_first_match || is_first_match(compl_first_match->next))
      return;

   compl = compl_first_match->prev;
   ins_compl_make_linear();
   if (compl_shows_dir_forward()) {
      compl_first_match->next->prev = NULL;
      compl_first_match->next = mergesort_list(
         compl_first_match->next, cp_get_next, cp_set_next, cp_get_prev, cp_set_prev, compare
      );
      compl_first_match->next->prev = compl_first_match;
   } else {
      compl->prev->next = NULL;
      compl_first_match = mergesort_list(compl_first_match, cp_get_next,
         cp_set_next, cp_get_prev, cp_set_prev, compare);
      InsertCompletion   *tail = compl_first_match;
      while (tail->next)
         tail = tail->next;
      tail->next = compl;
      compl->prev = tail;
    }
    (void)ins_compl_make_cyclic();
}

//Build a popup menu to show the completion matches.
//Return the popup menu entry that should be selected. Return -1 if nothing should be selected.
private int
ins_compl_build_pum(void) {
   InsertCompletion* compl;
   InsertCompletion* shown_compl = NULL;
   int did_find_shown_match = false;
   int shown_match_ok = false;
   int i = 0;
   int cur = -1;
   Unt cur_cot_flags = curBook->o.completeOpt;
   int compl_no_select = (cur_cot_flags & COT_NOSELECT) != 0 || compl_autocomplete;
   int fuzzy_filter = (cur_cot_flags & COT_FUZZY) != 0;
   InsertCompletion   *match_head = NULL;
   InsertCompletion   *match_tail = NULL;
   InsertCompletion   *matnext = NULL;
   int* match_count = NULL;
   int is_forward = compl_shows_dir_forward();
   int is_cpt_completion = (cpt_sources_array != NULL);
   Text* leader;

   //Need to build the popup menu list.
   displayedCompletionsSsize = 0;

   //If the current match is the original text don't find the first
   //match after it, don't highlight anything.
   if (match_at_original_text(compl_shown_match))
      shown_match_ok = true;

   if (compl_leader.c != NULL
       && STRCMP(compl_leader.c, compl_orig_text.c) == 0
       && shown_match_ok == false
   )
      compl_shown_match = compl_no_select ? compl_first_match : compl_first_match->next;

   if (is_cpt_completion) {
      match_count = ALLOC_CLEAR_MULT(int, cpt_sources_count);
      if (match_count == NULL)
         return -1;
   }

   (void)get_leader_for_startcol(NULL, true); // Clear the cache

   compl = compl_first_match;
   do {
      compl->cp_in_match_array = false;

      // Apply 'smartcase' behavior during normal mode
      if (ctrl_x_mode_normal() && !curBook->o.inferCase && compl_leader.c
            && !ignorecase(compl_leader.c) && !fuzzy_filter)
         compl->flags &= ~CP_ICASE;

      leader = get_leader_for_startcol(compl, true);

      if (!match_at_original_text(compl)
         && (leader->c == NULL || ins_compl_equal(compl, leader->c, (int)leader->len)
             || (fuzzy_filter && compl->cp_score != FUZZY_SCORE_NONE))
      ) {
         // Limit number of items from each source if max_items is set.
         int match_limit_exceeded = false;
         int cur_source = compl->indexOfSourceInCpt;
         if (is_forward && cur_source != -1 && is_cpt_completion) {
            match_count[cur_source]++;
            int max_matches = cpt_sources_array[cur_source].maxMatches;
            if (max_matches > 0 && match_count[cur_source] > max_matches)
               match_limit_exceeded = true;
         }

         if (!match_limit_exceeded) {
            ++displayedCompletionsSsize;
            compl->cp_in_match_array = true;
            if (match_head == NULL)
               match_head = compl;
            else
               match_tail->nextMatch = compl;
            match_tail = compl;

            if (!shown_match_ok && !fuzzy_filter) {
               if (compl == compl_shown_match || did_find_shown_match) {
                  // This item is the shown match or this is the
                  // first displayed item after the shown match.
                  compl_shown_match = compl;
                  did_find_shown_match = true;
                  shown_match_ok = true;
               } else
                  // Remember this displayed match for when the shown match is just below it.
                  shown_compl = compl;
               cur = i;
            } ei (fuzzy_filter) {
               if (i == 0)
                  shown_compl = compl;

               if (!shown_match_ok && compl == compl_shown_match) {
                  cur = i;
                  shown_match_ok = true;
               }
            }
            i++;
         }
      }

      if (compl == compl_shown_match && !fuzzy_filter) {
         did_find_shown_match = true;

         // When the original text is the shown match don't set compl_shown_match.
         if (match_at_original_text(compl))
            shown_match_ok = true;

         if (!shown_match_ok && shown_compl != NULL) {
            // The shown match isn't displayed, set it to the previously displayed match.
            compl_shown_match = shown_compl;
            shown_match_ok = true;
         }
      }
      compl = compl->next;
   } while (compl != NULL && !is_first_match(compl));

   eeglFree(match_count);

   if (displayedCompletionsSsize == 0)
      return -1;

   if (fuzzy_filter && !compl_no_select && !shown_match_ok) {
      compl_shown_match = shown_compl;
      shown_match_ok = true;
      cur = 0;
   }

   displayedCompletionsS = ALLOC_CLEAR_MULT(PopupItem, displayedCompletionsSsize);
   if (!displayedCompletionsS)
      return -1;

   compl = match_head;
   i = 0;
   while (compl) {
      displayedCompletionsS[i].pum_text = compl->cp_text[CPT_ABBR] != NULL
                ? compl->cp_text[CPT_ABBR] : compl->cp_str.c;
      displayedCompletionsS[i].pum_kind = compl->cp_text[CPT_KIND];
      displayedCompletionsS[i].pum_info = compl->cp_text[CPT_INFO];
      displayedCompletionsS[i].pum_cpt_source_idx = compl->indexOfSourceInCpt;
      displayedCompletionsS[i].abbreviationDeco = compl->abbrDeco;
      displayedCompletionsS[i].kindDeco = compl->kindDeco;
      displayedCompletionsS[i].pum_extra = compl->cp_text[CPT_MENU] != NULL
                ? compl->cp_text[CPT_MENU] : compl->fName;
      i++; 
      matnext = compl->nextMatch;
      compl->nextMatch = NULL;
      compl = matnext;
   }

   if (!shown_match_ok)    // no displayed match at all
      cur = -1;

   return cur;
}

// Show the popup menu for the list of matches.
// Also adjusts "compl_shown_match" to an entry that is actually displayed.
pub void
ins_compl_show_pum(void) {
   int i;
   int cur = -1;
   ColNr   col;

   if (!pum_wanted() || !pum_enough_matches())
      return;

   // Update the screen later, before drawing the popup menu over it.
   pum_callUpdateScreen();

   if (displayedCompletionsS == NULL)
      // Need to build the popup menu list.
      cur = ins_compl_build_pum();
   else {
      // popup menu already exists, only need to find the current item.
      for (i = 0; i < displayedCompletionsSsize; ++i) {
         if (displayedCompletionsS[i].pum_text == compl_shown_match->cp_str.c
             || displayedCompletionsS[i].pum_text == compl_shown_match->cp_text[CPT_ABBR]
         ) {
            cur = i;
            break;
         }
      }
   }

   if (!displayedCompletionsS) {
      if (compl_started && has_completechanged())
         trigger_complete_changed_event(cur);
      return;
   }

   // Compute the screen column of the start of the completed text.
   // Use the cursor to get all wrapping and other settings right.
   col = curPor->cursor.col;
   curPor->cursor.col = compl_col;
   compl_selected_item = cur;
   pum_display(displayedCompletionsS, displayedCompletionsSsize, cur);
   curPor->cursor.col = col;

   // After adding leader, set the current match to shown match.
   if (compl_started && compl_curr_match != compl_shown_match)
      compl_curr_match = compl_shown_match;

   if (has_completechanged())
      trigger_complete_changed_event(cur);
}

#define DICT_FIRST   (1)  //use just first element in "dict"
#define DICT_EXACT   (2)  //"dict" is the exact name of a file

// Get current completion leader
pub CS
ins_compl_leader(void) {
   return compl_leader.c ? compl_leader.c : compl_orig_text.c;
}

// Get current completion leader length
private Unt
ins_compl_leader_len(void) {
   return compl_leader.c != NULL ? compl_leader.len : compl_orig_text.len;
}

//Add any identifiers that match the given pattern "pat" in the list of
//dictionary files "dict_start" to the list of completions.
private void
ins_compl_dictionaries(
   NULLABLE CS dict_start,
   CS pat,
   Unt flags,      // DICT_FIRST and/or DICT_EXACT
   int thesaurus   // Thesaurus completion
){
   if (!dict_start)
      return;
      
   CS dict = dict_start;
   CS ptr;
   RegMatch   regmatch;
   Unt dir = compl_direction;

   CS buf = alloc(LSIZE);
   regmatch.regprog = NULL;   // so that we can goto theend

   // If @infercase is set, don't use 'smartcase' here
   Boole smartCaseSaved = p_scs;
   if (curBook->o.inferCase)
      p_scs = false;

   // When invoked to match whole lines for CTRL-X CTRL-L adjust the pattern
   // to only match at the start of a line.  Otherwise just match the
   // pattern. Also need to double backslashes.
   if (ctrl_x_mode_line_or_eval()) {
      CS pat_esc = copyStr_escaped(pat, (CS)"\\");
      if (!pat_esc)
         goto theend;
         
      Unt len = STRLEN(pat_esc) + 10;
      ptr = alloc(len);
      eeSnprintf(ptr, len, "^\\s*\\zs\\V%s", pat_esc);
      regmatch.regprog = compileRegexp(ptr, RE_MAGIC);
      eeglFree(pat_esc);
      eeglFree(ptr);
    } else {
      regmatch.regprog = compileRegexp(pat, RE_MAGIC);
      if (regmatch.regprog == NULL)
         goto theend;
   }

   // ignore case depends on 'ignorecase', 'smartcase' and "pat"
   regmatch.rm_ic = ignorecase(pat);
   ExpandMatch files = {};
   files.a = createArena();
   while (*dict != ZERO && !gotInterruptG && !compl_interrupted) {
      // copy one dictionary file name into buf
      if (flags == DICT_EXACT) {
          files = (ExpandMatch){.c = &dict, .len = 1};
      } else {
         // Expand wildcards in the dictionary name, but do not allow backticks
         strCutPathFromListOfPaths(OUT &dict, OUT buf, LSIZE, S",");
         if (!thesaurus && STRCMP(buf, "spell") == 0)
            files.len = 0;
         ei (firstOccurrence(buf, '`') != NULL
                || expand_wildcards(1, &buf, EW_FILE|EW_SILENT, OUT &files) != OK)
            files.len = 0;
      }

      if (files.len == 0) {
         //Complete from active spelling.  Skip "\<" in the pattern, we don't use it as a RE.
         if (pat[0] == '\\' && pat[1] == '<')
            ptr = pat + 2;
         else
            ptr = pat;
      } else  {  // avoid warning for using "files" uninit
         filterFromFiles(files, thesaurus, flags,
                (cfc_has_mode() ? NULL : &regmatch), buf, OUT &dir);
      }
      if (flags != 0)
         break;
   }
   deleteArena(files.a);

theend:
   p_scs = smartCaseSaved;
   eeRegFree(regmatch.regprog);
   eeglFree(buf);
}

//Add all the words in the line "*buf_arg" from the thesaurus file "fname"
//skipping the word at 'skip_word'.  Returns OK on success.
private Unt
thesaurus_add_words_in_line(CS fname, OUT CS* buf_arg, Unt dir, CS skip_word) {
   Unt status = OK;
   CS wstart;

   // Add the other matches on the line
   CS ptr = *buf_arg;
   while (!gotInterruptG) {
      // Find start of the next word. Skip whitespace and punctuation.
      ptr = findWordStart(ptr);
      if (*ptr == ZERO || *ptr == NL)
          break;
      wstart = ptr;

      // Find end of the word.
      // Japanese words may have characters in different classes, only separate words
      // with single-byte non-word characters.
      while (*ptr != ZERO) {
         int l = utfCharLen(ptr);

         if (l < 2 && !eeIsWordc(*ptr))
             break;
         ptr += l;
      }

      // Add the word. Skip the regexp match.
      if (wstart != skip_word) {
         status = ins_compl_add_infercase(wstart, (int)(ptr - wstart), p_ic,
             fname, dir, false, FUZZY_SCORE_NONE);
         if (status == FAIL)
            break;
      }
   }

   *buf_arg = ptr;
   return status;
}

// Process "count" dictionary/thesaurus "files" and add the text matching "regmatch".
private void
filterFromFiles(
   OUT ExpandMatch files,
   int thesaurus,
   Unt flags,
   RegMatch* regmatch,
   CS buf,
   OUT Unt* dir
) {
   CS ptr;
   FILE* fp;
   Unt add_r;
   CS leader = NULL;
   int leader_len = 0;
   int in_fuzzy_collect = cfc_has_mode();
   int score = 0;
   int len = 0;
   CS line_end = NULL;

   if (in_fuzzy_collect) {
      leader = ins_compl_leader();
      leader_len = (int)ins_compl_leader_len();
   }

   for (Unt i = 0; i < files.len && !gotInterruptG && !ins_compl_interrupted(); i++) {
      fp = FOPEN(files.c[i], "r");  // open dictionary file
      if (flags != DICT_EXACT && !compl_autocomplete) {
         msg_hist_off = true;   // reset in msgTruncDeco()
         eeSnprintf(IObuff, IOSIZE, _("Scanning dictionary: %s"), files.c[i]);
         (void)msgTruncDeco(IObuff, getDecoFlags(HLF_R));
      }

      if (!fp)
         continue;

      // Read dictionary file line by line. Check each line for a match.
      while (!gotInterruptG && !ins_compl_interrupted() && !eeFgets(buf, LSIZE, fp)) {
         ptr = buf;
         if (regmatch) {
            while (eeRegexec(regmatch, buf, (ColNr)(ptr - buf))) {
               ptr = regmatch->startp[0];
               ptr = ctrl_x_mode_line_or_eval() ? find_line_end(ptr) : find_word_end(ptr);
               add_r = ins_compl_add_infercase(
                     regmatch->startp[0], (int)(ptr - regmatch->startp[0]),
                     p_ic, files.c[i], *dir, false, FUZZY_SCORE_NONE
               );
               if (thesaurus) {
                  // For a thesaurus, add all the words in the line
                  ptr = buf;
                  add_r = thesaurus_add_words_in_line(
                        files.c[i], OUT &ptr, *dir, regmatch->startp[0]
                  );
               }
               if (add_r == OK)
                  // if dir was BACKWARD then honor it just once
                  *dir = FORWARD;
               ei (add_r == FAIL)
                  break;
               // avoid expensive call to eeRegexec() when at end of line
               if (*ptr == '\n' || gotInterruptG)
                  break;
            }
         } ei (in_fuzzy_collect && leader_len > 0) {
            line_end = find_line_end(ptr);
            while (ptr < line_end) {
               if (fuzzyMatchStr_in_line(&ptr, leader, &len, NULL, &score)) {
                  CS end_ptr = ctrl_x_mode_line_or_eval() 
                     ? find_line_end(ptr) : find_word_end(ptr);
                  add_r = ins_compl_add_infercase(
                     ptr, (int)(end_ptr - ptr), p_ic, files.c[i], *dir, false, score
                  );
                  if (add_r == FAIL)
                     break;
                  ptr = end_ptr;  // start from next word
                  if (compl_get_longest && ctrl_x_mode_normal()
                        && compl_first_match->next
                        && score == compl_first_match->next->cp_score)
                     complCountBestS++;
               }
            }
         }
         line_breakcheck();
         ins_compl_check_keys(50, false);
      }
      fclose(fp);
   }
}

// Free a completion item in the list
private void
ins_compl_item_free(InsertCompletion* match) {
   EE_CLEAR_STRING(match->cp_str);
   // several entries may use the same fname, free it just once.
   if (match->flags & CP_FREE_FNAME)
      eeglFree(match->fName);
   for (int i = 0; i < CPT_COUNT; ++i)
      eeglFree(match->cp_text[i]);
   clearVar(&match->userData);
   eeglFree(match);
}

// Free the list of completions
private void
ins_compl_free(void) {
   InsertCompletion *match;

   EE_CLEAR_STRING(compl_pattern);
   EE_CLEAR_STRING(compl_leader);

   if (compl_first_match == NULL)
      return;

   ins_compl_del_pum();
   pum_clear();

   compl_curr_match = compl_first_match;
   do {
      match = compl_curr_match;
      compl_curr_match = compl_curr_match->next;
      ins_compl_item_free(match);
   } while (compl_curr_match != NULL && !is_first_match(compl_curr_match));
   compl_first_match = compl_curr_match = NULL;
   compl_shown_match = NULL;
   compl_old_match = NULL;
}

// Reset/clear the completion state.
private void
ins_compl_clear(void){
   compl_cont_status = 0;
   compl_started = false;
   compl_cfc_longest_ins = false;
   compl_matches = 0;
   compl_selected_item = -1;
   compl_ins_end_col = 0;
   compl_curr_win = NULL;
   compl_curr_buf = NULL;
   EE_CLEAR_STRING(compl_pattern);
   EE_CLEAR_STRING(compl_leader);
   editSubmodeExtraMsgG = NULL;
   EE_CLEAR_STRING(compl_orig_text);
   compl_enter_selects = false;
   cpt_sources_clear();
   compl_autocomplete = false;
   compl_from_nonkeyword = false;
   complCountBestS = 0;
   // clear v:completed_item
   set_EeglVar_dict(VV_COMPLETED_ITEM, allocBag_lock(VAR_FIXED));
}

// Return true when Insert completion is active.
pub int
ins_compl_active(void) {
   return compl_started;
}

// Return True when wp is the actual completion window
pub int
ins_compl_win_active(Portal *wp) {
    return ins_compl_active() && wp == compl_curr_win
   && wp->book == compl_curr_buf;
}

// Selected a match. If false, the match was either edited or using the longest common string
private Boole
ins_compl_used_match(void) {
   return complUsedMatchS;
}

// Initialize get longest common string.
private void
ins_compl_init_get_longest(void) {
   compl_get_longest = false;
}

// Return true when insert completion is interrupted.
pub int
ins_compl_interrupted(void) {
   return compl_interrupted || InsertCompletionime_slice_expired;
}

// Return true if the <Enter> key selects a match in the completion popup menu.
private int
ins_compl_enter_selects(void) {
   return compl_enter_selects;
}

// Return the column where the text starts that is being completed
private ColNr
ins_compl_col(void) {
   return compl_col;
}

// Return the length in bytes of the text being completed
pub int
ins_compl_len(void) {
   return compl_length;
}

// Return true when the @completeopt "preinsert" flag is in effect, otherwise return false.
private int
ins_compl_has_preinsert(void) {
   Unt cur_cot_flags = curBook->o.completeOpt;
   return (cur_cot_flags & (COT_PREINSERT | COT_FUZZY | COT_MENUONE))
      == (COT_PREINSERT | COT_MENUONE) && !compl_autocomplete;
}

// Return true if the pre-insert effect is valid and the cursor is within the `compl_ins_end_col`
private int
ins_compl_preinsert_effect(void) {
   if (!ins_compl_has_preinsert())
      return false;
   return curPor->cursor.col < compl_ins_end_col;
}

//Delete one character before the cursor and show the subset of the matches
//that match the word that is now before the cursor.
//Return the character to be used, ZERO if the work is done and another char
//to be got from the user.
private int
ins_compl_bs(void) {
   if (ins_compl_preinsert_effect())
      ins_compl_delete();

   CS line = ml_get_curline();
   CS p = line + curPor->cursor.col;
   MB_PTR_BACK(line, p);

   // Stop completion when the whole word was deleted. For Omni completion
   // allow the word to be deleted, we won't match everything.
   if ((int)(p - line) - (int)compl_col < 0
          || ((int)(p - line) - (int)compl_col == 0 && !ctrl_x_mode_omni())
          || ctrl_x_mode_eval()
   )
      return K_BS;

   // Deleted more than what was used to find matches or didn't finish
   // finding all matches: need to look for matches all over again.
   if (curPor->cursor.col <= compl_col + compl_length || ins_compl_need_restart())
      ins_compl_restart();

   EE_CLEAR_STRING(compl_leader);
   compl_leader.len = (Unt)((p - line) - compl_col);
   compl_leader.c = copySubstr(line + compl_col, compl_leader.len);
   if (compl_leader.c == NULL) {
      compl_leader.len = 0;
      return K_BS;
   }

   ins_compl_new_leader();
   if (compl_shown_match != NULL)
      // Make sure current match is not a hidden item.
      compl_curr_match = compl_shown_match;
   return ZERO;
}

// Return true when we need to find matches again, ins_compl_restart() is to be called.
private int
ins_compl_need_restart(void) {
    // Return true if we didn't complete finding matches or when the
    // 'completefunc' returned "always" in the "refresh" dictionary item.
    return compl_was_interrupted
      || ((ctrl_x_mode_function() || ctrl_x_mode_omni()) && compl_opt_refresh_always);
}

//Called after changing "compl_leader".
//Show the popup menu with a different set of matches.
//May also search for matches again if the previous search was interrupted.
private void
ins_compl_new_leader(void) {
   Unt cur_cot_flags = curBook->o.completeOpt;
   int save_cursorRow = curPor->cursorRow;
   int save_leftCol = curPor->leftCol;

   ins_compl_del_pum();
   ins_compl_delete();
   ins_compl_insert_bytes(compl_leader.c + get_compl_len(), -1);
   complUsedMatchS = false;

   if (p_acl > 0) {
      drawUpdateScreen(UPD_VALID); // Show char (deletion) immediately
      out_flush();
   }

   if (compl_started) {
      ins_compl_set_original_text(compl_leader.c, compl_leader.len);
      if (is_cfn_refresh_always())
         cpt_compl_refresh();
   } else {
      //Matches were cleared, need to search for them now.  Before drawing
      //the popup menu display the changed text before the cursor.  Set
      //"compl_restarting" to avoid that the first match is inserted.
      pum_callUpdateScreen();
      save_cursorRow = curPor->cursorRow;
      save_leftCol = curPor->leftCol;
      compl_restarting = true;
      if (p_ac)
         compl_autocomplete = true;
      if (ins_complete(Ctrl_N, false) == FAIL)
         compl_cont_status = 0;
      compl_restarting = false;
   }

   // When @completeopt contains "fuzzy", set the cp_score and maybe sort
   if (cur_cot_flags & COT_FUZZY) {
      set_fuzzy_score();
      // Sort the matches linked list based on fuzzy score
      if (!(cur_cot_flags & COT_NOSORT)) {
         sort_compl_match_list(cp_compare_fuzzy);
         if ((cur_cot_flags & (COT_NOINSERT | COT_NOSELECT)) == COT_NOINSERT && compl_first_match) {
            compl_shown_match = compl_first_match;
            if (compl_shows_dir_forward())
                compl_shown_match = compl_first_match->next;
         }
      }
   }

   compl_enter_selects = !complUsedMatchS && compl_selected_item != -1;

   // Show the popup menu with a different set of matches.
   if (!compl_interrupted)
      show_pum(save_cursorRow, save_leftCol);

   // Don't let Enter select the original text when there is no popup menu.
   if (displayedCompletionsS == NULL)
      compl_enter_selects = false;
   ei (ins_compl_has_preinsert() && compl_leader.len > 0)
      ins_compl_insert(true);
}

//Return the length of the completion, from the completion start column to
//the cursor column. Making sure it never goes below zero.
private int
get_compl_len(void) {
   int off = (int)curPor->cursor.col - (int)compl_col;
   return MAX(0, off);
}

// Append one character to the match leader.  May reduce the number of matches.
private void
ins_compl_addleader(int c) {
   if (ins_compl_preinsert_effect())
      ins_compl_delete();
   if (stop_arrow() == FAIL)
      return;
      
   int cc;
   if ((cc = mb_char2len(c)) > 1) {
      Byte buf[MB_MAXBYTES + 1];

      mb_char2bytes(c, buf);
      buf[cc] = ZERO;
      opInsertCharBytes(buf, cc, false);
      if (compl_opt_refresh_always)
         AppendToRedobuff(buf);
   } else {
      insertChar(c);
      if (compl_opt_refresh_always)
         AppendCharToRedobuff(c);
   }

   // If we didn't complete finding matches we must search again.
   if (ins_compl_need_restart())
      ins_compl_restart();

   // When 'always' is set, don't reset compl_leader. While completing,
   // cursor doesn't point original position, changing compl_leader would break redo.
   if (!compl_opt_refresh_always) {
      EE_CLEAR_STRING(compl_leader);
      compl_leader.len = (Unt)(curPor->cursor.col - compl_col);
      compl_leader.c = copySubstr(ml_get_curline() + compl_col, compl_leader.len);
      if (!compl_leader.c) {
          compl_leader.len = 0;
          return;
      }

      ins_compl_new_leader();
   }
}

//Setup for finding completions again without leaving CTRL-X mode. Used when
//BS or a key was typed while still searching for matches.
private void
ins_compl_restart(void) {
   ins_compl_free();
   compl_started = false;
   compl_matches = 0;
   compl_cont_status = 0;
   compl_cont_mode = 0;
   cpt_sources_clear();
   compl_autocomplete = false;
   compl_from_nonkeyword = false;
   complCountBestS = 0;
}

//Set the first match, the original text.
private void
ins_compl_set_original_text(CS str, Unt len) {
   // Replace the original text entry.
   // The CP_ORIGINAL_TEXT flag is either at the first item or might possibly
   // be at the last item for backward completion
   if (match_at_original_text(compl_first_match)) {  // safety check
      CS p = copySubstr(str, len);
      EE_CLEAR_STRING(compl_first_match->cp_str);
      compl_first_match->cp_str.c = p;
      compl_first_match->cp_str.len = len;
   } ei (compl_first_match->prev && match_at_original_text(compl_first_match->prev)) {
      CS p = copySubstr(str, len);
      EE_CLEAR_STRING(compl_first_match->prev->cp_str);
      compl_first_match->prev->cp_str.c = p;
      compl_first_match->prev->cp_str.len = len;
   }
}

//Append one character to the match leader.  May reduce the number of matches.
private void
ins_compl_addfrommatch(void) {
   int len = (int)curPor->cursor.col - (int)compl_col;
   int c;

   CS p = compl_shown_match->cp_str.c;
   if ((int)compl_shown_match->cp_str.len <= len)  { // the match is too short
      InsertCompletion* cp;

      // When still at the original match use the first entry that matches the leader.
      if (!match_at_original_text(compl_shown_match))
         return;

      p = NULL;
      Unt plen = 0;
      for (cp = compl_shown_match->next; cp && !is_first_match(cp); cp = cp->next) {
         if (compl_leader.c == NULL || ins_compl_equal(cp, compl_leader.c, (int)compl_leader.len)) {
            p = cp->cp_str.c;
            plen = cp->cp_str.len;
            break;
         }
      }
      if (p == NULL || (int)plen <= len)
          return;
    }
    p += len;
    c = mb_ptr2char(p);
    ins_compl_addleader(c);
}

//Set the CTRL-X completion mode based on the key "c" typed after a CTRL-X.
//Use the global variables: ctrl_x_mode, editSubmodeMsgG, editSubmodePreMsgG,
//compl_cont_mode and compl_cont_status. Return true when the character is not to be inserted.
private Boole
set_ctrl_x_mode(Unt c) {
   switch (c) {
   case Ctrl_E:
   case Ctrl_Y:
      // scroll the window one line up or down
      ctrl_x_mode = CTRL_X_SCROLL;
      editSubmodeMsgG = (CS)_(" (insert) Scroll (^E/^Y)");
      editSubmodePreMsgG = NULL;
      showmode();
      break;
   case Ctrl_L:
       // complete whole line
       ctrl_x_mode = CTRL_X_WHOLE_LINE;
       break;
   case Ctrl_F:
       // complete filenames
       ctrl_x_mode = CTRL_X_FILES;
       break;
   case Ctrl_K:
       // complete words from a dictionary
       ctrl_x_mode = CTRL_X_DICTIONARY;
       break;
   case Ctrl_R:
       // When CTRL-R is followed by '=', don't trigger register completion
       // This allows expressions like <C-R>=func()<CR> to work normally
       if (vpeekc() == '=')
      break;
       ctrl_x_mode = CTRL_X_REGISTER;
       break;
   case Ctrl_T:
       // complete words from a thesaurus
       ctrl_x_mode = CTRL_X_THESAURUS;
       break;
   case Ctrl_U:
       // user defined completion
       ctrl_x_mode = CTRL_X_FUNCTION;
       break;
   case Ctrl_O:
       // omni completion
       ctrl_x_mode = CTRL_X_OMNI;
       break;
   case Ctrl_RSB:
       // complete tag names
       ctrl_x_mode = CTRL_X_TAGS;
       break;
   case Ctrl_I:
   case K_S_TAB:
       // complete keywords from included files
       ctrl_x_mode = CTRL_X_PATH_PATTERNS;
       break;
   case Ctrl_D:
       // complete definitions from included files
       ctrl_x_mode = CTRL_X_PATH_DEFINES;
       break;
   case Ctrl_V:
   case Ctrl_Q:
       // complete Eegl commands
       ctrl_x_mode = CTRL_X_CMDLINE;
       break;
   case Ctrl_Z:
       // stop completion
       ctrl_x_mode = CTRL_X_NORMAL;
       editSubmodeMsgG = NULL;
       showmode();
       return true;
   case Ctrl_P:
   case Ctrl_N:
      // ^X^P means LOCAL expansion if nothing interrupted (eg we just started ^X mode, or there
      // were enough ^X's to cancel the previous mode, say ^X^F^X^X^P or ^P^X^X^X^P, see below)
      // do normal expansion when interrupting a different mode (say ^X^F^X^P or ^P^X^X^P, see 
      // below) nothing changes if interrupting mode 0, (eg, the flag doesn't change when going 
      // to ADDING mode  -- Acevedo
      if (!(compl_cont_status & CONT_INTRPT))
         compl_cont_status |= CONT_LOCAL;
      ei (compl_cont_mode != 0)
         compl_cont_status &= ~CONT_LOCAL;
      // FALLTHROUGH
   default:
      //If we have typed at least 2 ^X's... for modes != 0, we set compl_cont_status = 0 (eg, as 
      //if we had just started ^X mode). For mode 0, we set "compl_cont_mode" to an impossible
      //value, in both cases ^X^X can be used to restart the same mode (avoiding ADDING mode).
      //Undocumented feature: In a mode != 0 ^X^P and ^X^X^P start 'complete' and local ^P 
      //expansions respectively. In mode 0 an extra ^X is needed since ^X^P goes to ADDING mode
      //-- Acevedo
      if (c == Ctrl_X) {
         if (compl_cont_mode != 0)
            compl_cont_status = 0;
         else
            compl_cont_mode = CTRL_X_NOT_DEFINED_YET;
      }
      ctrl_x_mode = CTRL_X_NORMAL;
      editSubmodeMsgG = NULL;
      showmode();
      break;
   }

   return false;
}

// Trigger CompleteDone event and adds relevant information to v:event
private void
trigger_complete_done_event(int mode, CS word) {
   SaveVEvent   save_v_event;
   Bag* v_event = get_v_event(&save_v_event);

   mode = mode & ~CTRL_X_WANT_IDENT;
   CS modeStr = (ctrl_x_mode_names[mode]) ? (CS)ctrl_x_mode_names[mode] : null;

   (void)bagAddString(v_event, S"complete_word", !word ? S"" : word);
   (void)bagAddString(v_event, S"complete_type", modeStr ? modeStr : S"");

   bagSetItemsRo(v_event);
   ins_applyAutocomms(EVENT_COMPLETEDONE);

   restore_v_event(v_event, &save_v_event);
}

// Stop insert completion mode
private int
ins_compl_stop(Unt c, int prev_mode, int retval) {

   // Remove pre-inserted text when present.
   if (ins_compl_preinsert_effect() && ins_compl_win_active(curPor))
      ins_compl_delete();

   // Get here when we have finished typing a sequence of ^N and
   // ^P or other completion characters in CTRL-X mode.  Free up
   // memory that was used, and make sure we can redo the insert.
   if (compl_curr_match || compl_leader.c || c == Ctrl_E) {
      CS ptr = NULL;

      // If any of the original typed text has been changed, eg when ignorecase is set, we must 
      // add back-spaces to the redo buffer. We add as few as necessary to delete just the part
      // of the original text that has changed. When using the longest match, edited the match or 
      // used CTRL-E then don't use the current match.
      if (compl_curr_match != NULL && complUsedMatchS && c != Ctrl_E)
         ptr = compl_curr_match->cp_str.c;
      ins_compl_fixRedoBufForLeader(ptr);
   }

   Boole want_cindent = (can_cindent && doIsIndentationExpressionBased());

   // When completing whole lines: fix indent for 'cindent'.
   // Otherwise, break line if it's too long.
   if (compl_cont_mode == CTRL_X_WHOLE_LINE) {
      // re-indent the current line
      if (want_cindent) {
          do_expr_indent();
          want_cindent = false;   // don't do it again
      }
   } else {
      int prev_col = curPor->cursor.col;

      // put the cursor on the last char, for 'tw' formatting
      if (prev_col > 0)
          dec_cursor();
      // only format when something was inserted
      if (!arrow_used && !needUndoS && c != Ctrl_E)
          insertchar0(ZERO, 0, -1);
      if (prev_col > 0
         && ml_get_curline()[curPor->cursor.col] != ZERO)
          inc_cursor();
    }

   // If the popup menu is displayed pressing CTRL-Y means accepting
   // the selection without inserting anything.  When
   // compl_enter_selects is set the Enter key does the same.
   CS word = NULL;
   if ((c == Ctrl_Y || (compl_enter_selects
          && (c == ENTER || c == K_KENTER || c == NL)))
       && pum_visible()
   ){
      word = copyStr(compl_shown_match->cp_str.c);
      retval = true;
   }

   // CTRL-E means completion is Ended, go back to the typed text.
   // but only do this, if the Popup is still visible
   if (c == Ctrl_E) {
      CS p = NULL;
      Unt   plen = 0;

      ins_compl_delete();
      if (compl_leader.c != NULL) {
         p = compl_leader.c;
         plen = compl_leader.len;
      } ei (compl_first_match != NULL) {
         p = compl_orig_text.c;
         plen = compl_orig_text.len;
      }
      if (p) {
         int compl_len = get_compl_len();

         if ((int)plen > compl_len)
            ins_compl_insert_bytes(p + compl_len, (int)plen - compl_len);
      }
      retval = true;
   }

   auto_format(false, true);

   //Trigger the CompleteDonePre event to give scripts a chance to
   //act upon the completion before clearing the info, and restore
   //ctrl_x_mode, so that complete_info() can be used.
   ctrl_x_mode = prev_mode;
   ins_applyAutocomms(EVENT_COMPLETEDONEPRE);

   ins_compl_free();
   compl_started = false;
   compl_matches = 0;
   msgClearCommline();   // necessary for "noshowmode"
   ctrl_x_mode = CTRL_X_NORMAL;
   compl_enter_selects = false;
   if (editSubmodeMsgG != NULL) {
      editSubmodeMsgG = NULL;
      showmode();
   }
   compl_autocomplete = false;
   compl_from_nonkeyword = false;
   compl_best_matches = 0;

   if (c == Ctrl_C && commPortTypeG != 0)
      // Avoid the popup menu remains displayed when leaving the command line window.
      drawUpdateScreen(0);
   // Trigger the CompleteDone event to give scripts a chance to act upon the end of completion.
   trigger_complete_done_event(prev_mode, word);
   eeglFree(word);

   return retval;
}

// Cancel completion.
private int
ins_compl_cancel(void) {
   return ins_compl_stop(' ', ctrl_x_mode, true);
}

//Prepare for Insert mode completion, or stop it. Called just after typing a character in Insert 
//mode. Return true when the character is not to be inserted;
private Boole
ins_compl_prep(Unt c) {
   Boole retval = false;
   int prev_mode = ctrl_x_mode;

   // Forget any previous 'special' messages if this is actually
   // a ^X mode key - bar ^R, in which case we wait to see what it gives us.
   if (c != Ctrl_R && eeIsCtrlXKey(c))
      editSubmodeExtraMsgG = NULL;

   // Ignore end of mouse scroll/movement.
   if (c == K_MOUSEDOWN || c == K_MOUSEUP
          || c == K_MOUSELEFT || c == K_MOUSERIGHT || c == K_MOUSEMOVE
          || c == K_COMMAND || c == K_SCRIPT_COMMAND)
      return retval;

   // Ignore mouse events in a popup window
   if (is_mouse_key(c)) {
      // Ignore drag and release events, the position does not need to be in
      // the popup and it may have just closed.
      if (c == K_LEFTRELEASE
            || c == K_LEFTRELEASE_NM
            || c == K_MIDDLERELEASE
            || c == K_RIGHTRELEASE
            || c == K_X1RELEASE
            || c == K_X2RELEASE
            || c == K_LEFTDRAG
            || c == K_MIDDLEDRAG
            || c == K_RIGHTDRAG
            || c == K_X1DRAG
            || c == K_X2DRAG
      )
         return retval;
      if (popup_visible) {
         int row = mouseRowG;
         int col = mouseColG;
         Portal* po = mouseFindPortal(&row, &col, FIND_POPUP);

         if (po && PORTAL_IS_POPUP(po))
            return retval;
      }
   }

   if (ctrl_x_mode == CTRL_X_CMDLINE_CTRL_X && c != Ctrl_X) {
      if (c == Ctrl_V || c == Ctrl_Q || c == Ctrl_Z || ins_compl_pum_key(c)
         || !eeIsCtrlXKey(c)
      ) {
         // Not starting another completion mode.
         ctrl_x_mode = CTRL_X_CMDLINE;

         // CTRL-X CTRL-Z should stop completion without inserting anything
         if (c == Ctrl_Z)
            retval = true;
      } else {
         ctrl_x_mode = CTRL_X_CMDLINE;

         // Other CTRL-X keys first stop completion, then start another completion mode.
         ins_compl_prep(' ');
         ctrl_x_mode = CTRL_X_NOT_DEFINED_YET;
      }
   }

   // Set "compl_get_longest" when finding the first matches.
   if (ctrl_x_mode_not_defined_yet() || (ctrl_x_mode_normal() && !compl_started)) {
      compl_get_longest = (curBook->o.completeOpt & COT_LONGEST) != 0;
      complUsedMatchS = true;
   }

   if (ctrl_x_mode_not_defined_yet())
      //We have just typed CTRL-X and aren't quite sure which CTRL-X mode it will be yet.
      //Now we decide.
      retval = set_ctrl_x_mode(c);
   ei (ctrl_x_mode_not_default()) {
      // We're already in CTRL-X mode, do we stay in it?
      if (!eeIsCtrlXKey(c)) {
         ctrl_x_mode = ctrl_x_mode_scroll() ? CTRL_X_NORMAL : CTRL_X_FINISHED;
         editSubmodeMsgG = NULL;
      }
      showmode();
   }

   if (compl_started || ctrl_x_mode == CTRL_X_FINISHED) {
      // Show error message from attempted keyword completion (probably 'Pattern not found') until 
      // another key is hit, then go back to showing what mode we are in.
      showmode();
      if ((ctrl_x_mode_normal() && c != Ctrl_N && c != Ctrl_P
                      && c != Ctrl_R && !ins_compl_pum_key(c)
          ) || ctrl_x_mode == CTRL_X_FINISHED
      )
         retval = ins_compl_stop(c, prev_mode, retval);
   } ei (ctrl_x_mode == CTRL_X_LOCAL_MSG)
      //Trigger the CompleteDone event to give scripts a chance to act
      //upon the (possibly failed) completion.
      trigger_complete_done_event(ctrl_x_mode, NULL);

    may_trigger_modechanged();

   // reset continue_* if we left expansion-mode, if we stay they'll be
   // (re)set properly in ins_complete()
   if (!eeIsCtrlXKey(c)) {
      compl_cont_status = 0;
      compl_cont_mode = 0;
   }

   return retval;
}

//Fix the redo buffer for the completion leader replacing some of the typed
//text. This inserts backspaces and appends the changed text.
//"ptr" is the known leader text or ZERO.
private void
ins_compl_fixRedoBufForLeader(CS ptr_arg) {
    int len = 0;
    CS p;
    CS ptr = ptr_arg;

   if (!ptr) {
      if (compl_leader.c)
         ptr = compl_leader.c;
      else
         return;  // nothing to do
   }
   if (compl_orig_text.c != NULL) {
      p = compl_orig_text.c;
      // Find length of common prefix between original text and new completion
      while (p[len] != ZERO && p[len] == ptr[len])
          len++;
      // Adjust length to not break inside a multi-byte character
      if (len > 0)
          len -= (*mb_head_off)(p, p + len);
      // Add backspace characters for each remaining character in original text
      for (p += len; *p != ZERO; MB_PTR_ADV(p))
          AppendCharToRedobuff(K_BS);
   }
   if (ptr)
      AppendToRedobuffLit(ptr + len, -1);
}

//Loop through the list of portals, loaded-books or non-loaded-books (depending on flag) 
//starting from book and looking for a non-scanned book (other than curBook).  curBook is special:
//if it is called with book=curBook then it has to be the first call for a given flag/expansion.
//Return the book to scan, if any, otherwise returns curBook -- Acevedo
private Book*
ins_compl_next_buf(Book* book, Unt flag) {
   static Portal    *wp = NULL;
   Boole skipBook;

   if (flag == 'w') {     // just portals
      if (book == curBook || !portalIsValid(wp))
         // first call for this flag/expansion or window was closed
         wp = curPor;

      while (true) {
         // Move to next window (wrap to first window if at the end)
         wp = (wp->next) ? wp->next : firstPor;
         // Break if we're back at start or found an unscanned book
         if (wp == curPor || !wp->book->scanned)
            break;
      }
      book = wp->book;
   } else {
      // 'b' (just loaded books), 'u' (just non-loaded books) or 'U' (unlisted books)
      // When completing whole lines skip unloaded books.
      while (true) {
         // Move to next book (wrap to first book if at the end)
         book = (book->next) ? book->next : firstBook;
         // Break if we're back at start book
         if (book == curBook)
            break;

         // Check book conditions based on flag
         if (flag == 'U')
            skipBook = book->o.bookListed;
         else
            skipBook = !book->o.bookListed || bookNoMemfile(book) != (flag == 'u');

         // Break if we found a book that matches our criteria
         if (!skipBook && !book->scanned)
            break;
      }
   }
   return book;
}

//Copy a Callback struct from src to *dest, clearing any existing
//entry and allocating memory for the destination.
private Unt
copyCompletionCbs(OUT Callback* dest, Callback* src) {
   evFreeCallback(dest);

   dest = ALLOC_ONE(Callback);

   if (src->name != NULL && src->name != ZERO)
      evCopyCallback(dest, src);

   return OK;
}

//Parse the @thesaurusfunc value and set the callback function.
//Invoked when the 'thesaurusfunc' option is set. The option value can be a
//name of a function (string), or function(<name>) or funcref(<name>) or a lambda expression.
pub CS
did_set_thesaurusfunc(OptionChange* cha) {
   int retval;
   updateStringRef(cha);

   if (cha->setScope == SET_LOCAL)
      // buffer-local option set
      retval = optSetCallback(OUT curBook->o.thesaurusFn, cha->newVal.string);
   else {
      // global option set
      retval = optSetCallback(OUT &thesaurusCbS, cha->newVal.string);
   }

   return retval == FAIL ? e_invalid_argument : NULL;
}

//Mark the global 'completefunc' 'omnifunc' and 'thesaurusfunc' callbacks with
//"copyID" so that they are not garbage collected.
pub int
set_ref_in_insexpand_funcs(int copyID) {
   return memSetRefInCallback(&completeFnS, copyID)
                 || memSetRefInCallback(&omniFnS, copyID)
                 || memSetRefInCallback(&thesaurusCbS, copyID)
                 || memSetRefInCallback(&customCompleteFnS, copyID);

}

// Get the user-defined completion function name for completion "type"
private CS
get_complete_funcname(int type) {
   switch (type) {
   case CTRL_X_FUNCTION: return curBook->o.completeFn->name;
   case CTRL_X_OMNI: return curBook->o.omniFn->name;
   case CTRL_X_THESAURUS: return curBook->o.thesaurusFn->name;
   default: return S"";
   }
}

// Get the callback to use for insert mode completion.
private Callback*
get_insert_callback(int type) {
   if (type == CTRL_X_FUNCTION)
      return curBook->o.completeFn;
   if (type == CTRL_X_OMNI)
      return curBook->o.omniFn;
   // CTRL_X_THESAURUS
   return curBook->o.thesaurusFn ? curBook->o.thesaurusFn : &thesaurusCbS;
}

//Execute user defined complete function 'completefunc', 'omnifunc' or 'thesaurusfunc', and get 
//matches in "matches". "type" can be one of CTRL_X_OMNI, CTRL_X_FUNCTION, or CTRL_X_THESAURUS.
//Callback function "cb" is set if triggered by a function in the 'cpt' option; otherwise, it's null
private void
expand_by_function(int type, CS base, Callback* cb) {
   List* matchlist = NULL;
   Bag* matchdict = NULL;
   Var args[3];
   int save_State = stateG;
   int is_cfntion = (cb != NULL);

   if (!is_cfntion) {
      CS funcname = get_complete_funcname(type);
      if (*funcname == ZERO)
         return;
      cb = get_insert_callback(type);
   }

   // Call 'completefunc' to obtain the list of matches.
   args[0].tag = VAR_NUMBER;
   args[0].number = 0;
   args[1].tag = VAR_STRING;
   args[1].string = base  ? base : (CS)"";
   args[2].tag = VAR_UNKNOWN;

   Pos pos = curPor->cursor;
   //Lock the text to avoid weird things from happening. Also disallow switching to another portal, 
   //it should not be needed and may end up in Insert mode in a different book.
   ++textlock;

   Var returnVar;
   int retval = call_callback(cb, 0, OUT &returnVar, 2, args);

   // Call a function which returns a list or dict.
   if (retval == OK) {
      switch (returnVar.tag) {
      case VAR_LIST:
         matchlist = returnVar.list;
         break;
      case VAR_BAG:
         matchdict = returnVar.bag;
         break;
      case VAR_SPECIAL:
         if (returnVar.number == VVAL_NONE)
            compl_opt_suppress_empty = true;
         // FALLTHROUGH
      default:
         emsg(_(e_list_or_number_required));
         clearVar(&returnVar);
         break;
      }
   }
   --textlock;

   curPor->cursor = pos;   // restore the cursor position
   check_cursor();  // make sure cursor position is valid, just in case
   validate_cursor();
   if (!EQUAL_POS(curPor->cursor, pos)) {
      emsg(_(e_complete_function_deleted_text));
      goto theend;
   }

   if (matchlist)
      ins_compl_add_list(matchlist);
   ei (matchdict)
      ins_compl_add_dict(matchdict);

theend:
   // Restore stateG, it might have been changed.
   stateG = save_State;

   if (matchdict)
      bagUnref(matchdict);
   if (matchlist)
      list_unref(matchlist);
}

private inline Decoration
getUserDecoration(CS hlname) {
   if (hlname && *hlname != ZERO)
      return decosByHiliteName(hlname);
   return EMPTY_DECO;
}

//Add a match to the list of matches from a typeval_T.
//If the given string is already in the list of completions, then return
//NOTDONE, otherwise add it to the list and return OK.  If there is an error,
//maybe because alloc() returns NULL, then FAIL is returned.
//When "fast" is true use fast_breakcheck() instead of ui_breakcheck().
private int
ins_compl_add_tv(Var* tv, Unt dir, int fast) {
   CS word;
   int      dup = false;
   int      empty = false;
   int      flags = fast ? CP_FAST : 0;
   CS  cptext[CPT_COUNT];
   Var   user_data;
   CS user_abbr_hlname;
   CS user_kind_hlname;
   Decoration userDecos[2] = { EMPTY_DECO, EMPTY_DECO };

   user_data.tag = VAR_UNKNOWN;
   if (tv->tag == VAR_BAG && tv->bag) {
      word = bagGetString(tv->bag, tConst("word"), false);
      cptext[CPT_ABBR] = bagGetString(tv->bag, tConst("abbr"), false);
      cptext[CPT_MENU] = bagGetString(tv->bag, tConst("menu"), false);
      cptext[CPT_KIND] = bagGetString(tv->bag, tConst("kind"), false);
      cptext[CPT_INFO] = bagGetString(tv->bag, tConst("info"), false);

      user_abbr_hlname = bagGetString(tv->bag, tConst("abbr_hlgroup"), false);
      userDecos[0] = getUserDecoration(user_abbr_hlname);

      user_kind_hlname = bagGetString(tv->bag, tConst("kind_hlgroup"), false);
      userDecos[1] = getUserDecoration(user_kind_hlname);

      bagGetVar(tv->bag, tConst("user_data"), &user_data);
      if (bagGetString(tv->bag, tConst("icase"), false) != NULL 
            && bagGetNumber(tv->bag, tConst("icase"))
      )
         flags |= CP_ICASE;
      if (bagGetString(tv->bag, tConst("dup"), false) != NULL)
         dup = bagGetNumber(tv->bag, tConst("dup"));
      if (bagGetString(tv->bag, tConst("empty"), false) != NULL)
         empty = bagGetNumber(tv->bag, tConst("empty"));
      if (bagGetString(tv->bag, tConst("equal"), false) != NULL 
            && bagGetNumber(tv->bag, tConst("equal"))
      )
         flags |= CP_EQUAL;
   } else {
      word = convertVarToStringSingleUse(tv);
      CLEAR_FIELD(cptext);
   }
   if (!word || (!empty && *word == ZERO)) {
      clearVar(&user_data);
      return FAIL;
   }
   Unt status = addMatchToList(word, -1, NULL, cptext,
       &user_data, dir, flags, dup, userDecos, FUZZY_SCORE_NONE);
   if (status != OK)
      clearVar(&user_data);
   return status;
}

// Add completions from a list.
private void
ins_compl_add_list(List* list) {
   ListItem   *li;
   Unt      dir = compl_direction;

   // Go through the List with matches and add each of them.
   CHECK_LIST_MATERIALIZE(list);
   FOR_ALL_LIST_ITEMS(list, li) {
      if (ins_compl_add_tv(&li->c, dir, true) == OK)
         // if dir was BACKWARD then honor it just once
         dir = FORWARD;
      ei (anyEmsgG)
         break;
   }
}

// Add completions from a dict.
private void
ins_compl_add_dict(Bag* dict) {
   // Check for optional "refresh" item.
   compl_opt_refresh_always = false;
   DictItem* di_refresh = bagFind(dict, tConst("refresh"));
   if (di_refresh != NULL && di_refresh->c.tag == VAR_STRING) {
      CS v = di_refresh->c.string;
      if (v && STRCMP(v, (CS)"always") == 0)
         compl_opt_refresh_always = true;
   }

   // Add completions from a "words" list.
   DictItem* di_words = bagFind(dict, tConst("words"));
   if (di_words != NULL && di_words->c.tag == VAR_LIST)
      ins_compl_add_list(di_words->c.list);
}

//Start completion for the complete() function.
//"startcol" is where the matched text starts (1 is first column). "list" is the list of matches.
private void
set_completion(ColNr startcol, List *list) {
   int save_cursorRow = curPor->cursorRow;
   int save_leftCol = curPor->leftCol;
   int flags = CP_ORIGINAL_TEXT;
   Unt cur_cot_flags = curBook->o.completeOpt;
   int compl_longest = (cur_cot_flags & COT_LONGEST) != 0;
   int compl_no_insert = (cur_cot_flags & COT_NOINSERT) != 0;
   int compl_no_select = (cur_cot_flags & COT_NOSELECT) != 0;

   // If already doing completions stop it.
   if (ctrl_x_mode_not_default())
      ins_compl_prep(' ');
   ins_compl_clear();
   ins_compl_free();
   compl_get_longest = compl_longest;

   compl_direction = FORWARD;
   if (startcol > curPor->cursor.col)
      startcol = curPor->cursor.col;
   compl_col = startcol;
   compl_lnum = curPor->cursor.lnum;
   compl_length = (int)curPor->cursor.col - (int)startcol;
   // compl_pattern doesn't need to be set
   compl_orig_text.c = copySubstr(ml_get_curline() + compl_col,
                     (Unt)compl_length);
   if (p_ic)
      flags |= CP_ICASE;
   if (compl_orig_text.c == NULL) {
      compl_orig_text.len = 0;
      return;
   }
   compl_orig_text.len = (Unt)compl_length;
   if (addMatchToList(compl_orig_text.c,
         (int)compl_orig_text.len, NULL, NULL, NULL, 0,
         flags | CP_FAST, false, NULL, FUZZY_SCORE_NONE) != OK
   )
      return;

   ctrl_x_mode = CTRL_X_EVAL;

   ins_compl_add_list(list);
   compl_matches = ins_compl_make_cyclic();
   compl_started = true;
   complUsedMatchS = true;
   compl_cont_status = 0;

   compl_curr_match = compl_first_match;
   int no_select = compl_no_select || compl_longest;
   if (compl_no_insert || no_select) {
      ins_complete(K_DOWN, false);
      if (no_select)
         // Down/Up has no real effect.
         ins_complete(K_UP, false);
   } else
      ins_complete(Ctrl_N, false);
   compl_enter_selects = compl_no_insert;

   // Lazily show the popup menu, unless we got interrupted.
   if (!compl_interrupted)
      show_pum(save_cursorRow, save_leftCol);
   may_trigger_modechanged();
   out_flush();
}

pub void
f_complete(Arr(Var) argvars, Var*) {
   if ((stateG & MODE_INSERT) == 0) {
      emsg(_(e_complete_can_only_be_used_in_insert_mode));
      return;
   }

   // Check for undo allowed here, because if something was already inserted
   // the line was already saved for undo and this check isn't done.
   if (!undo_allowed())
      return;

   if (confirmVarIsNonnullList(argvars, 1) != FAIL) {
      int startcol = (int)varGetNumberChk(argvars, NULL);
      if (startcol > 0)
         set_completion(startcol - 1, argvars[1].list);
   }
}

pub void
f_complete_add(Arr(Var) argvars, Var* returnVar) {
   returnVar->number = ins_compl_add_tv(&argvars[0], 0, false);
}

pub void
f_complete_check(Arr(Var), Var* returnVar) {
   int save_isRedrawingDisabledG = isRedrawingDisabledG;
   isRedrawingDisabledG = 0;

   ins_compl_check_keys(0, true);
   returnVar->number = ins_compl_interrupted();

   isRedrawingDisabledG = save_isRedrawingDisabledG;
}

// Add match item to the return list. Returns FAIL if out of memory, OK otherwise.
private Unt
add_match_to_list( Var  *returnVar, CS str, int len, int pos) {
   List* match = list_alloc();

   Unt ret;
   if ((ret = list_append_number(match, pos + 1)) == FAIL
       || (ret = list_append_string(match, str, len)) == FAIL
       || (ret = list_append_list(returnVar->list, match)) == FAIL
   ) {
      eeglFree(match);
      return FAIL;
   }

   return OK;
}

pub void
f_complete_match(Arr(Var) argvars, Var* returnVar) {
   LineNr lnum;
   ColNr col;
   RegMatch  regmatch;
   CS cur_end = NULL;
   int bytepos = 0;
   Byte part[MAXPATHL];
   int ret;

   allocReturnList(returnVar);

   CS ise = curBook->o.expandTriggers;

   if (argvars[0].tag == VAR_UNKNOWN) {
      lnum = curPor->cursor.lnum;
      col = curPor->cursor.col;
   } ei (argvars[1].tag == VAR_UNKNOWN) {
      emsg(_(e_invalid_argument));
      return;
   } else {
      lnum = (LineNr)tv_get_number(&argvars[0]);
      col = (ColNr)tv_get_number(&argvars[1]);
      if (lnum < 1 || lnum > curBook->mem.lineCount) {
          showErrFmtMsg(_(e_invalid_line_number_nr), lnum);
          return;
      }
      if (col < 1 || col > memGetBookLen(curBook, lnum)) {
          showErrFmtMsg(_(e_invalid_column_number_nr), col + 1);
          return;
      }
   }

   CS line = memGetLine(curBook, lnum, false);
   if (!line)
      return;

   CS before_cursor = copySubstr(line, col);
   if (!before_cursor)
      return;

   if (!ise) {
      regmatch.regprog = compileRegexp((CS)"\\k\\+$", RE_MAGIC);
      if (regmatch.regprog) {
         if (eeRegexec_nl(&regmatch, before_cursor, (ColNr)0)) {
            CS trig = copySubstr(regmatch.startp[0], regmatch.endp[0] - regmatch.startp[0]);
            if (trig == NULL) {
               eeglFree(before_cursor);
               eeRegFree(regmatch.regprog);
               return;
            }

            bytepos = (int)(regmatch.startp[0] - before_cursor);
            ret = add_match_to_list(returnVar, trig, -1, bytepos);
            eeglFree(trig);
            if (ret == FAIL) {
                eeglFree(before_cursor);
                eeRegFree(regmatch.regprog);
                return;
            }
         }
         eeRegFree(regmatch.regprog);
      }
   } else {
      CS p = ise;
      CS p_space = NULL;

      cur_end = before_cursor + (int)STRLEN(before_cursor);

      while (*p != ZERO) {
         int       len = 0;
         if (p_space) {
            len = p - p_space - 1;
            memcpy(part, p_space + 1, len);
            p_space = NULL;
         } else {
            CS next_comma = firstOccurrence((*p == ',') ? p + 1 : p, ',');
            if (next_comma && *(next_comma + 1) == ' ')
               p_space = next_comma;

            len = strCutPathFromListOfPaths(OUT &p, OUT part, MAXPATHL, S",");
         }

         if (len > 0 && len <= col) {
            if (STRNCMP(cur_end - len, part, len) == 0) {
               bytepos = col - len;
               if (add_match_to_list(returnVar, part, len, bytepos) == FAIL) {
                  eeglFree(before_cursor);
                  return;
               }
            }
         }
      }
   }

   eeglFree(before_cursor);
}

// Return Insert completion mode name string
private CS
ins_compl_mode(void) {
   if (ctrl_x_mode_not_defined_yet() || ctrl_x_mode_scroll() || compl_started)
      return (CS)ctrl_x_mode_names[ctrl_x_mode & ~CTRL_X_WANT_IDENT];

   return (CS)"";
}

// Assign the sequence number to all the completion matches which don't have one assigned yet.
private void
ins_compl_update_sequence_numbers(void) {
   int      number = 0;
   InsertCompletion   *match;

   if (compl_dir_forward()) {
      // Search backwards for the first valid (!= -1) number. This should normally succeed already at
      // the first loop cycle, so it's fast!
      for (match = compl_curr_match->prev; match && !is_first_match(match); match = match->prev) {
         if (match->cp_number != -1) {
            number = match->cp_number;
            break;
         }
      } 
      if (match) {
         // go up and assign all numbers which are not assigned yet
         for (match = match->next; match != NULL && match->cp_number == -1; match = match->next)
            match->cp_number = ++number;
      } 
   } else { // BACKWARD
      // Search forwards (upwards) for the first valid (!= -1)
      // number. This should normally succeed already at the first loop cycle, so it's fast!
      for (match = compl_curr_match->next; match && !is_first_match(match); match = match->next) {
         if (match->cp_number != -1) {
            number = match->cp_number;
            break;
         }
      }
      if (match) {
         // go down and assign all numbers which are not assigned yet
         for (match = match->prev; match && match->cp_number == -1; match = match->prev)
            match->cp_number = ++number;
      }
   }
}

// Fill the dict of complete_info
private void
fill_complete_info_dict(Bag *di, InsertCompletion *match, int add_match) {
   bagAddString(di, S"word", match->cp_str.c);
   bagAddString(di, S"abbr", match->cp_text[CPT_ABBR]);
   bagAddString(di, S"menu", match->cp_text[CPT_MENU]);
   bagAddString(di, S"kind", match->cp_text[CPT_KIND]);
   bagAddString(di, S"info", match->cp_text[CPT_INFO]);
   if (add_match)
      bagAdd_bool(di, S"match", match->cp_in_match_array);
   if (match->userData.tag == VAR_UNKNOWN)
      // Add an empty string for backwards compatibility
      bagAddString(di, S"user_data", (CS)"");
   else
      bagAddVar(di, S"user_data", &match->userData);
}

// Get complete information
private void
get_complete_info(List *what_list, Bag *retdict) {
   int      ret = OK;
   ListItem   *item;
#define CI_WHAT_MODE      0x01
#define CI_WHAT_PUM_VISIBLE   0x02
#define CI_WHAT_ITEMS      0x04
#define CI_WHAT_SELECTED   0x08
#define CI_WHAT_COMPLETED   0x10
#define CI_WHAT_MATCHES      0x20
#define CI_WHAT_ALL      0xff
   int      what_flag;

   if (!what_list)
      what_flag = CI_WHAT_ALL & ~(CI_WHAT_MATCHES | CI_WHAT_COMPLETED);
   else {
      what_flag = 0;
      CHECK_LIST_MATERIALIZE(what_list);
      FOR_ALL_LIST_ITEMS(what_list, item) {
         CS what = tv_get_string(&item->c);

         if (STRCMP(what, "mode") == 0)
            what_flag |= CI_WHAT_MODE;
         ei (STRCMP(what, "pum_visible") == 0)
            what_flag |= CI_WHAT_PUM_VISIBLE;
         ei (STRCMP(what, "items") == 0)
            what_flag |= CI_WHAT_ITEMS;
         ei (STRCMP(what, "selected") == 0)
            what_flag |= CI_WHAT_SELECTED;
         ei (STRCMP(what, "completed") == 0)
            what_flag |= CI_WHAT_COMPLETED;
         ei (STRCMP(what, "matches") == 0)
            what_flag |= CI_WHAT_MATCHES;
      }
   }

   if (ret == OK && (what_flag & CI_WHAT_MODE))
      ret = bagAddString(retdict, S"mode", ins_compl_mode());

   if (ret == OK && (what_flag & CI_WHAT_PUM_VISIBLE))
      ret = bagAddNumber(retdict, S"pum_visible", pum_visible());

   if (ret == OK && (what_flag & (CI_WHAT_ITEMS | CI_WHAT_SELECTED
                | CI_WHAT_MATCHES | CI_WHAT_COMPLETED))
   ){
      List       *li = NULL;
      Bag       *di;
      InsertCompletion     *match;
      int         selected_idx = -1;
      int       has_items = what_flag & CI_WHAT_ITEMS;
      int       has_matches = what_flag & CI_WHAT_MATCHES;
      int       has_completed = what_flag & CI_WHAT_COMPLETED;

      if (has_items || has_matches) {
         li = list_alloc();
         ret = bagAddList(retdict, (has_matches && !has_items) ? S"matches" : S"items", li);
      }
      if (ret == OK && what_flag & CI_WHAT_SELECTED)
         if (compl_curr_match != NULL && compl_curr_match->cp_number == -1)
            ins_compl_update_sequence_numbers();
      if (ret == OK && compl_first_match != NULL) {
         int list_idx = 0;
         match = compl_first_match;
         do {
            if (!match_at_original_text(match)) {
               if (has_items || (has_matches && match->cp_in_match_array)) {
                  di = allocBag();
                  ret = listAppendBag(li, di);
                  if (ret != OK)
                     return;
                  fill_complete_info_dict(di, match, has_matches && has_items);
               }
               if (compl_curr_match != NULL
                   && compl_curr_match->cp_number == match->cp_number)
                  selected_idx = list_idx;
               if (!has_matches || match->cp_in_match_array)
                  list_idx++;
            }
            match = match->next;
         }
          while (match != NULL && !is_first_match(match));
      }
      if (ret == OK && (what_flag & CI_WHAT_SELECTED))
          ret = bagAddNumber(retdict, S"selected", selected_idx);

      if (ret == OK && selected_idx != -1 && has_completed) {
          di = allocBag();
          fill_complete_info_dict(di, compl_curr_match, false);
          ret = bagAddBag(retdict, S"completed", di);
      }
   }
}

pub void
f_complete_info(Arr(Var) argvars, Var* returnVar) {
   List   *what_list = NULL;

   allocReturnDict(returnVar);

   if (argvars[0].tag != VAR_UNKNOWN) {
      if (confirmVarIsList(argvars, 0) == FAIL)
         return;
      what_list = argvars[0].list;
   }
   get_complete_info(what_list, returnVar->bag);
}

// Returns true when using a user-defined function for thesaurus completion.
private int
thesaurus_func_complete(int type) {
   return type == CTRL_X_THESAURUS && (curBook->o.thesaurusFn != ZERO);
}

// Check if 'cpt' list index can be advanced to the next completion source.
private int
may_advance_cpt_index(CS cpt) {
   CS p = cpt;

   if (cpt_sources_index == -1)
      return false;
   while (*p == ',' || *p == ' ') // Skip delimiters
      p++;
   return (*p != ZERO);
}

// Return value of process_next_cpt_value()
enum {
   INS_COMPL_CPT_OK = 1,
   INS_COMPL_CPT_CONT,
   INS_COMPL_CPT_END
};

//Process the next 'complete' option value in st->e_cpt.
//
//If successful, the arguments are set as below:
//  st->cpt - pointer to the next option value in "st->cpt"
//  InsertCompletionype_arg - type of insert mode completion to use
//  st->found_all - all matches of this type are found
//  st->scannedBook - search for completions in this buffer
//  st->first_match_pos - position of the first completion match
//  st->last_match_pos - position of the last completion match
//  st->set_match_pos - true if the first match position should be saved to
//            avoid loops after the search wraps around.
//  st->dict - name of the dictionary or thesaurus file to search
//  st->dict_f - flag specifying whether "dict" is an exact file name or not
//
//Return INS_COMPL_CPT_OK if the next value is processed successfully.
//Return INS_COMPL_CPT_CONT to skip the current completion source matching
//the "st->e_cpt" option value and process the next matching source.
//Return INS_COMPL_CPT_END if all the values in "st->e_cpt" are processed.
private int
process_next_cpt_value(
   OUT InsertionCompletionNext* st,
   OUT Unt* InsertCompletionype_arg,
   Pos* start_match_pos,
   int fuzzy_collect,
   OUT int* advance_cpt_idx
){
   Unt insertCompletionType = UNT;
   int status = INS_COMPL_CPT_OK;
   int skip_source = compl_autocomplete && compl_from_nonkeyword;

   st->found_all = false;
   *advance_cpt_idx = false;

   while (*st->e_cpt == ',' || *st->e_cpt == ' ')
      st->e_cpt++;

   if (*st->e_cpt == '.' && !curBook->scanned && !skip_source && !InsertCompletionime_slice_expired) {
      st->scannedBook = curBook;
      st->first_match_pos = *start_match_pos;
      // Move the cursor back one character so that ^N can match the word immediately after 
      // the cursor.
      if (ctrl_x_mode_normal() && (!fuzzy_collect && dec(&st->first_match_pos) < 0)) {
          //Move the cursor to after the last character in the book, so that word at start of 
          //book is found correctly.
          st->first_match_pos.lnum = st->scannedBook->mem.lineCount;
          st->first_match_pos.col = ml_get_len(st->first_match_pos.lnum);
      }
      st->last_match_pos = st->first_match_pos;
      insertCompletionType = 0;

      // Remember the first match so that the loop stops when we
      // wrap and come back there a second time.
      st->set_match_pos = true;
   } ei (!skip_source && !InsertCompletionime_slice_expired
       && firstOccurrence((CS)"buwU", *st->e_cpt) != NULL
       && (st->scannedBook = ins_compl_next_buf(st->scannedBook, *st->e_cpt)) != curBook
   ) {
      // Scan a buffer, but not the current one.
      if (st->scannedBook->mem.mfile != NULL) {  // loaded buffer
         compl_started = true;
         st->first_match_pos.col = st->last_match_pos.col = 0;
         st->first_match_pos.lnum = st->scannedBook->mem.lineCount + 1;
         st->last_match_pos.lnum = 0;
         insertCompletionType = 0;
      } else {  // unloaded buffer, scan like dictionary
         st->found_all = true;
         if (st->scannedBook->currFileName == NULL) {
            status = INS_COMPL_CPT_CONT;
            goto done;
         }
         insertCompletionType = CTRL_X_DICTIONARY;
         st->dict = st->scannedBook->currFileName;
         st->dict_f = DICT_EXACT;
      }
      if (!compl_autocomplete) {
         msg_hist_off = true;   // reset in msgTruncDeco()
         eeSnprintf(IObuff, IOSIZE, _("Scanning: %s"),
             st->scannedBook->currFileName == NULL
            ? bookSpName(st->scannedBook)
            : st->scannedBook->shortFileName == NULL
                ? st->scannedBook->currFileName
                : st->scannedBook->shortFileName);
         (void)msgTruncDeco(IObuff, getDecoFlags(HLF_R));
      }
   } ei (*st->e_cpt == ZERO)
      status = INS_COMPL_CPT_END;
   else {
      if (ctrl_x_mode_line_or_eval())
         insertCompletionType = UNT;
      ei (*st->e_cpt == 'F' || *st->e_cpt == 'o') {
         insertCompletionType = CTRL_X_FUNCTION;
         st->func_cb = get_callback_if_cfn(st->e_cpt);
         if (!st->func_cb)
            insertCompletionType = UNT;
      } ei (!skip_source) {
         if (*st->e_cpt == 'k' || *st->e_cpt == 's') {
            if (*st->e_cpt == 'k')
               insertCompletionType = CTRL_X_DICTIONARY;
            else
               insertCompletionType = CTRL_X_THESAURUS;
            if (*++st->e_cpt != ',' && *st->e_cpt != ZERO) {
                st->dict = st->e_cpt;
                st->dict_f = DICT_FIRST;
            }
         } ei (*st->e_cpt == 'i')
            insertCompletionType = CTRL_X_PATH_PATTERNS;
         ei (*st->e_cpt == 'd')
            insertCompletionType = CTRL_X_PATH_DEFINES;
         ei (*st->e_cpt == ']' || *st->e_cpt == 't') {
            insertCompletionType = CTRL_X_TAGS;
            if (!compl_autocomplete) {
                msg_hist_off = true;   // reset in msgTruncDeco()
                eeSnprintf(IObuff, IOSIZE, _("Scanning tags."));
                (void)msgTruncDeco(IObuff, getDecoFlags(HLF_R));
            }
         } else
            insertCompletionType = UNT;
      }

      // in any case e_cpt is advanced to the next entry
      (void)strCutPathFromListOfPaths(OUT &st->e_cpt, OUT IObuff, IOSIZE, S",");
      *advance_cpt_idx = may_advance_cpt_index(st->e_cpt);

      st->found_all = true;
      if (insertCompletionType == UNT)
          status = INS_COMPL_CPT_CONT;
   }

done:
   *InsertCompletionype_arg = insertCompletionType;
   return status;
}

// Get the next set of identifiers or defines matching "compl_pattern" in included files.
private void
get_next_include_file_completion(Unt insertCompletionType) {
   find_pattern_in_path(
      compl_pattern.c, compl_direction,
      (int)compl_pattern.len, false, false,
      (insertCompletionType == CTRL_X_PATH_DEFINES && !(compl_cont_status & CONT_SOL))
       ? FIND_DEFINE : FIND_ANY, 
      1L, ACTION_EXPAND, (LineNr)1, (LineNr)MAXLNUM, false, compl_autocomplete
   );
}

// Get the next set of words matching "compl_pattern" in dictionary or thesaurus files.
private void
get_next_dict_tsr_completion(int insertCompletionType, CS dict, int dict_f) {
   if (thesaurus_func_complete(insertCompletionType))
      expand_by_function(insertCompletionType, compl_pattern.c, NULL);
   else {
      ins_compl_dictionaries(
         dict 
            ? dict
            : (insertCompletionType == CTRL_X_THESAURUS ? curBook->o.thesaurus : curBook->o.dictionary),
         compl_pattern.c,
         dict ? dict_f : 0,
         insertCompletionType == CTRL_X_THESAURUS
      );
   } 
}

// Get the next set of tag names matching "compl_pattern".
private void
get_next_tag_completion(void) {
   ExpandMatch matches = {};
   matches.a = createArena();

   //set p_ic according to p_ic, p_scs and pat for find_tags().
   int save_p_ic = p_ic;
   p_ic = ignorecase(compl_pattern.c);

   //Find up to TAG_MANY matches. Avoids that an enormous number
   //of matches is found when compl_pattern is empty
   g_tag_at_cursor = true;
   if (find_tags(
         compl_pattern.c,
         TAG_REGEXP | TAG_NAMES | TAG_NOIC | TAG_INS_COMP 
            | (ctrl_x_mode_not_default() ? TAG_VERBOSE : 0),
         TAG_MANY, curBook->fullFileName, OUT &matches
      ) == OK && matches.len > 0
   )
      ins_compl_add_matches(OUT &matches, p_ic);
   deleteArena(matches.a); 
   g_tag_at_cursor = false;
   p_ic = save_p_ic;
}

// insert prefix with redraw
private void
ins_compl_longest_insert(CS prefix) {
   ins_compl_delete();
   ins_compl_insert_bytes(prefix + get_compl_len(), -1);
   redrawInInsertMode(false);
}

//Calculate the longest common prefix among the best fuzzy matches
//stored in compl_best_matches, and insert it as the longest.
private void
fuzzy_longest_match(void) {
   int i = 0;
   int j = 0;
   CS match_str = NULL;
   CS prefix_ptr = NULL;
   CS match_ptr = NULL;
   CS leader = NULL;
   Unt leader_len = 0;
   InsertCompletion   *compl = NULL;
   int more_candidates = false;

   if (complCountBestS == 0)
      return;

   InsertCompletion* nn_compl = compl_first_match->next->next;
   if (nn_compl && nn_compl != compl_first_match)
      more_candidates = true;

   compl = ctrl_x_mode_whole_line() ? compl_first_match : compl_first_match->next;
   if (complCountBestS == 1) {
      // no more candidates insert the match str
      if (!more_candidates) {
          ins_compl_longest_insert(compl->cp_str.c);
          complCountBestS = 0;
      }
      complCountBestS = 0;
      return;
   }

   compl_best_matches = (InsertCompletion **)alloc(complCountBestS * sizeof(InsertCompletion *));
   while (compl != NULL && i < complCountBestS) {
      compl_best_matches[i] = compl;
      compl = compl->next;
      i++;
   }

   CS prefix = compl_best_matches[0]->cp_str.c;
   int prefix_len = (int)compl_best_matches[0]->cp_str.len;

   for (i = 1; i < complCountBestS; i++) {
      match_str = compl_best_matches[i]->cp_str.c;
      prefix_ptr = prefix;
      match_ptr = match_str;
      j = 0;

      while (j < prefix_len && *match_ptr != ZERO && *prefix_ptr != ZERO) {
         if (STRNCMP(prefix_ptr, match_ptr, utfCharLen(prefix_ptr)) != 0)
            break;

         MB_PTR_ADV(prefix_ptr);
         MB_PTR_ADV(match_ptr);
         j++;
      }

      if (j > 0)
         prefix_len = j;
   }

   leader = ins_compl_leader();
   leader_len = ins_compl_leader_len();

   // skip non-consecutive prefixes
   if (leader_len > 0 && STRNCMP(prefix, leader, leader_len) != 0)
      goto end;

   prefix = copySubstr(prefix, prefix_len);
   if (prefix) {
      ins_compl_longest_insert(prefix);
      compl_cfc_longest_ins = true;
      eeglFree(prefix);
   }

end:
   eeglFree(compl_best_matches);
   compl_best_matches = NULL;
   complCountBestS = 0;
}

// Get the next set of filename matching "compl_pattern".
private void
get_next_filename_completion(void) {
   CS ptr;
   CS leader = ins_compl_leader();
   Unt   leader_len = ins_compl_leader_len();;
   int      in_fuzzy_collect = (cfc_has_mode() && leader_len > 0);
   CS last_sep = NULL;
   int need_collect_bests = in_fuzzy_collect && compl_get_longest;
   int max_score = 0;
   int current_score = 0;
   Unt dir = compl_direction;

   if (in_fuzzy_collect) {
      last_sep = lastOccurrence(leader, '/');
      if (last_sep == NULL) {
         // No path separator or separator is the last character,
         // fuzzy match the whole leader
         EE_CLEAR_STRING(compl_pattern);
         compl_pattern.c = copySubstr((CS)"*", 1);
         if (compl_pattern.c == NULL)
            return;
         compl_pattern.len = 1;
      } ei (*(last_sep + 1) == '\0')
         in_fuzzy_collect = false;
      else {
         // Split leader into path and file parts
         int path_len = last_sep - leader + 1;
         CS path_with_wildcard = alloc(path_len + 2);
         eeSnprintf(path_with_wildcard, path_len + 2, "%*.*s*", path_len, path_len, leader);
         EE_CLEAR_STRING(compl_pattern);
         compl_pattern.c = path_with_wildcard;
         compl_pattern.len = path_len + 1;

         // Move leader to the file part
         leader = last_sep + 1;
         leader_len -= path_len;
      }
   }

   ExpandMatch matches = {};
   matches.a = createArena();
   if (expand_wildcards(1, &compl_pattern.c, EW_FILE|EW_DIR|EW_ADDSLASH|EW_SILENT, OUT &matches) 
         != OK) {
      deleteArena(matches.a);
      return;
   } 

   // May change home directory back to "~".
   tilde_replace(compl_pattern.c, OUT &matches);

   if (in_fuzzy_collect) {
      Fuzzy fuzzy = {};
      fuzzy.a = matches.a;

      for (Unt i = 0; i < matches.len; i++) {
         ptr = matches.c[i];
         int score = fuzzyMatchStr(ptr, leader);
         if (score != FUZZY_SCORE_NONE) {
            addFuzzyMatch((FuzzyMatch){.score = score, .str = ptr }, OUT &fuzzy);
         }
      }

      if (fuzzy.len > 0) {
         CS match = NULL;
         fuzzySortByScore(OUT &fuzzy);

         for (Unt i = 0; i < fuzzy.len; ++i) {
            match = matches.c[fuzzy.c[i].idx];
            current_score = compl_fuzzy_scores[fuzzy.c[i].idx];
            if (addMatchToList(match, -1, NULL, NULL, NULL, dir,
                  CP_FAST | ((p_wic) ? CP_ICASE : 0),
                  false, NULL, current_score) == OK
            )
               dir = FORWARD;

            if (need_collect_bests) {
               if (i == 0 || current_score == max_score) {
                  complCountBestS++;
                  max_score = current_score;
               }
            }
         }

      }

      if (complCountBestS > 0 && compl_get_longest)
         fuzzy_longest_match();
      return;
   }

   if (matches.len > 0)
      ins_compl_add_matches(OUT &matches, p_wic);
   deleteArena(matches.a); 
}

// Get the next set of command-line completions matching "compl_pattern".
private void
get_next_cmdline_completion(void) {
   ExpandMatch matches = {};
   if (expandCommline(&compl_xp, compl_pattern.c,
      (int)compl_pattern.len, OUT &matches) == EXPAND_OK
   )
      ins_compl_add_matches(OUT &matches, false);
}

//Return the next word or line from buffer "scannedBook" at position
//"cur_match_pos" for completion. The length of the match is set in "len".
private CS
ins_compl_get_next_word_or_line(
   Book* scannedBook,      // buffer being scanned
   Pos* cur_match_pos,      // current match position
   int* match_len,
   int* cont_s_ipos
) {      // next ^X<> will set initial_pos

   *match_len = 0;
   CS ptr = memGetLine(scannedBook, cur_match_pos->lnum, false) + cur_match_pos->col;
   int len = (int)memGetBookLen(scannedBook, cur_match_pos->lnum) - cur_match_pos->col;
   if (ctrl_x_mode_line_or_eval()) {
      if (compl_status_adding()) {
         if (cur_match_pos->lnum >= scannedBook->mem.lineCount)
            return NULL;
         ptr = memGetLine(scannedBook, cur_match_pos->lnum + 1, false);
         len = memGetBookLen(scannedBook, cur_match_pos->lnum + 1);
         CS tmp_ptr = ptr;

         ptr = skipwhite(tmp_ptr);
         len -= (int)(ptr - tmp_ptr);
      }
   } else {
      CS tmp_ptr = ptr;

      if (compl_status_adding() && compl_length <= len) {
         tmp_ptr += compl_length;
         // Skip if already inside a word.
         if (eeIsWordPtr(tmp_ptr))
            return NULL;
         // Find start of next word.
         tmp_ptr = findWordStart(tmp_ptr);
      }
      // Find end of this word.
      tmp_ptr = find_word_end(tmp_ptr);
      len = (int)(tmp_ptr - ptr);

      if (compl_status_adding() && len == compl_length) {
         if (cur_match_pos->lnum < scannedBook->mem.lineCount) {
            //Try next line, if any. the new word will be "join" as if the normal command "J" was 
            //used. IOSIZE is always greater than compl_length, so the next STRNCPY always
            //works -- Acevedo
            STRNCPY(IObuff, ptr, len);
            ptr = memGetLine(scannedBook, cur_match_pos->lnum + 1, false);
            tmp_ptr = ptr = skipwhite(ptr);
            // Find start of next word.
            tmp_ptr = findWordStart(tmp_ptr);
            // Find end of next word.
            tmp_ptr = find_word_end(tmp_ptr);
            if (tmp_ptr > ptr) {
               if (*ptr != ')' && IObuff[len - 1] != TAB) {
                  if (IObuff[len - 1] != ' ')
                     IObuff[len++] = ' ';
                  // IObuf =~ "\k.* ", thus len >= 2
               }
               // copy as much as possible of the new word
               if (tmp_ptr - ptr >= IOSIZE - len)
                  tmp_ptr = ptr + IOSIZE - len - 1;
               STRNCPY(IObuff + len, ptr, tmp_ptr - ptr);
               len += (int)(tmp_ptr - ptr);
               *cont_s_ipos = true;
            }
            IObuff[len] = ZERO;
            ptr = IObuff;
         }
         if (len == compl_length)
            return NULL;
      }
   }

   *match_len = len;
   return ptr;
}

//Get the next set of words matching "compl_pattern" for default completion(s)
//(normal ^P/^N and ^X^L).
//Search for "compl_pattern" in the buffer "st->scannedBook" starting from the
//position "st->start_pos" in the "compl_direction" direction. If
//"st->set_match_pos" is true, then set the "st->first_match_pos" and "st->last_match_pos".
//Return OK if a new next match is found, otherwise returns FAIL.
private Unt
get_next_default_completion(InsertionCompletionNext* st, Pos* start_pos) {
   Unt found_new_match = FAIL;
   int looped_around = false;
   CS ptr = NULL;
   int len = 0;
   int in_fuzzy_collect = (cfc_has_mode() && compl_length > 0)
      || ((curBook->o.completeOpt & COT_FUZZY) && compl_autocomplete);
   CS leader = ins_compl_leader();
   int score = FUZZY_SCORE_NONE;
   Boole inCurBook = st->scannedBook == curBook;

   // If 'infercase' is set, don't use 'smartcase' here
   Boole smartCaseSaved = p_scs;
   if (st->scannedBook->o.inferCase)
      p_scs = false;

   //Buffers other than curBook are scanned from the beginning or the end but never from the 
   //middle, thus setting nowrapscan in this buffer is a good idea, on the other hand, we always set
   //wrapscan for curBook to avoid missing matches -- Acevedo,Webb
   if (!inCurBook)
      wrapSearchG = false;
   ei (*st->e_cpt == '.')
      wrapSearchG = true;
   looped_around = false;
   for (;;) {
      int   cont_s_ipos = false;
      ++msg_silent;  // Don't want messages for wrapscan.

      if (in_fuzzy_collect) {
         found_new_match = search_for_fuzzy_match(
            st->scannedBook, st->cur_match_pos, leader, compl_direction, start_pos, OUT &len, &ptr,
            &score
         );
      }
      //ctrl_x_mode_line_or_eval() || word-wise search that
      //has added a word that was at the beginning of the line
      ei (ctrl_x_mode_whole_line() || ctrl_x_mode_eval() || (compl_cont_status & CONT_SOL))
         found_new_match = search_for_exact_line(st->scannedBook,
                st->cur_match_pos, compl_direction, compl_pattern.c);
      else
         found_new_match = searchit(NULL, st->scannedBook, st->cur_match_pos,
               NULL, compl_direction, compl_pattern, 
               1L, SEARCH_KEEP + SEARCH_NFMSG, RE_LAST, NULL);
      --msg_silent;
      if (!compl_started || st->set_match_pos) {
         // set "compl_started" even on fail
         compl_started = true;
         st->first_match_pos = *st->cur_match_pos;
         st->last_match_pos = *st->cur_match_pos;
         st->set_match_pos = false;
      } ei (st->first_match_pos.lnum == st->last_match_pos.lnum
         && st->first_match_pos.col == st->last_match_pos.col
      ){
         found_new_match = FAIL;
      } ei (compl_dir_forward()
         && (st->prev_match_pos.lnum > st->cur_match_pos->lnum
             || (st->prev_match_pos.lnum == st->cur_match_pos->lnum
            && st->prev_match_pos.col >= st->cur_match_pos->col)))
      {
         if (looped_around)
            found_new_match = FAIL;
         else
            looped_around = true;
      } ei (!compl_dir_forward()
         && (st->prev_match_pos.lnum < st->cur_match_pos->lnum
             || (st->prev_match_pos.lnum == st->cur_match_pos->lnum
            && st->prev_match_pos.col <= st->cur_match_pos->col)))
      {
         if (looped_around)
            found_new_match = FAIL;
         else
            looped_around = true;
      }
      st->prev_match_pos = *st->cur_match_pos;
      if (found_new_match == FAIL)
         break;

      // when ADDING, the text before the cursor matches, skip it
      if (compl_status_adding() && inCurBook
            && start_pos->lnum == st->cur_match_pos->lnum
            && start_pos->col  == st->cur_match_pos->col)
         continue;

      if (!in_fuzzy_collect)
         ptr = ins_compl_get_next_word_or_line(st->scannedBook, st->cur_match_pos, &len, &cont_s_ipos);
      if (!ptr || (ins_compl_has_preinsert() && STRCMP(ptr, compl_pattern.c) == 0))
         continue;

      if (is_nearest_active() && inCurBook) {
         score = st->cur_match_pos->lnum - curPor->cursor.lnum;
         if (score < 0)
            score = -score;
      }

      if (ins_compl_add_infercase(ptr, len, p_ic,
            inCurBook ? NULL : st->scannedBook->shortFileName,
            0, cont_s_ipos, score) != NOTDONE)
      {
         if (in_fuzzy_collect && score == compl_first_match->next->cp_score)
            complCountBestS++;
         found_new_match = OK;
         break;
      }
   }
   p_scs = smartCaseSaved;
   wrapSearchG = true;

   return found_new_match;
}

//Return the callback function associated with "p" if it refers to a user-defined function in the 
//'complete' option. The "idx" parameter is used for indexing callback entries.
private Callback *
get_callback_if_cfn(CS p) {
   if (*p == 'o')
      return curBook->o.omniFn;

   if (*p == 'F') {
      if (*++p != ',' && *p != ZERO) {
          // Custom completion function 'F{func}' case
          return curBook->o.completeFn->name != NULL ? curBook->o.completeFn : NULL;
      } else
          return curBook->o.completeFn; // @completefunc
   }

   return NULL;
}

//Get completion matches from register contents.
//Extract words from all available registers and adds them to the completion list.
private void
get_register_completion(void) {
   Unt dir = compl_direction;
   YankReg* reg = NULL;
   void* reg_ptr = NULL;
   int adding_mode = compl_status_adding();

   for (int i = 0; i < NUM_REGISTERS; i++) {
      int regname = 0;
      if (i == 0)
         regname = '"';    // unnamed register
      ei (i < 10)
         regname = '0' + i;
      ei (i == DELETION_REGISTER)
         regname = '-';
      ei (i == STAR_REGISTER)
         regname = '*';
      ei (i == PLUS_REGISTER)
         regname = '+';
      else
         regname = 'a' + i - 10;

      // Skip invalid or black hole register
      if (!valid_yank_reg(regname, false) || regname == '_')
         continue;

      reg_ptr = get_register(regname, false);
      if (reg_ptr == NULL)
         continue;

      reg = (YankReg *)reg_ptr;

      for (int j = 0; j < reg->y_size; j++) {
         CS str = reg->y_array[j].c;
         if (!str)
            continue;

         if (adding_mode) {
            int str_len = (int)STRLEN(str);
            if (str_len == 0)
                continue;

            if (!compl_orig_text.c
               || (p_ic ? STRNICMP(str, compl_orig_text.c, compl_orig_text.len) == 0
                  : STRNCMP(str, compl_orig_text.c, compl_orig_text.len) == 0)
            ){
               if (ins_compl_add_infercase(str, str_len, p_ic, NULL,
                     dir, false, FUZZY_SCORE_NONE) == OK)
                  dir = FORWARD;
            }
         } else {
            // Calculate the safe end of string to avoid null byte issues
            CS str_end = str + STRLEN(str);
            CS p = str;

            // Safely iterate through the string
            while (p < str_end && *p != ZERO) {
               CS old_p = p;
               p = findWordStart(p);
               if (p >= str_end || *p == ZERO)
                  break;

               CS word_end = find_word_end(p);

               if (word_end <= p) {
                  word_end = p + utfCharLen(p);
               }

               if (word_end > str_end)
                  word_end = str_end;

               int len = (int)(word_end - p);
               if (len > 0 && (!compl_orig_text.c
                  || (p_ic ? STRNICMP(p, compl_orig_text.c,
                            compl_orig_text.len) == 0
                     : STRNCMP(p, compl_orig_text.c,
                            compl_orig_text.len) == 0))
               ) {
                  if (ins_compl_add_infercase(p, len, p_ic, NULL,
                         dir, false, FUZZY_SCORE_NONE) == OK)
                      dir = FORWARD;
               }

               p = word_end;

               if (p <= old_p) {
                  p = old_p + 1;
                  if (p < str_end)
                     p = old_p + utfCharLen(old_p);
               }
            }
         }
      }

      // Free the register copy
      put_register(regname, reg_ptr);
   }
}

//get the next set of completion matches for "type". true if a new match is found. otherwise false
private Unt
get_next_completion_match(int type, InsertionCompletionNext *st, Pos *ini) {
   Unt found_new_match = FAIL;

   switch (type) {
   case -1:
       break;
   case CTRL_X_PATH_PATTERNS:
   case CTRL_X_PATH_DEFINES:
       get_next_include_file_completion(type);
       break;

   case CTRL_X_DICTIONARY:
   case CTRL_X_THESAURUS:
       get_next_dict_tsr_completion(type, st->dict, st->dict_f);
       st->dict = NULL;
       break;

   case CTRL_X_TAGS:
       get_next_tag_completion();
       break;

   case CTRL_X_FILES:
       get_next_filename_completion();
       break;

   case CTRL_X_CMDLINE:
   case CTRL_X_CMDLINE_CTRL_X:
       get_next_cmdline_completion();
       break;

   case CTRL_X_FUNCTION:
       if (ctrl_x_mode_normal())  // Invoked by a func in 'cpt' option
      get_cfn_completion_matches(st->func_cb);
       else
      expand_by_function(type, compl_pattern.c, NULL);
       break;
   case CTRL_X_OMNI:
       expand_by_function(type, compl_pattern.c, NULL);
       break;

   case CTRL_X_REGISTER:
       get_register_completion();
       break;

   default:   // normal ^P/^N and ^X^L
      found_new_match = get_next_default_completion(st, ini);
      if (found_new_match == FAIL && st->scannedBook == curBook)
         st->found_all = true;
   }

   // check if compl_curr_match has changed, (e.g. other type of expansion added something)
   if (type != 0 && compl_curr_match != compl_old_match)
      found_new_match = OK;

   return found_new_match;
}

// Strip carets followed by numbers. This suffix typically represents the max_matches setting
private void
strip_caret_numbers_in_place(CS str) {
   if (!str)
      return;

   CS read = str;
   CS write = str;
   CS p;
   while (*read) {
      if (*read == '^') {
         p = read + 1;
         while (eeIsDigit(*p))
            p++;
         if ((*p == ',' || *p == '\0') && p != read + 1) {
            read = p;
            continue;
         } else
            *write++ = *read++;
      } else
         *write++ = *read++;
   }
   *write = '\0';
}

// Call functions specified in the 'cpt' option with findstart=1, and retrieve the startcol.
private int
prepare_cpt_compl_funcs(void) {
   Callback* cb = NULL;
   int idx = 0;
   int startcol;

   // Make a copy of 'cpt' in case the buffer gets wiped out
   CS cpt = copyStr(curBook->o.complete);
   strip_caret_numbers_in_place(cpt);

   for (CS p = cpt; *p;) {
      while (*p == ',' || *p == ' ') // Skip delimiters
          p++;
      if (*p == ZERO)
          break;

      cb = get_callback_if_cfn(p);
      if (cb) {
         if (get_userdefined_compl_info(curPor->cursor.col, cb, &startcol) == FAIL) {
            if (startcol == -3)
               cpt_sources_array[idx].refreshAlways = false;
            else
               startcol = -2;
         } ei (startcol < 0 || startcol > curPor->cursor.col)
            startcol = curPor->cursor.col;
         cpt_sources_array[idx].startCol = startcol;
      } else
         cpt_sources_array[idx].startCol = -3;

      (void)strCutPathFromListOfPaths(OUT &p, OUT IObuff, IOSIZE, S","); // Advance p
      idx++;
   }

   eeglFree(cpt);
   return OK;
   return FAIL;
}

// Start the timer for the current completion source.
private void
compl_source_start_timer(int source_idx) {
   if (compl_autocomplete && cpt_sources_array) {
      ELAPSED_INIT(cpt_sources_array[source_idx].matchCollectionStart);
      InsertCompletionime_slice_expired = false;
   }
}

// Safely advance the cpt_sources_index by one.
private int
advance_cpt_sources_index_safe(void) {
   if (cpt_sources_index >= 0 && cpt_sources_index < cpt_sources_count - 1) {
      cpt_sources_index++;
      return OK;
   }
   showErrFmtMsg(_(e_list_index_out_of_range_nr), cpt_sources_index);
   return FAIL;
}

#define COMPL_FUNC_TIMEOUT_MS      300
#define COMPL_FUNC_TIMEOUT_NON_KW_MS   1000
//Get the next expansion(s), using "compl_pattern".
//The search starts at position "ini" in curBook and in the direction compl_direction.
//When "compl_started" is false start at that position, otherwise continue
//where we stopped searching before. This may return before finding all the matches.
//Return the total number of matches or -1 if still unknown -- Acevedo
private int
ins_compl_get_exp(Pos* ini) {
   static InsertionCompletionNext   st;
   static int             st_cleared = false;
   int match_count;
   Unt found_new_match;
   Unt type = ctrl_x_mode;
   int may_advance_cpt_idx = false;
   Pos start_pos = *ini;

   if (!compl_started) {
      Book* book;

      FOR_ALL_BOOKS(book)
         book->scanned = 0;
      if (!st_cleared) {
         CLEAR_FIELD(st);
         st_cleared = true;
      }
      st.found_all = false;
      st.scannedBook = curBook;
      eeglFree(st.e_cpt_copy);
      // Make a copy of 'complete', in case the buffer is wiped out.
      st.e_cpt_copy = copyStr((compl_cont_status & CONT_LOCAL) ? S"." : curBook->o.complete);
      strip_caret_numbers_in_place(st.e_cpt_copy);
      st.e_cpt = st.e_cpt_copy == NULL ? (CS)"" : st.e_cpt_copy;

      // In large buffers, timeout may miss nearby matches — search above cursor
#define LOOKBACK_LINE_COUNT   1000
      if (compl_autocomplete && is_nearest_active()) {
          start_pos.lnum = MAX(1, start_pos.lnum - LOOKBACK_LINE_COUNT);
          start_pos.col = 0;
      }
      st.last_match_pos = st.first_match_pos = start_pos;
   } ei (st.scannedBook != curBook && !bookIsValid(st.scannedBook))
      st.scannedBook = curBook;  // In case the buffer was wiped out.

   compl_old_match = compl_curr_match;   // remember the last current match
   st.cur_match_pos = (compl_dir_forward()) ? &st.last_match_pos : &st.first_match_pos;

   if (cpt_sources_array != NULL && ctrl_x_mode_normal()
       && !ctrl_x_mode_line_or_eval()
       && !(compl_cont_status & CONT_LOCAL)
   ){
      cpt_sources_index = 0;
      if (compl_autocomplete) {
         compl_source_start_timer(0);
         InsertCompletionimeout_ms = COMPL_INITIAL_TIMEOUT_MS;
      }
   }

   // For ^N/^P loop over all the flags/windows/buffers in 'complete'.
   for (;;) {
      found_new_match = FAIL;
      st.set_match_pos = false;

      // For ^N/^P pick a new entry from e_cpt if compl_started is off,
      // or if found_all says this entry is done.  For ^X^L only use the
      // entries from 'complete' that look in loaded buffers.
      if ((ctrl_x_mode_normal() || ctrl_x_mode_line_or_eval())
                  && (!compl_started || st.found_all))
      {
         int status = process_next_cpt_value(OUT &st, OUT &type, &start_pos,
             cfc_has_mode(), OUT &may_advance_cpt_idx);

         if (status == INS_COMPL_CPT_END)
            break;
         if (status == INS_COMPL_CPT_CONT) {
            if (may_advance_cpt_idx) {
               if (!advance_cpt_sources_index_safe())
                  break;
               compl_source_start_timer(cpt_sources_index);
            }
            continue;
         }
      }

      if (compl_autocomplete && type == CTRL_X_FUNCTION)
         // LSP servers may sporadically take >1s to respond (e.g., while loading modules), but 
         // other sources might already have matches. To show results quickly use a short timeout
         // for keyword completion. Allow longer timeout for non-keyword completion
         // where only function based sources (e.g. LSP) are active.
         InsertCompletionimeout_ms = compl_from_nonkeyword
         ? COMPL_FUNC_TIMEOUT_NON_KW_MS : COMPL_FUNC_TIMEOUT_MS;

      // get the next set of completion matches
      found_new_match = get_next_completion_match(type, &st, &start_pos);

      // If complete() was called then compl_pattern has been reset.  The
      // following won't work then, bail out.
      if (compl_pattern.c == NULL)
         break;

      if (may_advance_cpt_idx) {
         if (!advance_cpt_sources_index_safe())
            break;
         compl_source_start_timer(cpt_sources_index);
      }

      // break the loop for specialized modes (use 'complete' just for the
      // generic ctrl_x_mode == CTRL_X_NORMAL) or when we've found a new match
      if ((ctrl_x_mode_not_default() && !ctrl_x_mode_line_or_eval()) || found_new_match != FAIL) {
         if (gotInterruptG)
            break;
         // Fill the popup menu as soon as possible.
         if (type != UNT)
            ins_compl_check_keys(0, false);

         if ((ctrl_x_mode_not_default() && !ctrl_x_mode_line_or_eval()) || compl_interrupted)
            break;
         compl_started = InsertCompletionime_slice_expired ? false : true;
      } else {
         // Mark a buffer scanned when it has been scanned completely
         if (bookIsValid(st.scannedBook) && (type == 0 || type == CTRL_X_PATH_PATTERNS))
            st.scannedBook->scanned = true;

         compl_started = false;
      }

      // Reset the timeout after collecting matches from function source
      if (compl_autocomplete && type == CTRL_X_FUNCTION)
          InsertCompletionimeout_ms = COMPL_INITIAL_TIMEOUT_MS;

      // For `^P` completion, reset `compl_curr_match` to the head to avoid
      // mixing matches from different sources.
      if (!compl_dir_forward()) {
         while (compl_curr_match->prev && !match_at_original_text(compl_curr_match->prev))
            compl_curr_match = compl_curr_match->prev;
      } 
   }
   cpt_sources_index = -1;
   compl_started = true;

   if ((ctrl_x_mode_normal() || ctrl_x_mode_line_or_eval()) && *st.e_cpt == ZERO)
      found_new_match = FAIL;      // Got to end of @complete

   match_count = -1;      // total of matches, unknown
   if (found_new_match == FAIL || (ctrl_x_mode_not_default() && !ctrl_x_mode_line_or_eval()))
      match_count = ins_compl_make_cyclic();

   if (cfc_has_mode() && compl_get_longest && complCountBestS > 0)
      fuzzy_longest_match();

   if (compl_old_match) {
      // If several matches were added (FORWARD) or the search failed and has
      // just been made cyclic then we have to move compl_curr_match to the
      // next or previous entry (if any) -- Acevedo
      compl_curr_match = compl_dir_forward() ? compl_old_match->next : compl_old_match->prev;
      if (compl_curr_match == NULL)
          compl_curr_match = compl_old_match;
   }
   may_trigger_modechanged();

   if (is_nearest_active())
      sort_compl_match_list(cp_compare_nearest);

   return match_count;
}

//Update "compl_shown_match" to the actually shown match, it may differ when
//"compl_leader" is used to omit some of the matches.
private void
ins_compl_update_shown_match(void) {
   (void)get_leader_for_startcol(NULL, true); // Clear the cache
   Text* leader = get_leader_for_startcol(compl_shown_match, true);

   while (!ins_compl_equal(compl_shown_match,
      leader->c, (int)leader->len)
       && compl_shown_match->next != NULL
       && !is_first_match(compl_shown_match->next)
   ){
      compl_shown_match = compl_shown_match->next;
      leader = get_leader_for_startcol(compl_shown_match, true);
   }

   // If we didn't find it searching forward, and compl_shows_dir is
   // backward, find the last match.
   if (compl_shows_dir_backward()
       && !ins_compl_equal(compl_shown_match, leader->c, (int)leader->len)
       && (compl_shown_match->next == NULL || is_first_match(compl_shown_match->next))
   ) {
      while (!ins_compl_equal(compl_shown_match, leader->c, (int)leader->len)
            && compl_shown_match->prev != NULL
            && !is_first_match(compl_shown_match->prev)
      ) {
         compl_shown_match = compl_shown_match->prev;
         leader = get_leader_for_startcol(compl_shown_match, true);
      }
   }
}

// Delete the old text being completed.
private void
ins_compl_delete(void) {
   // In insert mode: Delete the typed part.
   // In replace mode: Put the old characters back, if any.
   int col = compl_col + (compl_status_adding() ? compl_length : 0);
   Text   remaining = {NULL, 0};
   int       orig_col;
   int   has_preinsert = ins_compl_preinsert_effect();
   if (has_preinsert) {
      col += (int)ins_compl_leader_len();
      curPor->cursor.col = compl_ins_end_col;
   }

   if (curPor->cursor.lnum > compl_lnum) {
      if (curPor->cursor.col < ml_get_curline_len()) {
         CS line = ml_get_cursor();
         remaining.len = ml_get_cursor_len();
         remaining.c = copySubstr(line, remaining.len);
         if (!remaining.c)
            return;
      }
      while (curPor->cursor.lnum > compl_lnum) {
         if (ml_delete(curPor->cursor.lnum) == FAIL) {
            if (remaining.c)
               eeglFree(remaining.c);
            return;
         }
         deleted_lines_mark(curPor->cursor.lnum, 1L);
         curPor->cursor.lnum--;
      }
      // move cursor to end of line
      curPor->cursor.col = ml_get_curline_len();
   }

   if ((int)curPor->cursor.col > col) {
      if (stop_arrow() == FAIL) {
         if (remaining.c)
            eeglFree(remaining.c);
         return;
      }
      backspace_until_column(col);
      compl_ins_end_col = curPor->cursor.col;
   }

   if (remaining.c) {
      orig_col = curPor->cursor.col;
      ins_str(remaining.c, remaining.len);
      curPor->cursor.col = orig_col;
      eeglFree(remaining.c);
   }
   //TODO: is this sufficient for redrawing?  Redrawing everything causes
   //flicker, thus we can't do that.
   changed_cline_bef_curs();
   // clear v:completed_item
   set_EeglVar_dict(VV_COMPLETED_ITEM, allocBag_lock(VAR_FIXED));
}

//Insert a completion string that contains newlines. The string is split and inserted line by line.
private void
ins_compl_expand_multiple(CS str) {
   CS  start = str;
   CS curr = str;
   int base_indent = get_indent();

   while (*curr != ZERO) {
      if (*curr == '\n') {
         // Insert the text chunk before newline
         if (curr > start)
            opInsertCharBytes(start, (int)(curr - start), false);

         // Handle newline
         openLine(OPENLINE_KEEPTRAIL | OPENLINE_FORCE_INDENT, base_indent);
         start = curr + 1;
      }
      curr++;
   }

   // Handle remaining text after last newline (if any)
   if (curr > start)
      opInsertCharBytes(start, (int)(curr - start), false);

   compl_ins_end_col = curPor->cursor.col;
}

//Insert the new text being completed.
//"move_cursor" is used when 'completeopt' includes "preinsert" and when true
//cursor needs to move back from the inserted text to the compl_leader.
private void
ins_compl_insert(int move_cursor) {
   int compl_len = get_compl_len();
   int preinsert = ins_compl_has_preinsert();
   CS cp_str = compl_shown_match->cp_str.c;
   Unt cp_str_len = compl_shown_match->cp_str.len;
   Unt leader_len = ins_compl_leader_len();
   CS has_multiple = firstOccurrence(cp_str, '\n');

   // Since completion sources may provide matches with varying start positions, insert only the 
   // portion of the match that corresponds to the intended replacement range
   if (cpt_sources_array) {
      int cpt_idx = compl_shown_match->indexOfSourceInCpt;
      if (cpt_idx >= 0 && compl_col >= 0) {
         int startcol = cpt_sources_array[cpt_idx].startCol;
         if (startcol >= 0 && startcol < (int)compl_col) {
            int skip = (int)compl_col - startcol;
            if ((Unt)skip <= cp_str_len) {
               cp_str_len -= skip;
               cp_str += skip;
            }
         }
      }
   }

   // Make sure we don't go over the end of the string, this can happen with illegal bytes.
   if (compl_len < (int)cp_str_len) {
      if (has_multiple)
         ins_compl_expand_multiple(cp_str + compl_len);
      else {
         ins_compl_insert_bytes(cp_str + compl_len, -1);
         if (preinsert && move_cursor)
            curPor->cursor.col -= (ColNr)(cp_str_len - leader_len);
      }
   }
   if (match_at_original_text(compl_shown_match) || preinsert)
      complUsedMatchS = false;
   else
      complUsedMatchS = true;
   Bag *bag = ins_compl_allocBag(compl_shown_match);

   set_EeglVar_dict(VV_COMPLETED_ITEM, bag);
}

// show the file name for the completion match (if any). Truncate the file name to avoid a wait 
// for return
private void
ins_compl_show_filename(void) {
   CS  lead = _("match in file");
   int      space = shownCommandColG - eeglStrSize((CS)lead) - 2;
   CS s;
   CS e;

   if (space <= 0)
      return;

   // We need the tail that fits.  With double-byte encoding going back from the end is very slow,
   // thus go from the start and keep the text that fits in "space" between "s" and "e".
   for (s = e = compl_shown_match->fName; *e != ZERO; MB_PTR_ADV(e)) {
      space -= bookPtr2Cells(e);
      while (space < 0) {
         space += bookPtr2Cells(s);
         MB_PTR_ADV(s);
      }
   }
   msg_hist_off = true;
   eeSnprintf( IObuff, IOSIZE, "%s %s%s", lead, s > compl_shown_match->fName ? "<" : "", s);
   msg(IObuff);
   msg_hist_off = false;
   redrawCommlineG = false;       // don't overwrite!
}

//Find the appropriate completion item when 'complete' ('cpt') includes a 'max_matches' postfix. 
//In this case, we search for a match where 'cp_in_match_array' is set, indicating that the match 
//is also present in 'displayedCompletionsS'.
private InsertCompletion *
find_next_match_in_menu(void) {
   int       is_forward = compl_shows_dir_forward();
   InsertCompletion *match = compl_shown_match;

   do
      match = is_forward ? match->next : match->prev;
   while (match->next && !match->cp_in_match_array && !match_at_original_text(match));
   return match;
}

//Find the next set of matches for completion. Repeat the completion "todo"
//times. The number of matches found is returned in 'num_matches'.
//
//If "allow_get_expansion" is true, then ins_compl_get_exp() may be called to get more completions.
//If it is false, then do nothing when there are no more completions in the given direction.
//
//If "advance" is true, then completion will move to the first match.
//Otherwise, the original text will be shown.
//
//Return OK on success and FAIL if the number of matches are unknown.
private Unt
find_next_completion_match(
   int allow_get_expansion,
   int todo,      // repeat completion this many times
   int advance,
   int* num_matches
) {
   Boole  found_end = false;
   InsertCompletion   *found_compl = NULL;
   Unt cur_cot_flags = curBook->o.completeOpt;
   int compl_no_select = (cur_cot_flags & COT_NOSELECT) != 0 || compl_autocomplete;
   int compl_fuzzy_match = (cur_cot_flags & COT_FUZZY) != 0;
   Text* leader;

   while (--todo >= 0) {
      if (compl_shows_dir_forward() && compl_shown_match->next != NULL) {
         if (displayedCompletionsS != NULL)
            compl_shown_match = find_next_match_in_menu();
         else
            compl_shown_match = compl_shown_match->next;
         found_end = (compl_first_match != NULL
             && (is_first_match(compl_shown_match->next) || is_first_match(compl_shown_match))
         );
      } ei (compl_shows_dir_backward() && compl_shown_match->prev != NULL) {
         found_end = is_first_match(compl_shown_match);
         if (displayedCompletionsS != NULL)
            compl_shown_match = find_next_match_in_menu();
         else
            compl_shown_match = compl_shown_match->prev;
         found_end |= is_first_match(compl_shown_match);
      } else {
         if (!allow_get_expansion) {
            if (advance) {
               if (compl_shows_dir_backward())
                  compl_pending -= todo + 1;
               else
                  compl_pending += todo + 1;
            }
            return FAIL;
         }

         if (!compl_no_select && advance) {
            if (compl_shows_dir_backward())
               --compl_pending;
            else
               ++compl_pending;
         }

         // Find matches.
         *num_matches = ins_compl_get_exp(&compl_startpos);

         // handle any pending completions
         while (compl_pending != 0 && compl_direction == compl_shows_dir && advance) {
            if (compl_pending > 0 && compl_shown_match->next != NULL) {
               compl_shown_match = compl_shown_match->next;
               --compl_pending;
            }
            if (compl_pending < 0 && compl_shown_match->prev != NULL) {
               compl_shown_match = compl_shown_match->prev;
               ++compl_pending;
            } else
               break;
         }
         found_end = false;
      }

      leader = get_leader_for_startcol(compl_shown_match, false);

      if (!match_at_original_text(compl_shown_match)
            && leader->c
            && !ins_compl_equal(compl_shown_match, leader->c, (int)leader->len)
            && !(compl_fuzzy_match && compl_shown_match->cp_score != FUZZY_SCORE_NONE))
         ++todo;
      else
         // Remember a matching item.
         found_compl = compl_shown_match;

      // Stop at the end of the list when we found a usable match.
      if (found_end) {
         if (found_compl) {
            compl_shown_match = found_compl;
            break;
         }
         todo = 1;       // use first usable match after wrapping around
      }
   }

   return OK;
}

//Fill in the next completion in the current direction.
//If "allow_get_expansion" is true, then we may call ins_compl_get_exp() to get more completions. 
//If it is false, then we just do nothing when there are no more completions in a given direction.
//The latter case is used when we are still in the middle of finding completions, to allow browsing
//through the ones found so far. Return the total number of matches, or -1 if still unknown -- webb.
//
//compl_curr_match is currently being used by ins_compl_get_exp(), so we use compl_shown_match here.
//
//Note that this function may be called recursively once only. First with "allow_get_expansion" 
//true, which calls ins_compl_get_exp(), which in turn calls this function with 
//"allow_get_expansion" false.
private int
ins_compl_next(
   int allow_get_expansion,
   int count,      // repeat completion this many times; should be at least 1
   Boole doInsertMatch   // Insert the newly selected match
){
   int num_matches = -1;
   int todo = count;
   int advance;
   int started = compl_started;
   Book* orig_curbuf = curBook;
   Unt cur_cot_flags = curBook->o.completeOpt;
   int compl_no_insert = (cur_cot_flags & COT_NOINSERT) != 0 || compl_autocomplete;
   int compl_fuzzy_match = (cur_cot_flags & COT_FUZZY) != 0;
   int compl_preinsert = ins_compl_has_preinsert();

   // When user complete function return -1 for findstart which is next
   // time of 'always', compl_shown_match become NULL.
   if (compl_shown_match == NULL)
      return -1;

   if (compl_leader.c
          && !match_at_original_text(compl_shown_match)
          && !compl_fuzzy_match)
      // Update "compl_shown_match" to the actually shown match
      ins_compl_update_shown_match();

   if (allow_get_expansion && doInsertMatch && (!compl_get_longest || complUsedMatchS))
      // Delete old text to be replaced
      ins_compl_delete();

   // When finding the longest common text we stick at the original text,
   // don't let CTRL-N or CTRL-P move to the first match.
   advance = count != 1 || !allow_get_expansion || !compl_get_longest;

   // When restarting the search don't insert the first match either.
   if (compl_restarting) {
      advance = false;
      compl_restarting = false;
   }

   //Repeat this for when <PageUp> or <PageDown> is typed.  But don't wrap around.
   if (find_next_completion_match(allow_get_expansion, todo, advance, &num_matches) == FAIL)
      return -1;

   if (curBook != orig_curbuf) {
      // In case some completion function switched buffer, don't want to
      // insert the completion elsewhere.
      return -1;
   }

   // Insert the text of the new completion, or the compl_leader.
   if (compl_no_insert && !started && !compl_preinsert) {
      ins_compl_insert_bytes(compl_orig_text.c + get_compl_len(), -1);
      complUsedMatchS = false;
   } ei (doInsertMatch) {
      if (!compl_get_longest || complUsedMatchS)
         ins_compl_insert(true);
      else
         ins_compl_insert_bytes(compl_leader.c + get_compl_len(), -1);
   } else
      complUsedMatchS = false;

   if (!allow_get_expansion) {
      // may undisplay the popup menu first
      ins_compl_upd_pum();

      if (pum_enough_matches())
         // Will display the popup menu, don't redraw yet to avoid flicker.
         pum_callUpdateScreen();
      else
         // Not showing the popup menu yet, redraw to show the user what was inserted.
         drawUpdateScreen(0);

      // display the updated popup menu
      ins_compl_show_pum();

      //Delete old text to be replaced, since we're still searching and don't want to match 
      // ourselves!
      ins_compl_delete();
   }

   // Enter will select a match when the match wasn't inserted and the popup menu is visible.
   if (compl_no_insert && !started && compl_selected_item != -1)
      compl_enter_selects = true;
   else
      compl_enter_selects = !doInsertMatch && displayedCompletionsS;

   // Show the file name for the match (if any)
   if (compl_shown_match->fName)
      ins_compl_show_filename();

   return num_matches;
}

// Check if the current completion source exceeded its timeout. If so, stop collecting 
// & halve the timeout
private void
check_elapsed_time(void) {
   if (cpt_sources_array == NULL || cpt_sources_index < 0)
      return;

   Elapsed* start_tv = &cpt_sources_array[cpt_sources_index].matchCollectionStart;
   long elapsed_ms = ELAPSED_FUNC(*start_tv);

   if (elapsed_ms > InsertCompletionimeout_ms) {
      InsertCompletionime_slice_expired = true;
      DECAY_InsertCompletionIMEOUT();
   }
}

//Call this while finding completions, to check whether the user has hit a key
//that should change the currently displayed completion, or exit completion
//mode.  Also, when compl_pending is not zero, show a completion as soon as possible. -- webb
//"frequency" specifies out of how many calls we actually check.
//"in_compl_func" is true when called from complete_check(), don't set compl_curr_match.
pub void
ins_compl_check_keys(int frequency, Boole in_compl_func) {
   static int   count = 0;
   // Don't check when reading keys from a script, :normal or feedkeys().
   // That would break the test scripts.  But do check for keys when called from complete_check()
   if (!in_compl_func && (using_script() || ex_normal_busy))
      return;

   // Only do this at regular intervals
   if (++count < frequency)
      return;
   count = 0;

   // Check for a typed key. Do use mappings, otherwise eeIsCtrlXKey() can't work correctly
   Unt c = vpeekc_any();
   if (c != ZERO
       // If test_override("char_avail", 1) was called, ignore characters
       // waiting in the typeahead buffer.
       && !disable_char_avail_for_testing
    ) {
      if (eeIsCtrlXKey(c) && c != Ctrl_X && c != Ctrl_R) {
         c = safe_vgetc();   // Eat the character
         compl_shows_dir = ins_compl_key2dir(c);
         (void)ins_compl_next(false, ins_compl_key2count(c), c != K_UP && c != K_DOWN);
      } else {
         // Need to get the character to have keyWasTypedG set.  We'll put it
         // back with vungetc() below.  But skip K_IGNORE.
         c = safe_vgetc();
         if (c != K_IGNORE) {
            // Don't interrupt completion when the character wasn't typed,
            // e.g., when doing @q to replay keys.
            if (c != Ctrl_R && keyWasTypedG)
                compl_interrupted = true;

            vungetc(c);
         }
      }
   } ei (compl_autocomplete)
      check_elapsed_time();

   if (compl_pending != 0 && !gotInterruptG && !(cot_flags & COT_NOINSERT) && !compl_autocomplete) {
      // Insert the first match immediately and advance compl_shown_match,
      // before finding other matches.
      int todo = compl_pending > 0 ? compl_pending : -compl_pending;

      compl_pending = 0;
      (void)ins_compl_next(false, todo, true);
   }
}

// Decide the direction of Insert mode complete from the key typed. Return BACKWARD or FORWARD.
private Unt
ins_compl_key2dir(Unt c) {
   if (c == Ctrl_P || c == Ctrl_L || c == K_PAGEUP || c == K_KPAGEUP || c == K_S_UP || c == K_UP)
      return BACKWARD;
   return FORWARD;
}

// Return true for keys that are used for completion only when the popup menu is visible.
private Boole
ins_compl_pum_key(Unt c) {
    return pum_visible() && (c == K_PAGEUP || c == K_KPAGEUP || c == K_S_UP
           || c == K_PAGEDOWN || c == K_KPAGEDOWN || c == K_S_DOWN
           || c == K_UP || c == K_DOWN);
}

//Decide the number of completions to move forward.
//Return 1 for most keys, height of the popup menu for page-up/down keys.
private int
ins_compl_key2count(Unt c) {
   if (ins_compl_pum_key(c) && c != K_UP && c != K_DOWN) {
      int h = pum_get_height();
      if (h > 3)
          h -= 2; // keep some context
      return h;
   }
   return 1;
}

// Return true if completion with "c" should insert the match, false if only to change the 
// currently selected completion.
private Boole
shouldNewCharInsertTheMatch(int c) {
   switch (c) {
   case K_UP:
   case K_DOWN:
   case K_PAGEDOWN:
   case K_KPAGEDOWN:
   case K_S_DOWN:
   case K_PAGEUP:
   case K_KPAGEUP:
   case K_S_UP:
      return false;
   default:
      return true;
   }
}

//Get the pattern, column and length for normal completion (CTRL-N CTRL-P completion)
//Set the global variables: compl_col, compl_length and compl_pattern.
//Use the global variables: compl_cont_status and ctrl_x_mode
private Unt
get_normal_compl_info(CS line, int startcol, ColNr curs_col) {
   if ((compl_cont_status & CONT_SOL) || ctrl_x_mode_path_defines()) {
      if (!compl_status_adding()) {
          while (--startcol >= 0 && eeIsIdentifierChar(line[startcol]))
             {}
          compl_col += ++startcol;
          compl_length = curs_col - startcol;
      }
      if (p_ic) {
          compl_pattern.c = str_foldcase(line + compl_col,
             compl_length, NULL, 0);
         if (compl_pattern.c == NULL) {
            compl_pattern.len = 0;
            return FAIL;
         }
         compl_pattern.len = STRLEN(compl_pattern.c);
      } else {
         compl_pattern.c = copySubstr(line + compl_col, (Unt)compl_length);
         if (compl_pattern.c == NULL) {
            compl_pattern.len = 0;
            return FAIL;
         }
         compl_pattern.len = (Unt)compl_length;
      }
   } ei (compl_status_adding()) {
      CS prefix = S"\\<";
      Unt prefixlen = STRLEN_LITERAL("\\<");

      if (!eeIsWordPtr(line + compl_col)
         || (compl_col > 0 && (eeIsWordPtr(mb_prevptr(line, line + compl_col))))
      ) {
         prefix = S"";
         prefixlen = 0;
      }

      // we need up to 2 extra chars for the prefix
      Unt n = quote_meta(NULL, line + compl_col, compl_length) + prefixlen;
      compl_pattern.c = alloc(n);
      STRCPY((char *)compl_pattern.c, prefix);
      (void)quote_meta(compl_pattern.c + prefixlen,
         line + compl_col, compl_length);
      compl_pattern.len = n - 1;
   } ei (--startcol < 0 || !eeIsWordPtr(mb_prevptr(line, line + startcol + 1))) {
      Unt   len = STRLEN_LITERAL("\\<\\k\\k");

      // Match any word of at least two chars
      compl_pattern.c = copySubstr((CS)"\\<\\k\\k", len);
      if (compl_pattern.c == NULL) {
         compl_pattern.len = 0;
         return FAIL;
      }
      compl_pattern.len = len;
      compl_col += curs_col;
      compl_length = 0;
      compl_from_nonkeyword = true;
   } else {
      //Search the point of change class of multibyte character
      //or not a word single byte character backward.
      int head_off;

      startcol -= (*mb_head_off)(line, line + startcol);
      int base_class = mb_get_class(line + startcol);
      while (--startcol >= 0) {
         head_off = (*mb_head_off)(line, line + startcol);
         if (base_class != mb_get_class(line + startcol - head_off))
            break;
         startcol -= head_off;
      }

      compl_col += ++startcol;
      compl_length = (int)curs_col - startcol;
      if (compl_length == 1) {
         // Only match word with at least two chars -- webb
         // there's no need to call quote_meta, alloc(7) is enough  -- Acevedo
         compl_pattern.c = alloc(7);
         STRCPY((char *)compl_pattern.c, "\\<");
         (void)quote_meta(compl_pattern.c + 2, line + compl_col, 1);
         STRCAT((char *)compl_pattern.c, "\\k");
         compl_pattern.len = STRLEN(compl_pattern.c);
      } else {
         Unt  n = quote_meta(NULL, line + compl_col, compl_length) + 2;

         compl_pattern.c = alloc(n);
         STRCPY((char *)compl_pattern.c, "\\<");
         (void)quote_meta(compl_pattern.c + 2, line + compl_col, compl_length);
         compl_pattern.len = n - 1;
      }
   }

   // Call functions in 'complete' with 'findstart=1'
   if (ctrl_x_mode_normal() && !(compl_cont_status & CONT_LOCAL) && curBook->o.complete) {
      // ^N completion, not complete() or ^X^N
      if (setup_cpt_sources() == FAIL || prepare_cpt_compl_funcs() == FAIL)
          return FAIL;
   }

   return OK;
}

//Get the pattern, column and length for whole line completion or for the complete() function.
//Set the global variables: compl_col, compl_length and compl_pattern.
private int
get_wholeline_compl_info(CS line, ColNr curs_col) {
   compl_col = (ColNr)getwhitecols(line);
   compl_length = (int)curs_col - (int)compl_col;
   if (compl_length < 0)   // cursor in indent: empty pattern
      compl_length = 0;
   if (p_ic) {
      compl_pattern.c = str_foldcase(line + compl_col, compl_length, NULL, 0);
      if (compl_pattern.c == NULL) {
         compl_pattern.len = 0;
         return FAIL;
      }
      compl_pattern.len = STRLEN(compl_pattern.c);
   } else {
      compl_pattern.c = copySubstr(line + compl_col, (Unt)compl_length);
      if (compl_pattern.c == NULL) {
         compl_pattern.len = 0;
         return FAIL;
      }
      compl_pattern.len = (Unt)compl_length;
   }

   return OK;
}

//Get the pattern, column and length for filename completion.
//Set the global variables: compl_col, compl_length and compl_pattern.
private int
get_filename_compl_info(CS line, int startcol, ColNr curs_col) {
   // Go back to just before the first filename character.
   if (startcol > 0) {
      CS p = line + startcol;

      MB_PTR_BACK(line, p);
      while (p > line && eeIsFnameChar(mb_ptr2char(p)))
         MB_PTR_BACK(line, p);
      if (p == line && eeIsFnameChar(mb_ptr2char(p)))
         startcol = 0;
      else
         startcol = (int)(p - line) + 1;
   }

   compl_col += startcol;
   compl_length = (int)curs_col - startcol;
   compl_pattern.c = addstar((Text){line + compl_col, compl_length}, EXPAND_FILES);

   compl_pattern.len = STRLEN(compl_pattern.c);

   return OK;
}

// Get the pattern, column and length for command-line completion.
// Set the global variables: compl_col, compl_length and compl_pattern.
private int
get_cmdline_compl_info(CS line, ColNr curs_col) {
   compl_pattern.c = copySubstr(line, curs_col);
   if (!compl_pattern.c) {
      compl_pattern.len = 0;
      return FAIL;
   }
   compl_pattern.len = curs_col;
   setCompletionContextForCommand(&compl_xp, compl_pattern, curs_col, false);
   if (compl_xp.context == EXPAND_UNSUCCESSFUL || compl_xp.context == EXPAND_NOTHING)
      // No completion possible, use an empty pattern to get a "pattern not found" message.
      compl_col = curs_col;
   else
      compl_col = (int)(compl_xp.input.c - compl_pattern.c);
   compl_length = curs_col - compl_col;

   return OK;
}

// Set global variables related to completion: compl_col, compl_length, compl_pattern and 
// cpt_compl_pattern.
private int
set_compl_globals(int startcol, ColNr curs_col, int is_cpt_compl) {
   if (is_cpt_compl) {
      EE_CLEAR_STRING(cpt_compl_pattern);
      if (startcol < compl_col)
         return prepend_startcol_text(&cpt_compl_pattern, &compl_orig_text, startcol);
      else {
         cpt_compl_pattern.c = copySubstr(compl_orig_text.c, compl_orig_text.len);
         cpt_compl_pattern.len = compl_orig_text.len;
      }
   } else {
      if (startcol < 0 || startcol > curs_col)
         startcol = curs_col;

      // Re-obtain line in case it has changed
      CS line = ml_get(curPor->cursor.lnum);
      int   len = curs_col - startcol;

      compl_pattern.c = copySubstr(line + startcol, (Unt)len);
      if (compl_pattern.c == NULL) {
         compl_pattern.len = 0;
         return FAIL;
      }
      compl_pattern.len = (Unt)len;
      compl_col = startcol;
      compl_length = len;
   }

   return OK;
}

//Get the pattern, column and length for user defined completion ('omnifunc', 'completefunc' and 
//'thesaurusfunc').
//Callback function "cb" is set if triggered by a function in the 'cpt' option; otherwise, it is 
//null. "startcol", when not NULL, contains the column returned by function.
private int
get_userdefined_compl_info(ColNr curs_col, Callback* cb, int* startcol) {
   int ret = FAIL;

   // Call user defined function 'completefunc' with "a:findstart"
   // set to 1 to obtain the length of text to use for completion.
   Var   args[3];
   int      col;
   CS funcname = NULL;
   Pos pos;
   int save_State = stateG;
   int is_cfntion = (cb != NULL);

   if (!is_cfntion) {
      // Call 'completefunc' or 'omnifunc' or 'thesaurusfunc' and get pattern
      // length as a string
      funcname = get_complete_funcname(ctrl_x_mode);
      if (*funcname == ZERO) {
         showErrFmtMsg(_(e_option_str_is_not_set), ctrl_x_mode_function() ? "completefunc" : "omnifunc");
         return FAIL;
      }
      cb = get_insert_callback(ctrl_x_mode);
   }

   args[0].tag = VAR_NUMBER;
   args[0].number = 1;
   args[1].tag = VAR_STRING;
   args[1].string = (CS)"";
   args[2].tag = VAR_UNKNOWN;
   pos = curPor->cursor;
   ++textlock;
   col = call_callback_retnr(cb, 2, args);
   --textlock;

   stateG = save_State;
   curPor->cursor = pos;   // restore the cursor position
   check_cursor();  // make sure cursor position is valid, just in case
   validate_cursor();
   if (!EQUAL_POS(curPor->cursor, pos)) {
      emsg(_(e_complete_function_deleted_text));
      return FAIL;
   }

   if (startcol)
      *startcol = col;

   // Return value -2 means the user complete function wants to cancel the complete without an 
   // error, do the same if the function did not execute successfully.
   if (col == -2 || aborting())
      return FAIL;

   // Return value -3 does the same as -2 and leaves CTRL-X mode.
   if (col == -3) {
      if (is_cfntion)
         return FAIL;
      ctrl_x_mode = CTRL_X_NORMAL;
      editSubmodeMsgG = NULL;
      msgClearCommline();
      return FAIL;
   }

   // Reset extended parameters of completion, when starting new completion.
   compl_opt_refresh_always = false;
   compl_opt_suppress_empty = false;

   ret = !is_cfntion ? set_compl_globals(col, curs_col, false) : OK;

   return ret;
}

//Get the completion pattern, column and length.
//"startcol" - start column number of the completion pattern/text "cur_col" - current cursor column
//On return, "line_invalid" is set to true, if the current line may have
//become invalid and needs to be fetched again. Return OK on success.
private Unt
compl_get_info(CS line, int startcol, ColNr curs_col, OUT Boole* line_invalid) {
   if (ctrl_x_mode_normal() || ctrl_x_mode_register()
       || (ctrl_x_mode & CTRL_X_WANT_IDENT
      && !thesaurus_func_complete(ctrl_x_mode))
   ) {
      if (get_normal_compl_info(line, startcol, curs_col) != OK)
         return FAIL;
      *line_invalid = true; // 'cpt' func may have invalidated "line"
   } ei (ctrl_x_mode_line_or_eval()) {
      return get_wholeline_compl_info(line, curs_col);
   } ei (ctrl_x_mode_files()) {
      return get_filename_compl_info(line, startcol, curs_col);
   } ei (ctrl_x_mode == CTRL_X_CMDLINE) {
      return get_cmdline_compl_info(line, curs_col);
   } ei (ctrl_x_mode_function() || ctrl_x_mode_omni() || thesaurus_func_complete(ctrl_x_mode)){
      if (get_userdefined_compl_info(curs_col, NULL, NULL) != OK)
         return FAIL;
      *line_invalid = true;   // "line" may have become invalid
   } else {
      internal_error(S"ins_complete()");
      return FAIL;
   }

   return OK;
}

//Continue an interrupted completion mode search in "line".
//If this same ctrl_x_mode has been interrupted use the text from "compl_startpos" to the cursor 
//as a pattern to add a new word instead of expand the one before the cursor, in word-wise if 
//"compl_startpos" is not in the same line as the cursor then fix it (the line has been split 
//because it was longer than 'tw').  if SOL is set then skip the previous pattern, a word
//at the beginning of the line has been inserted, we'll look for that.
private void
ins_compl_continue_search(CS line) {
   // it is a continued search
   compl_cont_status &= ~CONT_INTRPT;   // remove INTRPT
   if (ctrl_x_mode_normal() || ctrl_x_mode_path_patterns() || ctrl_x_mode_path_defines()) {
      if (compl_startpos.lnum != curPor->cursor.lnum) {
         // line (probably) wrapped, set compl_startpos to the first non_blank in the line, if it 
         // is not a wordchar include it to get a better pattern, but then we don't
         // want the "\\<" prefix, check it below
         compl_col = (ColNr)getwhitecols(line);
         compl_startpos.col = compl_col;
         compl_startpos.lnum = curPor->cursor.lnum;
         compl_cont_status &= ~CONT_SOL;   // clear SOL if present
      } else {
         // S_IPOS was set when we inserted a word that was at the beginning of the line, which 
         // means that we'll go to SOL mode but first we need to redefine compl_startpos
         if (compl_cont_status & CONT_S_IPOS) {
            compl_cont_status |= CONT_SOL;
            compl_startpos.col = (ColNr)(skipwhite( line + compl_length + compl_startpos.col) - line);
         }
         compl_col = compl_startpos.col;
      }
      compl_length = curPor->cursor.col - (int)compl_col;
      // IObuff is used to add a "word from the next line" would we
      // have enough space?  just being paranoid
#define   MIN_SPACE 75
      if (compl_length > (IOSIZE - MIN_SPACE)) {
         compl_cont_status &= ~CONT_SOL;
         compl_length = (IOSIZE - MIN_SPACE);
         compl_col = curPor->cursor.col - compl_length;
      }
      compl_cont_status |= CONT_ADDING | CONT_N_ADDS;
      if (compl_length < 1)
          compl_cont_status &= CONT_LOCAL;
   } ei (ctrl_x_mode_line_or_eval() || ctrl_x_mode_register())
      compl_cont_status = CONT_ADDING | CONT_N_ADDS;
   else
      compl_cont_status = 0;
}

// start insert mode completion
private Unt
ins_compl_start(void) {
   int      startcol = 0;       // column where searched text starts
   Boole didAindentSaved = didAindentG;

   // First time we hit ^N or ^P (in a row, I mean)
   didAindentG = false;
   didSindentG = false;
   can_si = false;
   can_si_back = false;
   if (stop_arrow() == FAIL)
      return FAIL;

   CS line = ml_get(curPor->cursor.lnum);
   ColNr curs_col = curPor->cursor.col;
   compl_pending = 0;
   compl_lnum = curPor->cursor.lnum;

   if ((compl_cont_status & CONT_INTRPT) == CONT_INTRPT && compl_cont_mode == ctrl_x_mode)
      // this same ctrl-x_mode was interrupted previously. Continue the completion.
      ins_compl_continue_search(line);
   else
      compl_cont_status &= CONT_LOCAL;

   if (!compl_status_adding()) {  // normal expansion
      compl_cont_mode = ctrl_x_mode;
      if (ctrl_x_mode_not_default())
         // Remove LOCAL if ctrl_x_mode != CTRL_X_NORMAL
         compl_cont_status = 0;
      compl_cont_status |= CONT_N_ADDS;
      compl_startpos = curPor->cursor;
      startcol = (int)curs_col;
      compl_col = 0;
   }

   // Work out completion pattern and original text -- webb
   Boole line_invalid = false;
   if (compl_get_info(line, startcol, curs_col, OUT &line_invalid) == FAIL) {
      if (ctrl_x_mode_function() || ctrl_x_mode_omni() || thesaurus_func_complete(ctrl_x_mode))
         // restore didAindentG, so that adding comment leader works
         didAindentG = didAindentSaved;
      return FAIL;
   }
   // If "line" was changed while getting completion info get it again.
   if (line_invalid)
      line = ml_get(curPor->cursor.lnum);

   if (compl_status_adding()) {
      editSubmodePreMsgG = (CS)_(" Adding");
      if (ctrl_x_mode_line_or_eval()) {
         // Insert a new line, keep indentation but ignore 'comments'.
         compl_startpos.lnum = curPor->cursor.lnum;
         compl_startpos.col = compl_col;
         ins_eol('\r');
         compl_length = 0;
         compl_col = curPor->cursor.col;
         compl_lnum = curPor->cursor.lnum;
      } ei (ctrl_x_mode_normal() && cfc_has_mode()) {
          compl_startpos = curPor->cursor;
          compl_cont_status &= CONT_S_IPOS;
      }
   } else {
      editSubmodePreMsgG = NULL;
      compl_startpos.col = compl_col;
   }

   if (!compl_autocomplete) {
      if (compl_cont_status & CONT_LOCAL)
         editSubmodeMsgG = (CS)_(ctrl_x_msgs[CTRL_X_LOCAL_MSG]);
      else
         editSubmodeMsgG = (CS)_(CTRL_X_MSG(ctrl_x_mode));
   }

   // If any of the original typed text has been changed we need to fix the redo buffer.
   ins_compl_fixRedoBufForLeader(NULL);

   // Always add completion for the original text.
   EE_CLEAR_STRING(compl_orig_text);
   compl_orig_text.len = (Unt)compl_length;
   compl_orig_text.c = copySubstr(line + compl_col, (Unt)compl_length);
   Unt flags = CP_ORIGINAL_TEXT;
   if (p_ic)
      flags |= CP_ICASE;
   if (!compl_orig_text.c 
         || addMatchToList(
               compl_orig_text.c, (int)compl_orig_text.len, NULL, NULL, NULL, 0, flags, false, NULL,
               FUZZY_SCORE_NONE
            ) != OK
   ) {
      EE_CLEAR_STRING(compl_pattern);
      EE_CLEAR_STRING(compl_orig_text);
      return FAIL;
   }

   // showmode might reset the internal line pointers, so it must be called before 
   // line = ml_get(), or when this address is no longer needed.  -- Acevedo.
   if (!compl_autocomplete) {
      editSubmodeExtraMsgG = (CS)_("-- Searching...");
      editSubmodeHiG = 0;
      showmode();
      editSubmodeExtraMsgG = NULL;
      out_flush();
   }

   return OK;
}

// display the completion status message
private void
ins_compl_show_statusmsg(void) {
   // we found no match if the list has only the "compl_orig_text"-entry
   if (is_first_match(compl_first_match->next)) {
      editSubmodeExtraMsgG = compl_status_adding() && compl_length > 1
               ? _("Hit end of paragraph")
               : _("Pattern not found");
      editSubmodeHiG = HLF_E;
   }

   if (editSubmodeExtraMsgG == NULL) {
      if (match_at_original_text(compl_curr_match)) {
         editSubmodeExtraMsgG = (CS)_("Back at original");
         editSubmodeHiG = HLF_W;
      } ei (compl_cont_status & CONT_S_IPOS) {
         editSubmodeExtraMsgG = (CS)_("Word from other line");
         editSubmodeHiG = 0;
      } ei (compl_curr_match->next == compl_curr_match->prev) {
         editSubmodeExtraMsgG = (CS)_("The only match");
         editSubmodeHiG = 0;
         compl_curr_match->cp_number = 1;
      } else {
         //Update completion sequence number when needed.
         if (compl_curr_match->cp_number == -1)
            ins_compl_update_sequence_numbers();
         //The match should always have a sequence number now, this is just a safety check.
         if (compl_curr_match->cp_number != -1) {
            //Space for 10 text chars. + 2x10-digit no.s = 31.
            //Translations may need more than twice that.
            static Byte match_ref[81];

            if (compl_matches > 0)
               eeSnprintf(
                  match_ref, sizeof(match_ref), _("match %d of %d"), 
                  compl_curr_match->cp_number, compl_matches
               );
            else
               eeSnprintf(
                  match_ref, sizeof(match_ref), _("match %d"), compl_curr_match->cp_number
               );
            editSubmodeExtraMsgG = match_ref;
            editSubmodeHiG = HLF_R;
         }
      }
   }

   // Show a message about what (completion) mode we're in.
   if (!compl_opt_suppress_empty) {
      showmode();
      if (editSubmodeExtraMsgG) {
         if (!p_smd) {
            msg_hist_off = true;
            msgDeco(editSubmodeExtraMsgG, getDecoFlags(editSubmodeHiG));
            msg_hist_off = false;
         }
      } else
         msgClearCommline();   // necessary for "noshowmode"
   }
}

//Do Insert mode completion. Called when character "c" was typed, which has a meaning for 
//completion. Return OK if completion was done, FAIL if something failed (out of mem).
private Unt
ins_complete(Unt c, Boole enable_pum) {
   Elapsed   matchCollectionStart; // Timestamp when match collection starts

   compl_direction = ins_compl_key2dir(c);
   Boole doInsertMatch = shouldNewCharInsertTheMatch(c);

   if (!compl_started) {
      if (ins_compl_start() == FAIL)
         return FAIL;
   } ei (doInsertMatch && stop_arrow() == FAIL)
      return FAIL;

   if (compl_autocomplete && p_acl > 0)
      ELAPSED_INIT(matchCollectionStart);
   compl_curr_win = curPor;
   compl_curr_buf = curPor->book;
   compl_shown_match = compl_curr_match;
   compl_shows_dir = compl_direction;

   // Find next match (and following matches).
   int save_cursorRow = curPor->cursorRow;
   int save_leftCol = curPor->leftCol;
   int n = ins_compl_next(true, ins_compl_key2count(c), doInsertMatch);

   // may undisplay the popup menu
   ins_compl_upd_pum();

   if (n > 1)      // all matches have been found
      compl_matches = n;
   compl_curr_match = compl_shown_match;
   compl_direction = compl_shows_dir;

   // Eat the ESC that vgetc() returns after a CTRL-C to avoid leaving Insert mode.
   if (gotInterruptG && !global_busy) {
      (void)vgetc();
      gotInterruptG = false;
   }

   // we found no match if the list has only the "compl_orig_text"-entry
   Boole no_matches_found = is_first_match(compl_first_match->next);
   if (no_matches_found) {
      // remove N_ADDS flag, so next ^X<> won't try to go to ADDING mode,
      // because we couldn't expand anything at first place, but if we used
      // ^P, ^N, ^X^I or ^X^D we might want to add-expand a single-char-word
      // (such as M in M'exico) if not tried already.  -- Acevedo
      if (compl_length > 1
         || compl_status_adding()
         || (ctrl_x_mode_not_default()
             && !ctrl_x_mode_path_patterns()
             && !ctrl_x_mode_path_defines()))
          compl_cont_status &= ~CONT_N_ADDS;
   }

   if (compl_curr_match->flags & CP_CONT_S_IPOS)
      compl_cont_status |= CONT_S_IPOS;
   else
      compl_cont_status &= ~CONT_S_IPOS;

   if (!compl_autocomplete)
      ins_compl_show_statusmsg();

   // Wait for the autocompletion delay to expire
   if (compl_autocomplete && p_acl > 0 && !no_matches_found
       && ELAPSED_FUNC(matchCollectionStart) < p_acl
   ) {
      cursor_on();
      setcursor();
      out_flush();
      do {
         if (char_avail()) {
            ins_compl_restart();
            compl_interrupted = true;
            break;
         } else
            ui_delay(2L, true);
      } while (ELAPSED_FUNC(matchCollectionStart) < p_acl);
   }

   // Show the popup menu, unless we got interrupted.
   if (enable_pum && !compl_interrupted)
      show_pum(save_cursorRow, save_leftCol);

   compl_was_interrupted = compl_interrupted;
   compl_interrupted = false;

   return OK;
}

// Return true if the given character 'c' can be used to trigger autocompletion.
private Boole
ins_compl_setup_autocompl(Unt c) {
   if (bookIsCharPrintable(c)) {
      compl_autocomplete = true;
      return true;
   }
   return false;
}

// Remove (if needed) and show the popup menu
private void
show_pum(int prev_cursorRow, int prev_leftCol) {
   // isRedrawingDisabledG may be set when invoked through complete().
   int save_isRedrawingDisabledG = isRedrawingDisabledG;
   isRedrawingDisabledG = 0;

   // If the cursor moved or the display scrolled we need to remove the pum first.
   setcursor();
   if (prev_cursorRow != curPor->cursorRow || prev_leftCol != curPor->leftCol)
      ins_compl_del_pum();

   ins_compl_show_pum();
   setcursor();

   isRedrawingDisabledG = save_isRedrawingDisabledG;
}

//Looks in the first "len" chars. of "src" for search-metachars. If dest is not NULL the chars. 
//are copied there quoting (with a backslash) the metachars, and dest would be ZERO 
//terminated. Return the length (needed) of dest
private unsigned
quote_meta(CS dest, CS src, int len) {
   unsigned   m = (unsigned)len + 1;  // one extra for the ZERO
   for ( ; --len >= 0; src++) {
      switch (*src) {
      case '.':
      case '*':
      case '[':
      if (ctrl_x_mode_dictionary() || ctrl_x_mode_thesaurus())
         break;
      // FALLTHROUGH
      case '~':
         // FALLTHROUGH
      case '\\':
         if (ctrl_x_mode_dictionary() || ctrl_x_mode_thesaurus())
             break;
         // FALLTHROUGH
      case '^':      // currently it's not needed.
      case '$':
         m++;
         if (dest != NULL)
             *dest++ = '\\';
         break;
      }
      if (dest)
         *dest++ = *src;
      // Copy remaining bytes of a multibyte character.
      int mb_len = utfCharLen(src) - 1;
      if (mb_len > 0 && len >= mb_len)
      for (int i = 0; i < mb_len; ++i) {
         --len;
         ++src;
         if (dest)
            *dest++ = *src;
      }
   }
   if (dest)
      *dest = ZERO;

   return m;
}

#if defined(EXITFREE)
pub void
free_insexpand_stuff(void) {
   EE_CLEAR_STRING(compl_orig_text);
   evFreeCallback(&completeFnS);
   evFreeCallback(&omniFnS);
   evFreeCallback(&thesaurusCbS);
   inClearCompletionCbs(&customCompleteFnS, cpt_cb_count);
}
#endif

// Reset the info associated with completion sources.
private void
cpt_sources_clear(void) {
   EE_CLEAR(cpt_sources_array);
   cpt_sources_index = -1;
   cpt_sources_count = 0;
}

//Setup completion sources.
private Unt
setup_cpt_sources(void) {
   Byte  buf[LSIZE];
   int slen;
   int idx = 0;

   cpt_sources_clear();
   cpt_sources_array = ALLOC_CLEAR_MULT(CompletionSource, 1);

   for (CS p = curBook->o.complete; *p;) {
      while (*p == ',' || *p == ' ') // Skip delimiters
         p++;
      if (*p) { // If not end of string, count this segment
         slen = strCutPathFromListOfPaths(OUT &p, OUT buf, LSIZE, S","); // Advance p
         if (slen > 0) {
            CS caret = firstOccurrence(buf, '^');
            if (caret)
               cpt_sources_array[idx].maxMatches = atoi((char *)caret + 1);
         }
         idx++;
      }
   }

   return OK;
}

// true if any of the completion sources have 'refresh' set to 'always'.
private Boole
is_cfn_refresh_always(void) {
   for (int i = 0; i < cpt_sources_count; i++) {
      if (cpt_sources_array[i].refreshAlways)
         return true;
   } 
   return false;
}

// Make the completion list acyclic.
private void
ins_compl_make_linear(void) {
   if (compl_first_match == NULL || !compl_first_match->prev)
      return;
   InsertCompletion* m = compl_first_match->prev;
   m->next = NULL;
   compl_first_match->prev = NULL;
}

//Remove the matches linked to the current completion source (as indicated by cpt_sources_index) 
//from the completion list.
private InsertCompletion *
remove_old_matches(void) {
   InsertCompletion *sublist_start = NULL, *sublist_end = NULL, *insert_at = NULL;
   InsertCompletion *current, *next;
   int       compl_shown_removed = false;
   int       forward = (compl_first_match->indexOfSourceInCpt < 0);

   compl_direction = forward ? FORWARD : BACKWARD;
   compl_shows_dir = compl_direction;

   // Identify the sublist of old matches that needs removal
   for (current = compl_first_match; current != NULL; current = current->next) {
      if (current->indexOfSourceInCpt < cpt_sources_index &&
         (forward || (!forward && !insert_at)))
          insert_at = current;

      if (current->indexOfSourceInCpt == cpt_sources_index) {
         if (!sublist_start)
            sublist_start = current;
         sublist_end = current;
         if (!compl_shown_removed && compl_shown_match == current)
            compl_shown_removed = true;
      }

      if ((forward && current->indexOfSourceInCpt > cpt_sources_index) || (!forward && insert_at))
         break;
   }

   // Re-assign compl_shown_match if necessary
   if (compl_shown_removed) {
      if (forward)
         compl_shown_match = compl_first_match;
      else {  // Last node will have the prefix that is being completed
         for (current = compl_first_match; current->next != NULL; current = current->next)
            {}
         compl_shown_match = current;
      }
   }

   if (!sublist_start) // No nodes to remove
      return insert_at;

   // Update links to remove sublist
   if (sublist_start->prev)
      sublist_start->prev->next = sublist_end->next;
   else
      compl_first_match = sublist_end->next;

   if (sublist_end->next)
      sublist_end->next->prev = sublist_start->prev;

   // Free all nodes in the sublist
   sublist_end->next = NULL;
   for (current = sublist_start; current; current = next) {
      next = current->next;
      ins_compl_item_free(current);
   }

   return insert_at;
}

//Retrieve completion matches using the callback function "cb" and store the
//'refresh:always' flag.
private void
get_cfn_completion_matches(Callback* cb) {
   int   startcol = cpt_sources_array[cpt_sources_index].startCol;

   if (startcol == -2 || startcol == -3)
      return;

   if (set_compl_globals(startcol, curPor->cursor.col, true) == OK) {
      expand_by_function(0, cpt_compl_pattern.c, cb);

      cpt_sources_array[cpt_sources_index].refreshAlways = compl_opt_refresh_always;
      compl_opt_refresh_always = false;
   }
}

// Retrieve completion matches from functions in the 'cpt' option where the 'refresh:always' 
// flag is set
private void
cpt_compl_refresh(void) {
   Callback   *cb = NULL;
   int  startcol, ret;

   // Make the completion list linear (non-cyclic)
   ins_compl_make_linear();
   // Make a copy of 'cpt' in case the buffer gets wiped out
   CS cpt = copyStr(curBook->o.complete);
   strip_caret_numbers_in_place(cpt);

   cpt_sources_index = 0;
   for (CS p = cpt; *p != ZERO;) {
      while (*p == ',' || *p == ' ') // Skip delimiters
         p++;
      if (*p == ZERO)
         break;

      if (cpt_sources_array[cpt_sources_index].refreshAlways) {
         cb = get_callback_if_cfn(p);
         if (cb) {
            compl_curr_match = remove_old_matches();
            ret = get_userdefined_compl_info(curPor->cursor.col, cb, OUT &startcol);
            if (ret == FAIL) {
               if (startcol == -3)
                  cpt_sources_array[cpt_sources_index].refreshAlways = false;
               else
                  startcol = -2;
            } ei (startcol < 0 || startcol > curPor->cursor.col)
               startcol = curPor->cursor.col;
            cpt_sources_array[cpt_sources_index].startCol = startcol;
            if (ret == OK) {
               compl_source_start_timer(cpt_sources_index);
               get_cfn_completion_matches(cb);
            }
         }
      }

      (void)strCutPathFromListOfPaths(OUT &p, OUT IObuff, IOSIZE, S","); // Advance p
      if (may_advance_cpt_index(p))
         (void)advance_cpt_sources_index_safe();
   }
   cpt_sources_index = -1;

   eeglFree(cpt);
   // Make the list cyclic
   compl_matches = ins_compl_make_cyclic();
}

// Function given to expandGeneric() to obtain the list of :disassemble arguments.
pub CS
get_disassemble_argument(Expand* xp, int idx) {
   if (idx == 0)
      return S"debug";
   if (idx == 1)
      return S"profile";
   return get_user_func_name(xp, idx - 2);
}

// Copy a global callback function to a book-local callback.
private void
copyGlobalToBookLocalCb(Callback* globcb, Callback* bookCb) {
   evFreeCallback(bookCb);
   if (globcb->name && *globcb->name != ZERO)
      evCopyCallback(bookCb, globcb);
}

//Parse the @completefunc option value and set the callback function. Invoked when @completefunc 
//is set. The option value can be a name of a function (string), or function(<name>) or 
//funcref(<name>) or a lambda expression.
pub CS
setCompletefunc(OptionChange* cha) {
   CS new = cha->newVal.string;
   if (!new || *new == ZERO)
      return e_invalid_argument;
   if (optSetCallback(OUT &completeFnS, new) == FAIL)
      return e_invalid_argument;

   copyGlobalToBookLocalCb(&completeFnS, curBook->o.completeFn);

   return NULL;
}

// Copy the global @omnifunc callback function to the book-local @omnifunc callback for "book".
pub void
inSetOmniCbForBook(Book* book) {
   copyGlobalToBookLocalCb(&omniFnS, book->o.omniFn);
}

//Copy the global @tagfunc callback function to the book-local 'tagfunc' callback for 'book'.
pub void
inSetTagCbForBook(Book* book) {
   evFreeCallback(book->o.tagFn);
   if (thesaurusCbS.name && *thesaurusCbS.name != ZERO)
      evCopyCallback(OUT book->o.tagFn, &thesaurusCbS);
}

//Copy global custom 'complete' F{func} callbacks into the given book's local
//callback array. Clear any existing book-local callbacks first.
pub void
inSetCustomCompletionCbForBook(Book* book) {
   evFreeCallback(book->o.completeFn);
   if (customCompleteFnS.name && *customCompleteFnS.name != ZERO)
      evCopyCallback(OUT book->o.completeFn, &customCompleteFnS);
}

//Parse the @omnifunc option value and set the callback function.
//Invoked when the @omnifunc option is set. The option value can be a
//name of a function (string), or function(<name>) or funcref(<name>) or a lambda expression.
pub CS
setOmnifunc(OptionChange* cha) {
   if (!cha->newVal.string || *cha->newVal.string == ZERO)
      return e_invalid_argument;
      
   if (optSetCallback(OUT &omniFnS, cha->newVal.string) == FAIL)
      return e_invalid_argument;

   inSetOmniCbForBook(curBook);
   return NULL;
}

//Parse @complete option and initialize F{func} callbacks. Free any existing callbacks and 
//allocate new ones. Only F{func} entries are processed; others are ignored.
pub Unt
setCompletionCallbacks(OptionChange *cha) {
   if (!curBook)
      return FAIL;

   Byte buf[LSIZE];
   evFreeCallback(curBook->o.completeFn);

   curBook->o.completeFn = ALLOC_CLEAR_MULT(Callback, 1);

   for (CS p = curBook->o.complete; *p != ZERO; ) {
      while (*p == ',' || *p == ' ')
         p++; // Skip delimiters

      if (*p != ZERO) {
         int slen = strCutPathFromListOfPaths(OUT &p, OUT buf, LSIZE, S","); // Advance p
         if (slen > 0 && buf[0] == 'F' && buf[1] != ZERO) {
            CS caret = firstOccurrence(buf, '^');
            if (caret)
               *caret = ZERO;

            if (optSetCallback(OUT curBook->o.completeFn, buf + 1) != OK)
               curBook->o.completeFn->name = NULL;
         }
      }
   }

   if (cha->setScope == SET_GLOBAL // ':setglobal' used insted of ':set'
      // Cache the callback array
      && copyCompletionCbs(
            &completeFnS, curBook->o.completeFn
         ) != OK
   )
      return FAIL;

   return OK;
}

//}}}
//{{{text formatting

private int   did_add_space = false;   // auto_format() added an extra space under the cursor

#define WHITECHAR(cc) (SPACE_OR_TAB(cc) && (!utf_iscomposing(mb_ptr2char(ml_get_cursor() + 1))))

//Return true if format option 'x' is in effect.
pub Boole
has_format_option(int x) {
   return curBook->o.formatOptions && firstOccurrence(curBook->o.formatOptions, x) != NULL;
}

//Write a character at the current cursor position. It is directly written into the block.
private void
pchar_cursor(int c) {
   *(memGetLine(curBook, curPor->cursor.lnum, true) + curPor->cursor.col) = c;
}

//Format text at the current insert position.
//If the INSCHAR_COM_LIST flag is present, then the value of second_indent
//will be the comment leader length sent to openLine().
pub void
internal_format(
   int textwidth,
   int second_indent,
   int flags,
   int format_only,
   Unt c // character to be inserted (can be ZERO)
){
   int cc;
   int skip_pos;
   int save_char = ZERO;
   int haveto_redraw = false;
   int fo_ins_blank = has_format_option(FO_INS_BLANK);
   int fo_multibyte = has_format_option(FO_MBYTE_BREAK);
   int fo_rigor_tw  = has_format_option(FO_RIGOROUS_TW);
   int fo_white_par = has_format_option(FO_WHITE_PAR);
   int first_line = true;
   ColNr   leader_len;
   int no_leader = false;
   int doComments = (flags & INSCHAR_DO_COM);
   int safe_tw = trim_to_int(8 * (Long)textwidth);
   int has_bri = curPor->o.breakIndent;

   // make sure win_lbr_chartabsize() counts correctly
   curPor->o.breakIndent = false;

   // When 'ai' is off we don't want a space under the cursor to be
   // deleted.  Replace it with an 'x' temporarily.
   if (!curBook->o.autoIndent) {
      cc = gchar_cursor();
      if (SPACE_OR_TAB(cc)) {
         save_char = cc;
         pchar_cursor('x');
      }
   }

   // Repeat breaking lines, until the current line is not too long.
   while (!gotInterruptG) {
      int startcol;      // Cursor column at entry
      int wantcol;      // column at textwidth border
      int foundcol;      // column for start of spaces
      ColNr len;
      ColNr virtcol;
      ColNr col;
      int wcc;         // counter for whitespace chars
      int did_do_comment = false;
      int first_pass;

      //Cursor is currently at the end of line. No need to format
      //if line length is less than textwidth (8 * textwidth for utf safety)
      if (curPor->cursor.col < safe_tw) {
         virtcol = get_nolist_virtcol() + bookChar2Cells(c != ZERO ? c : gchar_cursor());
         if (virtcol <= (ColNr)textwidth)
            break;
      }

      if (no_leader)
         doComments = false;
      ei (!(flags & INSCHAR_FORMAT) && has_format_option(FO_WRAP_COMS))
         doComments = true;

      // Don't break until after the comment leader
      if (doComments) {
         CS line = ml_get_curline();

         leader_len = get_leader_len(line, NULL, false, true);
      } else
         leader_len = 0;

     //If the line doesn't start with a comment leader, then don't
     //start one in a following broken line.  Avoids that a %word
     //moved to the start of the next line causes all following lines to start with %.
     if (leader_len == 0)
         no_leader = true;
     if (!(flags & INSCHAR_FORMAT) && leader_len == 0 && !has_format_option(FO_WRAP))
         break;
     if ((startcol = curPor->cursor.col) == 0)
         break;

      // find column of textwidth border
      coladvance((ColNr)textwidth);
      wantcol = curPor->cursor.col;

      // If startcol is large (a long line), formatting takes too much
      // time. The algorithm is O(n^2), it walks from the end of the
      // line to textwidth border every time for each line break.
      //
      // Ceil to 8 * textwidth to optimize.
      curPor->cursor.col = startcol < safe_tw ? startcol : safe_tw;

      foundcol = 0;
      skip_pos = 0;
      first_pass = true;

      // Find position to break at. Stop at first entered white when 'formatoptions' has 'v'
      while ((!fo_ins_blank && !has_format_option(FO_INS_VI))
             || (flags & INSCHAR_FORMAT)
             || curPor->cursor.lnum != insertStartG.lnum
             || curPor->cursor.col >= insertStartG.col
      ){
        if (first_pass && c != ZERO) {
            cc = c;
            first_pass = false;
         } else
            cc = gchar_cursor();
         if (WHITECHAR(cc)) {

            // find start of sequence of blanks
            wcc = 0;
            while (curPor->cursor.col > 0 && WHITECHAR(cc)) {
               dec_cursor();
               cc = gchar_cursor();

               // Increment count of how many whitespace chars in this
               // group; we only need to know if it's more than one.
               if (wcc < 2)
                  wcc++;
           }
           
           if (curPor->cursor.col == 0 && WHITECHAR(cc))
               break;      // only spaces in front of text

            // Don't break after a period when 'formatoptions' has 'p' and
            // there are less than two spaces.
            if (has_format_option(FO_PERIOD_ABBR) && cc == '.' && wcc < 2)
               continue;

            // Don't break until after the comment leader
            if (curPor->cursor.col < leader_len)
               break;
            if (has_format_option(FO_ONE_LETTER)) {
               // do not break after one-letter words
               if (curPor->cursor.col == 0)
                  break;   // one-letter word at begin
               // do not break "#a b" when 'tw' is 2
               if (curPor->cursor.col <= leader_len)
                  break;
               col = curPor->cursor.col;
               dec_cursor();
               cc = gchar_cursor();

               if (WHITECHAR(cc))
                  continue;   // one-letter, continue
               curPor->cursor.col = col;
            }

            inc_cursor();

            foundcol = curPor->cursor.col;
            if (curPor->cursor.col <= (ColNr)wantcol)
                break;
         } ei ((cc >= 0x100 || !utf_allow_break_before(cc)) && fo_multibyte){
            Unt ncc;
            int allow_break;

            // Break after or before a multi-byte character.
            if (curPor->cursor.col != startcol) {
               // Don't break until after the comment leader
               if (curPor->cursor.col < leader_len)
                  break;
               col = curPor->cursor.col;
               inc_cursor();
               ncc = gchar_cursor();

               allow_break = utf_allow_break(cc, ncc);

               // If we have already checked this position, skip!
               if (curPor->cursor.col != skip_pos && allow_break) {
               foundcol = curPor->cursor.col;
               if (curPor->cursor.col <= (ColNr)wantcol)
                   break;
               }
               curPor->cursor.col = col;
            }

            if (curPor->cursor.col == 0)
               break;

            ncc = cc;
            col = curPor->cursor.col;

            dec_cursor();
            cc = gchar_cursor();

            if (WHITECHAR(cc))
                continue;      // break with space
            // Don't break until after the comment leader.
            if (curPor->cursor.col < leader_len)
                break;

            curPor->cursor.col = col;
            skip_pos = curPor->cursor.col;

            allow_break = (utf_allow_break(cc, ncc));

            // Must handle this to respect line break prohibition.
            if (allow_break) {
               foundcol = curPor->cursor.col;
            }
            if (curPor->cursor.col <= (ColNr)wantcol) {
               int ncc_allow_break = utf_allow_break_before(ncc);

               if (allow_break)
                  break;
               if (!ncc_allow_break && !fo_rigor_tw) {
                  //Enable at most 1 punct hang outside of textwidth.
                  if (curPor->cursor.col == startcol) {
                     //We are inserting a non-breakable char, postpone
                     //line break check to next insert.
                     break;
                  }

                  //Neither cc nor ncc is ZERO if we are here, so it's safe to inc_cursor.
                  col = curPor->cursor.col;

                  inc_cursor();
                  cc  = ncc;
                  ncc = gchar_cursor();
                  //handle insert
                  ncc = (ncc != ZERO) ? ncc : c;

                  allow_break = (utf_allow_break(cc, ncc));

                  if (allow_break) {
                     // Break only when we are not at end of line.
                     break;
                  }
                  curPor->cursor.col = col;
               }
            }
         }
         if (curPor->cursor.col == 0)
            break;
         dec_cursor();
      }

      if (foundcol == 0) {     // no spaces, cannot break line
         curPor->cursor.col = startcol;
         break;
      }

      //adjust startcol for spaces that will be deleted and
      //characters that will remain on top line
      curPor->cursor.col = foundcol;
      while ((cc = gchar_cursor(), WHITECHAR(cc)) && (!fo_white_par || curPor->cursor.col < startcol))
         inc_cursor();
      startcol -= curPor->cursor.col;
      if (startcol < 0)
         startcol = 0;

      // put cursor after pos. to break line
      if (!fo_white_par)
         curPor->cursor.col = foundcol;

      // Split the line just before the margin.
      // Only insert/delete lines, but don't really redraw the window.
      openLine(OPENLINE_DELSPACES + OPENLINE_MARKFIX
             + (fo_white_par ? OPENLINE_KEEPTRAIL : 0)
             + (doComments ? OPENLINE_DO_COM : 0)
             + OPENLINE_FORMAT
             + ((flags & INSCHAR_COM_LIST) ? OPENLINE_COM_LIST : 0),
          ((flags & INSCHAR_COM_LIST) ? second_indent : old_indent)
      );
      if (!(flags & INSCHAR_COM_LIST))
          old_indent = 0;

      // If a comment leader was inserted, may also do this on a following line.
      if (did_do_comment)
          no_leader = false;

      if (first_line) {
          if (!(flags & INSCHAR_COM_LIST)) {
             //This section is for auto-wrap of numeric lists. When not in insert mode (i.e. 
             //format_lines()), the INSCHAR_COM_LIST flag will be set and openLine() will handle 
             //it (as seen above). The code here (and in get_number_indent()) will recognize 
             //comments if needed...
             if (second_indent < 0 && has_format_option(FO_Q_NUMBER))
                 second_indent = get_number_indent(curPor->cursor.lnum - 1);
             if (second_indent >= 0) {
               if (leader_len > 0 && second_indent - leader_len > 0) {
                   int padding = second_indent - leader_len;

                   //We started at the first_line of a numbered list that has a comment. the 
                   //openLine() function has inserted the proper comment leader and positioned
                   //the cursor at the end of the split line. Now we add the additional whitespace 
                   //needed after the comment leader for the numbered list.
                   for (int i = 0; i < padding; i++)
                      ins_str((CS)" ", 1);
                } else {
                   (void)set_indent(second_indent, SIN_CHANGED);
                }
             }
          }
          first_line = false;
      }

      // Check if cursor is not past the ZERO off the line, cindent
      // may have added or removed indent.
      curPor->cursor.col += startcol;
      len = ml_get_curline_len();
      if (curPor->cursor.col > len)
         curPor->cursor.col = len;

      haveto_redraw = true;
      set_can_cindent(true);
      // moved the cursor, don't autoindent or cindent now
      didAindentG = false;
      didSindentG = false;
      can_si = false;
      can_si_back = false;
      line_breakcheck();
   }

   if (save_char != ZERO)      // put back space after cursor
      pchar_cursor(save_char);

   curPor->o.breakIndent = has_bri;
   if (!format_only && haveto_redraw) {
      update_topline();
      drawCurBookLater(UPD_VALID);
   }
}

//Blank lines, and lines containing only the comment leader, are left untouched by the formatting.
//The function returns true in this case.  It also returns true when a line starts with the end 
//of a comment ('e' in comment flags), so that this line is skipped, and not joined to the
//previous line.  A new paragraph starts after a blank line, or when the
//comment leader changes -- webb.
pub int
fmt_check_par(LineNr lnum, OUT int* leader_len, OUT CS* leader_flags, int doComments) {
   CS flags = NULL;

   CS ptr = ml_get(lnum);
   *leader_len = doComments ? get_leader_len(ptr, leader_flags, false, true) : 0;

   if (*leader_len > 0) {
      // Search for 'e' flag in comment leader flags.
      flags = *leader_flags;
      while (*flags && *flags != ':' && *flags != COM_END)
          ++flags;
   }

   return (*skipwhite(ptr + *leader_len) == ZERO
       || (*leader_len > 0 && *flags == COM_END)
       || startPS(lnum, ZERO, false));
}

//Return true when a paragraph starts in line "lnum".  Return false when the
//previous line is in the same paragraph.  Used for auto-formatting.
private int
paragraph_start(LineNr lnum) {
   int leader_len = 0;      // leader len of current line
   CS leader_flags = NULL;   // flags for leader of current line
   int next_leader_len;   // leader len of next line
   CS next_leader_flags;   // flags for leader of next line
   int doComments;      // format comments

   if (lnum <= 1)
      return true;      // start of the file

   CS p = ml_get(lnum - 1);
   if (*p == ZERO)
      return true;      // after empty line

   doComments = has_format_option(FO_Q_COMS);
   if (  // after non-paragraph line
         fmt_check_par(lnum - 1, OUT &leader_len, OUT &leader_flags, doComments)
         // "lnum" is not a paragraph line
         || fmt_check_par(lnum, OUT &next_leader_len, OUT &next_leader_flags, doComments)
         // missing trailing space in previous line.
         || (has_format_option(FO_WHITE_PAR) && !ends_in_white(lnum - 1))
         // numbered item starts in "lnum".
         || (has_format_option(FO_Q_NUMBER) && get_number_indent(lnum) > 0)
         // change of comment leader.
         ||  !same_leader(lnum - 1, leader_len, leader_flags, next_leader_len, next_leader_flags)
   ){
      return true;      
   } 

   return false;
}

//Called after inserting or deleting text: When 'formatoptions' includes the
//'a' flag format from the current line until the end of the paragraph.
//Keep the cursor at the same position relative to the text.
//The caller must have saved the cursor line for undo, following ones will be saved here.
pub void
auto_format(
    int trailblank,   // when true also format with trailing blank
    int prev_line   // may start in previous line
){
   if (!has_format_option(FO_AUTO))
      return;

   Pos pos = curPor->cursor;
   CS old = ml_get_curline();

   // may remove added space
   check_auto_format(false);

   //Don't format in Insert mode when the cursor is on a trailing blank, the user might insert 
   //normal text next. Also skip formatting when "1" is in 'formatoptions' and there is a single 
   //character before the cursor. Otherwise the line would be broken and when typing another 
   //non-white next they are not joined back together.
   int wasatend = (pos.col == ml_get_curline_len());
   if (*old != ZERO && !trailblank && wasatend) {
      dec_cursor();
      int cc = gchar_cursor();
      if (!WHITECHAR(cc) && curPor->cursor.col > 0 && has_format_option(FO_ONE_LETTER))
         dec_cursor();
      cc = gchar_cursor();
      if (WHITECHAR(cc)) {
         curPor->cursor = pos;
         return;
      }
      curPor->cursor = pos;
   }

   //With the 'c' flag in @formatoptions and 't' missing: only format comments.
   if (has_format_option(FO_WRAP_COMS) && !has_format_option(FO_WRAP)
            && get_leader_len(old, NULL, false, true) == 0
   )
      return;

   //May start formatting in a previous line, so that after "x" a word is moved to the previous 
   //line if it fits there now.  Only when this is not the start of a paragraph.
   if (prev_line && !paragraph_start(curPor->cursor.lnum)) {
      --curPor->cursor.lnum;
   if (u_save_cursor() == FAIL)
       return;
   }

   //Do the formatting and restore the cursor position.  "saved_cursor" will
   //be adjusted for the text formatting.
   saved_cursor = pos;
   format_lines((LineNr)-1, false);
   curPor->cursor = saved_cursor;
   saved_cursor.lnum = 0;

   if (curPor->cursor.lnum > curBook->mem.lineCount) {
      // "cannot happen"
      curPor->cursor.lnum = curBook->mem.lineCount;
      coladvance((ColNr)MAXCOL);
   } else
      check_cursor_col();

   //Insert mode: If the cursor is now after the end of the line while it
   //previously wasn't, the line was broken.  Because of the rule above we
   //need to add a space when 'w' is in 'formatoptions' to keep a paragraph formatted.
   if (!wasatend && has_format_option(FO_WHITE_PAR)) {
      CS new = ml_get_curline();
      ColNr   len = ml_get_curline_len();
      if (curPor->cursor.col == len) {
         CS pnew = copySubstr(new, len + 2);
         pnew[len] = ' ';
         pnew[len + 1] = ZERO;
         ml_replace(curPor->cursor.lnum, pnew, false);
         // remove the space later
         did_add_space = true;
      } else
         // may remove added space
         check_auto_format(false);
   }

   check_cursor();
}

//When an extra space was added to continue a paragraph for auto-formatting,
//delete it now.  The space must be under the cursor, just after the insert position.
pub void
check_auto_format(int end_insert){      // true when ending Insert mode
   if (!did_add_space)
      return;

   Unt c = ' ';
   Unt cc = gchar_cursor();
   if (!WHITECHAR(cc))
      // Somehow the space was removed already.
      did_add_space = false;
   else {
      if (!end_insert) {
         inc_cursor();
         c = gchar_cursor();
         dec_cursor();
      }
      if (c != ZERO) {
         // The space is no longer at the end of the line, delete it.
         del_char(false);
         did_add_space = false;
      }
   }
}

//Find out textwidth to be used for formatting:
// if 'textwidth' option is set, use it
// ei 'wrapmargin' option is set, use curPor->width - 'wrapmargin'
// if invalid value, use 0.
// Set default to window width (maximum 79) for "gq" operator.
pub int
comp_textwidth(int ff) {  // force formatting (for "gq" command)
   int textwidth = curBook->o.textWidth;
   if (textwidth == 0 && curBook->o.wrapMargin) {
      //The width is the portal width minus 'wrapmargin' minus all the
      //things that add to the margin.
      textwidth = curPor->width - curBook->o.wrapMargin;
      if (curBook == commPortBookG)
          textwidth -= 1;
      if (isSigncolumnOn(curPor))
         textwidth -= 1;
      if (curPor->o.relativeNumber)
         textwidth -= 8;
   }
   if (textwidth < 0)
      textwidth = 0;
   if (ff && textwidth == 0) {
      textwidth = curPor->width - 1;
      if (textwidth > 79)
          textwidth = 79;
   }
   return textwidth;
}

//}}}
//}}}
