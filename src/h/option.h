void optSetStringDefault(S name, S val);
void optFreeAllOptions(void);
void optInit1(void);
S optSetByName(S name, OptionValue newVal, SetScope setScope);
Unt stringToChar(S arg, Boole multi_byte);
ScriptPos* optGetScriptPos(S name);
OptionValue optGetValue(OUT Unt* flagsp, S name, int scope);
Boole optIsFnOption(Unt flags);
void optSetFromVar(S varname, Var *varp);
int optExpandForSet(Expand* xp, RegMatch* regmatch, OUT ExpandMatch* matches);
Boole optWasSet(S name);
int makefoldset(FILE *fd);
S optSetBinary(OptionChange* cha);
S setModifiable(OptionChange* cha);
void optChangeAndReportError(
	S name,
	OptionValue newValue,
	SetScope setScope
);
void afterCopyPortOpt(Portal* po);
void copyPortOpt(PortalOptions* t, PortalOptions* s);
void optClearPortOptions(PortalOptions* t);
void optInitExpandContextForSet(
	OUT Expand* xp,
	S arg,
	SetScope setScope
);
int optExpandOldOption(OUT ExpandMatch* matches);
int reset_optWasSet(S name);
long get_sidescrolloff_value(void);
int optSetCallback(OUT Callback* cb, S new);
S setShowTabpanel(OptionChange* cha);
void initPortalOptions(PortalOptions* o);
void updateStringRef(OptionChange* cha);
void optInit0();
int writeOptionsAsSet(FILE *);
void optSetLocalOptionsToDefault(Portal *wp, Boole doBook);
void c_get(Invocation* invo);
void c_set(Invocation* invo);
Bag* getBookOrPortOptions(Boole bufopt);
void optCopyBetweenPortals(OUT Portal* to, Portal* from);
void optCopyGlobalToPortal(OUT Portal* to);
void optFreeBookCallbacks(Book* book);
void optsCopyToBook(OUT Book* book, Unt flags);
int optExpandOption(
	Expand* xp,
	RegMatch* regmatch,
	S fuzzystr,
	Boole canFuzzy,
	OUT ExpandMatch* matches
);
Boole optImmutableMode();
void optChangeStringOptionDirect(
	S name,
	S val,
	SetScope scope,
	ScriptId set_sid
);
void optSetStringOptionDirectInPort(
	OUT Portal* po,
	S name,
	S val,
	Unt optFlags,
	int set_sid
);
void optSetStringOptionDirectInBook(
	Book* book,
	S name,
	S val,
	Unt optFlags,
	int set_sid
);
S get_mess_lang(void);
void init_locale(void);
void c_language(Invocation* invo);
void free_locales(void);
S get_lang_arg(Expand*, int idx);
S get_locales(Expand*, int idx);
