#define UPD_VALID_NO_UPDATE 5  // no new changes, keep the command line if possible
#define UPD_VALID          10  // book not changed, or changes marked with b_mod_*
#define UPD_INVERTED       20  // redisplay inverted part that changed
#define UPD_INVERTED_ALL   25  // redisplay whole inverted part
#define UPD_REDRAW_TOP     30  // display first w_upd_rows screen lines
#define UPD_SOME_VALID     35  // like UPD_NOT_VALID but may scroll
#define UPD_NOT_VALID      40  // book needs complete redraw
#define UPD_CLEAR          50  // screen messed up, clear it
#define SLF_RIGHTLEFT  1
#define SLF_INC_VCOL   4
