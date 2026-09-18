#define HI_HAS_FG    1
#define HI_HAS_BG    2
#define HI_HAS_UNDER 4
#define HI_IS_LINK   8
#define HLF_NONE     0 //No decorations
#define HLF_NONTEXT  1 //Non-text
#define HLF_FLOAT    2 //Normal float
#define HLF_AT       3 // characters at end of screen, characters that don't really exist in the text
#define HLF_D        4 //directories in CTRL-D listing
#define HLF_E        5 //error messages
#define HLF_W        6 //warning messages
#define HLF_M        7 //"--More--" message
#define HLF_CM       8 //Mode (e.g., "-- INSERT --")
#define HLF_CLN      9 //current line number
#define HLF_CLS     10 //current line sign column
#define HLF_CLF     11 //current line fold
#define HLF_R       12 //return to continue message and yes/no questions
#define HLF_S       13 //status lines
#define HLF_SNC     14 //status lines of not-current portals
#define HLF_C       15 //column to separate vertically split windows
#define HLF_T       16 //Titles for output from ":set all", ":autocmd" etc.
#define HLF_V       17 //Visual mode
#define HLF_VNC     18 //Visual mode, autoselecting and not clipboard owner
#define HLF_WM      19 //Wildmenu highlight
#define HLF_FL      20 //Folded line
#define HLF_ADD     21 //Added diff line
#define HLF_CHD     22 //Changed diff line
#define HLF_TXD     23 //Text Changed in changed diff line
#define HLF_TXA     24 //Text Added in changed diff line
#define HLF_DED     25 //Deleted diff line
#define HLF_SC      26 //Sign column
#define HLF_PNI     27 //popup menu normal item
#define HLF_PSI     28 //popup menu selected item
#define HLF_PMNI    29 //popup menu matched text in normal item
#define HLF_PMSI    30 //popup menu matched text in selected item
#define HLF_PNK     31 //popup menu normal item "kind"
#define HLF_PSK     32 //popup menu selected item "kind"
#define HLF_PNX     33 //popup menu normal item "menu" (extra text)
#define HLF_PSX     34 //popup menu selected item "menu" (extra text)
#define HLF_PSB     35 //popup menu scrollbar
#define HLF_PST     36 //popup menu scrollbar thumb
#define HLF_TPL     37 //tabpanel
#define HLF_TPLS    38 //tabpanel selected
#define HLF_TPLF    39 //tabpanel filler
#define HLF_QFL     40 //location portal line currently selected
#define HLF_ST      41 //status lines of terminal windows
#define HLF_STNC    42 //status lines of not-current terminal portals
#define HLF_TERMR   43 //status lines of not-current terminal portals
#define HLF_TERMG   44 //status lines of not-current terminal portals
#define HLF_TERMB   45 //status lines of not-current terminal portals
#define HLF_MSG     46 //message area
#define HLF_8       47 //Meta & special keys listed with ":map", text that is displayed different
#define HLF_N       48 //line number for ":number" and ":#" commands
#define HLF_LNA     49 //LineNrAbove
#define HLF_LNB     50 //LineNrBelow
