#ifndef AVOID_DUPLICATION
typedef struct RewindableArena RewindableArena;
declStruct(ChunkString);
#endif

Arena* createArena();
void* allocateOnArena(Unt allocSize, Arena* a);
void deleteArena(Arena* ar);
void arenaTryFree(void* start, Unt len, Arena* a);
Unt mb_ptr2char(Byte* p);
S str_foldcase(
	S str,
	Unt orglen,
	S buf,
	Unt bufLen
);
Unt nr2hex(Unt c);
void transchar_hex(S buf, int c);
S skipwhite(S q);
S skipSpace(S q);
S skipwhite_and_nl(S q);
int getwhitecols(S p);
S skipdigits(S q);
S skipbin(S q);
S skiphex(S q);
S skiptobin(S q);
S skiptodigit(S q);
S skiptohex(S q);
int eeIsDigit(int c);
int eeIsXDigit(Unt c);
Boole eeIsLower(Unt c);
int eeIsUpper(Unt c);
int eeglIsAlfa(int c);
int eeglToUpper(Unt c);
int eeglToLower(Unt c);
S skiptowhite(S p);
S skiptowhite_esc(S p);
S skipToLastSpace(S p);
long parseLong(OUT S* pp);
long parseLong_quoted(OUT S* pp);
int eeIsBlankLine(S lbuf);
void readLongNumber(
	S start,
	OUT int* prep,    //type of number 0 = decimal, 'x' or 'X' is hex, 'b' or 'B' is bin
	OUT int* len,     //detected length of number
	int what,     //what numbers to recognize
	OUT Long* nptr, //signed result
	OUT Ulong* unptr,  //unsigned result
	int maxlen,   //max length of string to check
	Boole strict,   //check strictly
	Boole* overflow  //when not NULL set to true for overflow
);
int hex2nr(int c);
int hexhex2nr(S p);
int rem_backslash(S filename);
void backslash_halve(S p);
S backslash_halve_save(S p);
void transchar_nonprint(S charbuf, int c);
Unt utf_ptr2len(S p);
Unt utf_byte2len(int b);
Unt utf_byte2len_zero(int b);
Boole utfNeedTruncate(S p, int size);
Unt utf_ptr2len_len(Byte const* p, int size);
Unt mb_char2len(Unt c);
int mb_char2bytes(Unt c, S buf);
int utf_iscomposing_uint(Unt c);
Boole utf_islower(Unt a);
Unt utf_tolower(Unt a);
Boole utf_isupper(Unt a);
Unt utf_fold(Unt a);
Unt utf_toupper(Unt a);
int caseInsensitiveCompareNChars2(S s1, S s2, Unt n1, Unt n2);
int caseInsensitiveCompareNChars(S s1, S s2, Unt nn);
Boole strInEmojiTable(Unt c);
Boole strInDoubleWidthTable(Unt c);
Unt strAdvanceMultibyte(OUT S* pp);
Unt mb_cptr2char_adv(OUT S* pp);
int utf_ambiguous_width(Unt c);
int mb_head_off(S base, S p);
int utf_eat_space(int cc);
Boole utf_allow_break_before(Unt cc);
int utf_allow_break(Unt cc, Unt ncc);
void mb_copy_char(OUT S* fp, OUT S* tp);
int mb_off_next(S base, S p);
int mb_tail_off(S base, S p);
int utf_valid_string(S s, S end);
S mb_prevptr(S line, S p);
int mb_charlen(S str);
int mb_charlen_len(S str, int len);
S mb_unescape(OUT S* pp);
Boole utf_iscomposing(Unt c);
Boole utf_printable(Unt c);
void appendSubDir(S subDir, OUT DirName* dn);
Boole removeSubDir(OUT DirName* restrict dn);
ChunkyString* createChunkyString(Arr(Byte const) c, Arena* a);
void appendToChunkyString(Arr(Byte const) c, ChunkyString* chunky);
S toStringChunky(ChunkyString* chunky);
Unt utfCharLen(S p);
Unt utfCharLen_len(Byte* p, int size);
Text mbText(NULLABLE S b);
Text text(S b);
Boole eq_Text_Text(Text a, Text b);
Boole eq_Text_CString(Text a, S b);
Boole eq_CString_CString(S a, S b);
Text skipTo(Text inp, S t);
S skipToComma(S s);
S skip_to_option_part(S p);
S skipLine(S s);
S copyStr(S string);
S copySubstr(S string, Unt len);
Polystring polystring(Unt cap);
void appendToPoly(Text s, OUT Polystring* buf);
void appendToMulti(Text s, OUT Multistring* mu);
void appendNullToMulti(OUT Multistring* mu);
void freeMultistring(OUT Multistring* mu);
void freePolystring(OUT Polystring* poly);
S copySubstrA(S string, Unt len, Arena* a);
Text copyText(Text slice);
S copyStrA(S string, Arena* a);
S copyStr_escaped(S string, S escChars);
S copyStrEscapedA(S string, S escChars, Arena* a);
S copyStr_escaped_ext(S string, S esc_chars, Unt cc, Boole bsl, Arena* a);
Arr(S) splitByCharIntoArray(S inp, Byte delimiter, OUT Unt* len);
ArrayList splitBySpace(S inp);
Text tokenizeSeparator(OUT S* inp, Byte separator);
S copyStr_up(S string);
S copySubstr_up(S string, Unt len);
int isSpace(int x);
void asciiToUpper(S p);
S strup_save(S orig);
Text skipQuoted(Text s);
Text skipSingleQuoted(Text s);
S strlow_save(S orig);
void del_trailing_spaces(S ptr);
void copySubstrToAllocation(OUT S to, Text from);
void concatenateStrings(S to, S from, Unt tosize);
int compareAscii(S s1, S s2, Unt len);
S firstOccurrence(S string, Unt c);
S firstByteOccurrence(S string, Byte b);
S eeStrbyte(S string, Unt c);
S lastOccurrence(S string, int c);
S eeStrpbrk(S s, S charset);
void sortStrings(Arr(S) files, int count);
int has_non_ascii(S s);
S concat_str(S str0, S str1);
S reverse_text(S s);
S string_quote(S str, int function);
long string_count(S haystack, S needle, int ic);
int eeSnprintfAdd0(S str, Unt str_m, const char *fmt, ...);
int eeSnprintf0(S str, Unt str_m, const char *fmt, ...);
Unt eeSnprintfSafelen0(S str, Unt str_m, const char *fmt, ...);
long char_idx2byte(S str, Unt str_len, Long idx);
S concatStrArray(Arr(S) arr, Unt len, Text separator, Arena* a);
S fileExtension(Text fName);
Boole startsWith(Text haystack, Text needle);
S encodeBase64(void const* binaryData_, int inputLen);
Boole decodeBase64ToArrayList(OUT ArrayList* ret, Text base64);
Boole decodeBase64(OUT S* ret, Text base64);
S remove_tail(S p, S pend, S name);
Boole after_pathsep(S fileName, S afterSep);
Boole path_is_url(S p);
Boole strStartsWithUrl(S fname);
void shorten_dir_len(S str, int trim_len);
void shorten_dir(S str);
void strPrintShortName(S src, S dst, int dstlen);
S fiGetShortFiName(S fname);
S gettail_sep(S fname);
S getnextcomp(S fname);
S skipInitialSlashes(S path);
Boole strIsRelative(S fname);
int eeIsAbsName(S name);
int pathcmp(S p, S q, int maxlen);
Boole hasEnvVar(S p);
int strCutPathFromListOfPaths(OUT S* option, OUT S buf, int maxlen, S sep_chars);
Boole strMatchLowPrioSuffix(S fname, S suffixes);
void add_pathsep(S p);
S concat_fnames(S fname1, S fname2, Boole sep);
Boole isComment(S c);
int endsComm(S c);
int valid_name(S val, S allowed);
int isValidForScriptName(int c);
int isValidForScriptName1(int c);
int isValidForFirstCharDictKey(int c);
int parse_hex_digit(int c);
void ga_clear(ArrayList* gap);
void ga_clear_strings(ArrayList* gap);
int ga_copy_strings(ArrayList *from, ArrayList *to);
void ga_init(ArrayList* gap);
void ga_init2(ArrayList *gap, Unt itemsize, int growsize);
int ga_grow(ArrayList *gap, int n);
int ga_grow_inner(ArrayList* gap, int n);
S ga_concat_strings(ArrayList *gap, char *sep);
int ga_copy_string(ArrayList *gap, S p);
int ga_add_string(ArrayList *gap, S p);
void ga_concat(ArrayList *gap, S s);
void ga_concat_len(ArrayList *gap, S s, Unt len);
int ga_append(ArrayList *gap, int c);
void * mergesort_list(
	void *head,
	void *(*get_next)(void *),
	void (*set_next)(void *, void *),
	void *(*get_prev)(void *),
	void (*set_prev)(void *, void *),
	int (*compare)(const void *, const void *)
);
void sha256_start(ContextSha256* ctx);
void sha256_update(ContextSha256 *ctx, S input, Unt length);
void sha256_finish(ContextSha256 *ctx, Byte digest[32]);
S sha256_bytes(S buf, int buf_len, S salt, int salt_len);
S sha256_key(S buf, S salt, int salt_len);
int sha256_self_test(void);
void sha2_seed(S header, int header_len, S salt, int salt_len);
int binarySearch_Unt(Unt key, int start, int end, Arr(Unt) arr);
int cmp_keyvalue_value(const void *a, const void *b);
int cmp_keyvalue_value_n(const void *a, const void *b);
int cmp_keyvalue_value_i(const void *a, const void *b);
int cmp_keyvalue_value_ni(const void *a, const void *b);
DictStringInt128* dictStringInt128New(Arr(Byte const) text, Arr(Int) values, Int size, Arena* a);
DictStringInt128* dictStringInt128NewJustIndices(Arr(Byte const) text, Int size, Arena* a);
Boole containsKey_DictStringInt128(Arr(Byte const) needle, DictStringInt128* restrict haystack);
Int get_DictStringInt128(Arr(Byte const) needle, DictStringInt128* restrict haystack);
Int get_Text_DictStringInt128(Text needle, DictStringInt128* restrict haystack);
Int getOrDefault_DictStringInt128( 	Arr(Byte const) needle, Int defaultValue, DictStringInt128* restrict haystack
);
Int getOrDefault_Text_DictStringInt128( 	Text needle, Int defaultValue, DictStringInt128* restrict haystack
);
Int getKv_Text_DictStringInt128( 	OUT Unt* key, Text needle, DictStringInt128* restrict haystack
);
RewindableArena* createRewindableArena();
void* allocateOnRewindableArena(Unt allocSize, RewindableArena* a);
void deleteRewindableArena(RewindableArena* a);
void setRewindPoint(OUT RewindableArena* a);
void rewindArena(OUT RewindableArena* a);
