// EEGL - the Extensible development Environment for GNU/Linux
// Licensed under GPLv3 (c) Egor Sozonov

//## eval.c: evaluation of functions

#include "eegl.h"

#define USING_FLOAT_STUFF
//{{{forward decls

private void f_and(Var *argvars, Var *returnVar);
private void f_balloon_gettext(Var *argvars, Var *returnVar);
private void f_balloon_show(Var *argvars, Var *returnVar);
private void f_balloon_split(Var *argvars, Var *returnVar);
private void f_base64_encode(Var *argvars, Var *returnVar);
private void f_base64_decode(Var *argvars, Var *returnVar);
private void f_bindtextdomain(Var *argvars, Var *returnVar);
private void f_byte2line(Var *argvars, Var *returnVar);
private void f_call(Var *argvars, Var *returnVar);
private void f_changenr(Var *argvars, Var *returnVar);
private void f_char2nr(Var *argvars, Var *returnVar);
private void f_charcol(Var *argvars, Var *returnVar);
private void f_col(Var *argvars, Var *returnVar);
private void f_confirm(Var *argvars, Var *returnVar);
private void f_copy(Var *argvars, Var *returnVar);
private void f_cursor(Var *argsvars, Var *returnVar);
private void f_deepcopy(Var *argvars, Var *returnVar);
private void f_did_filetype(Var *argvars, Var *returnVar);
private void f_echoraw(Var *argvars, Var *returnVar);
private void f_empty(Var *argvars, Var *returnVar);
private void f_environ(Var *argvars, Var *returnVar);
private void f_err_teapot(Var *argvars, Var *returnVar);
private void f_escape(Var *argvars, Var *returnVar);
private void f_eval(Var *argvars, Var *returnVar);
private void f_eventhandler(Var *argvars, Var *returnVar);
private void f_execute(Var *argvars, Var *returnVar);
private void f_expand(Var *argvars, Var *returnVar);
private void f_expandcmd(Var *argvars, Var *returnVar);
private void f_feedkeys(Var *argvars, Var *returnVar);
private void f_fnameescape(Var *argvars, Var *returnVar);
private void f_funcref(Var *argvars, Var *returnVar);
private void f_function(Var *argvars, Var *returnVar);
private void f_garbagecollect(Var *argvars, Var *returnVar);
private void f_get(Var *argvars, Var *returnVar);
private void f_getcellpixels(Var *argvars, Var *returnVar);
private void f_getchangelist(Var *argvars, Var *returnVar);
private void f_getcharpos(Var *argvars, Var *returnVar);
private void f_getcharsearch(Var *argvars, Var *returnVar);
private void f_getcurpos(Var *argvars, Var *returnVar);
private void f_getcursorcharpos(Var *argvars, Var *returnVar);
private void f_getenv(Var *argvars, Var *returnVar);
private void f_getfontname(Var *argvars, Var *returnVar);
private void f_getjumplist(Var *argvars, Var *returnVar);
private void f_getpid(Var *argvars, Var *returnVar);
private void f_getpos(Var *argvars, Var *returnVar);
private void f_getreg(Var *argvars, Var *returnVar);
private void f_getreginfo(Var *argvars, Var *returnVar);
private void f_getregion(Var *argvars, Var *returnVar);
private void f_getregionpos(Var *argvars, Var *returnVar);
private void f_getregtype(Var *argvars, Var *returnVar);
private void f_gettagstack(Var *argvars, Var *returnVar);
private void f_gettext(Var *argvars, Var *returnVar);
private void f_haslocaldir(Var *argvars, Var *returnVar);
private void f_index(Var *argvars, Var *returnVar);
private void f_indexof(Var *argvars, Var *returnVar);
private void f_input(Var *argvars, Var *returnVar);
private void f_inputdialog(Var *argvars, Var *returnVar);
private void f_inputlist(Var *argvars, Var *returnVar);
private void f_inputrestore(Var *argvars, Var *returnVar);
private void f_inputsave(Var *argvars, Var *returnVar);
private void f_inputsecret(Var *argvars, Var *returnVar);
private void f_interrupt(Var *argvars, Var *returnVar);
private void f_invert(Var *argvars, Var *returnVar);
private void f_islocked(Var *argvars, Var *returnVar);
private void f_keytrans(Var *argvars, Var *returnVar);
private void f_last_buffer_nr(Var *argvars, Var *returnVar);
private void f_line(Var *argvars, Var *returnVar);
private void f_line2byte(Var *argvars, Var *returnVar);
private void f_match(Var *argvars, Var *returnVar);
private void f_matchbufline(Var *argvars, Var *returnVar);
private void f_matchend(Var *argvars, Var *returnVar);
private void f_matchlist(Var *argvars, Var *returnVar);
private void f_matchstr(Var *argvars, Var *returnVar);
private void f_matchstrlist(Var *argvars, Var *returnVar);
private void f_matchstrpos(Var *argvars, Var *returnVar);
private void f_max(Var *argvars, Var *returnVar);
private void f_min(Var *argvars, Var *returnVar);
private void f_nextnonblank(Var *argvars, Var *returnVar);
private void f_ngettext(Var *argvars, Var *returnVar);
private void f_nr2char(Var *argvars, Var *returnVar);
private void f_or(Var *argvars, Var *returnVar);
private void f_prevnonblank(Var *argvars, Var *returnVar);
private void f_printf(Var *argvars, Var *returnVar);
private void f_pum_getpos(Var *argvars, Var *returnVar);
private void f_pumvisible(Var *argvars, Var *returnVar);
private void f_test_srand_seed(Var *argvars, Var *returnVar);
private void f_rand(Var *argvars, Var *returnVar);
private void f_range(Var *argvars, Var *returnVar);
private void f_reg_executing(Var *argvars, Var *returnVar);
private void f_reg_recording(Var *argvars, Var *returnVar);
private void f_rename(Var *argvars, Var *returnVar);
private void f_repeat(Var *argvars, Var *returnVar);
private void f_screenattr(Var *argvars, Var *returnVar);
private void f_screenchar(Var *argvars, Var *returnVar);
private void f_screenchars(Var *argvars, Var *returnVar);
private void f_screencol(Var *argvars, Var *returnVar);
private void f_screenrow(Var *argvars, Var *returnVar);
private void f_screenstring(Var *argvars, Var *returnVar);
private void f_search(Var *argvars, Var *returnVar);
private void f_searchdecl(Var *argvars, Var *returnVar);
private void f_searchpair(Var *argvars, Var *returnVar);
private void f_searchpairpos(Var *argvars, Var *returnVar);
private void f_searchpos(Var *argvars, Var *returnVar);
private void f_setcharpos(Var *argvars, Var *returnVar);
private void f_setcharsearch(Var *argvars, Var *returnVar);
private void f_setcursorcharpos(Var *argvars, Var *returnVar);
private void f_setenv(Var *argvars, Var *returnVar);
private void f_setfperm(Var *argvars, Var *returnVar);
private void f_setpos(Var *argvars, Var *returnVar);
private void f_setreg(Var *argvars, Var *returnVar);
private void f_settagstack(Var *argvars, Var *returnVar);
private void f_sha256(Var *argvars, Var *returnVar);
private void f_shellescape(Var *argvars, Var *returnVar);
private void f_shiftwidth(Var *argvars, Var *returnVar);
private void f_spellbadword(Var *argvars, Var *returnVar);
private void f_spellsuggest(Var *argvars, Var *returnVar);
private void f_split(Var *argvars, Var *returnVar);
private void f_srand(Var *argvars, Var *returnVar);
private void f_submatch(Var *argvars, Var *returnVar);
private void f_substitute(Var *argvars, Var *returnVar);
private void f_swapfilelist(Var *argvars, Var *returnVar);
private void f_swapinfo(Var *argvars, Var *returnVar);
private void f_swapname(Var *argvars, Var *returnVar);
private void f_synID(Var *argvars, Var *returnVar);
private void f_synIDattr(Var *argvars, Var *returnVar);
private void f_synIDtrans(Var *argvars, Var *returnVar);
private void f_synstack(Var *argvars, Var *returnVar);
private void f_synconcealed(Var *argvars, Var *returnVar);
private void f_tabpagebuflist(Var *argvars, Var *returnVar);
private void f_taglist(Var *argvars, Var *returnVar);
private void f_tagfiles(Var *argvars, Var *returnVar);
private void f_type(Var *argvars, Var *returnVar);
private void f_virtcol(Var *argvars, Var *returnVar);
private void f_visualmode(Var *argvars, Var *returnVar);
private void f_wildmenumode(Var *argvars, Var *returnVar);
private void f_wordcount(Var *argvars, Var *returnVar);
private void f_xor(Var *argvars, Var *returnVar);


private int letVars(
   CS arg_start, Var   *tv, Boole copy, int semicolon, int var_count, Unt flags, CS op
);
private DictItem* findVarAndSetHtable(Text name, OUT EeSet** htp, Boole no_autoload);
private int setVarImpl(Text name, ScriptId sid, Var* tv_arg, Boole copy, Unt flags_arg);
private int eval_variable(Text name, Var* returnVar, DictItem   **dip, int flags);
private void check_vars(Text name);
private int eval0_retarg(
   CS arg, Var* returnVar, Invocation* invo, EvalCtx* evalarg, CS* retarg
);

//}}}
//{{{Vimscript definition

// words that cannot be used as a variable
// Keep this array sorted, as bsearch() is used to search this array.
private CS reserved[] = {SMAP((CS),
   "false",
   "null",
   "null_blob",
   "null_channel",
   "null_dict",
   "null_function",
   "null_job",
   "null_list",
   "null_partial",
   "null_string",
   "true"
)};

// String compare function used for bsearch()
private int
compareNames(const void *s1, const void *s2) {
   return STRCMP(*(char **)s1, *(char **)s2);
}

// Return OK if "name" is not a reserved keyword. Otherwise FAIL.
int
checkIfNameReserved(CS name, int is_objm_access) {
   // "this" can be used in an object method
   if (is_objm_access && STRCMP("this", name) == 0)
      return OK;

   if (bsearch(
            &name, reserved, ARRAY_LENGTH(reserved), sizeof(reserved[0]), compareNames) != NULL
   ) {
      showErrFmtMsg(_(e_cannot_use_reserved_name_str), name);
      return FAIL;
   }

   return OK;
}


//}}}
//{{{expression evaluation 

//This specifies optional parameters for getLval(). Arguments may be NULL.

typedef struct {
   Var* var;    // Base internal var.
   int isArg;   // name is an arg (not a member).
} LvalRoot;

//Flags for eval_variable().
#define EVAL_VAR_VERBOSE    1   // may give error message
#define EVAL_VAR_NOAUTOLOAD 2   // do not use script autoloading
#define EVAL_VAR_NO_FUNC    4   // do not look for a function

private LvalRoot* lvalRootS = null;

#define USING_FLOAT_STUFF

// Get pointer to item in the stack.
#define STACK_TV(idx) (((Var *)ectx->ec_stack.c) + idx)

// Get pointer to item relative to the bottom of the stack, -1 is the last one.
#define STACK_TV_BOT(idx) (((Var *)ectx->ec_stack.c) + ectx->ec_stack.len + (idx))

#define NAMESPACE_CHAR   S"abglstvw"

// Double linked list of LoopVars in use.
private LoopVars* firstLoopvarS = NULL;

private int eval2(OUT CS* arg, Var* returnVar, EvalCtx *evalarg);
private int eval3(OUT CS* arg, Var* returnVar, EvalCtx *evalarg);
private int eval4(OUT CS* arg, Var* returnVar, EvalCtx *evalarg);
private int eval5(OUT CS* arg, Var* returnVar, EvalCtx *evalarg);
private int eval6(OUT CS* arg, Var* returnVar, EvalCtx *evalarg);
private int eval7(OUT CS* arg, Var* returnVar, EvalCtx *evalarg, Boole want_string);
private int eval8(OUT CS* arg, Var* returnVar, EvalCtx *evalarg, Boole want_string);
private int eval9(OUT CS* arg, Var* returnVar, EvalCtx *evalarg, Boole want_string);
private int eval9_leader(Var* returnVar, int numeric_only, CS start_leader, Byte **end_leaderp);
private void initGlobalAndSpecialVars(void);
private Text expandCurlyBraces(Text braces, Text outer);

//Return "n1" divided by "n2", taking care of dividing by zero.
//If "failed" is not NULL set it to TRUE when dividing by zero fails.
Long
num_divide(Long n1, Long n2, OUT Boole* failed) {
   Long   result;

   if (n2 == 0) {
	   emsg(_(e_divide_by_zero));
	   if (failed)
         *failed = true;
      if (n1 == 0)
         result = VARNUM_MIN; // similar to NaN
      ei (n1 < 0)
         result = -VARNUM_MAX;
      else
         result = VARNUM_MAX;
   } ei (n1 == VARNUM_MIN && n2 == -1) {
      //specific case: trying to do VARNUM_MIN / -1 results in a positive
      //number that doesn't fit in Long and causes an FPE
      result = VARNUM_MAX;
   } else
      result = n1 / n2;

   return result;
}

//Return "n1" modulus "n2", taking care of dividing by zero.
//If "failed" is not NULL set it to TRUE when dividing by zero fails.
Long
num_modulus(Long n1, Long n2, OUT Boole* failed) {
   if (n2 == 0) {
      emsg(_(e_divide_by_zero));
      if (failed)
         *failed = true;
   }
   return n1 % n2;
}

// Initialize the global and v: variables.
void
evalInitGlobals(void) {
   initGlobalAndSpecialVars();
   func_init();
}

#if defined(EXITFREE) || defined(PROTO)
void
eval_clear(void) {
   evalvars_clear();
   free_scriptnames();  // must come after evalvars_clear().
   free_locales();

   // autoloaded script names
   free_autoload_scriptnames();

   // unreferenced lists and dicts
   (void)garbage_collect(FALSE);

   // functions not garbage collected
   free_all_functions();
}
#endif

void
fillEvalArgFromInvo(OUT EvalCtx *evalarg, Invocation *invo, int skip) {
   init_evalarg(evalarg);
   evalarg->eval_flags = skip ? 0 : EVAL_EVALUATE;

   if (!invo)
      return;

   evalarg->eval_cstack = invo->cstack;
   if (sourcing_a_script(invo) || invo->ea_getline == get_list_line) {
      evalarg->eval_getline = invo->ea_getline;
      evalarg->eval_cookie = invo->cookie;
   }
}

// Top level evaluation function, returning a boolean.
// Sets "error" to TRUE if there was an error. Return TRUE or FALSE.
int
eval_to_bool(
   CS arg,
   OUT Boole* error,
   Invocation* invo,
   Boole skip,       // only parse, don't execute
   Boole use_simple_function
){
   Var   tv;
   Long   retval = FALSE;
   EvalCtx   evalarg;
   int      r;

   fillEvalArgFromInvo(OUT &evalarg, invo, skip);

   if (skip)
      ++emsg_skip;
   if (use_simple_function)
      r = eval0_simple_funccal(arg, &tv, invo, &evalarg);
   else
      r = eval0(arg, &tv, invo, &evalarg);
   if (r == FAIL)
      *error = TRUE;
   else {
      *error = FALSE;
      if (!skip) {
         retval = (varGetNumberChk(&tv, OUT error) != 0);
         clearVar(&tv);
      }
   }
   if (skip)
      --emsg_skip;
   clear_evalarg(&evalarg, invo);

   return (int)retval;
}

// Call eval1() and give an error message if not done at a lower level.
private int
eval1_emsg(Byte **arg, Var* returnVar, Invocation* invo) {
   Byte* start = *arg;
   int anyEmsgG_before = anyEmsgG;
   int called_emsg_before = called_emsg;
   EvalCtx evalarg;

   fillEvalArgFromInvo(OUT &evalarg, invo, invo && invo->skip);

   int ret = eval1(OUT arg, returnVar, &evalarg);
   if (ret == FAIL) {
      //Report the invalid expression unless the expression evaluation has been cancelled due to 
      //an aborting error, an interrupt, or an exception, or we already gave a more specific error.
      //Also check called_emsg for when using assert_fails().
      if (!aborting() && anyEmsgG == anyEmsgG_before && called_emsg == called_emsg_before)
         showErrFmtMsg(_(e_invalid_expression_str), start);
   }
   clear_evalarg(&evalarg, invo);
   return ret;
}

//Return whether a typval is a valid expression to pass to eval_expr_typval()
//or eval_expr_to_bool(). For an empty string return FALSE;
Boole
eval_expr_valid_arg(Var *tv) {
   return tv->tag != VAR_UNKNOWN
          && (tv->tag != VAR_STRING || (tv->string && *tv->string != ZERO));
}


//Evaluate a partial. Pass arguments "argv[argc]".
//Return the result in "returnVar" and OK or FAIL.
private int
partialEvalExp(Var* expr, Var* argv, int argc, Var* returnVar) {
   PartiallyApplied   *partial = expr->partial;
   if (!partial)
      return FAIL;

   Byte* s = partial_name(partial);
   FnExe funcexe;

   if (!s || *s == ZERO)
      return FAIL;

   CLEAR_FIELD(funcexe);
   funcexe.fe_evaluate = TRUE;
   funcexe.fe_partial = partial;
   if (call_func(s, -1, returnVar, argc, argv, &funcexe) == FAIL)
      return FAIL;
      

   return OK;
}

//Evaluate an expression which is a function. Pass arguments "argv[argc]".
//Return the result in "returnVar" and OK or FAIL.
private int
eval_expr_func(
   Var   *expr,
   Var   *argv,
   int      argc,
   Var   *returnVar
) {
   FnExe   funcexe;
   Byte   buf[NUMBUFLEN];
   Byte   *s;

   if (expr->tag == VAR_FUNC)
      s = expr->string;
   else
      s = convertVarToString_strict(expr, buf, FALSE);
   if (!s || *s == ZERO)
      return FAIL;

   CLEAR_FIELD(funcexe);
   funcexe.fe_evaluate = TRUE;
   if (call_func(s, -1, returnVar, argc, argv, &funcexe) == FAIL)
      return FAIL;

   return OK;
}

//Evaluate an expression, which is a string. Return the result in "returnVar" and OK or FAIL.
private int
eval_expr_string(Var* expr, Var* returnVar) {
   Byte buf[NUMBUFLEN];

   CS s = convertVarToString_strict(expr, buf, FALSE);
   if (!s)
      return FAIL;

   s = skipwhite(s);
   if (eval1_emsg(&s, returnVar, NULL) == FAIL)
      return FAIL;

   if (*skipwhite(s) != ZERO) { // check for trailing chars after expr
      clearVar(returnVar);
      showErrFmtMsg(_(e_invalid_expression_str), s);
      return FAIL;
   }

   return OK;
}

//Evaluate an expression, which can be a function, partial or string. Pass arguments "argv[argc]".
//If "want_func" is TRUE treat a string as a function name, not an expression.
//Return the result in "returnVar" and OK or FAIL.
int
eval_expr_typval(
   Var    *expr,
   int       want_func,
   Var    *argv,
   int       argc,
   Var    *returnVar)
{
   if (expr->tag == VAR_PARTIAL)
      return partialEvalExp(expr, argv, argc, returnVar);
   if (expr->tag == VAR_FUNC || want_func)
      return eval_expr_func(expr, argv, argc, returnVar);

   return eval_expr_string(expr, returnVar);
}

//Like eval_to_bool() but using a Var instead of a string. Work for string, funcref and partial.
private Boole
eval_expr_to_bool(Var *expr, OUT Boole* error) {
   Var   returnVar;
   int      res;

   if (eval_expr_typval(expr, false, NULL, 0, OUT &returnVar) == FAIL) {
      *error = true;
      return false;
   }
   res = (varGetNumberChk(&returnVar, OUT error) != 0);
   clearVar(&returnVar);
   return res;
}

//Top level evaluation function, returning a string.  If "skip" is TRUE,
//only parsing to "nextComm" is done, without reporting errors. Return
//pointer to allocated memory, or NULL for failure or when "skip" is TRUE.
CS
eval_to_string_skip(
   CS arg,
   Invocation* invo,
   int      skip)       // only parse, don't execute
{
   Var   tv;
   Byte   *retval;
   EvalCtx   evalarg;

   fillEvalArgFromInvo(OUT &evalarg, invo, skip);
   if (skip)
      ++emsg_skip;
   if (eval0(arg, &tv, invo, &evalarg) == FAIL || skip)
      retval = NULL;
   else {
      retval = copyStr(tv_get_string(&tv));
      clearVar(&tv);
   }
   if (skip)
      --emsg_skip;
   clear_evalarg(&evalarg, invo);

   return retval;
}

// Initialize "evalarg" for use.
void
init_evalarg(EvalCtx *evalarg) {
   CLEAR_POINTER(evalarg);
   ga_init2(&evalarg->eval_tofree_ga, sizeof(CS), 20);
}

//If "evalarg->eval_tofree" is not NULL free it later.
//Caller is expected to overwrite "evalarg->eval_tofree" next.
private void
free_eval_tofree_later(EvalCtx *evalarg) {
   if (evalarg->eval_tofree == NULL)
      return;

   if (ga_grow(&evalarg->eval_tofree_ga, 1) == OK)
      ((Byte **)evalarg->eval_tofree_ga.c)[evalarg->eval_tofree_ga.len++] = evalarg->eval_tofree;
   else
      eeglFree(evalarg->eval_tofree);
}

// After using "evalarg" filled from "invo": free the memory.
void
clear_evalarg(EvalCtx* evalarg, Invocation* invo) {
   if (evalarg == NULL)
      return;

   ArrayList *etga = &evalarg->eval_tofree_ga;

   if (evalarg->eval_tofree || evalarg->eval_using_cmdline) {
      if (invo) {
         // We may need to keep the original command line, e.g. for ":let" it has the variable 
         // names. But we may also need the new one, "nextcmd" points into it. Keep both.
         eeglFree(invo->commlineToFree);
         invo->commlineToFree = *invo->commline;

         if (evalarg->eval_using_cmdline && etga->len > 0) {
            // "nextcmd" points into the last line in eval_tofree_ga, need to keep it around.
            --etga->len;
            *invo->commline = ((Byte **)etga->c)[etga->len];
            eeglFree(evalarg->eval_tofree);
          } else
            *invo->commline = evalarg->eval_tofree;
      } else
         eeglFree(evalarg->eval_tofree);
      evalarg->eval_tofree = NULL;
   }

   ga_clear_strings(etga);
   EE_CLEAR(evalarg->eval_tofree_lambda);
}

// Skip over an expression at "*pp". Return FAIL for an error, OK otherwise.
int
skip_expr(Byte **pp, EvalCtx *evalarg) {
    Var   returnVar;

    *pp = skipwhite(*pp);
    return eval1(OUT pp, &returnVar, evalarg);
}

//Skip over an expression at "*arg".
//"evalarg->eval_tofree" will be set accordingly.
//"arg" is advanced to just after the expression.
//"start" is set to the start of the expression, "end" to just after the end.
//Also when the expression is copied to allocated memory. Return FAIL for an error, OK otherwise.
int
skip_expr_concatenate(
   Byte       **arg,
   Byte       **start,
   Byte       **end,
   EvalCtx   *evalarg)
{
   Var   returnVar;
   int      res;
   int      save_flags = evalarg == NULL ? 0 : evalarg->eval_flags;

   *start = *arg;

   // Don't evaluate the expression.
   if (evalarg)
      evalarg->eval_flags &= ~EVAL_EVALUATE;
   *arg = skipwhite(*arg);
   res = eval1(OUT arg, &returnVar, evalarg);
   *end = *arg;
   if (evalarg)
      evalarg->eval_flags = save_flags;

   return res;
}

//Convert "tv" to a string. When "join_list" is TRUE, convert a List into a sequence 
//of lines. Return an allocated string (NULL when out of memory).
CS
typval2string(Var *tv, int join_list) {
   ArrayList   ga;
   Byte   *retval = NULL;

   if (join_list && (tv->tag == VAR_LIST)) {
      ga_init2(&ga, sizeof(char), 80);
      if (tv->list) {
         list_join(&ga, tv->list, (CS)"\n", TRUE, FALSE, 0);
         if (tv->list->len > 0)
             ga_append(&ga, NL);
      }
      ga_append(&ga, ZERO);
      retval = (CS)ga.c;
   } ei (tv->tag == VAR_LIST || tv->tag == VAR_BAG) {
      Byte   *tofree;
      Byte   numbuf[NUMBUFLEN];

      retval = tv2string(tv, &tofree, numbuf, 0);
      // Make a copy if we have a value but it's not in allocated memory.
      if (retval && tofree == NULL)
         retval = copyStr(retval);
   } else
      retval = copyStr(tv_get_string(tv));
   return retval;
}

//Top-level evaluation function, returning a string. Do not handle line breaks.
//When "join_list" is TRUE, convert a List into a sequence of lines.
//Return pointer to allocated memory, or NULL for failure.
CS
evalToStringWithInvo(CS arg, Boole join_list, Invocation* invo, Boole use_simple_function){
   Var   tv;
   Byte   *retval;
   EvalCtx   evalarg;
   int      r;

   fillEvalArgFromInvo(OUT &evalarg, invo, invo && invo->skip);
   if (use_simple_function)
      r = eval0_simple_funccal(arg, &tv, NULL, &evalarg);
   else
      r = eval0(arg, &tv, NULL, &evalarg);
   if (r == FAIL)
      retval = NULL;
   else {
      retval = typval2string(&tv, join_list);
      clearVar(&tv);
   }
   clear_evalarg(&evalarg, NULL);

   return retval;
}

CS
eval_to_string(CS arg, Boole join_list, Boole use_simple_function) {
   return evalToStringWithInvo(arg, join_list, NULL, use_simple_function);
}

//Call eval_to_string() without using current local variables and using textlock.
CS
eval_to_string_safe(CS arg, Boole use_simple_function) {
   Byte   *retval;
   FnCallEntry funccal_entry;
   int      save_garbage = may_garbage_collect;

   save_funccal(&funccal_entry);
   ++textlock;
   may_garbage_collect = FALSE;
   retval = eval_to_string(arg, FALSE, use_simple_function);
   --textlock;
   may_garbage_collect = save_garbage;
   restore_funccal();
   return retval;
}

// Top-level evaluation function, returning a number.
// Evaluate "expr" silently. Return -1 for an error.
Long
eval_to_number(CS expr, int use_simple_function) {
   Var   returnVar;
   Long   retval;
   Byte   *p = skipwhite(expr);
   int      r = NOTDONE;

   ++emsg_off;

   if (use_simple_function)
      r = may_call_simple_func(expr, &returnVar);
   if (r == NOTDONE)
      r = eval1(OUT &p, &returnVar, &EVALARG_EVALUATE);
   if (r == FAIL)
      retval = -1;
   else {
      retval = varGetNumberChk(&returnVar, NULL);
      clearVar(&returnVar);
   }
   --emsg_off;

   return retval;
}

private Var*
evalExprInternal(CS arg, Invocation *invo, int use_simple_function) {
   EvalCtx evalarg;
   fillEvalArgFromInvo(OUT &evalarg, invo, invo && invo->skip);

   Var* tv = ALLOC_ONE(Var);
   if (tv) {
      int r = NOTDONE;

      if (use_simple_function)
         r = eval0_simple_funccal(arg, tv, invo, &evalarg);
      if (r == NOTDONE)
         r = eval0(arg, tv, invo, &evalarg);

      if (r == FAIL)
         EE_CLEAR(tv);
    }

    clear_evalarg(&evalarg, invo);
    return tv;
}

//Top-level evaluation function. Return an allocated Var with the result. Return NULL when there 
//is an error.
Var*
eval_expr(CS arg, Invocation *invo) {
   return evalExprInternal(arg, invo, FALSE);
}

//Evaluate a string constant and put the result into "returnVar".
//"*arg" points to the double quote or to after it when "interpolate" is TRUE.
//When "interpolate" is TRUE, reduce "{{" to "{", reduce "}}" to "}" and stop
//at a single "{". Return OK or FAIL.
private int
evalStringLiteral(Byte **arg, OUT Var *returnVar, Boole evaluate, Boole interpolate) {
   Byte* p;
   int extra = interpolate ? 1 : 0;
   int off = interpolate ? 0 : 1;
   int len;

   // Find the end of the string, skipping backslashed characters.
   for (p = *arg + off; *p != ZERO && *p != '"'; MB_PTR_ADV(p)) {
      if (*p == '\\' && p[1] != ZERO) {
         ++p;
         //A "\<x>" form occupies at least 4 characters, and produces up
         //to 9 characters (6 for the char and 3 for a modifier): reserve space for 5 extra.
         if (*p == '<') {
            int modifiers = 0;
            int flags = FSK_KEYCODE | FSK_IN_STRING;

            extra += 5;

            // Skip to the '>' to avoid using '{' inside for string interpolation.
            if (p[1] != '*')
               flags |= FSK_SIMPLIFY;
            if (find_special_key(&p, &modifiers, flags, NULL) != 0)
               --p;  // leave "p" on the ">"
         }
      } ei (interpolate && (*p == '{' || *p == '}')) {
         if (*p == '{' && p[1] != '{') // start of expression
            break;
         ++p;
         if (p[-1] == '}' && *p != '}') { // single '}' is an error
            showErrFmtMsg(_(e_stray_closing_curly_str), *arg);
            return FAIL;
         }
         --extra;  // "{{" becomes "{", "}}" becomes "}"
      }
   }

   if (*p != '"' && !(interpolate && *p == '{')) {
      showErrFmtMsg(_(e_missing_double_quote_str), *arg);
      return FAIL;
   }

   // If only parsing, set *arg and return here
   if (!evaluate) {
      *arg = p + off;
      return OK;
   }

   // Copy the string into allocated memory, handling backslashed characters.
   returnVar->tag = VAR_STRING;
   len = (int)(p - *arg + extra);
   returnVar->string = alloc(len);
   CS end = returnVar->string;

   for (p = *arg + off; *p != ZERO && *p != '"'; ) {
      if (*p == '\\') { // handle backslashed stuff like `\n`
         switch (*++p) {
         case 'b': *end++ = BS; ++p; break;
         case 'e': *end++ = ESC; ++p; break;
         case 'f': *end++ = FF; ++p; break;
         case 'n': *end++ = NL; ++p; break;
         case 'r': *end++ = ENTER; ++p; break;
         case 't': *end++ = TAB; ++p; break;

         case 'X': // hex: "\x1", "\x12"
         case 'x':
         case 'u': // Unicode: "\u0023"
         case 'U':
            if (eeIsXDigit(p[1])) {
               int   n, nr;
               int   c = SAFE_toupper(*p);

               if (c == 'X')
                  n = 2;
               ei (*p == 'u')
                  n = 4;
               else
                  n = 8;
               nr = 0;
               while (--n >= 0 && eeIsXDigit(p[1])) {
                  ++p;
                  nr = (nr << 4) + hex2nr(*p);
               }
               ++p;
               // For "\u" store the number according to 'encoding'.
               if (c != 'X')
                  end += mb_char2bytes(nr, end);
               else
                  *end++ = nr;
            }
            break;

         // Special key, e.g.: "\<C-W>"
         case '<': {
            int flags = FSK_KEYCODE | FSK_IN_STRING;

            if (p[1] != '*')
               flags |= FSK_SIMPLIFY;
            extra = trans_special(OUT &p, end, flags, FALSE, NULL);
            if (extra != 0) {
              end += extra;
              if (end >= returnVar->string + len)
                 internalErrMsg((CS)"evalStringLiteral() used more space than allocated");
              break;
            }
            }
            // FALLTHROUGH

         default: MB_COPY_CHAR(p, end);
              break;
         }
      } else {
         if (interpolate && (*p == '{' || *p == '}')) {
            if (*p == '{' && p[1] != '{') // start of expression
                break;
            ++p;  // reduce "{{" to "{" and "}}" to "}"
         }
         MB_COPY_CHAR(p, end);
      }
   }
   *end = ZERO;
   if (*p == '"' && !interpolate)
      ++p;
   *arg = p;

   return OK;
}

//Allocate a variable for a 'str''ing' constant. When "interpolate" is TRUE reduce "{{" to "{" and 
//stop at a single "{". Return OK when a "returnVar" was set to the string.
//Return FAIL on error, "returnVar" is not set.
private int
evalRawString(Byte **arg, Var *returnVar, int evaluate, int interpolate) {
   int reduce = interpolate ? -1 : 0;
   int off = interpolate ? 0 : 1;

   // Find the end of the string, skipping ''.
   CS p;
   for (p = *arg + off; *p != ZERO; MB_PTR_ADV(p)) {
      if (*p == '\'') {
         if (p[1] != '\'')
            break;
         ++reduce;
         ++p;
      } ei (interpolate) {
         if (*p == '{') {
            if (p[1] != '{')
                break;
            ++p;
            ++reduce;
         } ei (*p == '}') {
            ++p;
            if (*p != '}') {
               showErrFmtMsg(_(e_stray_closing_curly_str), *arg);
               return FAIL;
            }
            ++reduce;
         }
      }
   }

   if (*p != '\'' && !(interpolate && *p == '{')) {
      showErrFmtMsg(_(e_missing_single_quote_str), *arg);
      return FAIL;
   }

   // If only parsing return after setting "*arg"
   if (!evaluate) {
      *arg = p + off;
      return OK;
   }

   // Copy the string into allocated memory, handling '' to ' reduction and any expressions.
   CS str = alloc((p - *arg) - reduce);
   returnVar->tag = VAR_STRING;
   returnVar->string = str;

   for (p = *arg + off; *p != ZERO; ) {
      if (*p == '\'') {
          if (p[1] != '\'')
         break;
          ++p;
      } ei (interpolate && (*p == '{' || *p == '}')) {
          if (*p == '{' && p[1] != '{')
         break;
          ++p;
      }
      MB_COPY_CHAR(p, str);
    }
    *str = ZERO;
    *arg = p + off;

    return OK;
}

//Evaluate a single or double quoted string possibly containing expressions.
//"arg" points to the '$'.  The result is put in "returnVar". Return OK or FAIL.
private int
eval_interp_string(Byte **arg, Var *returnVar, int evaluate) {
   Var   tv;
   int      ret = OK;
   int      quote;
   ArrayList   ga;
   Byte   *p;

   ga_init2(&ga, 1, 80);

   // *arg is on the '$' character, move it to the first string character.
   ++*arg;
   quote = **arg;
   ++*arg;

   for (;;) {
      // Get the string up to the matching quote or to a single '{'.
      // "arg" is advanced to either the quote or the '{'.
      if (quote == '"')
         ret = evalStringLiteral(arg, &tv, evaluate, TRUE);
      else
         ret = evalRawString(arg, &tv, evaluate, TRUE);
      if (ret == FAIL)
         break;
      if (evaluate) {
         ga_concat(&ga, tv.string);
         clearVar(&tv);
      }

      if (**arg != '{') {
         // found terminating quote
         ++*arg;
         break;
      }
      p = eval_one_expr_in_str(*arg, &ga, evaluate);
      if (!p) {
         ret = FAIL;
         break;
      }
      *arg = p;
    }

   returnVar->tag = VAR_STRING;
   if (ret == FAIL || !evaluate || ga_append(&ga, ZERO) == FAIL) {
      ga_clear(&ga);
      returnVar->string = NULL;
      return ret;
   }

   returnVar->string = ga.c;
   return OK;
}

//}}}
//{{{function calls

//"*arg" points to what can be a function name in the form of "import.Name" or "Funcref". 
//Return the name of the function. Set "tofree" to something that was allocated.
//If "verbose" is FALSE no errors are given. Return NULL for any failure.
private CS
deref_function_name(
    Byte   **arg,
    Byte   **tofree,
    EvalCtx   *evalarg,
    int      verbose
){
   Var   ref;
   Byte   *name = *arg;
   int      save_flags = 0;
   int      evaluate = evalarg && (evalarg->eval_flags & EVAL_EVALUATE);

   ref.tag = VAR_UNKNOWN;
   if (evalarg) {
      // need to evaluate this to get an import, like in "a.Func"
      save_flags = evalarg->eval_flags;
      evalarg->eval_flags |= EVAL_EVALUATE;
   }
   if (eval9(arg, &ref, evalarg, FALSE) == FAIL) {
      DictItem   *v;

      // If <SID>VarName was used it would not be found, try another way.
      v = findVar_also_in_script(name, NULL, FALSE);
      if (v == NULL) {
         name = NULL;
         goto theend;
      }
      copy_tv(OUT &ref, &v->c);
   }
   if (*skipwhite(*arg) != ZERO) {
      if (verbose) {
         showErrFmtMsg(_(e_trailing_characters_str), *arg);
      } 
      name = NULL;
   } ei (ref.tag == VAR_FUNC && ref.string != NULL) {
      name = ref.string;
      ref.string = NULL;
      *tofree = name;
   } ei (ref.tag == VAR_PARTIAL && ref.partial) {
      if (ref.partial->argc > 0 || ref.partial->self) {
         if (verbose)
            emsg(_(e_cannot_use_partial_here));
         name = NULL;
      } else {
         name = copyStr(partial_name(ref.partial));
         *tofree = name;
      }
   } ei (evaluate) {
      if (verbose)
          showErrFmtMsg(_(e_not_callable_type_str), name);
      name = NULL;
   }

theend:
   clearVar(&ref);
   if (evalarg)
      evalarg->eval_flags = save_flags;
   return name;
}

//Call some Vim script function and return the result in "*returnVar".
//Uses argv[0] to argv[argc - 1] for the function arguments.  argv[argc]
//should have type VAR_UNKNOWN. Return OK or FAIL.
private int
callEeglFunction(
   Byte      *func,
   int      argc,
   Var   *argv,
   Var   *returnVar
){
   int      ret;
   FnExe   funcexe;
   Byte   *arg;
   Byte   *name;
   Byte   *tofree = NULL;
   int      ignore_errors;

   returnVar->tag = VAR_UNKNOWN;      // clearVar() uses this
   CLEAR_FIELD(funcexe);
   funcexe.fe_firstline = curPor->cursor.lnum;
   funcexe.fe_lastline = curPor->cursor.lnum;
   funcexe.fe_evaluate = TRUE;

   //The name might be "import.Func" or "Funcref". We don't know, we need to ignore errors for an 
   //undefined name. But we do want errors when an autoload script has errors.  Guess that when 
   //there is a dot in the name showing errors is the right choice.
   ignore_errors = firstOccurrence(func, '.') == NULL;
   arg = func;
   if (ignore_errors)
      ++emsg_off;
   name = deref_function_name(&arg, &tofree, &EVALARG_EVALUATE, FALSE);
   if (ignore_errors)
      --emsg_off;
   if (name == NULL)
      name = func;

   ret = call_func(name, -1, returnVar, argc, argv, &funcexe);

   if (ret == FAIL)
      clearVar(returnVar);
   eeglFree(tofree);

   return ret;
}

//Call Vim script function "func" and return the result as a string. Uses "argv[0]" to 
//"argv[argc - 1]" for the function arguments."argv[argc]" should have type VAR_UNKNOWN.
//Return NULL when calling the function fails.
void *
call_func_retstr(
   Byte      *func,
   int      argc,
   Var   *argv)
{
   Var   returnVar;
   Byte   *retval;

   if (callEeglFunction(func, argc, argv, &returnVar) == FAIL)
      return NULL;

   retval = copyStr(tv_get_string(&returnVar));
   clearVar(&returnVar);
   return retval;
}

//Call Vimscript function "func" and return the result as a List. Use "argv" and "argc" as 
//call_func_retstr(). Return NULL when there is something wrong.
//Give an error when the returned value is not a list.
void *
call_func_retlist(
   Byte      *func,
   int      argc,
   Var   *argv)
{
   Var   returnVar;

   if (callEeglFunction(func, argc, argv, OUT &returnVar) == FAIL)
      return NULL;

   if (returnVar.tag != VAR_LIST) {
      showErrFmtMsg(_(e_custom_list_completion_function_does_not_return_list_but_str),
         vartype_name(returnVar.tag));
      clearVar(&returnVar);
      return NULL;
   }

   return returnVar.list;
}

//Evaluate "arg", which is 'foldexpr'. Note: caller must set "curPor" to match "arg".
//Return the foldlevel, and any character preceding it in "*cp". Don't give error messages.
int
eval_foldexpr(Portal *wp, int *cp) {
   ScriptPos saved_sctx = scriptPosG;

   CS arg = skipwhite(wp->bookOpts.foldExpr);
   scriptPosG = wp->bookOpts.scriptLocs[PORT_foldExpr];

   ++emsg_off;
   ++textlock;
   *cp = ZERO;

   //Evaluate the expression. If the expression is "FuncName()", call the function directly.
   Long   retval;
   Var tv;
   if (eval0_simple_funccal(arg, OUT &tv, NULL, &EVALARG_EVALUATE) == FAIL)
      retval = 0;
   else {
      // If the result is a number, just return the number.
      if (tv.tag == VAR_NUMBER)
         retval = tv.number;
      ei (tv.tag != VAR_STRING || tv.string == NULL)
         retval = 0;
      else {
         // If the result is a string, check if there is a non-digit before the number.
         CS s = tv.string;
         if (*s != ZERO && !EE_ISDIGIT(*s) && *s != '-')
            *cp = *s++;
         retval = atol((char *)s);
      }
      clearVar(&tv);
   }
   --emsg_off;
   --textlock;
   clear_evalarg(&EVALARG_EVALUATE, NULL);
   scriptPosG = saved_sctx;

   return (int)retval;
}

//}}}
//{{{lvalues

// Flags for assignment functions.
#define ASSIGN_VAR   0     // ":var" (nothing special)
#define ASSIGN_FINAL   0x01  // ":final"
#define ASSIGN_CONST   0x02  // ":const"
#define ASSIGN_NO_DECL   0x04  // "name = expr" without ":let"/":const"/":final"
#define ASSIGN_DECL   0x08  // may declare variable if it does not exist
#define ASSIGN_UNPACK   0x10  // using [a, b] = list
#define ASSIGN_NO_MEMBER_TYPE 0x20 // use "any" for list and dict member type
#define ASSIGN_FOR_LOOP 0x40 // assigning to loop variable
#define ASSIGN_INIT   0x80 // not assigning a value, just a declaration
#define ASSIGN_UPDATE_BLOCK_ID 0x100  // update sav_block_id
#define ASSIGN_COMPOUND_OP 0x200  // compound operator e.g. "+="

#ifdef LOG_LOCKVAR
typedef struct {
   int       flag;
   char    *str;
} FlagString;

private CS
flags_tostring(Unt flags, FlagString* _fstring, CS buf, Unt n) {
   CS p = buf;
   *p = ZERO;
   for (FlagString* fstring = _fstring; fstring->flag; ++fstring) {
      if ((fstring->flag & flags) != 0) {
         Unt len = STRLEN(fstring->str);
         if (n > p - buf + len + 7) {
            STRCAT(p, fstring->str);
            p += len;
            STRCAT(p, " ");
            ++p;
         } else {
            STRCAT(buf, "...");
            break;
         }
      }
   }
   return buf;
}

FlagString glv_flag_strings[] = {
    { GLV_QUIET,      "QUIET" },
    { GLV_NO_AUTOLOAD,      "NO_AUTOLOAD" },
    { GLV_READ_ONLY,      "READ_ONLY" },
    { GLV_NO_DECL,      "NO_DECL" },
    { GLV_ASSIGN_WITH_OP,   "ASSIGN_WITH_OP" },
    { GLV_PREFER_FUNC,      "PREFER_FUNC" },
    { 0,         NULL }
};
#endif

//Fill in "lp" using "root". This is used in a special case when "getLval()" parses a bare word 
//when "lvalRootS" is not NULL.
//
//This is typically called with "lvalRootS" as "root". For a class, find the name from lp in the 
//class from root, fill in Lval if found. For a complex type, list/dict use it as the 
//result; just put the root into var.

//"lvalRootS" is a hack used during run-time/instr-execution to provide the starting point for 
//"getLval()" to traverse a chain of indexes. In some cases getLval sees a bare name and uses 
//this function to populate the Lval.
//
//For setting up "lvalRootS" (currently only used with lockvar)
//   compile_lock_unlock - pushes object on stack (which becomes lvalRootS)
//   execute_instructions: ISN_LOCKUNLOCK - sets lvalRootS from stack.
private void
fillLvalFromRoot(Lval *lp, LvalRoot* root) {
#ifdef LOG_LOCKVAR
   lo("LKVAR: fillLvalFromRoot(): name %s, tv %p", lp->name, (void*)lr->var);
#endif
   if (!root->var)
      return;

#ifdef LOG_LOCKVAR
   lo("LKVAR:    ... type: %s", vartype_name(lr->var->tag));
#endif
   lp->var = root->var;
   lp->isRoot = TRUE;
}

typedef enum {
   GLV_FAIL,
   GLV_OK,
   GLV_STOP
} GlvStatus;

//Get a Bag lval variable that can be assigned a value to: "name", "name[expr]", 
//"name[expr][expr]", "name.key", "name.key[expr]" etc.
//"name" points to the start of the name. If "returnVar" is not NULL, it points to the value to be
//assigned. "unlet" is TRUE for ":unlet": slightly different behavior when something is
//wrong; must end in space or cmd separator.
//
// flags:
//  GLV_QUIET:       do not give error messages
//  GLV_READ_ONLY:   will not change the variable
//  GLV_NO_AUTOLOAD: do not use script autoloading
//
//The Bag is returned in 'lp'.  Return GLV_OK on success and GLV_FAIL on failure. 
//Return GLV_STOP to stop processing the characters following 'key_end'.
private int
get_lval_dict_item(
   OUT Lval   *lp,
   Text key,
   GetLval arg,
   Byte   **key_end,
   Var   *var1
) {
   int      quiet = arg.flags & GLV_QUIET;
   Byte   *p = *key_end;

   CS keyStr = key.c;
   int len = key.len;
   if (len == 0) {
      // "[key]": get key from "var1"
      keyStr = convertVarToStringSingleUse(var1);   // is number or string
      if (!keyStr)
          return GLV_FAIL;
   }
   lp->ll_list = NULL;
   lp->ll_list = NULL;
   lp->ll_blob = NULL;

   // a NULL dict is equivalent with an empty dict
   if (lp->var->bag == NULL) {
      lp->var->bag = allocBag();
      ++lp->var->bag->refcount;
   }
   lp->bag = lp->var->bag;

   lp->ll_di = bagFind(lp->bag, (Text){.c = keyStr, .len = len});

   //When assigning to a scope dictionary check that a function and variable name is valid 
   //(only variable name unless it is l: or g: dictionary). Disallow overwriting a builtin function
   if (arg.returnVar && lp->bag->scope != 0) {
      int prec;

      if (len != -1) {
         prec = key.c[key.len];
         key.c[key.len] = ZERO;
      } else
         prec = 0; // avoid compiler warning
      int wrong = (lp->bag->scope == VAR_DEF_SCOPE
            && (arg.returnVar->tag == VAR_FUNC || arg.returnVar->tag == VAR_PARTIAL)
            && var_wrong_func_name(key, lp->ll_di == NULL))
          || !valid_varname(key, true);
      if (len != -1)
         key.c[key.len] = prec;
      if (wrong)
         return GLV_FAIL;
   }

   if (lp->ll_di == NULL) {
      // Can't add "v:" or "a:" variable.
      if (lp->bag == getEeglVarDict() || &lp->bag->hashTable == get_funccal_args_ht()) {
         showErrFmtMsg(_(e_illegal_variable_name_str), arg.name);
         return GLV_FAIL;
      }

      // Key does not exist in dict: may need to add it.
      if (*p == '[' || *p == '.' || arg.unlet) {
         if (!quiet)
            showErrFmtMsg(_(e_key_not_present_in_dictionary_str), key);
         return GLV_FAIL;
      }
      lp->newKey = copyText(key);

      *key_end = p;
      return GLV_STOP;
   }
   // existing variable, need to check if it can be changed
   ei ((arg.flags & GLV_READ_ONLY) == 0
          && (var_check_ro(lp->ll_di->flags, arg.name, false)
            || var_check_lock(lp->ll_di->flags, arg.name, false))
   )
      return GLV_FAIL;

   lp->var = &lp->ll_di->c;

   return GLV_OK;
}

// Get an blob lval variable that can be assigned a value to: "name",
// "na{me}", "name[expr]", "name[expr:expr]", "name[expr][expr]", etc.
//
// 'var1' specifies the starting blob index and 'var2' specifies the ending
// blob index.  If the first index is not specified in a range, then 'empty1'
// is TRUE.  If 'quiet' is TRUE, then error messages are not displayed for
// invalid indexes.
//
// The blob is returned in 'lp'.  Return OK on success and FAIL on failure.
private int
get_lval_blob(
   Lval   *lp,
   Var   *var1,
   Var   *var2,
   int      empty1,
   int      quiet)
{
   long   bloblen = blob_len(lp->var->blob);

   lp->ll_list = NULL;
   lp->bag = NULL;

   // Get the number and item for the only or first index of a List.
   if (empty1)
      lp->ll_n1 = 0;
   else
      // is number or string
      lp->ll_n1 = (long)tv_get_number(var1);

   if (check_blob_index(bloblen, lp->ll_n1, quiet) == FAIL)
      return FAIL;
   if (lp->ll_range && !lp->ll_empty2) {
      lp->ll_n2 = (long)tv_get_number(var2);
      if (check_blob_range(bloblen, lp->ll_n1, lp->ll_n2, quiet) == FAIL)
         return FAIL;
   }


   lp->ll_blob = lp->var->blob;
   lp->var = NULL;

   return OK;
}

// Get a List lval variable that can be assigned a value to: "name",
// "na{me}", "name[expr]", "name[expr:expr]", "name[expr][expr]", etc.
//
// 'var1' specifies the starting List index and 'var2' specifies the ending
// List index.  If the first index is not specified in a range, then 'empty1'
// is TRUE.  If 'quiet' is TRUE, then error messages are not displayed for
// invalid indexes.
//
// The List is returned in 'lp'.  Return OK on success and FAIL on failure.
private int
get_lval_list(
   Lval   *lp,
   Var   *var1,
   Var   *var2,
   int      empty1,
   int      quiet)
{
   // Get the number and item for the only or first index of the List.
   if (empty1)
     lp->ll_n1 = 0;
   else
     // is number or string
     lp->ll_n1 = (long)tv_get_number(var1);

   lp->bag = NULL;
   lp->ll_list = lp->var->list;
   lp->ll_li = check_range_index_one(
         lp->ll_list, &lp->ll_n1, quiet
   );
   if (lp->ll_li == NULL)
      return FAIL;

   //May need to find the item or absolute index for the second index of a range.
   //When no index given: "lp->ll_empty2" is TRUE.
   //Otherwise "lp->ll_n2" is set to the second index.
   if (lp->ll_range && !lp->ll_empty2) {
      lp->ll_n2 = (long)tv_get_number(var2);
      // is number or string
      if (check_range_index_two(lp->ll_list, &lp->ll_n1, lp->ll_li, &lp->ll_n2, quiet) == FAIL)
         return FAIL;
   }

   lp->var = &lp->ll_li->c;

   return OK;
}

//Check whether dot (".") is allowed after the variable "name" with type "tag". Only Bag, Class 
//and Object types support a dot after the name. Return TRUE if dot is allowed after the name.
private int
dot_allowed_after_type(Text name, VarTag tag, int quiet) {
   if (tag != VAR_BAG) {
      if (!quiet)
         showErrFmtMsg(_(e_dot_not_allowed_after_str_str), vartype_name(tag), name);
      return FALSE;
   }

   return TRUE;
}

//Check whether the variable "name" with type "tag" can be followed by an index. Only Bag, List, 
//Blob, Object and Class types support indexing.  Return TRUE if indexing is allowed after 
//the name.
private Boole
index_allowed_after_type(Text name, VarTag tag, int quiet) {
   if (tag != VAR_LIST
       && tag != VAR_BAG
       && tag != VAR_BLOB
   ) {
      if (!quiet)
         showErrFmtMsg(_(e_index_not_allowed_after_str_str), vartype_name(tag), name.c);
      return FALSE;
   }

   return TRUE;
}

// Get the lval of a list/dict/blob subitem starting at "p".
// Loop until no more [idx] or .key is following.
//
// If "returnVar" is not NULL it points to the value to be assigned. "unlet" is TRUE for ":unlet".
//
// Return a pointer to the character after the subscript on success or NULL on failure.
private CS
getLvalSubscript(Lval* lv, CS p, GetLval arg) {
   int      quiet = arg.flags & GLV_QUIET;
   Text key = {};
   int      len;
   Var   var1;
   Var   var2;
   int      empty1 = FALSE;
   int      rc = FAIL;

   // Loop until no more [idx] or .key is following.
   var1.tag = VAR_UNKNOWN;
   var2.tag = VAR_UNKNOWN;

   while (*p == '[' || (*p == '.' && p[1] != '=' && p[1] != '.')) {
      VarTag tag = lv->var->tag;

      if (*p == '.' && !dot_allowed_after_type(arg.name, tag, quiet))
         goto done;

      if (!index_allowed_after_type(arg.name, tag, quiet))
         goto done;

      // A NULL list/blob works like an empty list/blob, allocate one now.
      int r = OK;
      if (tag == VAR_LIST && lv->var->list == NULL)
         allocReturnList(lv->var);
      ei (tag == VAR_BLOB && lv->var->blob == NULL)
         r = returnVar_blob_alloc(lv->var);
      if (r == FAIL)
         goto done;

      if (lv->ll_range) {
         if (!quiet)
            emsg(_(e_slice_must_come_last));
         goto done;
      }
#ifdef LOG_LOCKVAR
      lo("LKVAR: getLval() loop: p: %s, type: %s", p, vartype_name(tag));
#endif

      len = -1;
      if (*p == '.') {
         key.c = p + 1;

         for (len = 0; ASCII_ISALNUM(key.c[len]) || key.c[len] == '_'; ++len)
            ;
         if (len == 0) {
            if (!quiet)
               emsg(_(e_cannot_use_empty_key_for_dictionary));
            goto done;
         }
         key.len = len;
         p = key.c + len;
      } else {
         // Get the index [expr] or the first index [expr: ].
         p = skipwhite(p + 1);
         if (*p == ':')
            empty1 = TRUE;
         else {
            empty1 = FALSE;
            if (eval1(OUT &p, &var1, &EVALARG_EVALUATE) == FAIL)  // recursive!
               goto done;
            if (convertVarToStringSingleUse(&var1) == NULL)
               // not a number or string
               goto done;
            p = skipwhite(p);
         }

         // Optionally get the second index [ :expr].
         if (*p == ':') {
            if (tag == VAR_BAG) {
               if (!quiet)
                  emsg(_(e_cannot_slice_dictionary));
               goto done;
            }
            if (arg.returnVar
               && !(arg.returnVar->tag == VAR_LIST && arg.returnVar->list)
               && !(arg.returnVar->tag == VAR_BLOB && arg.returnVar->blob))
            {
               if (!quiet)
                  emsg(_(e_slice_requires_list_or_blob_value));
               goto done;
            }
            p = skipwhite(p + 1);
            if (*p == ']')
               lv->ll_empty2 = TRUE;
            else {
               lv->ll_empty2 = FALSE;
               // recursive!
               if (eval1(OUT &p, &var2, &EVALARG_EVALUATE) == FAIL)
                  goto done;
               if (convertVarToStringSingleUse(&var2) == NULL)
                  // not a number or string
                  goto done;
            }
            lv->ll_range = TRUE;
         } else
            lv->ll_range = FALSE;

         if (*p != ']') {
            if (!quiet)
               emsg(_(e_missing_closing_square_brace));
            goto done;
         }

         // Skip to past ']'.
         ++p;
      }
#ifdef LOG_LOCKVAR
      if (len == -1)
          lo("LKVAR:    ... loop: p: %s, '[' key: %s", p,
             empty1 ? ":" : tv_get_string(&var1));
      else
          lo("LKVAR:    ... loop: p: %s, '.' key: %s", p, key.c);
#endif

      if (tag == VAR_BAG) {
         GlvStatus status = get_lval_dict_item(OUT lv, key, arg, &p, &var1);
         if (status == GLV_FAIL)
            goto done;
         ei (status == GLV_STOP)
            break;
      } ei (tag == VAR_BLOB) {
         if (get_lval_blob(lv, &var1, &var2, empty1, quiet) == FAIL)
            goto done;
         break;
      } ei (tag == VAR_LIST) {
         if (get_lval_list(lv, &var1, &var2, empty1, quiet) == FAIL)
            goto done;
      }

      clearVar(&var1);
      clearVar(&var2);
      var1.tag = VAR_UNKNOWN;
      var2.tag = VAR_UNKNOWN;
   }

    rc = OK;

done:
    clearVar(&var1);
    clearVar(&var2);
    return rc == OK ? p : NULL;
}

//Get an lval: variable, Bag item or List item that can be assigned a value to: "name", "na{me}",
//"name[expr]", "name[expr:expr]", "name[expr][expr]", "name.key", "name.key[expr]" etc.
//Indexing only works if "name" is an existing List or Dictionary. "name" points to the start of 
//the name. If "returnVar" is not NULL it points to the value to be assigned. "unlet" is TRUE for 
//":unlet": slightly different behavior when something is wrong; must end in space or cmd separator
//
//flags:
//  GLV_QUIET:       do not give error messages
//  GLV_READ_ONLY:   will not change the variable
//  GLV_NO_AUTOLOAD: do not use script autoloading
//
//Return a pointer to just after the name, including indexes. When an evaluation error occurs 
//"retVal->name" is NULL; Return NULL for a parsing error. Still need to free items in "letVal"!
CS
getLval(
   OUT Lval* retVal,
   GetLval arg
) { // flags for findNameEnd()
   DictItem* v = NULL;
   EeSet   *ht = NULL;
   Boole quiet = arg.flags & GLV_QUIET;

#ifdef LOG_LOCKVAR
   if (!lvalRootS)
      lo("LKVAR: getLval(): name: %s, lvalRootS (nil)", name);
   else
      lo("LKVAR: getLval(): name: %s, var %p isArg %d",
         name, (void*)lvalRootS->var, lvalRootS->isArg);
   Byte buf[80];
   lo("LKVAR:    ...: GLV flags: %s",
          flags_tostring(arg.flags, glv_flag_strings, buf, sizeof(buf))
   );
#endif

   CLEAR_POINTER(retVal);

   if (arg.skip) {
      // When skipping or compiling just find the end of the name.
      retVal->name = findNameEnd(arg.name, NULL, FNE_INCL_BR | arg.fneFlag);
      return retVal->name.c + retVal->name.len;
   }

   // Find the end of the name.
   Text expr;
   retVal->name = findNameEnd(arg.name, OUT &expr, arg.fneFlag);
   
   CS tail = retVal->name.c + retVal->name.len;
   if (expr.len > 0) {
      // Don't expand the name when we already know there is an error.
      if (arg.unlet && !SPACE_OR_TAB(*tail) && !endsComm(tail) 
            && *tail != '[' && *tail != '.'
      ) {
         showErrFmtMsg(_(e_trailing_characters_str), tail);
         return NULL;
      }

      retVal->expandedName = expandCurlyBraces(expr, arg.name);
      if (retVal->expandedName.len == 0) {
         // Report an invalid expression in braces, unless the expression evaluation has been 
         // cancelled due to an aborting error, an interrupt, or an exception.
         if (!aborting() && !quiet) {
            emsg_severe = TRUE;
            showErrFmtMsg(_(e_invalid_argument_str), arg.name);
            return NULL;
         }
      }
      retVal->name = retVal->expandedName;
   } else {
      retVal->name = copyText(retVal->name);
   }
   if (retVal->name.len == 0)
      return tail;

   // Without [idx] or .key we are done.
   if (*tail != '[' && *tail != '.') {
      if (lvalRootS)
         fillLvalFromRoot(retVal, lvalRootS);
      return tail;
   }

   if (!retVal->var) {
      // When we would write to the variable, pass &ht and prevent autoload.
      Boole writing = (arg.flags & GLV_READ_ONLY) == 0;
      v = findVarAndSetHtable(
         retVal->name, writing ? &ht : NULL, (arg.flags & GLV_NO_AUTOLOAD) != 0 || writing
      );
      if (!v && !quiet) {
         showErrFmtMsg(_(e_undefined_variable_str), retVal->name);
      } 
      if (!v)
         return NULL;
      retVal->var = &v->c;
   }

   //If the next character is a "." or a "[", then process the subitem.
   tail = getLvalSubscript(retVal, tail, arg);
   if (!tail)
      return null;

   retVal->name.len = tail - retVal->name.c;
   return tail;
}

private void
clear_type_list(ArrayList *gap) {
   while (gap->len > 0)
      eeglFree(((TypeSpec **)gap->c)[--gap->len]);
   ga_clear(gap);
}

//Clear lval "lp"'s memory (which was filled by getLval()).
void
clear_lval(OUT Lval* lp) {
   eeglFree(lp->expandedName.c);
   eeglFree(lp->newKey.c);
}

//Let a variable have value
//"op" is NULL, "+" for "+=", "-" for "-=", "*" for "*=", "/" for "/=",
//"%" for "%=", "." for ".=" or "=" for "=".
//Return true on success.
Boole
letImpl(
   OUT Lval* lval,
   Var* returnVar,
   Boole copy,
   Unt flags, // ASSIGN_CONST, ASSIGN_NO_DECL
   NULLABLE CS op
){
   DictItem   *di;

   if (!lval->var) {

      if (lval->ll_blob) {
         if (op && *op != '=') {
            showErrFmtMsg(_(e_wrong_variable_type_for_str_equal), op);
            return false;
         }
         if (value_check_lock(lval->ll_blob->lock, lval->name, false))
            return false;

         if (lval->ll_range && returnVar->tag == VAR_BLOB) {
            if (lval->ll_empty2)
               lval->ll_n2 = blob_len(lval->ll_blob) - 1;

            if (blob_set_range(lval->ll_blob, lval->ll_n1, lval->ll_n2, returnVar) == FAIL)
               return false;
         } else {
            Boole error = false;
            int val = (int)varGetNumberChk(returnVar, OUT &error);
            if (!error)
               blob_set_append(lval->ll_blob, lval->ll_n1, val);
         }
      } ei (op && *op != '=') {
         Var tv;

         if ((flags & (ASSIGN_CONST | ASSIGN_FINAL)) != 0 && (flags & ASSIGN_FOR_LOOP) == 0) {
            emsg(_(e_cannot_modify_existing_variable));
            return false;
         }

         // handle +=, -=, *=, /=, %= and .=
         di = NULL;
         if (eval_variable(lval->name, &tv, &di, EVAL_VAR_VERBOSE) == OK){
            if ((!di
                   || (!var_check_ro(di->flags, lval->name, false)
                     && !tv_check_lock(&di->c, lval->name, false)))
                     && tv_op(&tv, returnVar, op) == OK
            ) {
               setVarImpl(lval->name, 0, &tv, false, ASSIGN_NO_DECL | ASSIGN_COMPOUND_OP);
            } 
            clearVar(&tv);
         }
      } else {
         setVarImpl(lval->name, 0, returnVar, copy, flags);
      }
   } ei (value_check_lock(
            lval->newKey.len > 0 ? lval->var->bag->lock : lval->var->lock, lval->name, false
         )
   ) {
   } ei (lval->ll_range) {
      if ((flags & (ASSIGN_CONST | ASSIGN_FINAL)) && (flags & ASSIGN_FOR_LOOP) == 0) {
         emsg(_(e_cannot_lock_range));
         return false;
      }

      (void)list_assign_range(
         lval->ll_list, returnVar->list, lval->ll_n1, lval->ll_n2, lval->ll_empty2, op, lval->name
      );
   } else {
      // Assign to a List, Dictionary or Object item.
      if ((flags & (ASSIGN_CONST | ASSIGN_FINAL)) && (flags & ASSIGN_FOR_LOOP) == 0) {
         emsg(_(e_cannot_lock_list_or_dict));
         return false;
      }

      if (lval->newKey.len > 0) {
         if (op && *op != '=') {
            showErrFmtMsg(_(e_key_not_present_in_dictionary_str), lval->newKey);
            return false;
         }
         if (dict_wrong_func_name(lval->var->bag, returnVar, lval->newKey))
            return false;

         // Need to add an item to the Dictionary.
         di = dictitem_alloc(lval->newKey);
         if (bagAdd(lval->var->bag, di) == FAIL) {
            eeglFree(di);
            return false;
         }
         lval->var = &di->c;
      } ei (op && *op != '=') {
         tv_op(lval->var, returnVar, op);
         return false;
      } else
         clearVar(lval->var);

      // Assign the value to the variable or list item.
      if (copy)
         copy_tv(OUT lval->var, returnVar);
      else {
         *lval->var = *returnVar;
         lval->var->lock = 0;
         initVarToNull(OUT returnVar);
      }
   }
   return true;
}

// True for success
Boole
evalLetVarSimple(CS name, Var* newValue) {
   Lval   lv;
   CS afterName = getLval(OUT &lv, (GetLval){
         .name = mbText(name), .returnVar = null, .unlet = false, .skip = false,
         .flags = 0, .fneFlag = FNE_CHECK_START
      }
   );
   return (afterName && lv.name.len > 0) ? letImpl(OUT &lv, newValue, true, 0, null) : false;
}

// Handle "blob1 += blob2". Return OK or FAIL.
private int
tv_op_blob(Var *tv1, Var *tv2, CS op) {
   if (*op != '+' || tv2->tag != VAR_BLOB)
      return FAIL;

   // Blob += Blob
   if (tv2->blob == NULL)
      return OK;

   if (tv1->blob == NULL) {
      tv1->blob = tv2->blob;
      ++tv1->blob->refcount;
      return OK;
   }

   Blob* b1 = tv1->blob;
   Blob* b2 = tv2->blob;
   int len = blob_len(b2);

   for (int i = 0; i < len; i++)
      ga_append(&b1->c, blob_get(b2, i));

   return OK;
}

// Handle "list1 += list2". Return OK or FAIL.
private int
tv_op_list(Var *tv1, Var *tv2, CS op) {
   if (*op != '+' || tv2->tag != VAR_LIST)
      return FAIL;

   // List += List
   if (tv2->list == NULL)
      return OK;

   if (tv1->list == NULL) {
      tv1->list = tv2->list;
      ++tv1->list->refcount;
   } else
      list_extend(tv1->list, tv2->list, NULL);

   return OK;
}

// Handle number operations: nr += nr , nr -= nr , nr *=nr , nr /= nr , nr %= nr
// Return OK or FAIL.
private int
tv_op_number(Var *tv1, Var *tv2, CS op) {
   Long   n;
   Boole failed = false;

   n = tv_get_number(tv1);
   if (tv2->tag == VAR_FLOAT) {
      double f = n;

      if (*op == '%')
         return FAIL;
      switch (*op) {
         case '+': f += tv2->floatt; break;
         case '-': f -= tv2->floatt; break;
         case '*': f *= tv2->floatt; break;
         case '/': f /= tv2->floatt; break;
      }
      clearVar(tv1);
      tv1->tag = VAR_FLOAT;
      tv1->floatt = f;
   } else {
      switch (*op) {
      case '+': n += tv_get_number(tv2); break;
      case '-': n -= tv_get_number(tv2); break;
      case '*': n *= tv_get_number(tv2); break;
      case '/': n = num_divide(n, tv_get_number(tv2), &failed); break;
      case '%': n = num_modulus(n, tv_get_number(tv2), &failed); break;
      }
      clearVar(tv1);
      tv1->tag = VAR_NUMBER;
      tv1->number = n;
   }

   return failed ? FAIL : OK;
}

// Handle "str1 .= str2" Return OK or FAIL.
private int
tv_op_string(Var *tv1, Var *tv2, CS op UNUSED) {
   Byte numbuf[NUMBUFLEN];

   if (tv2->tag == VAR_FLOAT)
      return FAIL;

   // str .= str
   CS s = tv_get_string(tv1);
   s = concat_str(s, tv_get_string_buf(tv2, numbuf));
   clearVar(tv1);
   tv1->tag = VAR_STRING;
   tv1->string = s;

   return OK;
}

//Handle "tv1 += tv2", "tv1 -= tv2", "tv1 *= tv2", "tv1 /= tv2", "tv1 %= tv2" and "tv1 .= tv2".
//Return OK or FAIL
private int
tv_op_nr_or_string(Var *tv1, Var *tv2, CS op) {
   if (tv2->tag == VAR_LIST)
      return FAIL;

   if (firstOccurrence((CS)"+-*/%", *op) != NULL)
      return tv_op_number(tv1, tv2, op);

   return tv_op_string(tv1, tv2, op);
}

// Handle "f1 += f2", "f1 -= f2", "f1 *= f2", "f1 /= f2". Return OK or FAIL.
private int
tv_op_float(Var *tv1, Var *tv2, CS op) {
   double f;

   if (*op == '%' || *op == '.'
          || (tv2->tag != VAR_FLOAT
         && tv2->tag != VAR_NUMBER
         && tv2->tag != VAR_STRING))
      return FAIL;

   if (tv2->tag == VAR_FLOAT)
      f = tv2->floatt;
   else
      f = tv_get_number(tv2);
   switch (*op) {
   case '+': tv1->floatt += f; break;
   case '-': tv1->floatt -= f; break;
   case '*': tv1->floatt *= f; break;
   case '/': tv1->floatt /= f; break;
   }

   return OK;
}

//Handle "tv1 += tv2", "tv1 -= tv2", "tv1 *= tv2", "tv1 /= tv2", "tv1 %= tv2" and "tv1 .= tv2"
//Return OK or FAIL.
int
tv_op(Var *tv1, Var *tv2, CS op) {
   //Can't do anything with a Funcref or Bag on the right. v:true and friends only work with "..=".
   if (tv2->tag == VAR_FUNC || tv2->tag == VAR_BAG
      || ((tv2->tag == VAR_BOOL || tv2->tag == VAR_SPECIAL) && *op != '.')
   ) {
      showErrFmtMsg(_(e_wrong_variable_type_for_str_equal), op);
      return FAIL;
   }

   int retval = FAIL;
   switch (tv1->tag) {
   case VAR_UNKNOWN:
   case VAR_ANY:
   case VAR_VOID:
   case VAR_BAG:
   case VAR_FUNC:
   case VAR_PARTIAL:
   case VAR_BOOL:
   case VAR_SPECIAL:
   case VAR_JOB:
   case VAR_CHANNEL:
       break;

   case VAR_BLOB:
       retval = tv_op_blob(tv1, tv2, op);
       break;

   case VAR_LIST:
       retval = tv_op_list(tv1, tv2, op);
       break;

   case VAR_NUMBER:
   case VAR_STRING:
       retval = tv_op_nr_or_string(tv1, tv2, op);
       break;

   case VAR_FLOAT:
       retval = tv_op_float(tv1, tv2, op);
       break;
   }

   if (retval != OK)
      showErrFmtMsg(_(e_wrong_variable_type_for_str_equal), op);

   return retval;
}

//}}}
//{{{loops

//Evaluate the expression used in a ":for var in expr" command. "arg" points to "var".
//Set "*errp" to TRUE for an error, FALSE otherwise;
//Return a pointer that holds the info.  Null when there is an error.
void*
eval_for_line(
   CS   arg,
   OUT Boole* errp,
   Invocation   *invo,
   EvalCtx   *evalarg
) {
   Var   tv;
   List   *l;
   Boole skip = !(evalarg->eval_flags & EVAL_EVALUATE);

   *errp = true;   // default: there is an error

   ForInfo* fi = ALLOC_CLEAR_ONE(ForInfo);

   CS var_list_end = skip_var_list(arg, &fi->fi_varcount, &fi->endsWithSemicolon, false);
   if (!var_list_end)
      return fi;

   CS expr = skipwhite_and_linebreak(var_list_end, evalarg);
   if (expr[0] != 'i' || expr[1] != 'n'
              || !(expr[2] == ZERO || SPACE_OR_TAB(expr[2]))
   ){
      emsg(_(e_missing_in_after_for));
      return fi;
   }

   if (skip)
      ++emsg_skip;
   expr = skipwhite_and_linebreak(expr + 2, evalarg);
   if (eval0(expr, &tv, invo, evalarg) == OK) {
      *errp = FALSE;
      if (!skip) {
         if (tv.tag == VAR_LIST) {
            l = tv.list;
            if (l == NULL) {
               // a null list is like an empty list: do nothing
               clearVar(&tv);
            } else {
               // Need a real list here.
               CHECK_LIST_MATERIALIZE(l);

               // No need to increment the refcount, it's already set for the list being used in "tv".
               fi->fi_list = l;
               list_add_watch(l, &fi->fi_lw);
               fi->fi_lw.c = l->first;
            }
         } ei (tv.tag == VAR_BLOB) {
            fi->fi_bi = 0;
            if (tv.blob) {
               Var btv;
               // Make a copy, so that the iteration still works when the blob is changed.
               blob_copy(tv.blob, &btv);
               fi->fi_blob = btv.blob;
            }
            clearVar(&tv);
         }
          ei (tv.tag == VAR_STRING) {
         fi->fi_byte_idx = 0;
         fi->fi_string = tv.string;
         tv.string = NULL;
         if (fi->fi_string == NULL)
             fi->fi_string = copyStr((CS)"");
         } else {
            emsg(_(e_string_list_tuple_or_blob_required));
            clearVar(&tv);
         }
      }
   }
   if (skip)
      --emsg_skip;
   fi->fi_break_count = evalarg->eval_break_count;

   return fi;
}


//Use the first item in a ":for" list.  Advance to the next.
//Assign the values to the variable (list).  "arg" points to the first one.
//Return TRUE when a valid item was found, FALSE when at end of list or something wrong.
int
next_for_item(void *fi_void, CS arg) {
   ForInfo* fi = (ForInfo *)fi_void;
   int      result;
   Unt      flag = ASSIGN_FOR_LOOP;
   ListItem   *item;

   if (fi->fi_blob) {
      Var   tv;

      if (fi->fi_bi >= blob_len(fi->fi_blob))
         return FALSE;
      tv.tag = VAR_NUMBER;
      tv.lock = VAR_FIXED;
      tv.number = blob_get(fi->fi_blob, fi->fi_bi);
      ++fi->fi_bi;
      return letVars(arg, &tv, true, fi->endsWithSemicolon, fi->fi_varcount, flag, NULL) == OK;
   }

   if (fi->fi_string) {
      int len = utfCharLen(fi->fi_string + fi->fi_byte_idx);
      if (len == 0)
          return FALSE;
      Var tv;
      tv.tag = VAR_STRING;
      tv.lock = VAR_FIXED;
      tv.string = copySubstr(fi->fi_string + fi->fi_byte_idx, len);
      fi->fi_byte_idx += len;
      ++fi->fi_bi;
      result = letVars(arg, &tv, true, fi->endsWithSemicolon, fi->fi_varcount, flag, NULL) == OK;
      eeglFree(tv.string);
      return result;
   }

   item = fi->fi_lw.c;
   if (item == NULL)
      result = FALSE;
   else {
      fi->fi_lw.c = item->next;
      ++fi->fi_bi;
      result = (letVars(arg, &item->c, true, fi->endsWithSemicolon,
                  fi->fi_varcount, flag, NULL) == OK);
   }
   return result;
}

//Free the structure used to store info used by ":for".
void
free_for_info(void *fi_void) {
   ForInfo* fi = (ForInfo *)fi_void;

   if (!fi)
      return;
   if (fi->fi_list) {
      list_rem_watch(fi->fi_list, &fi->fi_lw);
      list_unref(fi->fi_list);
   } ei (fi->fi_blob)
      blob_unref(fi->fi_blob);
   else
      eeglFree(fi->fi_string);
   eeglFree(fi);
}

//}}}
//{{{evaluation 2

//eval1 = question marks: expr2 ? expr1 : expr1, expr2 ?? expr1
//eval2 = logical OR: expr2 || expr2 || expr2 
//eval3 = logical AND:  expr3 && expr3 && expr3
//eval4 = comparisons: var1 == var2, var1 > var2 etc
//eval5 = bitwise shifts: var1 << var2, var1 >> var2
//eval6 = addition, subtraction, string concatenation
//eval7 = number multiplication, division, number modulo
//eval8 = a type cast before a base level expression.
//eval9 = the damn rest (constants, registers, options, function calls, variables, lists etc)


// Return TRUE if an operator was started but not finished yet.
// Include typing a count or a register name.
int
op_pending(void) {
   return !(currOperatorG
       && !finish_op
       && currOperatorG->prev_opcount == 0
       && currOperatorG->prev_count0 == 0
       && currOperatorG->op_type == OP_NOP
       && currOperatorG->regname == ZERO);
}

void
set_context_for_expression(Expand   *xp, CS arg, CommIndex   id) {
   Boole has_expr = id != C_let;
   Byte   *p;

   if (id == C_let || id == C_const || id == C_final) {
      xp->context = EXPAND_USER_VARS;
      if (eeStrpbrk(arg, S"\"'+-*/%.=!?~|&$([<>,#") == NULL) { //])
         // ":let var1 var2 ...": find last space.
         for (p = arg + STRLEN(arg); p >= arg; ) {
            xp->input.c = p;
            MB_PTR_BACK(arg, p);
            if (SPACE_OR_TAB(*p))
               break;
         }
         return;
      }
   } else
      xp->context = id == C_call ? EXPAND_FUNCTIONS : EXPAND_EXPRESSION;
   while ((xp->input.c = eeStrpbrk(arg, S"\"'+-*/%.=!?~|&$([<>,#")) != NULL) {
      Unt c = xp->input.c[0];
      if (c == '&') {
         c = xp->input.c[1];
         if (c == '&') {
            ++xp->input.c;
            xp->input.len--;
            xp->context = has_expr ? EXPAND_EXPRESSION : EXPAND_NOTHING;
         } ei (c != ' ') {
            xp->context = EXPAND_OPTION;
            if ((c == 'l' || c == 'g') && xp->input.c[2] == ':') {
               xp->input.c += 2;
               xp->input.len -= 2;
            } 
         }
      } ei (c == '$') {
         // environment variable
         xp->context = EXPAND_ENV_VARS;
      } ei (c == '=') {
          has_expr = true;
          xp->context = EXPAND_EXPRESSION;
      } ei (c == '#' && xp->context == EXPAND_EXPRESSION) {
          // Autoload function/variable contains '#'.
          break;
      } ei ((c == '<' || c == '#')
         && xp->context == EXPAND_FUNCTIONS
         && firstOccurrence(xp->input.c, '(') == NULL
      ) {
          // Function name can start with "<SNR>" and contain '#'.
          break;
      } ei (has_expr) {
         if (c == '"')  {     // string
            xp->input = skipQuoted(xp->input);
            xp->context = EXPAND_NOTHING;
         } ei (c == '\'') {      // literal string
            // Trick: '' is like stopping and starting a literal string.
            xp->input = skipSingleQuoted(xp->input);
            xp->context = EXPAND_NOTHING;
         } ei (c == '|') {
            if (xp->input.c[1] == '|') {
               ++xp->input.c;
               xp->input.len--;
               xp->context = EXPAND_EXPRESSION;
            } else
               xp->context = EXPAND_COMMANDS;
         } else
            xp->context = EXPAND_EXPRESSION;
      } else
         // Doesn't look like something valid, expand as an expression anyway.
         xp->context = EXPAND_EXPRESSION;
      arg = xp->input.c;
      if (xp->input.len > 0)
         while ((c = *++arg) != ZERO && (c == ' ' || c == '\t'))
         /* skip */ ;
   }

   // ":exe one two" completes "two"
   if ((id == C_execute
      || id == C_echo
      || id == C_echon
      || id == C_echomsg
      || id == C_echowindow)
       && xp->context == EXPAND_EXPRESSION
   ) {
      for (;;) {
         CS n = skiptowhite(arg);

         if (n == arg || IS_WHITE_OR_ZERO(*skipwhite(n)))
            break;
         arg = skipwhite(n);
      }
   }
   xp->input.len -= (arg - xp->input.c);
   xp->input.c = arg;
}

// TRUE if "pat" matches "text". Always uses 'magic'.
int
pattern_match(CS pat, CS text, int ic) {
   int      matches = FALSE;
   RegMatch   regmatch;

   regmatch.regprog = compileRegexp(pat, RE_MAGIC + RE_STRING);
   if (regmatch.regprog) {
      regmatch.rm_ic = ic;
      matches = eeRegexec_nl(&regmatch, text, (ColNr)0);
      eeRegFree(regmatch.regprog);
   }
   return matches;
}

//Handle a name followed by "(".  Both for just "name(arg)" and for
//"expr->name(arg)". Return OK or FAIL.
private int
eval_func(
   Byte       **arg,   // points to "(", will be advanced
   EvalCtx   *evalarg,
   Text name,
   Var    *returnVar,
   int       flags,
   Var    *basetv)   // "expr" for "expr->name(arg)"
{
   int      evaluate = flags & EVAL_EVALUATE;
   
   CS s = name.c;
   if (!evaluate)
      check_vars(name);
      
   PartiallyApplied   *partial;
   int      ret = OK;

   // If "s" is the name of a variable of type VAR_FUNC use its contents.
   Boole found_var = false;
   Text t = deref_func_name(name, &partial, NULL, !evaluate, OUT &found_var);

   // Need to make a copy, in case evaluating the arguments makes the name invalid.
   s = copyStr(t.c);
   int len = t.len;
   if (evaluate && *s == ZERO)
      ret = FAIL;
   else {
      FnExe funcexe;

      // Invoke the function.
      CLEAR_FIELD(funcexe);
      funcexe.fe_firstline = curPor->cursor.lnum;
      funcexe.fe_lastline = curPor->cursor.lnum;
      funcexe.fe_evaluate = evaluate;
      funcexe.fe_partial = partial;
      funcexe.fe_basetv = basetv;
      funcexe.fe_found_var = found_var;
      ret = get_func_tv(s, len, returnVar, arg, evalarg, &funcexe);
   }
   eeglFree(s);

   // If evaluate is FALSE returnVar->tag was not set in
   // get_func_tv, but it's needed in handle_subscript() to parse
   // what follows. So set it here.
   if (returnVar->tag == VAR_UNKNOWN && !evaluate && **arg == '(') {
      returnVar->string = NULL;
      returnVar->tag = VAR_FUNC;
   }

   // Stop the expression evaluation when immediately aborting on error, or when an interrupt 
   // occurred or an exception was thrown but not caught.
   if (evaluate && aborting()) {
      if (ret == OK)
         clearVar(returnVar);
      ret = FAIL;
   }
   return ret;
}

// After a NL, skip over empty lines and comment-only lines.
private CS
newline_skip_comments(CS arg) {
  CS p = arg + 1;

  for (;;) {
     p = skipwhite(p);

     if (*p == ZERO)
        break;
     if (*p != NL)
        break;
     ++p;  // skip another NL
   }
   return p;
}


Boole
isComment(CS c) {
   return (*c == '/' && c[1] == '/');
}


// Call skipwhite() and get the next line if needed.
CS
skipwhite_and_linebreak(CS arg, EvalCtx *evalarg) {
   if (!evalarg)
      return skipwhite(arg);
   return skipwhite_and_nl(arg);
}

//The "eval" functions have an "evalarg" argument: When NULL or "evalarg->eval_flags" does not 
//have EVAL_EVALUATE, then the argument is only parsed but not executed. The functions may return 
//OK, but the returnVar will be of type VAR_UNKNOWN. The functions still return FAIL for a syntax 
//error.

//Handle zero level expression. Call eval1() and handle error message and nextcmd.
//Put the result in "returnVar" when returning OK and "evaluate" is TRUE.
//Note: "returnVar.lock" is not set. "evalarg" can be NULL, EVALARG_EVALUATE or a pointer.
//Return OK or FAIL.
int
eval0(
   CS arg,
   Var   *returnVar,
   Invocation   *invo,
   EvalCtx   *evalarg)
{
   return eval0_retarg(arg, returnVar, invo, evalarg, NULL);
}

//If "arg" is a simple function call without arguments then call it and return
//the result.  Otherwise return NOTDONE.
int
may_call_simple_func(CS arg, OUT Var* returnVar) {
   CS parens = STRSTR(arg, S"()");
   int r = NOTDONE;

   // If the expression is "FuncName()" then we can skip a lot of overhead.
   if (parens && *skipwhite(parens + 2) == ZERO) {
      CS p = STRNCMP(arg, "<SNR>", 5) == 0 ? skipdigits(arg + 5) : arg;

      if (toNameEnd(p, TRUE) == parens)
         r = call_simple_func(arg, (Unt)(parens - arg), OUT returnVar);
   }
   return r;
}

//Handle zero level expression with optimization for a simple function call.
//Same arguments and return value as eval0().
int
eval0_simple_funccal(
   CS arg,
   OUT Var* returnVar,
   Invocation* invo,
   EvalCtx* evalarg)
{
   int r = may_call_simple_func(arg, OUT returnVar);
   if (r == NOTDONE)
      r = eval0_retarg(arg, returnVar, invo, evalarg, NULL);
   return r;
}

//Like eval0() but when "retarg" is not NULL store the pointer to after the
//expression and don't check what comes after the expression.
private int
eval0_retarg(
   CS arg,
   Var* returnVar,
   Invocation* invo,
   EvalCtx* evalarg,
   CS* retarg
){
   int anyEmsgG_before = anyEmsgG;
   int called_emsg_before = called_emsg;
   int check_for_end = retarg == NULL;
   int end_error = FALSE;

   CS p = skipwhite(arg);
   int ret = eval1(OUT &p, returnVar, evalarg);

   if (ret != FAIL) {
      p = skipwhite(p);

      if (check_for_end)
         end_error = !endsComm(p);
   }

   if (ret == FAIL || end_error) {
      if (ret != FAIL)
          clearVar(returnVar);
      //Report the invalid expression unless the expression evaluation has been cancelled due to
      //an aborting error, an interrupt, or an exception, or we already gave a more specific error.
      //Also check called_emsg for when using assert_fails().
      if (!aborting()
         && anyEmsgG == anyEmsgG_before
         && called_emsg == called_emsg_before
      ) {
         if (end_error) {
            showErrFmtMsg(_(e_trailing_characters_str), p);
         } else {
            showErrFmtMsg(_(e_invalid_expression_str), arg);
         } 
      }

      if (invo && p) {
         //Some of the expression may not have been consumed. Only execute a next command if it 
         //cannot be a "||" operator. The next command may be "catch".
         CS nextcmd = check_nextcmd(p);
         if (nextcmd && *nextcmd != '|')
            invo->nextComm = nextcmd;
      }
      return FAIL;
   }

   if (retarg)
      *retarg = p;
   ei (check_for_end && invo)
      set_nextcmd(OUT invo, p);

   return ret;
}

//Handle top level expression:
//  expr2 ? expr1 : expr1
//  expr2 ?? expr1
//
//"arg" must point to the first non-white of the expression.
//"arg" is advanced to just after the recognized expression.
//
//Note: "returnVar.v_lock" is not set. Return OK or FAIL.
int
eval1(OUT CS* arg, Var *returnVar, OUT EvalCtx* evalarg) {
   CLEAR_POINTER(returnVar);

   // Get the first variable.
   if (eval2(OUT arg, returnVar, evalarg) == FAIL)
      return FAIL;

   CS p = skipwhite(*arg);
   if (*p == '?') {
      int      op_falsy = p[1] == '?';
      int      result;
      Var   var2;
      EvalCtx   *evalarg_used = evalarg;
      EvalCtx   local_evalarg;
      Unt orig_flags;
      int evaluate;

      if (!evalarg) {
         init_evalarg(&local_evalarg);
         evalarg_used = &local_evalarg;
      }
      orig_flags = evalarg_used->eval_flags;
      evaluate = evalarg_used->eval_flags & EVAL_EVALUATE;

      *arg = p;

      result = FALSE;
      if (evaluate) {
         Boole error = false;

         if (op_falsy)
            result = tv2bool(returnVar);
         ei (varGetNumberChk(returnVar, OUT &error) != 0)
            result = TRUE;
         if (error || !op_falsy || !result)
            clearVar(returnVar);
         if (error)
            return FAIL;
      }

      //Get the second variable.  Recursive!
      if (op_falsy)
         ++*arg;
      *arg = skipwhite_and_linebreak(*arg + 1, evalarg_used);
      evalarg_used->eval_flags = (op_falsy ? !result : result)
                 ? orig_flags : (orig_flags & ~EVAL_EVALUATE);
      if (eval1(OUT arg, &var2, evalarg_used) == FAIL) {
         evalarg_used->eval_flags = orig_flags;
         return FAIL;
      }
      if (!op_falsy || !result)
         *returnVar = var2;

      if (!op_falsy) {
         //Check for the ":".
         p = skipwhite(*arg);
         if (*p != ':') {
            emsg(_(e_missing_colon_after_questionmark));
            if (evaluate && result)
               clearVar(returnVar);
            evalarg_used->eval_flags = orig_flags;
            return FAIL;
         }
         *arg = p;

         //Get the third variable.  Recursive!
         *arg = skipwhite_and_linebreak(*arg + 1, evalarg_used);
         evalarg_used->eval_flags = !result ? orig_flags : (orig_flags & ~EVAL_EVALUATE);
         if (eval1(OUT arg, &var2, evalarg_used) == FAIL) {
            if (evaluate && result)
               clearVar(returnVar);
            evalarg_used->eval_flags = orig_flags;
            return FAIL;
         }
         if (evaluate && !result)
            *returnVar = var2;
      }

      if (!evalarg)
         clear_evalarg(&local_evalarg, NULL);
      else
         evalarg->eval_flags = orig_flags;
   }

   return OK;
}

//Handle first level expression:
//  expr2 || expr2 || expr2       logical OR
//
//"arg" must point to the first non-white of the expression.
//"arg" is advanced to just after the recognized expression.
//
//Return OK or FAIL.
private int
eval2(OUT Byte **arg, Var *returnVar, EvalCtx* evalarg) {
   // Get the first expression.
   if (eval3(arg, returnVar, evalarg) == FAIL)
      return FAIL;

   // Handle the  "||" operator.
   CS p = skipwhite(*arg);
   if (p[0] == '|' && p[1] == '|') {
      EvalCtx   *evalarg_used = evalarg;
      EvalCtx   local_evalarg;
      int       evaluate;
      int       orig_flags;
      long  result = FALSE;
      Var    var2;
      Boole error = false;

      if (evalarg == NULL) {
         init_evalarg(&local_evalarg);
         evalarg_used = &local_evalarg;
      }
      orig_flags = evalarg_used->eval_flags;
      evaluate = orig_flags & EVAL_EVALUATE;
      if (evaluate) {
         if (varGetNumberChk(returnVar, OUT &error) != 0)
            result = TRUE;
         clearVar(returnVar);
         if (error)
            return FAIL;
      }

      //Repeat until there is no following "||".
      while (p[0] == '|' && p[1] == '|') {
         *arg = p;

         //Get the second variable.
         *arg = skipwhite_and_linebreak(*arg + 2, evalarg_used);
         evalarg_used->eval_flags = !result ? orig_flags : (orig_flags & ~EVAL_EVALUATE);
         if (eval3(arg, &var2, evalarg_used) == FAIL)
            return FAIL;

         //Compute the result.
         if (evaluate && !result) {
            if (varGetNumberChk(&var2, OUT &error) != 0)
               result = TRUE;
            clearVar(&var2);
            if (error)
               return FAIL;
         }
         if (evaluate) {
            returnVar->tag = VAR_NUMBER;
            returnVar->number = result;
         }

         p = skipwhite(*arg);
      }

      if (evalarg == NULL)
          clear_evalarg(&local_evalarg, NULL);
      else
          evalarg->eval_flags = orig_flags;
   }

   return OK;
}

// Handle second level expression:
//   expr3 && expr3 && expr3       logical AND
//
// "arg" must point to the first non-white of the expression.
// "arg" is advanced to just after the recognized expression.
//
// Return OK or FAIL.
private int
eval3(Byte **arg, Var *returnVar, EvalCtx *evalarg) {
   Byte   *p;

   // Get the first expression.
   if (eval4(arg, returnVar, evalarg) == FAIL)
      return FAIL;

   // Handle the "&&" operator.
   p = skipwhite(*arg);
   if (p[0] == '&' && p[1] == '&') {
      EvalCtx   *evalarg_used = evalarg;
      EvalCtx   local_evalarg;
      int evaluate;
      long result = TRUE;
      Var var2;
      Boole error = false;

      if (evalarg == NULL) {
          init_evalarg(&local_evalarg);
          evalarg_used = &local_evalarg;
      }
      Unt orig_flags = evalarg_used->eval_flags;
      evaluate = orig_flags & EVAL_EVALUATE;
      if (evaluate) {
         if (varGetNumberChk(returnVar, OUT &error) == 0)
            result = FALSE;
         clearVar(returnVar);
         if (error)
            return FAIL;
      }

      //Repeat until there is no following "&&".
      while (p[0] == '&' && p[1] == '&') {
         *arg = p;

         //Get the second variable.
         *arg = skipwhite_and_linebreak(*arg + 2, evalarg_used);
         evalarg_used->eval_flags = result ? orig_flags : (orig_flags & ~EVAL_EVALUATE);
         CLEAR_FIELD(var2);
         if (eval4(arg, &var2, evalarg_used) == FAIL)
            return FAIL;

         //Compute the result.
         if (evaluate && result) {
            if (varGetNumberChk(&var2, OUT &error) == 0)
               result = FALSE;
            clearVar(&var2);
            if (error)
               return FAIL;
         }
         if (evaluate) {
            returnVar->tag = VAR_NUMBER;
            returnVar->number = result;
         }

         p = skipwhite(*arg);
      }

      if (evalarg == NULL)
          clear_evalarg(&local_evalarg, NULL);
      else
          evalarg->eval_flags = orig_flags;
   }

   return OK;
}

//Handle third level expression:
//  var1 == var2
//  var1 =~ var2
//  var1 != var2
//  var1 !~ var2
//  var1 > var2
//  var1 >= var2
//  var1 < var2
//  var1 <= var2
//  var1 is var2
//  var1 isnot var2
//
//"arg" must point to the first non-white of the expression.
//"arg" is advanced to just after the recognized expression.
//
//Return OK or FAIL.
private int
eval4(Byte **arg, Var *returnVar, EvalCtx *evalarg) {
   Byte   *p;
   ExprType   type = EXPR_UNKNOWN;
   int len = 2;
   int type_is = FALSE;

   //Get the first expression.
   if (eval5(arg, returnVar, evalarg) == FAIL)
      return FAIL;

   p = skipwhite(*arg);

   type = get_compare_type(p, &len, &type_is);

   //If there is a comparative operator, use it.
   if (type != EXPR_UNKNOWN) {
      Var var2;
      Boole ic = false;
      int evaluate = evalarg == NULL ? 0 : (evalarg->eval_flags & EVAL_EVALUATE);
      long comp_lnum = SOURCING_LNUM;

      // extra question mark appended: ignore case
      if (p[len] == '?') {
         ic = true;
         ++len;
      }

      //Get the second variable.
      *arg = skipwhite_and_linebreak(p + len, evalarg);
      if (eval5(arg, &var2, evalarg) == FAIL) {
         clearVar(returnVar);
         return FAIL;
      }
      if (evaluate) {
         int ret;

         // use the line of the comparison for messages
         SOURCING_LNUM = comp_lnum;
         ret = typval_compare(returnVar, &var2, type, ic);
         clearVar(&var2);
         return ret;
      }
   }

   return OK;
}

// Make a copy of blob "tv1" and append blob "tv2".
void
eval_addblob(Var *tv1, Var *tv2) {
   Blob  *b1 = tv1->blob;
   Blob  *b2 = tv2->blob;
   Blob  *b = blob_alloc();
   int       i;

   if (b == NULL)
      return;

   for (i = 0; i < blob_len(b1); i++)
   ga_append(&b->c, blob_get(b1, i));
   for (i = 0; i < blob_len(b2); i++)
   ga_append(&b->c, blob_get(b2, i));

    clearVar(tv1);
    returnVar_blob_set(tv1, b);
}

// Make a copy of list "tv1" and append list "tv2".
int
eval_addlist(Var *tv1, Var *tv2) {
   Var var3;

   // concatenate Lists
   if (list_concat(tv1->list, tv2->list, &var3) == FAIL) {
      clearVar(tv1);
      clearVar(tv2);
      return FAIL;
   }
   clearVar(tv1);
   *tv1 = var3;
   return OK;
}

// Left or right shift the number "tv1" by the number "tv2" and store the result in "tv1".
// Return OK or FAIL.
private int
eval_shift_number(Var *tv1, Var *tv2, int shift_type) {
   if (tv2->tag != VAR_NUMBER || tv2->number < 0) {
      // right operand should be a positive number
      if (tv2->tag != VAR_NUMBER)
         emsg(_(e_bitshift_ops_must_be_number));
      else
         emsg(_(e_bitshift_ops_must_be_positive));
      clearVar(tv1);
      clearVar(tv2);
      return FAIL;
   }

   if (tv2->number > MAX_LSHIFT_BITS)
      // shifting more bits than we have always results in zero
      tv1->number = 0;
   ei (shift_type == EXPR_LSHIFT)
      tv1->number = (ULong)tv1->number << tv2->number;
   else
      tv1->number = (ULong)tv1->number >> tv2->number;
   return OK;
}

//Handle fourth level expression (bitwise left/right shift operators):
//  var1 << var2
//  var1 >> var2
//
//"arg" must point to the first non-white of the expression.
//"arg" is advanced to just after the recognized expression.
//
//Return OK or FAIL.
private int
eval5(Byte **arg, Var *returnVar, EvalCtx *evalarg) {
   // Get the first expression.
   if (eval6(arg, returnVar, evalarg) == FAIL)
      return FAIL;

   // Repeat computing, until no '<<' or '>>' is following.
   for (;;) {
      ExprType   exprtype;
      int      evaluate;

      CS p = skipwhite(*arg);
      if (p[0] == '<' && p[1] == '<')
         exprtype = EXPR_LSHIFT;
      ei (p[0] == '>' && p[1] == '>')
         exprtype = EXPR_RSHIFT;
      else
         return OK;

      // Handle a bitwise left or right shift operator
      evaluate = evalarg == NULL ? 0 : (evalarg->eval_flags & EVAL_EVALUATE);
      if (evaluate && returnVar->tag != VAR_NUMBER) {
         // left operand should be a number
         emsg(_(e_bitshift_ops_must_be_number));
         clearVar(returnVar);
         return FAIL;
      }

      // Get the second variable.
      *arg = skipwhite_and_linebreak(p + 2, evalarg);
      Var var2;
      if (eval6(arg, &var2, evalarg) == FAIL) {
         clearVar(returnVar);
         return FAIL;
      }

      if (evaluate && eval_shift_number(returnVar, &var2, exprtype) == FAIL)
         return FAIL;

      clearVar(&var2);
   }

    return OK;
}

// Concatenate strings "tv1" and "tv2" and store the result in "tv1".
private int
eval_concat_str(Var *tv1, Var *tv2) {
   Byte   buf1[NUMBUFLEN], buf2[NUMBUFLEN];
   Byte   *s1 = tv_get_string_buf(tv1, buf1);
   Byte   *s2 = NULL;
   Byte   *p;

   s2 = convertVarToString(tv2, buf2);
   if (s2 == NULL) {     // type error ?
      clearVar(tv1);
      clearVar(tv2);
      return FAIL;
   }

   p = concat_str(s1, s2);
   clearVar(tv1);
   tv1->tag = VAR_STRING;
   tv1->string = p;

   return OK;
}

//Add or subtract numbers "tv1" and "tv2" and store the result in "tv1".
//The numbers can be whole numbers or floats.
private int
eval_addsub_number(Var *tv1, Var *tv2, int op) {
   Boole error = false;
   Long   n1, n2;
   double   f1 = 0, f2 = 0;

   if (tv1->tag == VAR_FLOAT) {
      f1 = tv1->floatt;
      n1 = 0;
   } else {
      n1 = varGetNumberChk(tv1, OUT &error);
      if (error) {
         //This can only happen for "list + non-list" or
         //"blob + non-blob".  For "non-list + ..." or
         //"something - ...", we returned before evaluating the 2nd operand.
         clearVar(tv1);
         clearVar(tv2);
         return FAIL;
      }
      if (tv2->tag == VAR_FLOAT)
         f1 = n1;
   }
   if (tv2->tag == VAR_FLOAT) {
      f2 = tv2->floatt;
      n2 = 0;
   } else {
      n2 = varGetNumberChk(tv2, OUT &error);
      if (error) {
         clearVar(tv1);
         clearVar(tv2);
         return FAIL;
      }
      if (tv1->tag == VAR_FLOAT)
         f2 = n2;
   }
   clearVar(tv1);

   // If there is a float on either side the result is a float.
   if (tv1->tag == VAR_FLOAT || tv2->tag == VAR_FLOAT) {
      if (op == '+')
         f1 = f1 + f2;
      else
         f1 = f1 - f2;
      tv1->tag = VAR_FLOAT;
      tv1->floatt = f1;
   } else {
      if (op == '+')
         n1 = n1 + n2;
      else
         n1 = n1 - n2;
      tv1->tag = VAR_NUMBER;
      tv1->number = n1;
   }

   return OK;
}

// Handle fifth level expression:
//   +   number addition, concatenation of list or blob
//   -   number subtraction
//   ..   string concatenation
//
// "arg" must point to the first non-white of the expression.
// "arg" is advanced to just after the recognized expression.
//
// Return OK or FAIL.
private int
eval6(Byte **arg, Var *returnVar, EvalCtx *evalarg) {
   if (eval7(arg, returnVar, evalarg, FALSE) == FAIL)
      return FAIL;

   //Repeat computing, until no '+', '-' or '.' is following.
   for (;;) {
      Var var2;
      long op_lnum = SOURCING_LNUM;

      //"+=", "-=" and "..=" are assignments
      //"++" and "--" on the next line are a separate command.
      CS p = skipwhite(*arg);
      int op = *p;
      int concat = op == '.' && (*(p + 1) == '.');
      if ((op != '+' && op != '-' && !concat) || p[1] == '=' || (p[1] == '.' && p[2] == '='))
         break;

      int evaluate = evalarg == NULL ? 0 : (evalarg->eval_flags & EVAL_EVALUATE);
      int oplen = (concat && p[1] == '.') ? 2 : 1;
      *arg = p;
      if ((op != '+' || (returnVar->tag != VAR_LIST && returnVar->tag != VAR_BLOB))
         && (op == '.' || returnVar->tag != VAR_FLOAT)
         && evaluate
      ) {
         Boole error = false;

         //For "list + ...", an illegal use of the first operand as a number cannot be determined 
         //before evaluating the 2nd operand: if this is also a list, all is ok.
         //For "something . ...", "something - ..." or "non-list + ...", we know that the first
         //operand needs to be a string or number without evaluating the 2nd operand. So check 
         //before to avoid side effects after an error.
         if (op != '.')
            varGetNumberChk(returnVar, OUT &error);
         if ((op == '.' && convertVarToStringSingleUse(returnVar) == NULL) || error) {
            clearVar(returnVar);
            return FAIL;
         }
      }

      //Get the second variable.
      *arg = skipwhite_and_linebreak(*arg + oplen, evalarg);
      if (eval7(arg, &var2, evalarg, op == '.') == FAIL) {
         clearVar(returnVar);
         return FAIL;
      }

      if (evaluate) {
         // Compute the result. use the line of the operation for messages
         SOURCING_LNUM = op_lnum;
         if (op == '.') {
            if (eval_concat_str(returnVar, &var2) == FAIL)
                return FAIL;
         } ei (op == '+' && returnVar->tag == VAR_BLOB && var2.tag == VAR_BLOB)
            eval_addblob(returnVar, &var2);
         ei (op == '+' && returnVar->tag == VAR_LIST && var2.tag == VAR_LIST) {
            if (eval_addlist(returnVar, &var2) == FAIL)
                return FAIL;
         } else {
            if (eval_addsub_number(returnVar, &var2, op) == FAIL)
               return FAIL;
         }
         clearVar(&var2);
      }
   }
   return OK;
}

//Multiply or divide or compute the modulo of numbers "tv1" and "tv2" and
//store the result in "tv1".  The numbers can be whole numbers or floats.
private int
eval_multdiv_number(Var *tv1, Var *tv2, int op) {
   Long   n1, n2;
   double   f1, f2;
   Boole use_float = false;

   f1 = 0;
   f2 = 0;
   Boole error = FALSE;
   if (tv1->tag == VAR_FLOAT) {
      f1 = tv1->floatt;
      use_float = true;
      n1 = 0;
   } else
      n1 = varGetNumberChk(tv1, OUT &error);
   clearVar(tv1);
   if (error) {
      clearVar(tv2);
      return FAIL;
   }

   if (tv2->tag == VAR_FLOAT) {
      if (!use_float) {
         f1 = n1;
         use_float = true;
      }
      f2 = tv2->floatt;
      n2 = 0;
   } else {
      n2 = varGetNumberChk(tv2, OUT &error);
      clearVar(tv2);
      if (error)
         return FAIL;
      if (use_float)
          f2 = n2;
   }

   //Compute the result. When either side is a float the result is a float.
   if (use_float) {
      if (op == '*')
         f1 = f1 * f2;
      ei (op == '/') {
         // We rely on the floating point library to handle divide
         // by zero to result in "inf" and not a crash.
         f1 = f1 / f2;
      } else {
         emsg(_(e_cannot_use_percent_with_float));
         return FAIL;
      }
      tv1->tag = VAR_FLOAT;
      tv1->floatt = f1;
   } else {
      Boole failed = false;

      if (op == '*')
          n1 = n1 * n2;
      ei (op == '/')
          n1 = num_divide(n1, n2, OUT &failed);
      else
          n1 = num_modulus(n1, n2, OUT &failed);
      if (failed)
          return FAIL;

      tv1->tag = VAR_NUMBER;
      tv1->number = n1;
   }

    return OK;
}

//Handle sixth level expression:
//  *   number multiplication
//  /   number division
//  %   number modulo
//
//"arg" must point to the first non-white of the expression.
//"arg" is advanced to just after the recognized expression.
//
//Return OK or FAIL.
private int
eval7(
   Byte   **arg,
   Var   *returnVar,
   EvalCtx   *evalarg,
   Boole      want_string)  // after "." operator
{
   if (eval8(arg, returnVar, evalarg, want_string) == FAIL)
      return FAIL;

   // Repeat computing, until no '*', '/' or '%' is following.
   for (;;) {
      int       evaluate;
      Var    var2;
      Byte       *p;
      int       op;

      // "*=", "/=" and "%=" are assignments
      p = skipwhite(*arg);
      op = *p;
      if ((op != '*' && op != '/' && op != '%') || p[1] == '=')
          break;

      evaluate = evalarg == NULL ? 0 : (evalarg->eval_flags & EVAL_EVALUATE);
      *arg = p;

      // Get the second variable.
      *arg = skipwhite_and_linebreak(*arg + 1, evalarg);
      if (eval8(arg, &var2, evalarg, FALSE) == FAIL)
          return FAIL;

      if (evaluate)
          // Compute the result.
          if (eval_multdiv_number(returnVar, &var2, op) == FAIL)
         return FAIL;
    }

    return OK;
}

//Handle seventh level expression: a type cast before a base level expression.
//"arg" must point to the first non-white of the expression.
//"arg" is advanced to just after the recognized expression. Return OK or FAIL.
private int
eval8(
   OUT CS* arg,
   Var* returnVar,
   EvalCtx* evalarg,
   Boole want_string   // after "." operator
){
   Boole evaluate = evalarg && (evalarg->eval_flags & EVAL_EVALUATE) != 0;

   int res = eval9(arg, returnVar, evalarg, want_string);

   TypeSpec* want_type = NULL;
   ArrayList type_list;       // list of pointers to allocated types
   if (want_type && evaluate) {
      clear_type_list(&type_list);
   }

   return res;
}

int
eval_leader(Byte **arg) {
   Byte   *p = *arg;

   while (*p == '!' || *p == '-' || *p == '+') {
      CS n = skipwhite(p + 1);
      p = n;
   }
   *arg = p;
   return OK;
}

//Check for a predefined value "true", "false" and "null.*". Return OK when recognized.
int
handle_predefined(CS s, int len, Var *returnVar) {
   switch (len) {
   case 4: 
      if (STRNCMP(s, "true", 4) == 0) {
          returnVar->tag = VAR_BOOL;
          returnVar->number = VVAL_TRUE;
          return OK;
      }
      if (STRNCMP(s, "null", 4) == 0) {
          returnVar->tag = VAR_SPECIAL;
          returnVar->number = VVAL_NULL;
          return OK;
      }
      break;
   case 5: 
      if (STRNCMP(s, "false", 5) == 0) {
          returnVar->tag = VAR_BOOL;
          returnVar->number = VVAL_FALSE;
          return OK;
      }
      break;
   case 8: if (STRNCMP(s, "null_job", 8) == 0)
      {
          returnVar->tag = VAR_JOB;
          returnVar->job = NULL;
          return OK;
      }
      break;
   case 9:
      if (STRNCMP(s, "null_", 5) != 0)
          break;
      // null_list
      if (STRNCMP(s + 5, "list", 4) == 0) {
          returnVar->tag = VAR_LIST;
          returnVar->list = NULL;
          return OK;
      }
      // null_dict
      if (STRNCMP(s + 5, "dict", 4) == 0) {
          returnVar->tag = VAR_BAG;
          returnVar->bag = NULL;
          return OK;
      }
      // null_blob
      if (STRNCMP(s + 5, "blob", 4) == 0) {
          returnVar->tag = VAR_BLOB;
          returnVar->blob = NULL;
          return OK;
      }
      break;
   case 10:
      if (STRNCMP(s, "null_", 5) != 0)
          break;
      break;
   case 11: 
      if (STRNCMP(s, "null_string", 11) == 0) {
          returnVar->tag = VAR_STRING;
          returnVar->string = NULL;
          return OK;
      }
      break;
   case 12:
      if (STRNCMP(s, "null_channel", 12) == 0) {
          returnVar->tag = VAR_CHANNEL;
          returnVar->channel = NULL;
          return OK;
      }
      if (STRNCMP(s, "null_partial", 12) == 0) {
          returnVar->tag = VAR_PARTIAL;
          returnVar->partial = NULL;
          return OK;
      }
      break;
   case 13: 
      if (STRNCMP(s, "null_function", 13) == 0) {
          returnVar->tag = VAR_FUNC;
          returnVar->string = NULL;
          return OK;
      }
      break;
    }
    return FAIL;
}

// Handle register contents: @r.
private void
eval9_reg_contents(
   Byte   **arg,
   Var   *returnVar,
   int      evaluate)
{

   ++*arg;   // skip '@'
   if (evaluate) {
       returnVar->tag = VAR_STRING;
       returnVar->string = get_reg_contents(**arg, GREG_EXPR_SRC);
   }
   if (**arg != ZERO)
      ++*arg;
}

// Handle a nested expression: (expression) or lambda: (arg) => expr
private int
eval9_nested_expr(
   OUT CS* arg,
   Var* returnVar,
   EvalCtx* evalarg
) {
   int ret = NOTDONE;

   *arg = skipwhite_and_linebreak(*arg + 1, evalarg);
   if (**arg == ')')
      // empty list
      ret = eval_list(arg, returnVar, evalarg, TRUE);
   else {
      ret = eval1(OUT arg, returnVar, evalarg);   // recursive!
      if (ret != OK)
         return ret;

      *arg = skipwhite_and_linebreak(*arg, evalarg);

      if (**arg == ')')
         ++*arg;
      ei (ret == OK) {
         emsg(_(e_missing_closing_paren));
         clearVar(returnVar);
         ret = FAIL;
      }
   }

   return ret;
}

// Handle be a variable or function name. Can also be a curly-braces kind of name: {expr}.
private int
eval9_var_func_name(
   CS* arg,
   Var* returnVar,
   EvalCtx* evalarg,
   int      evaluate,
   CS* name_start)
{
   int      ret = OK;
   CS s = *arg;
   CS alias;
   int len = get_name_len(arg, OUT &alias, evaluate, TRUE);
   if (alias)
      s = alias;

   if (len <= 0)
      ret = FAIL;
   else {
      Unt flags = evalarg == NULL ? 0 : evalarg->eval_flags;

      if (*skipwhite(*arg) == '(') {
         //"name(..."  recursive!
         *arg = skipwhite(*arg);
         ret = eval_func(arg, evalarg, (Text){s, len}, returnVar, flags, NULL);
      } ei (evaluate) {
         //get the value of "true", "false", etc. or a variable
         ret = FAIL;
         if (ret == FAIL) {
            *name_start = s;
            ret = eval_variable((Text){.c = s, .len = len}, returnVar, NULL, EVAL_VAR_VERBOSE);
         }
      } else {
         // skip the name
         check_vars((Text){s, len});
         ret = OK;
      }
   }
   eeglFree(alias);

   return ret;
}

//Handle eighth level expression:
// number      number constant
// 0zFFFFFFFF      Blob constant
// "string"      string constant
// 'string'      literal string constant
// &option-name   option value
// @r         register contents
// identifier      variable value
// function()      function call
// $VAR      environment variable
// (expression)   nested expression
// [expr, expr]   List
// {arg, arg -> expr}   Lambda
// {key: val, key: val}   Dictionary
// #{key: val, key: val}  Dictionary with literal keys
//
// Also handle:
// ! in front      logical NOT
// - in front      unary minus
// + in front      unary plus (ignored)
// trailing []      subscript in String or List
// trailing .name   entry in Dictionary
// trailing ->name()   method call
//
//"arg" must point to the first non-white of the expression.
//"arg" is advanced to just after the recognized expression.
//
//Return OK or FAIL.
private int
eval9(
   OUT CS* arg,
   Var   *returnVar,
   EvalCtx   *evalarg,
   Boole      want_string)   // after "." operator
{
   Boole evaluate = evalarg && (evalarg->eval_flags & EVAL_EVALUATE);
   CS name_start = NULL;
   CS start_leader;
   CS end_leader;
   int      ret = OK;
   static int   recurse = 0;

   // Initialize variable so that clearVar() can't mistake this for a string and free it
   returnVar->tag = VAR_UNKNOWN;

   // Skip '!', '-' and '+' characters.  They are handled later.
   start_leader = *arg;
   if (eval_leader(arg) == FAIL)
      return FAIL;
   end_leader = *arg;

   if (**arg == '.' && (!SAFE_isdigit(*(*arg + 1)))) {
      showErrFmtMsg(_(e_invalid_expression_str), *arg);
      ++*arg;
      return FAIL;
   }

   // Limit recursion to 1000 levels.  At least at 10000 we run out of stack and crash.
   if (recurse == 1000) {
      showErrFmtMsg(_(e_expression_too_recursive_str), *arg);
      return FAIL;
   }
   ++recurse;

   switch (**arg) {
   // Number constant.
   case '0':
   case '1':
   case '2':
   case '3':
   case '4':
   case '5':
   case '6':
   case '7':
   case '8':
   case '9':
   case '.': 
      ret = eval_number(arg, returnVar, evaluate, want_string);

      // Apply prefixed "-" and "+" now.  Matters especially when "->" follows.
      if (ret == OK && evaluate && end_leader > start_leader && returnVar->tag != VAR_BLOB)
          ret = eval9_leader(returnVar, TRUE, start_leader, &end_leader);
      break;

   // String constant: "string".
   case '"':   ret = evalStringLiteral(arg, returnVar, evaluate, FALSE); break;

   // Literal string constant: 'str''ing'.
   case '\'':   ret = evalRawString(arg, returnVar, evaluate, FALSE); break;

   // List: [expr, expr]
   case '[':   ret = eval_list(arg, returnVar, evalarg, TRUE); break;

   // Literal Dictionary: #{key: val, key: val}
   case '#':   ret = bagEvalLiteral(arg, returnVar, evalarg); break;

   // Lambda: {arg, arg -> expr} Dictionary: {'key': val, 'key': val}
   case '{':   
      ret = get_lambda_tv(arg, returnVar, evalarg);
      if (ret == NOTDONE)
          ret = bagEval(arg, returnVar, evalarg, FALSE);
      break;

   // Option value: &name
   case '&':   ret = eval_option(arg, returnVar, evaluate); break;

   // Environment variable: $VAR. Interpolated string: $"string" or $'string'.
   case '$':   
      if ((*arg)[1] == '"' || (*arg)[1] == '\'')
         ret = eval_interp_string(arg, returnVar, evaluate);
      else
         ret = eval_env_var(arg, returnVar, evaluate);
      break;

   // Register contents: @r.
   case '@':   eval9_reg_contents(arg, returnVar, evaluate); break;

   //nested expression: (expression). or lambda: (arg) => expr or tuple
   case '(':   ret = eval9_nested_expr(arg, returnVar, evalarg); break;

   default:   ret = NOTDONE; break;
   }

   if (ret == NOTDONE) {
      // Must be a variable or function name. Can also be a curly-braces kind of name: {expr}.
      ret = eval9_var_func_name(arg, returnVar, evalarg, evaluate, &name_start);
   }

   // Handle following '[', '(' and '.' for expr[expr], expr.name, expr(expr), expr->name(expr)
   if (ret == OK)
      ret = handle_subscript(arg, returnVar, evalarg, evaluate);

   // Apply logical NOT and unary '-', from right to left, ignore '+'.
   if (ret == OK && evaluate && end_leader > start_leader)
      ret = eval9_leader(returnVar, FALSE, start_leader, &end_leader);

   --recurse;
   return ret;
}

//Apply the leading "!" and "-" before an eval9 expression to "returnVar". When "numeric_only" 
//is TRUE only handle "+" and "-". Adjust "end_leaderp" until it is at "start_leader".
private int
eval9_leader(
   Var* returnVar,
   int numeric_only,
   CS start_leader,
   CS* end_leaderp)
{
   CS end_leader = *end_leaderp;
   int      ret = OK;
   Long val = 0;
   double       f = 0.0;
   Boole error = false;
   
   if (returnVar->tag == VAR_FLOAT)
      f = returnVar->floatt;
   else {
      while (SPACE_OR_TAB(end_leader[-1]))
         --end_leader;
      val = varGetNumberChk(returnVar, OUT &error);
   }
   if (error) {
      clearVar(returnVar);
      ret = FAIL;
   } else {
      while (end_leader > start_leader) {
         --end_leader;
         if (*end_leader == '!') {
            if (numeric_only) {
               ++end_leader;
               break;
            }
            if (returnVar->tag == VAR_FLOAT) {
               f = !f;
            } else {
               val = !val;
            }
         } ei (*end_leader == '-') {
            if (returnVar->tag == VAR_FLOAT)
               f = -f;
            else {
               val = -val;
            }
         }
      }
      if (returnVar->tag == VAR_FLOAT) {
          clearVar(returnVar);
          returnVar->floatt = f;
      } else {
         clearVar(returnVar);
         returnVar->tag = VAR_NUMBER;
          returnVar->number = val;
      }
   }
   *end_leaderp = end_leader;
   return ret;
}

// Call the function referred to in "returnVar".
private int
call_func_returnVar(
   Byte       **arg,
   EvalCtx   *evalarg,
   Var    *returnVar,
   int       evaluate,
   Bag       *selfdict,
   Var    *basetv
) {
   PartiallyApplied   *pt = NULL;
   FnExe   funcexe;
   Var   functv;
   Byte   *s;
   int      ret;

   // need to copy the funcref so that we can clear returnVar
   if (evaluate) {
      functv = *returnVar;
      returnVar->tag = VAR_UNKNOWN;

      // Invoke the function.  Recursive!
      if (functv.tag == VAR_PARTIAL) {
         pt = functv.partial;
         s = partial_name(pt);
      } else {
         s = functv.string;
         if (s == NULL || *s == ZERO) {
            emsg(_(e_empty_function_name));
            ret = FAIL;
            goto theend;
         }
      }
   } else
      s = Em;

   CLEAR_FIELD(funcexe);
   funcexe.fe_firstline = curPor->cursor.lnum;
   funcexe.fe_lastline = curPor->cursor.lnum;
   funcexe.fe_evaluate = evaluate;
   funcexe.fe_partial = pt;
   funcexe.fe_selfdict = selfdict;
   funcexe.fe_basetv = basetv;
   ret = get_func_tv(s, -1, returnVar, arg, evalarg, &funcexe);

theend:
   //Clear the funcref afterwards, so that deleting it while
   //evaluating the arguments is possible (see test55).
   if (evaluate)
      clearVar(&functv);

   return ret;
}

//Evaluate "->method()".
//"*arg" points to "method". Return FAIL or OK. "*arg" is advanced to after the ')'.
private int
eval_lambda(
   Byte   **arg,
   Var   *returnVar,
   EvalCtx   *evalarg,
   Boole verbose   // give error messages
){
   int      evaluate = evalarg && (evalarg->eval_flags & EVAL_EVALUATE);
   Var   base = *returnVar;
   int      ret;

   returnVar->tag = VAR_UNKNOWN;

   if (**arg == '{') {
      // ->{lambda}()
      ret = get_lambda_tv(arg, returnVar, evalarg);
   } else {
      // ->(lambda)()
      ++*arg;
      ret = eval1(OUT arg, returnVar, evalarg);
      *arg = skipwhite_and_linebreak(*arg, evalarg);
      if (**arg != ')') {
         emsg(_(e_missing_closing_paren));
         return FAIL;
      }
      if (returnVar->tag != VAR_STRING && returnVar->tag != VAR_FUNC
                         && returnVar->tag != VAR_PARTIAL
      ){
         emsg(_(e_string_or_function_required_for_arrow_parens_expr));
         return FAIL;
      }
      ++*arg;
   }
   if (ret != OK)
      return FAIL;

   if (**arg != '(') {
      if (verbose) {
         if (*skipwhite(*arg) == '(')
            emsg(_(e_no_white_space_allowed_before_parenthesis));
         else
            showErrFmtMsg(_(e_missing_parenthesis_str), "lambda");
      }
      clearVar(returnVar);
      ret = FAIL;
   } else
      ret = call_func_returnVar(arg, evalarg, returnVar, evaluate, NULL, &base);

   // Clear the funcref afterwards, so that deleting it while
   // evaluating the arguments is possible (see test55).
   if (evaluate)
      clearVar(&base);

   return ret;
}

//Evaluate "->method()". "*arg" points to "method".
//Return FAIL or OK. "*arg" is advanced to after the ')'.
private int
eval_method(
   Byte   **arg,
   Var   *returnVar,
   EvalCtx   *evalarg,
   Boole verbose   // give error messages
){
   Byte   *tofree = NULL;
   Var   base = *returnVar;
   int      ret = OK;
   int      evaluate = evalarg && (evalarg->eval_flags & EVAL_EVALUATE);

   returnVar->tag = VAR_UNKNOWN;

   CS name = *arg;
   CS alias;
   long len = get_name_len(arg, &alias, evaluate, evaluate);
   if (alias)
      name = alias;

   if (len <= 0) {
      if (verbose)
          emsg(_(e_missing_name_after_method));
      ret = FAIL;
    } else {
      Byte *paren;

      // If there is no "(" immediately following, but there is further on,
      // it can be "import.Func()", "dict.Func()", "list[nr]", etc.
      // Does not handle anything where "(" is part of the expression.
      *arg = skipwhite(*arg);

      if (**arg != '(' && alias == NULL && (paren = firstOccurrence(*arg, '(')) != NULL) {
         *arg = name;

         // Truncate the name at the "(".  Avoid trying to get another line
         // by making "getline" NULL.
         *paren = ZERO;
         LineGetter getline = NULL;
         if (evalarg) {
            getline = evalarg->eval_getline;
            evalarg->eval_getline = NULL;
         }

         CS deref = deref_function_name(arg, &tofree, evalarg, verbose);
         if (!deref) {
            *arg = name + len;
            ret = FAIL;
         } else {
            name = deref;
            len = (long)STRLEN(name);
         }

         *paren = '(';
         if (getline)
            evalarg->eval_getline = getline;
      }

      if (ret == OK) {
         *arg = skipwhite(*arg);

         if (**arg != '(') {
            if (verbose)
                showErrFmtMsg(_(e_missing_parenthesis_str), name);
            ret = FAIL;
         } ei (SPACE_OR_TAB((*arg)[-1])) {
            if (verbose)
               emsg(_(e_no_white_space_allowed_before_parenthesis));
            ret = FAIL;
         } else
            ret = eval_func(arg, evalarg, (Text){name, len}, returnVar,
                       evaluate ? EVAL_EVALUATE : 0, &base);
      }
    }

   // Clear the funcref afterwards, so that deleting it while
   // evaluating the arguments is possible (see test55).
   if (evaluate)
      clearVar(&base);
   eeglFree(tofree);

   if (alias)
      eeglFree(alias);

   return ret;
}

//Evaluate an "[expr]" or "[expr:expr]" index.  Also "dict.key".
//"*arg" points to the '[' or '.'.
//Return FAIL or OK. "*arg" is advanced to after the ']'.
private int
eval_index(
   Byte   **arg,
   Var* returnVar,
   EvalCtx* evalarg,
   int verbose)   // give error messages
{
   int evaluate = evalarg && (evalarg->eval_flags & EVAL_EVALUATE);
   int empty1 = FALSE, empty2 = FALSE;
   Var var1, var2;
   int range = FALSE;

   if (check_can_index(returnVar, evaluate, verbose) == FAIL)
      return FAIL;

   initVarToNull(OUT &var1);
   initVarToNull(OUT &var2);
   CS key = Em;
   int keylen = -1;
   if (**arg == '.') {
      //dict.name
      key = *arg + 1;
      for (keylen = 0; isValidForFirstCharDictKey(key[keylen]); ++keylen)
          ;
      if (keylen == 0)
          return FAIL;
      *arg = key + keylen;
   } else {
      //something[idx]
      //Get the (first) variable from inside the [].
      *arg = skipwhite_and_linebreak(*arg + 1, evalarg);
      if (**arg == ':')
          empty1 = TRUE;
      ei (eval1(OUT arg, &var1, evalarg) == FAIL) {  // recursive!
          return FAIL;
      } ei (evaluate) {
          int error = FALSE;

         // allow for indexing with float

         error = convertVarToStringSingleUse(&var1) == NULL;
         if (error) {
            // not a number or string
            clearVar(&var1);
            return FAIL;
         }
      }

      //Get the second variable from inside the [:].
      *arg = skipwhite_and_linebreak(*arg, evalarg);
      if (**arg == ':') {
         range = TRUE;
         ++*arg;
         *arg = skipwhite_and_linebreak(*arg, evalarg);
         if (**arg == ']')
            empty2 = TRUE;
         ei (eval1(OUT arg, &var2, evalarg) == FAIL) {   // recursive!
            if (!empty1)
                clearVar(&var1);
            return FAIL;
         } ei (evaluate && convertVarToStringSingleUse(&var2) == NULL) {
            // not a number or string
            if (!empty1)
               clearVar(&var1);
            clearVar(&var2);
            return FAIL;
         }
      }

      // Check for the ']'.
      *arg = skipwhite_and_linebreak(*arg, evalarg);
      if (**arg != ']') {
          if (verbose)
         emsg(_(e_missing_closing_square_brace));
          clearVar(&var1);
          if (range)
         clearVar(&var2);
          return FAIL;
      }
      *arg = *arg + 1;   // skip over the ']'
   }

   if (evaluate) {
      int res = eval_index_inner(
         returnVar, range, empty1 ? NULL : &var1, empty2 ? NULL : &var2, FALSE, key, keylen, verbose
      );

      if (!empty1)
          clearVar(&var1);
      if (range)
          clearVar(&var2);
      return res;
   }
   return OK;
}

//}}}
//{{{auxiliary

#define DICT_MAXNEST 100   // maximum nesting of lists and dicts

// Return the function name of partial "pt".
CS
partial_name(PartiallyApplied *pt) {
   if (pt) {
      if (pt->name)
         return pt->name;
      if (pt->fn)
         return pt->fn->uf_name;
   }
   return Em;
}

private void
partial_free(PartiallyApplied *pt) {
   int i;

   for (i = 0; i < pt->argc; ++i)
      clearVar(&pt->argv[i]);
   eeglFree(pt->argv);
   bagUnref(pt->self);
   if (pt->name) {
      func_unref(pt->name);
      eeglFree(pt->name);
   } else
      func_ptr_unref(pt->fn);

   eeglFree(pt);
}

// Unreference a closure: decrement the reference count and free it when it becomes zero.
void
partial_unref(PartiallyApplied *pt) {
   if (!pt)
      return;

   if (--pt->refcount <= 0)
      partial_free(pt);
}

//Return a textual representation of a string in "tv".
//If the memory is allocated, "tofree" is set to it, otherwise NULL.
//When both "echo_style" and "composite_val" are FALSE, put quotes around
//strings as "string()", otherwise does not put quotes around strings. May return NULL.
private CS
string_tv2string(
   Var   *tv,
   Byte   **tofree,
   int      echo_style,
   int      composite_val)
{
   Byte   *r = NULL;

   if (echo_style && !composite_val) {
      *tofree = NULL;
      r = tv->string;
      if (r == NULL)
          r = (CS)"";
   } else {
      *tofree = string_quote(tv->string, FALSE);
      r = *tofree;
   }

   return r;
}

//Return a textual representation of a function in "tv".
//If the memory is allocated "tofree" is set to it, otherwise NULL.
//When "echo_style" is FALSE, put quotes around the function name as
//"function()", otherwise does not put quotes around function name. May return NULL.
private CS
func_tv2string(Var *tv, Byte **tofree, int echo_style) {
   CS r = NULL;
   Byte buf[MAX_FUNC_NAME_LEN];

   if (echo_style) {
      *tofree = NULL;

      if (tv->string == NULL)
         r = S"function()";
      else {
         r = make_ufunc_name_readable(tv->string, buf, MAX_FUNC_NAME_LEN);
         if (r == buf)
            r = *tofree = copyStr(buf);
      }
   } else {
      CS s = (tv->string) ? make_ufunc_name_readable(tv->string, buf, MAX_FUNC_NAME_LEN) : null;
      r = *tofree = string_quote(s, TRUE);
   }

   return r;
}

//Return a textual representation of the object method in "tv", a VAR_PARTIAL.
//If the memory is allocated "tofree" is set to it, otherwise NULL.
//When "echo_style" is FALSE, put quotes around the function name as
//"function()", otherwise does not put quotes around function name. May return NULL.
private CS
method_tv2string(Var *tv, Byte **tofree, int echo_style) {
   Byte buf[MAX_FUNC_NAME_LEN];
   PartiallyApplied   *pt = tv->partial;

   Unt len = eeSnprintf(buf, sizeof(buf), "<SNR>%d.%s",
            pt->fn->scriptCtx.sid,
            pt->fn->uf_name);
   if (len >= sizeof(buf)) {
      if (echo_style) {
         *tofree = NULL;
         return S"function()";
      } else
         return *tofree = string_quote(Em, TRUE);
   }

   return *tofree = echo_style ? copyStr(buf) : string_quote(buf, true);
}

//Return a textual representation of a partial in "tv".
//If the memory is allocated "tofree" is set to it, otherwise NULL.
//"numbuf" is used for a number.  May return NULL.
private CS
partial_tv2string(
    Var   *tv,
    Byte   **tofree,
    Byte   *numbuf,
    int      copyID)
{
    Byte   *r = NULL;
    PartiallyApplied   *pt;
    Byte   *fname;
    ArrayList   ga;
    int      i;
    Byte   *tf;

   pt = tv->partial;
   fname = string_quote(pt == NULL ? NULL : partial_name(pt), FALSE);

   ga_init2(&ga, 1, 100);
   ga_concat(&ga, (CS)"function(");
   if (fname) {
      // When using uf_name prepend "g:" for a global function.
      if (pt && pt->name == NULL && fname[0] == '\'' && eeIsUpper(fname[1])) {
         ga_concat(&ga, (CS)"'g:");
         ga_concat(&ga, fname + 1);
      } else
         ga_concat(&ga, fname);
      eeglFree(fname);
    }
   if (pt && pt->argc > 0) {
      ga_concat(&ga, (CS)", [");
      for (i = 0; i < pt->argc; ++i) {
          if (i > 0)
         ga_concat(&ga, (CS)", ");
          ga_concat(&ga, tv2string(&pt->argv[i], &tf, numbuf, copyID));
          eeglFree(tf);
      }
      ga_concat(&ga, (CS)"]");
   }
   if (pt && pt->self) {
      Var dtv;

      ga_concat(&ga, (CS)", ");
      dtv.tag = VAR_BAG;
      dtv.bag = pt->self;
      ga_concat(&ga, tv2string(&dtv, &tf, numbuf, copyID));
      eeglFree(tf);
   }
   // terminate with ')' and a ZERO
   ga_concat_len(&ga, (CS)")", 2);

   *tofree = ga.c;
   r = *tofree;

   return r;
}

//Return a textual representation of a List in "tv". If the memory is allocated, "tofree" is set 
//to it, otherwise NULL. When "copyID" is not zero replace recursive lists with "...". When 
//"restore_copyID" is FALSE, repeated items in lists are replaced with "...". May return NULL.
private CS
list_tv2string(
   Var   *tv,
   Byte   **tofree,
   int      copyID,
   int      restore_copyID
) {
   CS r = NULL;

   if (tv->list == NULL) {
      // NULL list is equivalent to empty list.
      *tofree = NULL;
      r = (CS)"[]";
   } ei (copyID != 0 && tv->list->copyId == copyID && tv->list->len > 0) {
      *tofree = NULL;
      r = (CS)"[...]";
   } else {
      int old_copyID;
      if (restore_copyID)
          old_copyID = tv->list->copyId;

      tv->list->copyId = copyID;
      *tofree = list2string(tv, copyID, restore_copyID);
      if (restore_copyID)
          tv->list->copyId = old_copyID;
      r = *tofree;
   }

   return r;
}

//Return a textual representation of a Bag in "tv". If the memory is allocated "tofree" is set to 
//it, otherwise NULL. When "copyID" is not zero replace recursive dicts with "...".
//When "restore_copyID" is FALSE, repeated items in the dictionary are replaced with "...". May 
//return NULL.
private CS
dict_tv2string(
   Var   *tv,
   Byte   **tofree,
   int      copyID,
   int      restore_copyID)
{
   CS r = NULL;

   if (tv->bag == NULL) {
      // NULL dict is equivalent to empty dict.
      *tofree = NULL;
      r = (CS)"{}";
   } ei (copyID != 0 && tv->bag->copyId == copyID && tv->bag->hashTable.count != 0) {
      *tofree = NULL;
      r = (CS)"{...}";
   } else {
      int old_copyID;
      if (restore_copyID)
          old_copyID = tv->bag->copyId;

      tv->bag->copyId = copyID;
      *tofree = bagToString(tv, copyID, restore_copyID);
      if (restore_copyID)
          tv->bag->copyId = old_copyID;
      r = *tofree;
   }

   return r;
}

//Return a textual representation of a job or a channel in "tv". If the memory is allocated 
//"tofree" is set to it, otherwise NULL. "numBuf" is used for a number.
//When "composite_val" is FALSE, put quotes around strings as "string()",
//otherwise does not put quotes around strings. May return NULL.
private CS
jobchan_tv2string(
   Var   *tv UNUSED,
   OUT Byte   **tofree UNUSED,
   OUT CS numBuf,
   int      composite_val
) {
   Byte   *r = NULL;

   *tofree = NULL;

   if (tv->tag == VAR_JOB) {
      job_to_string_buf(OUT numBuf, tv);
      r = numBuf;
   } else {
      channel_to_string_buf(OUT numBuf, tv);
      r = numBuf;
   } 

   if (composite_val) {
      *tofree = string_quote(r, FALSE);
      r = *tofree;
   }

   return r;
}

//Return a string with the string representation of a variable. If the memory is allocated 
//"tofree" is set to it, otherwise NULL. "numbuf" is used for a number.
//When "copyID" is not zero replace recursive lists and dicts with "...". When both "echo_style" 
//and "composite_val" are FALSE, put quotes around strings as "string()", otherwise does not put 
//quotes around strings, as ":echo" displays values.
//When "restore_copyID" is FALSE, repeated items in dictionaries and lists are replaced with "...".
//May return NULL.
CS
echo_string_core(
   Var   *tv,
   Byte   **tofree,
   Byte   *numbuf,
   int      copyID,
   int      echo_style,
   int      restore_copyID,
   int      composite_val
) {
   static int   recurse = 0;
   Byte   *r = NULL;

   if (recurse >= DICT_MAXNEST) {
      if (!did_echo_string_emsg) {
          //Only give this message once for a recursive call to avoid flooding the user with 
          //errors. And stop iterating over lists and dicts and objects.
          did_echo_string_emsg = TRUE;
          emsg(_(e_variable_nested_too_deep_for_displaying));
      }
      *tofree = NULL;
      return (CS)"{E724}";
   }
   ++recurse;

   switch (tv->tag) {
   case VAR_STRING:
      r = string_tv2string(tv, tofree, echo_style, composite_val);
      break;

   case VAR_FUNC:
      r = func_tv2string(tv, tofree, echo_style);
      break;

   case VAR_PARTIAL:
      if (tv->partial == NULL)
         r = partial_tv2string(tv, tofree, numbuf, copyID);
      else
         r = method_tv2string(tv, tofree, echo_style);
      break;

   case VAR_BLOB:
      r = blob2string(tv->blob, tofree, numbuf);
      break;

   case VAR_LIST:
      r = list_tv2string(tv, tofree, copyID, restore_copyID);
      break;

   case VAR_BAG:
      r = dict_tv2string(tv, tofree, copyID, restore_copyID);
      break;

   case VAR_NUMBER:
   case VAR_UNKNOWN:
   case VAR_ANY:
   case VAR_VOID:
      *tofree = NULL;
      r = tv_get_string_buf(tv, numbuf);
      break;

   case VAR_JOB:
   case VAR_CHANNEL:
      r = jobchan_tv2string(tv, OUT tofree, OUT numbuf, composite_val);
      break;

   case VAR_FLOAT:
      *tofree = NULL;
      eeSnprintf(numbuf, NUMBUFLEN, "%g", tv->floatt);
      r = numbuf;
      break;

   case VAR_BOOL:
   case VAR_SPECIAL:
       *tofree = NULL;
       r = (CS)get_var_special_name(tv->number);
       break;

   }

   if (--recurse == 0)
      did_echo_string_emsg = FALSE;
   return r;
}

//Return a string with the string representation of a variable.
//If the memory is allocated "tofree" is set to it, otherwise NULL. "numbuf" is used for a number.
//Does not put quotes around strings, as ":echo" displays values.
//When "copyID" is not zero replace recursive lists and dicts with "...". May return NULL.
CS
echo_string(
    Var   *tv,
    Byte   **tofree,
    Byte   *numbuf,
    int      copyID)
{
    return echo_string_core(tv, tofree, numbuf, copyID, TRUE, FALSE, FALSE);
}

//Convert the specified byte index of line 'lnum' in book 'book' to a character index. Works only 
//for loaded books. Return -1 on failure. The index of the first byte and the first character is 
//zero.
private int
buf_byteidx_to_charidx(Book *book, int lnum, int byteidx) {
   Byte   *t;
   int      count;

   if (!book || book->mem.mfile == NULL)
      return -1;

   if (lnum > book->mem.lineCount)
      lnum = book->mem.lineCount;

   CS str = memGetLine(book, lnum, FALSE);
   if (str == NULL)
      return -1;

   if (*str == ZERO)
      return 0;

   // count the number of characters
   t = str;
   for (count = 0; *t != ZERO && t <= str + byteidx; count++)
      t += utfCharLen(t);

   // In insert mode, when the cursor is at the end of a non-empty line,
   // byteidx points to the ZERO character immediately past the end of the
   // string. In this case, add one to the character count.
   if (*t == ZERO && byteidx != 0 && t == str + byteidx)
      count++;

   return count - 1;
}

//Translate a String variable into a position. Return NULL when there is an error.
Pos*
var2fpos(
   Var* varp,
   int dollar_lnum,   // TRUE when $ is last line
   int* fnum,      // set to fnum for '0, 'A, etc.
   int charcol)   // return character column
{
   static Pos   pos;
   Pos      *pp;

   // Argument can be [lnum, col, coladd].
   if (varp->tag == VAR_LIST) {

      List* l = varp->list;
      if (!l)
         return NULL;

      // Get the line number
      Boole error = false;
      pos.lnum = list_find_nr(l, 0L, &error);
      if (error || pos.lnum <= 0 || pos.lnum > curBook->mem.lineCount)
         return NULL;   // invalid line number
      long len;
      if (charcol)
         len = (long)mb_charlen(ml_get(pos.lnum));
      else
         len = (long)ml_get_len(pos.lnum);

      // Get the column number
      // We accept "$" for the column number: last column.
      ListItem* li = list_find(l, 1L);
      if (li && li->c.tag == VAR_STRING
         && li->c.string != NULL
         && STRCMP(li->c.string, "$") == 0)
      {
          pos.col = len + 1;
      } else {
         pos.col = list_find_nr(l, 1L, &error);
         if (error)
            return NULL;
      }

      // Accept a position up to the ZERO after the line.
      if (pos.col == 0 || (int)pos.col > len + 1)
          return NULL;   // invalid column number
      --pos.col;

      // Get the virtual offset.  Defaults to zero.
      pos.coladd = list_find_nr(l, 2L, &error);
      if (error)
          pos.coladd = 0;

      return &pos;
   }


   CS name = convertVarToStringSingleUse(varp);
   if (!name)
      return NULL;

   pos.lnum = 0;
   if (name[0] == '.') {
      // cursor
      pos = curPor->cursor;
   } ei (name[0] == 'v' && name[1] == ZERO) {
      // Visual start
      if (VIsual_active)
          pos = VIsual;
      else
          pos = curPor->cursor;
   } ei (name[0] == '\'') {
   // mark
   pp = getmark_buf_fnum(curBook, name[1], FALSE, fnum);
   if (pp == NULL || pp == (Pos *)-1 || pp->lnum <= 0)
       return NULL;
   pos = *pp;
    }
   if (pos.lnum != 0) {
      if (charcol)
         pos.col = buf_byteidx_to_charidx(curBook, pos.lnum, pos.col);
      return &pos;
   }

   pos.coladd = 0;

   if (name[0] == 'w' && dollar_lnum) {
      // the "cacheState" flags are not reset when moving the cursor, but they
      // do matter for update_topline() and validate_botline().
      check_cursor_moved(curPor);

      pos.col = 0;
      if (name[1] == '0') {     // "w0": first visible line
         update_topline();
         // In silent Ex mode topline is zero, but that's not a valid line
         // number; use one instead.
         pos.lnum = curPor->topLine > 0 ? curPor->topLine : 1;
         return &pos;
      } ei (name[1] == '$') {  // "w$": last visible line
         validate_botline();
         // In silent Ex mode botline is zero, return zero then.
         pos.lnum = curPor->bottomLine > 0 ? curPor->bottomLine - 1 : 0;
         return &pos;
      }
   } ei (name[0] == '$') {     // last column or line
      if (dollar_lnum) {
         pos.lnum = curBook->mem.lineCount;
         pos.col = 0;
      } else {
         pos.lnum = curPor->cursor.lnum;
         if (charcol)
            pos.col = (ColNr)mb_charlen(ml_get_curline());
         else
            pos.col = ml_get_curline_len();
      }
      return &pos;
   }
   return NULL;
}

//Convert list in "arg" into position "posp" and optional file number "fnump".
//When "fnump" is NULL there is no file number, only 3 items: [lnum, col, off]
//Note that the column is passed on as-is, the caller may want to decrement
//it to use 1 for the first column.
//If "charcol" is TRUE use the column as the character index instead of the byte index.
//Return FAIL when conversion is not possible, doesn't check the position for validity.
int
list2fpos(
   Var   *arg,
   Pos   *posp,
   int      *fnump,
   ColNr   *curswantp,
   int      charcol
) {
   List   *l = arg->list;
   long   i = 0;
   long   n;

   // List must be: [fnum, lnum, col, coladd, curswant], where "fnum" is only
   // there when "fnump" isn't NULL; "coladd" and "curswant" are optional.
   if (arg->tag != VAR_LIST
          || l == NULL
          || l->len < (fnump == NULL ? 2 : 3)
          || l->len > (fnump == NULL ? 4 : 5))
      return FAIL;

   if (fnump) {
      n = list_find_nr(l, i++, NULL);   // fnum
      if (n < 0)
         return FAIL;
      if (n == 0)
         n = curBook->fiNum;      // current book
      *fnump = n;
   }

   n = list_find_nr(l, i++, NULL);   // lnum
   if (n < 0)
      return FAIL;
   posp->lnum = n;

   n = list_find_nr(l, i++, NULL);   // col
   if (n < 0)
      return FAIL;
   // If character position is specified, then convert to byte position
   // If the line number is zero use the cursor line.
   if (charcol) {
      // Get the text for the specified line in a loaded book
      Book* book = bookFindFileByBookNr(fnump == NULL ? curBook->fiNum : *fnump);
      if (book == NULL || book->mem.mfile == NULL)
          return FAIL;

      n = bookCharidxToByteidx(book, posp->lnum == 0 ? curPor->cursor.lnum : posp->lnum, n) + 1;
   }
   posp->col = n;

   n = list_find_nr(l, i, NULL);   // off
   if (n < 0)
      posp->coladd = 0;
   else
      posp->coladd = n;

   if (curswantp)
      *curswantp = list_find_nr(l, i + 1, NULL);  // curswant

   return OK;
}

//Get the length of an environment variable name.
//Advance "arg" to the first character after the name. Return 0 for error.
int
readEnvNameAndGetItsLen(OUT CS* arg) {
   Byte   *p;

   for (p = *arg; eeIsIdentifierChar(*p); ++p)
      ;
   if (p == *arg)       // no name found
      return 0;

   int len = (int)(p - *arg);
   *arg = p;
   return len;
}

//Get the length of the name of a function or internal variable.
//"arg" is advanced to after the name. Return 0 if something is wrong.
int
get_id_len(OUT CS* arg) {
   Byte   *p;
   int      len;

   // Find the end of the name.
   for (p = *arg; isValidForScriptName(*p); ++p) {
      if (*p == ':') {
          // "s:" is start of "s:var", but "n:" is not and can be used in
          // slice "[n:]".  Also "xx:" is not a namespace.
          len = (int)(p - *arg);
          if ((len == 1 && firstOccurrence(NAMESPACE_CHAR, **arg) == NULL)
             || len > 1)
         break;
      }
   }
   if (p == *arg)       // no name found
      return 0;

   len = (int)(p - *arg);
   *arg = p;

   return len;
}

//Get the length of the name of a variable or function. Only the name is recognized, do not handle 
//".key" or "[idx]". "arg" is advanced to the first non-white character after the name.
//Return -1 if curly braces expansion failed. 0 if something else is wrong.
//If the name contains 'magic' {}'s, expand them and return the
//expanded name in an allocated string via 'alias' - caller must free.
int
get_name_len(Byte** arg, Byte** alias, int evaluate, int verbose) {
   *alias = NULL;  // default to no alias

   if ((*arg)[0] == K_SPECIAL && (*arg)[1] == KS_EXTRA && (*arg)[2] == (int)KE_SNR) {
      // hard coded <SNR>, already translated
      *arg += 3;
      return get_id_len(arg) + 3;
   }
   int scriptPrefixLen = scriptCheckScriptPrefix(*arg);
   if (scriptPrefixLen > 0) {
      // literal "<SID>", "s:" or "<SNR>"
      *arg += scriptPrefixLen;
   }

   // Find the end of the name; check for {} construction.
   Text braces;
   Text argSlice = mbText(*arg);
   Text name = findNameEnd(argSlice, OUT &braces, scriptPrefixLen > 0 ? 0 : FNE_CHECK_START);
   if (braces.len > 0) {
      if (!evaluate) {
         *arg = skipwhite(name.c + name.len);
         return name.len + scriptPrefixLen;
      }

      // Include any <SID> etc in the expanded string: Thus the -len here.
      Text temp_string = expandCurlyBraces(
         braces, (Text){name.c - scriptPrefixLen, name.len + scriptPrefixLen}
      );
      if (temp_string.len == 0)
         return -1;
      *alias = temp_string.c;
      *arg = skipwhite(name.c);
      return temp_string.len;
   }

   int len = scriptPrefixLen + get_id_len(OUT arg);
   // Only give an error when there is something, otherwise it will be reported at a higher level.
   if (len == 0 && verbose && **arg != ZERO)
      showErrFmtMsg(_(e_invalid_expression_str), *arg);

   return len;
}

//Find the end of a variable or function name, taking care of magic braces. If "expr_start" is not
//NULL then "expr_start" and "expr_end" are set to the start and end of the first curly braces item.
//"flags" can have FNE_INCL_BR and FNE_CHECK_START. Return a pointer to just after the name. Equal 
//to "arg" if there is no valid name.
Text
findNameEnd(Text const arg, OUT Text* expr, Unt flags) {
   int mb_nest = 0;
   int br_nest = 0;
   Boole allow_curly = true;

   if (expr && expr->len > 0) {
      expr->len = 0;
   }

   // Quick check for valid starting character.
   if ((flags & FNE_CHECK_START) != 0 
         && !isValidForScriptName1(arg.c[0]) 
         && (arg.c[0] != '{' || !allow_curly)
   )
      return arg;

   CS p = arg.c;
   CS sentinel = arg.c + arg.len;
   for (; 
        p < sentinel
          && (isValidForScriptName(*p)
            || (*p == '{' && allow_curly)
            || ((flags & FNE_INCL_BR) != 0 
                  && (*p == '[' || (*p == '.' && isValidForFirstCharDictKey(p[1]))))
            || mb_nest != 0
            || br_nest != 0); 
        MB_PTR_ADV(p)
   ) {
      if (*p == '\'') {
         // skip over 'string' to avoid counting [ and ] inside it.
         for (p = p + 1; p < sentinel && *p != '\''; MB_PTR_ADV(p))
            {}
         if (p == sentinel)
            break;
      } ei (*p == '"') {
         // skip over "str\"ing" to avoid counting [ and ] inside it.
         for (p = p + 1; p < sentinel && *p != '"'; MB_PTR_ADV(p)) {
            if (*p == '\\' && p[1] != ZERO)
               ++p;
         } 
         if (p == sentinel)
            break;
      } ei (br_nest == 0 && mb_nest == 0 && *p == ':') {
         //"s:" is start of "s:var", but "n:" is not and can be used in
         //slice "[n:]". Also "xx:" is not a namespace. But {ns}: is.
         int len = (int)(p - arg.c);
         if ((len == 1 && firstOccurrence(NAMESPACE_CHAR, arg.c[0]) == NULL) 
               || (len > 1 && p[-1] != '}')
         )
            break;
      }

      if (mb_nest == 0) {
         if (*p == '[')
            ++br_nest;
         ei (*p == ']')
            --br_nest;
      }

      if (br_nest == 0 && allow_curly) {
         if (*p == '{') {
            mb_nest++;
            if (expr && !expr->c)
               expr->c = p;
         } ei (*p == '}') {
            mb_nest--;
            if (expr && mb_nest == 0 && expr->len == 0)
               expr->len = p - expr->c;
         }
      }
   }

   return (Text) {arg.c, p - arg.c};
}

//Expand out the 'magic' {}'s in a variable/function name.
//Note that this can call itself recursively, to deal with constructs like foo{bar}{baz}{bam}
//The two slice parameters' layout:   "foo{expre}ss{ion}bar"
//                                     |  |     |         |
//                            "outer"  +------------------+
//                                        |     | 
//                           "braces"     +-----+ 
//
//Return a new allocated string, which the caller must free, unless there was nothing to expand.
private Text
expandCurlyBraces(Text braces, Text outer) {
   if (braces.len == 0 || outer.len == 0)
      return outer;
      
   Text retval = {};
   braces.c[0]   = ZERO;
   braces.c[braces.len] = ZERO;
   Byte c1 = outer.c[outer.len];
   outer.c[outer.len] = ZERO;

   CS braceEvalResult = eval_to_string(braces.c + 1, false, false);
   if (braceEvalResult) {
      retval.len = (Unt)(outer.len) + STRLEN(braceEvalResult) - braces.len + 1;

      retval.c = alloc(retval.len);
      eeSnprintf(
            retval.c, retval.len, "%s%s%s", outer.c, braceEvalResult, braces.c + braces.len + 1
      );
      eeglFree(braceEvalResult);
   }

   // restore all the butchered chars for error messages
   outer.c[outer.len] = c1;
   braces.c[0] = '{';
   braces.c[braces.len] = '}';

   if (retval.len > 0) {
      (void)findNameEnd(retval, OUT &braces, 0);
      if (braces.len > 0) {
         // Further expansion!
         Text secondEvalResult = expandCurlyBraces(braces, retval);
         eeglFree(retval.c);
         return secondEvalResult;
      }
   }
   return retval;
}

//}}}
//{{{subscripts

//Handle:
//- expr[expr], expr[expr:expr] subscript
//- ".name" lookup
//- function call with Funcref variable: func(expr)
//- method call: var->method()
//
//Can all be combined in any order: dict.func(expr)[idx]['func'](expr)->len()
//"name_start" points to a variable before the subscript or is NULL.
int
handle_subscript(
   OUT CS* arg,
   Var   *returnVar,
   EvalCtx   *evalarg,
   int      verbose)   // give error messages
{
   int      evaluate = evalarg && (evalarg->eval_flags & EVAL_EVALUATE);
   int      ret = OK;
   Bag   *selfdict = NULL;
   int      check_white = TRUE;
   Byte   *p;

   while (ret == OK) {
      // When at the end of the line and ".name" or "->{" or "->X" follows in
      // the next line then consume the line break.
      p = skipwhite(*arg);

      if ((**arg == '(' && (!evaluate || returnVar->tag == VAR_FUNC
                || returnVar->tag == VAR_PARTIAL))
             && (!check_white || !SPACE_OR_TAB(*(*arg - 1)))
      ){
         ret = call_func_returnVar(arg, evalarg, returnVar, evaluate, selfdict, NULL);

         // Stop the expression evaluation when immediately aborting on
         // error, or when an interrupt occurred or an exception was thrown but not caught.
         if (aborting()) {
            if (ret == OK)
               clearVar(returnVar);
            ret = FAIL;
         }
         bagUnref(selfdict);
         selfdict = NULL;
      } ei (p[0] == '-' && p[1] == '>') {
         *arg = p + 2;
         if (SPACE_OR_TAB(**arg)) {
            emsg(_(e_no_white_space_allowed_before_parenthesis));
            ret = FAIL;
         } ei ((**arg == '{') || **arg == '(')
            // expr->{lambda}() or expr->(lambda)()
            ret = eval_lambda(arg, returnVar, evalarg, verbose);
         else
            // expr->name()
            ret = eval_method(arg, returnVar, evalarg, verbose);
      }
      // "." is ".name" lookup when we found a dict. String concatenation is "..".
      ei (**arg == '['
         || (**arg == '.' && (returnVar->tag == VAR_BAG
            || (!evaluate && (*arg)[1] != '.')))
      ){
         bagUnref(selfdict);
         if (returnVar->tag == VAR_BAG) {
            selfdict = returnVar->bag;
            if (selfdict)
               ++selfdict->refcount;
         } else
            selfdict = NULL;
         if (eval_index(arg, returnVar, evalarg, verbose) == FAIL) {
            clearVar(returnVar);
            ret = FAIL;
         }
      } else
         break;
   }

   // Turn "dict.Func" into a partial for "Func" bound to "dict".
   // Don't do this when "Func" is already a partial that was bound explicitly (isAuto == FALSE)
   if (selfdict
          && (returnVar->tag == VAR_FUNC
            || (returnVar->tag == VAR_PARTIAL
                && (returnVar->partial->isAuto || returnVar->partial->self == NULL))))
      selfdict = make_partial(selfdict, returnVar);

   bagUnref(selfdict);
   return ret;
}

//Make a copy of an item. Lists and Dictionaries are also copied.  A deep copy if "deep" is set.
//"top" is TRUE for the toplevel of copy(). For deepcopy() "copyID" is zero for a full copy or the 
//ID for when a reference to an already copied list/dict can be used. Return FAIL or OK.
int
item_copy(
   Var   *from,
   Var   *to,
   int      deep,
   int      top,
   int      copyID)
{
   static int   recurse = 0;
   int      ret = OK;

   if (recurse >= DICT_MAXNEST) {
      emsg(_(e_variable_nested_too_deep_for_making_copy));
      return FAIL;
   }
   ++recurse;

   switch (from->tag) {
   case VAR_NUMBER:
   case VAR_FLOAT:
   case VAR_STRING:
   case VAR_FUNC:
   case VAR_PARTIAL:
   case VAR_BOOL:
   case VAR_SPECIAL:
   case VAR_JOB:
   case VAR_CHANNEL:
   case VAR_LIST:
      to->tag = VAR_LIST;
      to->lock = 0;
      if (from->list == NULL)
         to->list = NULL;
      ei (copyID != 0 && from->list->copyId == copyID) {
         // use the copy made earlier
         to->list = from->list->copyList;
         ++to->list->refcount;
      } else
         to->list = list_copy(from->list, deep, top, copyID);
      if (to->list == NULL)
         ret = FAIL;
      break;
   case VAR_BLOB:
      ret = blob_copy(from->blob, to);
      break;
   case VAR_BAG:
      to->tag = VAR_BAG;
      to->lock = 0;
      if (from->bag == NULL)
         to->bag = NULL;
      ei (copyID != 0 && from->bag->copyId == copyID) {
         // use the copy made earlier
         to->bag = from->bag->dv_copydict;
         ++to->bag->refcount;
      } else
         to->bag = dict_copy(from->bag, deep, top, copyID);
      if (to->bag == NULL)
         ret = FAIL;
      break;
   case VAR_UNKNOWN:
   case VAR_ANY:
   case VAR_VOID:
      internal_error_no_abort(S"item_copy(UNKNOWN)");
      ret = FAIL;
   }
   --recurse;
   return ret;
}

//}}}
//{{{built-in functions

//Fill the buffer 'buf' with 'len' random bytes.
//Return FAIL if the OS PRNG is not available or something went wrong.
int
mch_get_random(OUT CS buf, int len) {
   static int dev_urandom_state = NOTDONE;

   if (dev_urandom_state == FAIL)
      return FAIL;

   int fd = open("/dev/urandom", O_RDONLY);

   // Attempt reading /dev/urandom.
   if (fd == -1)
      dev_urandom_state = FAIL;
   ei (read(fd, buf, len) == len) {
      dev_urandom_state = OK;
      close(fd);
   } else {
      dev_urandom_state = FAIL;
      close(fd);
   }

   return dev_urandom_state;
}


void
echo_one(Var *returnVar, int with_space, int *atstart, int *needclr) {
   Byte   *tofree;
   Byte   numbuf[NUMBUFLEN];
   CS p = echo_string(returnVar, &tofree, numbuf, get_copyID());

   if (*atstart) {
      *atstart = FALSE;
      // Call msg_start() after eval1(), evaluating the expression
      // may cause a message to appear.
      if (with_space) {
         // Mark the saved text as finishing the line, so that what
         // follows is displayed on a new line when scrolling back at the more prompt.
         msg_sb_eol();
         msg_start();
      }
   } ei (with_space)
      msgPutsDeco(S" ", echoDecoFlagsG);

   if (p) {
      for ( ; *p != ZERO && !gotInterruptG; ++p) {
         if (*p == '\n' || *p == '\r' || *p == TAB) {
            if (*p != TAB && *needclr) {
               // remove any text still there from the command
               msg_clr_eos();
               *needclr = FALSE;
            }
            msgPutcharDeco(*p, echoDecoFlagsG);
         } else {
            int i = utfCharLen(p);
            (void)msgOuttransLenDeco((Text){p, i}, echoDecoFlagsG);
            p += i - 1;
         } 
      }
   } 
   eeglFree(tofree);
}

// ":echo expr1 ..."   print each argument separated with a space, add a newline at the end.
// ":echon expr1 ..."   print each argument plain.
void
c_echo(Invocation *invo) {
   Byte   *arg = invo->arg;
   Var   returnVar;
   Byte   *arg_start;
   int      needclr = TRUE;
   int      atstart = TRUE;
   int      anyEmsgG_before = anyEmsgG;
   int      called_emsg_before = called_emsg;
   EvalCtx   evalarg;

   fillEvalArgFromInvo(OUT &evalarg, invo, invo->skip);

   if (invo->skip)
      ++emsg_skip;
   while ((!endsComm(arg) || isComment(arg)) && !gotInterruptG) {
      // If eval1() causes an error message the text from the command may
      // still need to be cleared. E.g., "echo 22,44".
      need_clr_eos = needclr;

      arg_start = arg;
      if (eval1(OUT &arg, &returnVar, &evalarg) == FAIL) {
         //Report the invalid expression unless the expression evaluation
         //has been cancelled due to an aborting error, an interrupt, or an exception.
         if (!aborting() && anyEmsgG == anyEmsgG_before && called_emsg == called_emsg_before)
            showErrFmtMsg(_(e_invalid_expression_str), arg_start);
         need_clr_eos = FALSE;
         break;
      }
      need_clr_eos = FALSE;

      if (!invo->skip) {
         if (returnVar.tag == VAR_VOID) {
            showErrFmtMsg(_(e_expression_does_not_result_in_value_str), arg_start);
            break;
         }
         echo_one(&returnVar, invo->id == C_echo, &atstart, &needclr);
      }

      clearVar(&returnVar);
      arg = skipwhite(arg);
   }
   set_nextcmd(OUT invo, arg);
   clear_evalarg(&evalarg, invo);

   if (invo->skip)
      --emsg_skip;
   else {
      // remove text that may still be there from the command
      if (needclr)
         msg_clr_eos();
      if (invo->id == C_echo)
         msg_end();
   }
}

// ":echohl {name}".
void
c_echohl(Invocation *invo) {
   echoDecoFlagsG = decosByHiliteName(invo->arg).flags;
}

// Return the :echo attribute
int
get_echo_attr(void) {
   return echoDecoFlagsG;
}

//":execute expr1 ..."   execute the result of an expression.
//":echomsg expr1 ..."   Print a message
//":echowindow expr1 ..." Print a message in the messages window
//":echoerr expr1 ..."   Print an error
//":echoconsole expr1 ..." Print a message on stdout
//Each gets spaces around each argument and a newline at the end for echo commands
void
c_execute(Invocation *invo) {
   Arr(Byte) arg = invo->arg;
   Var   returnVar;
   int      ret = OK;
   Byte   *p;
   ArrayList   ga;
   int      len;
   long   start_lnum = SOURCING_LNUM;

   ga_init2(&ga, 1, 80);

   if (invo->skip)
      ++emsg_skip;
   while (!endsComm(arg) || isComment(arg)) {
      ret = eval1_emsg(&arg, &returnVar, invo);
      if (ret == FAIL)
         break;

      if (!invo->skip) {
         Byte buf[NUMBUFLEN];

         if (invo->id == C_execute) {
            if (returnVar.tag == VAR_CHANNEL || returnVar.tag == VAR_JOB) {
               showErrFmtMsg(_(e_using_invalid_value_as_string_str), vartype_name(returnVar.tag));
               p = NULL;
            } else
               p = tv_get_string_buf(&returnVar, buf);
         } else
            p = tv_stringify(&returnVar, buf);
         if (!p) {
            clearVar(&returnVar);
            ret = FAIL;
            break;
         }
         len = (int)STRLEN(p);
         if (ga_grow(&ga, len + 2) == FAIL) {
            clearVar(&returnVar);
            ret = FAIL;
            break;
         }
         if (ga.len)
            ((CS)(ga.c))[ga.len++] = ' ';
         STRCPY((CS)(ga.c) + ga.len, p);
         ga.len += len;
      }

      clearVar(&returnVar);
      arg = skipwhite(arg);
   }

   if (ret != FAIL && ga.c) {
      // use the first line of continuation lines for messages
      SOURCING_LNUM = start_lnum;

      if (invo->id == C_echomsg
            || invo->id == C_echowindow
            || invo->id == C_echoerr
      ) {
          //Mark the already saved text as finishing the line, so that what
          //follows is displayed on a new line when scrolling back at the more prompt.
          msg_sb_eol();
      }

      if (invo->id == C_echomsg) {
         msgDeco(ga.c, echoDecoFlagsG);
         out_flush();
      } ei (invo->id == C_echowindow) {
         start_echowindow(invo->addr_count > 0 ? invo->line2 : 0);
         msgDeco(ga.c, echoDecoFlagsG);
         end_echowindow();
      } ei (invo->id == C_echoconsole) {
         ui_write(ga.c, (int)STRLEN(ga.c), TRUE);
         ui_write((CS)"\r\n", 2, TRUE);
      } ei (invo->id == C_echoerr) {
         int save_anyEmsgG = anyEmsgG;

         // We don't want to abort following commands, restore anyEmsgG.
         emsg(ga.c);
         if (!force_abort)
            anyEmsgG = save_anyEmsgG;
      } ei (invo->id == C_execute) {
         doCommand((CS)ga.c, invo->ea_getline, invo->cookie, DOCMD_NOWAIT|DOCMD_VERBOSE);
      }
   }

   ga_clear(&ga);

   if (invo->skip)
      --emsg_skip;
   set_nextcmd(OUT invo, arg);
}

//}}}
//{{{aux2

//Skip over the name of an option, i.e. "&option", "&g:option" or "&l:option".
//"arg" points to the "&" or '+' when called, to "option" when returning.
//Return NULL when no option name found. Otherwise pointer to the char after the option name.
CS
find_option_end(OUT CS* arg, OUT int *scope) {
   Byte   *p = *arg;

   ++p;
   if (*p == 'g' && p[1] == ':') {
      *scope = OPT_GLOBAL;
      p += 2;
   } ei (*p == 'l' && p[1] == ':') {
      *scope = OPT_LOCAL;
      p += 2;
   } else
      *scope = 0;

   if (!ASCII_ISALPHA(*p))
      return NULL;
   *arg = p;

   if (p[0] == 't' && p[1] == '_' && p[2] != ZERO && p[3] != ZERO)
      p += 4;       // termcap option
   else {
      while (ASCII_ISALPHA(*p))
         ++p;
   } 
   return p;
}

//Display script name where an item was last set.
//Should only be invoked when 'verbose' is non-zero.
void
lastSetMsg(ScriptPos script_ctx) {
   if (script_ctx.sid == 0)
      return;

   CS p = home_replace_save(NULL, get_scriptname(script_ctx.sid));
   if (!p)
      return;

   verbose_enter();
   msg_puts(_("\n\tLast set from "));
   msg_puts(p);
   if (script_ctx.lineNr > 0) {
      msg_puts(_(line_msg));
      msg_outnum((long)script_ctx.lineNr);
   }
   verbose_leave();
   eeglFree(p);
}


//Perform a substitution on "str" with pattern "pat" and substitute "sub".
//When "sub" is NULL "expr" is used, must be a VAR_FUNC or VAR_PARTIAL.
//"flags" can be "g" to do a global substitute. Return an allocated string, NULL for error.
CS
do_string_sub(
   CS str,
   Unt   len,
   Byte   *pat,
   Byte   *sub,
   Var   *expr,
   Byte   *flags,
   Unt   *ret_len)      // length of returned buffer
{
   RegMatch   regmatch;
   ArrayList   ga;
   Byte   *ret;

   ga_init2(&ga, 1, 200);

   regmatch.rm_ic = p_ic;
   regmatch.regprog = compileRegexp(pat, RE_MAGIC + RE_STRING);
   if (regmatch.regprog) {
      Byte   *tail = str;
      Byte   *end = str + len;
      int   do_all = (flags[0] == 'g');
      int   sublen;
      int   i;
      Byte   *zero_width = NULL;

      while (eeRegexec_nl(&regmatch, str, (ColNr)(tail - str))) {
         // Skip empty match except for first match.
         if (regmatch.startp[0] == regmatch.endp[0]) {
            if (zero_width == regmatch.startp[0]) {
               // avoid getting stuck on a match with an empty string
               i = utfCharLen(tail);
               mch_memmove((CS)ga.c + ga.len, tail, (Unt)i);
               ga.len += i;
               tail += i;
               continue;
            }
            zero_width = regmatch.startp[0];
         }

         //Get some space for a temporary buffer to do the substitution into.  It will contain:
         //- The text up to where the match is.
         //- The substituted text.
         //- The text after the match.
         sublen = eeRegsub(&regmatch, sub, expr, tail, 0, REGSUB_MAGIC);
         if (sublen <= 0) {
            ga_clear(&ga);
            break;
         }
         if (ga_grow(&ga, (int)((end - tail) + sublen -
                   (regmatch.endp[0] - regmatch.startp[0]))) == FAIL) {
            ga_clear(&ga);
            break;
         }

         // copy the text up to where the match is
         i = (int)(regmatch.startp[0] - tail);
         mch_memmove((CS)ga.c + ga.len, tail, (Unt)i);
         // add the substituted text
         (void)eeRegsub(
            &regmatch, sub, expr, (CS)ga.c + ga.len + i, sublen, REGSUB_COPY | REGSUB_MAGIC
         );
         ga.len += i + sublen - 1;
         tail = regmatch.endp[0];
         if (*tail == ZERO)
            break;
         if (!do_all)
            break;
      }

      if (ga.c) {
         STRCPY((char *)ga.c + ga.len, tail);
         ga.len += (int)(end - tail);
      }

      eeRegFree(regmatch.regprog);
    }

   if (ga.c) {
      str = (CS)ga.c;
      len = (Unt)ga.len;
    }
    ret = copySubstr(str, len);
    ga_clear(&ga);

   if (ret_len)
      *ret_len = len;

   return ret;
}

//To be called after eval_next_non_blank() sets "getnext" to TRUE.
//
//If "arg" is not NULL, then the caller should assign the return value to "arg".
private CS
eval_next_line(CS arg, EvalCtx *evalarg) {
   ArrayList* gap = &evalarg->eval_ga;
   CS line;

   if (arg) {
      if (*arg == NL)
          return newline_skip_comments(arg);
      // Truncate before a trailing comment, so that concatenating the lines
      // won't turn the rest into a comment.
      CS q = skipwhite(arg);
      if (*q == '#' || (*q == '/' && q[1] == '/'))
         *arg = ZERO;
    }

   if (evalarg->eval_cookie)
      line = evalarg->eval_getline(0, evalarg->eval_cookie, 0, GETLINE_CONCAT_ALL);
   if (line == NULL)
      return NULL;

   ++evalarg->eval_break_count;
   if (gap->ga_itemsize > 0 && ga_grow(gap, 1) == OK) {
      CS p = skipwhite(line);

      //Going to concatenate the lines after parsing. For an empty or
      //comment line use an empty string.
      if (*p == ZERO || isComment(p)) {
         eeglFree(line);
         line = copyStr(E);
      }

      ((Byte **)gap->c)[gap->len] = line;
      ++gap->len;
   } ei (evalarg->eval_cookie) {
      free_eval_tofree_later(evalarg);
      evalarg->eval_tofree = line;
   }

   //Advanced to the next line, "arg" no longer points into the previous line. The caller assigns 
   //the return value to "arg". If "arg" is NULL, then the return value is discarded. In that 
   //case, "arg" still points to the previous line. So don't reset "eval_using_cmdline".
   if (arg)
      evalarg->eval_using_cmdline = FALSE;
   return skipwhite(line);
}


//For garbage collecting: set references in all variables referenced by all loopvars.
int
set_ref_in_loopvars(int copyID) {
   for (LoopVars* loopvars = firstLoopvarS; loopvars; loopvars = loopvars->lvs_next) {
      Var* stack = loopvars->lvs_ga.c;

      for (int i = 0; i < loopvars->lvs_ga.len; ++i) {
         if (set_ref_in_item(stack + i, copyID, NULL, NULL))
            return TRUE;  // abort
      } 
   }
   return FALSE;
}

//}}}
//{{{eval loops

//Used when looping over a :for line, skip the "in expr" part.
void
skipForLines(void *fi_void, EvalCtx *evalarg) {
   ForInfo* fi = (ForInfo *)fi_void;
   int      i;

   for (i = 0; i < fi->fi_break_count; ++i)
      eval_next_line(NULL, evalarg);
}

//}}}
//{{{eval variables: functions for dealing with variables

private DictItem   globvars_var;      // variable used for g:
private Bag      globvardict;      // Dictionary with g: variables
#define globvarht globvardict.hashTable

//Array to hold the value of v: variables.
//The value is in a dictitem, so that it can also be used in the v: scope.
//The reason to use this table anyway is for very quick access to the
//variables with the VV_ defines.

//values for flags:
#define VV_RO       2   // read-only

#define VV_NAME(s, t)  (CS)s, {{t, 0, {0}}, 0, 0, {0}}


typedef struct  {
   CS name;     //name of variable, without v:
   DictItem16 entry; //value and name for key (max 16 chars!)
   Boole isReadonly;
} EeglVar;

private EeglVar eeglVars[EV_LEN] = {
   // The order here must match the VV_ defines in eegl.h!
   // Initializing a union does not work, leave tv.c empty to get zero's.
   {VV_NAME("count",        VAR_NUMBER), true},
   {VV_NAME("count1",       VAR_NUMBER), true},
   {VV_NAME("prevcount",    VAR_NUMBER), true},
   {VV_NAME("errmsg",       VAR_STRING), false},
   {VV_NAME("warningmsg",    VAR_STRING), false},
   {VV_NAME("statusmsg",    VAR_STRING), false},
   {VV_NAME("shell_error",    VAR_NUMBER), true},
   {VV_NAME("this_session",    VAR_STRING), false},
   {VV_NAME("version",       VAR_NUMBER), true},
   {VV_NAME("lnum",       VAR_NUMBER),  false},
   {VV_NAME("termresponse",    VAR_STRING),  true},
   {VV_NAME("fname",       VAR_STRING),  true},
   {VV_NAME("lang",       VAR_STRING),  true},
   {VV_NAME("lc_time",       VAR_STRING),  true},
   {VV_NAME("ctype",       VAR_STRING),  true},
   {VV_NAME("fname_in",    VAR_STRING),  true},
   {VV_NAME("fname_out",    VAR_STRING),  true},
   {VV_NAME("fname_new",    VAR_STRING),  true},
   {VV_NAME("fname_diff",    VAR_STRING), true},
   {VV_NAME("cmdarg",       VAR_STRING),  true},
   
   {VV_NAME("foldstart",    VAR_NUMBER),  false},
   {VV_NAME("foldend",       VAR_NUMBER),  false},
   {VV_NAME("folddashes",    VAR_STRING),  false},
   {VV_NAME("foldlevel",    VAR_NUMBER),  false},
   {VV_NAME("progname",    VAR_STRING),  true},
   {VV_NAME("servername",    VAR_STRING),  true},
   {VV_NAME("dying",       VAR_NUMBER),  true},
   {VV_NAME("exception",    VAR_STRING),  true},
   {VV_NAME("throwpoint",    VAR_STRING),  true},
   {VV_NAME("register",    VAR_STRING),  true},
   {VV_NAME("cmdbang",       VAR_NUMBER),  true},
   {VV_NAME("insertmode",    VAR_STRING),  true},
   {VV_NAME("val",       VAR_UNKNOWN),  true},
   {VV_NAME("key",       VAR_UNKNOWN),  true},
   {VV_NAME("profiling",    VAR_NUMBER),  true},
   {VV_NAME("fcs_reason",    VAR_STRING),  true},
   {VV_NAME("fcs_choice",    VAR_STRING),  false},
   {VV_NAME("beval_bufnr",    VAR_NUMBER),  true},
   {VV_NAME("beval_winnr",    VAR_NUMBER),  true},
   {VV_NAME("beval_winid",    VAR_NUMBER),  true},
   
   {VV_NAME("beval_lnum",    VAR_NUMBER),  true},
   {VV_NAME("beval_col",    VAR_NUMBER),  true},
   {VV_NAME("beval_text",    VAR_STRING),  true},
   {VV_NAME("scrollstart",    VAR_STRING),  false},
   {VV_NAME("swapname",    VAR_STRING),  true},
   {VV_NAME("swapchoice",    VAR_STRING),  false},
   {VV_NAME("swapcommand",    VAR_STRING),  true},
   {VV_NAME("char",       VAR_STRING),  false},
   {VV_NAME("mouse_win",    VAR_NUMBER),  false},
   {VV_NAME("mouse_winid",    VAR_NUMBER),  false},
   {VV_NAME("mouse_lnum",    VAR_NUMBER),  false},
   {VV_NAME("mouse_col",    VAR_NUMBER),  false},
   {VV_NAME("operator",    VAR_STRING),  true},
   {VV_NAME("searchforward",    VAR_NUMBER),  false},
   {VV_NAME("hlsearch",    VAR_NUMBER),  false},
   {VV_NAME("oldfiles",    VAR_LIST),  false},
   {VV_NAME("windowid",    VAR_NUMBER),  true},
   {VV_NAME("progpath",    VAR_STRING),  true},
   {VV_NAME("completed_item",    VAR_BAG),  false},
   {VV_NAME("option_new",    VAR_STRING),  true},
   
   {VV_NAME("option_old",    VAR_STRING),  true},
   {VV_NAME("option_oldlocal",    VAR_STRING),  true},
   {VV_NAME("option_oldglobal", VAR_STRING),  true},
   {VV_NAME("option_command",    VAR_STRING),  true},
   {VV_NAME("option_type",    VAR_STRING),  true},
   {VV_NAME("errors",       VAR_LIST),  false},
   {VV_NAME("false",       VAR_BOOL),  true},
   {VV_NAME("true",       VAR_BOOL),  true},
   {VV_NAME("none",       VAR_SPECIAL),  true},
   {VV_NAME("null",       VAR_SPECIAL),  true},
   {VV_NAME("numbermax",    VAR_NUMBER),  true},
   {VV_NAME("numbermin",    VAR_NUMBER),  true},
   {VV_NAME("numbersize",    VAR_NUMBER),  true},
   {VV_NAME("eegl_did_enter",    VAR_NUMBER),  true},
   {VV_NAME("testing",       VAR_NUMBER),  false},
   {VV_NAME("t_number",    VAR_NUMBER),  true},
   {VV_NAME("t_string",    VAR_NUMBER),  true},
   {VV_NAME("t_func",       VAR_NUMBER),  true},
   {VV_NAME("t_list",       VAR_NUMBER),  true},
   {VV_NAME("t_dict",       VAR_NUMBER),  true},
   
   {VV_NAME("t_float",       VAR_NUMBER),  true},
   {VV_NAME("t_bool",       VAR_NUMBER),  true},
   {VV_NAME("t_none",       VAR_NUMBER),  true},
   {VV_NAME("t_job",       VAR_NUMBER),  true},
   {VV_NAME("t_channel",    VAR_NUMBER),  true},
   {VV_NAME("t_blob",       VAR_NUMBER),  true},
   {VV_NAME("termrfgresp",    VAR_STRING),  true},
   {VV_NAME("termrbgresp",    VAR_STRING),  true},
   {VV_NAME("termu7resp",    VAR_STRING),  true},
   {VV_NAME("termstyleresp",    VAR_STRING),  true},
   {VV_NAME("termblinkresp",    VAR_STRING),  true},
   {VV_NAME("event",       VAR_BAG),  true},
   {VV_NAME("versionlong",    VAR_NUMBER),  true},
   {VV_NAME("echospace",    VAR_NUMBER),  true},
   {VV_NAME("argv",       VAR_LIST),  true},
   {VV_NAME("collate",       VAR_STRING),  true},
   {VV_NAME("exiting",       VAR_SPECIAL),  true},
   {VV_NAME("colornames",       VAR_BAG),  true},
   {VV_NAME("sizeofint",    VAR_NUMBER),  true},
   {VV_NAME("sizeoflong",    VAR_NUMBER),  true},
   
   {VV_NAME("sizeofpointer",    VAR_NUMBER),  true},
   {VV_NAME("maxcol",       VAR_NUMBER),  true},
   {VV_NAME("t_enum",       VAR_NUMBER),  true},
   {VV_NAME("t_enumvalue",    VAR_NUMBER), true},
   {VV_NAME("stacktrace",    VAR_LIST), true},
   {VV_NAME("t_tuple",       VAR_NUMBER), true},
   {VV_NAME("wayland_display",  VAR_STRING),  true},
};

// Type values for type().
#define VAR_TYPE_NUMBER     0
#define VAR_TYPE_STRING     1
#define VAR_TYPE_FUNC       2
#define VAR_TYPE_LIST       3
#define VAR_TYPE_DICT       4
#define VAR_TYPE_FLOAT      5
#define VAR_TYPE_BOOL       6
#define VAR_TYPE_NONE       7
#define VAR_TYPE_JOB        8
#define VAR_TYPE_CHANNEL    9
#define VAR_TYPE_BLOB      10
#define VAR_TYPE_TYPEALIAS 11
#define VAR_TYPE_ENUM      12
#define VAR_TYPE_ENUMVALUE 13

private DictItem   currEeglVarS;      // variable used for v:
private Bag      eeglVarsS;      // Dictionary with v: variables
#define eeglVarsHt  eeglVarsS.hashTable

private void list_globVars(int *first);
private void list_buf_vars(int *first);
private void list_win_vars(int *first);
private void list_tabVars(int *first);
private CS list_arg_vars(Invocation *invo, Byte *arg, int *first);
private CS letOne(
     CS arg, Var *tv, Boole copy, Unt flags, CS endchars, CS op
);
private int do_lock_var(
      Lval* lp, CommIndex commandId, CS nameEnd, Boole forceIt UNUSED, int deep
);

private void list_one_var(DictItem *v, CS prefix, int *first);
private void list_one_var_a(CS prefix, CS name, int type, CS string, int *first);

// Initialize global and Eegl-special variables
private void
initGlobalAndSpecialVars(void) {
   int i;
   EeglVar* p;

   init_var_dict(&globvardict, &globvars_var, VAR_DEF_SCOPE);
   
   init_var_dict(&eeglVarsS, &currEeglVarS, VAR_SCOPE);
   eeglVarsS.lock = VAR_FIXED;

   for (i = 0; i < EV_LEN; ++i) {
      p = &eeglVars[i];
      int nameLen = STRLEN(p->name);
      if (nameLen > DICTITEM16_KEY_LEN) {
         internalErrMsg(S"Name too long, increase size of dictitem16_T");
         exitEegl(1);
      }
      STRCPY(p->entry.key, p->name);
      p->entry.len = nameLen;
      if (p->isReadonly)
          p->entry.flags = DI_FLAGS_RO | DI_FLAGS_FIX;
      else
          p->entry.flags = DI_FLAGS_FIX;

      // add to v: scope dict, unless the value is not always available
      if (p->entry.c.tag != VAR_UNKNOWN)
          hash_add(&eeglVarsHt, (Text){p->entry.key, nameLen}, S"initialization");
   }
   set_EeglVar_nr(VV_VERSION, EEGL_VERSION_100);
   set_EeglVar_nr(VV_VERSIONLONG, EEGL_VERSION_100 * 10000 + highest_patch());

   set_EeglVar_nr(VV_SEARCHFORWARD, 1L);
   set_EeglVar_nr(VV_HLSEARCH, 1L);
   set_EeglVar_nr(VV_EXITING, VVAL_NULL);
   set_EeglVar_dict(VV_COMPLETED_ITEM, allocBag_lock(VAR_FIXED));
   set_EeglVar_list(VV_ERRORS, list_alloc());
   set_EeglVar_dict(VV_EVENT, allocBag_lock(VAR_FIXED));

   set_EeglVar_nr(VV_FALSE, VVAL_FALSE);
   set_EeglVar_nr(VV_TRUE, VVAL_TRUE);
   set_EeglVar_nr(VV_NONE, VVAL_NONE);
   set_EeglVar_nr(VV_NULL, VVAL_NULL);
   set_EeglVar_nr(VV_NUMBERMAX, VARNUM_MAX);
   set_EeglVar_nr(VV_NUMBERMIN, VARNUM_MIN);
   set_EeglVar_nr(VV_NUMBERSIZE, sizeof(Long) * 8);
   set_EeglVar_nr(VV_SIZEOFINT, sizeof(int));
   set_EeglVar_nr(VV_SIZEOFLONG, sizeof(long));
   set_EeglVar_nr(VV_SIZEOFPOINTER, sizeof(char *));
   set_EeglVar_nr(VV_MAXCOL, MAXCOL);

   set_EeglVar_nr(VV_TYPE_NUMBER,  VAR_TYPE_NUMBER);
   set_EeglVar_nr(VV_TYPE_STRING,  VAR_TYPE_STRING);
   set_EeglVar_nr(VV_TYPE_FUNC,    VAR_TYPE_FUNC);
   set_EeglVar_nr(VV_TYPE_LIST,    VAR_TYPE_LIST);
   set_EeglVar_nr(VV_TYPE_DICT,    VAR_TYPE_DICT);
   set_EeglVar_nr(VV_TYPE_FLOAT,   VAR_TYPE_FLOAT);
   set_EeglVar_nr(VV_TYPE_BOOL,    VAR_TYPE_BOOL);
   set_EeglVar_nr(VV_TYPE_NONE,    VAR_TYPE_NONE);
   set_EeglVar_nr(VV_TYPE_JOB,     VAR_TYPE_JOB);
   set_EeglVar_nr(VV_TYPE_CHANNEL, VAR_TYPE_CHANNEL);
   set_EeglVar_nr(VV_TYPE_BLOB,    VAR_TYPE_BLOB);
   set_EeglVar_nr(VV_TYPE_ENUM,    VAR_TYPE_ENUM);
   set_EeglVar_nr(VV_TYPE_ENUMVALUE,  VAR_TYPE_ENUMVALUE);

   set_EeglVar_nr(VV_ECHOSPACE,    sc_col - 1);

   set_EeglVar_dict(VV_COLORNAMES, allocBag());

   // Default for v:register is not 0 but '"'.  This is adjusted once the
   // clipboard has been setup by calling reset_reg_var().
   set_reg_var(0);
}

#if defined(EXITFREE) || defined(PROTO)
// Free all Eegl variables information on exit
void
evalvars_clear(void) {
   for (int i = 0; i < EV_LEN; ++i) {
      EeglVar* p = &eeglVars[i];
      if (p->entry.type == VAR_STRING)
         EE_CLEAR(p->entry.c.string);
      ei (p->entry.type == VAR_LIST) {
         list_unref(p->vList);
         p->vList = NULL;
      }
   }
   hash_clear(&eeglVarsHt);
   hash_init(&eeglVarsHt);  // garbage_collect() will access it

   //global variables
   vars_clear(&globvarht);

   //Script-local variables. Clear all the variables here.
   //The ScriptVar is cleared later in free_scriptnames(), because a
   //variable in one script might hold a reference to the whole scope of another script.
   for (int i = 1; i <= script_items.len; ++i)
      vars_clear(&SCRIPT_VARS(i));
}
#endif

int
garbage_collect_globvars(int copyID) {
   return setRefInSet(&globvarht, copyID, NULL);
}

int
garbageCollectEeglVars(int copyID) {
   return setRefInSet(&eeglVarsHt, copyID, NULL);
}

int
garbage_collect_scriptvars(int copyID) {
   int abort = FALSE;
   for (Unt i = 1; i <= (Unt)script_items.len; ++i) {
      abort = abort || setRefInSet(&SCRIPT_VARS(i), copyID, NULL);

      ScriptItem* si = SCRIPT_ITEM(i);
      for (int idx = 0; idx < si->sn_var_vals.len; ++idx) {
         Svar    *sv = ((Svar *)si->sn_var_vals.c) + idx;
         if (sv->sv_name)
            abort = abort || set_ref_in_item(sv->sv_tv, copyID, NULL, NULL);
      }
   }

   return abort;
}

//Set an internal variable to a string value. Creates the variable if it does not already exist.
void
set_internal_string_var(CS name, CS value) {
   if (!name)
      return;
   CS val = copyStr(value);

   Var* var = allocStringVar(val);
   set_var(text(name), var, false);
   freeVar(var);
}

void
eval_diff(CS origfile, CS newfile, CS outfile){
   ScriptPos   saved_sctx = scriptPosG;

   set_EeglVar_string(VV_FNAME_IN, origfile, -1);
   set_EeglVar_string(VV_FNAME_NEW, newfile, -1);
   set_EeglVar_string(VV_FNAME_OUT, outfile, -1);

   ScriptPos* ctx = optGetScriptPos(S"diffexpr");
   if (ctx)
      scriptPosG = *ctx;

   // errors are ignored
   Var* var = evalExprInternal(p_dex, NULL, TRUE);
   freeVar(var);

   set_EeglVar_string(VV_FNAME_IN, NULL, -1);
   set_EeglVar_string(VV_FNAME_NEW, NULL, -1);
   set_EeglVar_string(VV_FNAME_OUT, NULL, -1);
   scriptPosG = saved_sctx;
}

void
eval_patch(CS origfile, CS diffFile, CS outfile) {
   ScriptPos saved_sctx = scriptPosG;

   set_EeglVar_string(VV_FNAME_IN, origfile, -1);
   set_EeglVar_string(VV_FNAME_DIFF, diffFile, -1);
   set_EeglVar_string(VV_FNAME_OUT, outfile, -1);

   ScriptPos* ctx = optGetScriptPos(S"patchexpr");
   if (ctx)
      scriptPosG = *ctx;

   // errors are ignored
   Var* tv = evalExprInternal(p_pex, NULL, TRUE);
   freeVar(tv);

   set_EeglVar_string(VV_FNAME_IN, NULL, -1);
   set_EeglVar_string(VV_FNAME_DIFF, NULL, -1);
   set_EeglVar_string(VV_FNAME_OUT, NULL, -1);
   scriptPosG = saved_sctx;
}

// Evaluate an expression to a list with suggestions.
// For the "expr:" part of 'spellsuggest'. Return NULL when there is an error.
List *
eval_spell_expr(CS badword, CS expr) {
   Var   save_val;
   Var   returnVar;
   List   *list = NULL;
   Byte   *p = skipwhite(expr);
   ScriptPos   saved_sctx = scriptPosG;
   ScriptPos   *ctx;
   int      r;

   // Set "v:val" to the bad word.
   prepareEeglVar(VV_VAL, OUT &save_val);
   set_EeglVar_string(VV_VAL, badword, -1);
   if (p_verbose == 0)
      ++emsg_off;
   ctx = optGetScriptPos(S"spellsuggest");
   if (ctx)
      scriptPosG = *ctx;

   r = may_call_simple_func(p, &returnVar);
   if (r == NOTDONE)
      r = eval1(OUT &p, &returnVar, &EVALARG_EVALUATE);
   if (r == OK) {
      if (returnVar.tag != VAR_LIST)
         clearVar(&returnVar);
      else
         list = returnVar.list;
   }

   if (p_verbose == 0)
      --emsg_off;
   clearVar(get_EeglVar_tv(VV_VAL));
   restoreEeglVar(VV_VAL, &save_val);
   scriptPosG = saved_sctx;

   return list;
}

//"list" is supposed to contain two items: a word and a number.  Return the word in "pp" and the 
//number as the return value. Return -1 if anything isn't right.
//Used to get the good word and score from the eval_spell_expr() result.
int
get_spellword(List *list, Byte **pp) {
   ListItem* li = list->first;
   if (!li)
      return -1;
   *pp = tv_get_string(&li->c);

   li = li->next;
   if (!li)
      return -1;
   return (int)tv_get_number(&li->c);
}

// Prepare v: variable "idx" to be used. Save the current typeval in "save_tv" and clear it. When 
// not used yet add the variable to the v: hashtable.
void
prepareEeglVar(int idx, OUT Var *save_tv) {
   *save_tv = eeglVars[idx].entry.c;
   eeglVars[idx].entry.c.string = NULL;  // don't free it yet
   if (eeglVars[idx].entry.c.tag == VAR_UNKNOWN)
      hash_add(&eeglVarsHt, textOfDi16(&eeglVars[idx].entry), S"prepare eeglvar");
}

//Restore v: variable "idx" to typeval "save_tv".
//Note that the v: variable must have been cleared already.
//When no longer defined, remove the variable from the v: hashtable.
void
restoreEeglVar(int idx, Var *save_tv) {
   eeglVars[idx].entry.c = *save_tv;
   if (eeglVars[idx].entry.c.tag != VAR_UNKNOWN)
      return;

   EeSetItem* hi = hash_find(&eeglVarsHt, textOfDi16(&eeglVars[idx].entry));
   if (HASHITEM_EMPTY(hi))
      internal_error(S"restoreEeglVar()");
   else
      hash_remove(&eeglVarsHt, hi, S"restore eeglvar");
}

// List Eegl variables.
private void
listEeglVars(int *first) {
   list_hashtable_vars(&eeglVarsHt, S"v:", FALSE, first);
}

// List script-local variables, if there is a script.
private void
list_script_vars(int *first) {
   if (SCRIPT_ID_VALID(scriptPosG.sid))
   list_hashtable_vars(&SCRIPT_VARS(scriptPosG.sid), S"s:", FALSE, first);
}

// Return TRUE if "name" starts with "g:", "w:", "t:" or "b:".
// But only when an identifier character follows.
int
is_scoped_variable(CS name) {
   return firstOccurrence(S"gwbt", name[0]) != NULL
      && name[1] == ':'
      && isValidForScriptName(name[2]);
}

// Evaluate one Vim expression {expr} in string "p" and append the resulting string to "gap". 
// "p" points to the opening "{". When "evaluate" is FALSE only skip over the expression.
// Return a pointer to the character after "}", NULL for an error.
CS
eval_one_expr_in_str(CS p, ArrayList *gap, int evaluate) {
   Byte   *block_start = skipwhite(p + 1);  // skip the opening {
   Byte   *block_end = block_start;
   Byte   *expr_val;

   if (*block_start == ZERO) {
      showErrFmtMsg(_(e_missing_close_curly_str), p);
      return NULL;
   }
   if (skip_expr(&block_end, NULL) == FAIL)
      return NULL;
   block_end = skipwhite(block_end); //{
   if (*block_end != '}') {
      showErrFmtMsg(_(e_missing_close_curly_str), p);
      return NULL;
   }
   if (evaluate) {
      *block_end = ZERO;
      expr_val = eval_to_string(block_start, FALSE, FALSE); //{
      *block_end = '}';
      if (expr_val == NULL)
         return NULL;
      ga_concat(gap, expr_val);
      eeglFree(expr_val);
   }

   return block_end + 1;
}

//Evaluate all the Vim expressions {expr} in "str" and return the resulting
//string in allocated memory. "{{" is reduced to "{" and "}}" to "}".
//Used for a heredoc assignment. Return NULL for an error.
private CS
eval_all_expr_in_str(CS str) {
   ArrayList   ga;

   ga_init2(&ga, 1, 80);
   CS p = str;

   while (*p != ZERO) {
      Byte   *lit_start;
      int   escaped_brace = FALSE;

      // Look for a block start.
      lit_start = p;
      while (*p != '{' && *p != '}' && *p != ZERO)
          ++p;

      if (*p != ZERO && *p == p[1]) {
          // Escaped brace, unescape and continue.
          // Include the brace in the literal string.
          ++p;
          escaped_brace = TRUE;
      } ei (*p == '}') {
          showErrFmtMsg(_(e_stray_closing_curly_str), str);
          ga_clear(&ga);
          return NULL;
      }

      // Append the literal part.
      ga_concat_len(&ga, lit_start, (Unt)(p - lit_start));

      if (*p == ZERO)
          break;

      if (escaped_brace) {
         // Skip the second brace.
         ++p;
         continue;
      }

      // Evaluate the expression and append the result.
      p = eval_one_expr_in_str(p, &ga, TRUE);
      if (p == NULL) {
         ga_clear(&ga);
         return NULL;
      }
    }
    ga_append(&ga, ZERO);

    return ga.c;
}

//Get a list of lines from a HERE document. The here document is a list of
//lines surrounded by a marker.
//  cmd << {marker}
//    {line1}
//    {line2}
//    ....
//  {marker}
//
//The {marker} is a string. If the optional 'trim' word is supplied before the
//marker, then the leading indentation before the lines (matching the
//indentation in the "cmd" line) is stripped.
//
//Return a List with {lines} or NULL on failure.
List *
heredoc_get(Invocation *invo, CS cmd, int script_get) {
   Byte   *theline = NULL;
   Byte   *marker;
   List   *l;
   Byte   *p;
   Byte   *str;
   int      marker_indent_len = 0;
   int      text_indent_len = 0;
   Byte   *text_indent = NULL;
   Byte   dot[] = ".";
   int      evalstr = FALSE;
   int      eval_failed = FALSE;
   int      heredoc_in_string = FALSE;
   Byte   *line_arg = NULL;
   Byte   *nl_ptr = firstOccurrence(cmd, '\n');

   if (nl_ptr) {
      heredoc_in_string = TRUE;
      line_arg = nl_ptr + 1;
      *nl_ptr = ZERO;
   } ei (invo->ea_getline == NULL) {
      emsg(_(e_cannot_use_heredoc_here));
      return NULL;
   }

   // Check for the optional 'trim' word before the marker
   cmd = skipwhite(cmd);

   while (TRUE) {
      if (STRNCMP(cmd, "trim", 4) == 0 && (cmd[4] == ZERO || SPACE_OR_TAB(cmd[4]))) {
         cmd = skipwhite(cmd + 4);

         // Trim the indentation from all the lines in the here document. The amount of indentation
         // trimmed is the same as the indentation of the first line after the :let command line. 
         // To find the end marker the indent of the :let command line is trimmed.
         p = *invo->commline;
         while (SPACE_OR_TAB(*p)) {
            p++;
            marker_indent_len++;
         }
         text_indent_len = -1;

         continue;
      }
      if (STRNCMP(cmd, "eval", 4) == 0 && (cmd[4] == ZERO || SPACE_OR_TAB(cmd[4]))) {
         cmd = skipwhite(cmd + 4);
         evalstr = TRUE;
         continue;
      }
      break;
   }

   // The marker is the next word.
   if (*cmd != ZERO && isComment(cmd)) {
      marker = skipwhite(cmd);
      p = skiptowhite(marker);
      CS next = skipwhite(p);
      if (*next != ZERO && !isComment(next)) {
         showErrFmtMsg(_(e_trailing_characters_str), p);
         return NULL;
      }
      *p = ZERO;
      if (!script_get && eeIsLower(*marker)) {
          emsg(_(e_marker_cannot_start_with_lower_case_letter));
          return NULL;
      }
   } else {
      //When getting lines for an embedded script, if the marker is missing, accept '.' as the 
      //marker.
      if (script_get)
          marker = dot;
      else {
          emsg(_(e_missing_marker));
          return NULL;
      }
   }

   l = list_alloc();

   for (;;) {
      int   mi = 0;
      int   ti = 0;

      if (heredoc_in_string) {
         Byte   *next_line;

         // heredoc in a string separated by newlines. Get the next line from the string.

         if (*line_arg == ZERO) {
            showErrFmtMsg(_(e_missing_end_marker_str), marker);
            break;
         }

         theline = line_arg;
         next_line = firstOccurrence(theline, '\n');
         if (next_line == NULL)
            line_arg += STRLEN(line_arg);
         else {
            *next_line = ZERO;
            line_arg = next_line + 1;
         }
      } else {
         eeglFree(theline);
         theline = invo->ea_getline(ZERO, invo->cookie, 0, FALSE);
         if (theline == NULL) {
            showErrFmtMsg(_(e_missing_end_marker_str), marker);
            break;
         }
      }

      //with "trim": skip the indent matching the :let line to find the marker
      if (marker_indent_len > 0 && STRNCMP(theline, *invo->commline, marker_indent_len) == 0)
         mi = marker_indent_len;
      if (STRCMP(marker, theline + mi) == 0)
         break;

      //If expression evaluation failed in the heredoc, then skip till the end marker.
      if (eval_failed)
         continue;

      if (text_indent_len == -1 && *theline != ZERO) {
         //set the text indent from the first line.
         p = theline;
         text_indent_len = 0;
         while (SPACE_OR_TAB(*p)) {
            p++;
            text_indent_len++;
         }
         text_indent = copySubstr(theline, text_indent_len);
      }
      //with "trim": skip the indent matching the first line
      if (text_indent) {
         for (ti = 0; ti < text_indent_len; ++ti) {
            if (theline[ti] != text_indent[ti])
               break;
         } 
      } 

      str = theline + ti;
      int free_str = FALSE;

      if (evalstr && !invo->skip) {
         str = eval_all_expr_in_str(str);
         if (!str) {
            //expression evaluation failed
            eval_failed = TRUE;
            continue;
         }
         free_str = TRUE;
      }

      if (list_append_string(l, str, -1) == FAIL)
         break;
      if (free_str)
         eeglFree(str);
    
   }
   if (heredoc_in_string)
      // Next command follows the heredoc in the string.
      invo->nextComm = line_arg;
   else
      eeglFree(theline);
   eeglFree(text_indent);

   if (eval_failed) {
      // expression evaluation in the heredoc failed
      list_free(l);
      return NULL;
   }
   return l;
}

//":let"         list all variable values
//":let var1 var2"      list variable values
//":let var = expr"      assignment command.
//":let var += expr"      assignment command.
//":let var -= expr"      assignment command.
//":let var *= expr"      assignment command.
//":let var /= expr"      assignment command.
//":let var %= expr"      assignment command.
//":let var .= expr"      assignment command.
//":let var ..= expr"      assignment command.
//":let [var1, var2] = expr"   unpack list.
//":let var =<< ..."      heredoc
//
//":final var = expr"      assignment command.
//":final [var1, var2] = expr"   unpack list.
//
//":const"         list all variable values
//":const var1 var2"      list variable values
//":const var = expr"      assignment command.
//":const [var1, var2] = expr"   unpack list.
void
c_let(Invocation* invo) {
   CS arg = invo->arg;
   CS expr = NULL;
   Var returnVar;
   int var_count = 0;
   int semicolon = 0;
   Byte   op[4];
   CS argend;
   int first = TRUE;
   int concat;
   int has_assign;
   Unt flags = 0;

   if (invo->id == C_final) {
      //In Vim script ":final" is short for ":finally".
      c_finally(invo);
      return;
   }

   if (invo->id == C_const)
      flags |= ASSIGN_CONST;
   ei (invo->id == C_final)
      flags |= ASSIGN_FINAL;

   // Vim9 assignment without ":let", ":const" or ":final"
   if (invo->arg == invo->comm)
      flags |= ASSIGN_NO_DECL;

   argend = skip_var_list(arg, &var_count, &semicolon, FALSE);
   if (argend == NULL)
      return;
   if (argend > arg && argend[-1] == '.')  // for var.='str'
      --argend;
   expr = skipwhite(argend);
   concat = expr[0] == '.' && (expr[1] == '.' && expr[2] == '=');
   has_assign = *expr == '=' || (firstOccurrence((CS)"+-*/%", *expr) != NULL && expr[1] == '=');
   if (!has_assign && !concat) {
      // ":let" without "=": list variables
      if (*arg == '[')
         emsg(_(e_invalid_argument));
      ei (!endsComm(invo->comm)) {
         // ":let var1 var2" - list values
         arg = list_arg_vars(invo, arg, &first);
      } ei (!invo->skip) {
         // ":let"
         list_globVars(&first);
         list_buf_vars(&first);
         list_win_vars(&first);
         list_tabVars(&first);
         list_script_vars(&first);
         list_func_vars(&first);
         listEeglVars(&first);
      }
      set_nextcmd(OUT invo, arg);
      return;
   }

   if (expr[0] == '=' && expr[1] == '<' && expr[2] == '<') {
      long cur_lnum = SOURCING_LNUM;

      // :let text =<< [trim] [eval] END
      // :var text =<< [trim] [eval] END
      List* l = heredoc_get(invo, expr + 3, FALSE);

      if (l) {
         returnVar_list_set(&returnVar, l);
         if (!invo->skip) {
            // errors are for the assignment, not the end marker
            SOURCING_LNUM = cur_lnum;
            op[0] = '=';
            op[1] = ZERO;
            (void)letVars(invo->arg, &returnVar, false, semicolon, var_count, flags, op);
         }
         clearVar(&returnVar);
      }
      return;
   }

   EvalCtx   evalarg;
   int len = 1;

   CLEAR_FIELD(returnVar);

   op[0] = '=';
   op[1] = ZERO;
   if (*expr != '=') {
      if (firstOccurrence((CS)"+-*/%.", *expr) != NULL) {
         op[0] = *expr;   // +=, -=, *=, /=, %= or .=
         ++len;
         if (expr[0] == '.' && expr[1] == '.') { // ..=
            ++expr;
            ++len;
         }
      }
      expr += 2;
   } else
      ++expr;


   if (invo->skip)
      ++emsg_skip;
   fillEvalArgFromInvo(OUT &evalarg, invo, invo->skip);
   expr = skipwhite_and_linebreak(expr, &evalarg);
   int cur_lnum = SOURCING_LNUM;
   int eval_res = eval0(expr, &returnVar, invo, &evalarg);
   if (invo->skip)
      --emsg_skip;
   clear_evalarg(&evalarg, invo);

   // Restore the line number so that any type error is given for the
   // declaration, not the expression.
   SOURCING_LNUM = cur_lnum;

   if (!invo->skip && eval_res != FAIL)
      (void)letVars(invo->arg, &returnVar, false, semicolon, var_count, flags, op);
   if (eval_res != FAIL)
      clearVar(&returnVar);
}

//Assign the typeval "tv" to the variable or variables at "arg_start".
//Handle both "var" with any type and "[var, var; var]" with a list type.
//When "op" is not NULL, it points to a string with characters that
//must appear after the variable(s).  Use "+", "-" or "." for add, subtract or concatenate.
//Return OK or FAIL;
private int
letVars(
    Byte   *arg_start,
    Var   *tv,
    Boole      copy,      // copy values from "tv", don't move
    int      semicolon,   // from skip_var_list()
    int      var_count,   // from skip_var_list()
    Unt      flags,      // ASSIGN_FINAL, ASSIGN_CONST, etc.
    Byte   *op)
{
   Byte   *arg = arg_start;
   List   *l;
   int      i;
   int      var_idx = 0;
   ListItem   *item = NULL;
   Var   ltv;

   if (tv->tag == VAR_VOID) {
      emsg(_(e_cannot_use_void_value));
      return FAIL;
   }
   if (*arg != '[') {
      // ":let var = expr" or ":for var in list"
      if (letOne(arg, tv, copy, flags, op, op) == NULL)
         return FAIL;
      return OK;
   }

   // ":let [v1, v2] = list" or ":for [v1, v2] in listlist"
   if (tv->tag != VAR_LIST) {
      emsg(_(e_list_or_tuple_required));
      return FAIL;
   }
   l = tv->list;
   if (!l) {
      emsg(_(e_list_required));
      return FAIL;
   }
   i = list_len(l);

   if (semicolon == 0 && var_count < i) {
      emsg(_(e_less_targets_than_list_items));
      return FAIL;
   }
   if (var_count - semicolon > i) {
      emsg(_(e_more_targets_than_list_items));
      return FAIL;
   }

   CHECK_LIST_MATERIALIZE(l);
   item = l->first;

   while (*arg != ']') {
      arg = skipwhite(arg + 1);
      ++var_idx;
      arg = letOne( arg, &item->c, true, flags | ASSIGN_UNPACK, (CS)",;]", op);
      item = item->next;
      if (!arg)
          return FAIL;

      arg = skipwhite(arg);
      if (*arg == ';') {
         //Put the rest of the list (may be empty) into the var
         //after ';'.  Create a new list for this.
         // Put the rest of the list (may be empty) in the var
         // after ';'.  Create a new list for this.
         l = list_alloc();

         // list
         while (item) {
           list_append_tv(l, &item->c);
           item = item->next;
         }

         ltv.tag = VAR_LIST;
         ltv.lock = 0;
         ltv.list = l;
         l->refcount = 1;

         ++var_idx;
         arg = letOne(skipwhite(arg + 1), &ltv, false, flags | ASSIGN_UNPACK, S"]", op);
         clearVar(&ltv);
         if (arg == NULL)
            return FAIL;
         break;
      } ei (*arg != ',' && *arg != ']') {
         internal_error(S"letVars()");
         return FAIL;
      }
   }

   return OK;
}

// Skip one (assignable) variable name, including @r, $VAR, &option, d.key, l[idx].
private CS
skip_var_one(CS arg) {
   if (*arg == '@' && arg[1] != ZERO)
      return arg + 2;

   // termcap option name may have non-alpha characters
   if (STRNCMP(arg, "&t_", 3) == 0 && arg[3] != ZERO && arg[4] != ZERO)
      return arg + 5;

   Text t = *arg == '$' || *arg == '&' ? text(arg + 1) : text(arg);
   Text end = findNameEnd(t, null, FNE_INCL_BR | FNE_CHECK_START);

   // "a: type" is declaring variable "a" with a type, not "a:". Same for "s: type".

   return end.c + end.len;
}


//Skip over assignable variable "var" or list of variables "[var, var]".
//Used for ":let varvar = expr" and ":for varvar in expr".
//For "[var, var]" increment "*var_count" for each variable.
//for "[var, var; var]" set "semicolon" to 1.
//If "silent" is TRUE do not give an "invalid argument" error message. Return NULL for an error.
CS
skip_var_list(CS arg, OUT int *var_count, OUT int *semicolon, int silent) {
   Byte   *p, *s;

   if (*arg == '[') {
      // "[var, var]": find the matching ']'.
      p = arg;
      for (;;) {
         p = skipwhite(p + 1);   // skip whites after '[', ';' or ','
         s = skip_var_one(p);
         if (s == p) {
            if (!silent)
               showErrFmtMsg(_(e_invalid_argument_str), p);
            return NULL;
         }
         ++*var_count;

         p = skipwhite(s);
         if (*p == ']')
            break;
         ei (*p == ';') {
            if (*semicolon == 1) {
               if (!silent)
                  emsg(_(e_double_semicolon_in_list_of_variables));
               return NULL;
            }
            *semicolon = 1;
         } ei (*p != ',') {
            if (!silent)
               showErrFmtMsg(_(e_invalid_argument_str), p);
            return NULL;
         }
      }
      return p + 1;
   }

   return skip_var_one(arg);
}

// List variables for Set "ht" with prefix "prefix".
// If "empty" is TRUE also list NULL strings as empty strings.
void
list_hashtable_vars(
   EeSet   *ht,
   CS prefix,
   int      empty,
   int      *first)
{
   EeSetItem   *hi;
   DictItem   *di;
   int      todo;
   Byte buf[IOSIZE];

   int save_flags = ht->flags;
   ht->flags |= HTFLAGS_FROZEN;

   todo = (int)ht->count;
   for (hi = ht->array; todo > 0 && !gotInterruptG; ++hi) {
      if (!HASHITEM_EMPTY(hi)) {
          --todo;
          di = HI2DI(hi);

         // apply :filter /pat/ to variable name
         copySubstrToAllocation(buf, (Text){prefix, IOSIZE - 1});
         concatenateStrings(buf, di->key, IOSIZE);
         if (message_filtered(buf))
            continue;

         if (empty || di->c.tag != VAR_STRING || di->c.string)
            list_one_var(di, prefix, first);
      }
   }

   ht->flags = save_flags;
}

// List global variables.
private void
list_globVars(int *first) {
   list_hashtable_vars(&globvarht, E, TRUE, first);
}

// List book variables.
private void
list_buf_vars(int *first) {
   list_hashtable_vars(&curBook->bVars->hashTable, S"b:", TRUE, first);
}

// List window variables.
private void
list_win_vars(int *first) {
   list_hashtable_vars(&curPor->internalVars->hashTable, S"w:", TRUE, first);
}

// List tab variables.
private void
list_tabVars(int *first) {
   list_hashtable_vars(&curtab->vars->hashTable, S"t:", TRUE, first);
}

// List variables in "arg".
private CS
list_arg_vars(Invocation *invo, CS arg, int *first) {
   int      error = FALSE;
   int      len;
   Byte   *name;
   Byte   *name_start;
   Byte   *arg_subsc;
   Byte   *tofree;
   Var    tv;

   while (!endsComm(arg) && !gotInterruptG) {
      if (error || invo->skip) {
         arg = findNameEnd(mbText(arg), NULL, FNE_INCL_BR | FNE_CHECK_START).c;
         if (!SPACE_OR_TAB(*arg) && !endsComm(arg)) {
            emsg_severe = TRUE;
            if (!anyEmsgG) {
               showErrFmtMsg(_(e_trailing_characters_str), arg);
            } 
            break;
         }
      } else {
         // get_name_len() takes care of expanding curly braces
         name_start = name = arg;
         len = get_name_len(&arg, &tofree, TRUE, TRUE);
         if (len <= 0) {
            // This is mainly to keep test 49 working: when expanding
            // curly braces fails overrule the exception error message.
            if (len < 0 && !aborting()) {
               emsg_severe = TRUE;
               showErrFmtMsg(_(e_invalid_argument_str), arg);
               break;
            }
            error = TRUE;
         } else {
            arg = skipwhite(arg);
            if (tofree)
                name = tofree;
            if (eval_variable((Text){name, len}, &tv, NULL, EVAL_VAR_VERBOSE) == FAIL)
                error = TRUE;
            else {
               // handle d.key, l[idx], f(expr)
               arg_subsc = arg;
               if (handle_subscript(&arg, &tv, &EVALARG_EVALUATE, TRUE) == FAIL)
               error = TRUE;
               else {
                  if (arg == arg_subsc && len == 2 && name[1] == ':') {
                     switch (*name) {
                     case 'g': list_globVars(first); break;
                     case 'b': list_buf_vars(first); break;
                     case 'w': list_win_vars(first); break;
                     case 't': list_tabVars(first); break;
                     case 'v': listEeglVars(first); break;
                     case 's': list_script_vars(first); break;
                     case 'l': list_func_vars(first); break;
                     default:
                        showErrFmtMsg(_(e_cant_list_variables_for_str), name);
                     }
                  } else {
                      Byte   numbuf[NUMBUFLEN];
                      Byte   *tf;
                      int      c;
                      Byte   *s;

                      s = echo_string(&tv, &tf, numbuf, 0);
                      c = *arg;
                      *arg = ZERO;
                      list_one_var_a(E,
                         arg == arg_subsc ? name : name_start,
                         tv.tag,
                         s == NULL ? (CS)"" : s,
                         first);
                      *arg = c;
                      eeglFree(tf);
                  }
                  clearVar(&tv);
               }
            }
         }

         eeglFree(tofree);
      }

      arg = skipwhite(arg);
   }

   return arg;
}

// Set an environment variable, part of letOne().
private CS
letEnv(
   Byte   *arg,
   Var   *tv,
   int      flags,
   Byte   *endchars,
   Byte   *op)
{
   Byte   *arg_end = NULL;
   int      len;

   if ((flags & (ASSIGN_CONST | ASSIGN_FINAL)) && (flags & ASSIGN_FOR_LOOP) == 0) {
      emsg(_(e_cannot_lock_environment_variable));
      return NULL;
   }

   // Find the end of the name.
   ++arg;
   CS name = arg;
   len = readEnvNameAndGetItsLen(OUT &arg);
   if (len == 0)
      showErrFmtMsg(_(e_invalid_argument_str), name - 1);
   else {
      if (op && firstOccurrence((CS)"+-*/%", *op) != NULL)
          showErrFmtMsg(_(e_wrong_variable_type_for_str_equal), op);
      ei (endchars && firstOccurrence(endchars, *skipwhite(arg)) == NULL)
          emsg(_(e_unexpected_characters_in_let));
      else {
         Byte   *tofree = NULL;
         int      c1 = name[len];

         name[len] = ZERO;
         CS p = convertVarToStringSingleUse(tv);
         if (p && op && *op == '.') {
            int   mustfree = FALSE;
            Byte  *s = eeglGetEnv(name);

            if (s) {
               p = tofree = concat_str(s, p);
               if (mustfree)
                  eeglFree(s);
            }
         }
         if (p) {
            eeSetenv_ext(name, p);
            arg_end = arg;
         }
         name[len] = c1;
         eeglFree(tofree);
      }
    }
    return arg_end;
}

// Set an option, part of letOne().
private CS
letOption(
   CS arg,
   Var* tv,
   int flags,
   CS endchars,
   CS op
) {
   Byte   *p;
   int scope;
   CS arg_end = NULL;

   if ((flags & (ASSIGN_CONST | ASSIGN_FINAL)) && (flags & ASSIGN_FOR_LOOP) == 0) {
      emsg(_(e_cannot_lock_option));
      return NULL;
   }

   // Find the end of the name.
   p = find_option_end(&arg, &scope);
   if (!p || (endchars && firstOccurrence(endchars, *skipwhite(p)) == NULL)) {
      emsg(_(e_unexpected_characters_in_let));
      return NULL;
   }

   Byte   *s = NULL;
   Boole failed = false;
   Byte   *tofree = NULL;
   Byte   numbuf[NUMBUFLEN];

   Unt c1 = *p;
   *p = ZERO;

   Unt opt_p_flags;
   OptionValue oldVal = optGetValue(OUT &opt_p_flags, arg, scope);
   if (oldVal.tag == OPTION_STRING && (arg[0] != 't' || arg[1] != '_')) {
      showErrFmtMsg(_(e_unknown_option_str_2), arg);
      goto theend;
   }
   OptionValue newVal = (OptionValue){.tag = oldVal.tag};
   if (op && *op != '='
      && (((oldVal.tag == OPTION_BOOLE || oldVal.tag == OPTION_NUM) && *op == '.')
          || (oldVal.tag == OPTION_STRING && *op != '.'))
   ) {
      showErrFmtMsg(_(e_wrong_variable_type_for_str_equal), op);
      goto theend;
   }

   if (newVal.tag == OPTION_BOOLE || newVal.tag == OPTION_NUM) {
      if (oldVal.tag == OPTION_BOOLE)
         newVal.boole = (long)varGetNumberChk(tv, OUT &failed) > 0;
      else
         newVal.num = (long)varGetNumberChk(tv, OUT &failed);
      if (failed)
         goto theend;
   }

   if ( optIsFnOption(opt_p_flags) && (tv->tag == VAR_PARTIAL || tv->tag == VAR_FUNC)) {
      //If the option can be set to a function reference or a lambda
      //and the passed value is a function reference, then convert it to
      //the name (string) of the function reference.
      s = tv2string(tv, &tofree, numbuf, 0);
      if (s == NULL)
          goto theend;
   }
   // Avoid setting a string option to the text "v:false" or similar.
   ei (tv->tag != VAR_BOOL && tv->tag != VAR_SPECIAL) {
      s = convertVarToStringSingleUse(tv);
      if (!s)
         goto theend;
   } ei (oldVal.tag == OPTION_STRING) {
      emsg(_(e_string_required));
      goto theend;
   }

   if (op && *op != '=') {
      if (oldVal.tag == OPTION_NUM) {
         switch (*op) {
         case '+': newVal.num = oldVal.num + newVal.num; break;
         case '-': newVal.num = oldVal.num - newVal.num; break;
         case '*': newVal.num = oldVal.num * newVal.num; break;
         case '/': newVal.num = (long)num_divide(oldVal.num, newVal.num, &failed); break;
         case '%': newVal.num = (long)num_modulus(oldVal.num, newVal.num, &failed); break;
         }
         s = NULL;
         if (failed)
            goto theend;
      } ei (oldVal.tag == OPTION_STRING && oldVal.string && s) {
         // string
         s = concat_str(oldVal.string, s);
         newVal.string = s;
      }
   }

   CS err = optSetByName(arg, newVal, scope);
   arg_end = p;
   if (err)
      emsg(_(err));

theend:
    *p = c1;
    if (oldVal.tag == OPTION_STRING) {
       eeglFree(oldVal.string);
    }
    eeglFree(tofree);
    return arg_end;
}

// Set a register, part of letOne().
private CS
letRegister(
   CS arg,
   Var* tv,
   Unt flags,
   CS endchars,
   CS op)
{
   Byte   *arg_end = NULL;

   if ((flags & (ASSIGN_CONST | ASSIGN_FINAL)) && (flags & ASSIGN_FOR_LOOP) == 0) {
      emsg(_(e_cannot_lock_register));
      return NULL;
   }
   ++arg;
   if (op && firstOccurrence(S"+-*/%", *op) != NULL)
      showErrFmtMsg(_(e_wrong_variable_type_for_str_equal), op);
   ei (endchars && firstOccurrence(endchars, *skipwhite(arg + 1)) == NULL)
      emsg(_(e_unexpected_characters_in_let));
   else {
      Byte   *ptofree = NULL;
      Byte   *p;

      p = convertVarToStringSingleUse(tv);
      if (p && op && *op == '.') {
         Byte  *s = get_reg_contents(*arg == '@' ? '"' : *arg, GREG_EXPR_SRC);

         if (s) {
            p = ptofree = concat_str(s, p);
            eeglFree(s);
         }
      }
      if (p) {
         write_reg_contents(*arg == '@' ? '"' : *arg, p, -1, FALSE);
         arg_end = arg + 1;
      }
      eeglFree(ptofree);
   }
   return arg_end;
}

//Set one item of ":let var = expr" or ":let [v1, v2] = list" to its value.
//Return a pointer to the char just after the var name. NULL if there is an error.
private CS
letOne(
   CS arg,    // points to variable name
   Var   *tv,      // value to assign to variable
   Boole      copy,      // copy value from "tv"
   Unt      flags,      // ASSIGN_CONST, ASSIGN_FINAL, etc.
   Byte   *endchars,   // valid chars after variable name  or NULL
   Byte   *op      // "+", "-", "."  or NULL
) {
   Byte   *arg_end = NULL;

   if (*arg == '$') {
      // ":let $VAR = expr": Set environment variable.
      return letEnv(arg, tv, flags, endchars, op);
   } ei (*arg == '&') {
      // ":let &option = expr": Set option value.
      // ":let &l:option = expr": Set local option value.
      // ":let &g:option = expr": Set global option value.
      // ":for &ts in range(8)": Set option value for for loop
      return letOption(arg, tv, flags, endchars, op);
   } ei (*arg == '@') {
      // ":let @r = expr": Set register contents.
      return letRegister(arg, tv, flags, endchars, op);
   } ei (isValidForScriptName1(*arg) || *arg == '{') {
      Lval   lv;
      Unt   lval_flags = (flags & (ASSIGN_NO_DECL | ASSIGN_DECL)) ? GLV_NO_DECL : 0;
      lval_flags |= (flags & ASSIGN_FOR_LOOP) ? GLV_FOR_LOOP : 0;
      if (op && *op != '=')
         lval_flags |= GLV_ASSIGN_WITH_OP;
          
      //":let var = expr": Set internal variable.
      //":let {expr} = expr": Idem, name made with curly braces
      CS afterName = getLval(OUT &lv, (GetLval){
            .name = mbText(arg), .returnVar = tv, .unlet = false, .skip = false,
            .flags = lval_flags, .fneFlag = FNE_CHECK_START
         }
      );
      if (afterName && lv.name.len > 0) {
         afterName = skipwhite(afterName);
         if (endchars && firstOccurrence(endchars, *afterName) == NULL) {
            emsg(_(e_unexpected_characters_in_let));
         } else {
            letImpl(OUT &lv, tv, copy, flags, op);
            arg_end = lv.name.c + lv.name.len;
         }
      }
      clear_lval(OUT &lv);
   } else
      showErrFmtMsg(_(e_invalid_argument_str), arg);

   return arg_end;
}

// ":unlet", ":lockvar" and ":unlockvar" are quite similar.
private void
unletOrLock(
   Invocation* invo,
   CS argstart,
   int deep,
   int (*callback)(Lval *, CommIndex, CS, Boole, int)
) {
   CS arg = argstart;
   CS nameEnd;
   Boole error = false;
   Lval   lv;

   do {
      if (*arg == '$') { // environment variable in the argument
         lv.name = text(arg);
         lv.var = NULL;
         ++arg;
         if (readEnvNameAndGetItsLen(OUT &arg) == 0) {
            showErrFmtMsg(_(e_invalid_argument_str), arg - 1);
            return;
         }
         if (!error && !invo->skip 
               && callback(&lv, invo->id, arg, invo->forceit, deep) == FAIL
         )
            error = true;
         nameEnd = arg;
      } else {
         // Parse the name and find the end.
         nameEnd = getLval(OUT &lv, (GetLval){
               .name = mbText(arg), .returnVar = null, .unlet = true, .skip = invo->skip || error,
               .flags = GLV_NO_DECL, .fneFlag = FNE_CHECK_START
            }
         );
         
         if (lv.name.len == 0)
            error = true;       // error but continue parsing
         if (!nameEnd) { // parsing error
            if (!(invo->skip || error))
               clear_lval(OUT &lv);
            break;
         } ei (!SPACE_OR_TAB(*nameEnd) && !endsComm(nameEnd)) {
            emsg_severe = TRUE;
            showErrFmtMsg(_(e_trailing_characters_str), nameEnd);
            if (!(invo->skip || error))
               clear_lval(OUT &lv);
            break;
         }

         if (!error && !invo->skip && callback(&lv, invo->id, nameEnd, invo->forceit, deep) == FAIL)
            error = true;

         if (!invo->skip)
            clear_lval(OUT &lv);
      }

      arg = skipwhite(nameEnd);
   } while (!endsComm(nameEnd));

   set_nextcmd(OUT invo, arg);
}

private int
unletVar(
   Lval* lv,
   CommIndex commandId UNUSED,
   CS nameEnd,
   Boole forceIt,
   int deep UNUSED
) {
   int      ret = OK;
   int      cc;

   if (!lv->var) {
      cc = *nameEnd;
      *nameEnd = ZERO;

      // Environment variable, normal name or expanded name.
      if (lv->name.c[0] == '$')
         eeUnsetenv(lv->name.c + 1);
      ei (unletImpl(lv->name.c, forceIt) == FAIL)
         ret = FAIL;
      *nameEnd = cc;
   } ei ((lv->ll_list
          && value_check_lock(lv->ll_list->lock, lv->name, false))
          || (lv->bag && value_check_lock(lv->bag->lock, lv->name, false))
   )
      return FAIL;
   ei (lv->ll_range)
      list_unlet_range(lv->ll_list, lv->ll_li, lv->ll_n1, !lv->ll_empty2, lv->ll_n2);
   ei (lv->ll_list)
      // unlet a List item.
      listitem_remove(lv->ll_list, lv->ll_li);
   else
      // unlet a Dictionary item.
      dictitem_remove(lv->bag, lv->ll_di, S"unlet");

   return ret;
}


// ":unlet[!] var1 ... " command.
void
c_unlet(Invocation *invo) {
   unletOrLock(invo, invo->arg, 0, &unletVar);
}

// ":lockvar" and ":unlockvar" commands
void
c_lockvar(Invocation *invo) {
   Byte   *arg = invo->arg;
   int deep = 2;

   if (invo->forceit)
      deep = -1;
   ei (eeIsDigit(*arg)) {
      deep = getdigits(&arg);
      arg = skipwhite(arg);
   }

   unletOrLock(invo, arg, deep, &do_lock_var);
}

// Unlet one item or a range of items from a list. Return OK or FAIL.
void
list_unlet_range(
   List* l,
   ListItem* li_first,
   long n1_arg,
   int has_n2,
   long n2
){
   // Delete a range of List items.
   ListItem* li = li_first;
   int n1 = n1_arg;
   while (li && (!has_n2 || n2 >= n1)) {
      ListItem *next = li->next;

      listitem_remove(l, li);
      li = next;
      ++n1;
   }
}

// Unlet (undefine and free) a variable from a particular hash table, it it's there
int
unletVarFromHashTable(CS name, CS nameWithPrefix, EeSet* ht, Boole forceit) {
   if (ht && *name != ZERO) {
      Bag* d = get_current_funccal_dict(ht);
      DictItem   *di;
      if (!d) {
         if (ht == &globvarht)
            d = &globvardict;
         else {
            di = findVar_in_ht(ht, *nameWithPrefix, (Text){}, false);
            d = di == NULL ? NULL : di->c.bag;
         }
         if (!d) {
            internal_error(S"unletImpl()");
            return FAIL;
         }
      }
      EeSetItem* hi = hash_find(ht, mbText(name));
      if (HASHITEM_EMPTY(hi))
         hi = find_hi_in_scoped_ht(nameWithPrefix, &ht);
      if (hi && !HASHITEM_EMPTY(hi)) {
         di = HI2DI(hi);
         Text nmWithPrefix = mbText(nameWithPrefix);
         if (var_check_fixed(di->flags, nmWithPrefix, false)
                || var_check_ro(di->flags, nmWithPrefix, false)
                || value_check_lock(d->lock, nmWithPrefix, false)
                || check_hashtab_frozen(ht, S"unlet"))
            return FAIL;

         delete_var(ht, hi);
         return OK;
      }
   }
   if (forceit)
      return OK;
   showErrFmtMsg(_(e_no_such_variable_str), nameWithPrefix);
   return FAIL;
}

// "unlet" a variable. Return OK if it existed, FAIL if not.
// "nameWithPrefix" is the full variable name like "g:foo"
// When "forceit" is TRUE don't complain if the variable doesn't exist.
int
unletImpl(CS nameWithPrefix, Boole forceit) {
   CS name = NULL;
   EeSet* ht = findVarHashTable(mbText(nameWithPrefix), OUT &name);
   return unletVarFromHashTable(name, nameWithPrefix, ht, forceit);
}

// Lock or unlock variable indicated by "lp". "deep" is the levels to go (-1 for unlimited);
// "lock" is TRUE for ":lockvar", FALSE for ":unlockvar".
private int
do_lock_var(
   Lval* lp,
   CommIndex commandId,
   CS nameEnd,
   Boole forceIt UNUSED,
   int deep
) {
   Boole lock = commandId == C_lockvar;
   int ret = OK;
   int cc;
   DictItem   *di;

#ifdef LOG_LOCKVAR
   lo("LKVAR: do_lock_var(): name %s, is_root %d", lp->name, lp->isRoot);
#endif

   if (lp->var == NULL) {
      cc = *nameEnd;
      *nameEnd = ZERO;
      if (lp->name.c[0] == '$') {
          showErrFmtMsg(_(e_cannot_lock_or_unlock_variable_str), lp->name);
          ret = FAIL;
      } else {
         // Normal name or expanded name.
         di = findVar(lp->name.c, true);
         if (!di) {
            ret = FAIL;
         } else {
            if ((di->flags & DI_FLAGS_FIX)
                   && di->c.tag != VAR_BAG
                   && di->c.tag != VAR_LIST)
            {
                // For historic reasons this error is not given for a list
                // or dict.  E.g., the b: dict could be locked/unlocked.
                showErrFmtMsg(_(e_cannot_lock_or_unlock_variable_str), lp->name);
                ret = FAIL;
            }
            else {
               if (ret == OK) {
                  if (lock)
                     di->flags |= DI_FLAGS_LOCK;
                  else
                     di->flags &= ~DI_FLAGS_LOCK;
                  if (deep != 0)
                     item_lock(&di->c, deep, lock, FALSE);
                }
            }
          }
      }
      *nameEnd = cc;
   } ei (deep == 0) {
   // nothing to do
   } ei (lp->isRoot)
      // (un)lock the item.
      item_lock(lp->var, deep, lock, FALSE);
   ei (lp->ll_range) {
      ListItem    *li = lp->ll_li;

      // (un)lock a range of List items.
      while (li && (lp->ll_empty2 || lp->ll_n2 >= lp->ll_n1)) {
         item_lock(&li->c, deep, lock, FALSE);
         li = li->next;
         ++lp->ll_n1;
      }
   } ei (lp->ll_list) {
      // (un)lock a List item.
      item_lock(&lp->ll_li->c, deep, lock, FALSE);
   } else {
      // (un)lock a Dictionary item.
      if (lp->ll_di == NULL) {
         emsg(_(e_dictionary_required));
         ret = FAIL;
      } else
         item_lock(&lp->ll_di->c, deep, lock, FALSE);
   }

   return ret;
}

// Lock or unlock an item.  "deep" is nr of levels to go. When "check_refcount" is TRUE do not 
// lock a list or dict with a reference count larger than 1.
void
item_lock(Var *tv, int deep, int lock, int check_refcount) {
   static int   recurse = 0;
   List   *l;
   ListItem   *li;
   Bag   *d;
   Blob   *b;
   EeSetItem   *hi;
   int      todo;

#ifdef LOG_LOCKVAR
    lo("LKVAR: item_lock(): type %s", vartype_name(tv->tag));
#endif

   if (recurse >= DICT_MAXNEST) {
      emsg(_(e_variable_nested_too_deep_for_unlock));
      return;
   }
   if (deep == 0)
      return;
   ++recurse;

   // lock/unlock the item itself
   if (lock)
      tv->lock |= VAR_LOCKED;
   else
      tv->lock &= ~VAR_LOCKED;

   switch (tv->tag) {
   case VAR_UNKNOWN:
   case VAR_ANY:
   case VAR_VOID:
   case VAR_NUMBER:
   case VAR_BOOL:
   case VAR_STRING:
   case VAR_FUNC:
   case VAR_PARTIAL:
   case VAR_FLOAT:
   case VAR_SPECIAL:
   case VAR_JOB:
   case VAR_CHANNEL:
       break;

   case VAR_BLOB:
      if ((b = tv->blob) != NULL && !(check_refcount && b->refcount > 1)) {
         if (lock)
            b->lock |= VAR_LOCKED;
         else
            b->lock &= ~VAR_LOCKED;
      }
      break;
   case VAR_LIST:
      if ((l = tv->list) != NULL && !(check_refcount && l->refcount > 1)) {
         if (lock)
            l->lock |= VAR_LOCKED;
         else
            l->lock &= ~VAR_LOCKED;
         if (deep < 0 || deep > 1) {
            if (l->first == &range_list_item)
               l->lock |= VAR_ITEMS_LOCKED;
            else {
               // recursive: lock/unlock the items the List contains
               CHECK_LIST_MATERIALIZE(l);
               FOR_ALL_LIST_ITEMS(l, li) item_lock(&li->c, deep - 1, lock, check_refcount);
            }
         }
      }
      break;
   case VAR_BAG:
      if ((d = tv->bag) != NULL && !(check_refcount && d->refcount > 1)) {
         if (lock)
            d->lock |= VAR_LOCKED;
         else
            d->lock &= ~VAR_LOCKED;
         if (deep < 0 || deep > 1) {
            // recursive: lock/unlock the items the List contains
            todo = (int)d->hashTable.count;
            FOR_ALL_HASHTAB_ITEMS(&d->hashTable, hi, todo) {
               if (!HASHITEM_EMPTY(hi)) {
                   --todo;
                   item_lock(&HI2DI(hi)->c, deep - 1, lock, check_refcount);
               }
            }
         }
      }
   }
   --recurse;
}

// Delete all "menutrans_" variables.
void
del_menutrans_vars(void) {
   EeSetItem   *hi;
   int      todo;

   hash_lock(&globvarht);
   todo = (int)globvarht.count;
   for (hi = globvarht.array; todo > 0 && !gotInterruptG; ++hi) {
      if (!HASHITEM_EMPTY(hi)) {
          --todo;
          if (STRNCMP(HI2DI(hi)->key, "menutrans_", 10) == 0)
         delete_var(&globvarht, hi);
      }
   }
   hash_unlock(&globvarht);
}

// Local string buffer for the next two functions to store a variable name with its prefix. 
// Allocated in cat_prefix_varname(), freed later in get_user_var_name().

private CS varnamebuf = NULL;
private Unt varnamebuflen = 0;

// Function to concatenate a prefix and a variable name.
CS
cat_prefix_varname(int prefix, CS name) {
   Unt len = STRLEN(name) + 3;
   if (len > varnamebuflen) {
      eeglFree(varnamebuf);
      len += 10;         // some additional space
      varnamebuf = alloc(len);
      varnamebuflen = len;
   }
   *varnamebuf = prefix;
   varnamebuf[1] = ':';
   STRCPY(varnamebuf + 2, name);
   return varnamebuf;
}

// Function given to expandGeneric() to obtain the list of user defined
// (global/book/portal/built-in) variable names.
CS
get_user_var_name(Expand *xp, int idx) {
   static Ulong   gdone;
   static Ulong   bdone;
   static Ulong   wdone;
   static Ulong   tdone;
   static int      vidx;
   static EeSetItem   *hi;
   EeSet      *ht;

   if (idx == 0) {
      gdone = bdone = wdone = vidx = 0;
      tdone = 0;
   }

   // Global variables
   if (gdone < globvarht.count) {
      if (gdone++ == 0)
         hi = globvarht.array;
      else
         ++hi;
      while (HASHITEM_EMPTY(hi))
         ++hi;
      if (STRNCMP("g:", xp->input.c, 2) == 0)
         return cat_prefix_varname('g', hi->hi_key);
      return hi->hi_key;
   }

   // b: variables
   ht = &prevPor_curPor()->book->bVars->hashTable;
   if (bdone < ht->count) {
      if (bdone++ == 0)
         hi = ht->array;
      else
         ++hi;
      while (HASHITEM_EMPTY(hi))
         ++hi;
      return cat_prefix_varname('b', hi->hi_key);
   }

   // w: variables
   ht = &prevPor_curPor()->internalVars->hashTable;
   if (wdone < ht->count) {
      if (wdone++ == 0)
         hi = ht->array;
      else
         ++hi;
      while (HASHITEM_EMPTY(hi))
         ++hi;
      return cat_prefix_varname('w', hi->hi_key);
   }

   // t: variables
   ht = &curtab->vars->hashTable;
   if (tdone < ht->count) {
      if (tdone++ == 0)
         hi = ht->array;
      else
         ++hi;
      while (HASHITEM_EMPTY(hi))
         ++hi;
      return cat_prefix_varname('t', hi->hi_key);
   }

   // v: variables
   if (vidx < EV_LEN)
      return cat_prefix_varname('v', (CS)eeglVars[vidx++].name);

   EE_CLEAR(varnamebuf);
   varnamebuflen = 0;
   return NULL;
}

char *
get_var_special_name(int nr) {
   switch (nr) {
   case VVAL_FALSE: return "v:false";
   case VVAL_TRUE:  return "v:true";
   case VVAL_NULL:  return "v:null";
   case VVAL_NONE:  return "v:none";
   }
   internal_error(S"get_var_special_name()");
   return "42";
}

// Return the global variable dictionary
Bag*
get_globvar_dict(void) {
   return &globvardict;
}

// Return the global variable hash table
EeSet *
get_globvar_ht(void) {
   return &globvarht;
}

// Return the v: variable dictionary
Bag*
getEeglVarDict(void) {
   return &eeglVarsS;
}

// Return the index of a v:variable. Negative if not found. Return DI_ flags in "flags".
int
find_EeglVar(CS name, OUT Unt* flags) {
   DictItem* di = findVar_in_ht(&eeglVarsHt, 0, mbText(name), true);
   if (!di)
      return -1;
      
   *flags = di->flags;
   EeglVar* vv = (EeglVar *)((char *)di - offsetof(EeglVar, entry));
   return (int)(vv - eeglVars);
}


// Set tag of v: variable to "tag".
void
set_EeglVar_type(int idx, VarTag tag) {
   eeglVars[idx].entry.c.tag = tag;
}

// Set number v: variable to "val".
// Note that this does not set the type, use set_EeglVar_type() for that.
void
set_EeglVar_nr(int idx, Long val) {
   eeglVars[idx].entry.c.number = val;
}

CS
get_EeglVar_name(int idx) {
   return eeglVars[idx].name;
}

// Get Var v: variable value.
Var *
get_EeglVar_tv(int idx) {
   return &eeglVars[idx].entry.c;
}

// Set v: variable to "tv".  Only accepts the same type. Takes over the value of "tv".
int
set_EeglVar_tv(int idx, Var *tv) {
   if (eeglVars[idx].entry.c.tag != tv->tag) {
      emsg(_(e_type_mismatch_for_v_variable));
      clearVar(tv);
      return FAIL;
   }
   // isReadonly is also checked when compiling, but let's check here as well.
   if (eeglVars[idx].isReadonly) {
      showErrFmtMsg(_(e_cannot_change_readonly_variable_str), eeglVars[idx].name);
      return FAIL;
   }
   clearVar(&eeglVars[idx].entry.c);
   eeglVars[idx].entry.c = *tv;
   return OK;
}

// Get number v: variable value.
Long
get_EeglVar_nr(int idx) {
   return eeglVars[idx].entry.c.number;
}

//Get string v: variable value. Use a static buffer, can only be used once.
//If the String variable has never been set, return an empty string. Never return NULL
CS
get_EeglVar_str(int idx) {
   return tv_get_string(&eeglVars[idx].entry.c);
}

// Get List v: variable value.  Caller must take care of reference count when needed.
List *
get_EeglVar_list(int idx) {
   return eeglVars[idx].entry.c.list;
}

// Get Bag v: variable value.  Caller must take care of reference count when needed.
Bag *
get_EeglVar_dict(int idx) {
   return eeglVars[idx].entry.c.bag;
}

// Set v:char to character "c".
void
set_EeglVar_char(int c) {
   Byte buf[MB_MAXBYTES + 1];

   buf[mb_char2bytes(c, buf)] = ZERO;
   set_EeglVar_string(VV_CHAR, buf, -1);
}

//Set v:count to "count" and v:count1 to "count1".
//When "set_prevcount" is TRUE first set v:prevcount from v:count.
void
set_vcount( long   count, long   count1, int      set_prevcount) {
   if (set_prevcount)
      eeglVars[VV_PREVCOUNT].entry.c.number = eeglVars[VV_COUNT].entry.c.number;
   eeglVars[VV_COUNT].entry.c.number = count;
   eeglVars[VV_COUNT1].entry.c.number = count1;
}

// Save variables that might be changed as a side effect.  Used when executing a timer callback.
void
saveEeglVars(EeglVarsSave* evSave) {
   evSave->prevCount = eeglVars[VV_PREVCOUNT].entry.c.number;
   evSave->count = eeglVars[VV_COUNT].entry.c.number;
   evSave->count1 = eeglVars[VV_COUNT1].entry.c.number;
}

//Restore variables saved by save_cimVars().
void
restoreEeglVars(EeglVarsSave* evSave) {
   eeglVars[VV_PREVCOUNT].entry.c.number = evSave->prevCount;
   eeglVars[VV_COUNT].entry.c.number = evSave->count;
   eeglVars[VV_COUNT1].entry.c.number = evSave->count1;
}

// Set string v: variable to a copy of "val". If 'copy' is FALSE, then set the value.
void
set_EeglVar_string(int idx, Byte* val, int len) { //length of "val" to use or -1 (whole string)
   clearVar(&eeglVars[idx].entry.c);
   eeglVars[idx].entry.c.tag = VAR_STRING;
   if (val == NULL)
      eeglVars[idx].entry.c.string = NULL;
   ei (len == -1)
      eeglVars[idx].entry.c.string = copyStr(val);
   else
      eeglVars[idx].entry.c.string = copySubstr(val, len);
}

// Set List v: variable to "val".
void
set_EeglVar_list(int idx, List *val) {
   clearVar(&eeglVars[idx].entry.c);
   eeglVars[idx].entry.c.tag = VAR_LIST;
   eeglVars[idx].entry.c.list = val;
   if (val)
      ++val->refcount;
}

// Set Dictionary v: variable to "val".
void
set_EeglVar_dict(int idx, Bag *val) {
   clearVar(&eeglVars[idx].entry.c);
   eeglVars[idx].entry.c.tag = VAR_BAG;
   eeglVars[idx].entry.c.bag = val;
   if (!val)
      return;

   ++val->refcount;
   bagSetItemsRo(val);
}

// Set the v:argv list.
void
set_argv_var(char **argv, int argc) {
   List* l = list_alloc();
   l->lock = VAR_FIXED;
   for (int i = 0; i < argc; ++i) {
      if (list_append_string(l, (CS)argv[i], -1) == FAIL)
          exitEegl(1);
      l->lv_u.mat.last->c.lock = VAR_FIXED;
   }
   set_EeglVar_list(VV_ARGV, l);
}

// Reset v:register, taking the 'clipboard' setting into account.
void
reset_reg_var(void) {
   int regname = 0;

   // Adjust the register according to 'clipboard', so that when
   // "unnamed" is present it becomes '*' or '+' instead of '"'.
   adjust_clip_reg(&regname);
   set_reg_var(regname);
}

// Set v:register if needed.
void
set_reg_var(int c) {
    Byte   regname;

   if (c == 0 || c == ' ')
      regname = '"';
   else
      regname = c;
   // Avoid free/alloc when the value is already right.
   if (eeglVars[VV_REG].entry.c.string == NULL || eeglVars[VV_REG].entry.c.string[0] != c)
      set_EeglVar_string(VV_REG, &regname, 1);
}

//Get or set v:exception.  If "oldval" == NULL, return the current value.
//Otherwise, restore the value to "oldval" and return NULL.
//Must always be called in pairs to save and restore v:exception!  Does not take care of memory 
//allocations.
CS
v_exception(CS oldval) {
   if (oldval == NULL)
      return eeglVars[VV_EXCEPTION].entry.c.string;

   eeglVars[VV_EXCEPTION].entry.c.string = oldval;
   return NULL;
}

//Get or set v:throwpoint.  If "oldval" == NULL, return the current value.
//Otherwise, restore the value to "oldval" and return NULL.
//Must always be called in pairs to save and restore v:throwpoint!  Does not
//take care of memory allocations.
CS
v_throwpoint(CS oldval) {
   if (!oldval)
      return eeglVars[VV_THROWPOINT].entry.c.string;

   eeglVars[VV_THROWPOINT].entry.c.string = oldval;
   return NULL;
}

//Set v:cmdarg.
//If "invo" != NULL, use "invo" to generate the value and return the old value.
//If "oldarg" != NULL, restore the value to "oldarg" and return NULL.
//Must always be called in pairs!
CS
set_cmdarg(Invocation *invo, CS oldarg) {
   Byte   *oldval;
   Byte   *newval;
   unsigned   len;

   oldval = eeglVars[VV_CMDARG].entry.c.string;
   if (invo == NULL) {
      eeglFree(oldval);
      eeglVars[VV_CMDARG].entry.c.string = oldarg;
      return NULL;
   }

   if (invo->force_bin == FORCE_BIN)
      len = 6;
   ei (invo->force_bin == FORCE_NOBIN)
      len = 8;
   else
      len = 0;

   if (invo->read_edit)
      len += 7;

   if (invo->bad_char != 0)
      len += 7 + 4;  // " ++bad=" + "keep" or "drop"

   newval = alloc(len + 1);

   if (invo->force_bin == FORCE_BIN)
      sprintf((char *)newval, " ++bin");
   ei (invo->force_bin == FORCE_NOBIN)
      sprintf((char *)newval, " ++nobin");
   else
      *newval = ZERO;

   if (invo->read_edit)
      STRCAT(newval, " ++edit");

   if (invo->bad_char == BAD_KEEP)
      STRCPY(newval + STRLEN(newval), " ++bad=keep");
   ei (invo->bad_char == BAD_DROP)
      STRCPY(newval + STRLEN(newval), " ++bad=drop");
   ei (invo->bad_char != 0)
      sprintf((char *)newval + STRLEN(newval), " ++bad=%c", invo->bad_char);
   eeglVars[VV_CMDARG].entry.c.string = newval;
   return oldval;
}

//Get the value of internal variable "name".
//Return OK or FAIL.  If OK is returned "returnVar" must be cleared.
private int
eval_variable(
   Text name,
   Var   *returnVar,      // NULL when only checking existence
   DictItem   **dip,      // non-NULL when typval's dict item is needed
   int      flags)      // EVAL_VAR_ flags
{
   int      ret = OK;
   Var   *tv = NULL;
   int      found = FALSE;
   EeSet   *ht = NULL;
   Byte cc = 0;
   TypeSpec* type = NULL;

   if (name.len > 0) {
      // truncate the name, so that we can use strcmp()
      cc = name.c[name.len];
      name.c[name.len] = ZERO;
   }

   // Check for user-defined variables.
   DictItem* v = findVarAndSetHtable(name, OUT &ht, flags & EVAL_VAR_NOAUTOLOAD);

   if (v) {
      tv = &v->c;
      if (dip)
         *dip = v;
   } else
      ht = NULL;

   if (!found) {
      if (tv == NULL) {
         if (returnVar && (flags & EVAL_VAR_VERBOSE)) {
            showErrFmtMsg(_(e_undefined_variable_str), name);
         } 
         ret = FAIL;
      } ei (returnVar) {
         Svar  *sv = NULL;
         if (ht && ht == get_script_local_ht() && tv != &SCRIPT_SV(scriptPosG.sid)->sv_var.c) {
            sv = find_typval_in_script(tv, 0, TRUE);
            if (sv) {
               type = sv->sv_type;
            }
         }

         // If a list or dict variable wasn't initialized and has
         // meaningful type, do it now.  Not for global variables, they are not declared.
         if (ht != &globvarht) {
            if (tv->tag == VAR_BAG && tv->bag == NULL) {
               tv->bag = allocBag();
               ++tv->bag->refcount;
               tv->bag->ty = alloc_type(type);
               if (sv)
                   sv->sv_flags |= SVFLAG_ASSIGNED;
            } ei (tv->tag == VAR_LIST && tv->list == NULL) {
               tv->list = list_alloc();
               ++tv->list->refcount;
               tv->list->ty = alloc_type(type);
               if (sv)
                  sv->sv_flags |= SVFLAG_ASSIGNED;
            } ei (tv->tag == VAR_BLOB && tv->blob == NULL) {
               tv->blob = blob_alloc();
               ++tv->blob->refcount;
               if (sv)
                  sv->sv_flags |= SVFLAG_ASSIGNED;
            }
         }
         copy_tv(OUT returnVar, tv);
      }
   }

   if (cc != 0)
      name.c[name.len] = cc;

   return ret;
}

//Get the value of internal variable "name", also handling "import.name".
//Return OK or FAIL. If OK is returned, "returnVar" must be cleared.
int
eval_variable_import(CS name, Var   *returnVar) {
   Byte  *s = name;
   while (ASCII_ISALNUM(*s) || *s == '_')
      ++s;
   int len = (int)(s - name);
   Text nam = (Text){s, len};
   if (eval_variable(nam, returnVar, NULL, 0) == FAIL)
      return FAIL;
   if (returnVar->tag == VAR_ANY && *s == '.') {
      CS ns = s + 1;
      s = ns;
      while (ASCII_ISALNUM(*s) || *s == '_')
         ++s;
      return eval_variable((Text){ns, (s - ns)}, returnVar, NULL, 0);
   }
   return OK;
}


//Check if variable "name" is a local variable or an argument.
//If so, "*eval_lavars_used" is set to TRUE.
private void
check_vars(Text name) {
   if (eval_lavars_used == NULL)
      return;

   Byte   *varname;
   // truncate the name, so that we can use strcmp()
   Byte cc = name.c[name.len];
   name.c[name.len] = ZERO;

   EeSet* ht = findVarHashTable(name, OUT &varname);
   if ((ht == get_funccal_local_ht() || ht == get_funccal_args_ht()) && (findVar(name.c, true))){  
      *eval_lavars_used = TRUE;
   }

   name.c[name.len] = cc;
}

//Find variable "name" in the list of variables. Return a pointer to it if found, NULL if not 
//found. Careful: "a:0" variables don't have a name. When "htp" is not NULL, set "htp" to the 
//EeSet used.
private DictItem*
findVarAndSetHtable(Text name, OUT EeSet** htp, Boole no_autoload) {
   CS varname;
   EeSet* ht = findVarHashTable(name, OUT &varname);
   if (htp)
      *htp = ht;
   if (!ht)
      return NULL;
   DictItem* ret = findVar_in_ht(ht, name.c[0], mbText(varname), no_autoload);
   if (ret)
      return ret;

   // Search in parent scope for lambda
   ret = findVar_in_scoped_ht(name, no_autoload);
   if (ret)
      return ret;

   // and finally try
   return findVar_autoload_prefix(name.c, 0, htp, NULL);
}


// Find variable "name" in the list of variables. Return a pointer to it if found, NULL if not 
// found. Careful: "a:0" variables don't have a name.
DictItem *
findVar(CS name, Boole noAutoload) {
   return findVarAndSetHtable(mbText(name), null, noAutoload);
}

//Find variable "name" with sn_autoload_prefix.
//Return a pointer to it if found, NULL if not found.
//When "sid" > 0, use it otherwise use "scriptPosG.sid".
//When "htp" is not NULL  set "htp" to the EeSet used.
//When "namep" is not NULL set "namep" to the generated name, and
//then the caller gets ownership and is responsible for freeing the name.
DictItem *
findVar_autoload_prefix(CS name, int sid, EeSet **htp, Byte **namep) {
   EeSet   *ht;
   DictItem   *ret = NULL;
   // When using "vim9script autoload" script-local items are prefixed but can
   // be used with s:name.
   int check_sid = sid > 0 ? sid : scriptPosG.sid;
   if (SCRIPT_ID_VALID(check_sid) && (name[0] == 's' && name[1] == ':')) {
      ScriptItem *si = SCRIPT_ITEM(check_sid);

      if (si->sn_autoload_prefix) {
         CS base_name = (name[0] == 's' && name[1] == ':') ? name + 2 : name;
         CS auto_name = concat_str(si->sn_autoload_prefix, base_name);

         if (auto_name) {
            int free_auto_name = TRUE;
            ht = &globvarht;
            ret = findVar_in_ht(ht, 'g', mbText(auto_name), TRUE);
            if (ret) {
               if (htp)
                  *htp = ht;
               if (namep) {
                  free_auto_name = FALSE;
                  *namep = auto_name;
               }
            }
            if (free_auto_name)
                eeglFree(auto_name);
         }
      }
    }

    return ret;
}

//Like findVar() but if the name starts with <SNR>99_ then look in the
//referenced script (used for a funcref).
DictItem *
findVar_also_in_script(CS name, OUT EeSet** htp, Boole no_autoload) {
   if (STRNCMP(name, "<SNR>", 5) == 0 && SAFE_isdigit(name[5])) {
      Byte       *p = name + 5;
      int       sid = getdigits(&p);

      if (SCRIPT_ID_VALID(sid) && *p == '_') {
         EeSet   *ht = &SCRIPT_VARS(sid);

         if (ht) {
            DictItem *di = findVar_in_ht(ht, 0, text(p + 1), no_autoload);

            if (di) {
                if (htp != NULL)
               *htp = ht;
                return di;
            }
         }
      }
   }

   return findVarAndSetHtable(mbText(name), OUT htp, no_autoload);
}

//Find variable "varname" in hashtab "ht" on level "level". When "varname" is empty, return 
//curPor/curtab/etc vars dictionary. Return NULL if not found.
DictItem *
findVar_in_ht(
   EeSet* ht,
   VarLevel level,
   Text varname,
   Boole no_autoload
) {
   if (varname.len == 0) {
      switch (level) {
      case VAR_SCRIPT: return &SCRIPT_SV(scriptPosG.sid)->sv_var;
      case VAR_GLOBAL: return &globvars_var;
      case VAR_EEGL: return &currEeglVarS;
      case VAR_BOOK: return &curBook->bookVar;
      case VAR_PORTAL: return &curPor->wVar;
      case VAR_TAB: return &curtab->tabVar;
      case VAR_LOCAL: return get_funccal_local_var();
      }
      return NULL;
   }

   EeSetItem* hi = hash_find(ht, varname);
   if (HASHITEM_EMPTY(hi)) {
      // For global variables we may try auto-loading the script. If it
      // worked find the variable again.  Don't auto-load a script if it was
      // loaded already, otherwise it would be loaded every time when
      // checking if a function name is a Funcref variable.
      if (ht == &globvarht && !no_autoload) {
         //Note: scriautoload() may make "hi" invalid. It must either be obtained again or not used
         if (!scriautoload(varname.c, FALSE) || aborting())
            return NULL;
         hi = hash_find(ht, varname);
      }
      if (HASHITEM_EMPTY(hi))
         return NULL;
   }
   return HI2DI(hi);
}

// Get the script-local hashtable. NULL if not in a script context.
EeSet *
get_script_local_ht(void) {
   ScriptId sid = scriptPosG.sid;

   if (SCRIPT_ID_VALID(sid))
      return &SCRIPT_VARS(sid);
   return NULL;
}

// Look for "name" in script-local variables and functions.
// When "cmd" is TRUE it must look like a command, a function must be followed by "(" or "->".
// Return OK when found, FAIL when not found.
int
lookup_scriptitem(Text name, int cmd) {
   EeSet* ht = get_script_local_ht();
   if (!ht)
      return FAIL;
      
   Byte buf[30];
   Boole is_global = false;
   CS p;
   if (name.len < sizeof(buf) - 1) {
      // avoid an alloc/free for short names
      copySubstrToAllocation(OUT buf, name);
      p = buf;
   } else {
      p = copySubstr(name.c, name.len);
   }

   EeSetItem* hi = hash_find(ht, text(p));
   int res = HASHITEM_EMPTY(hi) ? FAIL : OK;

   // if not script-local, then perhaps autoload-exported
   if (res == FAIL && findVar_autoload_prefix(p, 0, NULL, NULL) != NULL)
      res = OK;

   if (p != buf)
      eeglFree(p);

   // Find a function, so that a following "->" works.
   // When used as a command require "(" or "->" to follow, "Cmd" is a user
   // command while "Cmd()" is a function call.
   CS fname = name.c;
   if (res != OK) {
      p = skipwhite(name.c + name.len);

      if (!cmd || name.c[name.len] == '(' || (p[0] == '-' && p[1] == '>')) {
         // Do not check for an internal function, since it might also be a
         // valid command, such as ":split" versus "split()".
         // Skip "g:" before a function name.
         if (name.c[0] == 'g' && name.c[1] == ':') {
            is_global = true;
            fname = name.c + 2;
         }
         if (find_func(fname, is_global) != NULL)
            res = OK;
      }
   }

   return res;
}

//Find the hash table used for a variable name. Return NULL if the name is not valid.
//Set "varname" to the start of name without prefixes like "b:", "g:" etc.
EeSet*
findVarHashTable(Text name, OUT CS* varname) {
   if (name.len == 0)
      return NULL;
   EeSet* ht;
   if (name.c[1] == ':') {
      *varname = name.c + 2;
      if (name.c[0] == 'g')            // global variable
         return &globvarht;
      // There must be no ':' or '#' in the rest of the name, unless g: is used
      for (Byte* p = name.c; p < name.c + name.len; p++) {
         if (*p == ':' || *p == AUTOLOAD_CHAR) {
            return null;
         }
      }
      switch (name.c[0]) {
         case 'b': return &curBook->bVars->hashTable;   // book variable
         case 'w': return &curPor->internalVars->hashTable;   // portal variable
         case 't': return &curtab->vars->hashTable;   // tab variable
         case 'v': return &eeglVarsHt;   // Eegl variable
         case 's':   // script variable
            ht = get_script_local_ht();
            if (ht)
               return ht;
      }
      if (get_current_funccal()) {
         if (name.c[0] == 'a')         // a: function argument
            return get_funccal_args_ht();
         if (name.c[0] == 'l')         // l: local function variable
            return get_funccal_local_ht();
      }
      return NULL;
   } else { 
      // The name must not start with a colon or #.
      if (name.c[0] == ':' || name.c[0] == AUTOLOAD_CHAR)
         return null;
      *varname = name.c;
      ht = get_funccal_local_ht();
      if (ht)
         return ht;            // local variable
      return &globvarht;         // global variable
   }
}

// Get the string value of a (global/local) variable.
// Note: see tv_get_string() for how long the pointer remains valid. Return NULL if it doesn't exist
CS
get_var_value(Byte *name) {
   DictItem* v = findVar(name, false);
   if (!v)
      return NULL;
   return tv_get_string(&v->c);
}

// Allocate a new hashtab for a sourced script.  It will be used while
//sourcing this script and when executing functions defined in the script.
void
new_script_vars(ScriptId id) {
   ScriptVar* sv = ALLOC_CLEAR_ONE(ScriptVar);
   if (!sv)
      return;
   init_var_dict(&sv->sv_dict, &sv->sv_var, VAR_SCOPE);
   SCRIPT_ITEM(id)->sn_vars = sv;
}

//Initialize bag "bag" as a scope and set variable "dict_var" to point to it.
void
init_var_dict(Bag *bag, DictItem *dict_var, int scope) {
   hash_init(&bag->hashTable);
   bag->lock = 0;
   bag->scope = scope;
   bag->refcount = DO_NOT_FREE_CNT;
   bag->copyId = 0;
   dict_var->c.bag = bag;
   dict_var->c.tag = VAR_BAG;
   dict_var->c.lock = VAR_FIXED;
   dict_var->flags = DI_FLAGS_RO | DI_FLAGS_FIX;
   dict_var->key[0] = ZERO;
}

// Unreference a dictionary initialized by init_var_dict().
void
unref_var_dict(Bag *dict) {
   // Now the dict needs to be freed if no one else is using it, go back to normal reference counting.
   dict->refcount -= DO_NOT_FREE_CNT - 1;
   bagUnref(dict);
}

// Clean up a list of internal variables. Free all allocated variables and the value they contain.
// Clear hashtab "ht", does not free it.
void
vars_clear(EeSet *ht) {
   vars_clear_ext(ht, TRUE);
}

// Like vars_clear(), but only free the value if "free_val" is TRUE.
void
vars_clear_ext(EeSet *ht, int free_val) {
   EeSetItem   *hi;
   DictItem   *v;

   hash_lock(ht);
   int todo = (int)ht->count;
   FOR_ALL_HASHTAB_ITEMS(ht, hi, todo) {
      if (!HASHITEM_EMPTY(hi)) {
         --todo;

         // Free the variable.  Don't remove it from the hashtab,
         // array might change then.  hash_clear() takes care of it later.
         v = HI2DI(hi);
         if (free_val)
            clearVar(&v->c);
         if (v->flags & DI_FLAGS_ALLOC)
            eeglFree(v);
      }
   }
   hash_clear(ht);
   hash_init(ht);
}

// Delete a variable from hashtab "ht" at item "hi". Clear the variable value and free the dictitem
void
delete_var(EeSet *ht, EeSetItem *hi) {
    DictItem   *di = HI2DI(hi);

   if (hash_remove(ht, hi, S"delete variable") != OK)
      return;

    clearVar(&di->c);
    eeglFree(di);
}

// List the value of one internal variable.
private void
list_one_var(DictItem *v, CS prefix, int *first) {
   Byte   *tofree;
   Byte   *s;
   Byte   numbuf[NUMBUFLEN];

   s = echo_string(&v->c, &tofree, numbuf, get_copyID());
   list_one_var_a(prefix, v->key, v->c.tag, s == NULL ? E : s, first);
   eeglFree(tofree);
}

private void
list_one_var_a(
   CS prefix,
   CS name,
   int      type,
   CS string,
   int* first)  // when TRUE clear rest of screen and set to FALSE
{
   // don't use msg() or msgDeco() to avoid overwriting "v:statusmsg"
   msg_start();
   msg_puts(prefix);
   if (name)   // "a:" vars don't have a name stored
      msg_puts(name);
   msg_putchar(' ');
   msg_advance(22);
   if (type == VAR_NUMBER)
      msg_putchar('#');
   ei (type == VAR_FUNC || type == VAR_PARTIAL)
      msg_putchar('*');
   ei (type == VAR_LIST) {
      msg_putchar('[');
      if (*string == '[')
          ++string;
   } ei (type == VAR_BAG) {
      msg_putchar('{');
      if (*string == '{')
          ++string;
   } else
      msg_putchar(' ');

    msg_outtrans(string);

   if (type == VAR_FUNC || type == VAR_PARTIAL)
      msg_puts(S"()");
   if (*first) {
      msg_clr_eos();
      *first = FALSE;
   }
}

// Addition handling for setting a v: variable.
// Return TRUE if the variable should be set normally, FALSE if nothing else needs to be done.
int
before_set_vvar(
    Byte   *varname,
    DictItem   *di,
    Var   *tv,
    int      copy,
    int      *type_error
) {
   if (di->c.tag == VAR_STRING) {
      EE_CLEAR(di->c.string);
      if (copy || tv->tag != VAR_STRING) {
         CS val = tv_get_string(tv);

         // Careful: when assigning to v:errmsg and
         // tv_get_string() causes an error message the variable
         // will already be set.
         if (di->c.string == NULL)
            di->c.string = copyStr(val);
      } else {
          // Take over the string to avoid an extra alloc/free.
          di->c.string = tv->string;
          tv->string = NULL;
      }
      return FALSE;
   } ei (di->c.tag == VAR_NUMBER) {
      di->c.number = tv_get_number(tv);
      if (STRCMP(varname, "searchforward") == 0)
         set_search_direction(di->c.number ? '/' : '?');
      ei (STRCMP(varname, "hlsearch") == 0) {
         hiliteSearchG = di->c.number != 0;
         redraw_all_later(UPD_SOME_VALID);
      }
      return FALSE;
   } ei (di->c.tag != tv->tag) {
      *type_error = TRUE;
      return FALSE;
   }
   return TRUE;
}

//Set variable "name" to "newValue".
//If the variable already exists, its value is updated. Otherwise the variable is created.
void
set_var(Text name, Var* newValue, Boole copy){  // make copy of value in "tv"
   setVarImpl(name, 0, newValue, copy, ASSIGN_DECL);
}

// Set variable "name" to value in "tv_arg". When "sid" is non-zero, "name" is in the script with 
// this ID. If the variable already exists and "is_const" is FALSE, the value is updated.
// Otherwise the variable is created.
private int
setVarImpl(
   Text name,
   ScriptId sid,
   Var* newValue,
   Boole      copy,       // make copy of value in "tv"
   Unt      flags_arg  // ASSIGN_CONST, ASSIGN_FINAL, etc.
){
   Var* tv = newValue;
   CS varname;
   CS name_tofree = NULL;
   EeSet* ht = NULL;
   Boole var_in_autoload = false;
   Unt flags = flags_arg;
   Boole freeNewValue = !copy;  // free newValue if not used
   int retStatus = FAIL;

   if (sid != 0) {
      varname = NULL;
      if (SCRIPT_ID_VALID(sid)) {
         Byte   *auto_name = NULL;
         if (findVar_autoload_prefix(name.c, sid, &ht, &auto_name) != NULL) {
            var_in_autoload = true;
            varname = auto_name;
            name_tofree = varname;
         } else
            ht = &SCRIPT_VARS(sid);
      }
      if (!varname)
         varname = name.c;
   } else {
      ht = findVarHashTable(name, OUT &varname);
   }
   if (!ht || *varname == ZERO) {
      showErrFmtMsg(_(e_illegal_variable_name_str), name);
      goto failed;
   }
   Boole is_script_local = ht == get_script_local_ht() || sid != 0 || var_in_autoload;

   if ((flags & ASSIGN_FOR_LOOP) != 0 && is_scoped_variable(name.c))
      // Do not make g:var, w:var, b:var or t:var final.
      flags &= ~ASSIGN_FINAL;

   DictItem* di = findVar_in_ht(ht, 0, mbText(varname), true);
   // Search in parent scope which is possible to reference from lambda
   if (!di)
      di = findVar_in_scoped_ht(name, TRUE);

   if ((tv->tag == VAR_FUNC || tv->tag == VAR_PARTIAL) && var_wrong_func_name(name, di == NULL))
      goto failed;

   if (di) {
      // Item already exists.  Allowed to replace when reloading.
      if ((di->flags & DI_FLAGS_RELOAD) == 0) {
         if (((flags & (ASSIGN_CONST | ASSIGN_FINAL)) != 0) && (flags & ASSIGN_FOR_LOOP) == 0) {
            emsg(_(e_cannot_modify_existing_variable));
            goto failed;
         }

         // List and Blob types can be modified in-place using the "+="
         // compound operator.  For other types, this is not allowed.
         int type_inplace_modifiable = di->c.tag == VAR_LIST || di->c.tag == VAR_BLOB;

         //Modifying a final variable with a List value using the "+="
         //operator is allowed.  For other types, it is not allowed.
         if ((((flags & ASSIGN_FOR_LOOP) == 0 || (flags & ASSIGN_DECL) == 0)
               && ((flags & ASSIGN_COMPOUND_OP) == 0 || !type_inplace_modifiable))
                      ? var_check_permission(di, name.c) == FAIL
                      : var_check_ro(di->flags, name, false))
            goto failed;
      } else {
         //can only redefine once
         di->flags &= ~DI_FLAGS_RELOAD;
      }

      // existing variable, need to clear the value

      //Handle setting internal v: variables separately where needed to prevent changing the type.
      int type_error = FALSE;
      if (ht == &eeglVarsHt && !before_set_vvar(varname, di, tv, copy, &type_error)) {
         if (type_error)
            showErrFmtMsg(_(e_setting_v_str_to_value_with_wrong_type), varname);
         goto failed;
      }

      clearVar(&di->c);
   } else {
      //Item not found, check if a function already exists.
      if (is_script_local && (flags & (ASSIGN_NO_DECL | ASSIGN_DECL)) == 0
             && lookup_scriptitem(name, FALSE) == OK
      ) {
         showErrFmtMsg(_(e_redefining_script_item_str), name);
         goto failed;
      }

      if (check_hashtab_frozen(ht, S"add variable"))
         goto failed;

      //Can't add "v:" or "a:" variable.
      if (ht == &eeglVarsHt || ht == get_funccal_args_ht()) {
         showErrFmtMsg(_(e_illegal_variable_name_str), name);
         goto failed;
      }

      Text varNameT = mbText(varname);
      if (!valid_varname(varNameT, true))
         goto failed;

      di = alloc(offsetof(DictItem, key) + varNameT.len + 1);
      STRCPY(di->key, varname);
      di->len = varNameT.len;
      if (hash_add(ht, textOfDi(di), S"add variable") == FAIL) {
         eeglFree(di);
         goto failed;
      }
      di->flags = DI_FLAGS_ALLOC;
      if ((flags & (ASSIGN_CONST | ASSIGN_FINAL)) > 0)
         di->flags |= DI_FLAGS_LOCK;
   }

   Var* dest_tv = &di->c;
   if (copy || tv->tag == VAR_NUMBER || tv->tag == VAR_FLOAT)
      copy_tv(OUT dest_tv, tv);
   else {
      *dest_tv = *tv;
      dest_tv->lock = 0;
      initVarToNull(OUT tv);
   }
   freeNewValue = false;

   //":const var = value" locks the value
   //":final var = value" locks "var"
   if ((flags & ASSIGN_CONST) > 0)
      //Like :lockvar! name: lock the value and what it contains, but only
      //if the reference count is up to one. That locks only literal values.
      item_lock(dest_tv, DICT_MAXNEST, TRUE, TRUE);

   retStatus = OK;

failed:
   eeglFree(name_tofree);
   if (freeNewValue)
      clearVar(newValue);

   return retStatus;
}

// Check in this order for backwards compatibility:
// - Whether the variable is read-only
// - Whether the variable value is locked
// - Whether the variable is locked
// NOTE: "name" is only used for error messages.
int
var_check_permission(DictItem *di, CS name) {
   if (var_check_ro(di->flags, mbText(name), false)
             || value_check_lock(di->c.lock, mbText(name), false)
             || var_check_lock(di->flags, mbText(name), FALSE))
      return FAIL;
   return OK;
}

// Return TRUE if flags "flags" indicates variable "name" is read-only.
// Also give an error message.
Boole
var_check_ro(int flags, Text name, Boole use_gettext) {
   if (flags & DI_FLAGS_RO) {
      if (name.len == 0)
         emsg(_(e_cannot_change_readonly_variable));
      else
         showErrFmtMsg(_(e_cannot_change_readonly_variable_str), use_gettext 
               ? _(name.c) : name.c);
      return true;
   }
   return false;
}

// Return TRUE if flags "flags" indicates variable "name" is locked. Also give an error message.
int
var_check_lock(Unt flags, Text name, Boole use_gettext) {
   if (flags & DI_FLAGS_LOCK) {
      showErrFmtMsg(_(e_variable_is_locked_str), use_gettext ? _(name.c) : name.c);
      return true;
   }
   return false;
}

// Return TRUE if flags "flags" indicates variable "name" is fixed. Also give an error message.
Boole
var_check_fixed(int flags, Text name, Boole use_gettext) {
   if (flags & DI_FLAGS_FIX) {
      if (name.len == 0)
          emsg(_(e_cannot_delete_variable));
      else
          showErrFmtMsg(_(e_cannot_delete_variable_str), use_gettext ? text(_(name.c)) : name);
      return true;
   }
   return false;
}


// Return TRUE if "flags" indicates variable "name" has a locked (immutable)
// value.  Also give an error message, using "name" or _("name") when "use_gettext" is TRUE.
Boole
value_check_lock(int lock, Text name, Boole use_gettext) {
   if (lock & VAR_LOCKED) {
      if (name.len == 0)
         emsg(_(e_value_is_locked));
      else
         showErrFmtMsg(_(e_value_is_locked_str), use_gettext ? _(name.c) : name.c);
      return true;
   }
   if (lock & VAR_FIXED) {
      if (name.len == 0)
          emsg(_(e_cannot_change_value));
      else
          showErrFmtMsg(_(e_cannot_change_value_of_str), use_gettext ? _(name.c) : name.c);
      return true;
   }
   return false;
}

//Check if a variable name is valid.  When "autoload" is true "#" is allowed.
//If "len" is -1 use all of "varname", otherwise up to "varname[len]". Return FALSE and give an 
//error if not.
Boole
valid_varname(Text varname, Boole autoload) {
   for (CS p = varname.c; p < varname.c + varname.len; ++p) {
      if (!isValidForScriptName1(*p) 
            && (p == varname.c || !EE_ISDIGIT(*p)) && !(autoload && *p == AUTOLOAD_CHAR)) {
         showErrFmtMsg(_(e_illegal_variable_name_str), varname.c);
         return false;
      }
   }
   return true;
}

// Implement the logic to retrieve local variable and option values.
// Used by "getwinvar()" "gettabvar()" "gettabwinvar()" "getbufvar()".
private void
getVarFrom(
   CS varname,
   Var* returnVar,
   Var* deftv,       // Default value if not found.
   VarLevel level,
   Tab* t,       // can be NULL
   Portal* port,
   Book* book // Ignored if htname is not 'b'.
) {     
   DictItem   *v;
   int      done = FALSE;
   SwitchPort   switchPort;
   int      needSwitchPortal;
   int      do_change_curbuf = book && level == VAR_BOOK;

   ++emsg_off;

   returnVar->tag = VAR_STRING;
   returnVar->string = NULL;

   if (varname && t && port && (level != VAR_BOOK || book)) {
      //Set curPor to be our portal, temporarily.  Also set the tab, otherwise the portal is not 
      //valid. Only do this when needed, autocommands get blocked. If we have a book reference 
      //avoid the switching, we're saving and restoring curBook directly.
      needSwitchPortal = !(t == curtab && port == curPor) && !do_change_curbuf;
      if (!needSwitchPortal || portSwitch(&switchPort, port, t, TRUE) == OK) {
         //Handle options. There are no tab-local options.
         if (*varname == '&' && level == VAR_TAB) {
            Book   *save_curbuf = curBook;

            //Change curBook so the option is read from the correct book.
            if (do_change_curbuf)
               curBook = book;

            if (varname[1] == ZERO) {
               // get all portal-local or book-local options in a dict
               Bag* opts = getBufOrPortOptions(level == VAR_BOOK);

               if (opts) {
                  returnVar_dict_set(returnVar, opts);
                  done = TRUE;
               }
            } ei (eval_option(&varname, returnVar, TRUE) == OK)
               //Local option
               done = TRUE;

            curBook = save_curbuf;
         } ei (*varname == ZERO) {
            //Empty string: return a dict with all the local variables.
            if (level == VAR_BOOK)
               v = &book->bookVar;
            ei (level == VAR_PORTAL)
               v = &port->wVar;
            else
               v = &t->tabVar;
            copy_tv(OUT returnVar, &v->c);
            done = TRUE;
         } else {
            EeSet* ht;
            if (level == VAR_BOOK)
               ht = &book->bVars->hashTable;
            ei (level == VAR_PORTAL)
               ht = &port->internalVars->hashTable;
            else
               ht = &t->vars->hashTable;

            // Look up the variable.
            v = findVar_in_ht(ht, level, mbText(varname), false);
            if (v) {
               copy_tv(OUT returnVar, &v->c);
               done = TRUE;
            }
         }
      }

      if (needSwitchPortal)
         //restore previous notion of curPor
         portRestore(&switchPort, TRUE);
   }

   if (!done && deftv->tag != VAR_UNKNOWN)
      //use the default value
      copy_tv(OUT returnVar, deftv);

   --emsg_off;
}

// getwinvar() and gettabwinvar()
private void
getPortalVar(
   Var* argvars,
   Var* returnVar,
   int off       // 1 for gettabwinvar()
){
   Tab* t;
   if (off == 1)
      t = getTab((int)varGetNumberChk(argvars, NULL));
   else
      t = curtab;
   Portal* port = portFindByNr(&argvars[off], t);
   CS varname = convertVarToStringSingleUse(&argvars[off + 1]);

   getVarFrom(varname, returnVar, &argvars[off + 2], 'w', t, port, NULL);
}

// "setwinvar()" and "settabwinvar()" functions
private void
setPortVar(Var *argvars, int off) {
   Tab* t = off == 1 ? getTab((int)varGetNumberChk(argvars, NULL)) : curtab;
   Portal* port = portFindByNr(&argvars[off], t);
   CS varname = convertVarToStringSingleUse(&argvars[off + 1]);
   Var* varp = &argvars[off + 2];

   if (!port || !varname)
      return;

   int needSwitchPortal = !(t == curtab && port == curPor);
   SwitchPort switchPort;
   if (!needSwitchPortal || portSwitch(OUT &switchPort, port, t, TRUE) == OK) {
      if (*varname == '&')
         optSetFromVar(varname + 1, varp);
      else {
         CS portVarname = alloc(STRLEN(varname) + 3);
         STRCPY(portVarname, "w:");
         STRCPY(portVarname + 2, varname);
         set_var(text(portVarname), varp, true);
         eeglFree(portVarname);
      }
   }
   if (needSwitchPortal)
      portRestore(OUT &switchPort, TRUE);
}

// Add an assert error to v:errors.
void
assert_error(ArrayList *gap) {
   EeglVar* vp = &eeglVars[VV_ERRORS];

   if (vp->entry.c.tag != VAR_LIST || eeglVars[VV_ERRORS].entry.c.list == NULL)
      // Make sure v:errors is a list.
      set_EeglVar_list(VV_ERRORS, list_alloc());
   list_append_string(eeglVars[VV_ERRORS].entry.c.list, gap->c, gap->len);
}

int
var_exists(CS var) {
   CS arg = var;
   CS tofree;
   int n = FALSE;

   // get_name_len() takes care of expanding curly braces
   CS name = var;
   int len = get_name_len(OUT &arg, OUT &tofree, TRUE, FALSE);
   if (len > 0) {
      if (tofree)
         name = tofree;
      Var tv;
      n = (eval_variable((Text){name, len}, &tv, NULL, EVAL_VAR_NOAUTOLOAD) == OK);
      if (n) {
         // handle d.key, l[idx], f(expr)
         arg = skipwhite(arg);
         n = (handle_subscript(&arg, &tv, &EVALARG_EVALUATE, FALSE) == OK);
         if (n)
            clearVar(&tv);
      }
   }
   if (*arg != ZERO)
      n = FALSE;

   eeglFree(tofree);
   return n;
}

private Lval   *redirLvalS = NULL;
#define EVALCMD_BUSY (redirLvalS == (Lval *)&redirLvalS)
private ArrayList redirArrayList;   // only valid when redirLvalS is not NULL
private CS redirNameEndS = NULL;
private CS redirVarnameS = NULL;

int
alloc_redirLvalS(void) {
   redirLvalS = ALLOC_CLEAR_ONE(Lval);
   if (redirLvalS == NULL)
      return FAIL;
   return OK;
}

void
clear_redirLvalS(void) {
   EE_CLEAR(redirLvalS);
}

void
init_redirArrayList(void) {
   ga_init2(&redirArrayList, sizeof(char), 500);
}

private CS
getRedirLval() {
   return getLval(OUT redirLvalS, (GetLval){
         .name = mbText(redirVarnameS), .returnVar = null, .unlet = false, .skip = false,
         .flags = 0, .fneFlag = FNE_CHECK_START
      }
   );
}

//Start recording command output to a variable. When "append" is TRUE append to an existing 
//variable. Return OK if successfully completed the setup. FAIL otherwise.
int
var_redir_start(CS name, int append) {
   // Catch a bad name early.
   if (!isValidForScriptName1(*name)) {
      emsg(_(e_invalid_argument));
      return FAIL;
   }

   // Make a copy of the name, it is used in redirLvalS until redir ends.
   redirVarnameS = copyStr(name);

   if (alloc_redirLvalS() == FAIL) {
      var_redir_stop();
      return FAIL;
   }

   // The output is stored in growarray "redirArrayList" until redirection ends.
   init_redirArrayList();

   // Parse the variable name (can be a dict or list entry).
   redirNameEndS = getRedirLval();
   if (!redirNameEndS || *redirNameEndS != ZERO || redirLvalS->name.len == 0) {
      clear_lval(OUT redirLvalS);
      if (redirNameEndS && *redirNameEndS != ZERO) {
         // Trailing characters are present after the variable name
         showErrFmtMsg(_(e_trailing_characters_str), redirNameEndS);
      } else {
         showErrFmtMsg(_(e_invalid_argument_str), name);
      } 
      redirNameEndS = NULL;  // don't store a value, only cleanup
      var_redir_stop();
      return FAIL;
   }

   // check if we can write to the variable: set it to or append an empty string
   int called_emsg_before = called_emsg;
   Var tv;
   tv.tag = VAR_STRING;
   tv.string = E;
   letImpl(OUT redirLvalS, &tv, TRUE, ASSIGN_NO_DECL, append ? S"." : S"=");
   clear_lval(OUT redirLvalS);
   if (called_emsg > called_emsg_before) {
      redirNameEndS = NULL;  // don't store a value, only cleanup
      var_redir_stop();
      return FAIL;
   }

   return OK;
}

//Append "value[value_len]" to the variable set by var_redir_start().
//The actual appending is postponed until redirection ends, because the value
//appended may in fact be the string we write to, changing it may cause freed memory to be used:
//   :redir => foo
//   :let foo
//   :redir END
void
var_redir_str(CS value, int value_len) {
   if (!redirLvalS)
      return;

   int len;
   if (value_len == -1)
      len = (int)STRLEN(value);   // Append the entire string
   else
      len = value_len;      // Append only "value_len" characters

   if (ga_grow(&redirArrayList, len) == OK) {
      mch_memmove((char *)redirArrayList.c + redirArrayList.len, value, len);
      redirArrayList.len += len;
   } else
      var_redir_stop();
}

// Stop redirecting command output to a variable. Free the allocated memory.
void
var_redir_stop(void) {
   if (EVALCMD_BUSY) {
      redirLvalS = NULL;
      return;
   }

   if (redirLvalS) {
      // If there was no error: assign the text to the variable.
      if (redirNameEndS != NULL) {
         ga_append(&redirArrayList, ZERO);  // Append the trailing ZERO.
         Var tv = {};
         tv.tag = VAR_STRING;
         tv.string = redirArrayList.c;
         // Call getLval() again, if it's inside a Bag or List it may have changed.
         redirNameEndS = getRedirLval();
         if (redirNameEndS && redirLvalS->name.len > 0)
            letImpl(OUT redirLvalS, &tv, FALSE, 0, S".");
         clear_lval(OUT redirLvalS);
      }

      // free the collected output
      EE_CLEAR(redirArrayList.c);
      EE_CLEAR(redirLvalS);
   }
   EE_CLEAR(redirVarnameS);
}

// Get the collected redirected text and clear redirArrayList.
CS
get_clear_redirArrayList(void) {
   ga_append(&redirArrayList, ZERO);  // Append the trailing ZERO.
   CS res = redirArrayList.c;
   redirArrayList.c = NULL;
   return res;
}

// "mode()" function
void
f_mode(Var *argvars, Var *returnVar) {
   Byte buf[MODE_MAX_LENGTH];

   get_mode(buf);

   // Clear out the minor mode when the argument is not a non-zero number or non-empty string.
   if (!non_zero_arg(&argvars[0]))
      buf[1] = ZERO;

   returnVar->string = copyStr(buf);
   returnVar->tag = VAR_STRING;
}

private void
may_add_state_char(ArrayList *gap, CS include, int c) {
   if (!include || firstOccurrence(include, c) != NULL)
      ga_append(gap, c);
}


// "state()" function
void
f_state(Var *argvars, Var *returnVar) {
   ArrayList   ga;
   Byte   *include = NULL;

   ga_init2(&ga, 1, 20);
   if (argvars[0].tag != VAR_UNKNOWN)
      include = tv_get_string(&argvars[0]);

   if (!(stuff_empty() && typeBufG.validLen == 0 && scriptin[curscript] == NULL))
      may_add_state_char(&ga, include, 'm');
   if (op_pending())
      may_add_state_char(&ga, include, 'o');
   if (autocmd_busy)
      may_add_state_char(&ga, include, 'x');
   if (ins_compl_active())
      may_add_state_char(&ga, include, 'a');

   if (channel_in_blocking_wait())
      may_add_state_char(&ga, include, 'w');
   if (!get_was_safe_state())
      may_add_state_char(&ga, include, 'S');
   for (int i = 0; i < get_callback_depth() && i < 3; ++i)
      may_add_state_char(&ga, include, 'c');
   if (msg_scrolled > 0)
      may_add_state_char(&ga, include, 's');

   returnVar->tag = VAR_STRING;
   returnVar->string = ga.c;
}

void
f_gettabvar(Var *argvars, Var *returnVar) {
   Byte   *varname;
   Tab   *t;
   Portal   *port = NULL;

   varname = convertVarToStringSingleUse(&argvars[1]);
   t = getTab((int)varGetNumberChk(argvars, NULL));
   if (t)
      port = t == curtab || t->firstPor == NULL ? firstPor : t->firstPor;

   getVarFrom(varname, returnVar, &argvars[2], 't', t, port, NULL);
}

void
f_gettabwinvar(Var *argvars, Var *returnVar) {
   getPortalVar(argvars, returnVar, 1);
}

void
f_getwinvar(Var *argvars, Var *returnVar) {
   getPortalVar(argvars, returnVar, 0);
}

void
f_getbufvar(Var *argvars, Var *returnVar) {
   CS varname = convertVarToStringSingleUse(&argvars[1]);
   Book* book = daGetBookFromArg(&argvars[0]);

   getVarFrom(varname, returnVar, &argvars[2], 'b', curtab, curPor, book);
}

void
f_settabvar(Var *argvars, Var *returnVar UNUSED) {
   Tab* t = getTab((int)varGetNumberChk(argvars, NULL));
   CS varname = convertVarToStringSingleUse(&argvars[1]);
   Var* varp = &argvars[2];

   if (!varname || !t)
      return;

   Tab* save_curtab = curtab;
   Tab* save_lu_tp = lastUsedTabG;
   gotoTab(t, FALSE, FALSE);

   CS tabvarname = alloc(STRLEN(varname) + 3);
   STRCPY(tabvarname, "t:");
   STRCPY(tabvarname + 2, varname);
   set_var(text(tabvarname), varp, true);
   eeglFree(tabvarname);

   // Restore current tabpage and last accessed tabpage.
   if (isTabValid(save_curtab)) {
      gotoTab(save_curtab, FALSE, FALSE);
      if (isTabValid(save_lu_tp))
          lastUsedTabG = save_lu_tp;
   }
}

void
f_settabwinvar(Var *argvars, Var *returnVar UNUSED) {
   setPortVar(argvars, 1);
}

void
f_setwinvar(Var *argvars, Var *returnVar UNUSED) {
   setPortVar(argvars, 0);
}

void
f_setbufvar(Var *argvars, Var *returnVar UNUSED) {
   CS varname = convertVarToStringSingleUse(&argvars[1]);
   Book* book = daGetBookFromArg(&argvars[0]);
   Var* varp = &argvars[2];

   if (!book || varname == NULL)
      return;

   if (*varname == '&') {
      AutocommSave   aco;
      //safe the current portal position, it could change because of 'scrollbind' portal-local
      //options
      LineNr old_topline = curPor->topLine;

      // Set curBook to be our book, temporarily.
      auCommPrepareBook(&aco, book);
      if (curBook == book) {
         // Only when it worked to set "curBook".
         optSetFromVar(varname + 1, varp);

         // reset notion of book
         auCommRestoreBuf(&aco);
      }
      curPor->topLine = old_topline;
   } else {
      CS bufvarname = alloc(STRLEN(varname) + 3);
      Book *save_curbuf = curBook;
      curBook = book;
      STRCPY(bufvarname, "b:");
      STRCPY(bufvarname + 2, varname);
      set_var(text(bufvarname), varp, true);
      eeglFree(bufvarname);
      curBook = save_curbuf;
   }
}

//}}}
//{{{var callbacks

// Get a callback from "arg".  It can be a Funcref or a function name. When "arg" is zero 
// "res.name" is set to an empty string. If "res.name" is allocated then 
// "res.needsFreeing" is set to TRUE. "res.name" is set to NULL for an invalid argument.
Callback
get_callback(Var *arg) {
   int r = OK;

   Callback  res;
   CLEAR_FIELD(res);
   if (arg->tag == VAR_PARTIAL && arg->partial) {
      res.cb_partial = arg->partial;
      ++res.cb_partial->refcount;
      res.name = partial_name(res.cb_partial);
   } else {
      if (arg->tag == VAR_STRING && arg->string && SAFE_isdigit(*arg->string))
         r = FAIL;
      ei (arg->tag == VAR_FUNC || arg->tag == VAR_STRING) {
         res.name = arg->string;
         if (arg->tag == VAR_STRING) {
            CS name = get_scriptlocal_funcname(arg->string);
            if (name) {
               res.name = name;
               res.needsFreeing = TRUE;
            }
         }
         func_ref(res.name);
      } ei (arg->tag == VAR_NUMBER && arg->number == 0)
         res.name = Em;
      else
         r = FAIL;

      if (r == FAIL) {
          emsg(_(e_invalid_callback_argument));
          res.name = NULL;
      }
   }
   return res;
}

// Copy a callback into a Var.
void
putCallback(OUT Var* tv, Callback* cb) {
   if (cb->cb_partial) {
      tv->tag = VAR_PARTIAL;
      tv->partial = cb->cb_partial;
      ++tv->partial->refcount;
   } else {
      tv->tag = VAR_FUNC;
      tv->string = copyStr(cb->name);
      func_ref(cb->name);
   }
}

// Make a copy of "src" into "dest", allocating the function name if needed,
// without incrementing the refcount.
void
set_callback(Callback *dest, Callback *src) {
   if (src->cb_partial == NULL) {
      // just a function name, make a copy
      dest->name = copyStr(src->name);
      dest->needsFreeing = true;
   } else {
      // name is a pointer into cb_partial
      dest->name = src->name;
      dest->needsFreeing = false;
   }
   dest->cb_partial = src->cb_partial;
}

// Copy callback from "src" to "dest", incrementing the refcounts.
void
evCopyCallback(OUT Callback* dest, Callback* src) {
   dest->cb_partial = src->cb_partial;
   if (dest->cb_partial) {
      dest->name = src->name;
      dest->needsFreeing = false;
      ++dest->cb_partial->refcount;
   } else {
      dest->name = copyStr(src->name);
      dest->needsFreeing = true;
      func_ref(src->name);
   }
}

// Unref/free "callback" returned by get_callback() or set_callback().
void
evFreeCallback(Callback *callback) {
   if (callback->cb_partial) {
      partial_unref(callback->cb_partial);
      callback->cb_partial = NULL;
   } ei (callback->name) {
      func_unref(callback->name);
   } 
   if (callback->needsFreeing) {
      eeglFree(callback->name);
      callback->needsFreeing = false;
   }
   callback->name = NULL;
}

//}}}
//{{{internal declarations

//Array with names and number of arguments of all internal functions
//MUST BE KEPT SORTED IN strcmp() ORDER FOR BINARY SEARCH!
//
//The builtin function may be varargs. In that case
//  - f_max_argc == VARGS
//  - For varargs, f_argcheck must be NULL terminated. The last non-null
//    entry in f_argcheck should validate all the remaining args.
typedef struct {
   CS f_name;   // function name
   Byte f_min_argc;   // minimal number of arguments
   Byte f_max_argc;   // maximal number of arguments
   Byte f_argtype;   // for method: FEARG_ values; bits FE_
   void   (*f_func)(Var *args, Var *rvar); // implementation of function
} BuiltinFn;

// Set f_max_argc to VARGS for varargs.
#define VARGS    CHAR_MAX

// values for f_argtype; zero means it cannot be used as a method
#define FEARG_1       0x01    // base is the first argument
#define FEARG_2     0x02    // base is the second argument
#define FEARG_3     0x03    // base is the third argument
#define FEARG_4     0x04    // base is the fourth argument
#define FEARG_MASK  0x0F    // bits in f_argtype used as argument index
#define FE_X       0x10    // builtin accepts a non-value (class, typealias)

# define MATH_FUNC(name) name
# define TIMER_FUNC(name) name
#define TERM_FUNC(name) name

//}}}
//{{{global functions

private BuiltinFn globalFunctions[] = {
   {S"abs",      1, 1, FEARG_1, &f_abs},
   {S"acos",     1, 1, FEARG_1, &f_acos},
   {S"add",      2, 2, FEARG_1, &f_add},
   {S"and",      2, 2, FEARG_1, &f_and},
   {S"append",   2, 2, FEARG_2, &f_append},
   {S"appendbufline",   3, 3, FEARG_3, &f_appendbufline},
   {S"argc",     0, 1, 0, &f_argc},
   {S"argidx",   0, 0, 0, &f_argidx},
   {S"arglistid", 0, 2, 0, &f_arglistid},
   {S"argv",      0, 2, 0,  &f_argv},
   {S"asin",      1, 1, FEARG_1, &f_asin},
   {S"assert_equal", 2, 3, FEARG_2, &f_assert_equal},
   {S"assert_equalfile", 2, 3, FEARG_1, &f_assert_equalfile},
   {S"assert_exception", 1, 2, 0, &f_assert_exception},
   {S"assert_fails", 1, 5, FEARG_1, &f_assert_fails},
   {S"assert_false", 1, 2, FEARG_1, &f_assert_false},
   {S"assert_inrange", 3, 4, FEARG_3, &f_assert_inrange},
   {S"assert_match",  2, 3, FEARG_2, &f_assert_match},
   {S"assert_notequal", 2, 3, FEARG_2, &f_assert_notequal},
   {S"assert_notmatch", 2, 3, FEARG_2, &f_assert_notmatch},
   {S"assert_report",   1, 1, FEARG_1, &f_assert_report},
   {S"assert_true",   1, 2, FEARG_1, &f_assert_true},
   {S"atan",      1, 1, FEARG_1,    &f_atan},
   {S"atan2",     2, 2, FEARG_1,  &f_atan2},
   {S"autocmd_add",   1, 1, FEARG_1, &f_autocmd_add},
   {S"autocmd_delete",   1, 1, FEARG_1, &f_autocmd_delete},
   {S"autocmd_get",   0, 1, FEARG_1, &f_autocmd_get},
   {S"balloon_gettext",   0, 0, 0, &f_balloon_gettext },
   {S"balloon_show",   1, 1, FEARG_1, &f_balloon_show },
   {S"balloon_split",   1, 1, FEARG_1, &f_balloon_split },
   {S"base64_decode",   1, 1, FEARG_1, &f_base64_decode},
   {S"base64_encode",   1, 1, FEARG_1, &f_base64_encode},
   {S"bindtextdomain",   2, 2, 0,  &f_bindtextdomain},
   {S"blob2list",   1, 1, FEARG_1, &f_blob2list},
   {S"blob2str",   1, 2, FEARG_1, &f_blob2str},
   {S"browse",      4, 4, 0,   &f_browse},
   {S"browsedir",   2, 2, 0,   &f_browsedir},
   {S"bufadd",      1, 1, FEARG_1, &f_bufadd},
   {S"bufexists",   1, 1, FEARG_1,  &f_bufexists},
   {S"buflisted",   1, 1, FEARG_1, &f_buflisted},
   {S"bufload",      1, 1, FEARG_1, &f_bufload},
   {S"bufloaded",   1, 1, FEARG_1, &f_bufloaded},
   {S"bufname",      0, 1, FEARG_1, &f_bufname},
   {S"bufnr",      0, 2, FEARG_1,      &f_bufnr},
   {S"bufwinid",   1, 1, FEARG_1,       &f_bufwinid},
   {S"bufwinnr",   1, 1, FEARG_1,        &f_bufwinnr},
   {S"byte2line",   1, 1, FEARG_1,       &f_byte2line},
   {S"byteidx",      2, 3, FEARG_1,        &f_byteidx},
   {S"byteidxcomp",   2, 3, FEARG_1,       &f_byteidxcomp},
   {S"call",      2, 3, FEARG_1,           &f_call},
   {S"ceil",      1, 1, FEARG_1,           &f_ceil},
   {S"ch_canread",   1, 1, FEARG_1,     &f_ch_canread},
   {S"ch_close",   1, 1, FEARG_1,          &f_ch_close},
   {S"ch_close_in",   1, 1, FEARG_1,       &f_ch_close_in},
   {S"ch_evalexpr",   2, 3, FEARG_1,       &f_ch_evalexpr},
   {S"ch_evalraw",   2, 3, FEARG_1,        &f_ch_evalraw},
   {S"ch_getbufnr",   2, 2, FEARG_1,       &f_ch_getbufnr},
   {S"ch_getjob",   1, 1, FEARG_1,         &f_ch_getjob},
   {S"ch_info",      1, 1, FEARG_1,        &f_ch_info},
   {S"ch_log",      1, 2, FEARG_1,         &f_ch_log},
   {S"ch_logfile",   1, 2, FEARG_1,        &f_ch_logfile},
   {S"ch_open",      1, 2, FEARG_1,        &f_ch_open},
   {S"ch_read",      1, 2, FEARG_1,        &f_ch_read},
   {S"ch_readblob",   1, 2, FEARG_1,       &f_ch_readblob},
   {S"ch_readraw",   1, 2, FEARG_1,        &f_ch_readraw},
   {S"ch_sendexpr",   2, 3, FEARG_1,       &f_ch_sendexpr},
   {S"ch_sendraw",   2, 3, FEARG_1,        &f_ch_sendraw},
   {S"ch_setoptions",   2, 2, FEARG_1,     &f_ch_setoptions},
   {S"ch_status",   1, 2, FEARG_1,             &f_ch_status},
   {S"changenr",   0, 0, 0,          &f_changenr},
   {S"char2nr",      1, 2, FEARG_1,        &f_char2nr},
   {S"charclass",   1, 1, FEARG_1,         &f_charclass},
   {S"charcol",      1, 2, FEARG_1,        &f_charcol},
   {S"charidx",      2, 4, FEARG_1,        &f_charidx},
   {S"chdir",      1, 2, FEARG_1,          &f_chdir},
   {S"clearmatches",   0, 1, FEARG_1,        &f_clearmatches},
   {S"cmdcomplete_info",0, 0, 0,           &f_cmdcomplete_info},
   {S"col",      1, 2, FEARG_1,            &f_col},
   {S"complete",   2, 2, FEARG_2,          &f_complete},
   {S"complete_add",   1, 1, FEARG_1,        &f_complete_add},
   {S"complete_check",   0, 0, 0,       &f_complete_check},
   {S"complete_info",   0, 1, FEARG_1,        &f_complete_info},
   {S"complete_match",   0, 2, 0,        &f_complete_match},
   {S"confirm",      1, 4, FEARG_1,       &f_confirm},
   {S"copy",      1, 1, FEARG_1,          &f_copy},
   {S"cos",      1, 1, FEARG_1,           &f_cos},
   {S"cosh",      1, 1, FEARG_1,          &f_cosh},
   {S"count",      2, 4, FEARG_1,         &f_count},
   {S"cscope_connection",0,3, 0,          &f_cscope_connection},
   {S"cursor",      1, 3, FEARG_1,        &f_cursor},
   {S"deepcopy",   1, 2, FEARG_1,        &f_deepcopy},
   {S"delete",      1, 2, FEARG_1,     &f_delete},
   {S"deletebufline",   2, 3, FEARG_1,    &f_deletebufline},
   {S"did_filetype",   0, 0, 0,     &f_did_filetype},
   {S"diff",      2, 3, FEARG_1,   &f_diff},
   {S"diff_filler",   1, 1, FEARG_1,        &f_diff_filler},
   {S"diff_hlID",   2, 2, FEARG_1,          &f_diff_hlID},
   {S"echoraw",      1, 1, FEARG_1,         &f_echoraw},
   {S"empty",      1, 1, FEARG_1,        &f_empty},
   {S"environ",      0, 0, 0,      &f_environ},
   {S"err_teapot",   0, 1, 0,      &f_err_teapot},
   {S"escape",      2, 2, FEARG_1,        &f_escape},
   {S"eval",      1, 1, FEARG_1,          &f_eval},
   {S"eventhandler",   0, 0, 0,       &f_eventhandler},
   {S"executable",   1, 1, FEARG_1,       &f_executable},
   {S"execute",      1, 2, FEARG_1,       &f_execute},
   {S"exepath",      1, 1, FEARG_1,        &f_exepath},
   {S"exists",      1, 1, FEARG_1,      &f_exists},
   {S"exp",      1, 1, FEARG_1,         &f_exp},
   {S"expand",      1, 3, FEARG_1,       &f_expand},
   {S"expandcmd",   1, 2, FEARG_1,       &f_expandcmd},
   {S"extend",      2, 3, FEARG_1,       &f_extend},
   {S"extendnew",   2, 3, FEARG_1,       &f_extendnew},
   {S"feedkeys",   1, 2, FEARG_1,         &f_feedkeys},
   {S"filecopy",   2, 2, FEARG_1,     &f_filecopy},
   {S"filereadable",   1, 1, FEARG_1,     &f_filereadable},
   {S"filewritable",   1, 1, FEARG_1,        &f_filewritable},
   {S"filter",      2, 2, FEARG_1,         &f_filter},
   {S"finddir",      1, 3, FEARG_1,        &f_finddir},
   {S"findfile",   1, 3, FEARG_1,          &f_findfile},
   {S"flatten",      1, 2, FEARG_1,        &f_flatten},
   {S"flattennew",   1, 2, FEARG_1,       &f_flattennew},
   {S"float2nr",   1, 1, FEARG_1,         &f_float2nr},
   {S"floor",      1, 1, FEARG_1,         &f_floor},
   {S"fmod",      2, 2, FEARG_1,          &f_fmod},
   {S"fnameescape",   1, 1, FEARG_1,        &f_fnameescape},
   {S"fnamemodify",   2, 2, FEARG_1,        &f_fnamemodify},
   {S"foldclosed",   1, 1, FEARG_1,         &f_foldclosed},
   {S"foldclosedend",   1, 1, FEARG_1,       &f_foldclosedend},
   {S"foldlevel",   1, 1, FEARG_1,           &f_foldlevel},
   {S"foldtext",   0, 0, 0,        &f_foldtext},
   {S"foldtextresult",   1, 1, FEARG_1,        &f_foldtextresult},
   {S"foreach",      2, 2, FEARG_1,          &f_foreach},
   {S"fullcommand",   1, 2, FEARG_1,        &f_fullcommand},
   {S"funcref",      1, 3, FEARG_1,     &f_funcref},
   {S"function",   1, 3, FEARG_1,      &f_function},
   {S"garbagecollect",   0, 1, 0,          &f_garbagecollect},
   {S"get",      2, 3, FEARG_1,          &f_get},
   {S"getbufinfo",   0, 1, FEARG_1,   &f_getbufinfo},
   {S"getbufline",   2, 3, FEARG_1,     &f_getbufline},
   {S"getbufoneline",   2, 2, FEARG_1,        &f_getbufoneline},
   {S"getbufvar",   2, 3, FEARG_1,          &f_getbufvar},
   {S"getcellpixels",   0, 0, 0,            &f_getcellpixels},
   {S"getchangelist",   0, 1, FEARG_1,       &f_getchangelist},
   {S"getchar",      0, 2, 0,          &f_getchar},
   {S"getcharmod",   0, 0, 0,          &f_getcharmod},
   {S"getcharpos",   1, 1, FEARG_1,     &f_getcharpos},
   {S"getcharsearch",   0, 0, 0,         &f_getcharsearch},
   {S"getcharstr",   0, 2, 0,            &f_getcharstr},
   {S"getcmdcomplpat",   0, 0, 0,        &f_getcmdcomplpat},
   {S"getcmdcompltype",   0, 0, 0,       &f_getcmdcompltype},
   {S"getcmdline",   0, 0, 0,         &f_getCommline},
   {S"getcmdpos",   0, 0, 0,          &f_getcmdpos},
   {S"getcmdprompt",   0, 0, 0,       &f_getcmdprompt},
   {S"getcmdscreenpos",   0, 0, 0,        &f_getcmdscreenpos},
   {S"getcmdtype",   0, 0, 0,          &f_getcmdtype},
   {S"getcmdwintype",   0, 0, 0,       &f_getcmdwintype},
   {S"getcompletion",   2, 3, FEARG_1,     &f_getcompletion},
   {S"getcompletiontype", 1, 1, FEARG_1,       &f_getcompletiontype},
   {S"getcurpos",   0, 1, FEARG_1,       &f_getcurpos},
   {S"getcursorcharpos", 0, 1, FEARG_1,     &f_getcursorcharpos},
   {S"getcwd",      0, 2, FEARG_1,           &f_getcwd},
   {S"getenv",      1, 1, FEARG_1,           &f_getenv},
   {S"getfontname",   0, 1, 0,           &f_getfontname},
   {S"getfperm",   1, 1, FEARG_1,        &f_getfperm},
   {S"getfsize",   1, 1, FEARG_1,        &f_getfsize},
   {S"getftime",   1, 1, FEARG_1,        &f_getftime},
   {S"getftype",   1, 1, FEARG_1,        &f_getftype},
   {S"getjumplist",   0, 2, FEARG_1,        &f_getjumplist},
   {S"getline",      1, 2, FEARG_1,         &f_getline},
   {S"getloclist",   1, 2, 0,     &f_getloclist},
   {S"getmarklist",   0, 1, FEARG_1,   &f_getmarklist},
   {S"getmatches",   0, 1, 0,    &f_getmatches},
   {S"getmousepos",   0, 0, 0,     &f_getmousepos},
   {S"getpid",      0, 0, 0,           &f_getpid},
   {S"getpos",      1, 1, FEARG_1,      &f_getpos},
   {S"getreg",      0, 3, FEARG_1,        &f_getreg},
   {S"getreginfo",   0, 1, FEARG_1,        &f_getreginfo},
   {S"getregion",   2, 3, FEARG_1,      &f_getregion},
   {S"getregionpos",   2, 3, FEARG_1,  &f_getregionpos},
   {S"getregtype",   0, 1, FEARG_1,          &f_getregtype},
   {S"getscriptinfo",   0, 1, 0,      &f_getscriptinfo},
   {S"getstacktrace",   0, 0, 0,      &f_getstacktrace},
   {S"gettabinfo",   0, 1, FEARG_1,   &f_gettabinfo},
   {S"gettabvar",   2, 3, FEARG_1,         &f_gettabvar},
   {S"gettabwinvar",   3, 4, FEARG_1,        &f_gettabwinvar},
   {S"gettagstack",   0, 1, FEARG_1,         &f_gettagstack},
   {S"gettext",      1, 2, FEARG_1,          &f_gettext},
   {S"getwininfo",   0, 1, FEARG_1,     &f_getwininfo},
   {S"getwinpos",   0, 1, FEARG_1,        &f_getwinpos},
   {S"getwinposx",   0, 0, 0,        &f_getwinposx},
   {S"getwinposy",   0, 0, 0,        &f_getwinposy},
   {S"getwinvar",   2, 3, FEARG_1,       &f_getwinvar},
   {S"glob",      1, 4, FEARG_1,         &f_glob},
   {S"glob2regpat",   1, 1, FEARG_1,        &f_glob2regpat},
   {S"globpath",   2, 5, FEARG_2,           &f_globpath},
   {S"has",      1, 2, 0,      &f_has},
   {S"has_key",      2, 2, FEARG_1,      &f_has_key},
   {S"haslocaldir",   0, 2, FEARG_1,        &f_haslocaldir},
   {S"hasmapto",   1, 3, FEARG_1,        &f_hasmapto},
   {S"histadd",      2, 2, FEARG_2,      &f_histadd},
   {S"histdel",      1, 2, FEARG_1,      &f_histdel},
   {S"histget",      1, 2, FEARG_1,         &f_histget},
   {S"histnr",      1, 1, FEARG_1,          &f_histnr},
   {S"hlID",      1, 1, FEARG_1,            &f_hlID},
   {S"hlexists",   1, 1, FEARG_1,        &f_hlexists},
   {S"hlget",      0, 2, FEARG_1,      &f_hlget},
   {S"hostname",   0, 0, 0,           &f_hostname},
   {S"id",      1, 1, FEARG_1,           &f_id},
   {S"indent",      1, 1, FEARG_1,        &f_indent},
   {S"index",      2, 4, FEARG_1,         &f_index},
   {S"indexof",      2, 3, FEARG_1,       &f_indexof},
   {S"input",      1, 3, FEARG_1,         &f_input},
   {S"inputdialog",   1, 3, FEARG_1,        &f_inputdialog},
   {S"inputlist",   1, 1, FEARG_1,          &f_inputlist},
   {S"inputrestore",   0, 0, 0,        &f_inputrestore},
   {S"inputsave",   0, 0, 0,         &f_inputsave},
   {S"inputsecret",   1, 2, FEARG_1,        &f_inputsecret},
   {S"insert",      2, 3, FEARG_1,          &f_insert},
   {S"interrupt",   0, 0, 0,          &f_interrupt},
   {S"invert",      1, 1, FEARG_1,        &f_invert},
   {S"isabsolutepath",   1, 1, FEARG_1,    &f_isabsolutepath},
   {S"isdirectory",   1, 1, FEARG_1,       &f_isdirectory},
   {S"isinf",      1, 1, FEARG_1,             MATH_FUNC(f_isinf)},
   {S"islocked",   1, 1, FEARG_1,          &f_islocked},
   {S"isnan",      1, 1, FEARG_1,          MATH_FUNC(f_isnan)},
   {S"items",      1, 1, FEARG_1,             &f_items},
   {S"job_getchannel",   1, 1, FEARG_1,       &f_job_getchannel},
   {S"job_info",   0, 1, FEARG_1,             &f_job_info},
   {S"job_setoptions",   2, 2, FEARG_1,       &f_job_setoptions},
   {S"job_start",   1, 2, FEARG_1,            &f_startJob},
   {S"job_status",   1, 1, FEARG_1,           &f_job_status},
   {S"job_stop",   1, 2, FEARG_1,          &f_job_stop},
   {S"join",      1, 2, FEARG_1,             &f_join},
   {S"json_decode",   1, 1, FEARG_1,         &f_json_decode},
   {S"json_encode",   1, 1, FEARG_1,         &f_json_encode},
   {S"keys",      1, 1, FEARG_1,         &f_keys},
   {S"keytrans",   1, 1, FEARG_1,          &f_keytrans},
   {S"last_buffer_nr",   0, 0, 0,  &f_last_buffer_nr },   // obsolete
   {S"len",      1, 1, FEARG_1,        &f_len},
   {S"line",      1, 2, FEARG_1,        &f_line},
   {S"line2byte",   1, 1, FEARG_1,        &f_line2byte},
   {S"list2blob",   1, 1, FEARG_1,         &f_list2blob},
   {S"list2str",   1, 2, FEARG_1,          &f_list2str},
   {S"listener_add",   1, 2, FEARG_2,        &f_listener_add},
   {S"listener_flush",   0, 1, FEARG_1,        &f_listener_flush},
   {S"listener_remove",   1, 1, FEARG_1,    &f_listener_remove},
   {S"localtime",   0, 0, 0,          &f_localtime},
   {S"log",      1, 1, FEARG_1,         &f_log},
   {S"log10",      1, 1, FEARG_1,       &f_log10},
   {S"map",      2, 2, FEARG_1,          &f_map},
   {S"maparg",      1, 4, FEARG_1,         &f_maparg},
   {S"mapcheck",   1, 3, FEARG_1,          &f_mapcheck},
   {S"maplist",      0, 1, 0,     &f_maplist},
   {S"mapnew",      2, 2, FEARG_1,        &f_mapnew},
   {S"mapset",      1, 3, FEARG_1,        &f_mapset},
   {S"match",      2, 4, FEARG_1,         &f_match},
   {S"matchadd",   2, 5, FEARG_1,         &f_matchadd},
   {S"matchaddpos",   2, 5, FEARG_1,        &f_matchaddpos},
   {S"matcharg",   1, 1, FEARG_1,        &f_matcharg},
   {S"matchbufline",   4, 5, FEARG_1,        &f_matchbufline},
   {S"matchdelete",   1, 2, FEARG_1,      &f_matchdelete},
   {S"matchend",   2, 4, FEARG_1,            &f_matchend},
   {S"matchfuzzy",   2, 3, FEARG_1,          &f_matchfuzzy},
   {S"matchfuzzypos",   2, 3, FEARG_1,       &f_matchfuzzypos},
   {S"matchlist",   2, 4, FEARG_1,        &f_matchlist},
   {S"matchstr",   2, 4, FEARG_1,            &f_matchstr},
   {S"matchstrlist",   2, 3, FEARG_1,        &f_matchstrlist},
   {S"matchstrpos",   2, 4, FEARG_1,         &f_matchstrpos},
   {S"max",      1, 1, FEARG_1,           &f_max},
   {S"min",      1, 1, FEARG_1,           &f_min},
   {S"mkdir",      1, 3, FEARG_1,      &f_mkdir},
   {S"mode",      0, 1, FEARG_1,          &f_mode},
   {S"nextnonblank",   1, 1, FEARG_1,        &f_nextnonblank},
   {S"ngettext",   3, 4, FEARG_3,            &f_ngettext},
   {S"nr2char",      1, 2, FEARG_1,          &f_nr2char},
   {S"or",      2, 2, FEARG_1,           &f_or},
   {S"pathshorten",   1, 2, FEARG_1,        &f_pathshorten},
   {S"popup_atcursor",   2, 2, FEARG_1,        &f_popup_atcursor},
   {S"popup_beval",   2, 2, FEARG_1,           &f_popup_beval},
   {S"popup_clear",   0, 1, 0,           &f_popup_clear},
   {S"popup_close",   1, 2, FEARG_1,        &f_popup_close},
   {S"createPopup",   2, 2, FEARG_1,        &f_createPopup},
   {S"popup_dialog",   2, 2, FEARG_1,       &f_popup_dialog},
   {S"popup_filter_menu", 2, 2, 0,          &f_popup_filter_menu},
   {S"popup_filter_yesno", 2, 2, 0,         &f_popup_filter_yesno},
   {S"popup_findecho",   0, 0, 0,           &f_popup_findecho},
   {S"popup_findinfo",   0, 0, 0,           &f_popup_findinfo},
   {S"popup_findpreview", 0, 0, 0,          &f_popup_findpreview},
   {S"popup_getoptions", 1, 1, FEARG_1,        &f_popup_getoptions},
   {S"popup_getpos",   1, 1, FEARG_1,          &f_popup_getpos},
   {S"popup_hide",   1, 1, FEARG_1,           &f_popup_hide},
   {S"popup_list",   0, 0, 0,       &f_popup_list},
   {S"popup_locate",   2, 2, 0,             &f_popup_locate},
   {S"popup_menu",   2, 2, FEARG_1,         &f_popup_menu},
   {S"popup_move",   2, 2, FEARG_1,         &f_popup_move},
   {S"popup_notification", 2, 2, FEARG_1,   &f_popup_notification},
   {S"popup_setbuf",   2, 2, FEARG_1,       &f_popup_setbuf},
   {S"popup_setoptions", 2, 2, FEARG_1,     &f_popup_setoptions},
   {S"popup_settext",   2, 2, FEARG_1,      &f_popup_settext},
   {S"popup_show",   1, 1, FEARG_1,         &f_popup_show},
   {S"pow",      2, 2, FEARG_1,           &f_pow},
   {S"prevnonblank",   1, 1, FEARG_1,        &f_prevnonblank},
   {S"printf",      1, 19, FEARG_2,          &f_printf},
   {S"prompt_getprompt", 1, 1, FEARG_1,        &f_prompt_getprompt},
   {S"prompt_setcallback", 2, 2, FEARG_1,        &f_prompt_setcallback},
   {S"prompt_setinterrupt", 2, 2, FEARG_1,       &f_prompt_setinterrupt},
   {S"prompt_setprompt", 2, 2, FEARG_1,          &f_prompt_setprompt},
   {S"prop_add",   3, 3, FEARG_1,            &f_prop_add},
   {S"prop_add_list",   2, 2, FEARG_1,        &f_prop_add_list},
   {S"prop_clear",   1, 3, FEARG_1,           &f_prop_clear},
   {S"prop_find",   1, 2, FEARG_1,            &f_prop_find},
   {S"prop_list",   1, 2, FEARG_1,            &f_prop_list},
   {S"prop_remove",   1, 3, FEARG_1,          &f_prop_remove},
   {S"prop_type_add",   2, 2, FEARG_1,        &f_prop_type_add},
   {S"prop_type_change", 2, 2, FEARG_1,       &f_prop_type_change},
   {S"prop_type_delete", 1, 2, FEARG_1,       &f_prop_type_delete},
   {S"prop_type_get",   1, 2, FEARG_1,        &f_prop_type_get},
   {S"prop_type_list",   0, 1, FEARG_1,       &f_prop_type_list},
   {S"pum_getpos",   0, 0, 0,        &f_pum_getpos},
   {S"pumvisible",   0, 0, 0,        &f_pumvisible},
   {S"rand",      0, 1, FEARG_1,          &f_rand},
   {S"range",      1, 3, FEARG_1,      &f_range},
   {S"readblob",   1, 3, FEARG_1,         &f_readblob},
   {S"readdir",      1, 3, FEARG_1,    &f_readdir},
   {S"readdirex",   1, 3, FEARG_1,   &f_readdirex},
   {S"readfile",   1, 3, FEARG_1,      &f_readfile},
   {S"reduce",      2, 3, FEARG_1,        &f_reduce},
   {S"reg_executing",   0, 0, 0,          &f_reg_executing},
   {S"reg_recording",   0, 0, 0,          &f_reg_recording},
   {S"reltime",      0, 2, FEARG_1,       &f_reltime},
   {S"reltimefloat",   1, 1, FEARG_1,        &f_reltimefloat},
   {S"reltimestr",   1, 1, FEARG_1,          &f_reltimestr},
   {S"remote_expr",   2, 4, FEARG_1,         &f_remote_expr},
   {S"remote_foreground", 1, 1, FEARG_1,       &f_remote_foreground},
   {S"remote_peek",   1, 2, FEARG_1,           &f_remote_peek},
   {S"remote_read",   1, 2, FEARG_1,           &f_remote_read},
   {S"remote_send",   2, 3, FEARG_1,           &f_remote_send},
   {S"remote_startserver", 1, 1, FEARG_1,        &f_remote_startserver},
   {S"remove",      2, 3, FEARG_1,            &f_remove},
   {S"rename",      2, 2, FEARG_1,         &f_rename},
   {S"repeat",      2, 2, FEARG_1,            &f_repeat},
   {S"resolve",      1, 1, FEARG_1,           &f_resolve},
   {S"reverse",      1, 1, FEARG_1,           &f_reverse},
   {S"round",      1, 1, FEARG_1,             &f_round},
   {S"screenattr",   2, 2, FEARG_1,           &f_screenattr},
   {S"screenchar",   2, 2, FEARG_1,           &f_screenchar},
   {S"screenchars",   2, 2, FEARG_1,       &f_screenchars},
   {S"screencol",   0, 0, 0,           &f_screencol},
   {S"screenpos",   3, 3, FEARG_1,     &f_screenpos},
   {S"screenrow",   0, 0, 0,           &f_screenrow},
   {S"screenstring",   2, 2, FEARG_1,        &f_screenstring},
   {S"search",      1, 5, FEARG_1,           &f_search},
   {S"searchcount",   0, 1, FEARG_1,         &f_searchcount},
   {S"searchdecl",   1, 3, FEARG_1,       &f_searchdecl},
   {S"searchpair",   3, 7, 0,          &f_searchpair},
   {S"searchpairpos",   3, 7, 0,      &f_searchpairpos},
   {S"searchpos",   1, 5, FEARG_1,     &f_searchpos},
   {S"server2client",   2, 2, FEARG_1,     &f_server2client},
   {S"serverlist",   0, 0, 0,          &f_serverlist},
   {S"setbufline",   3, 3, FEARG_3,     &f_setbufline},
   {S"setbufvar",   3, 3, FEARG_3,         &f_setbufvar},
   {S"setcharpos",   2, 2, FEARG_2,        &f_setcharpos},
   {S"setcharsearch",   1, 1, FEARG_1,        &f_setcharsearch},
   {S"setcmdline",   1, 2, FEARG_1,        &f_setcmdline},
   {S"setcmdpos",   1, 1, FEARG_1,       &f_setcmdpos},
   {S"setcursorcharpos", 1, 3, FEARG_1,      &f_setcursorcharpos},
   {S"setenv",      2, 2, FEARG_2,            &f_setenv},
   {S"setfperm",   2, 2, FEARG_1,          &f_setfperm},
   {S"setline",      2, 2, FEARG_2,        &f_setline},
   {S"setloclist",   2, 4, FEARG_2,        &f_setloclist},
   {S"setmatches",   1, 2, FEARG_1,        &f_setmatches},
   {S"setpos",      2, 2, FEARG_2,         &f_setpos},
   {S"setreg",      2, 3, FEARG_2,       &f_setreg},
   {S"settabvar",   3, 3, FEARG_3,          &f_settabvar},
   {S"settabwinvar",   4, 4, FEARG_4,       &f_settabwinvar},
   {S"settagstack",   2, 3, FEARG_2,     &f_settagstack},
   {S"setwinvar",   3, 3, FEARG_3,          &f_setwinvar},
   {S"sha256",      1, 1, FEARG_1,    &f_sha256 },
   {S"shellescape",   1, 2, FEARG_1,          &f_shellescape},
   {S"shiftwidth",   0, 1, FEARG_1,           &f_shiftwidth},
   {S"sign_define",   1, 2, FEARG_1,          &f_sign_define},
   {S"sign_getdefined",   0, 1, FEARG_1,   &f_sign_getdefined},
   {S"sign_getplaced",   0, 2, FEARG_1,    &f_sign_getplaced},
   {S"sign_jump",   3, 3, FEARG_1,         &f_sign_jump},
   {S"sign_place",   4, 5, FEARG_1,        &f_sign_place},
   {S"sign_placelist",   1, 1, FEARG_1,    &f_sign_placelist},
   {S"sign_undefine",   0, 1, FEARG_1,     &f_sign_undefine},
   {S"sign_unplace",   1, 2, FEARG_1,      &f_sign_unplace},
   {S"sign_unplacelist", 1, 1, FEARG_1,    &f_sign_unplacelist},
   {S"simplify",   1, 1, FEARG_1,          &f_simplify},
   {S"sin",      1, 1, FEARG_1,           &f_sin},
   {S"sinh",      1, 1, FEARG_1,          &f_sinh},
   {S"slice",      2, 3, FEARG_1,         &f_slice},
   {S"sort",      1, 3, FEARG_1,          &f_sort},
   {S"spellbadword",   0, 1, FEARG_1,     &f_spellbadword},
   {S"spellsuggest",   1, 3, FEARG_1,     &f_spellsuggest},
   {S"split",      1, 3, FEARG_1,         &f_split},
   {S"sqrt",      1, 1, FEARG_1,           &f_sqrt},
   {S"srand",      0, 1, FEARG_1,       &f_srand},
   {S"state",      0, 1, FEARG_1,          &f_state},
   {S"str2blob",   1, 2, FEARG_1,          &f_str2blob},
   {S"str2float",   1, 2, FEARG_1,         &f_str2float},
   {S"str2list",   1, 2, FEARG_1,       &f_str2list},
   {S"str2nr",      1, 3, FEARG_1,         &f_str2nr},
   {S"strcharlen",   1, 1, FEARG_1,           &f_strcharlen},
   {S"strcharpart",   2, 4, FEARG_1,          &f_strcharpart},
   {S"strchars",   1, 2, FEARG_1,             &f_strchars},
   {S"strdisplaywidth",   1, 2, FEARG_1,        &f_strdisplaywidth},
   {S"strftime",   1, 2, FEARG_1,       &f_strftime },
   {S"strgetchar",   2, 2, FEARG_1,           &f_strgetchar},
   {S"stridx",      2, 3, FEARG_1,            &f_stridx},
   {S"string",      1, 1, FEARG_1|FE_X,       &f_string},
   {S"strlen",      1, 1, FEARG_1,            &f_strlen},
   {S"strpart",      2, 4, FEARG_1,           &f_strpart},
   {S"strptime",   2, 2, FEARG_1,       &f_strptime },
   {S"strridx",      2, 3, FEARG_1,           &f_strridx},
   {S"strtrans",   1, 1, FEARG_1,             &f_strtrans},
   {S"strwidth",   1, 1, FEARG_1,            &f_strwidth},
   {S"submatch",   1, 2, FEARG_1,            &f_submatch},
   {S"substitute",   4, 4, FEARG_1,          &f_substitute},
   {S"swapfilelist",   0, 0, 0,        &f_swapfilelist},
   {S"swapinfo",   1, 1, FEARG_1,         &f_swapinfo},
   {S"swapname",   1, 1, FEARG_1,         &f_swapname},
   {S"synID",      3, 3, 0,           &f_synID},
   {S"synIDattr",   2, 3, FEARG_1,            &f_synIDattr},
   {S"synIDtrans",   1, 1, FEARG_1,           &f_synIDtrans},
   {S"synconcealed",   2, 2, 0,            &f_synconcealed},
   {S"synstack",   2, 2, 0,        &f_synstack},
   {S"system",      1, 2, FEARG_1,         &f_system},
   {S"systemlist",   1, 2, FEARG_1,     &f_systemlist},
   {S"tabpagebuflist",   0, 1, FEARG_1,     &f_tabpagebuflist},
   {S"tabpagenr",   0, 1, 0,           &f_tabpagenr},
   {S"tabpagewinnr",   1, 2, FEARG_1,        &f_tabpagewinnr},
   {S"tagfiles",   0, 0, 0,       &f_tagfiles},
   {S"taglist",      1, 2, FEARG_1,   &f_taglist},
   {S"tan",      1, 1, FEARG_1,            &f_tan},
   {S"tanh",      1, 1, FEARG_1,           &f_tanh},
   {S"tempname",   0, 0, 0,            &f_tempname},
   {S"term_dumpdiff",   2, 3, FEARG_1,        TERM_FUNC(f_term_dumpdiff)},
   {S"term_dumpload",   1, 2, FEARG_1,        TERM_FUNC(f_term_dumpload)},
   {S"term_dumpwrite",   2, 3, FEARG_2,       TERM_FUNC(f_term_dumpwrite)},
   {S"term_getaltscreen", 1, 1, FEARG_1,         TERM_FUNC(f_term_getaltscreen)},
   {S"term_getansicolors", 1, 1, FEARG_1, &f_term_getansicolors },
//   {S"term_getattr",   2, 2, FEARG_1,            TERM_FUNC(f_term_getattr)},
   {S"term_getcursor",   1, 1, FEARG_1,          TERM_FUNC(f_term_getcursor)},
   {S"term_getjob",   1, 1, FEARG_1,            TERM_FUNC(f_term_getjob)},
   {S"term_getline",   2, 2, FEARG_1,           TERM_FUNC(f_term_getline)},
   {S"term_getscrolled", 1, 1, FEARG_1,         TERM_FUNC(f_term_getscrolled)},
   {S"term_getsize",   1, 1, FEARG_1,        TERM_FUNC(f_term_getsize)},
   {S"term_getstatus",   1, 1, FEARG_1,         TERM_FUNC(f_term_getstatus)},
   {S"term_gettitle",   1, 1, FEARG_1,          TERM_FUNC(f_term_gettitle)},
   {S"term_gettty",   1, 2, FEARG_1,            TERM_FUNC(f_term_gettty)},
   {S"term_list",   0, 0, 0,         TERM_FUNC(f_term_list)},
   {S"term_scrape",   2, 2, FEARG_1,  TERM_FUNC(f_term_scrape)},
   {S"term_sendkeys",   2, 2, FEARG_1,        TERM_FUNC(f_term_sendkeys)},
   {S"term_setansicolors", 2, 2, FEARG_1,  &f_term_setansicolors },
   {S"term_setapi",   2, 2, FEARG_1,            TERM_FUNC(f_term_setapi)},
   {S"term_setkill",   2, 2, FEARG_1,           TERM_FUNC(f_term_setkill)},
   {S"term_setrestore",   2, 2, FEARG_1,        TERM_FUNC(f_term_setrestore)},
   {S"term_setsize",   3, 3, FEARG_1,           TERM_FUNC(f_term_setsize)},
   {S"term_start",   1, 2, FEARG_1,            TERM_FUNC(f_term_start)},
   {S"term_wait",   1, 2, FEARG_1,            TERM_FUNC(f_term_wait)},
   {S"terminalprops",   0, 0, 0,         &f_terminalprops},
   {S"test_alloc_fail",   3, 3, FEARG_1,       &f_test_alloc_fail},
   {S"test_autochdir",   0, 0, 0,           &f_test_autochdir},
   {S"test_feedinput",   1, 1, FEARG_1,        &f_test_feedinput},
   {S"test_garbagecollect_now",   0, 0, 0,        &f_test_garbagecollect_now},
   {S"test_garbagecollect_soon", 0, 0, 0,        &f_test_garbagecollect_soon},
   {S"test_getvalue",   1, 1, FEARG_1,           &f_test_getvalue},
   {S"test_ignore_error", 1, 1, FEARG_1,         &f_test_ignore_error},
   {S"test_null_blob",   0, 0, 0,            &f_test_null_blob},
   {S"test_null_channel", 0, 0, 0,           &f_test_null_channel},
   {S"test_null_dict",   0, 0, 0,           &f_test_null_dict},
   {S"test_null_function", 0, 0, 0,         &f_test_null_function},
   {S"test_null_job",   0, 0, 0,            &f_test_null_job},
   {S"test_null_list",   0, 0, 0,           &f_test_null_list},
   {S"test_null_partial", 0, 0, 0,          &f_test_null_partial},
   {S"test_null_string", 0, 0, 0,           &f_test_null_string},
   {S"test_option_not_set", 1, 1, FEARG_1,       &f_test_option_not_set},
   {S"test_override",   2, 2, FEARG_2,           &f_test_override},
   {S"test_refcount",   1, 1, FEARG_1|FE_X,       &f_test_refcount},
   {S"test_setmouse",   2, 2, 0,          &f_test_setmouse},
   {S"test_settime",   1, 1, FEARG_1,         &f_test_settime},
   {S"test_srand_seed",   0, 1, FEARG_1,        &f_test_srand_seed},
   {S"test_unknown",   0, 0, 0,           &f_test_unknown},
   {S"test_void",   0, 0, 0,           &f_test_void},
   {S"timer_info",   0, 1, FEARG_1,   TIMER_FUNC(f_timer_info)},
   {S"timer_pause",   2, 2, FEARG_1,        TIMER_FUNC(f_timer_pause)},
   {S"timer_start",   2, 3, FEARG_1,        TIMER_FUNC(f_timer_start)},
   {S"timer_stop",   1, 1, FEARG_1,         TIMER_FUNC(f_timer_stop)},
   {S"timer_stopall",   0, 0, 0,            TIMER_FUNC(f_timer_stopall)},
   {S"tolower",      1, 1, FEARG_1,         &f_tolower},
   {S"toupper",      1, 1, FEARG_1,         &f_toupper},
   {S"tr",      3, 3, FEARG_1,            &f_tr},
   {S"trim",      1, 3, FEARG_1,          &f_trim},
   {S"trunc",      1, 1, FEARG_1,         &f_trunc},
   {S"type",      1, 1, FEARG_1|FE_X,       &f_type},
   {S"undofile",   1, 1, FEARG_1,           &f_undofile},
   {S"undotree",   0, 1, FEARG_1,           &f_undotree},
   {S"uniq",      1, 3, FEARG_1,            &f_uniq},
   {S"values",      1, 1, FEARG_1,       &f_values},
   {S"virtcol",      1, 3, FEARG_1,           &f_virtcol},
   {S"virtcol2col",   3, 3, FEARG_1,          &f_virtcol2col},
   {S"visualmode",   0, 1, 0,           &f_visualmode},
   {S"wildmenumode",   0, 0, 0,         &f_wildmenumode},
   {S"wildtrigger",   0, 0, 0,          &f_wildtrigger},
   {S"win_execute",   2, 3, FEARG_2,        &f_win_execute},
   {S"win_findbuf",   1, 1, FEARG_1,     &f_win_findbuf},
   {S"win_getid",   0, 2, FEARG_1,          &f_win_getid},
   {S"win_gettype",   0, 1, FEARG_1,        &f_win_gettype},
   {S"gotoPortalid",   1, 1, FEARG_1,        &f_gotoPortalid},
   {S"win_id2tabwin",   1, 1, FEARG_1,     &f_win_id2tabwin},
   {S"win_id2win",   1, 1, FEARG_1,           &f_win_id2win},
   {S"win_move_separator", 2, 2, FEARG_1,     &f_win_move_separator},
   {S"win_move_statusline", 2, 2, FEARG_1,     &f_win_move_statusline},
   {S"win_screenpos",   1, 1, FEARG_1,       &f_win_screenpos},
   {S"splitPortalmove",   2, 3, FEARG_1,     &f_splitPortalmove},
   {S"winbufnr",   1, 1, FEARG_1,       &f_winbufnr},
   {S"wincol",      0, 0, 0,        &f_wincol},
   {S"winheight",   1, 1, FEARG_1,         &f_winheight},
   {S"winlayout",   0, 1, FEARG_1,         &f_winlayout},
   {S"winline",      0, 0, 0,           &f_winline},
   {S"winnr",      0, 1, FEARG_1,       &f_winnr},
   {S"winrestcmd",   0, 0, 0,           &f_winrestcmd},
   {S"winrestview",   1, 1, FEARG_1,        &f_winrestview},
   {S"winsaveview",   0, 0, 0,   &f_winsaveview},
   {S"winwidth",   1, 1, FEARG_1, &f_winwidth},
   {S"wordcount",   0, 0, 0, &f_wordcount},
   {S"writefile",   2, 3, FEARG_1, &f_writefile},
   {S"xor",      2, 2, FEARG_1, &f_xor},
};

//}}}
//{{{ aux for global functions

// Return true if specified function allows a type as an argument.
//private int
//func_allows_type(int idx) {
//   return (globalFunctions[idx].f_argtype & FE_X) != 0;
//}

//Function given to expandGeneric() to obtain the list of internal
//or user defined function names.
CS
get_function_name(Expand *xp, int idx) {
   static int   intidx = -1;
   CS name;

   if (idx == 0)
      intidx = -1;
   if (intidx < 0) {
      name = get_user_func_name(xp, idx);
      if (name) {
         if (*name != ZERO && *name != '<' && STRNCMP("g:", xp->input.c, 2) == 0)
            return cat_prefix_varname('g', name);
         return name;
      }
   }
   if (++intidx < (int)ARRAY_LENGTH(globalFunctions)) {
      // Skip if the function doesn't have an implementation (feature not implemented).
      if (globalFunctions[intidx].f_func == NULL)
          return (CS)"";
      STRCPY(IObuff, globalFunctions[intidx].f_name);
      STRCAT(IObuff, "(");
      if (globalFunctions[intidx].f_max_argc == 0)
          STRCAT(IObuff, ")");
      return IObuff;
   }

    return NULL;
}

//Function given to expandGeneric() to obtain the list of internal or
//user defined variable or function names.
CS
get_expr_name(Expand *xp, int idx) {
   static int   intidx = -1;
   Byte   *name;

   if (idx == 0)
      intidx = -1;
   if (intidx < 0) {
      name = get_function_name(xp, idx);
      if (name)
         return name;
   }
   return get_user_var_name(xp, ++intidx);
}

//Find internal function "name" in table "globalFunctions".
//Return index, or -1 if not found or "implemented" is TRUE and the function is not implemented.
private Unt
find_internal_func_opt(CS name, int implemented) {
   int      first = 0;
   int      cmp;
   int      x;

   int last = (int)ARRAY_LENGTH(globalFunctions) - 1;

   // Find the function name in the table. Binary search.
   while (first <= last) {
      x = first + ((unsigned)(last - first) >> 1);
      cmp = STRCMP(name, globalFunctions[x].f_name);
      if (cmp < 0)
         last = x - 1;
      ei (cmp > 0)
         first = x + 1;
      ei (implemented && globalFunctions[x].f_func == NULL)
         break;
      else
         return x;
   }
   return UNT;
}

//Find internal function "name" in table "globalFunctions".
//Return index, or UNT if not found or the function is not implemented.
Unt
find_internal_func(CS name) {
   return find_internal_func_opt(name, TRUE);
}

int
has_internal_func(CS name) {
   return find_internal_func_opt(name, TRUE) < UNT;
}

private int
has_internal_func_name(CS name) {
   return find_internal_func_opt(name, FALSE) < UNT;
}

private CS
internal_func_name(int idx) {
   return globalFunctions[idx].f_name;
}

//Get the argument count for function "idx".
//"argcount" is the total argument count, "min_argcount" the non-optional argument count.
void
internal_func_get_argcount(int idx, OUT int *argcount, OUT int *min_argcount) {
   *argcount = globalFunctions[idx].f_max_argc;
   *min_argcount = globalFunctions[idx].f_min_argc;
}


// Return TRUE if "idx" is for the map() function.
int
internal_func_is_map(int idx) {
   return globalFunctions[idx].f_func == f_map;
}

//Check the argument count to use for internal function "idx".
//Return -1 for failure, 0 if no method base accepted, 1 if method base is
//first argument, 2 if method base is second argument, etc.  9 if method base is last argument.
int
check_internal_func(int idx, int argcount) {
   FnError       res;

   if (argcount < globalFunctions[idx].f_min_argc)
      res = FCERR_TOOFEW;
   ei (argcount > globalFunctions[idx].f_max_argc)
      res = FCERR_TOOMANY;
   else
      return globalFunctions[idx].f_argtype & FEARG_MASK;

   CS name = internal_func_name(idx);
   if (res == FCERR_TOOMANY)
      showErrFmtMsg(_(e_too_many_arguments_for_function_str), name);
   else
      showErrFmtMsg(_(e_not_enough_arguments_for_function_str), name);
   return -1;
}

FnError
call_internal_func(
   Arr(Byte) name,
   int argcount,
   Var* argvars,
   Var* returnVar
){
   int i;

   i = find_internal_func(name);
   if (i < 0)
      return FCERR_UNKNOWN;
   if (argcount < globalFunctions[i].f_min_argc)
      return FCERR_TOOFEW;
   if (argcount > globalFunctions[i].f_max_argc)
      return FCERR_TOOMANY;
   argvars[argcount].tag = VAR_UNKNOWN;
   globalFunctions[i].f_func(argvars, returnVar);
   return FCERR_NONE;
}

void
call_internal_func_by_idx(
   int       idx,
   Var    *argvars,
   Var    *returnVar)
{
   globalFunctions[idx].f_func(argvars, returnVar);
}

//Invoke a method for base->method().
FnError
call_internal_method(
   Byte       *name,
   int       argcount,
   Var    *argvars,
   Var    *returnVar,
   Var    *basetv)
{
   int      fi;
   Var   argv[MAX_FUNC_ARGS + 1];

   fi = find_internal_func(name);
   if (fi < 0)
      return FCERR_UNKNOWN;
   if ((globalFunctions[fi].f_argtype & FEARG_MASK) == 0)
      return FCERR_NOTMETHOD;
   if (argcount + 1 < globalFunctions[fi].f_min_argc)
      return FCERR_TOOFEW;
   if (argcount + 1 > globalFunctions[fi].f_max_argc)
      return FCERR_TOOMANY;

   if ((globalFunctions[fi].f_argtype & FEARG_MASK) == FEARG_2) {
      if (argcount < 1)
         return FCERR_TOOFEW;

      // base value goes second
      argv[0] = argvars[0];
      argv[1] = *basetv;
      for (int i = 1; i < argcount; ++i)
         argv[i + 1] = argvars[i];
   } ei ((globalFunctions[fi].f_argtype & FEARG_MASK) == FEARG_3) {
      if (argcount < 2)
         return FCERR_TOOFEW;

      // base value goes third
      argv[0] = argvars[0];
      argv[1] = argvars[1];
      argv[2] = *basetv;
      for (int i = 2; i < argcount; ++i)
         argv[i + 1] = argvars[i];
   } ei ((globalFunctions[fi].f_argtype & FEARG_MASK) == FEARG_4) {
      if (argcount < 3)
         return FCERR_TOOFEW;

      // base value goes fourth
      argv[0] = argvars[0];
      argv[1] = argvars[1];
      argv[2] = argvars[2];
      argv[3] = *basetv;
      for (int i = 3; i < argcount; ++i)
         argv[i + 1] = argvars[i];
   } else {
      // FEARG_1: base value goes first
      argv[0] = *basetv;
      for (int i = 0; i < argcount; ++i)
          argv[i + 1] = argvars[i];
   }
   argv[argcount + 1].tag = VAR_UNKNOWN;

   globalFunctions[fi].f_func(argv, returnVar);
   return FCERR_NONE;
}

// Return TRUE for a non-zero Number and a non-empty String.
int
non_zero_arg(Var *argvars) {
   return ((argvars[0].tag == VAR_NUMBER && argvars[0].number != 0)
      || (argvars[0].tag == VAR_BOOL && argvars[0].number == VVAL_TRUE)
      || (argvars[0].tag == VAR_STRING 
            && argvars[0].string && *argvars[0].string != ZERO
         )
   );
}

//}}}
//{{{API functions

// "and(expr, expr)" function
private void
f_and(Var *argvars, Var *returnVar) {
   returnVar->number = varGetNumberChk(argvars, NULL) & varGetNumberChk(argvars + 1, NULL);
}

//"balloon_show()" function
private void
f_balloon_gettext(Var *argvars UNUSED, Var *returnVar) {
   returnVar->tag = VAR_STRING;
   if (!balloonEval)
      return;

   if (balloonEval->msg == NULL)
      returnVar->string = NULL;
   else
      returnVar->string = copyStr(balloonEval->msg);
}

private void
f_balloon_show(Var *argvars, Var *returnVar UNUSED) {
   if (balloonEval == NULL)
      return;


   if (argvars[0].tag == VAR_LIST) {
      List *l = argvars[0].list;

      // empty list removes the balloon
      post_balloon(balloonEval, NULL, l == NULL || l->len == 0 ? NULL : l);
   } else {
      CS mesg = convertVarToStringSingleUse(&argvars[0]);
      if (mesg)
         // empty string removes the balloon
         post_balloon(balloonEval, *mesg == ZERO ? NULL : mesg, NULL);
   }
}

private void
f_balloon_split(Var *argvars, Var *returnVar UNUSED) {
   allocReturnList(returnVar);

   CS msg = convertVarToStringSingleUse(&argvars[0]);
   if (msg) {
      Arr(PopupItem) array;
      Unt size = balloonSplitMessage(msg, OUT &array);

      // Skip the first and last item, they are always empty.
      for (Unt i = 1; i < size - 1; ++i)
         list_append_string(returnVar->list, array[i].pum_text, -1);
      while (size > 0)
         eeglFree(array[--size].pum_text);
      eeglFree(array);
   }
}

// Encode the bytes in "blob" using base-64 encoding.
private void*
base64_encode(Blob *blob) {
   return encodeBase64(blob->c.c, blob->c.len);
}

// Decode the base64 string "data" into "blob"
private void
base64_decode(Byte const *base64, Blob *blob) {
   int base64Len = STRLEN(base64);
   ArrayList mbResult = decodeBase64(base64, base64Len);
   
   if (mbResult.len > 0) {
      blob->c = mbResult;
   } else {
      ga_clear(&blob->c);
   } 
}

//"base64_decode(string)" function
private void
f_base64_decode(Var *argvars, Var *returnVar) {
   if (check_for_string_arg(argvars, 0) == FAIL)
      return;

   if (returnVar_blob_alloc(returnVar) == FAIL)
      return;

   CS str = convertVarToStringSingleUse(&argvars[0]);
   if (str)
      base64_decode(str, returnVar->blob);
}

//"base64_encode(blob)" function
private void
f_base64_encode(Var *argvars, Var *returnVar) {
   if (check_for_blob_arg(argvars, 0) == FAIL)
      return;

   returnVar->tag = VAR_STRING;
   returnVar->string = NULL;

   Blob *blob = argvars->blob;
   if (blob)
      returnVar->string = base64_encode(blob);
}

//Get the book from "arg". Give an error and return NULL if it is not valid.
Book *
evGetBookArg(Var *arg) {
   ++emsg_off;
   Book* book = daGetBook(arg, FALSE);
   --emsg_off;
   if (!book)
      showErrFmtMsg(_(e_invalid_buffer_name_str), tv_get_string(arg));
   return book;
}

//"bindtextdomain(package, path)" function
private void
f_bindtextdomain(Var *argvars, Var *returnVar) {
   returnVar->tag = VAR_BOOL;
   returnVar->number = VVAL_TRUE;

   if (check_for_nonempty_string_arg(argvars, 0) == FAIL
         || check_for_nonempty_string_arg(argvars, 1) == FAIL)
      return;

   if (strcmp((const char *)argvars[0].string, EEGLPACKAGE) == 0)
      showErrFmtMsg(_(e_invalid_argument_str), tv_get_string(&argvars[0]));
   else {
      if (BINDTEXTDOMAIN(argvars[0].string, argvars[1].string) == NULL) {
         do_outofmem_msg((long)0);
         returnVar->number = VVAL_FALSE;
      }
   }

    return;
}

//"byte2line(byte)" function
private void
f_byte2line(Var *argvars UNUSED, Var *returnVar) {
   long boff = tv_get_number(&argvars[0]) - 1;  // boff gets -1 on type error
   if (boff < 0)
      returnVar->number = -1;
   else
      returnVar->number = ml_find_line_or_offset(curBook, (LineNr)0, &boff);
}

// "call(func, arglist [, dict])" function
private void
f_call(Var *argvars, Var *returnVar) {
   Byte   *func;
   PartiallyApplied   *partial = NULL;
   Bag   *selfdict = NULL;
   Byte   *tofree = NULL;

   if (confirmVarIsList(argvars, 1) == FAIL)
      return;
   if (argvars[1].list == NULL)
      return;

   if (argvars[0].tag == VAR_FUNC)
      func = argvars[0].string;
   ei (argvars[0].tag == VAR_PARTIAL) {
      partial = argvars[0].partial;
      func = partial_name(partial);
   } else
      func = tv_get_string(&argvars[0]);
   if (func == NULL || *func == ZERO)
      return;      // type error, empty name or null function

   if (argvars[0].tag == VAR_STRING) {
      Byte   *p = func;
      tofree = trans_function_name(&p, NULL, FALSE, TFN_INT|TFN_QUIET);
      if (tofree == NULL) {
         emsg_funcname(e_unknown_function_str, func);
         return;
      }
      func = tofree;
   }

   if (argvars[2].tag != VAR_UNKNOWN) {
      if (check_for_dict_arg(argvars, 2) == FAIL)
          goto done;

      selfdict = argvars[2].bag;
   }

   (void)func_call(func, &argvars[1], partial, selfdict, returnVar);

done:
   eeglFree(tofree);
}

private void
f_changenr(Var *argvars UNUSED, Var *returnVar) {
   returnVar->number = curBook->undo.seqCurr;
}

private void
f_char2nr(Var *argvars, Var *returnVar) {
   int   utf8 = 0;

   if (argvars[1].tag != VAR_UNKNOWN)
      utf8 = (int)varGetNumberChk(argvars + 1, NULL);

   if (utf8)
      returnVar->number = mb_ptr2char(tv_get_string(&argvars[0]));
   else
      returnVar->number = (*mb_ptr2char)(tv_get_string(&argvars[0]));
}

//Get the current cursor column and store it in 'returnVar'. If 'charcol' is TRUE,
//return the character index of the column. Otherwise, return the byte index of the column.
private void
get_col(Var *argvars, Var *returnVar, int charcol) {
   ColNr   col = 0;
   Pos   *fp;
   SwitchPort   switchPort;
   Boole portChanged = false;

   if (check_for_string_or_list_arg(argvars, 0) == FAIL
          || check_for_opt_number_arg(argvars, 1) == FAIL)
      return;

   if (argvars[1].tag != VAR_UNKNOWN) {
      Tab   *t;

      // use the window specified in the second argument
      Portal* po = getPortAndTab((int)tv_get_number(&argvars[1]), OUT &t);
      if (!po || !t)
          return;

      if (portSwitchNoblock(&switchPort, po, t, TRUE) != OK)
          return;

      check_cursor();
      portChanged = true;
   }

   int fnum = curBook->fiNum;
   fp = var2fpos(&argvars[0], FALSE, &fnum, charcol);
   if (fp && fnum == curBook->fiNum) {
      if (fp->col == MAXCOL) {
         // '> can be MAXCOL, get the length of the line then
         if (fp->lnum <= curBook->mem.lineCount)
            col = ml_get_len(fp->lnum) + 1;
         else
            col = MAXCOL;
      } else {
         col = fp->col + 1;
         // col(".") when the cursor is on the ZERO at the end of the line
         // because of "coladd" can be seen as an extra column.
         if (virtual_active() && fp == &curPor->cursor) {
            Byte   *p = ml_get_cursor();

            if (curPor->cursor.coladd >= (ColNr)chartabsize(p,
                   curPor->virtCol - curPor->cursor.coladd)
            ) {
               int      l;

               if (*p != ZERO && p[(l = utfCharLen(p))] == ZERO)
                  col += l;
            }
         }
      }
   }
   returnVar->number = col;

   if (portChanged)
      portRestoreNoblock(&switchPort, TRUE);
}

private void
f_charcol(Var *argvars, Var *returnVar) {
   get_col(argvars, returnVar, TRUE);
}

Portal*
getOptionalPortal(Var *argvars, int idx) {
   if (argvars[idx].tag == VAR_UNKNOWN)
      return curPor;

   Portal* port = portFindByNrOrId(&argvars[idx]);
   if (!port) {
      emsg(_(e_invalid_portal_number));
      return NULL;
   }
   return port;
}

//"col(string)" function
private void
f_col(Var *argvars, Var *returnVar) {
   get_col(argvars, returnVar, FALSE);
}

//"confirm(message, buttons[, default [, type]])" function
private void
f_confirm(Var *argvars UNUSED, Var *returnVar UNUSED) {
   CS buttons = NULL;
   Byte buf[NUMBUFLEN];
   Byte buf2[NUMBUFLEN];
   int def = 1;
   int type = EE_GENERIC;
   Boole error = false;

   CS message = convertVarToStringSingleUse(&argvars[0]);
   if (!message)
      error = true;
   if (argvars[1].tag != VAR_UNKNOWN) {
      buttons = convertVarToString(&argvars[1], buf);
      if (!buttons)
         error = TRUE;
      if (argvars[2].tag != VAR_UNKNOWN) {
         def = (int)varGetNumberChk(argvars + 2, OUT &error);
         if (argvars[3].tag != VAR_UNKNOWN) {
            CS typestr = convertVarToString(&argvars[3], buf2);
            if (typestr == NULL)
               error = true;
            else {
               switch (TOUPPER_ASC(*typestr)) {
               case 'E': type = EE_ERROR; break;
               case 'Q': type = EE_QUESTION; break;
               case 'I': type = EE_INFO; break;
               case 'W': type = EE_WARNING; break;
               case 'G': type = EE_GENERIC; break;
               }
            }
         }
      }
   }

   if (buttons == NULL || *buttons == ZERO)
      buttons = (CS)_("&Ok");

   if (!error)
      returnVar->number = do_dialog(type, NULL, message, buttons, def, NULL, FALSE);
}

private void
f_copy(Var *argvars, Var *returnVar) {
   item_copy(&argvars[0], returnVar, FALSE, TRUE, 0);
}

//Set the cursor position. If "charcol" is TRUE, then use the column number as a character offset.
//Otherwise use the column number as a byte offset.
private void
set_cursorpos(Var *argvars, OUT Var *returnVar, int charcol) {
   long   lnum, col;
   long   coladd = 0;
   Boole set_curswant = true;

   returnVar->number = -1;
   if (argvars[0].tag == VAR_LIST) {
      Pos       pos;
      ColNr       curswant = -1;

      if (list2fpos(argvars, &pos, NULL, &curswant, charcol) == FAIL) {
         emsg(_(e_invalid_argument));
         return;
      }
      lnum = pos.lnum;
      col = pos.col;
      coladd = pos.coladd;
      if (curswant >= 0) {
         curPor->cursWant = curswant - 1;
         set_curswant = false;
      }
   } ei ((argvars[0].tag == VAR_NUMBER || argvars[0].tag == VAR_STRING)
       && (argvars[1].tag == VAR_NUMBER || argvars[1].tag == VAR_STRING)
   ) {
      lnum = tv_get_lnum(argvars);
      if (lnum < 0)
         showErrFmtMsg(_(e_invalid_argument_str), tv_get_string(&argvars[0]));
      ei (lnum == 0)
         lnum = curPor->cursor.lnum;
      col = (long)varGetNumberChk(argvars + 1, NULL);
      if (charcol)
         col = bookCharidxToByteidx(curBook, lnum, col) + 1;
      if (argvars[2].tag != VAR_UNKNOWN)
         coladd = (long)varGetNumberChk(argvars + 2, NULL);
   } else {
      emsg(_(e_invalid_argument));
      return;
   }
   if (lnum < 0 || col < 0 || coladd < 0)
      return;      // type error; errmsg already given
   if (lnum > 0)
      curPor->cursor.lnum = lnum;
   if (col != MAXCOL && --col < 0)
      col = 0;
   curPor->cursor.col = col;
   curPor->cursor.coladd = coladd;

   // Make sure the cursor is in a valid position.
   check_cursor();
   // Correct cursor for multi-byte character.
   mb_adjust_cursor();

   curPor->setCursWant = set_curswant;
   returnVar->number = 0;
}

//"cursor(lnum, col)" function, or "cursor(list)"
//Move the cursor to the specified line and column.
//Return 0 when the position could be set, -1 otherwise.
private void
f_cursor(Var *argvars, Var *returnVar) {
   set_cursorpos(argvars, OUT returnVar, FALSE);
}

private void
f_deepcopy(Var *argvars, Var *returnVar) {
   Long   noref = 0;

   if (check_for_opt_bool_arg(argvars, 1) == FAIL)
      return;

   if (argvars[1].tag != VAR_UNKNOWN)
   noref = varGetNumberChk(argvars + 1, NULL);

   item_copy(&argvars[0], returnVar, TRUE, TRUE, noref == 0 ? get_copyID() : 0);
}

private void
f_did_filetype(Var *argvars UNUSED, Var *returnVar UNUSED) {
   returnVar->number = curBook->didFiletype;
}

//"echoraw({expr})" function
private void
f_echoraw(Var *argvars, Var *returnVar UNUSED) {
   CS str = convertVarToStringSingleUse(&argvars[0]);
   if (str && *str != ZERO) {
      out_str(str);
      out_flush();
   }
}

//"empty({expr})" function
private void
f_empty(Var *argvars, Var *returnVar) {
   int      n = FALSE;

   switch (argvars[0].tag) {
   case VAR_STRING:
   case VAR_FUNC:
      n = argvars[0].string == NULL || *argvars[0].string == ZERO;
      break;
   case VAR_PARTIAL:
      n = argvars[0].partial == NULL;
      break;
   case VAR_NUMBER:
      n = argvars[0].number == 0;
      break;
   case VAR_FLOAT:
      n = argvars[0].floatt == 0.0;
      break;
   case VAR_LIST:
      n = argvars[0].list == NULL || argvars[0].list->len == 0;
      break;
   case VAR_BAG:
      n = argvars[0].bag == NULL || argvars[0].bag->hashTable.count == 0;
      break;
   case VAR_BOOL:
   case VAR_SPECIAL:
      n = argvars[0].number != VVAL_TRUE;
      break;

   case VAR_BLOB:
      n = argvars[0].blob == NULL || argvars[0].blob->c.len == 0;
      break;

   case VAR_JOB:
      n = argvars[0].job == NULL || argvars[0].job->jv_status != JOB_STARTED;
      break;
   case VAR_CHANNEL:
      n = argvars[0].channel == NULL || !channel_is_open(argvars[0].channel);
      break;

   case VAR_UNKNOWN:
   case VAR_ANY:
   case VAR_VOID:
      internal_error_no_abort(S"f_empty(UNKNOWN)");
      n = TRUE;
      break;
   }

   returnVar->number = n;
}

//"environ()" function
private void
f_environ(Var *argvars UNUSED, Var *returnVar) {
   int         i = 0;
   Byte      *entry, *value;
   extern Byte      **environ;

   allocReturnDict(returnVar);

   if (*environ == NULL)
      return;

   for (i = 0; ; ++i) {
      if ((entry = environ[i]) == NULL)
         return;
      entry = copyStr(entry);
      if ((value = firstOccurrence(entry, '=')) == NULL) {
         eeglFree(entry);
         continue;
      }
      *value++ = ZERO;
      bagAddString(returnVar->bag, entry, value);
      eeglFree(entry);
   }
}

private void
f_err_teapot(Var *argvars, Var *returnVar UNUSED) {
   if (argvars[0].tag != VAR_UNKNOWN) {
      if (argvars[0].tag == VAR_STRING) {
         CS s = tv_get_string_strict(&argvars[0]);
         if (*skipwhite(s) == ZERO)
            return;
      }

      Boole err = false;
      Boole do_503 = eval_expr_to_bool(&argvars[0], OUT &err);
      if (!err && do_503) {
         emsg(_(e_coffee_currently_not_available));
         return;
      }
   }

   emsg(_(e_im_a_teapot));
}

//"escape({string}, {chars})" function
private void
f_escape(Var *argvars, Var *returnVar) {
   Byte   buf[NUMBUFLEN];
   returnVar->string = copyStr_escaped(tv_get_string(&argvars[0]),
                tv_get_string_buf(&argvars[1], buf));
   returnVar->tag = VAR_STRING;
}

private void
f_eval(Var *argvars, Var *returnVar) {
   CS s = convertVarToStringSingleUse(&argvars[0]);
   if (s)
      s = skipwhite(s);

   CS p = s;
   if (!s || eval1(OUT &s, returnVar, &EVALARG_EVALUATE) == FAIL) {
      if (p && !aborting())
         showErrFmtMsg(_(e_invalid_expression_str), p);
      need_clr_eos = FALSE;
      returnVar->tag = VAR_NUMBER;
      returnVar->number = 0;
   } ei (*s != ZERO)
      showErrFmtMsg(_(e_trailing_characters_str), s);
}

private void
f_eventhandler(Var *argvars UNUSED, Var *returnVar) {
   returnVar->number = vgetcBusyG || input_busy;
}

private ArrayList   redir_execute_ga;

//Append "value[value_len]" to the execute() output.
void
execute_redir_str(CS value, int value_len) {
   int      len;

   if (value_len == -1)
      len = (int)STRLEN(value);   // Append the entire string
   else
      len = value_len;      // Append only "value_len" characters
   if (ga_grow(&redir_execute_ga, len) == FAIL)
      return;

   mch_memmove((char *)redir_execute_ga.c + redir_execute_ga.len, value, len);
   redir_execute_ga.len += len;
}

#if defined(PROTO)
//Get next line from a string containing NL separated lines.
//Called by doCommand() to get the next line.
//Return an allocated string, or NULL when at the end of the string.
private CS
get_str_line(
   Unt       c UNUSED,
   void* cookie,
   int       indent UNUSED,
   GetlineAlgo options UNUSED)
{
   Byte   *start = *(Byte **)cookie;
   Byte   *line;
   Byte   *p;

   p = start;
   if (p == NULL || *p == ZERO)
      return NULL;
   p = firstOccurrence(p, '\n');
   if (p == NULL)
      line = copyStr(start);
   else {
      line = copySubstr(start, p - start);
      p++;
   }

   *(Byte **)cookie = p;
   return line;
}

// Execute a series of commands in 'str'
void
execute_cmds_from_string(CS str) {
   doCommand(NULL, get_str_line, (void *)&str,
      DOCMD_NOWAIT|DOCMD_VERBOSE|DOCMD_REPEAT|DOCMD_KEYTYPED);
}
#endif

//}}}
//{{{API functions 2

//Get next line from a list. Called by doCommand() to get the next line.
//Return allocated string, or NULL for end of function.
CS
get_list_line(
   Unt c UNUSED,
   void* cookie,
   int indent UNUSED,
   GetlineAlgo options UNUSED)
{
   ListItem **p = (ListItem **)cookie;
   ListItem *item = *p;
   Byte buf[NUMBUFLEN];

   if (!item)
      return NULL;
   CS s = convertVarToString(&item->c, buf);
   *p = item->next;
   return s == NULL ? NULL : copyStr(s);
}

// "execute()" function
void
execute_common(Var *argvars, Var *returnVar, int arg_off) {
   Byte   *cmd = NULL;
   List   *list = NULL;
   int      save_msg_silent = msg_silent;
   int      save_emsg_silent = emsg_silent;
   int      save_emsg_noredir = emsg_noredir;
   int      save_redir_execute = redir_execute;
   int      save_redir_off = redir_off;
   ArrayList   save_ga;
   int      saveMsgCol = msgColG;
   int      save_stickyCommandModifiersG = stickyCommandModifiersG;
   int      echo_output = FALSE;

   returnVar->string = NULL;
   returnVar->tag = VAR_STRING;

   if (argvars[arg_off].tag == VAR_LIST) {
      list = argvars[arg_off].list;
      if (list == NULL || list->len == 0)
         // empty list, no commands, empty output
         return;
      ++list->refcount;
   } ei (argvars[arg_off].tag == VAR_JOB || argvars[arg_off].tag == VAR_CHANNEL) {
      showErrFmtMsg(_(e_using_invalid_value_as_string_str), vartype_name(argvars[arg_off].tag));
      return;
   } else {
      cmd = convertVarToStringSingleUse(&argvars[arg_off]);
      if (cmd == NULL)
          return;
   }

   if (argvars[arg_off + 1].tag != VAR_UNKNOWN) {
      Byte   buf[NUMBUFLEN];
      Byte  *s = convertVarToString_strict(&argvars[arg_off + 1], buf, FALSE);

      if (s == NULL)
         return;
      if (*s == ZERO)
         echo_output = TRUE;
      if (STRNCMP(s, "silent", 6) == 0)
         ++msg_silent;
      if (STRCMP(s, "silent!") == 0) {
         emsg_silent = TRUE;
         emsg_noredir = TRUE;
      }
   } else
      ++msg_silent;

   if (redir_execute)
      save_ga = redir_execute_ga;
   ga_init2(&redir_execute_ga, sizeof(char), 500);
   redir_execute = TRUE;
   redir_off = FALSE;
   if (!echo_output)
      msgColG = 0;  // prevent leading spaces

   if (cmd)
      executeCommLine(cmd);
   else {
      ListItem   *item;

      CHECK_LIST_MATERIALIZE(list);
      item = list->first;
      doCommand(NULL, get_list_line, (void *)&item,
               DOCMD_NOWAIT|DOCMD_VERBOSE|DOCMD_REPEAT|DOCMD_KEYTYPED);
      --list->refcount;
   }
   stickyCommandModifiersG = save_stickyCommandModifiersG;

   // Need to append a ZERO to the result.
   if (ga_grow(&redir_execute_ga, 1) == OK) {
      ((char *)redir_execute_ga.c)[redir_execute_ga.len] = ZERO;
      returnVar->string = redir_execute_ga.c;
   } else {
      ga_clear(&redir_execute_ga);
      returnVar->string = NULL;
   }
   msg_silent = save_msg_silent;
   emsg_silent = save_emsg_silent;
   emsg_noredir = save_emsg_noredir;

   redir_execute = save_redir_execute;
   if (redir_execute)
      redir_execute_ga = save_ga;
   redir_off = save_redir_off;

   // "silent reg" or "silent echo x" leaves msgColG somewhere in the line.
   if (echo_output)
      // When not working silently: put it in column zero.  A following
      // "echon" will overwrite the message, unavoidably.
      msgColG = 0;
   else
      // When working silently: Put it back where it was, since nothing
      // should have been written.
      msgColG = saveMsgCol;
}

// "execute()" function
private void
f_execute(Var *argvars, Var *returnVar) {
   execute_common(argvars, returnVar, 0);
}

// "exists()" function
void
f_exists(Var *argvars, Var *returnVar) {
   int      n = FALSE;

   CS p = tv_get_string(&argvars[0]);
   if (*p == '$')  {       // environment variable
      // first try "normal" environment variables (fast)
      if (mch_getenv(p + 1))
         n = TRUE;
      else {
         // try expanding things like $EEGL and ${HOME}
         p = expand_env_save(p);
         if (p && *p != '$')
            n = TRUE;
         eeglFree(p);
      }
   }
   ei (*p == '&' || *p == '+') {       // option
      n = (eval_option(&p, NULL, TRUE) == OK);
      if (*skipwhite(p) != ZERO)
         n = FALSE;         // trailing garbage
   } ei (*p == '*') {       // internal or user defined function
      n = function_exists(p + 1, FALSE);
   }
   ei (*p == '?')   {      // internal function only
      n = has_internal_func_name(p + 1);
   } ei (*p == ':') {
      n = cmd_exists(p + 1);
   } ei (*p == '#') {
      if (p[1] == '#')
         n = autocmd_supported(p + 2);
      else
         n = au_exists(p + 1);
   } else {           // internal variable
      n = var_exists(p);
   }

   returnVar->number = n;
}

// "expand()" function
private void
f_expand(Var *argvars, Var *returnVar) {
   Unt   len;
   int      options = WILD_SILENT|WILD_USE_NL|WILD_LIST_NOTFOUND;
   Expand   xpc;
   Boole error = false;
   CS result;

   returnVar->tag = VAR_STRING;
   if (argvars[1].tag != VAR_UNKNOWN
          && argvars[2].tag != VAR_UNKNOWN
          && varGetNumberChk(argvars + 2, OUT &error)
          && !error)
      returnVar_list_set(returnVar, NULL);

   CS s = tv_get_string(&argvars[0]);
   if (*s == '%' || *s == '#' || *s == '<') {
      CS errorMsg = NULL;

      if (p_verbose == 0)
         ++emsg_off;
      result = evalVars(NULL, OUT &errorMsg, s, s, &len, NULL, FALSE);
      if (p_verbose == 0)
         --emsg_off;
      ei (errorMsg)
         emsg(errorMsg);
      if (returnVar->tag == VAR_LIST) {
         allocReturnList(returnVar);
         if (result)
            list_append_string(returnVar->list, result, -1);
         eeglFree(result);
      }
      else
          returnVar->string = result;
   } else {
      // When the optional second argument is non-zero, don't remove matches
      // for 'wildignore' and don't put matches for 'suffixes' at the end.
      if (argvars[1].tag != VAR_UNKNOWN && varGetNumberChk(argvars + 1, OUT &error))
         options |= WILD_KEEP_ALL;
      if (!error) {
         expandInit(&xpc);
         xpc.context = EXPAND_FILES;
         if (p_wic)
            options += WILD_ICASE;
         if (returnVar->tag == VAR_STRING)
            returnVar->string = expandWildcard(&xpc, s, NULL, options, WILD_ALL);
         else {
            allocReturnList(returnVar);
            expandWildcard(OUT &xpc, s, NULL, options, WILD_ALL_KEEP);
            for (Unt i = 0; i < xpc.files.len; i++)
               list_append_string(returnVar->list, xpc.files.c[i], -1);
            scrExpandCleanup(&xpc);
         }
      } else
         returnVar->string = NULL;
   }
}

// "expandcmd()" function
// Expand all the special characters in a command string.
private void
f_expandcmd(Var *argvars, Var *returnVar) {
   Invocation   invo;
   Byte   *cmdstr;
   CS errorMsg = NULL;
   Boole emsgoff = true;

   if (argvars[1].tag == VAR_BAG && bagGetBool(argvars[1].bag, tConst("errmsg"), false))
      emsgoff = false;

   returnVar->tag = VAR_STRING;
   cmdstr = copyStr(tv_get_string(&argvars[0]));

   CLEAR_FIELD(invo);
   invo.comm = cmdstr;
   invo.arg = cmdstr;
   invo.argFlags |= commandFlagNoSpacesInExtra();
   invo.usefilter = FALSE;
   invo.nextComm = NULL;
   invo.id = C_USER;

   if (emsgoff)
      ++emsg_off;
   if (expand_filename(&invo, &cmdstr, OUT &errorMsg) == FAIL)
      if (!emsgoff && errorMsg && *errorMsg != ZERO)
          emsg(errorMsg);
   if (emsgoff)
      --emsg_off;

   returnVar->string = cmdstr;
}

// "feedkeys()" function
private void
f_feedkeys(Var *argvars, Var *returnVar UNUSED) {
   Boole remap = true;
   Boole insert = false;
   Byte nbuf[NUMBUFLEN];
   Boole typed = false;
   Boole execute = false;
   Boole context = false;
   Boole dangerous = false;
   Boole lowlevel = false;

   CS keys = tv_get_string(&argvars[0]);

   if (argvars[1].tag != VAR_UNKNOWN) {
      CS flags = tv_get_string_buf(&argvars[1], nbuf);
      for ( ; *flags != ZERO; ++flags) {
         switch (*flags) {
         case 'n': remap = FALSE; break;
         case 'm': remap = TRUE; break;
         case 't': typed = TRUE; break;
         case 'i': insert = TRUE; break;
         case 'x': execute = TRUE; break;
         case 'c': context = TRUE; break;
         case '!': dangerous = TRUE; break;
         case 'L': lowlevel = TRUE; break;
         }
      }
   }

   if (*keys != ZERO || execute) {
      if (lowlevel) {
#ifdef USE_INPUT_BUF
         lo("feedkeys() lowlevel: %s", keys);

         int len = (int)STRLEN(keys);
         for (int idx = 0; idx < len; ++idx) {
            // if a CTRL-C was typed, set gotInterruptG, similar to what
            // happens in fill_input_buf()
            if (keys[idx] == 3 && ctrl_c_interrupts && typed)
                gotInterruptG = TRUE;
            add_to_input_buf(keys + idx, 1);
         }
#else
         emsg(_(e_lowlevel_input_not_supported));
#endif
      } else {
         //Need to escape K_SPECIAL and CSI before putting the string in the typeahead buffer.
         CS keys_esc = copyStr_escape_csi(keys);
         if (!keys_esc)
            return;

         lo("feedkeys(%s): %s", typed ? "typed" : "", keys);

         insertIntoTypebuf(keys_esc, (remap ? REMAP_YES : REMAP_NONE),
                  insert ? 0 : typeBufG.validLen, !typed, FALSE);
         if (vgetcBusyG || timer_busy || input_busy)
            typebuf_was_filled = TRUE;

         eeglFree(keys_esc);
      }

      if (execute) {
         int      save_msg_scroll = msg_scroll;
         ScriptPos   save_sctx;

         // Avoid a 1 second delay when the keys start Insert mode.
         msg_scroll = FALSE;

         lo("feedkeys() executing");

         if (context) {
            save_sctx = scriptPosG;
            scriptPosG.sid = 0;
         }

         if (!dangerous) {
            ++ex_normal_busy;
            ++in_feedkeys;
         }
         exec_normal(TRUE, lowlevel, TRUE);
         if (!dangerous) {
            --ex_normal_busy;
            --in_feedkeys;
         }

         msg_scroll |= save_msg_scroll;

         if (context)
            scriptPosG = save_sctx;
      }
   }
}

// "fnameescape({string})" function
private void
f_fnameescape(Var *argvars, Var *returnVar) {
   returnVar->string = copyStr_fnameescape(tv_get_string(&argvars[0]), VSE_NONE);
   returnVar->tag = VAR_STRING;
}

// "function()" and  "funcref()" function
private void
common_function(Var *argvars, Var *returnVar, int is_funcref) {
   Byte   *s;
   Byte   *name;
   int      use_string = FALSE;
   PartiallyApplied   *arg_pt = NULL;
   Byte   *trans_name = NULL;
   Boole is_global = false;
   Byte   *start_bracket = NULL;

   if (argvars[0].tag == VAR_FUNC) {
      // function(MyFunc, [arg], dict)
      s = argvars[0].string;
   } ei (argvars[0].tag == VAR_PARTIAL && argvars[0].partial) {
      // function(dict.MyFunc, [arg])
      arg_pt = argvars[0].partial;
      s = partial_name(arg_pt);
   } else {
      // function('MyFunc', [arg], dict)
      s = tv_get_string(&argvars[0]);
      use_string = TRUE;
   }
   if (s == NULL) {
      showErrFmtMsg(_(e_invalid_argument_str), "NULL");
      return;
   }

   if ((use_string && firstOccurrence(s, AUTOLOAD_CHAR) == NULL) || is_funcref) {
      name = s;
      trans_name = save_function_name(&name, OUT &is_global, FALSE,
            TFN_INT | TFN_QUIET | TFN_NO_AUTOLOAD | TFN_NO_DEREF, NULL);
      if (*name != ZERO)
         s = NULL;
   }

   if (s == NULL || *s == ZERO || (use_string && EE_ISDIGIT(*s))
                || (is_funcref && trans_name == NULL)) {
      showErrFmtMsg(_(e_invalid_argument_str),
                 use_string ? tv_get_string(&argvars[0]) : s);
   } 
   // Don't check an autoload name for existence here.
   ei (trans_name && (is_funcref
          ? find_func(trans_name, is_global) == NULL
          : !translated_function_exists(trans_name, is_global)))
      showErrFmtMsg(_(e_unknown_function_str_2), s);
   else {
      int   dict_idx = 0;
      int   arg_idx = 0;
      List   *list = NULL;

      if (STRNCMP(s, "s:", 2) == 0 || STRNCMP(s, "<SID>", 5) == 0)
          // Expand s: and <SID> into <SNR>nr_, so that the function can
          // also be called from another script. Using trans_function_name()
          // would also work, but some plugins depend on the name being
          // printable text.
          name = get_scriptlocal_funcname(s);
      ei (trans_name && *trans_name == K_SPECIAL)
          name = alloc_printable_func_name(trans_name);
      else
          name = copyStr(s);

      if (argvars[1].tag != VAR_UNKNOWN) {
         if (argvars[2].tag != VAR_UNKNOWN) {
            // function(name, [args], dict)
            arg_idx = 1;
            dict_idx = 2;
         } ei (argvars[1].tag == VAR_BAG)
            // function(name, dict)
            dict_idx = 1;
         else
            // function(name, [args])
            arg_idx = 1;
         if (dict_idx > 0) {
            if (check_for_dict_arg(argvars, dict_idx) == FAIL) {
                eeglFree(name);
                goto theend;
            }
            if (!argvars[dict_idx].bag)
               dict_idx = 0;
         }
         if (arg_idx > 0) {
            if (argvars[arg_idx].tag != VAR_LIST) {
                emsg(_(e_second_argument_of_function_must_be_list_or_dict));
                eeglFree(name);
                goto theend;
            }
            list = argvars[arg_idx].list;
            if (list == NULL || list->len == 0)
               arg_idx = 0;
            ei (list->len > MAX_FUNC_ARGS) {
               emsg_funcname(e_too_many_arguments_for_function_str, s);
               eeglFree(name);
               goto theend;
            }
         }
      }
      if (dict_idx > 0 || arg_idx > 0 || arg_pt || is_funcref) {
          PartiallyApplied   *pt = ALLOC_CLEAR_ONE(PartiallyApplied);

         // result is a VAR_PARTIAL
         if (pt == NULL)
            eeglFree(name);
         else {
            if (arg_idx > 0 || (arg_pt && arg_pt->argc > 0)) {
               ListItem   *li;
               int      i = 0;
               int      arg_len = 0;
               int      len = 0;

               if (arg_pt)
                  arg_len = arg_pt->argc;
               if (list)
                  len = list->len;
               pt->argc = arg_len + len;
               pt->argv = ALLOC_MULT(Var, pt->argc);
               for (i = 0; i < arg_len; i++)
                  copy_tv(OUT pt->argv + i, &arg_pt->argv[i]);
               if (len > 0) {
                  CHECK_LIST_MATERIALIZE(list);
                  FOR_ALL_LIST_ITEMS(list, li) {
                     copy_tv(OUT pt->argv + i, &li->c);
                     i++; 
                  } 
               }
            }

            // For "function(dict.func, [], dict)" and "func" is a partial
            // use "dict". That is backwards compatible.
            if (dict_idx > 0) {
               // The dict is bound explicitly, auto is FALSE.
               pt->self = argvars[dict_idx].bag;
               ++pt->self->refcount;
            } ei (arg_pt) {
               // If the dict was bound automatically the result is also bound automatically.
               pt->self = arg_pt->self;
               pt->isAuto = arg_pt->isAuto;
               if (pt->self)
                  ++pt->self->refcount;
            }

            pt->refcount = 1;
            if (arg_pt && arg_pt->fn) {
               pt->fn = arg_pt->fn;
               func_ptr_ref(pt->fn);
               eeglFree(name);
            } ei (is_funcref) {
               pt->fn = find_func(trans_name, is_global);
               func_ptr_ref(pt->fn);
               eeglFree(name);
            } else {
               pt->name = name;
               func_ref(name);
            }

         }
         returnVar->tag = VAR_PARTIAL;
         returnVar->partial = pt;
      } else {
         // result is a VAR_FUNC
         returnVar->tag = VAR_FUNC;
         if (start_bracket == NULL) {
            returnVar->string = name;
            func_ref(name);
         } else {
            // generic function
            STRCPY(IObuff, name);
            STRCAT(IObuff, start_bracket);
            returnVar->string = copyStr(IObuff);
            eeglFree(name);
         }
      }
   }
theend:
   eeglFree(trans_name);
}

private void
f_funcref(Var *argvars, Var *returnVar) {
   common_function(argvars, returnVar, TRUE);
}

private void
f_function(Var *argvars, Var *returnVar) {
   common_function(argvars, returnVar, FALSE);
}

private void
f_garbagecollect(Var *argvars, Var *returnVar UNUSED) {
   // This is postponed until we are back at the toplevel, because we may be
   // using Lists and Dicts internally.  E.g.: ":echo [garbagecollect()]".
   want_garbage_collect = TRUE;

   if (argvars[0].tag != VAR_UNKNOWN && tv_get_bool(&argvars[0]) == 1)
   garbage_collect_at_exit = TRUE;
}

private void
f_get(Var *argvars, Var *returnVar) {
   ListItem   *li;
   List   *l;
   DictItem   *di;
   Bag   *d;
   Var   *tv = NULL;
   int      what_is_dict = FALSE;

   if (argvars[0].tag == VAR_BLOB) {
      Boole error = false;
      int idx = varGetNumberChk(argvars + 1, OUT &error);

      if (!error) {
         returnVar->tag = VAR_NUMBER;
         if (idx < 0)
            idx = blob_len(argvars[0].blob) + idx;
         if (idx < 0 || idx >= blob_len(argvars[0].blob))
            returnVar->number = -1;
         else {
            returnVar->number = blob_get(argvars[0].blob, idx);
            tv = returnVar;
         }
      }
   } ei (argvars[0].tag == VAR_LIST) {
      if ((l = argvars[0].list)) {
         Boole error = false;

         li = list_find(l, (long)varGetNumberChk(argvars + 1, OUT &error));
         if (!error && li)
            tv = &li->c;
      }
   } ei (argvars[0].tag == VAR_BAG) {
      if ((d = argvars[0].bag)) {
         di = bagFind(d, mbText(tv_get_string(&argvars[1])));
         if (di)
            tv = &di->c;
      }
   } ei (argvars[0].tag == VAR_PARTIAL || argvars[0].tag == VAR_FUNC) {
      PartiallyApplied   *pt;
      PartiallyApplied   fref_pt;

      if (argvars[0].tag == VAR_PARTIAL)
          pt = argvars[0].partial;
      else {
          CLEAR_FIELD(fref_pt);
          fref_pt.name = argvars[0].string;
          pt = &fref_pt;
      }

      if (pt) {
         CS what = tv_get_string(&argvars[1]);

         if (STRCMP(what, "func") == 0 || STRCMP(what, "name") == 0) {
            CS name = partial_name(pt);

            returnVar->tag = (*what == 'f' ? VAR_FUNC : VAR_STRING);
            if (name == NULL)
               returnVar->string = NULL;
            else {
               if (returnVar->tag == VAR_FUNC)
                  func_ref(name);
               if (*what == 'n' && pt->name == NULL && pt->fn)
                  // use <SNR> instead of the byte code
                  name = printable_func_name(pt->fn);
               returnVar->string = copyStr(name);
            }
         } ei (STRCMP(what, "dict") == 0) {
            what_is_dict = TRUE;
            if (pt->self)
               returnVar_dict_set(returnVar, pt->self);
         } ei (STRCMP(what, "args") == 0) {
            returnVar->tag = VAR_LIST;
            allocReturnList(returnVar);
            for (int i = 0; i < pt->argc; ++i) {
               list_append_tv(returnVar->list, &pt->argv[i]);
            } 
         } ei (STRCMP(what, "arity") == 0) {
            int required = 0, optional = 0, varargs = FALSE;
            CS name = partial_name(pt);

            get_func_arity(name, &required, &optional, &varargs);

            returnVar->tag = VAR_BAG;
            allocReturnDict(returnVar);
            Bag* b = returnVar->bag;

            // Take into account the arguments of the partial, if any.
            // Note that it is possible to supply more arguments than the function accepts.
            if (pt->argc >= required + optional)
               required = optional = 0;
            ei (pt->argc > required) {
               optional -= pt->argc - required;
               required = 0;
            } else
               required -= pt->argc;

             bagAddNumber(b, S"required", required);
             bagAddNumber(b, S"optional", optional);
             bagAdd_bool(b, S"varargs", varargs);
         } else
            showErrFmtMsg(_(e_invalid_argument_str), what);

         // When {what} == "dict" and pt->self == NULL, evaluate the third argument
         if (!what_is_dict)
            return;
      }
   } else
      showErrFmtMsg(_(e_argument_of_str_must_be_list_tuple_dictionary_or_blob), "get()");

   if (!tv) {
      if (argvars[2].tag != VAR_UNKNOWN)
         copy_tv(OUT returnVar, &argvars[2]);
   } else
      copy_tv(OUT returnVar, tv);
}

// "getcellpixels()" function
private void
f_getcellpixels(Var *argvars UNUSED, Var *returnVar) {
   allocReturnList(returnVar);

   struct cellsize cs;
   mch_calc_cell_size(&cs);

   // failed get pixel size.
   if (cs.cs_xpixel == -1)
      return;

   // success pixel size and no gui.
   list_append_number(returnVar->list, (Long)cs.cs_xpixel);
   list_append_number(returnVar->list, (Long)cs.cs_ypixel);

}

private void
f_getchangelist(Var *argvars, Var *returnVar) {

   allocReturnList(returnVar);

   Book* book;
   if (argvars[0].tag == VAR_UNKNOWN)
      book = curBook;
   else
      book = daGetBookFromArg(&argvars[0]);
   if (!book)
      return;

   List* l = list_alloc();
   if (list_append_list(returnVar->list, l) == FAIL) {
      eeglFree(l);
      return;
   }

   //The current window change list index tracks only the position for the
   //current book. For other books use the stored index for the current
   //portal, or, if that's not available, the change list length.
   Unt changelistindex;
   if (book == curPor->book) {
      changelistindex = curPor->changeListInd;
   } else {
      PortInfo   *wip;

      FOR_ALL_BOOK_PORTINFOS(book, wip) {
         if (wip->portal == curPor)
            break;
      } 
      changelistindex = wip ? (Unt)wip->wi_changelistidx : book->changeListLen;
   }
   list_append_number(returnVar->list, (Long)changelistindex);

   for (Unt i = 0; i < book->changeListLen; ++i) {
      if (book->changeList[i].lnum == 0)
         continue;
      Bag* b = allocBag();
      if (listAppendBag(l, b) == FAIL)
         return;
      bagAddNumber(b, S"lnum", (long)book->changeList[i].lnum);
      bagAddNumber(b, S"col", (long)book->changeList[i].col);
      bagAddNumber(b, S"coladd", (long)book->changeList[i].coladd);
   }
}

private void
getpos_both(
   Var   *argvars,
   Var   *returnVar,
   int      getcurpos,
   int      charcol)
{
   Pos   *fp = NULL;
   Pos   pos;
   Portal   *wp = curPor;
   int fnum = -1;

   allocReturnList(returnVar);
   List* l = returnVar->list;
   if (getcurpos) {
      if (argvars[0].tag != VAR_UNKNOWN) {
         wp = portFindByNrOrId(&argvars[0]);
         if (wp)
            fp = &wp->cursor;
      } else
         fp = &curPor->cursor;
      if (fp && charcol) {
         pos = *fp;
         pos.col = buf_byteidx_to_charidx(wp->book, pos.lnum, pos.col);
         fp = &pos;
      }
   } else
      fp = var2fpos(&argvars[0], TRUE, &fnum, charcol);
   if (fnum != -1)
      list_append_number(l, (Long)fnum);
   else
      list_append_number(l, (Long)0);
      
   list_append_number(l, (fp) ? (Long)fp->lnum : (Long)0);
   list_append_number(l, (fp) 
       ? (Long)(fp->col == MAXCOL ? MAXCOL : fp->col + 1)
       : (Long)0);
   list_append_number(l, (fp) ? (Long)fp->coladd : (Long)0);
   if (getcurpos) {
      Boole save_set_curswant = curPor->setCursWant;
      ColNr save_curswant = curPor->cursWant;
      ColNr save_virtcol = curPor->virtCol;

      if (wp == curPor)
         update_curswant();
      list_append_number(l, wp == NULL ? 0 : wp->cursWant == MAXCOL
          ?  (Long)MAXCOL : (Long)wp->cursWant + 1);

      // Do not change "curswant", as it is unexpected that a get
      // function has a side effect.
      if (wp == curPor && save_set_curswant) {
         curPor->setCursWant = save_set_curswant;
         curPor->cursWant = save_curswant;
         curPor->virtCol = save_virtcol;
         curPor->cacheState &= ~VALID_VIRTCOL;
      }
   }
}

private void
f_getcharpos(Var *argvars UNUSED, Var *returnVar) {
   getpos_both(argvars, returnVar, FALSE, TRUE);
}

private void
f_getcharsearch(Var *argvars UNUSED, Var *returnVar) {
   allocReturnDict(returnVar);
   Bag *bag = returnVar->bag;

   bagAddString(bag, S"char", last_csearch());
   bagAddNumber(bag, S"forward", last_csearch_forward());
   bagAddNumber(bag, S"until", last_csearch_until());
}

private void
f_getenv(Var *argvars, Var *returnVar) {
   int       mustfree = FALSE;

   CS p = eeglGetEnv(tv_get_string(&argvars[0]));
   if (p == NULL) {
      returnVar->tag = VAR_SPECIAL;
      returnVar->number = VVAL_NULL;
      return;
   }
   if (!mustfree)
      p = copyStr(p);
   returnVar->string = p;
   returnVar->tag = VAR_STRING;
}

// "getfontname()" function
private void
f_getfontname(Var *argvars UNUSED, Var *returnVar) {
   returnVar->tag = VAR_STRING;
   returnVar->string = NULL;
}

// "getjumplist()" function
private void
f_getjumplist(Var *argvars, Var *returnVar) {
   allocReturnList(returnVar);

   Portal* wp = find_tabwin(&argvars[0], &argvars[1], NULL);
   if (!wp)
      return;

   cleanup_jumplist(wp, TRUE);

   List* l = list_alloc();
   if (list_append_list(returnVar->list, l) == FAIL) {
      eeglFree(l);
      return;
   }

   list_append_number(returnVar->list, (Long)wp->jumpListInd);

   for (int i = 0; i < wp->jumpListLen; ++i) {
      if (wp->jumpList[i].fmark.mark.lnum == 0)
         continue;
      Bag* b = allocBag();
      if (listAppendBag(l, b) == FAIL)
         return;
      bagAddNumber(b, S"lnum", (long)wp->jumpList[i].fmark.mark.lnum);
      bagAddNumber(b, S"col", (long)wp->jumpList[i].fmark.mark.col);
      bagAddNumber(b, S"coladd", (long)wp->jumpList[i].fmark.mark.coladd);
      bagAddNumber(b, S"bufnr", (long)wp->jumpList[i].fmark.fnum);
      if (wp->jumpList[i].fname)
         bagAddString(b, S"filename", wp->jumpList[i].fname);
    }
}

// "getpid()" function
private void
f_getpid(Var *argvars UNUSED, Var *returnVar) {
   returnVar->number = mch_get_pid();
}

// "getcurpos()" function
private void
f_getcurpos(Var *argvars, Var *returnVar) {
   getpos_both(argvars, returnVar, TRUE, FALSE);
}

private void
f_getcursorcharpos(Var *argvars, Var *returnVar) {
   getpos_both(argvars, returnVar, TRUE, TRUE);
}

//"getpos(string)" function
private void
f_getpos(Var *argvars, Var *returnVar) {
   getpos_both(argvars, returnVar, FALSE, FALSE);
}

//}}}
//{{{API functions 3

// Convert from block_def to string
private CS
block_def2str(BlockDef *bd) {
   Unt size = bd->startspaces + bd->endspaces + bd->textlen;

   CS ret = alloc(size + 1);
   CS p = ret;
   memset(p, ' ', bd->startspaces);
   p += bd->startspaces;
   mch_memmove(p, bd->textstart, bd->textlen);
   p += bd->textlen;
   memset(p, ' ', bd->endspaces);
   *(p + bd->endspaces) = ZERO;
   return ret;
}

private int
getregionpos(
   Var* argvars,
   Var* returnVar,
   Pos* p1,
   Pos* p2,
   int* inclusive,
   int* region_type,
   Operator   *oper
){
   int      fnum1 = -1, fnum2 = -1;
   Byte   *type;
   Book   *findbuf;
   Byte   default_type[] = "v";
   int      block_width = 0;
   int      l;

   allocReturnList(returnVar);

   if (confirmVarIsList(argvars, 0) == FAIL
          || confirmVarIsList(argvars, 1) == FAIL
          || check_for_oself_arg(argvars, 2) == FAIL)
      return FAIL;

   if (list2fpos(&argvars[0], p1, &fnum1, NULL, FALSE) != OK
          || list2fpos(&argvars[1], p2, &fnum2, NULL, FALSE) != OK
          || fnum1 != fnum2)
      return FAIL;

   if (argvars[2].tag == VAR_BAG) {
      type = bagGetString(argvars[2].bag, tConst("type"), FALSE);
      if (!type)
          type = default_type;
   } else {
      type = default_type;
   }

   if (type[0] == 'v' && type[1] == ZERO)
      *region_type = MCHAR;
   ei (type[0] == 'V' && type[1] == ZERO)
      *region_type = MLINE;
   ei (type[0] == Ctrl_V) {
      Byte *p = type + 1;

      if (*p != ZERO && ((block_width = getdigits(&p)) <= 0 || *p != ZERO)) {
         showErrFmtMsg(_(e_invalid_value_for_argument_str_str), "type", type);
         return FAIL;
      }
      *region_type = MBLOCK;
   } else {
      showErrFmtMsg(_(e_invalid_value_for_argument_str_str), "type", type);
      return FAIL;
   }

   findbuf = fnum1 != 0 ? bookFindFileByBookNr(fnum1) : curBook;
   if (findbuf == NULL || findbuf->mem.mfile == NULL) {
      emsg(_(e_buffer_is_not_loaded));
      return FAIL;
   }

   if (p1->lnum < 1 || p1->lnum > findbuf->mem.lineCount) {
      showErrFmtMsg(_(e_invalid_line_number_nr), p1->lnum);
      return FAIL;
   }
   if (p1->col == MAXCOL)
      p1->col = memGetBookLen(findbuf, p1->lnum) + 1;
   ei (p1->col < 1 || p1->col > memGetBookLen(findbuf, p1->lnum) + 1) {
      showErrFmtMsg(_(e_invalid_column_number_nr), p1->col);
      return FAIL;
   }

   if (p2->lnum < 1 || p2->lnum > findbuf->mem.lineCount) {
      showErrFmtMsg(_(e_invalid_line_number_nr), p2->lnum);
      return FAIL;
   }
   if (p2->col == MAXCOL)
      p2->col = memGetBookLen(findbuf, p2->lnum) + 1;
   ei (p2->col < 1 || p2->col > memGetBookLen(findbuf, p2->lnum) + 1) {
      showErrFmtMsg(_(e_invalid_column_number_nr), p2->col);
      return FAIL;
   }

   curBook = findbuf;
   curPor->book = curBook;
   virtual_op = virtual_active();

   // NOTE: Adjustment is needed.
   p1->col--;
   p2->col--;

   if (!LT_POS(*p1, *p2)) {
      // swap position
      Pos p;

      p = *p1;
      *p1 = *p2;
      *p2 = p;
   }

   if (*region_type == MCHAR) {
      // If p2 is on ZERO (end of line), inclusive becomes false.
      if (*inclusive && !virtual_op && *ml_get_pos(p2) == ZERO)
          *inclusive = FALSE;
   } ei (*region_type == MBLOCK) {
      ColNr sc1, ec1, sc2, ec2;

      bookGetVirtualColInVirtualMode(curPor, p1, &sc1, NULL, &ec1);
      bookGetVirtualColInVirtualMode(curPor, p2, &sc2, NULL, &ec2);
      oper->motion_type = MBLOCK;
      oper->inclusive = TRUE;
      oper->op_type = OP_NOP;
      oper->start = *p1;
      oper->end = *p2;
      oper->start_vcol = MIN(sc1, sc2);
      if (block_width > 0)
          oper->end_vcol = oper->start_vcol + block_width - 1;
      else
          oper->end_vcol = MAX(ec1, ec2);
   }

   // Include the trailing byte of a multi-byte char.
   l = utfCharLen((CS)ml_get_pos(p2));
   if (l > 1)
      p2->col += l - 1;

   return OK;
}

// "getregion()" function
private void
f_getregion(Var *argvars, Var *returnVar) {
   Pos      p1, p2;
   int         inclusive = TRUE;
   int         region_type = -1;
   Operator      oa;

   Book      *save_curbuf;
   int         save_virtual;
   Byte      *akt = NULL;
   LineNr      lnum;

   save_curbuf = curBook;
   save_virtual = virtual_op;

   if (getregionpos(argvars, returnVar, &p1, &p2, &inclusive, &region_type, &oa) == FAIL)
      return;

   for (lnum = p1.lnum; lnum <= p2.lnum; lnum++) {
      int ret = 0;
      BlockDef   bd;

      if (region_type == MLINE)
         akt = copyStr(ml_get(lnum));
      ei (region_type == MBLOCK) {
         block_prep(&oa, OUT &bd, lnum, false);
         akt = block_def2str(&bd);
      } ei (p1.lnum < lnum && lnum < p2.lnum)
         akt = copyStr(ml_get(lnum));
      else {
         charwise_block_prep(p1, p2, &bd, lnum, inclusive);
         akt = block_def2str(&bd);
      }

      if (akt) {
         ret = list_append_string(returnVar->list, akt, -1);
         eeglFree(akt);
      }

      if (akt == NULL || ret == FAIL) {
         clearVar(returnVar);
         allocReturnList(returnVar);
         break;
      }
   }

   // getregionpos() may change curBook and virtual_op
   curBook = save_curbuf;
   curPor->book = curBook;
   virtual_op = save_virtual;
}

private void
add_regionpos_range(Var *returnVar, Pos p1, Pos p2) {
   List* l1 = list_alloc();
   if (list_append_list(returnVar->list, l1) == FAIL) {
      eeglFree(l1);
      return;
   }

   List* l2 = list_alloc();
   if (list_append_list(l1, l2) == FAIL) {
      eeglFree(l1);
      eeglFree(l2);
      return;
   }

   List* l3 = list_alloc();
   if (list_append_list(l1, l3) == FAIL) {
      eeglFree(l1);
      eeglFree(l2);
      eeglFree(l3);
      return;
   }

   list_append_number(l2, curBook->fiNum);
   list_append_number(l2, p1.lnum);
   list_append_number(l2, p1.col);
   list_append_number(l2, p1.coladd);

   list_append_number(l3, curBook->fiNum);
   list_append_number(l3, p2.lnum);
   list_append_number(l3, p2.col);
   list_append_number(l3, p2.coladd);
}

private void
f_getregionpos(Var *argvars, Var *returnVar) {
   Pos   p1, p2;
   int      inclusive = TRUE;
   int      region_type = -1;
   int      allow_eol = FALSE;
   Operator   oa;
   int      lnum;

   Book* save_curbuf = curBook;
   int save_virtual = virtual_op;

   if (getregionpos(argvars, returnVar, &p1, &p2, &inclusive, &region_type, &oa) == FAIL)
      return;

   if (argvars[2].tag == VAR_BAG)
      allow_eol = bagGetBool(argvars[2].bag, tConst("eol"), false);

   for (lnum = p1.lnum; lnum <= p2.lnum; lnum++) {
      Pos      ret_p1, ret_p2;
      Byte      *line = ml_get(lnum);
      ColNr      line_len = ml_get_len(lnum);

      if (region_type == MLINE) {
          ret_p1.col = 1;
          ret_p1.coladd = 0;
          ret_p2.col = MAXCOL;
          ret_p2.coladd = 0;
      } else {
         BlockDef   bd;

         if (region_type == MBLOCK)
            block_prep(&oa, OUT &bd, lnum, false);
         else
            charwise_block_prep(p1, p2, &bd, lnum, inclusive);

         if (bd.is_oneChar) { // selection entirely inside one char
            if (region_type == MBLOCK) {
                ret_p1.col = mb_prevptr(line, bd.textstart) - line + 1;
                ret_p1.coladd = bd.start_char_vcols - (bd.start_vcol - oa.start_vcol);
            } else {
                ret_p1.col = p1.col + 1;
                ret_p1.coladd = p1.coladd;
            }
         } ei (region_type == MBLOCK && oa.start_vcol > bd.start_vcol) {
            // blockwise selection entirely beyond end of line
            ret_p1.col = MAXCOL;
            ret_p1.coladd = oa.start_vcol - bd.start_vcol;
            bd.is_oneChar = TRUE;
         } ei (bd.startspaces > 0) {
            ret_p1.col = mb_prevptr(line, bd.textstart) - line + 1;
            ret_p1.coladd = bd.start_char_vcols - bd.startspaces;
         } else {
            ret_p1.col = bd.textcol + 1;
            ret_p1.coladd = 0;
         }

          if (bd.is_oneChar) { // selection entirely inside one char
            ret_p2.col = ret_p1.col;
            ret_p2.coladd = ret_p1.coladd + bd.startspaces + bd.endspaces;
          } ei (bd.endspaces > 0) {
            ret_p2.col = bd.textcol + bd.textlen + 1;
            ret_p2.coladd = bd.endspaces;
          } else {
            ret_p2.col = bd.textcol + bd.textlen;
            ret_p2.coladd = 0;
          }
      }

      if (!allow_eol && ret_p1.col > line_len) {
         ret_p1.col = 0;
         ret_p1.coladd = 0;
      } ei (ret_p1.col > line_len + 1)
         ret_p1.col = line_len + 1;

      if (!allow_eol && ret_p2.col > line_len) {
         ret_p2.col = ret_p1.col == 0 ? 0 : line_len;
         ret_p2.coladd = 0;
      } ei (ret_p2.col > line_len + 1)
         ret_p2.col = line_len + 1;

      ret_p1.lnum = lnum;
      ret_p2.lnum = lnum;
      add_regionpos_range(returnVar, ret_p1, ret_p2);
   }

   // getregionpos() may change curBook and virtual_op
   curBook = save_curbuf;
   curPor->book = curBook;
   virtual_op = save_virtual;
}

// Common between getreg(), getreginfo() and getregtype(): get the register
// name from the first argument. Return 0 on error.
private int
getreg_get_regname(Var *argvars) {
   CS strregname;

   if (argvars[0].tag != VAR_UNKNOWN) {
      strregname = convertVarToStringSingleUse(&argvars[0]);
      if (!strregname)       // type error; errmsg already given
          return 0;
   } else
      // Default to v:register
      strregname = get_EeglVar_str(VV_REG);

   return *strregname == 0 ? '"' : *strregname;
}

// "getreg()" function
private void
f_getreg(Var *argvars, Var *returnVar) {
   int arg2 = FALSE;
   int return_list = FALSE;

   int regname = getreg_get_regname(argvars);
   if (regname == 0)
      return;

   if (argvars[0].tag != VAR_UNKNOWN && argvars[1].tag != VAR_UNKNOWN) {
      Boole error = false;
      arg2 = (int)varGetNumberChk(argvars + 1, OUT &error);

      if (!error && argvars[2].tag != VAR_UNKNOWN)
         return_list = (int)varGetNumberChk(argvars + 2, OUT &error);
      if (error)
         return;
    }

   if (return_list) {
      returnVar->tag = VAR_LIST;
      returnVar->list = (List *)get_reg_contents(regname, (arg2 ? GREG_EXPR_SRC : 0) | GREG_LIST);
      if (returnVar->list == NULL)
         allocReturnList(returnVar);
      else
         ++returnVar->list->refcount;
   } else {
      returnVar->tag = VAR_STRING;
      returnVar->string = get_reg_contents(regname, arg2 ? GREG_EXPR_SRC : 0);
   }
}

private void
f_getregtype(Var *argvars, Var *returnVar) {
   Byte buf[NUMBUFLEN + 2];
   long reglen = 0;

   // on error return an empty string
   returnVar->tag = VAR_STRING;
   returnVar->string = NULL;

   int regname = getreg_get_regname(argvars);
   if (regname == 0)
      return;

   buf[0] = ZERO;
   buf[1] = ZERO;
   switch (get_reg_type(regname, &reglen)) {
   case MLINE: buf[0] = 'V'; break;
   case MCHAR: buf[0] = 'v'; break;
   case MBLOCK:
      buf[0] = Ctrl_V;
      sprintf((char *)buf + 1, "%ld", reglen + 1);
      break;
   }
   returnVar->string = copyStr(buf);
}

private void
f_gettagstack(Var *argvars, Var *returnVar) {
   Portal   *wp = curPor;         // default is current portal

   allocReturnDict(returnVar);

   if (argvars[0].tag != VAR_UNKNOWN) {
      wp = portFindByNrOrId(&argvars[0]);
      if (wp == NULL)
          return;
   }

   get_tagstack(wp, returnVar->bag);
}

private void
f_gettext(Var *argvars, Var *returnVar) {

   if (check_for_nonempty_string_arg(argvars, 0) == FAIL 
         || check_for_opt_string_arg(argvars, 1) == FAIL)
      return;

   returnVar->tag = VAR_STRING;

   CS prev = NULL;
   if (argvars[1].tag == VAR_STRING && argvars[1].string && *(argvars[1].string) != ZERO) {
      returnVar->string = copyStr(
            (CS)dgettext((const char *)argvars[1].string, (const char *)argvars[0].string)
      );

      if (prev)
         bind_textdomain_codeset((const char *)argvars[1].string, (char*)prev);
   } else
      returnVar->string = copyStr((CS)_(argvars[0].string));
}

//{{{has function

void
f_has(Var *argvars, Var *returnVar) {
   int      i;
   Byte   *name;
   int      x = FALSE;
   int      n = FALSE;
   typedef struct {
      char *name;
      short present;
   } has_item_T;
    static has_item_T has_list[] = {
   {"linux",
      1
      },
   {"unix",
      1
      },
   {"ebcdic", 0 },
   {"fname_case",
      1
      },
   {"autocmd", 1},
   {"autochdir",
      0
      },
   {"balloon_eval",
      0
      },
   {"balloon_multiline",
      0
      },
   {"balloon_eval_term",
      1
      },
   {"builtin_terms", 1},
   {"all_builtin_terms", 1},
   {"browsefilter",
      0
      },
   {"byte_offset",
      1
      },
   {"channel", 1},
   {"cindent", 1},
   {"clientserver",
      1
      },
   {"clipboard", 1   },
   {"cmdline_compl", 1},
   {"cmdline_hist", 1},
   {"cmdwin", 1},
   {"comments", 1},
   {"conceal", 0 },
   {"cscope",
      1
      },
   {"cursorbind", 1},
   {"debug",
#ifdef DEBUG
      1
#else
      0
#endif
      },
   {"diff",
      1
      },
   {"digraphs", 0 },
   {"dnd",
      1
      },
   {"emacs_tags", 0},
   {"eval", 1},      // always present, of course!
   {"ex_extra", 1},   // graduated feature
   {"extra_search",
      1
      },
   {"file_in_path", 1},
   {"filterpipe",
      1
      },
   {"find_in_path",
      1
      },
   {"float", 1},
   {"folding",
      1
      },
   {"footer", 0},
   {"fork",
      1
      },
   {"gettext",
      1
      },
   {"gui",
      0
      },
   {"gui_neXtaw", 0 },
   {"gui_athena", 0 },
   {"insert_expand", 1},
   {"ipv6", 0},
   {"job",   1},
   {"jumplist", 1},
   {"keymap", 1 },
   {"lambda", 1}, 
   {"langmap", 1},
   {"libcall", 0 },
   {"linebreak", 1   },
   {"listcmds", 1},
   {"localmap", 1},
   {"lua",
      0
      },
   {"mksession",
      1
      },
   {"modify_fname", 1},
   {"mouse", 1},
   {"mouse_sgr",
      1
      },
   {"multi_byte", 1},
   {"multi_byte_ime",
#ifdef FEAT_MBYTE_IME
      1
#else
      0
#endif
      },
   {"multi_lang", 1 },
   {"nanotime",
#ifdef ST_MTIM_NSEC
      1
#else
      0
#endif
   },
   {"num64", 1},
   {"packages",
      1
      },
   {"path_extra", 1},
   {"persistent_undo",
      1
      },
   {"popupwin", 1 },
   {"prof_nsec",
#ifdef PROF_NSEC
      1
#else
      0
#endif
      },
   {"reltime",
      1
      },
   {"quickfix",
      1
      },
   {"rightleft", 0
      },
   {"ruby", 0 },
   {"scrollbind", 1},
   {"showcmd", 1},
   {"cmdline_info", 1},
   {"signs", 1 },
   {"smartindent", 1},
   {"statusline",
      1
      },
   {"sound", 0 },
   {"spell", 1 },
   {"syntax", 1 },
   {"system",
      0
      },
   {"tabpanel", 1 },
   {"tag_binary", 1},   // graduated feature
   {"termguicolors",
      1
      },
   {"terminal",
      1
      },
   {"termresponse",
      1
      },
   {"textobjects", 1},
   {"textprop",
      1
      },
   {"tgetent",
      1
      },
   {"timers",
      1
      },
   {"title", 1},
   {"toolbar",
      0
      },
   {"unnamedplus",
      1
      },
   {"user-commands", 1},    // was accidentally included in 5.4
   {"user_commands", 1},
   {"vartabs",
      0
      },
   {"vertsplit", 1},
   {"eeglinfo",
      1
      },
   {"vimscript-1", 1},
   {"vimscript-2", 1},
   {"vimscript-3", 1},
   {"vimscript-4", 1},
   {"virtualedit", 1},
   {"visual", 1},
   {"visualextra", 1},
   {"vreplace", 1},
   {"vtp",
      0
      },
   {"wayland",
#ifdef FEAT_WAYLAND
      1
#else
      0
#endif
      },
   {"wayland_clipboard",
#ifdef FEAT_WAYLAND
      1
#else
      0
#endif
      },
   {"wildignore", 1},
   {"wildmenu", 1},
   {"windows", 1},
   {"xattr",
      1
      },
   {"xim", 0 },
   {"xfontset",
#ifdef HAVE_X11
      1
#else
      0
#endif
      },
   {"xterm_clipboard",
#ifdef FEAT_X11
      1
#else
      0
#endif
      },
   {"xterm_save",
#ifdef FEAT_XTERM_SAVE
      1
#else
      0
#endif
      },
   {"X11",
#if defined(FEAT_X11)
      1
#else
      0
#endif
      },
   {NULL, 0}
   };

   name = tv_get_string(&argvars[0]);
   for (i = 0; has_list[i].name; ++i) {
      if (caseInsensitiveCompare(name, has_list[i].name) == 0) {
          x = TRUE;
          n = has_list[i].present;
          break;
      }
   } 

   // features also in has_list[] but sometimes enabled at runtime
   if (x == TRUE && n == FALSE) {
      if (0) {
          // intentionally empty
      }
   }

   if (argvars[1].tag != VAR_UNKNOWN && tv_get_bool(&argvars[1]))
      // return whether feature could ever be enabled
      returnVar->number = x;
   else
      // return whether feature is enabled
      returnVar->number = n;
}

//}}}

//Return TRUE if "feature" can change later.
//Also when checking for the feature has side effects, such as loading a DLL.
int
dynamic_feature(CS feature) {
    return (!feature
       || caseInsensitiveCompare(feature, "syntax_items") == 0
       // once "starting" is zero it will stay that way
       || (caseInsensitiveCompare(feature, "eegl_starting") == 0 && starting != 0)
       || caseInsensitiveCompare(feature, "multi_byte_encoding") == 0
       );
}

private void
f_haslocaldir(Var *argvars, Var *returnVar) {
   Tab   *t = NULL;

   Portal* wp = find_tabwin(&argvars[0], &argvars[1], OUT &t);

   // Check for window-local and tab-local directories
   if (wp && wp->localDir != NULL)
      returnVar->number = 1;
   ei (t && t->localdir != NULL)
      returnVar->number = 2;
   else
      returnVar->number = 0;
}

// "highlightID(name)" function
void
f_hlID(Var *argvars, Var *returnVar) {
   returnVar->number = hiliteGroupByName(mbText(tv_get_string(&argvars[0])));
}

// "highlight_exists()" function
void
f_hlexists(Var *argvars, Var *returnVar) {
   returnVar->number = hiliteExists(mbText(tv_get_string(&argvars[0])));
}

// "hostname()" function
void
f_hostname(Var *argvars UNUSED, Var *returnVar) {
   Byte hostname[256];

   mch_get_host_name(hostname, 256);
   returnVar->tag = VAR_STRING;
   returnVar->string = copyStr(hostname);
}

//"id()" function. Identity. Return address of item as a hex string, %p format.
//Currently only valid for object/container types. Return empty string if not an object.
void
f_id(Var *argvars, Var *returnVar) {
   Byte numbuf[NUMBUFLEN];
   CS p = numbuf;

   switch (argvars[0].tag) {
   case VAR_LIST:
   case VAR_BAG:
   case VAR_JOB:
   case VAR_CHANNEL:
   case VAR_BLOB:
   default:
       break;
   }
   *p = ZERO;

   returnVar->tag = VAR_STRING;
   returnVar->string = copyStr((CS)numbuf);
}

// index() function for a blob
private void
index_func_blob(Var *argvars, Var *returnVar) {
   Var tv;
   int start = 0;
   Boole error = false;
   Boole ic = false;

   Blob* b = argvars[0].blob;
   if (!b)
      return;

   if (argvars[2].tag != VAR_UNKNOWN) {
      start = varGetNumberChk(argvars + 2, OUT &error);
      if (error)
         return;
   }

   if (start < 0) {
      start = blob_len(b) + start;
      if (start < 0)
          start = 0;
   }

   for (int idx = start; idx < blob_len(b); ++idx) {
      tv.tag = VAR_NUMBER;
      tv.number = blob_get(b, idx);
      if (tv_equal(&tv, &argvars[1], ic)) {
         returnVar->number = idx;
         return;
      }
   }
}

//index() function for a list
private void
index_func_list(Var *argvars, Var *returnVar) {
   long  idx = 0;
   Boole ic = false;
   Boole error = false;

   List* l = argvars[0].list;
   if (!l)
      return;

   CHECK_LIST_MATERIALIZE(l);
   ListItem* item = l->first;
   if (argvars[2].tag != VAR_UNKNOWN) {
      // Start at specified item.  Use the cached index that list_find()
      // sets, so that a negative number also works.
      item = list_find(l, (long)varGetNumberChk(argvars + 2, OUT &error));
      idx = l->lv_u.mat.cachedInd;
      if (argvars[3].tag != VAR_UNKNOWN)
         ic = (int)varGetNumberChk(argvars + 3, OUT &error);
      if (error)
         item = NULL;
   }

   for ( ; item; item = item->next, ++idx) {
      if (tv_equal(&item->c, &argvars[1], ic)) {
         returnVar->number = idx;
         break;
      }
   } 
}

private void
f_index(Var *argvars, Var *returnVar) {
   returnVar->number = -1;

   if (argvars[0].tag == VAR_BLOB)
      index_func_blob(argvars, returnVar);
   ei (argvars[0].tag == VAR_LIST)
      index_func_list(argvars, returnVar);
   else
      emsg(_(e_list_or_blob_required));
}

//Evaluate 'expr' with the v:key and v:val arguments and return the result.
//The expression is expected to return a boolean value.  The caller should set
//the VV_KEY and VV_VAL vim variables before calling this function.
Boole
indexof_eval_expr(Var *expr) {
   Var   argv[3];
   Var   newtv;
   Boole error = false;

   argv[0] = *get_EeglVar_tv(VV_KEY);
   argv[1] = *get_EeglVar_tv(VV_VAL);
   newtv.tag = VAR_UNKNOWN;

   if (eval_expr_typval(expr, FALSE, argv, 2, &newtv) == FAIL)
      return false;

   Long found = varGetNumberChk(&newtv, OUT &error);
   clearVar(&newtv);

   return error ? false : (Boole)found;
}

//Evaluate 'expr' for each byte in the Blob 'b' starting with the byte at
//'startidx' and return the index of the byte where 'expr' is TRUE.  Return
//-1 if 'expr' doesn't evaluate to TRUE for any of the bytes.
private int
indexof_blob(Blob *b, long startidx, Var *expr) {
   if (!b)
      return -1;

   if (startidx < 0) {
      // negative index: index from the last byte
      startidx = blob_len(b) + startidx;
      if (startidx < 0)
          startidx = 0;
   }

   set_EeglVar_type(VV_KEY, VAR_NUMBER);
   set_EeglVar_type(VV_VAL, VAR_NUMBER);

   int called_emsg_start = called_emsg;
   for (long idx = startidx; idx < blob_len(b); ++idx) {
      set_EeglVar_nr(VV_KEY, idx);
      set_EeglVar_nr(VV_VAL, blob_get(b, idx));

      if (indexof_eval_expr(expr))
         return idx;

      if (called_emsg != called_emsg_start)
         return -1;
   }

   return -1;
}

//Evaluate 'expr' for each item in the List 'l' starting with the item at
//'startidx' and return the index of the item where 'expr' is TRUE.  Return
//-1 if 'expr' doesn't evaluate to TRUE for any of the items.
private int
indexof_list(List *l, long startidx, Var *expr) {
   ListItem* item;
   long   idx = 0;

   if (!l)
      return -1;

   CHECK_LIST_MATERIALIZE(l);

   if (startidx == 0)
      item = l->first;
   else {
      // Start at specified item.  Use the cached index that list_find()
      // sets, so that a negative number also works.
      item = list_find(l, startidx);
      if (item)
         idx = l->lv_u.mat.cachedInd;
   }

   set_EeglVar_type(VV_KEY, VAR_NUMBER);

   int called_emsg_start = called_emsg;
   for ( ; item; item = item->next, ++idx) {
      set_EeglVar_nr(VV_KEY, idx);
      copy_tv(OUT get_EeglVar_tv(VV_VAL), &item->c);

      Boole found = indexof_eval_expr(expr);
      clearVar(get_EeglVar_tv(VV_VAL));

      if (found)
         return idx;

      if (called_emsg != called_emsg_start)
         return -1;
   }

   return -1;
}

// "indexof()" function
private void
f_indexof(Var *argvars, Var *returnVar) {
   long startidx = 0;
   Var save_val;
   Var save_key;
   int save_anyEmsgG;

   returnVar->number = -1;

   if (check_for_list_or_blob_arg(argvars, 0) == FAIL
          || check_for_string_or_func_arg(argvars, 1) == FAIL
          || check_for_oself_arg(argvars, 2) == FAIL)
      return;

   if ((argvars[1].tag == VAR_STRING &&
         (argvars[1].string == NULL
          || *argvars[1].string == ZERO))
          || (argvars[1].tag == VAR_FUNC
         && argvars[1].partial == NULL))
      return;

   if (argvars[2].tag == VAR_BAG)
      startidx = bagGetNumber_def(argvars[2].bag, tConst("startidx"), 0);

   prepareEeglVar(VV_VAL, OUT &save_val);
   prepareEeglVar(VV_KEY, OUT &save_key);

   //We reset "anyEmsgG" to be able to detect whether an error occurred
   //during evaluation of the expression.
   save_anyEmsgG = anyEmsgG;
   anyEmsgG = FALSE;

   if (argvars[0].tag == VAR_BLOB)
      returnVar->number = indexof_blob(argvars[0].blob, startidx, &argvars[1]);
   else
      returnVar->number = indexof_list(argvars[0].list, startidx, &argvars[1]);

   restoreEeglVar(VV_KEY, &save_key);
   restoreEeglVar(VV_VAL, &save_val);
   anyEmsgG |= save_anyEmsgG;
}

private int inputsecret_flag = 0;

// "input()" function Also handles inputsecret() when inputsecret is set.
private void
f_input(Var *argvars, Var *returnVar) {
   get_user_input(argvars, returnVar, FALSE, inputsecret_flag);
}

private void
f_inputdialog(Var *argvars, Var *returnVar) {
   get_user_input(argvars, returnVar, TRUE, inputsecret_flag);
}

private void
f_inputlist(Var *argvars, Var *returnVar) {
   if (argvars[0].tag != VAR_LIST || argvars[0].list == NULL) {
      showErrFmtMsg(_(e_argument_of_str_must_be_list), "inputlist()");
      return;
   }

   msg_start();
   msgRowG = visibleRowsG - 1;  // for when @commheight > 1
   lines_left = visibleRowsG;   // avoid more prompt
   msg_scroll = TRUE;
   msg_clr_eos();

   List* l = argvars[0].list;
   CHECK_LIST_MATERIALIZE(l);
   ListItem   *li;
   FOR_ALL_LIST_ITEMS(l, li) {
      msg_puts(tv_get_string(&li->c));
      msg_putchar('\n');
   }

   // Ask for choice.
   
   int mouse_used;
   int selected = prompt_for_number(&mouse_used);
   if (mouse_used)
      selected -= lines_left;

   returnVar->number = selected;
}

private ArrayList       ga_userinput = {0, 0, sizeof(TypeaheadSave), 4, NULL};

// "inputrestore()" function
private void
f_inputrestore(Var *argvars UNUSED, Var *returnVar) {
   if (ga_userinput.len > 0) {
      --ga_userinput.len;
      restore_typeahead((TypeaheadSave *)(ga_userinput.c) + ga_userinput.len, TRUE);
      // default return is zero == OK
   } ei (p_verbose > 1) {
      verb_msg(_("called inputrestore() more often than inputsave()"));
      returnVar->number = 1; // Failed
   }
}

// "inputsave()" function
private void
f_inputsave(Var *argvars UNUSED, Var *returnVar) {
   // Add an entry to the stack of typeahead storage.
   if (ga_grow(&ga_userinput, 1) == OK) {
      save_typeahead((TypeaheadSave *)(ga_userinput.c) + ga_userinput.len);
      ++ga_userinput.len;
      // default return is zero == OK
   } else
      returnVar->number = 1; // Failed
}

// "inputsecret()" function
private void
f_inputsecret(Var *argvars, Var *returnVar) {
   ++inputsecret_flag;
   f_input(argvars, returnVar);
   --inputsecret_flag;
}

// "interrupt()" function
private void
f_interrupt(Var *argvars UNUSED, Var *returnVar UNUSED) {
    gotInterruptG = TRUE;
}

// "invert(expr)" function
private void
f_invert(Var *argvars, Var *returnVar) {
    returnVar->number = ~varGetNumberChk(argvars, NULL);
}

// Free resources in lvalRootS allocated by fill_exec_lvalRootS().
private void
freeLvalRoot(LvalRoot *root) {
   if (root->var)
      freeVar(root->var);
   root->var = NULL;
}

// "islocked()" function
private void
f_islocked(Var *argvars, Var *returnVar) {
   DictItem   *di;
   returnVar->number = -1;

   CS name = tv_get_string(&argvars[0]);
#ifdef LOG_LOCKVAR
   lo("LKVAR: f_islocked(): name: %s", name);
#endif

   LvalRoot *root = NULL;

   LvalRoot   *lval_root_save = lvalRootS;
   lvalRootS = root;
   Lval lv;
   CS end = getLval(OUT &lv, (GetLval){
         .name = mbText(name), .returnVar = null, .unlet = false, .skip = false,
         .flags = GLV_NO_AUTOLOAD | GLV_READ_ONLY | GLV_NO_DECL, .fneFlag = FNE_CHECK_START
      }
   );
              
   lvalRootS = lval_root_save;

   if (end && lv.name.len > 0) {
      if (*end != ZERO) {
         showErrFmtMsg(_(lv.name.len == 0
            ? e_invalid_argument_str : e_trailing_characters_str), end);
      } else {
         if (!lv.var) {
            di = findVar(lv.name.c, true);
            if (di) {
               // Consider a variable locked when:
               // 1. the variable itself is locked
               // 2. the value of the variable is locked.
               // 3. the List or Bag value is locked.
               returnVar->number = ((di->flags & DI_FLAGS_LOCK) || tv_islocked(&di->c));
            }
         } ei (lv.isRoot) {
            returnVar->number = tv_islocked(lv.var);
         } ei (lv.ll_range)
            emsg(_(e_range_not_allowed));
         ei (lv.newKey.len > 0)
            showErrFmtMsg(_(e_key_not_present_in_dictionary_str), lv.newKey);
         ei (lv.ll_list)
            // List item.
            returnVar->number = tv_islocked(&lv.ll_li->c);
         else
            // Dictionary item.
            returnVar->number = tv_islocked(&lv.ll_di->c);
      }
   }

   if (root)
      freeLvalRoot(root);
   clear_lval(OUT &lv);
}

private void
f_keytrans(Var *argvars, Var *returnVar) {
   returnVar->tag = VAR_STRING;
   if (check_for_string_arg(argvars, 0) == FAIL || argvars[0].string == NULL)
      return;
   // Need to escape K_SPECIAL and CSI for mb_unescape().
   CS escaped = copyStr_escape_csi(argvars[0].string);
   returnVar->string = str2special_save(escaped, TRUE, TRUE);
   eeglFree(escaped);
}

private void
f_last_buffer_nr(Var *argvars UNUSED, Var *returnVar) {
   int      n = 0;
   Book   *book;
   FOR_ALL_BOOKS(book) {
      if (n < book->fiNum)
          n = book->fiNum;
   } 

   returnVar->number = n;
}

void
f_len(Var *argvars, Var *returnVar) {
   switch (argvars[0].tag) {
   case VAR_STRING:
   case VAR_NUMBER:
      returnVar->number = (Long)STRLEN( tv_get_string(&argvars[0]));
      break;
   case VAR_BLOB:
      returnVar->number = blob_len(argvars[0].blob);
      break;
   case VAR_LIST:
      returnVar->number = list_len(argvars[0].list);
      break;
   case VAR_BAG:
      returnVar->number = bagSize(argvars[0].bag);
      break;
   case VAR_UNKNOWN:
   case VAR_ANY:
   case VAR_VOID:
   case VAR_BOOL:
   case VAR_SPECIAL:
   case VAR_FLOAT:
   case VAR_FUNC:
   case VAR_PARTIAL:
   case VAR_JOB:
   case VAR_CHANNEL:
   }
}


//"line(string, [winid])" function
private void
f_line(Var *argvars, Var *returnVar) {
   LineNr   lnum = 0;
   Pos   *fp = NULL;
   int      fnum;
   Tab   *t;
   Portal   *wp;
   SwitchPort   switchPort;


   if (argvars[1].tag != VAR_UNKNOWN) {
      // use window specified in the second argument
      int id = (int)tv_get_number(&argvars[1]);
      wp = getPortAndTab(id, OUT &t);
      if (wp && t) {
         if (portSwitchNoblock(&switchPort, wp, t, TRUE) == OK) {
            // With 'splitkeep' != cursor and in diff mode, prevent that the
            // window scrolls and keep the topline.
            if (curPor->bookOpts.diff && switchPort.curPor->bookOpts.diff)
               skipUpdateToplineG = TRUE;
            check_cursor();
            fp = var2fpos(&argvars[0], TRUE, &fnum, FALSE);
         }
         skipUpdateToplineG = FALSE;
         portRestoreNoblock(&switchPort, TRUE);
      }
   } else
      // use current portal
      fp = var2fpos(&argvars[0], TRUE, &fnum, FALSE);

   if (fp)
      lnum = fp->lnum;
    returnVar->number = lnum;
}

// "line2byte(lnum)" function
private void
f_line2byte(Var *argvars UNUSED, Var *returnVar) {
   LineNr lnum = tv_get_lnum(argvars);
   if (lnum < 1 || lnum > curBook->mem.lineCount + 1)
      returnVar->number = -1;
   else
      returnVar->number = ml_find_line_or_offset(curBook, lnum, NULL);
   if (returnVar->number >= 0)
      ++returnVar->number;
}


typedef enum {
   MATCH_END,       // matchend()
   MATCH_MATCH,    // match()
   MATCH_STR,       // matchstr()
   MATCH_LIST,       // matchlist()
   MATCH_POS       // matchstrpos()
} matchTypeSpec;

private void
find_some_match(Var *argvars, Var *returnVar, matchTypeSpec type) {
   Byte   *str = NULL;
   long   len = 0;
   Byte   *expr = NULL;
   Byte   patbuf[NUMBUFLEN];
   Byte   strbuf[NUMBUFLEN];
   long   start = 0;
   long   nth = 1;
   ColNr   startcol = 0;
   int      match = 0;
   List   *l = NULL;
   long   idx = 0;
   Byte   *tofree = NULL;

   returnVar->number = -1;
   if (type == MATCH_LIST || type == MATCH_POS) {
      // type MATCH_LIST: return empty list when there are no matches.
      // type MATCH_POS: return ["", -1, -1, -1]
      allocReturnList(returnVar);
      if (type == MATCH_POS
         && (list_append_string(returnVar->list, (CS)"", 0) == FAIL
             || list_append_number(returnVar->list, (Long)-1) == FAIL
             || list_append_number(returnVar->list, (Long)-1) == FAIL
             || list_append_number(returnVar->list, (Long)-1) == FAIL
            )
      ) {
         list_free(returnVar->list);
         returnVar->list = NULL;
         goto theend;
      }
   } ei (type == MATCH_STR) {
      returnVar->tag = VAR_STRING;
      returnVar->string = NULL;
   }


   ListItem* li = NULL;
   if (argvars[0].tag == VAR_LIST) {
      if ((l = argvars[0].list) == NULL)
         goto theend;
      CHECK_LIST_MATERIALIZE(l);
      li = l->first;
   } else {
      expr = str = tv_get_string(&argvars[0]);
      len = (long)STRLEN(str);
   }

   CS pat = convertVarToString(&argvars[1], patbuf);
   if (!pat)
      goto theend;

   if (argvars[2].tag != VAR_UNKNOWN) {
      Boole error = false;

      start = (long)varGetNumberChk(argvars + 2, OUT &error);
      if (error)
         goto theend;
      if (l) {
         li = list_find(l, start);
         if (!li)
            goto theend;
         idx = l->lv_u.mat.cachedInd;   // use the cached index
      } else {
         if (start < 0)
            start = 0;
         if (start > len)
            goto theend;
         // When "count" argument is there ignore matches before "start",
         // otherwise skip part of the string.  Differs when pattern is "^" or "\<".
         if (argvars[3].tag != VAR_UNKNOWN)
            startcol = start;
         else {
            str += start;
            len -= start;
         }
      }

      if (argvars[3].tag != VAR_UNKNOWN)
          nth = (long)varGetNumberChk(argvars + 3, OUT &error);
      if (error)
          goto theend;
   }

   RegMatch   regmatch;
   regmatch.regprog = compileRegexp(pat, RE_MAGIC + RE_STRING);
   if (regmatch.regprog) {
      regmatch.rm_ic = p_ic;

      for (;;) {
         if (l) {
            if (li == NULL) {
                match = FALSE;
                break;
            }
            eeglFree(tofree);
            expr = str = echo_string(&li->c, &tofree, strbuf, 0);
            if (str == NULL)
                break;
         }

         match = eeRegexec_nl(&regmatch, str, startcol);

         if (match && --nth <= 0)
            break;
         if (l == NULL && !match)
            break;

         // Advance to just after the match.
         if (l) {
            li = li->next;
            ++idx;
         } else {
            startcol = (ColNr)(regmatch.startp[0]
                      + utfCharLen(regmatch.startp[0]) - str);
            if (startcol > (ColNr)len || str + startcol <= regmatch.startp[0]) {
               match = FALSE;
               break;
            }
         }
      }

      if (match) {
         if (type == MATCH_POS) {
            ListItem *li1 = returnVar->list->first;
            ListItem *li2 = li1->next;
            ListItem *li3 = li2->next;
            ListItem *li4 = li3->next;

            eeglFree(li1->c.string);
            li1->c.string = copySubstr(regmatch.startp[0], regmatch.endp[0] - regmatch.startp[0]);
            li3->c.number = (Long)(regmatch.startp[0] - expr);
            li4->c.number = (Long)(regmatch.endp[0] - expr);
            if (l)
               li2->c.number = (Long)idx;
         } ei (type == MATCH_LIST) {
            int i;

            // return list with matched string and submatches
            for (i = 0; i < NSUBEXP; ++i) {
               if (regmatch.endp[i] == NULL) {
               if (list_append_string(returnVar->list, (CS)"", 0) == FAIL)
                   break;
               } ei (list_append_string(returnVar->list,
                     regmatch.startp[i],
                     (int)(regmatch.endp[i] - regmatch.startp[i]))
                      == FAIL)
                  break;
            }
         } ei (type == MATCH_STR) {
            // return matched string
            if (l)
               copy_tv(OUT returnVar, &li->c);
            else
               returnVar->string = copySubstr(regmatch.startp[0], regmatch.endp[0] - regmatch.startp[0]);
         } ei (l)
            returnVar->number = idx;
         else {
            if (type != MATCH_END)
               returnVar->number = (Long)(regmatch.startp[0] - str);
            else
               returnVar->number = (Long)(regmatch.endp[0] - str);
            returnVar->number += (Long)(str - expr);
         }
      }
      eeRegFree(regmatch.regprog);
   }

theend:
   if (type == MATCH_POS && l == NULL && returnVar->list)
      // matchstrpos() without a list: drop the second item.
      listitem_remove(returnVar->list, returnVar->list->first->next);
   eeglFree(tofree);
}

//Return all the matches in string "str" for pattern "rmp". The matches are returned in the List 
//"mlist". If "submatches" is TRUE, then submatch information is also returned. "matchbuf" is 
//TRUE when called for matchbufline().
private int
get_matches_in_str(
   CS str,
   RegMatch   *rmp,
   List   *mlist,
   int      idx,
   Boole      submatches,
   Boole      matchbuf
) {
   long   len = (long)STRLEN(str);
   int      match = 0;
   ColNr   startidx = 0;

   for (;;) {
      match = eeRegexec_nl(rmp, str, startidx);
      if (!match)
          break;

      Bag *d = allocBag();
      if (listAppendBag(mlist, d) == FAIL)
          return FAIL;

      if (bagAddNumber(d, matchbuf ? S"lnum" : S"idx", idx) == FAIL)
          return FAIL;

      if (bagAddNumber(d, S"byteidx", (ColNr)(rmp->startp[0] - str)) == FAIL)
          return FAIL;

      if (bagAddString_len(d, S"text", rmp->startp[0],
             (int)(rmp->endp[0] - rmp->startp[0])) == FAIL)
          return FAIL;

      if (submatches) {
         List *sml = list_alloc();

         if (bagAddList(d, S"submatches", sml) == FAIL)
            return FAIL;

         // return a list with the submatches
         for (int i = 1; i < NSUBEXP; ++i) {
            if (rmp->endp[i] == NULL) {
               if (list_append_string(sml, (CS)"", 0) == FAIL)
                  return FAIL;
            } ei (list_append_string(sml, rmp->startp[i], (int)(rmp->endp[i] - rmp->startp[i])) 
                  == FAIL
            )
                return FAIL;
         }
      }
      startidx = (ColNr)(rmp->endp[0] - str);
      if (startidx >= (ColNr)len || str + startidx <= rmp->startp[0])
          break;
    }
    return OK;
}

private void
f_matchbufline(Var *argvars, Var *returnVar) {
   Byte patbuf[NUMBUFLEN];
   RegMatch regmatch;

   returnVar->number = -1;
   allocReturnList(returnVar);
   List* retlist = returnVar->list;

   if (check_for_buffer_arg(argvars, 0) == FAIL
          || check_for_string_arg(argvars, 1) == FAIL
          || check_for_lnum_arg(argvars, 2) == FAIL
          || check_for_lnum_arg(argvars, 3) == FAIL
          || check_for_oself_arg(argvars, 4) == FAIL
   )
      return;

   int prev_anyEmsgG = anyEmsgG;
   Book *book = daGetBook(&argvars[0], FALSE);
   if (!book) {
      if (anyEmsgG == prev_anyEmsgG)
         showErrFmtMsg(_(e_invalid_buffer_name_str), tv_get_string(&argvars[0]));
      return;
   }
   if (!book->mem.mfile) {
      emsg(_(e_buffer_is_not_loaded));
      return;
   }

   CS pat = tv_get_string_buf(&argvars[1], patbuf);

   int      anyEmsgG_before = anyEmsgG;
   LineNr slnum = tv_get_lnum_buf(&argvars[2], book);
   if (anyEmsgG > anyEmsgG_before)
      return;
   if (slnum < 1) {
      showErrFmtMsg(_(e_invalid_value_for_argument_str), "lnum");
      return;
   }

   LineNr elnum = tv_get_lnum_buf(&argvars[3], book);
   if (anyEmsgG > anyEmsgG_before)
      return;
   if (elnum < 1 || elnum < slnum) {
      showErrFmtMsg(_(e_invalid_value_for_argument_str), "end_lnum");
      return;
   }

   if (elnum > book->mem.lineCount)
      elnum = book->mem.lineCount;

   Boole submatches = false;
   if (argvars[4].tag != VAR_UNKNOWN) {
      Bag *d = argvars[4].bag;
      if (d) {
         DictItem *di = bagFind(d, tConst("submatches"));
         if (di) {
            if (di->c.tag != VAR_BOOL) {
                showErrFmtMsg(_(e_invalid_value_for_argument_str), "submatches");
                return;
            }
            submatches = tv_get_bool(&di->c);
         }
      }
   }

   regmatch.regprog = compileRegexp(pat, RE_MAGIC + RE_STRING);
   if (regmatch.regprog == NULL)
      goto theend;
   regmatch.rm_ic = p_ic;

   while (slnum <= elnum) {
      CS str = memGetLine(book, slnum, FALSE);
      if (get_matches_in_str(str, &regmatch, retlist, slnum, submatches, TRUE) == FAIL)
         goto cleanup;
      slnum++;
   }

cleanup:
   eeRegFree(regmatch.regprog);

theend:
}

private void
f_match(Var *argvars, Var *returnVar) {
   find_some_match(argvars, returnVar, MATCH_MATCH);
}

private void
f_matchend(Var *argvars, Var *returnVar) {
    find_some_match(argvars, returnVar, MATCH_END);
}

private void
f_matchlist(Var *argvars, Var *returnVar) {
    find_some_match(argvars, returnVar, MATCH_LIST);
}

private void
f_matchstr(Var *argvars, Var *returnVar) {
    find_some_match(argvars, returnVar, MATCH_STR);
}

private void
f_matchstrlist(Var *argvars, Var *returnVar) {
   List   *retlist = NULL;
   List   *l = NULL;
   ListItem   *li = NULL;
   Byte   patbuf[NUMBUFLEN];
   RegMatch   regmatch;

   returnVar->number = -1;
   allocReturnList(returnVar);
   retlist = returnVar->list;

   if (confirmVarIsList(argvars, 0) == FAIL
       || check_for_string_arg(argvars, 1) == FAIL
       || check_for_oself_arg(argvars, 2) == FAIL)
      return;

   if ((l = argvars[0].list) == NULL)
      return;

   CS pat = convertVarToString(&argvars[1], patbuf);
   if (!pat)
      return;


   regmatch.regprog = compileRegexp(pat, RE_MAGIC + RE_STRING);
   if (regmatch.regprog == NULL)
      goto theend;
   regmatch.rm_ic = p_ic;

   Boole submatches = false;
   if (argvars[2].tag != VAR_UNKNOWN) {
      Bag *d = argvars[2].bag;
      if (d) {
         DictItem *di = bagFind(d, tConst("submatches"));
         if (di) {
            if (di->c.tag != VAR_BOOL) {
               showErrFmtMsg(_(e_invalid_value_for_argument_str), "submatches");
               goto cleanup;
            }
            submatches = tv_get_bool(&di->c);
         }
      }
   }

   int idx = 0;
   CHECK_LIST_MATERIALIZE(l);
   FOR_ALL_LIST_ITEMS(l, li) {
      if (li->c.tag == VAR_STRING && li->c.string) {
         CS str = li->c.string;
         if (get_matches_in_str(str, &regmatch, retlist, idx, submatches, false) == FAIL)
            goto cleanup;
      }
      idx++;
   }

cleanup:
   eeRegFree(regmatch.regprog);

theend:
}

private void
f_matchstrpos(Var *argvars, Var *returnVar) {
   find_some_match(argvars, returnVar, MATCH_POS);
}

private void
max_min(Var *argvars, Var *returnVar, int domax) {
   Long   n = 0;
   Long   i;
   Boole error = false;

   if (argvars[0].tag == VAR_LIST) {
      List      *l;
      ListItem   *li;

      l = argvars[0].list;
      if (l && l->len > 0) {
          if (l->first == &range_list_item) {
         if ((l->lv_u.nonmat.stride > 0) ^ domax)
             n = l->lv_u.nonmat.start;
         else
             n = l->lv_u.nonmat.start + ((Long)l->len - 1) * l->lv_u.nonmat.stride;
         } else {
            li = l->first;
            if (li) {
               n = varGetNumberChk(&li->c, OUT &error);
               if (error)
                  return; // type error; errmsg already given
               for (;;) {
                  li = li->next;
                  if (!li)
                     break;
                  i = varGetNumberChk(&li->c, OUT &error);
                  if (error)
                     return; // type error; errmsg already given
                  if (domax ? i > n : i < n)
                     n = i;
               }
            }
         }
      }
   } ei (argvars[0].tag == VAR_BAG) {
      int      first = TRUE;
      int      todo;

      Bag* d = argvars[0].bag;
      if (d) {
         todo = (int)d->hashTable.count;
         EeSetItem* hi;
         FOR_ALL_HASHTAB_ITEMS(&d->hashTable, hi, todo) {
            if (!HASHITEM_EMPTY(hi)) {
               --todo;
               i = varGetNumberChk(&HI2DI(hi)->c, OUT &error);
               if (error)
                  return; // type error; errmsg already given
               if (first) {
                  n = i;
                  first = FALSE;
               } ei (domax ? i > n : i < n)
                  n = i;
            }
         }
      }
   } else
      showErrFmtMsg(_(e_argument_of_str_must_be_list_or_dictionary), domax ? "max()" : "min()");

   returnVar->number = n;
}

// "max()" function
private void
f_max(Var *argvars, Var *returnVar) {
   max_min(argvars, returnVar, TRUE);
}

// "min()" function
private void
f_min(Var *argvars, Var *returnVar) {
   max_min(argvars, returnVar, FALSE);
}

// "nextnonblank()" function
private void
f_nextnonblank(Var *argvars, Var *returnVar) {
    LineNr   lnum;

   for (lnum = tv_get_lnum(argvars); ; ++lnum) {
   if (lnum < 0 || lnum > curBook->mem.lineCount) {
      lnum = 0;
      break;
   }
   if (*skipwhite(ml_get(lnum)) != ZERO)
      break;
   }
   returnVar->number = lnum;
}

private void
f_ngettext(Var *argvars, Var *returnVar) {
   if (check_for_nonempty_string_arg(argvars, 0) == FAIL
         || check_for_nonempty_string_arg(argvars, 1) == FAIL
         || check_for_number_arg(argvars, 2) == FAIL
         || check_for_opt_string_arg(argvars, 3) == FAIL
   )
      return;

   returnVar->tag = VAR_STRING;

   CS prev = NULL;
   if (argvars[3].tag == VAR_STRING && argvars[3].string && *(argvars[3].string) != ZERO) {
      returnVar->string = copyStr(
         (CS)dngettext((const char *)argvars[3].string, 
         (const char *)argvars[0].string, (const char *)argvars[1].string, (int)argvars[2].number)
      );

      if (prev)
         bind_textdomain_codeset((const char *)argvars[3].string, (char*)prev);
   } else {
      returnVar->string = copyStr(
         (CS)NGETTEXT((const char *)argvars[0].string, 
         (const char *)argvars[1].string, argvars[2].number)
      );
   } 
}

private void
f_nr2char(Var *argvars, Var *returnVar) {
   Byte   buf[NUMBUFLEN];

   int   utf8 = 0;

   if (argvars[1].tag != VAR_UNKNOWN)
      utf8 = (int)varGetNumberChk(argvars + 1, NULL);
   if (utf8)
      buf[mb_char2bytes((int)tv_get_number(argvars), buf)] = ZERO;
   else
      buf[(*mb_char2bytes)((int)tv_get_number(argvars), buf)] = ZERO;
   returnVar->tag = VAR_STRING;
   returnVar->string = copyStr(buf);
}

//"or(expr, expr)" function
private void
f_or(Var *argvars, Var *returnVar) {
   returnVar->number = 
      varGetNumberChk(argvars, NULL) | varGetNumberChk(argvars + 1, NULL);
}

private void
f_prevnonblank(Var *argvars, Var *returnVar) {
   LineNr   lnum;

   lnum = tv_get_lnum(argvars);
   if (lnum < 1 || lnum > curBook->mem.lineCount)
      lnum = 0;
   else {
      while (lnum >= 1 && *skipwhite(ml_get(lnum)) == ZERO)
         --lnum;
   } 
   returnVar->number = lnum;
}

// This dummy va_list is here because:
// - passing a NULL pointer doesn't work when va_list isn't a pointer
// - locally in the function results in a "used before set" warning
// - using va_start() to initialize it gives "function with fixed args" error
//private va_list   ap;

private void
f_printf(Var *argvars UNUSED, Var *returnVar UNUSED) {
//   Byte   buf[NUMBUFLEN];
//   int      saved_anyEmsgG = anyEmsgG;
//
//   returnVar->tag = VAR_STRING;
//   returnVar->string = NULL;
//
//   // Get the required length, allocate the buffer and do it for real.
//   anyEmsgG = FALSE;
//   CS fmt = tv_get_string_buf(&argvars[0], buf);
// TODO why null?   int len = eeVarPrintf(null, 0, fmt, ap, argvars + 1);
//   if (!anyEmsgG) {
//      CS s = alloc(len + 1);
//      if (s) {
//         returnVar->string = s;
//         (void)eeVarPrintf(s, len + 1, fmt, ap, argvars + 1);
//      }
//   }
//   anyEmsgG |= saved_anyEmsgG;
}

private void
f_pum_getpos(Var *argvars UNUSED, Var *returnVar UNUSED) {
   allocReturnDict(returnVar);
   pum_set_event_info(returnVar->bag);
}

private void
f_pumvisible(Var *argvars UNUSED, Var *returnVar UNUSED) {
   if (pum_visible())
   returnVar->number = 1;
}


private UINT32 srand_seed_for_testing = 0;
private int   srand_seed_for_testing_is_used = FALSE;

private void
f_test_srand_seed(Var *argvars, Var *returnVar UNUSED) {
   if (argvars[0].tag == VAR_UNKNOWN)
      srand_seed_for_testing_is_used = FALSE;
   else {
      srand_seed_for_testing = (UINT32)tv_get_number(&argvars[0]);
      srand_seed_for_testing_is_used = TRUE;
   }
}

private void
init_srand(UINT32 *x) {
   struct {
      union {
         UINT32 number;
         Byte   bytes[sizeof(UINT32)];
      } contents;
   } buf;

   if (srand_seed_for_testing_is_used) {
      *x = srand_seed_for_testing;
      return;
   }

   if (mch_get_random(OUT buf.contents.bytes, sizeof(buf.contents.bytes)) == OK) {
      *x = buf.contents.number;
      return;
   }

   // The system's random number generator doesn't work, fall back to:
   // - randombytes_random()
   // - reltime() or time()
   // - XOR with process ID
   ProfTime res;
   profile_start(&res);
   *x = (UINT32)res.tv_fsec;
   *x ^= mch_get_pid();
}

#define ROTL(x, k) (((x) << (k)) | ((x) >> (32 - (k))))
#define SPLITMIX32(x, z) ( \
    (z) = ((x) += 0x9e3779b9), \
    (z) = ((z) ^ ((z) >> 16)) * 0x85ebca6b, \
    (z) = ((z) ^ ((z) >> 13)) * 0xc2b2ae35, \
    (z) ^ ((z) >> 16) \
    )
#define SHUFFLE_XOSHIRO128STARSTAR(x, y, z, w) \
    result = ROTL((y) * 5, 7) * 9; \
    t = (y) << 9; \
    (z) ^= (x); \
    (w) ^= (y); \
    (y) ^= (z), (x) ^= (w); \
    (z) ^= t; \
    (w) = ROTL(w, 11);

private void
f_rand(Var *argvars, Var *returnVar) {
   List   *l = NULL;
   static UINT32   gx, gy, gz, gw;
   static int   initialized = FALSE;
   ListItem   *lx, *ly, *lz, *lw;
   UINT32   x = 0, y, z, w, t, result;

   if (argvars[0].tag == VAR_UNKNOWN) {
      // When no argument is given use the global seed list.
      if (initialized == FALSE) {
         // Initialize the global seed list.
         init_srand(&x);

         gx = SPLITMIX32(x, z);
         gy = SPLITMIX32(x, z);
         gz = SPLITMIX32(x, z);
         gw = SPLITMIX32(x, z);
         initialized = TRUE;
      }

      SHUFFLE_XOSHIRO128STARSTAR(gx, gy, gz, gw);
    } ei (argvars[0].tag == VAR_LIST) {
      l = argvars[0].list;
      if (l == NULL || list_len(l) != 4)
          goto theend;

      lx = list_find(l, 0L);
      ly = list_find(l, 1L);
      lz = list_find(l, 2L);
      lw = list_find(l, 3L);
      if (lx->c.tag != VAR_NUMBER) goto theend;
      if (ly->c.tag != VAR_NUMBER) goto theend;
      if (lz->c.tag != VAR_NUMBER) goto theend;
      if (lw->c.tag != VAR_NUMBER) goto theend;
      x = (UINT32)lx->c.number;
      y = (UINT32)ly->c.number;
      z = (UINT32)lz->c.number;
      w = (UINT32)lw->c.number;

      SHUFFLE_XOSHIRO128STARSTAR(x, y, z, w);

      lx->c.number = (Long)x;
      ly->c.number = (Long)y;
      lz->c.number = (Long)z;
      lw->c.number = (Long)w;
   } else
      goto theend;

   returnVar->tag = VAR_NUMBER;
   returnVar->number = (Long)result;
   return;

theend:
   showErrFmtMsg(_(e_invalid_argument_str), tv_get_string(&argvars[0]));
   returnVar->tag = VAR_NUMBER;
   returnVar->number = -1;
}

private void
f_srand(Var *argvars, Var *returnVar) {
   UINT32 x = 0, z;

   allocReturnList(returnVar);

   if (argvars[0].tag == VAR_UNKNOWN) {
      init_srand(&x);
   } else {
      Boole error = false;
      x = (UINT32)varGetNumberChk(argvars, OUT &error);
      if (error)
         return;
   }

   list_append_number(returnVar->list, (Long)SPLITMIX32(x, z));
   list_append_number(returnVar->list, (Long)SPLITMIX32(x, z));
   list_append_number(returnVar->list, (Long)SPLITMIX32(x, z));
   list_append_number(returnVar->list, (Long)SPLITMIX32(x, z));
}

#undef ROTL
#undef SPLITMIX32
#undef SHUFFLE_XOSHIRO128STARSTAR

private void
f_range(Var *argvars, Var *returnVar) {
   allocReturnList(returnVar);
   Long   end;
   Long   stride = 1;
   Boole error = false;
   Long start = varGetNumberChk(argvars, OUT &error);
   if (argvars[1].tag == VAR_UNKNOWN) {
      end = start - 1;
      start = 0;
   } else {
      end = varGetNumberChk(argvars + 1, OUT &error);
      if (argvars[2].tag != VAR_UNKNOWN)
         stride = varGetNumberChk(argvars + 2, OUT &error);
   }

   if (error)
      return;      // type error; errmsg already given
   if (stride == 0) {
      emsg(_(e_stride_is_zero));
      return;
   }
   if (stride > 0 ? end + 1 < start : end - 1 > start) {
      emsg(_(e_start_past_end));
      return;
   }

   List *list = returnVar->list;

   // Create a non-materialized list.  This is much more efficient and
   // works with ":for".  If used otherwise CHECK_LIST_MATERIALIZE() must be called.
   list->first = &range_list_item;
   list->lv_u.nonmat.start = start;
   list->lv_u.nonmat.end = end;
   list->lv_u.nonmat.stride = stride;
   if (stride > 0 ? end < start : end > start)
      list->len = 0;
   else
      list->len = (end - start) / stride + 1;
}

// Materialize "list". Do not call directly, use CHECK_LIST_MATERIALIZE()
void
range_list_materialize(List *list) {
   Long start = list->lv_u.nonmat.start;
   Long end = list->lv_u.nonmat.end;
   int      stride = list->lv_u.nonmat.stride;
   Long i;

   list->first = NULL;
   list->lv_u.mat.last = NULL;
   list->len = 0;
   list->lv_u.mat.cachedItem = NULL;
   for (i = start; stride > 0 ? i <= end : i >= end; i += stride) {
      if (list_append_number(list, i) == FAIL)
         break;
      if (list->lock & VAR_ITEMS_LOCKED)
         list->lv_u.mat.last->c.lock = VAR_LOCKED;
   }
   list->lock &= ~VAR_ITEMS_LOCKED;
}

private void
f_getreginfo(Var *argvars, Var *returnVar) {
   int      regname;
   Byte   buf[NUMBUFLEN + 2];
   long   reglen = 0;
   List   *list;

   regname = getreg_get_regname(argvars);
   if (regname == 0)
      return;

   if (regname == '@')
      regname = '"';

   allocReturnDict(returnVar);
   Bag* dict = returnVar->bag;

   list = (List *)get_reg_contents(regname, GREG_EXPR_SRC | GREG_LIST);
   if (list == NULL)
      return;
   (void)bagAddList(dict, S"regcontents", list);

   buf[0] = ZERO;
   buf[1] = ZERO;
   switch (get_reg_type(regname, &reglen)) {
   case MLINE: buf[0] = 'V'; break;
   case MCHAR: buf[0] = 'v'; break;
   case MBLOCK:
      eeSnprintf(buf, sizeof(buf), "%c%ld", Ctrl_V, reglen + 1);
      break;
   }
   (void)bagAddString(dict, S"regtype", buf);

   buf[0] = get_register_name(get_unname_register());
   buf[1] = ZERO;
   if (regname == '"')
      (void)bagAddString(dict, S"points_to", buf);
   else {
      DictItem* item = dictitem_alloc(tConst("isunnamed"));
      item->c.tag = VAR_BOOL;
      item->c.number = regname == buf[0] ? VVAL_TRUE : VVAL_FALSE;
      (void)bagAdd(dict, item);
   }
}

private void
return_register(int regname, Var *returnVar) {
   Byte buf[2] = {(Byte)regname, ZERO};
   returnVar->tag = VAR_STRING;
   returnVar->string = copyStr(buf);
}

private void
f_reg_executing(Var *argvars UNUSED, Var *returnVar) {
    return_register(reg_executing, returnVar);
}

private void
f_reg_recording(Var *argvars UNUSED, Var *returnVar) {
    return_register(reg_recording, returnVar);
}

// "rename({from}, {to})" function
private void
f_rename(Var *argvars, Var *returnVar) {
   Byte buf[NUMBUFLEN];
   returnVar->number = -1;

   returnVar->number = eeRename(tv_get_string(&argvars[0]),
                  tv_get_string_buf(&argvars[1], buf));
}

// Repeat the list "l" "n" times and set "returnVar" to the new list.
private void
repeat_list(List *l, int n, Var *returnVar) {
   if (!l || n <= 0)
      return;

   allocReturnList(returnVar);
   
   while (n-- > 0) {
      if (list_extend(returnVar->list, l, NULL) == FAIL)
         break;
   } 
}

//Repeat the blob "b" "n" times and set "returnVar" to the new blob.
private void
repeat_blob(Var *blob_tv, int n, Var *returnVar) {
   int      slen;
   int      len;
   int      i;
   Blob   *blob = blob_tv->blob;

   if (returnVar_blob_alloc(returnVar) == FAIL
         || blob == NULL
         || n <= 0
   )
      return;

   slen = blob->c.len;
   len = (int)slen * n;
   if (len <= 0)
      return;

   if (ga_grow(&returnVar->blob->c, len) == FAIL)
      return;

   returnVar->blob->c.len = len;

   for (i = 0; i < slen; ++i) {
      if (blob_get(blob, i) != 0)
          break;
   } 

   if (i == slen)
      // No need to copy since all bytes are already zero
      return;

   for (i = 0; i < n; ++i) {
      blob_set_range(returnVar->blob, (long)i * slen, ((long)i + 1) * slen - 1, blob_tv);
   } 
}

// Repeat the string "str" "n" times and set "returnVar" to the new string.
private void
repeat_string(Var *str_tv, int n, Var *returnVar) {
   Byte   *p;
   int      slen;
   int      len;
   Byte   *r;
   int      i;

   p = tv_get_string(str_tv);
   returnVar->tag = VAR_STRING;
   returnVar->string = NULL;

   slen = (int)STRLEN(p);
   len = slen * n;
   if (len <= 0)
      return;

   r = alloc(len + 1);

   for (i = 0; i < n; i++)
      mch_memmove(r + i * slen, p, (Unt)slen);
   r[len] = ZERO;

   returnVar->string = r;
}

private void
f_repeat(Var *argvars, Var *returnVar) {
   Long n = tv_get_number(&argvars[1]);
   if (argvars[0].tag == VAR_LIST)
      repeat_list(argvars[0].list, n, returnVar);
   ei (argvars[0].tag == VAR_BLOB)
      repeat_blob(&argvars[0], n, returnVar);
   else
      repeat_string(&argvars[0], n, returnVar);
}

#define SP_NOMOVE   0x01       // don't move cursor
#define SP_REPEAT   0x02       // repeat to find outer pair
#define SP_RETCOUNT   0x04       // return matchcount
#define SP_SETPCMARK   0x08       // set previous context mark
#define SP_START   0x10       // accept match at start position
#define SP_SUBPAT   0x20       // return nr of matching sub-pattern
#define SP_END      0x40       // leave cursor at end of match
#define SP_COLUMN   0x80       // start at cursor column

//Get flags for a search function. Return BACKWARD, FORWARD or zero 
//(for an error).
private int
get_search_arg(Var *varp, Unt *flagsp) {
   int dir = FORWARD;
   Byte   nbuf[NUMBUFLEN];
   int      mask;

   if (varp->tag == VAR_UNKNOWN)
      return FORWARD;

   CS flags = convertVarToString(varp, nbuf);
   if (!flags)
      return 0;      // type error; errmsg already given
   while (*flags != ZERO) {
      switch (*flags) {
          case 'b': dir = BACKWARD; break;
          case 'w': wrapSearchG = true; break;
          case 'W': wrapSearchG = false; break;
          default:  mask = 0;
              if (flagsp)
              switch (*flags) {
                  case 'c': mask = SP_START; break;
                  case 'e': mask = SP_END; break;
                  case 'm': mask = SP_RETCOUNT; break;
                  case 'n': mask = SP_NOMOVE; break;
                  case 'p': mask = SP_SUBPAT; break;
                  case 'r': mask = SP_REPEAT; break;
                  case 's': mask = SP_SETPCMARK; break;
                  case 'z': mask = SP_COLUMN; break;
              }
              if (mask == 0) {
                 showErrFmtMsg(_(e_invalid_argument_str), flags);
                 dir = 0;
              } else
                 *flagsp |= mask;
      }
      if (dir == 0)
         break;
      ++flags;
    }
    return dir;
}

// Shared by search() and searchpos() functions.
private int
search_cmn(Var *argvars, OUT Pos *match_pos, OUT Unt* flagsp) {
   Unt   patlen;
   Pos   save_cursor;
   int      retval = 0;   // default: FAIL
   long   lnum_stop = 0;
   long   time_limit = 0;
   int      options = SEARCH_KEEP;
   int      subpatnum;
   SearchitArg sia;
   Boole use_skip = false;

   CS pat = tv_get_string(&argvars[0]);
   int dir = get_search_arg(&argvars[1], OUT flagsp);   // may set wrapSearchG
   if (dir == 0)
      goto theend;
   Unt flags = *flagsp;
   if (flags & SP_START)
      options |= SEARCH_START;
   if (flags & SP_END)
      options |= SEARCH_END;
   if (flags & SP_COLUMN)
      options |= SEARCH_COL;

   // Optional arguments: line number to stop searching, timeout and skip.
   if (argvars[1].tag != VAR_UNKNOWN && argvars[2].tag != VAR_UNKNOWN) {
      lnum_stop = (long)varGetNumberChk(argvars + 2, NULL);
      if (lnum_stop < 0)
          goto theend;
      if (argvars[3].tag != VAR_UNKNOWN) {
         time_limit = (long)varGetNumberChk(argvars + 3, NULL);
         if (time_limit < 0)
            goto theend;
         use_skip = eval_expr_valid_arg(&argvars[4]);
      }
   }

   //This function does not accept SP_REPEAT and SP_RETCOUNT flags.
   //Check to make sure only those flags are set.
   //Also, Only the SP_NOMOVE or the SP_SETPCMARK flag can be set. Both
   //flags cannot be set. Check for that condition also.
   if (((flags & (SP_REPEAT | SP_RETCOUNT)) != 0)
       || ((flags & SP_NOMOVE) && (flags & SP_SETPCMARK))
   ) {
      showErrFmtMsg(_(e_invalid_argument_str), tv_get_string(&argvars[1]));
      goto theend;
   }

   Pos pos = save_cursor = curPor->cursor;
   Pos   firstpos;
   CLEAR_FIELD(firstpos);
   CLEAR_FIELD(sia);
   sia.sa_stop_lnum = (LineNr)lnum_stop;
   sia.sa_tm = time_limit;

   patlen = STRLEN(pat);

   // Repeat until {skip} return FALSE.
   for (;;) {
      subpatnum = searchit(
         curPor, curBook, &pos, NULL, dir, pat, patlen, 1L, options, RE_SEARCH, &sia
      );
      // finding the first match again means there is no match where {skip}
      // evaluates to zero.
      if (firstpos.lnum != 0 && EQUAL_POS(pos, firstpos))
          subpatnum = FAIL;

      if (subpatnum == FAIL || !use_skip)
          // didn't find it or no skip argument
          break;
      if (firstpos.lnum == 0)
          firstpos = pos;

      // If the skip expression matches, ignore this match.
      {
         Pos   save_pos = curPor->cursor;
         curPor->cursor = pos;
         Boole err = false;
         Boole do_skip = eval_expr_to_bool(&argvars[4], &err);
         curPor->cursor = save_pos;
         if (err) {
            // Evaluating {skip} caused an error, break here.
            subpatnum = FAIL;
            break;
         }
         if (!do_skip)
            break;
      }

      // clear the start flag to avoid getting stuck here
      options &= ~SEARCH_START;
   }

   if (subpatnum != FAIL) {
      if (flags & SP_SUBPAT)
         retval = subpatnum;
      else
         retval = pos.lnum;
      if (flags & SP_SETPCMARK)
         setpcmark();
      curPor->cursor = pos;
      if (match_pos) {
         // Store the match cursor position
         match_pos->lnum = pos.lnum;
         match_pos->col = pos.col + 1;
      }
      // "/$" will put the cursor after the end of the line, may need to correct that here
      check_cursor();
   }

   // If 'n' flag is used: restore cursor position.
   if (flags & SP_NOMOVE)
      curPor->cursor = save_cursor;
   else
      curPor->setCursWant = true;
theend:
   wrapSearchG = true;

   return retval;
}

private void
f_screenattr(Var *argvars, Var *returnVar) {
   int      row;
   int      col;
   char flags;

   row = (int)varGetNumberChk(argvars, NULL) - 1;
   col = (int)varGetNumberChk(argvars + 1, NULL) - 1;
   if (row < 0 || row >= screenLinesRowsG || col < 0 || col >= screenLinesColsG)
      flags = 0;
   else
      flags = screenDecosG[lineOffsetG[row] + col].flags;
   returnVar->number = flags;
}

private void
f_screenchar(Var *argvars, Var *returnVar) {
   int row = (int)varGetNumberChk(argvars, NULL) - 1;
   int col = (int)varGetNumberChk(argvars + 1, NULL) - 1;
   Unt      c;
   if (row < 0 || row >= screenLinesRowsG || col < 0 || col >= screenLinesColsG)
      c = UNT;
   else {
      Byte buf[MB_MAXBYTES + 1];
      screen_getbytes(row, col, buf, NULL);
      c = mb_ptr2char(buf);
   }
   returnVar->number = c;
}

private void
f_screenchars(Var *argvars, Var *returnVar) {
   allocReturnList(returnVar);

   int row = (int)varGetNumberChk(argvars, NULL) - 1;
   int col = (int)varGetNumberChk(argvars + 1, NULL) - 1;
   if (row < 0 || row >= screenLinesRowsG || col < 0 || col >= screenLinesColsG)
      return;

   Byte buf[MB_MAXBYTES + 1];
   screen_getbytes(row, col, buf, NULL);
   int pcc[MAX_COMBINED_SYMBOLS];
   int c = utfc_ptr2char(buf, pcc);
   list_append_number(returnVar->list, (Long)c);

   for (int i = 0; i < MAX_COMBINED_SYMBOLS && pcc[i] != 0; ++i)
      list_append_number(returnVar->list, (Long)pcc[i]);
}

//"screencol()" function. First column is 1 to be consistent with virtcol().
private void
f_screencol(Var *argvars UNUSED, Var *returnVar) {
   returnVar->number = screen_screencol() + 1;
}

private void
f_screenrow(Var *argvars UNUSED, Var *returnVar) {
   returnVar->number = screen_screenrow() + 1;
}

private void
f_screenstring(Var *argvars, Var *returnVar) {
   int      row;
   int      col;
   Byte   buf[MB_MAXBYTES + 1];

   returnVar->string = NULL;
   returnVar->tag = VAR_STRING;

   row = (int)varGetNumberChk(argvars, NULL) - 1;
   col = (int)varGetNumberChk(argvars + 1, NULL) - 1;
   if (row < 0 || row >= screenLinesRowsG || col < 0 || col >= screenLinesColsG)
      return;

   screen_getbytes(row, col, buf, NULL);
   returnVar->string = copyStr(buf);
}

private void
f_search(Var *argvars, Var *returnVar) {
   Unt      flags = 0;
   returnVar->number = search_cmn(argvars, NULL, OUT &flags);
}

private void
f_searchdecl(Var *argvars, Var *returnVar) {
   Boole locally = true;
   Boole thisblock = false;
   Boole error = false;

   returnVar->number = 1;   // default: FAIL

   CS name = convertVarToStringSingleUse(&argvars[0]);
   if (argvars[1].tag != VAR_UNKNOWN) {
      locally = !(int)varGetNumberChk(argvars + 1, OUT &error);
   if (!error && argvars[2].tag != VAR_UNKNOWN)
      thisblock = (int)varGetNumberChk(argvars + 2, OUT &error);
   }
   if (!error && name)
      returnVar->number = 
         find_decl(name, (int)STRLEN(name), locally, thisblock, SEARCH_KEEP) == FAIL;
}

// Used by searchpair() and searchpairpos()
private int
searchpair_cmn(Var *argvars, Pos *match_pos) {
   Var   *skip;
   Byte   nbuf1[NUMBUFLEN];
   Byte   nbuf2[NUMBUFLEN];
   int      retval = 0;      // default: FAIL
   long   lnum_stop = 0;
   long   time_limit = 0;

   // Get the three pattern arguments: start, middle, end. Will result in an
   // error if not a valid argument.
   CS spat = convertVarToStringSingleUse(&argvars[0]);
   CS mpat = convertVarToString(&argvars[1], nbuf1);
   CS epat = convertVarToString(&argvars[2], nbuf2);
   if (spat == NULL || mpat == NULL || epat == NULL)
      goto theend;       // type error

   // Handle the optional fourth argument: flags
   Unt flags = 0;
   int dir = get_search_arg(&argvars[3], OUT &flags); // may set wrapSearchG
   if (dir == 0)
      goto theend;

   // Don't accept SP_END or SP_SUBPAT.
   // Only one of the SP_NOMOVE or SP_SETPCMARK flags can be set.
   if ((flags & (SP_END | SP_SUBPAT)) != 0
       || ((flags & SP_NOMOVE) && (flags & SP_SETPCMARK))
   ) {
      showErrFmtMsg(_(e_invalid_argument_str), tv_get_string(&argvars[3]));
      goto theend;
   }

   // Using 'r' implies 'W', otherwise it doesn't work.
   if (flags & SP_REPEAT)
      wrapSearchG = false;

   // Optional fifth argument: skip expression
   if (argvars[3].tag == VAR_UNKNOWN || argvars[4].tag == VAR_UNKNOWN)
      skip = NULL;
   else {
      // Type is checked later.
      skip = &argvars[4];

      if (argvars[5].tag != VAR_UNKNOWN) {
         lnum_stop = (long)varGetNumberChk(argvars + 5, NULL);
         if (lnum_stop < 0) {
            showErrFmtMsg(_(e_invalid_argument_str), tv_get_string(&argvars[5]));
            goto theend;
         }
         if (argvars[6].tag != VAR_UNKNOWN) {
            time_limit = (long)varGetNumberChk(argvars + 6, NULL);
            if (time_limit < 0) {
               showErrFmtMsg(_(e_invalid_argument_str), tv_get_string(&argvars[6]));
               goto theend;
            }
         }
      }
   }

   retval = do_searchpair(spat, mpat, epat, dir, skip, flags, match_pos, lnum_stop, time_limit);

theend:
   wrapSearchG = true;

   return retval;
}

private void
f_searchpair(Var *argvars, Var *returnVar) {
    returnVar->number = searchpair_cmn(argvars, NULL);
}

private void
f_searchpairpos(Var *argvars, Var *returnVar) {
   int      lnum = 0;
   int      col = 0;

   allocReturnList(returnVar);

   Pos   match_pos;
   if (searchpair_cmn(argvars, &match_pos) > 0) {
      lnum = match_pos.lnum;
      col = match_pos.col;
   }

   list_append_number(returnVar->list, (Long)lnum);
   list_append_number(returnVar->list, (Long)col);
}

//Search for a start/middle/end thing. Used by searchpair(), see its documentation for the details.
//Return 0 or -1 for no match,
long
do_searchpair(
   Byte   *spat,       // start pattern
   Byte   *mpat,       // middle pattern
   Byte   *epat,       // end pattern
   int      dir,       // BACKWARD or FORWARD
   Var   *skip,       // skip expression
   int      flags,       // SP_SETPCMARK and other SP_ values
   Pos   *match_pos,
   LineNr   lnum_stop,  // stop at this line if not zero
   long   time_limit UNUSED) // stop after this many msec
{
   long   retval = 0;
   Pos   save_pos;
   int n;
   Boole r;
   int nest = 1;
   Boole use_skip = false;
   Boole err;
   Unt options = SEARCH_KEEP;

   // Make two search patterns: start/end (pat2, for in nested pairs) and
   // start/middle/end (pat3, for the top pair).
   Unt spatlen = STRLEN(spat);
   Unt epatlen = STRLEN(epat);
   Unt pat2size = spatlen + epatlen + 17;
   CS pat2 = alloc(pat2size);
   Unt pat3size = spatlen + STRLEN(mpat) + epatlen + 25;
   CS pat3 = alloc(pat3size);
   Unt pat2len = eeSnprintf(pat2, pat2size, "\\m\\(%s\\m\\)\\|\\(%s\\m\\)", spat, epat);
   Unt pat3len;
   if (*mpat == ZERO) {
      STRCPY(pat3, pat2);
      pat3len = pat2len;
   } else
      pat3len = eeSnprintf(pat3, pat3size, 
            "\\m\\(%s\\m\\)\\|\\(%s\\m\\)\\|\\(%s\\m\\)", spat, epat, mpat
      );
   if (flags & SP_START)
      options |= SEARCH_START;

   if (skip)
      use_skip = eval_expr_valid_arg(skip);

   if (time_limit > 0)
      init_regexp_timeout(time_limit);
   Pos save_cursor = curPor->cursor;
   Pos pos = curPor->cursor;
   
   Pos firstpos;
   CLEAR_POS(OUT &firstpos);
   Pos foundpos;
   CLEAR_POS(OUT &foundpos);
   CS pat = pat3;
   Unt patlen = pat3len;
   for (;;) {
      SearchitArg sia;

      CLEAR_FIELD(sia);
      sia.sa_stop_lnum = lnum_stop;
      n = searchit(curPor, curBook, &pos, NULL, dir, pat, patlen, 1L,
                          options, RE_SEARCH, &sia);
      if (n == FAIL || (firstpos.lnum != 0 && EQUAL_POS(pos, firstpos)))
         // didn't find it or found the first match again: FAIL
         break;

      if (firstpos.lnum == 0)
          firstpos = pos;
      if (EQUAL_POS(pos, foundpos)) {
          // Found the same position again.  Can happen with a pattern that
          // has "\zs" at the end and searching backwards.  Advance one character and try again.
          if (dir == BACKWARD)
         decl(&pos);
          else
         incl(&pos);
      }
      foundpos = pos;

      // clear the start flag to avoid getting stuck here
      options &= ~SEARCH_START;

      // If the skip pattern matches, ignore this match.
      if (use_skip) {
         save_pos = curPor->cursor;
         curPor->cursor = pos;
         err = false;
         r = eval_expr_to_bool(skip, &err);
         curPor->cursor = save_pos;
         if (err) {
            // Evaluating {skip} caused an error, break here.
            curPor->cursor = save_cursor;
            retval = -1;
            break;
         }
         if (r)
            continue;
      }

      if ((dir == BACKWARD && n == 3) || (dir == FORWARD && n == 2)) {
         // Found end when searching backwards or start when searching forward: nested pair.
         ++nest;
         pat = pat2;      // nested, don't search for middle
      } else {
         // Found end when searching forward or start when searching
         // backward: end of (nested) pair; or found middle in outer pair.
         if (--nest == 1)
            pat = pat3;   // outer level, search for middle
      }

      if (nest == 0) {
         // Found the match: return matchcount or line number.
         if (flags & SP_RETCOUNT)
            ++retval;
         else
            retval = pos.lnum;
         if (flags & SP_SETPCMARK)
            setpcmark();
         curPor->cursor = pos;
         if (!(flags & SP_REPEAT))
            break;
         nest = 1;       // search for next unmatched
      }
   }

   if (match_pos) {
      // Store the match cursor position
      match_pos->lnum = curPor->cursor.lnum;
      match_pos->col = curPor->cursor.col + 1;
   }

   // If 'n' flag is used or search failed: restore cursor position.
   if ((flags & SP_NOMOVE) || retval == 0)
      curPor->cursor = save_cursor;

   if (time_limit > 0)
      disable_regexp_timeout();
   eeglFree(pat2);
   eeglFree(pat3);

   return retval;
}

private void
f_searchpos(Var *argvars, Var *returnVar) {
   allocReturnList(returnVar);

   Unt flags = 0;
   Pos   match_pos;
   int n = search_cmn(argvars, OUT &match_pos, OUT &flags);
   int lnum = 0;
   int col = 0;
   if (n > 0) {
      lnum = match_pos.lnum;
      col = match_pos.col;
   }

   list_append_number(returnVar->list, (Long)lnum);
   list_append_number(returnVar->list, (Long)col);
   if (flags & SP_SUBPAT)
      list_append_number(returnVar->list, (Long)n);
}

//Set the cursor or mark position. If "charpos" is TRUE, then use the column number as a character 
//offset. Otherwise use the column number as a byte offset.
private void
set_position(Var *argvars, Var *returnVar, int charpos) {
   Pos   pos;
   int      fnum;
   Byte   *name;
   ColNr   curswant = -1;

   returnVar->number = -1;

   name = convertVarToStringSingleUse(argvars);
   if (!name)
      return;

   if (list2fpos(&argvars[1], &pos, &fnum, &curswant, charpos) != OK)
      return;

   if (pos.col != MAXCOL && --pos.col < 0)
   pos.col = 0;
   if ((name[0] == '.' && name[1] == ZERO)) {
      // set cursor; "fnum" is ignored
      curPor->cursor = pos;
      if (curswant >= 0) {
         curPor->cursWant = curswant - 1;
         curPor->setCursWant = false;
      }
      check_cursor();
      returnVar->number = 0;
   } ei (name[0] == '\'' && name[1] != ZERO && name[2] == ZERO) {
      // set mark
      if (setmark_pos(name[1], &pos, fnum) == OK)
         returnVar->number = 0;
   } else
      emsg(_(e_invalid_argument));
}

private void
f_setcharpos(Var *argvars, Var *returnVar) {
   set_position(argvars, returnVar, TRUE);
}

private void
f_setcharsearch(Var *argvars, Var *returnVar UNUSED) {
   Bag   *d;
   DictItem   *di;
   Byte   *csearch;

   if (check_for_dict_arg(argvars, 0) == FAIL)
      return;

   if ((d = argvars[0].bag) == NULL)
      return;

   csearch = bagGetString(d, tConst("char"), FALSE);
   if (csearch) {
       int pcc[MAX_COMBINED_SYMBOLS];
       int c = utfc_ptr2char(csearch, pcc);

       set_last_csearch(c, csearch, utfCharLen(csearch));
   }

   di = bagFind(d, tConst("forward"));
   if (di) {
      set_csearch_direction((int)tv_get_number(&di->c)
         ? FORWARD : BACKWARD);
   } 

   di = bagFind(d, tConst("until"));
   if (di)
      set_csearch_until(!!tv_get_number(&di->c));
}

// "setcursorcharpos" function
private void
f_setcursorcharpos(Var *argvars, Var *returnVar) {
   set_cursorpos(argvars, returnVar, TRUE);
}

//"setenv()" function
private void
f_setenv(Var *argvars, Var *returnVar UNUSED) {
   Byte   namebuf[NUMBUFLEN];
   Byte   valbuf[NUMBUFLEN];

   CS name = tv_get_string_buf(&argvars[0], namebuf);
   if (argvars[1].tag == VAR_SPECIAL && argvars[1].number == VVAL_NULL)
      eeUnsetenv(name);
   else
      eeSetenv_ext(name, tv_get_string_buf(&argvars[1], valbuf));
}

// "setfperm({fname}, {mode})" function
private void
f_setfperm(Var *argvars, Var *returnVar) {
   Byte   *fname;
   Byte   modeBuf[NUMBUFLEN];
   Byte   *mode_str;
   int      i;
   int      mask;
   int      mode = 0;

   returnVar->number = 0;

   fname = convertVarToStringSingleUse(&argvars[0]);
   if (fname == NULL)
      return;
   mode_str = convertVarToString(&argvars[1], modeBuf);
   if (mode_str == NULL)
      return;
   if (STRLEN(mode_str) != 9) {
      showErrFmtMsg(_(e_invalid_argument_str), mode_str);
      return;
   }

   mask = 1;
   for (i = 8; i >= 0; --i) {
      if (mode_str[i] != '-')
         mode |= mask;
      mask = mask << 1;
   }
   returnVar->number = mch_setperm(fname, mode) == OK;
}

// "setpos()" function
private void
f_setpos(Var *argvars, Var *returnVar) {
    set_position(argvars, returnVar, FALSE);
}

// Translate a register type string to the yank type and block length
private int
get_yank_type(Byte **pp, CS yank_type, long *block_len) {
   CS stropt = *pp;
   switch (*stropt) {
   case 'v': case 'c':   // character-wise selection
       *yank_type = MCHAR;
       break;
   case 'V': case 'l':   // line-wise selection
       *yank_type = MLINE;
       break;
   case 'b': case Ctrl_V:   // block-wise selection
      *yank_type = MBLOCK;
      if (EE_ISDIGIT(stropt[1])) {
         ++stropt;
         *block_len = getdigits(&stropt) - 1;
         --stropt;
      }
      break;
   default:
       return FAIL;
   }
   *pp = stropt;
   return OK;
}

private void
f_setreg(Var *argvars, Var *returnVar) {
   int      regname;
   Byte   *strregname;
   Byte   *stropt;
   Byte   *strval;
   int      append;
   Byte   yank_type;
   long   block_len;
   Var   *regcontents;
   int      pointreg;


   pointreg = 0;
   regcontents = NULL;
   block_len = -1;
   yank_type = MAUTO;
   append = FALSE;

   strregname = convertVarToStringSingleUse(argvars);
   returnVar->number = 1;      // FAIL is default

   if (strregname == NULL)
      return;      // type error; errmsg already given
   regname = *strregname;
   if (regname == 0 || regname == '@')
      regname = '"';

   if (argvars[1].tag == VAR_BAG) {
      Bag       *d = argvars[1].bag;
      if (!d || d->hashTable.count == 0) {
          // Empty dict, clear the register (like setreg(0, []))
          CS lstval[2] = {NULL, NULL};
          write_reg_contents_lst(regname, lstval, 0, FALSE, MAUTO, -1);
          return;
      }

      DictItem* di = bagFind(d, tConst("regcontents"));
      if (di)
          regcontents = &di->c;

      stropt = bagGetString(d, tConst("regtype"), FALSE);
      if (stropt) {
          int ret = get_yank_type(&stropt, &yank_type, &block_len);

          if (ret == FAIL || *++stropt != ZERO) {
         showErrFmtMsg(_(e_invalid_value_for_argument_str), "value");
         return;
          }
      }

      if (regname == '"') {
         stropt = bagGetString(d, tConst("points_to"), FALSE);
         if (stropt) {
            pointreg = *stropt;
            regname = pointreg;
         }
      } ei (bagGetBool(d, tConst("isunnamed"), false))
          pointreg = regname;
   } else
      regcontents = &argvars[1];

   if (argvars[2].tag != VAR_UNKNOWN) {
      if (yank_type != MAUTO) {
          showErrFmtMsg(_(e_too_many_arguments_for_function_str), "setreg");
          return;
      }

      stropt = convertVarToStringSingleUse(&argvars[2]);
      if (stropt == NULL)
          return;      // type error
      for (; *stropt != ZERO; ++stropt)
         switch (*stropt) {
         case 'a': case 'A':   // append
             append = TRUE;
             break;
         default:
             get_yank_type(&stropt, &yank_type, &block_len);
         }
    }

   if (regcontents && regcontents->tag == VAR_LIST) {
      Byte      buf[NUMBUFLEN];
      List      *ll = regcontents->list;
      ListItem   *li;

      // If the list is NULL handle like an empty list.
      int len = ll == NULL ? 0 : ll->len;

      // First half: use for pointers to result lines; second: use for pointers to allocated copies
      Byte** lstval = ALLOC_MULT(CS, (len + 1) * 2);
      Byte** curval = lstval;
      Byte** allocval = lstval + len + 2;
      Byte** curallocval = allocval;

      if (ll) {
         CHECK_LIST_MATERIALIZE(ll);
         FOR_ALL_LIST_ITEMS(ll, li) {
            strval = convertVarToString(&li->c, buf);
            if (!strval)
               goto free_lstval;
            if (strval == buf) {
               // Need to make a copy, next convertVarToString() will overwrite the string
               strval = copyStr(buf);
               *curallocval++ = strval;
            }
            *curval++ = strval;
         }
      }
      *curval++ = NULL;
      write_reg_contents_lst(regname, lstval, -1, append, yank_type, block_len);
   free_lstval:
      while (curallocval > allocval)
          eeglFree(*--curallocval);
      eeglFree(lstval);
    } ei (regcontents) {
      strval = convertVarToStringSingleUse(regcontents);
      if (strval == NULL)
         return;
      write_reg_contents_ex(regname, strval, -1, append, yank_type, block_len);
    }
   if (pointreg != 0)
   get_yank_register(pointreg, TRUE);

    returnVar->number = 0;
}

private void
f_settagstack(Var *argvars, Var *returnVar) {
   Portal   *wp;
   int      action = 'r';

   returnVar->number = -1;

   // first argument: window number or id
   wp = portFindByNrOrId(&argvars[0]);
   if (wp == NULL)
      return;

   // second argument: dict with items to set in the tag stack
   if (check_for_dict_arg(argvars, 1) == FAIL)
      return;
   Bag* d = argvars[1].bag;
   if (!d)
      return;

    // third argument: action - 'a' for append and 'r' for replace.
    // default is to replace the stack.
   if (argvars[2].tag == VAR_UNKNOWN)
      action = 'r';
   ei (check_for_string_arg(argvars, 2) == FAIL)
      return;
   else {
      Byte   *actstr;
      actstr = convertVarToStringSingleUse(&argvars[2]);
      if (actstr == NULL)
         return;
      if ((*actstr == 'r' || *actstr == 'a' || *actstr == 't') && actstr[1] == ZERO)
         action = *actstr;
      else {
         showErrFmtMsg(_(e_invalid_action_str_2), actstr);
         return;
      }
   }

   if (set_tagstack(wp, d, action) == OK)
   returnVar->number = 0;
}

// "sha256({string})" function
private void
f_sha256(Var *argvars, Var *returnVar) {
   Byte   *p;

   p = tv_get_string(&argvars[0]);
   returnVar->string = copyStr(sha256_bytes(p, (int)STRLEN(p), NULL, 0));
    returnVar->tag = VAR_STRING;
}

// "shellescape({string})" function
private void
f_shellescape(Var *argvars, Var *returnVar) {
   int do_special;

   do_special = non_zero_arg(&argvars[1]);
   returnVar->string = copyStr_shellescape(tv_get_string(&argvars[0]), do_special, do_special);
   returnVar->tag = VAR_STRING;
}

// shiftwidth() function
private void
f_shiftwidth(Var *argvars UNUSED, Var *returnVar){
   returnVar->number = 0;


   if (argvars[0].tag != VAR_UNKNOWN) {
      long   col;

      col = (long)varGetNumberChk(argvars, NULL);
      if (col < 0)
         return;   // type error; errmsg already given
   }

    returnVar->number = get_sw_value(curBook);
}

// "spellbadword()" function
private void
f_spellbadword(Var *argvars UNUSED, Var *returnVar) {
   Byte   *word = (CS)"";
   Unt   deco = 0;
   int      len = 0;
   int      spell_save = curPor->bookOpts.spell;

   if (!curPor->bookOpts.spell) {
      parse_spelllang(curPor);
      curPor->bookOpts.spell = TRUE;
   }

   if (*curPor->ownSyntax->spellLang == ZERO) {
      emsg(_(e_spell_checking_is_not_possible));
      curPor->bookOpts.spell = spell_save;
      return;
   }

   allocReturnList(returnVar);

   if (argvars[0].tag == VAR_UNKNOWN) {
      // Find the start and length of the badly spelled word.
      len = spell_move_to(curPor, FORWARD, SMT_ALL, TRUE, &deco);
      if (len != 0) {
         word = ml_get_cursor();
         curPor->setCursWant = true;
      }
   } ei (*curBook->syntax.spellLang != ZERO) {
      Byte   *str = convertVarToStringSingleUse(&argvars[0]);
      int   capcol = -1;

      if (str) {
         // Check the argument for spelling.
         while (*str != ZERO) {
            len = spell_check(curPor, str, &deco, &capcol, FALSE);
            if (deco != 0) {
               word = str;
               break;
            }
            str += len;
            capcol -= len;
            len = 0;
         }
      }
   }
   curPor->bookOpts.spell = spell_save;

   list_append_string(returnVar->list, word, len);
   list_append_string(returnVar->list, (CS)(
         deco == HLF_SPB ? "bad" :
         deco == HLF_SPR ? "rare" :
         deco == HLF_SPL ? "local" :
         deco == HLF_SPC ? "caps" :
         ""), -1);
}

// "spellsuggest()" function
private void
f_spellsuggest(Var *argvars UNUSED, Var *returnVar) {
   Byte   *str;
   Boole typeerr = false;
   int      maxcount;
   ArrayList   ga;
   int      i;
   ListItem   *li;
   int spell_save = curPor->bookOpts.spell;

   if (!curPor->bookOpts.spell) {
      parse_spelllang(curPor);
      curPor->bookOpts.spell = TRUE;
   }

   if (*curPor->ownSyntax->spellLang == ZERO) {
      emsg(_(e_spell_checking_is_not_possible));
      curPor->bookOpts.spell = spell_save;
      return;
   }

   allocReturnList(returnVar);

   str = tv_get_string(&argvars[0]);
   if (argvars[1].tag != VAR_UNKNOWN) {
      maxcount = (int)varGetNumberChk(argvars + 1, OUT &typeerr);
      if (maxcount <= 0)
         return;
      if (argvars[2].tag != VAR_UNKNOWN) {
         if (typeerr)
            return;
      }
   } else
      maxcount = 25;

   spell_suggest_list(&ga, str, maxcount, FALSE);

   for (i = 0; i < ga.len; ++i) {
      str = ((Byte **)ga.c)[i];

      li = listitem_alloc();
      li->c.tag = VAR_STRING;
      li->c.lock = 0;
      li->c.string = str;
      list_append(returnVar->list, li);
   }
   ga_clear(&ga);
   curPor->bookOpts.spell = spell_save;
}

private void
f_split(Var *argvars, Var *returnVar) {
   Byte   *str;
   Byte   *end;
   Byte   *pat = NULL;
   RegMatch   regmatch;
   Byte   patbuf[NUMBUFLEN];
   int      match;
   ColNr   col = 0;
   int      keepempty = FALSE;
   Boole typeerr = false;


   str = tv_get_string(&argvars[0]);
   if (argvars[1].tag != VAR_UNKNOWN) {
      pat = convertVarToString(&argvars[1], patbuf);
      if (!pat)
         typeerr = true;
      if (argvars[2].tag != VAR_UNKNOWN)
         keepempty = (int)varGetNumberChk(argvars + 2, OUT &typeerr);
   }
   if (typeerr)
      goto theend;
      
   if (!pat || *pat == ZERO)
      pat = S"[\\x01- ]\\+";

   allocReturnList(returnVar);

   regmatch.regprog = compileRegexp(pat, RE_MAGIC + RE_STRING);
   if (regmatch.regprog) {
      regmatch.rm_ic = FALSE;
      while (*str != ZERO || keepempty) {
          if (*str == ZERO)
         match = FALSE;   // empty item at the end
          else
         match = eeRegexec_nl(&regmatch, str, col);
          if (match)
         end = regmatch.startp[0];
          else
         end = str + STRLEN(str);
          if (keepempty || end > str || (returnVar->list->len > 0
               && *str != ZERO && match && end < regmatch.endp[0])
         ) {
         if (list_append_string(returnVar->list, str,
                         (int)(end - str)) == FAIL)
             break;
         }
         if (!match)
            break;
         // Advance to just after the match.
         if (regmatch.endp[0] > str)
            col = 0;
         else
            // Don't get stuck at the same match.
            col = utfCharLen(regmatch.endp[0]);
         str = regmatch.endp[0];
      }

      eeRegFree(regmatch.regprog);
   }

theend:
}

// "submatch()" function
private void
f_submatch(Var *argvars, Var *returnVar) {
   Boole error = false;

   int no = (int)varGetNumberChk(argvars, OUT &error);
   if (error)
      return;
   if (no < 0 || no >= NSUBEXP) {
      showErrFmtMsg(_(e_invalid_submatch_number_nr), no);
      return;
   }
   int retList = 0;
   if (argvars[1].tag != VAR_UNKNOWN)
      retList = (int)varGetNumberChk(argvars + 1, OUT &error);
   if (error)
      return;

   if (retList == 0) {
      returnVar->tag = VAR_STRING;
      returnVar->string = reg_submatch(no);
   } else {
      returnVar->tag = VAR_LIST;
      returnVar->list = reg_submatch_list(no);
   }
}

private void
f_substitute(Var *argvars, Var *returnVar) {
   Byte patbuf[NUMBUFLEN];
   Byte subbuf[NUMBUFLEN];
   Byte flagsbuf[NUMBUFLEN];
   Byte* sub = NULL;
   Var* expr = NULL;

   CS str = convertVarToStringSingleUse(&argvars[0]);
   CS pat = convertVarToString(&argvars[1], patbuf);
   CS flg = convertVarToString(&argvars[3], flagsbuf);

   if (argvars[2].tag == VAR_FUNC || argvars[2].tag == VAR_PARTIAL) 
      expr = &argvars[2];
   else
      sub = convertVarToString(&argvars[2], subbuf);

   returnVar->tag = VAR_STRING;
   if (!str || !pat || (!sub && !expr) || !flg)
      returnVar->string = NULL;
   else
      returnVar->string = do_string_sub(str, STRLEN(str), pat, sub, expr, flg, NULL);
}

// "swapfilelist()" function
private void
f_swapfilelist(Var *argvars UNUSED, Var *returnVar) {
   allocReturnList(returnVar);
   recover_names(NULL, FALSE, returnVar->list, 0, NULL);
}

// "swapinfo(swap_filename)" function
private void
f_swapinfo(Var *argvars, Var *returnVar) {
   allocReturnDict(returnVar);
   get_b0_dict(tv_get_string(argvars), returnVar->bag);
}

// "swapname(expr)" function
private void
f_swapname(Var *argvars, Var *returnVar) {
   returnVar->tag = VAR_STRING;

   Book* book = daGetBook(&argvars[0], FALSE);
   if (book == NULL || book->mem.mfile == NULL || book->mem.mfile->fName == NULL)
      returnVar->string = NULL;
   else
      returnVar->string = copyStr(book->mem.mfile->fName);
}

// "synID(lnum, col, trans)" function
private void
f_synID(Var *argvars UNUSED, Var *returnVar) {
   int      id = 0;
   Boole transerr = false;

   LineNr lnum = tv_get_lnum(argvars);      // -1 on type error
   ColNr col = (LineNr)tv_get_number(&argvars[1]) - 1;   // -1 on type error
   int trans = (int)varGetNumberChk(argvars + 2, OUT &transerr);

   if (!transerr && lnum >= 1 && lnum <= curBook->mem.lineCount
          && col >= 0 && col < (long)ml_get_len(lnum)
   )
      id = syn_get_id(curPor, lnum, col, trans, NULL, false);

   returnVar->number = id;
}

// "synIDattr(id, what [, mode])" function
private void
f_synIDattr(Var *argvars, Var *returnVar) {
   int id = (int)tv_get_number(&argvars[0]);
   Short hiId = (id < SHORT && id >= 0) ? (Short)id : SHORT;
   CS what = tv_get_string(&argvars[1]);

   CS p = NULL;
   switch (what[0]) {
   case 'b':
      if (what[1] == 'g')   // bg[#]
         p = hiliteColor(hiId, what[2] == '#' ? BG_RGB : BG_COLOR).c;
      else               // bold
         p = hiliteHasFlag(hiId, HL_BOLD);
      break;

   case 'f':               // fg[#]
      if (what[1] == 'g')
         p = hiliteColor(hiId, what[2] == '#' ? FG_RGB : FG_COLOR).c;
      break;

   case 'i':
      if (TOLOWER_ASC(what[1]) == 'n')
         p = hiliteHasFlag(hiId, HL_INVERSE);
      else           
         p = hiliteHasFlag(hiId, HL_ITALIC);
      break;

   case 'n':
      if (TOLOWER_ASC(what[1]) == 'o')
         p = hiliteHasFlag(hiId, HL_NOCOMBINE);
      else           
         p = getHiliteGroupName(NULL, id).c;
      break;
   case 'u':
      if (STRLEN(what) >= 9) {
         if (TOLOWER_ASC(what[5]) == 'l') // underline
            p = hiliteHasFlag(id, HL_UNDERLINE);
         ei (TOLOWER_ASC(what[5]) != 'd') // undercurl
            p = hiliteHasFlag(id, HL_UNDERCURL);
      } ei (what[1] == 'n') // under
         p = hiliteColor(hiId, what[2] == '#' ? UNDER_RGB : UNDER_COLOR).c;
      break;
   }

   if (p)
      p = copyStr(p);
   returnVar->tag = VAR_STRING;
   returnVar->string = p;
}

// "synIDtrans(id)" function
private void
f_synIDtrans(Var *argvars UNUSED, Var *returnVar) {
   int id = (int)tv_get_number(&argvars[0]);

   if (id > 0)
      id = hiResolveLinks(id);
   else
      id = 0;

   returnVar->number = id;
}

// "synconcealed(lnum, col)" function
private void
f_synconcealed(Var *argvars UNUSED, Var *returnVar) {
   returnVar_list_set(returnVar, NULL);
}

// "synstack(lnum, col)" function
private void
f_synstack(Var *argvars UNUSED, Var *returnVar) {
   LineNr lnum;
   ColNr col;
   int i;
   int id;

   returnVar_list_set(returnVar, NULL);

   lnum = tv_get_lnum(argvars);      // -1 on type error
   col = (ColNr)tv_get_number(&argvars[1]) - 1;   // -1 on type error

   if (lnum >= 1 && lnum <= curBook->mem.lineCount
       && col >= 0 && col <= (long)ml_get_len(lnum)
   ) {
      allocReturnList(returnVar);
      (void)syn_get_id(curPor, lnum, col, false, NULL, true);
      for (i = 0; ; ++i) {
         id = syn_get_stack_item(i);
         if (id < 0)
            break;
         if (list_append_number(returnVar->list, id) == FAIL)
            break;
      }
   }
}

private void
f_tabpagebuflist(Var *argvars UNUSED, Var *returnVar UNUSED) {
   Portal* wp = NULL;

   if (argvars[0].tag == VAR_UNKNOWN)
      wp = firstPor;
   else {
      Tab* t = getTab((int)tv_get_number(&argvars[0]));
      if (t)
         wp = (t == curtab) ? firstPor : t->firstPor;
   }
   if (wp) {
      allocReturnList(returnVar);
      for (; wp; wp = wp->next)
         if (list_append_number(returnVar->list, wp->book->fiNum) == FAIL)
            break;
   }
}

private void
f_tagfiles(Var *argvars UNUSED, Var *returnVar) {
   TagName   tn;

   allocReturnList(returnVar);
   Byte fname[MAXPATHL];

   for (int first = TRUE; ; first = FALSE) {
      if (get_tagfname(&tn, first, fname) == FAIL
            || list_append_string(returnVar->list, fname, -1) == FAIL)
         break;
   } 
   eeglFree(fname);
}

private void
f_taglist(Var *argvars, Var *returnVar) {
   Byte  *fname = NULL;

   CS tag_pattern = tv_get_string(&argvars[0]);

   returnVar->number = FALSE;
   if (*tag_pattern == ZERO)
      return;

   if (argvars[1].tag != VAR_UNKNOWN)
      fname = tv_get_string(&argvars[1]);
   allocReturnList(returnVar);
   (void)get_tags(returnVar->list, tag_pattern, fname);
}

// "type(expr)" function
private void
f_type(Var *argvars, Var *returnVar) {
   int n = -1;

   switch (argvars[0].tag) {
   case VAR_NUMBER:  n = VAR_TYPE_NUMBER; break;
   case VAR_STRING:  n = VAR_TYPE_STRING; break;
   case VAR_PARTIAL:
   case VAR_FUNC:    n = VAR_TYPE_FUNC; break;
   case VAR_LIST:    n = VAR_TYPE_LIST; break;
   case VAR_BAG:    n = VAR_TYPE_DICT; break;
   case VAR_FLOAT:   n = VAR_TYPE_FLOAT; break;
   case VAR_BOOL:     n = VAR_TYPE_BOOL; break;
   case VAR_SPECIAL: n = VAR_TYPE_NONE; break;
   case VAR_JOB:     n = VAR_TYPE_JOB; break;
   case VAR_CHANNEL: n = VAR_TYPE_CHANNEL; break;
   case VAR_BLOB:    n = VAR_TYPE_BLOB; break;
   case VAR_UNKNOWN:
   case VAR_ANY:
   case VAR_VOID:
      internal_error_no_abort(S"f_type(UNKNOWN)");
      n = -1;
      break;
   }
   returnVar->number = n;
}

// "virtcol({expr}, [, {list} [, {winid}]])" function
private void
f_virtcol(Var *argvars, Var *returnVar) {
   ColNr   vcol_start = 0;
   ColNr   vcol_end = 0;
   Pos   *fp;
   SwitchPort   switchPort;
   int      winchanged = FALSE;
   int      len;

   if (argvars[1].tag != VAR_UNKNOWN && argvars[2].tag != VAR_UNKNOWN) {
      Tab   *t;
      Portal      *wp;

      // use the window specified in the third argument
      wp = getPortAndTab((int)tv_get_number(&argvars[2]), OUT &t);
      if (!wp || !t)
          goto theend;

      if (portSwitchNoblock(&switchPort, wp, t, TRUE) != OK)
          goto theend;

      check_cursor();
      winchanged = TRUE;
    }

   int fnum = curBook->fiNum;
   fp = var2fpos(&argvars[0], FALSE, &fnum, FALSE);
   if (fp && fp->lnum <= curBook->mem.lineCount && fnum == curBook->fiNum) {
      // Limit the column to a valid value, bookGetVirtualColInVirtualMode() doesn't check.
      if (fp->col < 0)
          fp->col = 0;
      else {
         len = (int)ml_get_len(fp->lnum);
         if (fp->col > len)
            fp->col = len;
      }
      bookGetVirtualColInVirtualMode(curPor, fp, &vcol_start, NULL, &vcol_end);
      ++vcol_start;
      ++vcol_end;
   }

theend:
   if (argvars[1].tag != VAR_UNKNOWN && tv_get_bool(&argvars[1])) {
      allocReturnList(returnVar);
      list_append_number(returnVar->list, vcol_start);
      list_append_number(returnVar->list, vcol_end);
   } else
      returnVar->number = vcol_end;

   if (winchanged)
      portRestoreNoblock(&switchPort, TRUE);
}

private void
f_visualmode(Var *argvars, Var *returnVar) {
   Byte   str[2];

   returnVar->tag = VAR_STRING;
   str[0] = curBook->visual.kind;
   str[1] = ZERO;
   returnVar->string = copyStr(str);

   // A non-zero number or non-empty string argument: reset mode.
   if (non_zero_arg(&argvars[0]))
      curBook->visual.kind = ZERO;
}

private void
f_wildmenumode(Var *argvars UNUSED, Var *returnVar UNUSED) {
   if (wild_menu_showing || ((stateG & MODE_COMMLINE) && cmdline_pum_active()))
   returnVar->number = 1;
}

private void
f_wordcount(Var *argvars UNUSED, Var *returnVar) {
   allocReturnDict(returnVar);
   cursor_pos_info(returnVar->bag);
}

// "xor(expr, expr)" function
private void
f_xor(Var *argvars, Var *returnVar) {
   returnVar->number = varGetNumberChk(argvars, NULL) ^ varGetNumberChk(argvars + 1, NULL);
}

//}}}
//{{{exceptions & messages

private CS get_end_emsg(CondStack *cstack);

//Exception handling terms:
//
//  :try      ":try" command      |
//      ...      try block      |
//  :catch RE   ":catch" command   |
//      ...      catch clause      |- try conditional
//  :finally   ":finally" command   |
//      ...      finally clause      |
//  :endtry      ":endtry" command   /
//
//The try conditional may have any number of catch clauses and at most one
//finally clause.  A ":throw" command can be inside the try block, a catch
//clause, the finally clause, or in a function called or script sourced from
//there or even outside the try conditional.  Try conditionals may be nested.
//
//Configuration whether an exception is thrown on error or interrupt.  When
//the preprocessor macros below evaluate to FALSE, an error (anyEmsgG) or
//interrupt (gotInterruptG) under an active try conditional terminates the script
//after the non-active finally clauses of all active try conditionals have been
//executed.  Otherwise, errors and/or interrupts are converted into catchable
//exceptions (did_throw additionally set), which terminate the script only if
//not caught.  For user exceptions, only did_throw is set.  (Note: gotInterruptG can
//be set asynchronously afterwards by a SIGINT, so did_throw && gotInterruptG is not
//a reliant test that the exception currently being thrown is an interrupt
//exception.  Similarly, anyEmsgG can be set afterwards on an error in an
//(unskipped) conditional command inside an inactive conditional, so did_throw
//&& anyEmsgG is not a reliant test that the exception currently being thrown
//is an error exception.)  -  The macros can be defined as expressions checking
//for a variable that is allowed to be changed during execution of a script.
#if 0
// Expressions used for testing during the development phase.
# define THROW_ON_ERROR      (!eval_to_number("$EEGLNOERRTHROW"))
# define THROW_ON_INTERRUPT   (!eval_to_number("$EEGLNOINTTHROW"))
# define THROW_TEST
#else
// Values used for the Eegl release.
# define THROW_ON_ERROR      TRUE
# define THROW_ON_ERROR_TRUE
# define THROW_ON_INTERRUPT   TRUE
# define THROW_ON_INTERRUPT_TRUE
#endif

//When several errors appear in a row, setting "force_abort" is delayed until the failing command 
//returned.  "cause_abort" is set to TRUE meanwhile, in order to indicate that situation.  This 
//is useful when "force_abort" was set during execution of a function call from an expression: 
//the aborting of the expression evaluation is done without producing any error messages, but all
//error messages on parsing errors during the expression evaluation are given (even if a try 
//conditional is active).
private int cause_abort = FALSE;

//Return TRUE when immediately aborting on error, or when an interrupt occurred or an exception 
//was thrown but not caught.  Use for ":{range}call" to check whether an aborted function that 
//does not handle a range itself should be called again for the next line in the range. Also used 
//for cancelling expression evaluation after a function call caused an immediate abort. Note that 
//the first emsg() call temporarily resets "force_abort" until the throw point for error messages 
//has been reached.  That is, during cancellation of an expression evaluation after an aborting 
//function call or due to a parsing error, aborting() always returns the same value.
//"gotInterruptG" is also set by calling interrupt().
int
aborting(void) {
   return (anyEmsgG && force_abort) || gotInterruptG || did_throw;
}

//The value of "force_abort" is temporarily reset by the first emsg() call during an expression 
//evaluation, and "cause_abort" is used instead.  It might be necessary to restore "force_abort" 
//even before the throw point for the error message has been reached.  update_force_abort() 
//should be called then.
void
update_force_abort(void) {
   if (cause_abort)
   force_abort = TRUE;
}

//Return TRUE if a command with a subcommand resulting in "retcode" should abort the script 
//processing. Can be used to suppress an autocommand after execution of a failing subcommand as 
//long as the error message has not been displayed and actually caused the abortion.
int
should_abort(int retcode) {
   return ((retcode == FAIL && trylevel != 0 && !emsg_silent) || aborting());
}

//Return TRUE if a function with the "abort" flag should not be considered ended on an error. 
//This means that parsing commands is continued in order to find finally clauses to be executed, 
//and that some errors in skipped commands are still reported.
int
aborted_in_try(void) {
   // This function is only called after an error.  In this case, "force_abort"
   // determines whether searching for finally clauses is necessary.
   return force_abort;
}

//cause_errthrow(): Cause a throw of an error exception if appropriate. Return TRUE if the error 
//message should not be displayed by emsg(). Set "ignore", if the emsg() call should be ignored 
//completely.
//When several messages appear in the same command, the first is usually the most specific one and
//used as the exception value.  The "severe" flag can be set to TRUE, if a later but severer 
//message should be used instead.
int
cause_errthrow(Byte   *mesg, int severe, int* ignore) {
   MsgList   *elem;
   MsgList   **plist;

    /*
     * Do nothing when displaying the interrupt message or reporting an
     * uncaught exception (which has already been discarded then) at the top
     * level.  Also when no exception can be thrown. The message will be displayed by emsg().
     */
   if (suppress_errthrow)
      return FALSE;

    /*
     * If emsg() has not been called previously, temporarily reset
     * "force_abort" until the throw point for error messages has been
     * reached.  This ensures that aborting() returns the same value for all
     * errors that appear in the same command.  This means particularly that
     * for parsing errors during expression evaluation emsg() will be called
     * multiply, even when the expression is evaluated from a finally clause
     * that was activated due to an aborting error, interrupt, or exception.
     */
   if (!anyEmsgG) {
      cause_abort = force_abort;
      force_abort = FALSE;
   }

   //If no try conditional is active and no exception is being thrown and there has not been an
   //error in a try conditional or a throw so far, do nothing (for compatibility of non-EH 
   //scripts). The message will then be displayed by emsg(). When ":silent!" was used and we are 
   //not currently throwing an exception, do nothing.  The message text will
   //then be stored to v:errmsg by emsg() without displaying it.
   if (((trylevel == 0 && !cause_abort) || emsg_silent) && !did_throw)
      return FALSE;

    /*
     * Ignore an interrupt message when inside a try conditional or when an
     * exception is being thrown or when an error in a try conditional or
     * throw has been detected previously.  This is important in order that an
     * interrupt exception is catchable by the innermost try conditional and
     * not replaced by an interrupt message error exception.
     */
   if (mesg == (CS)_(e_interrupted)) {
      *ignore = TRUE;
      return TRUE;
   }

   //Ensure that all commands in nested function calls and sourced files are aborted immediately
   cause_abort = TRUE;

   //When an exception is being thrown, some commands (like conditionals) are not skipped. Errors 
   //in those commands may affect what of the subsequent commands are regarded part of catch and 
   //finally clauses. Catching the exception would then cause execution of commands not intended 
   //by the user, who wouldn't even get aware of the problem. Therefore, discard the
   //exception currently being thrown to prevent it from being caught. Just
   //execute finally clauses and terminate.
   if (did_throw) {
      // When discarding an interrupt exception, reset gotInterruptG to prevent the
      // same interrupt being converted to an exception again and discarding
      // the error exception we are about to throw here.
      if (current_exception->type == ET_INTERRUPT)
          gotInterruptG = FALSE;
      discard_current_exception();
   }

#ifdef THROW_TEST
   if (!THROW_ON_ERROR) {
      //Print error message immediately without searching for a matching
      //catch clause; just finally clauses are executed before the script is terminated.
      return FALSE;
   } else
#endif
    {
   //Prepare the throw of an error exception, so that everything will be aborted (except for 
   //executing finally clauses), until the error exception is caught; if still uncaught at 
   //the top level, the error message will be displayed and the script processing terminated
   //then.  -  This function has no access to the conditional stack. Thus, the actual throw is made
   //after the failing command has returned.  -  Throw only the first of several errors in a row, 
   //except a severe error is following.
   if (msg_list) {
      plist = msg_list;
      while (*plist != NULL)
         plist = &(*plist)->next;

      elem = ALLOC_CLEAR_ONE(MsgList);
      if (elem == NULL) {
         suppress_errthrow = TRUE;
         emsg(_(e_out_of_memory));
      } else {
         elem->msg = copyStr(mesg);
         if (elem->msg == NULL) {
            eeglFree(elem);
            suppress_errthrow = TRUE;
            emsg(_(e_out_of_memory));
         } else {
            elem->next = NULL;
            elem->throw_msg = NULL;
            *plist = elem;
            if (plist == msg_list || severe) {
               // Skip the extra "Eegl " prefix for message "E458".
               CS tmsg = elem->msg;
               if (STRNCMP(tmsg, "Eegl E", 5) == 0
                     && EE_ISDIGIT(tmsg[5])
                     && EE_ISDIGIT(tmsg[6])
                     && EE_ISDIGIT(tmsg[7])
                     && tmsg[8] == ':'
                     && tmsg[9] == ' ')
                  (*msg_list)->throw_msg = &tmsg[4];
               else
                  (*msg_list)->throw_msg = tmsg;
             }

             // Get the source name and lnum now, it may change before
             // reaching do_errthrow().
             elem->sfile = estack_sfile(ESTACK_NONE);
             elem->slnum = SOURCING_LNUM;
             elem->msg_compiling = estack_compiling;
         }
      }
   }
   return TRUE;
   }
}

// Free a "msg_list" and the messages it contains.
private void
free_msglist(MsgList *l) {
   MsgList  *messages, *next;

   messages = l;
   while (messages) {
      next = messages->next;
      eeglFree(messages->msg);
      eeglFree(messages->sfile);
      eeglFree(messages);
      messages = next;
   }
}

// Free global "*msg_list" and the messages it contains, then set "*msg_list" to NULL.
void
free_global_msglist(void) {
   free_msglist(*msg_list);
   *msg_list = NULL;
}

//Throw the message specified in the call to cause_errthrow() above as an error exception. 
//If cstack is NULL, postpone the throw until doCommand() has returned (see do_one_cmd()).
void
do_errthrow(CondStack *cstack, CS cmdname) {
   // Ensure that all commands in nested function calls and sourced files are aborted immediately.
   if (cause_abort) {
      cause_abort = FALSE;
      force_abort = TRUE;
   }

   // If no exception is to be thrown or the conversion should be done after
   // returning to a previous invocation of do_one_cmd(), do nothing.
   if (msg_list == NULL || *msg_list == NULL)
      return;

   if (throw_exception(*msg_list, ET_ERROR, cmdname) == FAIL)
      free_msglist(*msg_list);
   else {
      if (cstack)
         do_throw(cstack);
      else
         need_rethrow = TRUE;
   }
   *msg_list = NULL;
}

//do_intthrow(): Replace the current exception by an interrupt or interrupt exception if 
//appropriate.  Return TRUE if the current exception is discarded, FALSE otherwise.
int
do_intthrow(CondStack *cstack) {
   //If no interrupt occurred or no try conditional is active and no exception
   //is being thrown, do nothing (for compatibility of non-EH scripts).
   if (!gotInterruptG || (trylevel == 0 && !did_throw))
      return FALSE;

#ifdef THROW_TEST   // avoid warning for condition always true
   if (!THROW_ON_INTERRUPT) {
   //The interrupt aborts everything except for executing finally clauses.
   //Discard any user or error or interrupt exception currently being thrown.
   if (did_throw)
       discard_current_exception();
   } else
#endif
    {
   /*
    * Throw an interrupt exception, so that everything will be aborted
    * (except for executing finally clauses), until the interrupt exception
    * is caught; if still uncaught at the top level, the script processing
    * will be terminated then.  -  If an interrupt exception is already being thrown, do nothing.
    *
    */
   if (did_throw) {
       if (current_exception->type == ET_INTERRUPT)
      return FALSE;

       // An interrupt exception replaces any user or error exception.
       discard_current_exception();
   }
   if (throw_exception("Eegl:Interrupt", ET_INTERRUPT, NULL) != FAIL)
       do_throw(cstack);
    }

    return TRUE;
}

//Get an exception message that is to be stored in current_exception->value.
CS
get_exception_string(
void   *value,
   ExceptionKind type,
   Byte   *cmdname,
   int      *should_free)
{
   CS ret;
   CS mesg;
   int      cmdlen;
   CS   p;
   CS val;

   if (type == ET_ERROR) {
      *should_free = TRUE;
      mesg = ((MsgList *)value)->throw_msg;
      if (cmdname && *cmdname != ZERO) {
         cmdlen = (int)STRLEN(cmdname);
         ret = copySubstr(S"Eegl(", 4 + cmdlen + 2 + STRLEN(mesg));
         STRCPY(&ret[4], cmdname);
         STRCPY(&ret[4 + cmdlen], "):");
         val = ret + 4 + cmdlen + 2;
      } else {
         ret = copySubstr(S"Eegl:", 4 + STRLEN(mesg));
         val = ret + 4;
      }

      // msg_add_fname may have been used to prefix the message with a file
      // name in quotes.  In the exception value, put the file name in
      // parentheses and move it to the end.
      for (p = mesg; ; p++) {
          if (*p == ZERO
             || (*p == 'E'
            && EE_ISDIGIT(p[1])
            && (p[2] == ':'
                || (EE_ISDIGIT(p[2])
               && (p[3] == ':'
                   || (EE_ISDIGIT(p[3])
                  && p[4] == ':')))))
         ){
            if (*p == ZERO || p == mesg)
                STRCAT(val, mesg);  // 'E123' missing or at beginning
            else {
               // '"filename" E123: message text'
               if (mesg[0] != '"' || p-2 < &mesg[1] || p[-2] != '"' || p[-1] != ' ')
                  // "E123:" is part of the file name.
                  continue;

                STRCAT(val, p);
                p[-2] = ZERO;
                sprintf((char *)(val + STRLEN(p)), " (%s)", &mesg[1]);
                p[-2] = '"';
            }
            break;
        }
      }
   } else {
      *should_free = FALSE;
      ret = value;
   }

   return ret;
}


//Throw a new exception.  Return FAIL when out of memory or it was tried to throw an illegal user 
//exception. "value" is the exception string for a user or interrupt exception, or points to a 
//message list in case of an error exception.
int
throw_exception(void *value, ExceptionKind type, CS commName) {
   Exception   *excp;
   int      should_free;

   //Disallow faking Interrupt or error exceptions as user exceptions.  They
   //would be treated differently from real interrupt or error exceptions
   //when no active try block is found, see doCommand().
   if (type == ET_USER) {
      if (STRNCMP((CS)value, "Eegl", 3) == 0
         && (((CS)value)[3] == ZERO || ((CS)value)[3] == ':'
             || ((CS)value)[3] == '(')
      ){
          emsg(_(e_cannot_throw_exceptions_with_eegl_prefix));
          goto fail;
      }
   }

   excp = ALLOC_ONE(Exception);
   if (excp == NULL)
      goto nomem;

   if (type == ET_ERROR)
      // Store the original message and prefix the exception value with
      // "Eegl:" or, if a command name is given, "Eegl(commname):".
      excp->messages = (MsgList *)value;

   excp->value = get_exception_string(value, type, commName, &should_free);
   if (excp->value == NULL && should_free)
      goto nomem;

   excp->type = type;
   if (type == ET_ERROR && ((MsgList *)value)->sfile != NULL) {
      MsgList *entry = (MsgList *)value;

      excp->throw_name = entry->sfile;
      entry->sfile = NULL;
      excp->throw_lnum = entry->slnum;
   } else {
      excp->throw_name = estack_sfile(ESTACK_NONE);
      if (excp->throw_name == NULL)
         excp->throw_name = copyStr((CS)"");
      if (excp->throw_name == NULL) {
         if (should_free)
            eeglFree(excp->value);
         goto nomem;
      }
      excp->throw_lnum = SOURCING_LNUM;
   }

   excp->stacktrace = stacktrace_create();
   if (excp->stacktrace)
      excp->stacktrace->refcount = 1;

   if (p_verbose >= 13 || debug_break_level > 0) {
   int   save_msg_silent = msg_silent;

   if (debug_break_level > 0)
       msg_silent = FALSE;      // display messages
   else
       verbose_enter();
   ++no_wait_return;
   if (debug_break_level > 0 || *p_vfile == ZERO)
       msg_scroll = TRUE;       // always scroll up, don't overwrite

   smsg(_("Exception thrown: %s"), excp->value);
   msg_puts(S"\n");   // don't overwrite this either

   if (debug_break_level > 0 || *p_vfile == ZERO)
       commlineRowG = msgRowG;
   --no_wait_return;
   if (debug_break_level > 0)
       msg_silent = save_msg_silent;
   else
       verbose_leave();
   }

   current_exception = excp;
   return OK;

nomem:
   eeglFree(excp);
   suppress_errthrow = TRUE;
   emsg(_(e_out_of_memory));
fail:
   current_exception = NULL;
   return FAIL;
}

//Discard an exception.  "was_finished" is set when the exception has been caught and the catch 
//clause has been ended normally.
private void
discard_exception(Exception *excp, int was_finished) {
   Byte      *saved_IObuff;

   if (current_exception == excp)
      current_exception = NULL;
   if (excp == NULL) {
      internal_error(S"discard_exception()");
      return;
   }

   if (p_verbose >= 13 || debug_break_level > 0) {
      int   save_msg_silent = msg_silent;

      saved_IObuff = copyStr(IObuff);
      if (debug_break_level > 0)
          msg_silent = FALSE;      // display messages
      else
          verbose_enter();
      ++no_wait_return;
      if (debug_break_level > 0 || *p_vfile == ZERO)
          msg_scroll = TRUE;       // always scroll up, don't overwrite
      smsg(was_finished
             ? _("Exception finished: %s")
             : _("Exception discarded: %s"),
         excp->value);
      msg_puts(S"\n");   // don't overwrite this either
      if (debug_break_level > 0 || *p_vfile == ZERO)
          commlineRowG = msgRowG;
      --no_wait_return;
      if (debug_break_level > 0)
          msg_silent = save_msg_silent;
      else
          verbose_leave();
      STRCPY(IObuff, saved_IObuff);
      eeglFree(saved_IObuff);
   }
   if (excp->type != ET_INTERRUPT)
      eeglFree(excp->value);
   if (excp->type == ET_ERROR)
      free_msglist(excp->messages);
   eeglFree(excp->throw_name);
   list_unref(excp->stacktrace);
   eeglFree(excp);
}

void
discard_current_exception(void) {
   if (current_exception)
      discard_exception(current_exception, FALSE);
   did_throw = FALSE;
   need_rethrow = FALSE;
}

// Put an exception on the caught stack.
void
catch_exception(Exception *excp) {
   excp->caught = caught_stack;
   caught_stack = excp;
   set_EeglVar_string(VV_EXCEPTION, (CS)excp->value, -1);
   set_EeglVar_list(VV_STACKTRACE, excp->stacktrace);
   if (*excp->throw_name != ZERO) {
      if (excp->throw_lnum != 0)
         eeSnprintf(
            IObuff, IOSIZE, _("%s, line %ld"), excp->throw_name, (long)excp->throw_lnum
         );
      else
         eeSnprintf(IObuff, IOSIZE, "%s", excp->throw_name);
      set_EeglVar_string(VV_THROWPOINT, IObuff, -1);
   } else
      // throw_name not set on an exception from a command that was typed.
      set_EeglVar_string(VV_THROWPOINT, NULL, -1);

   if (p_verbose >= 13 || debug_break_level > 0) {
      int   save_msg_silent = msg_silent;

      if (debug_break_level > 0)
         msg_silent = FALSE;      // display messages
      else
         verbose_enter();
      ++no_wait_return;
      if (debug_break_level > 0 || *p_vfile == ZERO)
         msg_scroll = TRUE;       // always scroll up, don't overwrite

      smsg(_("Exception caught: %s"), excp->value);
      msg_puts(S"\n");   // don't overwrite this either

      if (debug_break_level > 0 || *p_vfile == ZERO)
         commlineRowG = msgRowG;
      --no_wait_return;
      if (debug_break_level > 0)
         msg_silent = save_msg_silent;
      else
         verbose_leave();
    }
}

// Remove an exception from the caught stack.
void
finish_exception(Exception *excp) {
   if (excp != caught_stack)
      internal_error(S"finish_exception()");
   caught_stack = caught_stack->caught;
   if (caught_stack) {
   set_EeglVar_string(VV_EXCEPTION, (CS)caught_stack->value, -1);
   set_EeglVar_list(VV_STACKTRACE, caught_stack->stacktrace);
   if (*caught_stack->throw_name != ZERO) {
      if (caught_stack->throw_lnum != 0) {
         eeSnprintf(
            IObuff, IOSIZE, _("%s, line %ld"), caught_stack->throw_name, 
            (long)caught_stack->throw_lnum
         );
      } else {
         eeSnprintf(IObuff, IOSIZE, "%s", caught_stack->throw_name);
      } 
      set_EeglVar_string(VV_THROWPOINT, IObuff, -1);
   } else
      // throw_name not set on an exception from a command that was typed.
      set_EeglVar_string(VV_THROWPOINT, NULL, -1);
   } else {
      set_EeglVar_string(VV_EXCEPTION, NULL, -1);
      set_EeglVar_string(VV_THROWPOINT, NULL, -1);
      set_EeglVar_list(VV_STACKTRACE, NULL);
   }

   // Discard the exception, but use the finish message for 'verbose'.
   discard_exception(excp, TRUE);
}

// Save the current exception state in "estate"
void
exception_state_save(ExceptionState *estate) {
   estate->currentException = current_exception;
   estate->didThrow = did_throw;
   estate->needRethrow = need_rethrow;
   estate->tryLevel = trylevel;
   estate->didEmsg = anyEmsgG;
}

// Restore the current exception state from "estate"
void
exception_state_restore(ExceptionState *estate) {
   // Handle any outstanding exceptions before restoring the state
   if (did_throw)
      handle_did_throw();
   current_exception = estate->currentException;
   did_throw = estate->didThrow;
   need_rethrow = estate->needRethrow;
   trylevel = estate->tryLevel;
   anyEmsgG = estate->didEmsg;
}

// Clear the current exception state
void
exception_state_clear(void) {
   current_exception = NULL;
   did_throw = FALSE;
   need_rethrow = FALSE;
   trylevel = 0;
   anyEmsgG = 0;
}

//}}}
//{{{report

// Flags specifying the message displayed by report_pending.
#define RP_MAKE      0
#define RP_RESUME   1
#define RP_DISCARD   2

//Report information about something pending in a finally clause if required by the 'verbose' 
//option or when debugging.  "action" tells whether something is made pending or something 
//pending is resumed or discarded. "pending" tells what is pending. "value" specifies the return 
//value for a pending ":return" or the exception value for a pending exception.
private void
report_pending(int action, int pending, void *value) {
   CS mesg;
   CS s;
   int      save_msg_silent;

   switch (action) {
   case RP_MAKE:
      mesg = _("%s made pending");
      break;
   case RP_RESUME:
      mesg = _("%s resumed");
      break;
   // case RP_DISCARD:
   default:
      mesg = _("%s discarded");
      break;
   }

   switch (pending) {
   case CSTP_NONE:
       return;

   case CSTP_CONTINUE:
       s = S":continue";
       break;
   case CSTP_BREAK:
       s = S":break";
       break;
   case CSTP_FINISH:
       s = S":finish";
       break;
   case CSTP_RETURN:
       // ":return" command producing value, allocated
       s = get_return_cmd(value);
       break;

   default:
      if (pending & CSTP_THROW) {
         eeSnprintf(IObuff, IOSIZE, mesg, _("Exception"));
         mesg = copySubstr(IObuff, STRLEN(IObuff) + 4);
         STRCAT(mesg, ": %s");
         s = ((Exception *)value)->value;
      } ei ((pending & CSTP_ERROR) && (pending & CSTP_INTERRUPT))
         s = _("Error and interrupt");
      ei (pending & CSTP_ERROR)
         s = _("Error");
      else // if (pending & CSTP_INTERRUPT)
         s = _("Interrupt");
   }

   save_msg_silent = msg_silent;
   if (debug_break_level > 0)
      msg_silent = FALSE;   // display messages
   ++no_wait_return;
   msg_scroll = TRUE;      // always scroll up, don't overwrite
   smsg(mesg, s);
   msg_puts(S"\n");   // don't overwrite this either
   commlineRowG = msgRowG;
   --no_wait_return;
   if (debug_break_level > 0)
      msg_silent = save_msg_silent;

   if (pending == CSTP_RETURN)
      eeglFree(s);
   ei (pending & CSTP_THROW)
      eeglFree(mesg);
}

//If something is made pending in a finally clause, report it if required by
//the 'verbose' option or when debugging.
void
report_make_pending(int pending, void *value) {
   if (p_verbose >= 14 || debug_break_level > 0) {
      if (debug_break_level <= 0)
          verbose_enter();
      report_pending(RP_MAKE, pending, value);
      if (debug_break_level <= 0)
          verbose_leave();
   }
}

//If something pending in a finally clause is resumed at the ":endtry", report
//it if required by the 'verbose' option or when debugging.
private void
report_resume_pending(int pending, void *value) {
   if (p_verbose >= 14 || debug_break_level > 0) {
      if (debug_break_level <= 0)
          verbose_enter();
      report_pending(RP_RESUME, pending, value);
      if (debug_break_level <= 0)
          verbose_leave();
    }
}

//If something pending in a finally clause is discarded, report it if required
//by the 'verbose' option or when debugging.
private void
report_discard_pending(int pending, void *value) {
   if (p_verbose >= 14 || debug_break_level > 0) {
      if (debug_break_level <= 0)
         verbose_enter();
      report_pending(RP_DISCARD, pending, value);
      if (debug_break_level <= 0)
         verbose_leave();
    }
}

//}}}
//{{{if, else

//Return TRUE if "arg" is only a variable, register, environment variable,
//option name or string.
int
cmd_is_name_only(CS arg) {
   Byte  *p = arg;
   Byte  *alias = NULL;
   int       name_only = FALSE;

   if (*p == '@') {
      ++p;
      if (*p != ZERO)
         ++p;
   } ei (*p == '\'' || *p == '"') {
      int       r;

      if (*p == '"')
         r = evalStringLiteral(&p, NULL, FALSE, FALSE);
      else
         r = evalRawString(&p, NULL, FALSE, FALSE);
      if (r == FAIL)
          return FALSE;
   } else {
      if (*p == '&') {
         ++p;
         if (STRNCMP("l:", p, 2) == 0 || STRNCMP("g:", p, 2) == 0)
            p += 2;
      } ei (*p == '$')
         ++p;
      (void)get_name_len(&p, &alias, FALSE, FALSE);
   }
   name_only = endsComm(skipwhite(p));
   eeglFree(alias);
   return name_only;
}

//":eval".
void
c_eval(Invocation *invo) {
   Var   tv;
   EvalCtx   evalarg;
   fillEvalArgFromInvo(OUT &evalarg, invo, invo->skip);

   if (eval0(invo->arg, &tv, invo, &evalarg) == OK) {
      clearVar(&tv);
   }

   clear_evalarg(&evalarg, invo);
}

//Start a new scope/block.  Caller should have checked that ind is not exceeding CSTACK_LEN.
private void
enter_block(CondStack *cstack) {
   ++cstack->ind;
   cstack->cs_script_var_len[cstack->ind] = 0;
   cstack->cs_block_id[cstack->ind] = 0;
}

private void
leave_block(CondStack *cstack) {
   --cstack->ind;
}

// ":if".
void
c_if(Invocation *invo) {
   Boole error = false;
   CondStack   *cstack = invo->cstack;
   
   if (cstack->ind == CSTACK_LEN - 1) {
      invo->errmsg = _(e_if_nesting_too_deep);
      return;
   } 
   enter_block(cstack);
   cstack->flags[cstack->ind] = 0;

   //Don't do something after an error, interrupt, or throw, or when
   //there is a surrounding conditional and it was not active.
   Boole skip = anyEmsgG || gotInterruptG || did_throw || (cstack->ind > 0
      && !(cstack->flags[cstack->ind - 1] & CSF_ACTIVE));

   int result = eval_to_bool(invo->arg, OUT &error, invo, skip, false);

   if (!skip && !error) {
      if (result)
         cstack->flags[cstack->ind] = CSF_ACTIVE | CSF_TRUE;
   } else
       // set TRUE, so this conditional will never get active
       cstack->flags[cstack->ind] = CSF_TRUE;
}

// ":endif".
void
c_endif(Invocation *invo) {
   CondStack   *cstack = invo->cstack;

   did_endif = TRUE;
   if (cstack->ind < 0
          || (cstack->flags[cstack->ind] & (CSF_WHILE | CSF_FOR | CSF_TRY | CSF_BLOCK))
   )
      invo->errmsg = _(e_endif_without_if);
   else {
      //When debugging or a breakpoint was encountered, display the debug prompt (if not already 
      //done). This shows the user that an ":endif" is executed when the ":if" or a previous 
      //":elseif" was not TRUE. Handle a ">quit" debug command as if an interrupt had occurred 
      //before the ":endif". That is, throw an interrupt exception if appropriate.
      //Doing this here prevents an exception for a parsing error being discarded by throwing 
      //the interrupt exception later on.
      if (!(cstack->flags[cstack->ind] & CSF_TRUE) && dbg_check_skipped(invo))
         (void)do_intthrow(cstack);

      leave_block(cstack);
   }
}

//":else" and ":elseif".
void
c_else(Invocation *invo) {
   Boole error;
   Boole skip;
   int result;
   CondStack   *cstack = invo->cstack;

   //Don't do something after an error, interrupt, or throw, or when there is
   //a surrounding conditional and it was not active.
   skip = anyEmsgG || gotInterruptG || did_throw || (cstack->ind > 0
       && !(cstack->flags[cstack->ind - 1] & CSF_ACTIVE));

   if (cstack->ind < 0
       || (cstack->flags[cstack->ind]
            & (CSF_WHILE | CSF_FOR | CSF_TRY | CSF_BLOCK))
   ) {
      if (invo->id == C_else) {
         invo->errmsg = _(e_else_without_if);
         return;
      }
      invo->errmsg = _(e_elseif_without_if);
      skip = TRUE;
   } ei (cstack->flags[cstack->ind] & CSF_ELSE) {
      if (invo->id == C_else) {
         invo->errmsg = _(e_multiple_else);
         return;
      }
      invo->errmsg = _(e_elseif_after_else);
      skip = TRUE;
   }

   if (cstack->ind >= 0) {
      //Variables declared in the previous block can no longer be
      //used. Needs to be done before setting "flags".
      leave_block(cstack);
      enter_block(cstack);
   }

   // if skipping or the ":if" was TRUE, reset ACTIVE, otherwise set it
   if (skip || cstack->flags[cstack->ind] & CSF_TRUE) {
      if (invo->errmsg == NULL)
         cstack->flags[cstack->ind] = CSF_TRUE;
      skip = true;   // don't evaluate an ":elseif"
   } else
      cstack->flags[cstack->ind] = CSF_ACTIVE;

   //When debugging or a breakpoint was encountered, display the debug prompt (if not already 
   //done). This shows the user that an ":else" or ":elseif" is executed when the ":if" or 
   //previous ":elseif" was not TRUE.  Handle a ">quit" debug command as if an interrupt had 
   //occurred before the ":else" or ":elseif". That is, set "skip" and throw an interrupt
   //exception if appropriate. Doing this here prevents that an exception for a parsing errors is
   //discarded when throwing the interrupt exception later on.
   if (!skip && dbg_check_skipped(invo) && gotInterruptG) {
      (void)do_intthrow(cstack);
      skip = true;
   }

   if (invo->id == C_elseif) {
   // When skipping we ignore most errors, but a missing expression is wrong, perhaps it should 
   // have been "else". A double quote here is the start of a string, not a comment.
   if (skip && !isComment(invo->arg) && endsComm(invo->arg))
       showErrFmtMsg(_(e_invalid_expression_str), invo->arg);
   else
       result = eval_to_bool(invo->arg, OUT &error, invo, skip, false);

   // When throwing error exceptions, we want to throw always the first of several errors in a row.
   // This is what actually happens when a conditional error was detected above and there is 
   // another failure when parsing the expression. Since the skip flag is set in this
   // case, the parsing error will be ignored by emsg().
   if (!skip && !error) {
      if (result)
         cstack->flags[cstack->ind] = CSF_ACTIVE | CSF_TRUE;
      else
         cstack->flags[cstack->ind] = 0;
   } ei (invo->errmsg == NULL)
      // set TRUE, so this conditional will never get active
      cstack->flags[cstack->ind] = CSF_TRUE;
   } else
      cstack->flags[cstack->ind] |= CSF_ELSE;
}

//}}}
//{{{loops

// Handle ":while" and ":for".
void
c_while(Invocation *invo) {
   int      result;
   CondStack   *cstack = invo->cstack;
   int      prev_flags = 0;

   if (cstack->ind == CSTACK_LEN - 1) {
      invo->errmsg = _(e_while_for_nesting_too_deep);
      return; 
   } 
   
   //The loop flag is set when we have jumped back from the matching
   //":endwhile" or ":endfor".  When not set, need to initialise this
   //cstack entry.
   if ((cstack->loopFlags & CSL_HAD_LOOP) == 0) {
      enter_block(cstack);
      ++cstack->loopLevel;
      cstack->cs_line[cstack->ind] = -1;
   }
   prev_flags = cstack->flags[cstack->ind];
   cstack->flags[cstack->ind] = invo->id == C_while ? CSF_WHILE : CSF_FOR;

   //Don't do anything after an error, interrupt, or throw, or when there is a surrounding 
   //conditional and it was not active.
   Boole skip = anyEmsgG || gotInterruptG || did_throw || (cstack->ind > 0
      && !(cstack->flags[cstack->ind - 1] & CSF_ACTIVE));
   Boole error = false;
   if (invo->id == C_while) {
       // ":while bool-expr"
       result = eval_to_bool(invo->arg, OUT &error, invo, skip, false);
   } else {
      EvalCtx   evalarg;

      // ":for var in list-expr"
      fillEvalArgFromInvo(OUT &evalarg, invo, skip);
      
      ForInfo* fi;
      if ((cstack->loopFlags & CSL_HAD_LOOP) != 0) {
         // Jumping here from a ":continue" or ":endfor": use the
         // previously evaluated list.
         fi = cstack->forInfo[cstack->ind];
         error = false;

         // the "in expr" is not used, skip over it
         //skipForLines(fi, &evalarg);
      } else {
         long save_lnum = SOURCING_LNUM;

         // Evaluate the argument and get the info in a structure.
         fi = eval_for_line(invo->arg, OUT &error, invo, &evalarg);
         cstack->forInfo[cstack->ind] = fi;

         // Errors should use the first line number.
         SOURCING_LNUM = save_lnum;
      }

      // use the element at the start of the list and advance
      if (!error && fi && !skip)
         result = next_for_item(fi, invo->arg);
      else
         result = FALSE;
      if (fi)
         // OR all the flags together, if a function was defined in
         // any round then the loop variable may have been used.
         fi->csFlags |= prev_flags;

      if (!result) {
         // If a function was defined in any round then set the
         // CSF_FUNC_DEF flag now, so that it's seen by leave_block().
         if (fi && (fi->csFlags & CSF_FUNC_DEF))
            cstack->flags[cstack->ind] |= CSF_FUNC_DEF;

         free_for_info(fi);
         cstack->forInfo[cstack->ind] = NULL;
      }
      clear_evalarg(&evalarg, invo);
   }

   //If this cstack entry was just initialised and is active, set the
   //loop flag, so doCommand() will set the line number in cs_line[].
   //If executing the command a second time, clear the loop flag.
   if (!skip && !error && result) {
      cstack->flags[cstack->ind] |= (CSF_ACTIVE | CSF_TRUE);
      cstack->loopFlags ^= CSL_HAD_LOOP;
   } else {
      cstack->loopFlags &= ~CSL_HAD_LOOP;
      // If the ":while" evaluates to FALSE or ":for" is past the end of
      // the list, show the debug prompt at the ":endwhile"/":endfor" as
      // if there was a ":break" in a ":while"/":for" evaluating to TRUE.
      if (!skip && !error)
         cstack->flags[cstack->ind] |= CSF_TRUE;
   }
}

//":continue"
void
c_continue(Invocation *invo) {
   int      idx;
   CondStack   *cstack = invo->cstack;

   if (cstack->loopLevel <= 0 || cstack->ind < 0)
      invo->errmsg = _(e_continue_without_while_or_for);
   else {
      // Try to find the matching ":while". This might stop at a try conditional not in its 
      // "finally" clause (which is then to be executed next). Therefore, inactivate all 
      // conditionals except the ":while" itself (if reached).
      idx = cleanup_conditionals(cstack, CSF_WHILE | CSF_FOR, FALSE);
      if (idx >= 0 && (cstack->flags[idx] & (CSF_WHILE | CSF_FOR))) {
         rewind_conditionals(cstack, idx, CSF_TRY, &cstack->tryLevel);
         //Set CSL_HAD_CONT, so doCommand() will jump back to the matching ":while".
         cstack->loopFlags |= CSL_HAD_CONT;   // let doCommand() handle it
      } else {
         // If a try conditional not in its finally clause is reached first,
         // make the ":continue" pending for execution at the ":endtry".
         cstack->pending[idx] = CSTP_CONTINUE;
         report_make_pending(CSTP_CONTINUE, NULL);
      }
   }
}

//":break"
void
c_break(Invocation *invo) {
   CondStack   *cstack = invo->cstack;

   if (cstack->loopLevel <= 0 || cstack->ind < 0)
      invo->errmsg = _(e_break_without_while_or_for);
   else {
      // Inactivate conditionals until the matching ":while" or a try conditional not in its 
      // finally clause (which is then to be executed next) is found.  In the latter case, make 
      // the ":break" pending for execution at the ":endtry".
      int idx = cleanup_conditionals(cstack, CSF_WHILE | CSF_FOR, TRUE);
      if (idx >= 0 && !(cstack->flags[idx] & (CSF_WHILE | CSF_FOR))) {
         cstack->pending[idx] = CSTP_BREAK;
         report_make_pending(CSTP_BREAK, NULL);
      }
   }
}

//":endwhile" and ":endfor"
void
c_endwhile(Invocation *invo) {
   CondStack   *cstack = invo->cstack;
   CS err;
   int      csf;

   if (invo->id == C_endwhile) {
      err = e_endwhile_without_while;
      csf = CSF_WHILE;
   } else {
      err = e_endfor_without_for;
      csf = CSF_FOR;
   }

   if (cstack->loopLevel <= 0 || cstack->ind < 0)
      invo->errmsg = _(err);
   else {
      int fl = cstack->flags[cstack->ind];
      if (!(fl & csf)) {
         // If we are in a ":while" or ":for" but used the wrong endloop
         // command, do not rewind to the next enclosing ":for"/":while".
         if (fl & CSF_WHILE)
            invo->errmsg = _(e_using_endfor_with_while);
         ei (fl & CSF_FOR)
            invo->errmsg = _(e_using_endwhile_with_for);
      }
      if (!(fl & (CSF_WHILE | CSF_FOR))) {
         if (!(fl & CSF_TRY))
            invo->errmsg = _(e_missing_endif);
         ei (fl & CSF_FINALLY)
            invo->errmsg = _(e_missing_endtry);
         // Try to find the matching ":while" and report what's missing.
         int idx;
         for (idx = cstack->ind; idx > 0; --idx) {
            fl =  cstack->flags[idx];
            if ((fl & CSF_TRY) && !(fl & CSF_FINALLY)) {
               // Give up at a try conditional not in its finally clause.
               // Ignore the ":endwhile"/":endfor".
               invo->errmsg = _(err);
               return;
            }
            if (fl & csf)
               break;
         }
         // Cleanup and rewind all contained (and unclosed) conditionals.
         (void)cleanup_conditionals(cstack, CSF_WHILE | CSF_FOR, FALSE);
         rewind_conditionals(cstack, idx, CSF_TRY, &cstack->tryLevel);
      }

      //When debugging or a breakpoint was encountered, display the debug prompt (if not already 
      //done).  This shows the user that an ":endwhile"/":endfor" is executed when the ":while" 
      //was not TRUE or after a ":break".  Handle a ">quit" debug command as if an interrupt had 
      //occurred before the ":endwhile"/":endfor".  That is, throw an interrupt exception if 
      //appropriate.  Doing this here prevents that an exception for a parsing error is discarded 
      //when throwing the interrupt exception later on.
      ei (cstack->flags[cstack->ind] & CSF_TRUE
            && !(cstack->flags[cstack->ind] & CSF_ACTIVE)
            && dbg_check_skipped(invo))
         (void)do_intthrow(cstack);

      // Set loop flag, so doCommand() will jump back to the matching ":while" or ":for".
      cstack->loopFlags |= CSL_HAD_ENDLOOP;
   }
}

//"opening bracket", start of a block
void
c_block(Invocation *invo) {
   CondStack   *cstack = invo->cstack;

   if (cstack->ind == CSTACK_LEN - 1)
      invo->errmsg = _(e_block_nesting_too_deep);
   else {
      enter_block(cstack);
      cstack->flags[cstack->ind] = CSF_BLOCK | CSF_ACTIVE | CSF_TRUE;
   }
}

//"closing bracket", end of a block in Vimscript
void
c_endblock(Invocation *invo) {
   CondStack   *cstack = invo->cstack;

   if (cstack->ind < 0 || (cstack->flags[cstack->ind] & CSF_BLOCK) == 0)
      invo->errmsg = _(e_endblock_without_block);
   else
      leave_block(cstack);
}

int
inside_block(Invocation *invo) {
   CondStack   *cstack = invo->cstack;
   int      i;

   for (i = 0; i <= cstack->ind; ++i) {
      if (cstack->flags[cstack->ind] & CSF_BLOCK)
          return TRUE;
   } 
   return FALSE;
}

//}}}
//{{{exceptions

//":throw expr"
void
c_throw(Invocation *invo) {
   Byte   *arg = invo->arg;
   Byte   *value;

   if (*arg != ZERO && *arg != '|' && *arg != '\n')
      value = eval_to_string_skip(arg, invo, invo->skip);
   else {
      emsg(_(e_argument_required));
      value = NULL;
   }

   // On error or when an exception is thrown during argument evaluation, do not throw.
   if (!invo->skip && value) {
      if (throw_exception(value, ET_USER, NULL) == FAIL)
          eeglFree(value);
      else
          do_throw(invo->cstack);
   }
}

//Throw the current exception through the specified cstack.  Common routine
//for ":throw" (user exception) and error and interrupt exceptions.  Also
//used for rethrowing an uncaught exception.
void
do_throw(CondStack *cstack) {
   int      idx;
   int      inactivate_try = FALSE;

   //Cleanup and inactivate up to the next surrounding try conditional that is not in its finally
   //clause.  Normally, do not inactivate the try conditional itself, so that its ACTIVE flag can 
   //be tested below. But if a previous error or interrupt has not been converted to an exception,
   //inactivate the try conditional, too, as if the conversion had been done, and reset the 
   //anyEmsgG or gotInterruptG flag, so this won't happen again at the next surrounding try 
   //conditional.
#ifndef THROW_ON_ERROR_TRUE
   if (anyEmsgG && !THROW_ON_ERROR) {
      inactivate_try = TRUE;
      anyEmsgG = FALSE;
   }
#endif
#ifndef THROW_ON_INTERRUPT_TRUE
   if (gotInterruptG && !THROW_ON_INTERRUPT) {
      inactivate_try = TRUE;
      gotInterruptG = FALSE;
   }
#endif
   idx = cleanup_conditionals(cstack, 0, inactivate_try);
   if (idx >= 0) {
      //If this try conditional is active and we are before its first ":catch", set THROWN so that
      //the ":catch" commands will check whether the exception matches.  When the exception came 
      //from any of the catch clauses, it will be made pending at the ":finally" (if present) and 
      //rethrown at the ":endtry".  This will also happen if the try conditional is inactive. This 
      //is the case when we are throwing an exception due to an error or interrupt on the way from
      //a preceding ":continue", ":break", ":return", ":finish", error or interrupt (not converted 
      //to an exception) to the finally clause or from a preceding throw of a user or error or 
      //interrupt exception to the matching catch clause or the finally clause.
      if (!(cstack->flags[idx] & CSF_CAUGHT)) {
         if (cstack->flags[idx] & CSF_ACTIVE)
            cstack->flags[idx] |= CSF_THROWN;
         else
            // THROWN may have already been set for a catchable exception
            // that has been discarded.  Ensure it is reset for the new exception.
            cstack->flags[idx] &= ~CSF_THROWN;
      }
      cstack->flags[idx] &= ~CSF_ACTIVE;
      cstack->pend.csp_ex[idx] = current_exception;
    }

    did_throw = TRUE;
}

// ":try"
void
c_try(Invocation *invo) {
   int      skip;
   CondStack   *cstack = invo->cstack;

   if (cstack->ind == CSTACK_LEN - 1)
      invo->errmsg = _(e_try_nesting_too_deep);
   else {
      enter_block(cstack);
      ++cstack->tryLevel;
      cstack->flags[cstack->ind] = CSF_TRY;
      cstack->pending[cstack->ind] = CSTP_NONE;

      //Don't do anything after an error, interrupt, or throw, or when there
      //is a surrounding conditional and it was not active.
      skip = anyEmsgG || gotInterruptG || did_throw || (cstack->ind > 0
         && !(cstack->flags[cstack->ind - 1] & CSF_ACTIVE));

      if (!skip) {
         // Set ACTIVE and TRUE.  TRUE means that the corresponding ":catch"
         // commands should check for a match if an exception is thrown and
         // that the finally clause needs to be executed.
         cstack->flags[cstack->ind] |= CSF_ACTIVE | CSF_TRUE;

         //":silent!", even when used in a try conditional, disables displaying of error messages 
         //and conversion of errors to exceptions.  When the silent commands again open a try
         //conditional, save "emsg_silent" and reset it so that errors are again converted to 
         //exceptions.  The value is restored when that try conditional is left.  If it is left 
         //normally, the commands following the ":endtry" are again silent.  If it is left by
         //a ":continue", ":break", ":return", or ":finish", the commands executed next are again 
         //silent. If it is left due to an aborting error, an interrupt, or an exception, restoring
         //"emsg_silent" does not matter since we are already in the aborting state and/or the 
         //exception has already been thrown. The effect is then just freeing the memory that was 
         //allocated to save the value.
         if (emsg_silent) {
            EMsgList* elem = ALLOC_ONE(EMsgList);
            if (!elem)
               emsg(_(e_out_of_memory));
            else {
               elem->saved_emsg_silent = emsg_silent;
               elem->next = cstack->cs_emsg_silent_list;
               cstack->cs_emsg_silent_list = elem;
               cstack->flags[cstack->ind] |= CSF_SILENT;
               emsg_silent = 0;
            }
         }
      }
   }
}

// ":catch /{pattern}/" and ":catch"
void
c_catch(Invocation *invo) {
   int      idx = 0;
   int      give_up = FALSE;
   int      skip = FALSE;
   int      caught = FALSE;
   Byte   *end;
   int      save_char = 0;
   RegMatch   regmatch;
   int      prev_gotInterruptG;
   CondStack   *cstack = invo->cstack;
   Byte   *pat;

   if (cstack->tryLevel <= 0 || cstack->ind < 0) {
      invo->errmsg = _(e_catch_without_try);
      give_up = TRUE;
   } else {
      if (!(cstack->flags[cstack->ind] & CSF_TRY)) {
         // Report what's missing if the matching ":try" is not in its finally clause.
         invo->errmsg = get_end_emsg(cstack);
         skip = TRUE;
      }
      for (idx = cstack->ind; idx > 0; --idx)
          if (cstack->flags[idx] & CSF_TRY)
         break;
      if (cstack->flags[idx] & CSF_TRY)
          cstack->flags[idx] |= CSF_CATCH;
      if (cstack->flags[idx] & CSF_FINALLY) {
         // Give up for a ":catch" after ":finally" and ignore it. Just parse.
         invo->errmsg = _(e_catch_after_finally);
         give_up = TRUE;
      } else
         rewind_conditionals(cstack, idx, CSF_WHILE | CSF_FOR, &cstack->loopLevel);
   }

   if (endsComm(invo->arg)) {  // no argument, catch all errors
      pat = (CS)".*";
      end = NULL;
      invo->nextComm = find_nextcmd(invo->arg);
   } else {
      pat = invo->arg + 1;
      end = skip_regexp_err(pat, *invo->arg, TRUE);
      if (end == NULL)
          give_up = TRUE;
    }

   if (!give_up) {
      //Don't do something when no exception has been thrown or when the corresponding try block 
      //never got active (because of an inactive surrounding conditional or after an error or 
      //interrupt or throw).
      if (!did_throw || !(cstack->flags[idx] & CSF_TRUE))
          skip = TRUE;

      //Check for a match only if an exception is thrown but not caught by
      //a previous ":catch".  An exception that has replaced a discarded
      //exception is not checked (THROWN is not set then).
      if (!skip && (cstack->flags[idx] & CSF_THROWN) && !(cstack->flags[idx] & CSF_CAUGHT)) {
         if (end && *end != ZERO && !endsComm(skipwhite(end + 1))) {
            showErrFmtMsg(_(e_trailing_characters_str), end);
            return;
         }

         // When debugging or a breakpoint was encountered, display the debug prompt (if not already 
         // done) before checking for a match. This is a helpful hint for the user when the regular 
         // expression matching fails.  Handle a ">quit" debug command as if an interrupt had occurred 
         // before the ":catch".  That is, discard the original exception, replace it by an interrupt 
         // exception, and don't catch it in this try block.
         if (!dbg_check_skipped(invo) || !do_intthrow(cstack)) {
            if (end) {
               save_char = *end;
               *end = ZERO;
            }
            // Disable error messages, it will make current_exception invalid.
            ++emsg_off;
            regmatch.regprog = compileRegexp(pat, RE_MAGIC + RE_STRING);
            --emsg_off;
            regmatch.rm_ic = FALSE;
            if (end)
                *end = save_char;
            if (regmatch.regprog == NULL)
                showErrFmtMsg(_(e_invalid_argument_str), pat);
            else {
               //Save the value of gotInterruptG and reset it. We don't want a previous 
               //interruption cancel matching, only hitting CTRL-C while matching should abort it.
               prev_gotInterruptG = gotInterruptG;
               gotInterruptG = FALSE;
               caught = eeRegexec_nl(&regmatch,
                      (CS)current_exception->value, (ColNr)0);
               gotInterruptG |= prev_gotInterruptG;
               eeRegFree(regmatch.regprog);
            }
         }
      }

      if (caught) {
          // Make this ":catch" clause active and reset anyEmsgG, gotInterruptG,
          // and did_throw. Put the exception on the caught stack.
          cstack->flags[idx] |= CSF_ACTIVE | CSF_CAUGHT;
          anyEmsgG = gotInterruptG = did_throw = FALSE;
          catch_exception((Exception *)cstack->pend.csp_ex[idx]);

         if (cstack->ind >= 0 && (cstack->flags[cstack->ind] & CSF_TRY)) {
            // Variables declared in the previous block can no longer be used.
            leave_block(cstack);
            enter_block(cstack);
         }

         // It's mandatory that the current exception is stored in the cstack
         // so that it can be discarded at the next ":catch", ":finally", or
         // ":endtry" or when the catch clause is left by a ":continue",
         // ":break", ":return", ":finish", error, interrupt, or another exception.
         if (cstack->pend.csp_ex[cstack->ind] != current_exception)
            internal_error(S"c_catch()");
      } else {
         //If there is a preceding catch clause and it caught the exception, finish the exception 
         //now.  This happens also after errors except when this ":catch" was after the ":finally"
         //or not within a ":try".  Make the try conditional inactive so that the following catch 
         //clauses are skipped.  On an error or interrupt after the preceding try block or catch 
         //clause was left by a ":continue", ":break", ":return", or ":finish", discard the pending
         //action.
         cleanup_conditionals(cstack, CSF_TRY, TRUE);
      }
   }

   if (end)
      invo->nextComm = find_nextcmd(end);
}

// ":finally"
void
c_finally(Invocation *invo) {
   int idx;
   int skip = FALSE;
   int pending = CSTP_NONE;
   CondStack* cstack = invo->cstack;

   for (idx = cstack->ind; idx >= 0; --idx) {
      if (cstack->flags[idx] & CSF_TRY)
         break;
   } 
   if (cstack->tryLevel <= 0 || idx < 0) {
      invo->errmsg = _(e_finally_without_try);
      return;
   }

   if (!(cstack->flags[cstack->ind] & CSF_TRY)) {
      invo->errmsg = get_end_emsg(cstack);
      // Make this error pending, so that the commands in the following
      // finally clause can be executed.  This overrules also a pending
      // ":continue", ":break", ":return", or ":finish".
      pending = CSTP_ERROR;
   }

   if (cstack->flags[idx] & CSF_FINALLY) {
      // Give up for a multiple ":finally" and ignore it.
      invo->errmsg = _(e_multiple_finally);
      return;
   }
   rewind_conditionals(cstack, idx, CSF_WHILE | CSF_FOR, &cstack->loopLevel);

   //Don't do something when the corresponding try block never got active (because of an inactive 
   //surrounding conditional or after an error or interrupt or throw) or for a ":finally" without
   //":try" or a multiple ":finally".  After every other error (anyEmsgG or the conditional
   //errors detected above) or after an interrupt (gotInterruptG) or an exception (did_throw), the
   //finally clause must be executed. skip = !(cstack->flags[cstack->ind] & CSF_TRUE);

   if (!skip) {
      // When debugging or a breakpoint was encountered, display the debug prompt (if not already 
      // done). The user then knows that the finally clause is executed.
      if (dbg_check_skipped(invo)) {
         // Handle a ">quit" debug command as if an interrupt had occurred before the ":finally".
         // That is, discard the original exception and replace it by an interrupt exception.
         (void)do_intthrow(cstack);
      }

      //If there is a preceding catch clause and it caught the exception, finish the exception now.
      //This happens also after errors except when this is a multiple ":finally" or one not within 
      //a ":try". After an error or interrupt, this also discards a pending ":continue", ":break",
      //":finish", or ":return" from the preceding try block or catch clause.
      cleanup_conditionals(cstack, CSF_TRY, FALSE);

      if (cstack->ind >= 0 && (cstack->flags[cstack->ind] & CSF_TRY)) {
         // Variables declared in the previous block can no longer be used.
         leave_block(cstack);
         enter_block(cstack);
      }

      //Make anyEmsgG, gotInterruptG, did_throw pending. If set, they overrule a pending 
      //":continue", ":break", ":return", or ":finish". Then we have particularly to discard a 
      //pending return value (as done by the call to cleanup_conditionals() above when anyEmsgG or
      //gotInterruptG is set).  The pending values are restored by the ":endtry", except if there 
      //is a new error, interrupt, exception, ":continue", ":break", ":return", or ":finish" in 
      //the following finally clause.  A missing ":endwhile", ":endfor" or ":endif"
      //detected here is treated as if anyEmsgG and did_throw had already been set, respectively 
      //in case that the error is not converted to an exception, did_throw had already been unset.
      //We must not set anyEmsgG here since that would suppress the error message.
      if (pending == CSTP_ERROR || anyEmsgG || gotInterruptG || did_throw) {
         if (cstack->pending[cstack->ind] == CSTP_RETURN) {
            report_discard_pending(CSTP_RETURN, cstack->pend.csp_rv[cstack->ind]);
            discard_pending_return(cstack->pend.csp_rv[cstack->ind]);
         }
         if (pending == CSTP_ERROR && !anyEmsgG)
            pending |= (THROW_ON_ERROR) ? CSTP_THROW : 0;
         else
            pending |= did_throw ? CSTP_THROW : 0;
         pending |= anyEmsgG  ? CSTP_ERROR     : 0;
         pending |= gotInterruptG   ? CSTP_INTERRUPT : 0;
         cstack->pending[cstack->ind] = pending;

         // It's mandatory that the current exception is stored in the cstack so that it can be 
         // rethrown at the ":endtry" or be discarded if the finally clause is left by a 
         // ":continue", ":break", ":return", ":finish", error, interrupt, or another exception. 
         // When emsg() is called for a missing ":endif" or a missing ":endwhile"/":endfor" 
         // detected here, the exception will be discarded.
         if (did_throw && cstack->pend.csp_ex[cstack->ind] != current_exception)
            internal_error(S"ex_finally()");
      }

      //Set CSL_HAD_FINA, so doCommand() will reset anyEmsgG, gotInterruptG, and did_throw and 
      //make the finally clause active. This will happen after emsg() has been called for a missing
      //":endif" or a missing ":endwhile"/":endfor" detected here, so that the following finally 
      //clause will be executed even then.
      cstack->loopFlags |= CSL_HAD_FINA;
   }
}

//":endtry"
void
c_endtry(Invocation *invo) {
   int      idx;
   int      skip;
   int      rethrow = FALSE;
   int      pending = CSTP_NONE;
   void   *returnVar = NULL;
   CondStack   *cstack = invo->cstack;

   for (idx = cstack->ind; idx >= 0; --idx) {
      if (cstack->flags[idx] & CSF_TRY)
          break;
   } 
   if (cstack->tryLevel <= 0 || idx < 0) {
      invo->errmsg = _(e_endtry_without_try);
      return;
   }

   //Don't do anything after an error, interrupt or throw in the try block, catch clause, or 
   //finally clause preceding this ":endtry" or when an error or interrupt occurred after a 
   //":continue", ":break", ":return", or ":finish" in a try block or catch clause preceding this
   //":endtry" or when the try block never got active (because of an inactive surrounding 
   //conditional or after an error or interrupt or throw) or when there is a surrounding 
   //conditional and it has been made inactive by a ":continue", ":break", ":return", or ":finish" 
   //in the finally clause.  The latter case need not be tested since then
   //anything pending has already been discarded.
   skip = anyEmsgG || gotInterruptG || did_throw || !(cstack->flags[cstack->ind] & CSF_TRUE);

   if (!(cstack->flags[cstack->ind] & CSF_TRY)) {
      invo->errmsg = get_end_emsg(cstack);

      // Find the matching ":try" and report what's missing.
      rewind_conditionals(cstack, idx, CSF_WHILE | CSF_FOR,
                        &cstack->loopLevel);
      skip = TRUE;

      //If an exception is being thrown, discard it to prevent it from being rethrown at the end 
      //of this function. It would be discarded by the error message, anyway. Resets did_throw.
      //This does not affect the script termination due to the error since "trylevel" is 
      //decremented after emsg() has been called.
      if (did_throw)
          discard_current_exception();

      // report invo->errmsg, also when there already was an error
      anyEmsgG = FALSE;
   } else {
      idx = cstack->ind;

      //If we stopped with the exception currently being thrown at this
      //try conditional since we didn't know that it doesn't have
      //a finally clause, we need to rethrow it after closing the try conditional.
      if (did_throw && (cstack->flags[idx] & CSF_TRUE) && !(cstack->flags[idx] & CSF_FINALLY))
         rethrow = TRUE;
   }

   // If there was no finally clause, show the user when debugging or
   // a breakpoint was encountered that the end of the try conditional has
   // been reached: display the debug prompt (if not already done).  Do
   // this on normal control flow or when an exception was thrown, but not
   // on an interrupt or error not converted to an exception or when
   // a ":break", ":continue", ":return", or ":finish" is pending.  These
   // actions are carried out immediately.
   if ((rethrow || (!skip && !(cstack->flags[idx] & CSF_FINALLY)
          && !cstack->pending[idx]))
       && dbg_check_skipped(invo))
   {
      // Handle a ">quit" debug command as if an interrupt had occurred before the ":endtry".
      // That is, throw an interrupt exception and set "skip" and "rethrow".
      if (gotInterruptG) {
         skip = TRUE;
         (void)do_intthrow(cstack);
         // The do_intthrow() call may have reset did_throw or cstack->pending[idx].
         rethrow = FALSE;
         if (did_throw && !(cstack->flags[idx] & CSF_FINALLY))
            rethrow = TRUE;
      }
   }

   //If a ":return" is pending, we need to resume it after closing the try conditional; remember 
   //the return value. If there was a finally clause making an exception pending, we need to 
   //rethrow it. Make it the exception currently being thrown.
   if (!skip) {
      pending = cstack->pending[idx];
      cstack->pending[idx] = CSTP_NONE;
      if (pending == CSTP_RETURN)
         returnVar = cstack->pend.csp_rv[idx];
      ei (pending & CSTP_THROW)
         current_exception = cstack->pend.csp_ex[idx];
   }

   //Discard anything pending on an error, interrupt, or throw in the finally clause. If there was 
   //no ":finally", discard a pending ":continue", ":break", ":return", or ":finish" if an error or
   //interrupt occurred afterwards, but before the ":endtry" was reached. If an exception was 
   //caught by the last of the catch clauses and there was no finally clause, finish the exception 
   //now. This happens also after errors except when this ":endtry" is not within a ":try".
   //Restore "emsg_silent" if it has been reset by this try conditional.
   (void)cleanup_conditionals(cstack, CSF_TRY | CSF_SILENT, TRUE);

   if (cstack->ind >= 0 && (cstack->flags[cstack->ind] & CSF_TRY))
      leave_block(cstack);
   --cstack->tryLevel;

   if (!skip) {
      report_resume_pending(pending,
             (pending == CSTP_RETURN) ? returnVar :
             (pending & CSTP_THROW) ? (void *)current_exception : NULL);
      switch (pending) {
      case CSTP_NONE:
         break;

      // Reactivate a pending ":continue", ":break", ":return",
      // ":finish" from the try block or a catch clause of this try
      // conditional.  This is skipped, if there was an error in an
      // (unskipped) conditional command or an interrupt afterwards
      // or if the finally clause is present and executed a new error,
      // interrupt, throw, ":continue", ":break", ":return", or ":finish".
      case CSTP_CONTINUE:
         c_continue(invo);
         break;
      case CSTP_BREAK:
         c_break(invo);
         break;
      case CSTP_RETURN:
         do_return(invo, FALSE, FALSE, returnVar);
         break;
      case CSTP_FINISH:
         do_finish(invo, FALSE);
         break;

      // When the finally clause was entered due to an error,
      // interrupt or throw (as opposed to a ":continue", ":break",
      // ":return", or ":finish"), restore the pending values of
      // anyEmsgG, gotInterruptG, and did_throw.  This is skipped, if there
      // was a new error, interrupt, throw, ":continue", ":break",
      // ":return", or ":finish".  in the finally clause.
      default:
         if (pending & CSTP_ERROR)
             anyEmsgG = TRUE;
         if (pending & CSTP_INTERRUPT)
             gotInterruptG = TRUE;
         if (pending & CSTP_THROW)
             rethrow = TRUE;
         break;
      }
    }

   if (rethrow)
      // Rethrow the current exception (within this cstack).
      do_throw(cstack);
}

//enter_cleanup() and leave_cleanup()
//
//Functions to be called before/after invoking a sequence of autocommands for cleanup for a 
//failed command.  (Failure means here that a call to emsg() has been made, an interrupt occurred,
//or there is an uncaught exception from a previous autocommand execution of the same command.)
//
//Call enter_cleanup() with a pointer to a Cleanup and pass the same pointer to leave_cleanup().
//The Cleanup structure stores the pending error/interrupt/exception state.

//This function works a bit like ex_finally() except that there was not actually an extra try 
//block around the part that failed and an error or interrupt has not (yet) been converted to 
//an exception.  This function saves the error/interrupt/ exception state and prepares for the 
//call to doCommand() that is going to be made for the cleanup autocommand execution.
void
enter_cleanup(Cleanup *csp) {
   int      pending = CSTP_NONE;

   //Postpone anyEmsgG, gotInterruptG, did_throw.  The pending values will be
   //restored by leave_cleanup() except if there was an aborting error,
   //interrupt, or uncaught exception after this function ends.
   if (anyEmsgG || gotInterruptG || did_throw || need_rethrow) {
      csp->pending = (anyEmsgG     ? CSTP_ERROR     : 0)
              | (gotInterruptG      ? CSTP_INTERRUPT : 0)
              | (did_throw    ? CSTP_THROW     : 0)
              | (need_rethrow ? CSTP_THROW     : 0);

      //If we are currently throwing an exception (did_throw), save it as well. On an error not 
      //yet converted to an exception, update "force_abort" and reset "cause_abort" (as 
      //do_errthrow() would do). This is needed for the doCommand() call that is going to be made
      //for autocommand execution.  We need not save *msg_list because there is an extra instance 
      //for every call of doCommand(), anyway.
      if (did_throw || need_rethrow) {
         csp->exception = current_exception;
         current_exception = NULL;
      } else {
         csp->exception = NULL;
         if (anyEmsgG) {
            force_abort |= cause_abort;
            cause_abort = FALSE;
         }
      }
      anyEmsgG = gotInterruptG = did_throw = need_rethrow = FALSE;

      // Report if required by the 'verbose' option or when debugging.
      report_make_pending(pending, csp->exception);
   } else {
      csp->pending = CSTP_NONE;
      csp->exception = NULL;
   }
}

//See comment above enter_cleanup() for how this function is used.
//
//This function is a bit like ex_endtry() except that there was not actually
//an extra try block around the part that failed and an error or interrupt
//had not (yet) been converted to an exception when the cleanup autocommand
//sequence was invoked.
//
//This function has to be called with the address of the Cleanup structure
//filled by enter_cleanup() as an argument; it restores the error/interrupt/
//exception state saved by that function - except there was an aborting
//error, an interrupt or an uncaught exception during execution of the
//cleanup autocommands.  In the latter case, the saved error/interrupt/
//exception state is discarded.
void
leave_cleanup(Cleanup *csp) {
   int      pending = csp->pending;

   if (pending == CSTP_NONE)   // nothing to do
      return;

   // If there was an aborting error, an interrupt, or an uncaught exception after the 
   // corresponding call to enter_cleanup(), discard what has been made pending by it. Report this 
   // to the user if required by the 'verbose' option or when debugging.
   if (aborting() || need_rethrow) {
      if (pending & CSTP_THROW) // Cancel the pending exception (includes report).
         discard_exception(csp->exception, FALSE);
      else
         report_discard_pending(pending, NULL);

      // If an error was about to be converted to an exception when
      // enter_cleanup() was called, free the message list.
      if (msg_list)
         free_global_msglist();
   }

   //If there was no new error, interrupt, or throw between the calls to enter_cleanup() and 
   //leave_cleanup(), restore the pending error/interrupt/exception state.
   else {
      //If there was an exception being thrown when enter_cleanup() was
      //called, we need to rethrow it.  Make it the exception currently being thrown.
      if (pending & CSTP_THROW)
         current_exception = csp->exception;

      //If an error was about to be converted to an exception when
      //enter_cleanup() was called, let "cause_abort" take the part of
      //"force_abort" (as done by cause_errthrow()).
      ei (pending & CSTP_ERROR) {
         cause_abort = force_abort;
         force_abort = FALSE;
      }

      //Restore the pending values of anyEmsgG, gotInterruptG, and did_throw.
      if (pending & CSTP_ERROR)
         anyEmsgG = TRUE;
      if (pending & CSTP_INTERRUPT)
         gotInterruptG = TRUE;
      if (pending & CSTP_THROW)
         need_rethrow = TRUE;    // did_throw will be set by do_one_cmd()

      // Report if required by the 'verbose' option or when debugging.
      report_resume_pending(pending, (pending & CSTP_THROW) ? (void *)current_exception : NULL);
   }
}

//Make conditionals inactive and discard what's pending in finally clauses until the conditional 
//type searched for or a try conditional not in its finally clause is reached.  If this is in an 
//active catch clause, finish the caught exception. Return the cstack index where the search 
//stopped. Values used for "searched_cond" are (CSF_WHILE | CSF_FOR) or CSF_TRY or 0,
//the latter meaning the innermost try conditional not in its finally clause. "inclusive" tells 
//whether the conditional searched for should be made inactive itself (a try conditional not in 
//its finally clause possibly find before is always made inactive).  If "inclusive" is TRUE and
//"searched_cond" is CSF_TRY|CSF_SILENT, the saved former value of "emsg_silent", if reset when 
//the try conditional finally reached was entered, is restored (used by ex_endtry()). This is 
//normally done only when such a try conditional is left.
int
cleanup_conditionals(
   CondStack   *cstack,
   int      searched_cond,
   int      inclusive
) {
   int      idx;
   int      stop = FALSE;

   for (idx = cstack->ind; idx >= 0; --idx) {
      if (cstack->flags[idx] & CSF_TRY) {
         //Discard anything pending in a finally clause and continue the search. There may also be 
         //a pending ":continue", ":break", ":return", or ":finish" before the finally clause. We 
         //must not discard it, unless an error or interrupt occurred afterwards.
         if (anyEmsgG || gotInterruptG || (cstack->flags[idx] & CSF_FINALLY)) {
            switch (cstack->pending[idx]) {
            case CSTP_NONE:
               break;

            case CSTP_CONTINUE:
            case CSTP_BREAK:
            case CSTP_FINISH:
               report_discard_pending(cstack->pending[idx], NULL);
               cstack->pending[idx] = CSTP_NONE;
               break;

            case CSTP_RETURN:
               report_discard_pending(CSTP_RETURN, cstack->pend.csp_rv[idx]);
               discard_pending_return(cstack->pend.csp_rv[idx]);
               cstack->pending[idx] = CSTP_NONE;
               break;

            default:
               if (cstack->flags[idx] & CSF_FINALLY) {
                  if ((cstack->pending[idx] & CSTP_THROW) && cstack->pend.csp_ex[idx] != NULL) {
                     //Cancel the pending exception. This is in the finally clause, so that the 
                     //stack of the caught exceptions is not involved.
                     discard_exception( (Exception *)cstack->pend.csp_ex[idx], FALSE);
                  } else
                     report_discard_pending(cstack->pending[idx], NULL);
                  cstack->pending[idx] = CSTP_NONE;
               }
               break;
            }
         }

         //Stop at a try conditional not in its finally clause.  If this try
         //conditional is in an active catch clause, finish the caught exception.
         if (!(cstack->flags[idx] & CSF_FINALLY)) {
            if ((cstack->flags[idx] & CSF_ACTIVE)
               && (cstack->flags[idx] & CSF_CAUGHT)
               && !(cstack->flags[idx] & CSF_FINISHED)
            ) {
               finish_exception((Exception *)cstack->pend.csp_ex[idx]);
               cstack->flags[idx] |= CSF_FINISHED;
            }
            // Stop at this try conditional - except the try block never got active (because of 
            // an inactive surrounding conditional or when the ":try" appeared after an error or 
            // interrupt or throw).
            if (cstack->flags[idx] & CSF_TRUE) {
               if (searched_cond == 0 && !inclusive)
                  break;
               stop = TRUE;
            }
         }
      }

      // Stop on the searched conditional type (even when the surrounding
      // conditional is not active or something has been made pending).
      // If "inclusive" is TRUE and "searched_cond" is CSF_TRY|CSF_SILENT,
      // check first whether "emsg_silent" needs to be restored.
      if (cstack->flags[idx] & searched_cond) {
         if (!inclusive)
            break;
         stop = TRUE;
      }
      cstack->flags[idx] &= ~CSF_ACTIVE;
      if (stop && searched_cond != (CSF_TRY | CSF_SILENT))
         break;

      //When leaving a try conditional that reset "emsg_silent" on its entry after saving the 
      //original value, restore that value here and free the memory used to store it.
      if ((cstack->flags[idx] & CSF_TRY) && (cstack->flags[idx] & CSF_SILENT)) {
          EMsgList* elem = cstack->cs_emsg_silent_list;
          cstack->cs_emsg_silent_list = elem->next;
          emsg_silent = elem->saved_emsg_silent;
          eeglFree(elem);
          cstack->flags[idx] &= ~CSF_SILENT;
      }
      if (stop)
         break;
   }
   return idx;
}

// Return an appropriate error message for a missing endwhile/endfor/endif.
private CS
get_end_emsg(CondStack *cstack) {
   if (cstack->flags[cstack->ind] & CSF_WHILE)
      return _(e_missing_endwhile);
   if (cstack->flags[cstack->ind] & CSF_FOR)
      return _(e_missing_endfor);
   return _(e_missing_endif);
}

//}}}
//{{{syntax endings
//Rewind conditionals until index "idx" is reached.  "cond_type" and
//"cond_level" specify a conditional type and the address of a level variable
//which is to be decremented with each skipped conditional of the specified type.
//Also free "for info" structures where needed.
void
rewind_conditionals(
   CondStack   *cstack,
   int      idx,
   int      cond_type,
   int      *cond_level
){
   while (cstack->ind > idx) {
      if (cstack->flags[cstack->ind] & cond_type)
         --*cond_level;
      if (cstack->flags[cstack->ind] & CSF_FOR)
         free_for_info(cstack->forInfo[cstack->ind]);
      leave_block(cstack);
   }
}

// ":endfunction" or ":enddef" when not after a ":function"
void
c_endfunction(Invocation *invo) {
   if (invo->id == C_enddef)
      showErrFmtMsg(_(e_str_not_inside_function), ":enddef");
   else
      showErrFmtMsg(_(e_str_not_inside_function), ":endfunction");
}

// Return TRUE if the string "p" looks like a ":while" or ":for" command.
int
has_loop_cmd(CS p) {
   // skip modifiers, white space and ':'
   for (;;) {
      while (*p == ' ' || *p == '\t' || *p == ':')
          ++p;
      int len = modifier_len(p);
      if (len == 0)
          break;
      p += len;
   }
   if ((p[0] == 'w' && p[1] == 'h') || (p[0] == 'f' && p[1] == 'o' && p[2] == 'r'))
      return TRUE;
   return FALSE;
}

//}}}
