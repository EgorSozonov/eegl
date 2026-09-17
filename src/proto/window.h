YankReg * get_y_regs(void);
YankReg * get_y_current(void);
YankReg * get_y_previous(void);
void set_y_current(YankReg *yreg);
void set_y_previous(YankReg *yreg);
void reset_y_append(void);
int get_expr_register(void);
void set_expr_line(CS new_line, Invocation* invo);
CS get_expr_line(void);
int valid_yank_reg(int regname, Boole writing);
int get_yank_register(int regname, int writing);
void * get_register(Unt name, int copy);
void put_register(int name, void *reg);
void free_register(void *reg);
int yank_register_mline(int regname);
int do_record(int c);
int get_execreg_lastc(void);
void set_execreg_lastc(int lastc);
int do_execreg(
    int       regname,
    int       colon,      // insert ':' before each line
    int       addcr,      // always add '\n' to end of line
    int       silent)      // set "silent" flag in typeahead buffer
;
int insert_reg(Unt regname, int literally_arg);
int get_spec_reg(
   int regname,
   OUT CS* retVal,
   int* allocated,   // return: true when value was allocated
   int errmsg      // give error message when failing
);
int cmdline_paste_reg(
   int regname,
   int literally_arg,   // Insert text literally instead of "as typed"
   int remcr      // don't add CR characters
);
void shift_delete_registers(void);
void yank_do_autocmd(Operator* opArg, YankReg *reg);
void init_yank(void);
void clear_registers(void);
void free_yank_all(void);
int op_yank(Operator *opArg, int deleting, Boole mess);
void do_put(
   int      regname,
   CS expr_result,   // result for regname "=" when compiled
   Unt dir,      // BACKWARD for 'P', FORWARD for 'p'
   long   count,
   Unt      flags
);
int get_register_name(int num);
int get_unname_register(void);
void c_display(Invocation* invo);
void dnd_yank_drag_data(CS str, long len);
Byte get_reg_type(int regname, long *reglen);
CS get_reg_contents(int regname, int flags);
void write_reg_contents(
   int      name,
   CS str,
   int maxlen,
   int must_append)
;
void write_reg_contents_lst(
   int name,
   Byte** strings,
   int,
   int must_append,
   Unt yank_type,
   long block_len
);
void write_reg_contents_ex(
   int name,
   CS str,
   int maxlen,
   int must_append,
   Unt yank_type,
   long block_len
);
int may_get_selection(Unt regname);
void clipGetDefaultRegister(OUT int* rp);
