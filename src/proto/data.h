void list_add_watch(List *l, ListWatch *lw);
void list_rem_watch(List* l, ListWatch* lwrem);
List * list_alloc(void);
List * list_alloc_id(AllocId id UNUSED);
List * list_alloc_with_items(int count);
void list_set_item(List *l, int idx, Var *tv);
void allocReturnList(OUT Var* returnVar);
int allocReturnList_id(Var* returnVar, AllocId id);
void returnVar_list_set(OUT Var* returnVar, List *l);
void list_unref(List* l);
int list_free_nonref(int copyID);
void list_free_items(int copyID);
void list_free(List* l);
ListItem * listitem_alloc(void);
void listitem_free(List* l, ListItem* item);
void listitem_remove(List* l, ListItem* item);
long list_len(List* l);
int list_equal(List* l1, List* l2, int ic);
ListItem * list_find(List* l, long n);
long list_find_nr(List* l, long idx, OUT Boole* errorp);
CS list_find_str(List *l, long idx);
ListItem * list_find_index(List *l, long *idx);
long list_idx_of_item(List *l, ListItem *item);
void list_append(List* l, ListItem* item);
int list_append_tv(List* l, Var* newVal);
int listAppendBag(List *list, Bag* bag);
int list_append_list(List *list1, List *list2);
int list_append_string(List *l, CS str, int len);
int list_append_number(List* l, Long n);
int list_insert_tv(List *l, Var *tv, ListItem *item);
void list_insert(List *l, ListItem *ni, ListItem *item);
ListItem * check_range_index_one(List* l, long* n1, int quiet);
int check_range_index_two(List* l, long* n1, ListItem* li1, long* n2, int quiet);
int list_assign_range(
   List* dest,
   List* src,
   long idx1_arg,
   long idx2,
   int empty_idx2,
   CS op,
   Text varname
);
void f_flatten(Arr(Var) argvars, Var* returnVar);
void f_flattennew(Arr(Var) argvars, Var* returnVar);
void list2items(Arr(Var) argvars, Var* returnVar);
void string2items(Arr(Var) argvars, Var* returnVar);
int list_extend(List *l1, List *l2, ListItem *bef);
int list_concat(List *l1, List *l2, Var *tv);
List * list_slice(List *ol, long n1, long n2);
int list_slice_or_index(
   List* list,
   int range,
   Long n1_arg,
   Long n2_arg,
   int exclusive,
   Var* returnVar,
   int verbose
);
List * list_copy(List *orig, int deep, int top, int copyID);
void list_remove(List* l, ListItem* item, ListItem* item2);
CS list2string(Var* tv, int copyID, int restore_copyID);
int list_join(
    ArrayList* gap,
    List* l,
    CS sep,
    int echo_style,
    int restore_copyID,
    int copyID
);
void f_join(Arr(Var) argvars, Var* returnVar);
int eval_list(Byte **arg, Var* returnVar, EvalCtx *evalarg, int do_error);
int write_list(FILE* fd, List* list, int binary);
void init_static_list(StaticList10 *sl);
void f_list2str(Arr(Var) argvars, Var* returnVar);
void f_sort(Arr(Var) argvars, Var* returnVar);
void f_uniq(Arr(Var) argvars, Var* returnVar);
int filter_map_one(
   Var   *tv,         // original value
   Var   *expr,       // callback
   FilterMap filtermap,
   Var   *newtv,       // for map() and mapnew(): new value
   int      *remp      // for filter(): remove flag
);
void f_filter(Arr(Var) argvars, Var* returnVar);
void f_map(Arr(Var) argvars, Var* returnVar);
void f_mapnew(Arr(Var) argvars, Var* returnVar);
void f_foreach(Arr(Var) argvars, Var* returnVar);
void f_add(Arr(Var) argvars, Var* returnVar);
void f_count(Arr(Var) argvars, Var* returnVar);
void f_extend(Arr(Var) argvars, Var* returnVar);
void f_extendnew(Arr(Var) argvars, Var* returnVar);
void f_insert(Arr(Var) argvars, Var* returnVar);
void f_remove(Arr(Var) argvars, Var* returnVar);
void f_reverse(Arr(Var) argvars, Var* returnVar);
void f_reduce(Arr(Var) argvars, Var* returnVar);
void f_slice(Arr(Var) argvars, Var* returnVar);
int ga_grow_id(ArrayList *gap, int n, AllocId id);
EeSet * hash_create(void);
void hash_init(EeSet* ht);
int check_hashtab_frozen(EeSet* ht, CS command);
void hash_clear(EeSet* ht);
void hash_clear_all(EeSet* ht, int off);
EeSetItem * hash_find(EeSet* ht, Text key);
EeSetItem * hash_lookup(EeSet* ht, Text key, Hash hash);
void hash_debug_results(void);
int hash_add(EeSet* ht, Text key, CS command);
int hash_add_item(EeSet* ht, EeSetItem* hi, Text newItem, Hash hash);
void hash_set(EeSetItem *hi, Byte *key);
int hash_remove(EeSet* ht, EeSetItem* hi, CS command);
void hash_lock(EeSet *ht);
void hash_lock_size(EeSet *ht, int size);
void hash_unlock(EeSet* ht);
Hash calcHash(Text const key);
ExpandMatch expandMatchOfArrayList(ArrayList al);
void addExpandMatch(CS m, OUT ExpandMatch* t);
int type_any_or_unknown(TypeSpec *type);
Arr(char) vartype_name(VarTag type);
Var * allocVar(void);
Var * allocStringVar(CS s);
void freeVar(Var* varp);
void clearVar(Var* varp);
void initVarToNull(OUT Var* varp);
Long tv_get_number(Var* varp);
Long varGetNumberChk(Var* varp, OUT Boole* denote);
Boole tv_get_bool(Var* varp);
double tv_get_float(Var* varp);
int check_for_unknown_arg(Arr(Var) args, int idx);
int check_for_string_arg(Var* args, int idx);
int check_for_nonempty_string_arg(Var* args, int idx);
int check_for_opt_string_arg(Arr(Var) args, int idx);
int check_for_number_arg(Arr(Var) args, int idx);
int check_for_opt_number_arg(Var* args, int idx);
int check_for_float_or_nr_arg(Var* args, int idx);
int check_for_bool_arg(Var* args, int idx);
int check_for_opt_bool_arg(Var* args, int idx);
int check_for_opt_bool_or_number_arg(Var* args, int idx);
int check_for_blob_arg(Var* args, int idx);
int confirmVarIsList(Var* arg, int idx);
int confirmVarIsNonnullList(Var* args, int idx);
int confirmVarIsOptionalList(Arr(Var) args, int idx);
int check_for_dict_arg(Arr(Var) args, int idx);
int check_for_nonnull_dict_arg(Arr(Var) args, int idx);
int check_for_oself_arg(Arr(Var) args, int idx);
int check_for_opt_nonnull_dict_arg(Var* args, int idx);
int check_for_chan_or_job_arg(Var* args, int idx);
int check_for_opt_chan_or_job_arg(Var* args, int idx);
int check_for_job_arg(Var* args, int idx);
int check_for_opt_job_arg(Arr(Var) args, int idx);
int check_for_string_or_number_arg(Arr(Var) args, int idx);
int check_for_opt_string_or_number_arg(Var* args, int idx);
int check_for_buffer_arg(Var* args, int idx);
int check_for_opt_buffer_arg(Arr(Var) args, int idx);
int check_for_lnum_arg(Arr(Var) args, int idx);
int check_for_opt_lnum_arg(Arr(Var) args, int idx);
int check_for_string_or_blob_arg(Var* args, int idx);
int check_for_string_or_list_arg(Arr(Var) args, int idx);
int check_for_opt_string_or_list_arg(Arr(Var) args, int idx);
int check_for_string_or_dict_arg(Arr(Var) args, int idx);
int check_for_string_or_number_or_list_arg(Arr(Var) args, int idx);
int check_for_opt_string_or_number_or_list_arg(Arr(Var) args, int idx);
int check_for_repeat_func_arg(Arr(Var) args, int idx);
int check_for_string_list_or_dict_arg(Arr(Var) args, int idx);
int check_for_string_or_func_arg(Arr(Var) args, int idx);
int check_for_list_or_blob_arg(Arr(Var) args, int idx);
int check_for_list_arg(Arr(Var) args, int idx);
int check_for_list_or_or_blob_arg(Arr(Var) args, int idx);
int check_for_list_or_dict_arg(Arr(Var) args, int idx);
int check_for_list_or_dict_or_blob_arg(Arr(Var) args, int idx);
int check_for_list_dict_blob_or_string_arg(Arr(Var) args, int idx);
int check_for_opt_buffer_or_dict_arg(Arr(Var) args, int idx);
CS tv_get_string(Var* varp);
CS tv_get_string_strict(Var* varp);
CS tv_get_string_buf(Var* varp, CS buf);
CS convertVarToStringSingleUse(Var* varp);
CS convertVarToString(Var* varp, CS buf);
CS convertVarToString_strict(Var* varp, CS buf, int strict);
CS tv_stringify(Var* varp, CS buf);
int tv_check_lock(Var* tv, Text name, Boole use_gettext);
void copy_tv(OUT Var* to, Var* from);
int daCompareVars(
   Var* tv1,   // first operand
   Var* tv2,   // second operand
   ExprType type,   // operator
   Boole ignoreCase
);
int daCompareVars_list(
   Var    *tv1,
   Var    *tv2,
   ExprType  type,
   Boole ic,
   OUT int* res
);
int daCompareVars_null(Var *tv1, Var *tv2);
int daCompareVars_blob(Var* tv1, Var* tv2, ExprType type, int* res);
int daCompareVars_dict(
   Var* tv1,
   Var* tv2,
   ExprType  type,
   int ic,
   int* res
);
int daCompareVars_func(
   Var* tv1,
   Var* tv2,
   ExprType type,
   int ic,
   int* res
);
int daCompareVars_string(
   Var* tv1,
   Var* tv2,
   ExprType type,
   Boole ignoreCase,
   int* res
);
CS daStringOfVar(Var *arg, int quotes);
int tv_islocked(Var *tv);
int tv_equal(Var* tv1, Var* tv2, int ic);
int eval_option(
   Byte** arg,
   Var* returnVar,   // when NULL, only check if option exists
   int evaluate
);
int eval_number(CS* arg, Var* returnVar, int evaluate, int want_string);
CS tv2string(
   Var* tv,
   Byte** tofree,
   CS numbuf,
   int copyID)
;
int eval_env_var(OUT CS* arg, Var* returnVar, int evaluate);
LineNr tv_get_lnum(Arr(Var) argvars);
LineNr daGetLnumFromBookOrVar(Var* argvars, Book* book);
Book * daGetBook(Var* tv, Boole curtab_only);
Book * daGetBookFromArg(Var* tv);
int equal_type(TypeSpec *type1, TypeSpec *type2, int flags);
ExprType get_compare_type(CS p, int* len, int* type_is);
int tv2bool(Var* tv);
Bag * allocBag(void);
Bag * allocBag_id(AllocId id UNUSED);
Bag * allocBag_lock(int lock);
void allocReturnDict(Var* returnVar);
void returnVar_dict_set(Var* returnVar, Bag* b);
CS char_from_string(CS str, Long index);
Text textOfDi(DictItem* di);
Text textOfDi16(DictItem16* di);
Text textOfItem(EeSetItem* si);
int bagAdd(Bag* b, DictItem* item);
int bagAddNumber(Bag* d, CS key, Long nr);
int bagAdd_bool(Bag* d, CS key, Long nr);
int bagAddString(Bag* d, CS key, CS str);
int bagAddString_len(Bag *d, CS key, CS str, int len);
int bagAddList(Bag* d, CS key, List* list);
int bagAddVar(Bag* b, CS key, Var* tv);
int bagAddCallback(Bag* d, CS key, Callback* cb);
int bagAddFn(Bag* d, CS key, UserFunc* fp);
void bagIterateStart(Var* var, DictIterator* iter);
CS bagIterateNext(DictIterator* iter, Var** tv_result);
int bagAddBag(Bag* d, CS key, Bag* dict);
Ulong bagSize(Bag* b);
DictItem* bagFind(Bag* b, Text const key);
int bagHasKey(Bag* b, Text key);
int bagGetVar(Bag *d, Text key, Var* returnVar);
CS bagGetString(Bag* d, Text key, Boole save);
Long bagGetNumber(Bag *d, Text key);
Long bagGetNumber_def(Bag* b, Text const key, int def);
Long bagGetNumber_check(Bag* b, Text const key);
Boole bagGetBool(Bag *d, Text key, Boole def);
CS bagToString(Var* tv, int copyID, int restore_copyID);
int bagEval(OUT CS* arg, Var* returnVar, EvalCtx *evalarg, int literal);
int bagEvalLiteral(OUT CS* arg, Var* returnVar, EvalCtx *evalarg);
void bagExtend(Bag* d1, Bag* d2, CS action);
DictItem * bagLookup(EeSetItem* hi);
int bagEqual(Bag* d1, Bag* d2, int ic);
long bagCount(Bag* d, Var* needle, int ic);
void bagExtend_func(Var* argvars, CS arg_errmsg, int is_new, Var* returnVar);
void bagFilterMap(
   Bag* d,
   FilterMap filtermap,
   CS arg_errmsg,
   Var* expr,
   Var* returnVar
);
void bagRemove(Arr(Var) argvars, Var* returnVar, CS arg_errmsg);
void f_items(Arr(Var) argvars, Var* returnVar);
void f_keys(Arr(Var) argvars, Var* returnVar);
void f_values(Arr(Var) argvars, Var* returnVar);
void bagSetItemsRo(Bag* di);
void f_has_key(Arr(Var) argvars, Var* returnVar);
CS string_slice(CS str, Long first, Long last, int exclusive);
void bagUnref(Bag* b);
int dict_free_nonref(int copyID);
void hashtab_free_contents(EeSet* ht);
void dict_free_items(int copyID);
void dict_free_contents(Bag *d);
DictItem * dictitem_alloc(Text value);
void dictitem_remove(Bag* bag, DictItem* item, CS command);
void dictitem_free(DictItem *item);
Bag * dict_copy(Bag* orig, int deep, int top, int copyID);
int dictWrongFuncName(Bag* b, Var* tv, Text name);
int string2float(CS text, OUT double* value, Boole skip_quotes);
void f_abs(Arr(Var) argvars, Var* returnVar);
void f_acos(Arr(Var) argvars, Var* returnVar);
void f_asin(Arr(Var) argvars, Var* returnVar);
void f_atan(Arr(Var) argvars, Var* returnVar);
void f_atan2(Arr(Var) argvars, Var* returnVar);
void f_ceil(Arr(Var) argvars, Var* returnVar);
void f_cos(Arr(Var) argvars, Var* returnVar);
void f_cosh(Arr(Var) argvars, Var* returnVar);
void f_exp(Arr(Var) argvars, Var* returnVar);
void f_float2nr(Arr(Var) argvars, Var* returnVar);
void f_floor(Arr(Var) argvars, Var* returnVar);
void f_fmod(Arr(Var) argvars, Var* returnVar);
void f_isinf(Arr(Var) argvars, Var* returnVar);
void f_isnan(Arr(Var) argvars, Var* returnVar);
void f_log(Arr(Var) argvars, Var* returnVar);
void f_log10(Arr(Var) argvars, Var* returnVar);
void f_pow(Arr(Var) argvars, Var* returnVar);
void f_sqrt(Arr(Var) argvars, Var* returnVar);
void f_str2float(Arr(Var) argvars, Var* returnVar);
void f_tan(Arr(Var) argvars, Var* returnVar);
void f_tanh(Arr(Var) argvars, Var* returnVar);
void f_trunc(Arr(Var) argvars, Var* returnVar);
void f_round(Arr(Var) argvars, Var* returnVar);
void f_sin(Arr(Var) argvars, Var* returnVar);
void f_sinh(Arr(Var) argvars, Var* returnVar);
void f_assert_equal(Arr(Var) argvars, Var* returnVar);
void f_assert_equalfile(Arr(Var) argvars, Var* returnVar);
void f_assert_notequal(Arr(Var) argvars, Var* returnVar);
void f_assert_exception(Arr(Var) argvars, Var* returnVar);
void f_assert_fails(Arr(Var) argvars, Var* returnVar);
void f_assert_false(Arr(Var) argvars, Var* returnVar);
void f_assert_inrange(Arr(Var) argvars, Var* returnVar);
void f_assert_match(Arr(Var) argvars, Var* returnVar);
void f_assert_notmatch(Arr(Var) argvars, Var* returnVar);
void f_assert_report(Arr(Var) argvars, Var* returnVar);
void f_assert_true(Arr(Var) argvars, Var* returnVar);
void f_test_alloc_fail(Arr(Var) argvars, Var* returnVar UNUSED);
void f_test_autochdir(Arr(Var) argvars UNUSED, Var* returnVar UNUSED);
void f_test_feedinput(Arr(Var) argvars, Var* returnVar UNUSED);
void f_test_getvalue(Arr(Var) argvars, Var* returnVar);
void f_test_option_not_set(Arr(Var) argvars, Var* returnVar UNUSED);
void f_test_override(Arr(Var) argvars, Var* returnVar UNUSED);
void f_test_refcount(Arr(Var) argvars, Var* returnVar);
void f_test_garbagecollect_now(Arr(Var) argvars UNUSED, Var* returnVar UNUSED);
void f_test_garbagecollect_soon(Arr(Var) argvars UNUSED, Var* returnVar UNUSED);
void f_test_ignore_error(Arr(Var) argvars, Var* returnVar UNUSED);
void f_test_null_blob(Arr(Var) argvars UNUSED, Var* returnVar);
void f_test_null_channel(Arr(Var) argvars UNUSED, Var* returnVar);
void f_test_null_dict(Arr(Var) argvars UNUSED, Var* returnVar);
void f_test_null_job(Arr(Var) argvars UNUSED, Var* returnVar);
void f_test_null_list(Arr(Var) argvars UNUSED, Var* returnVar);
void f_test_null_function(Arr(Var) argvars UNUSED, Var* returnVar);
void f_test_null_partial(Arr(Var) argvars UNUSED, Var* returnVar);
void f_test_null_string(Arr(Var) argvars UNUSED, Var* returnVar);
void f_test_unknown(Arr(Var) argvars UNUSED, Var* returnVar);
void f_test_void(Arr(Var) argvars UNUSED, Var* returnVar);
void f_test_setmouse(Arr(Var) argvars, Var* returnVar UNUSED);
void f_test_settime(Arr(Var) argvars, Var* returnVar UNUSED);
int check_can_index(Var* var, int evaluate, int verbose);
int eval_index_inner(
   Var* returnVar,
   int is_range,
   Var* var1,
   Var* var2,
   int exclusive,
   CS key,
   int keylen,
   int verbose
);
Blob * blob_alloc(void);
int returnVar_blob_alloc(Var* returnVar);
void returnVar_blob_set(Var* returnVar, Blob *b);
int blob_copy(Blob *from, Var *to);
void blob_free(Blob *b);
void blob_unref(Blob *b);
long blob_len(Blob *b);
int blob_get(Blob *b, int idx);
void blob_set(Blob* blob, int idx, int byte);
void blob_set_append(Blob *blob, int idx, int byte);
int blob_equal(Blob   *b1, Blob   *b2);
CS blob2string(Blob *blob, Byte **tofree, Byte *numbuf);
Blob* string2blob(CS str);
int blob_slice_or_index(
   Blob      *blob,
   int      is_range,
   Long   n1,
   Long   n2,
   int      exclusive,
   Var   *returnVar)
;
int check_blob_index(long bloblen, Long n1, int quiet);
int check_blob_range(long bloblen, Long n1, Long n2, int quiet);
int blob_set_range(Blob *dest, long n1, long n2, Var *src);
void blob_add(Arr(Var) argvars, Var* returnVar);
void blob_remove(Arr(Var) argvars, Var* returnVar, CS arg_errmsg);
void blob_filter_map(
   Blob* blob_arg,
   FilterMap filtermap,
   Var* expr,
   CS arg_errmsg,
   Var* returnVar
);
void blob_insert_func(Arr(Var) argvars, Var* returnVar);
void blob_reduce(Var* argvars, Var* expr, Var* returnVar);
void blob_reverse(Blob* b, Var* returnVar);
void f_blob2list(Arr(Var) argvars, Var* returnVar);
void f_list2blob(Arr(Var) argvars, Var* returnVar);
void f_blob2str(Arr(Var) argvars, OUT Var* returnVar);
void f_str2blob(Arr(Var) argvars, OUT Var* returnVar);
void addFuzzyMatch(FuzzyMatch m, OUT Fuzzy* t);
int fuzzy_match(
   CS str,
   CS pat_arg,
   int matchseq,
   int* outScore,
   Arr(Unt) matches,
   int maxMatches
);
void f_matchfuzzy(Arr(Var) argvars, OUT Var* returnVar);
void f_matchfuzzypos(Arr(Var) argvars, OUT Var* returnVar);
void fuzzySortByScore(OUT Fuzzy* fuzzy);
int fuzzyMatchStr(CS str, CS pat);
ArrayList * fuzzyMatchStr_with_pos(CS str, CS pat);
CS find_word_end(CS ptr);
CS find_line_end(CS ptr);
int fuzzyMatchStr_in_line(
   Byte   **ptr,
   CS pat,
   int* len,
   Pos* current_pos,
   int* score)
;
int search_for_fuzzy_match(
   Book* book,
   Pos* pos,
   CS pattern,
   int dir,
   Pos* start_pos,
   OUT int* len,
   OUT CS* ptr,
   int* score
);
void fuzmatch_str_free(FuzzyMatch *fuzmatch, int count);
int defuzz(
   OUT ExpandMatch* matches,
   Fuzzy fuzzy,
   Boole funcsort
);
void string_filter_map(CS str, FilterMap filtermap, Var* expr, Var* returnVar);
void f_byteidx(Arr(Var) argvars, OUT Var* returnVar);
void f_byteidxcomp(Var* argvars, Var* returnVar);
void f_charidx(Var* argvars, Var* returnVar);
void f_str2list(Arr(Var) argvars, OUT Var* returnVar);
void f_str2nr(Var* argvars, Var* returnVar);
void f_strgetchar(Var* argvars, Var* returnVar);
void f_stridx(Var* argvars, Var* returnVar);
void f_string(Arr(Var) argvars, OUT Var* returnVar);
void f_strlen(Arr(Var) argvars, OUT Var* returnVar);
void f_strcharlen(Arr(Var) argvars, OUT Var* returnVar);
void f_strchars(Arr(Var) argvars, OUT Var* returnVar);
void f_strdisplaywidth(Arr(Var) argvars, OUT Var* returnVar);
void f_strwidth(Var* argvars, OUT Var* returnVar);
void f_strcharpart(Arr(Var) argvars, OUT Var* returnVar);
void f_strpart(Arr(Var) argvars, OUT Var* returnVar);
void f_strridx(Arr(Var) argvars, OUT Var* returnVar);
void f_strtrans(Arr(Var) argvars, OUT Var* returnVar);
void f_tolower(Arr(Var) argvars, OUT Var* returnVar);
void f_toupper(Arr(Var) argvars, OUT Var* returnVar);
void f_tr(Arr(Var) argvars, OUT Var* returnVar);
void f_trim(Arr(Var) argvars, OUT Var* returnVar);
int eeVarPrintf0(
   CS str,
   Unt str_m,
   char const* fmt,
   va_list ap_start,
   Var* tvs
);
void op_format(Operator* oper, int keep_cursor);
void op_formatexpr(Operator* oper);
int fex_format(LineNr lnum, long count, int c);
CS copyStr_shellescape(CS string, int do_special, int do_newline);
CS json_encode(Var* val, int options);
CS json_encode_nr_expr(int nr, Var* val, int options);
CS json_encode_lsp_msg(Var* val);
int json_decode(OUT Var* res, JsReader* reader);
int json_find_end(JsReader* reader);
void f_json_decode(Arr(Var) argvars, Var* returnVar);
void f_json_encode(Arr(Var) argvars, Var* returnVar);
