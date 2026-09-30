#define FSK_KEYCODE     0x01   //prefer key code, e.g. K_DEL instead of DEL
#define FSK_KEEP_X_KEY  0x02   //don't translate xHome to Home key
#define FSK_IN_STRING   0x04   //true in string, double quote is escaped
#define FSK_SIMPLIFY    0x08   //simplify <C-H> and <A-x>
#define FSK_FROM_PART   0x10   //left-hand-side of mapping
extern char *UP, *BC, PC;
char *ptsname(int);
int grantpt(int);
