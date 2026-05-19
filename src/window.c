//EEGL - the Extensible development Environment for GNU/Linux
//Licensed under GPLv3, see the LICENSE file (c) Egor Sozonov

//## window.c: functions for displaying the window in X11 or in Wayland

#include "eegl.h"
// for shm_open:
#include <sys/mman.h>
#include <fcntl.h>
int fstat(int fd, struct stat* statbuf); //from sys/stat.h
int stat(const char* restrict path, struct stat* restrict buf);

//{{{X11

#ifdef FEAT_X11

#include <X11/Intrinsic.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/Intrinsic.h>
#include <X11/Shell.h>
#include <X11/StringDefs.h>
private Widget xterm_Shell = (Widget)0;

//This file provides procedures that implement the command server
//functionality of Eegl when in contact with an X11 server.
//
//Adapted from TCL/TK's send command  in tkSend.c of the tk 3.6 distribution.
//Adapted for use in Eegl by Flemming Madsen. Protocol changed to that of tk 4

//Copyright (c) 1989-1993 The Regents of the University of California.
//All rights reserved.
//
//Permission is hereby granted, without written agreement and without
//license or royalty fees, to use, copy, modify, and distribute this
//software and its documentation for any purpose, provided that the
//above copyright notice and the following two paragraphs appear in
//all copies of this software.
//
//IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO ANY PARTY FOR
//DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES ARISING OUT
//OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF
//CALIFORNIA HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
//THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY WARRANTIES,
//INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
//AND FITNESS FOR A PARTICULAR PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS
//ON AN "AS IS" BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
//PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.


//When a result is being awaited from a sent command, one of the following structures is present 
//on a list of all outstanding sent commands.  The information in the structure is used to
//process the result when it arrives. You're probably wondering how there could ever be multiple 
//outstanding sent commands. This could happen if Eegl instances invoke each other recursively.
//It's unlikely, but possible.

typedef struct PendingCommand {
   int       serial;   // Serial number expected in result.
   int       code;   // Result Code. 0 is OK
   Byte  *result;   // String result for command (malloc'ed). NULL means command still pending.
   struct PendingCommand *nextPtr;
         // Next in list of all outstanding commands. NULL means end of list.
} PendingCommand;

private PendingCommand *pendingCommands = NULL; // List of all commands currently being waited for.

//The information below is used for communication between processes
//during "send" commands.  Each process keeps a private window, never
//even mapped, with one property, "Comm".  When a command is sent to
//an interpreter, the command is appended to the comm property of the
//communication window associated with the interp's process.  Similarly,
//when a result is returned from a sent command, it is also appended
//to the comm property.
//
//Each command and each result takes the form of ASCII text.  For a
//command, the text consists of a ZERO character followed by several
//ZERO-terminated ASCII strings.  The first string consists of a
//single letter:
//"c" for an expression
//"k" for keystrokes
//"r" for reply
//"n" for notification.
//Subsequent strings have the form "option value" where the following options
//are supported:
//
//-r commWindow serial
//
//  This option means that a response should be sent to the window
//  whose X identifier is "commWindow" (in hex), and the response should
//  be identified with the serial number given by "serial" (in decimal).
//  If this option isn't specified then the send is asynchronous and
//  no response is sent.
//
//-n name
//  "Name" gives the name of the application for which the command is
//  intended.  This option must be present.
//
//-E encoding
//  Encoding name used for the text.  This is the 'encoding' of the
//  sender.  The receiver may want to do conversion to his 'encoding'.
//
//-s script
//  "Script" is the script to be executed.  This option must be
//  present.  Taken as a series of keystrokes in a "k" command where
//  <Key>'s are expanded
//
//The options may appear in any order.  The -n and -s options must be
//present, but -r may be omitted for asynchronous RPCs.  For compatibility
//with future releases that may add new features, there may be additional
//options present;  as long as they start with a "-" character, they will
//be ignored.
//
//A result also consists of a zero character followed by several null-
//terminated ASCII strings.  The first string consists of the single
//letter "r".  Subsequent strings have the form "option value" where
//the following options are supported:
//
//-s serial
//  Identifies the command for which this is the result.  It is the
//  same as the "serial" field from the -s option in the command.  This
//  option must be present.
//
//-r result
//  "Result" is the result string for the script, which may be either
//  a result or an error message.  If this field is omitted then it
//  defaults to an empty string.
//
//-c code
//  0: for OK. This is the default.
//  1: for error: Result is the last error
//
//-i errorInfo
//-e errorCode
//  Not applicable for Eegl
//
//Options may appear in any order, and only the -s option must be
//present.  As with commands, there may be additional options besides
//these;  unknown options are ignored.

//Maximum size property that can be read at one time by this module:

#define MAX_PROP_WORDS 100000

struct ServerReply {
   Window  id;
   ArrayList strings;
};
private ArrayList serverReply = { 0, 0, 0, 0, 0 };
enum ServerReplyOp { SROP_Find, SROP_Add, SROP_Delete };

typedef int (*EndCond)(void *);

struct x_cmdqueue {
   Byte      *propInfo;
   Ulong      len;
   struct x_cmdqueue   *next;
   struct x_cmdqueue   *prev;
};

typedef struct x_cmdqueue x_queue_T;

// dummy node, header for circular queue
private x_queue_T head = {NULL, 0, NULL, NULL};

//Forward declarations for procedures defined later in this file:

private Window   lookupName(Display *dpy, CS name, int delete, Byte **loose);
private int   SendInit(Display *dpy);
private int   DoRegisterName(Display *dpy, CS name);
private void   DeleteAnyLingerer(Display *dpy, Window w);
private int   GetRegProp(Display *dpy, Byte **regPropp, Ulong *numItemsp, int domsg);
private int   WaitForPend(void *p);
private int   WindowValid(Display *dpy, Window w);
private void   ServerWait(Display *dpy, Window w, EndCond endCond, void *endData, int localLoop, int seconds);
private int   AppendPropCarefully(Display *display, Window window, Atom property, CS value, int length);
private int   x_error_check(Display *dpy, XErrorEvent *error_event);
private int   IsSerialName(CS name);
private void   save_in_queue(CS buf, Ulong len);
private void   server_parse_message(Display *dpy, CS propInfo, Ulong numItems);

// Private variables for the "server" functionality
private Atom   registryProperty = None;
private Atom   eeglProperty = None;
private int   got_x_error = FALSE;

private Byte   *empty_prop = (CS)"";   // empty GetRegProp() result

//Associate an ASCII name with Eegl. Try real hard to get a unique one. Return FAIL or OK.
int
serverRegisterName(
   Display   *dpy,      // display to register with
   Byte   *name)      // the name that will be used as a base
{
   Byte   *p = NULL;

   int res = DoRegisterName(dpy, name);
   if (res >= 0)
      return OK;

   int i = 1;
   do {
      if (res < -1 || i >= 1000) {
         msgDeco(_("Unable to register a command server name"), getDecoFlags(HLF_W));
         return FAIL;
      }
      if (!p)
         p = alloc(STRLEN(name) + 10);
      sprintf((char *)p, "%s%d", name, i++);
      res = DoRegisterName(dpy, p);
   } while (res < 0);
   eeglFree(p);

   return OK;
}

private int
DoRegisterName(Display *dpy, CS name) {
   Window   w;
   XErrorHandler old_handler;
#define MAX_NAME_LENGTH 100
   Byte   propInfo[MAX_NAME_LENGTH + 20];

   if (commProperty == None && SendInit(dpy) < 0)
      return -2;

   //Make sure the name is unique, and append info about it to
   //the registry property.  It's important to lock the server
   //here to prevent conflicting changes to the registry property.
   //WARNING: Do not step through this while debugging, it will hangup the X server!
   XGrabServer(dpy);
   w = lookupName(dpy, name, FALSE, NULL);
   if (w != (Window)0) {
      Status      status;
      int      dummyInt;
      unsigned int   dummyUns;
      Window      dummyWin;

      //The name is currently registered. See if the commPortal associated with the name exists. 
      //If not, or if the commPortal is *our* commWindow, then just unregister the old name (this
      //could happen if an application dies without cleaning up the registry).
      old_handler = XSetErrorHandler(x_error_check);
      status = XGetGeometry(dpy, w, &dummyWin, &dummyInt, &dummyInt,
                 &dummyUns, &dummyUns, &dummyUns, &dummyUns);
      (void)XSetErrorHandler(old_handler);
      if (status != Success && w != commWindow) {
         XUngrabServer(dpy);
         XFlush(dpy);
         return -1;
      }
      (void)lookupName(dpy, name, /*delete=*/TRUE, NULL);
   }
   sprintf((char *)propInfo, "%x %.*s", (Unt)commWindow, MAX_NAME_LENGTH, name);
   old_handler = XSetErrorHandler(x_error_check);
   got_x_error = FALSE;
   XChangeProperty(dpy, RootWindow(dpy, 0), registryProperty, XA_STRING, 8,
          PropModeAppend, propInfo, STRLEN(propInfo) + 1);
   XUngrabServer(dpy);
   XSync(dpy, False);
   (void)XSetErrorHandler(old_handler);

   if (!got_x_error) {
      set_EeglVar_string(VV_SEND_SERVER, name, -1);
      serverName = copyStr(name);
      return 0;
   }
   return -2;
}

//Send to an instance of Eegl via the X display. Return 0 for OK, negative for an error.
int
serverSendToEegl(
   Display   *dpy,         // Where to send.
   Byte   *name,         // Where to send.
   Byte   *cmd,         // What to send.
   Byte   **result,      // Result of eval'ed expression
   Window   *server,      // Actual ID of receiving app
   Boole   asExpr,         // Interpret as keystrokes or expr ?
   int      timeout,      // seconds to wait or zero
   Boole   localLoop,      // Throw away everything but result
   int      silent)         // don't complain about no server
{
   Window       w;
   Byte       *property;
   int          length;
   int          res;
   static int       serial = 0;   // Running count of sent commands.
            // Used to give each command a different serial number.
   PendingCommand  pending;
   Byte       *loosename = NULL;

   if (result)
      *result = NULL;
   if (name == NULL || *name == ZERO)
      name = (CS)"EEGL";    // use a default name

   if (commProperty == None && dpy != NULL && SendInit(dpy) < 0)
      return -1;

   lo("serverSendToEegl(%s, %s)", name, cmd);

   // Execute locally if no display or target is ourselves
   if (dpy == NULL || (serverName != NULL && caseInsensitiveCompare(name, serverName) == 0))
      return sendToLocalEm(cmd, asExpr, result);

   //Bind the server name to a communication window.
   //
   //Find any survivor with a serialno attached to the name if the
   //original registrant of the wanted name is no longer present.
   //
   //Delete any lingering names from dead editors.
   while (TRUE) {
      w = lookupName(dpy, name, FALSE, &loosename);
      // Check that the window is hot
      if (w != None) {
         if (!WindowValid(dpy, w)) {
            lookupName(dpy, loosename ? loosename : name, /*DELETE=*/TRUE, NULL);
            eeglFree(loosename);
            continue;
         }
      }
      break;
    }
   if (w == None) {
      if (!silent)
          showErrFmtMsg(_(e_no_registered_server_named_str), name);
      return -1;
   } ei (loosename != NULL)
      name = loosename;
   if (server)
      *server = w;

   //Send the command to target interpreter by appending it to the comm portal in the 
   //communication portal. Length must be computed exactly!
   length = STRLEN(name) + STRLEN(cmd) + 14;
   property = alloc(length + 30);

   sprintf((char *)property, "%c%c%c-n %s%c-E %c-s %s",
            0, asExpr ? 'c' : 'k', 0, name, 0, 0, cmd);
   if (name == loosename)
      eeglFree(loosename);
   // Add a back reference to our comm window
   serial++;
   sprintf((char *)property + length, "%c-r %x %d", 0, (Unt)commWindow, serial);
   // Add length of what "-r %x %d" resulted in, skipping the ZERO.
   length += STRLEN(property + length + 1) + 1;

   res = AppendPropCarefully(dpy, w, commProperty, property, length + 1);
   eeglFree(property);
   if (res < 0) {
      emsg(_(e_failed_to_send_command_to_destination_program));
      return -1;
   }

   if (!asExpr) // There is no answer for this - Keys are sent async
      return 0;

   //Register the fact that we're waiting for a command to complete (this is needed by 
   //SendEventProc and by AppendErrorProc to pass back the command's results).
   pending.serial = serial;
   pending.code = 0;
   pending.result = NULL;
   pending.nextPtr = pendingCommands;
   pendingCommands = &pending;

   ServerWait(dpy, w, WaitForPend, &pending, localLoop, timeout > 0 ? timeout : 600);

   // Unregister the information about the pending command and return the result.
   if (pendingCommands == &pending)
      pendingCommands = pending.nextPtr;
   else {
      PendingCommand *pcPtr;

      for (pcPtr = pendingCommands; pcPtr != NULL; pcPtr = pcPtr->nextPtr)
         if (pcPtr->nextPtr == &pending) {
            pcPtr->nextPtr = pending.nextPtr;
            break;
         }
   }

   lo("serverSendToEegl() result: %s", pending.result == NULL ? "NULL" : (char *)pending.result);
   if (result)
      *result = pending.result;
   else
      eeglFree(pending.result);

   return pending.code == 0 ? 0 : -1;
}

private int
WaitForPend(void *p) {
   PendingCommand *pending = (PendingCommand *) p;
   return pending->result != NULL;
}

//Return TRUE if window "w" exists and has a "Eegl" property on it.
private int
WindowValid(Display *dpy, Window w) {
   XErrorHandler   old_handler;
   Atom       *plist;
   int          numProp;
   int          i;

   old_handler = XSetErrorHandler(x_error_check);
   got_x_error = 0;
   plist = XListProperties(dpy, w, &numProp);
   XSync(dpy, False);
   XSetErrorHandler(old_handler);
   if (plist == NULL || got_x_error)
      return FALSE;

   for (i = 0; i < numProp; i++) {
      if (plist[i] == eeglProperty) {
          XFree(plist);
          return TRUE;
      }
   } 
   XFree(plist);
   return FALSE;
}

// Enter a loop processing X events & polling chars until we see a result
private void
ServerWait(
   Display   *dpy,
   Window   w,
   EndCond   endCond,
   void   *endData,
   int      localLoop,
   int      seconds)
{
   Tyme       start;
   Tyme       now;
   XEvent       event;

#define UI_MSEC_DELAY 53
#define SEND_MSEC_POLL 500
   fd_set       fds;
   FD_ZERO(&fds);
   FD_SET(ConnectionNumber(dpy), &fds);

   time(&start);
   while (TRUE) {
      while (XCheckWindowEvent(dpy, commWindow, PropertyChangeMask, &event))
          serverEventProc(dpy, &event, 1);
      server_parse_messages();

      if (endCond(endData) != 0)
          break;
      if (!WindowValid(dpy, w))
          break;
      time(&now);
      if (seconds >= 0 && (now - start) >= seconds)
          break;

      check_due_timer();

      // Just look out for the answer without calling back into Eegl
      if (localLoop) {
         TimeVal  tv;

         // Set the time every call, select() may change it to the remaining time.
         tv.tv_sec = 0;
         tv.tv_usec =  SEND_MSEC_POLL * 1000;
         if (select(FD_SETSIZE, &fds, NULL, NULL, &tv) < 0)
            break;
      } else {
         if (gotInterruptG)
            break;
         ui_delay((long)UI_MSEC_DELAY, TRUE);
         ui_breakcheck();
      }
   }
}


//Fetch a list of all the Eegl instance names currently registered for the display.
//
//Return a newline separated list in allocated memory or NULL.
CS
serverGetEeglNames(Display *dpy) {
   Byte   *regProp;
   Byte   *entry;
   Ulong   numItems;
   Unt   w;
   ArrayList   ga;

   if (registryProperty == None && SendInit(dpy) < 0)
      return NULL;

   //Read the registry property.
   if (GetRegProp(dpy, &regProp, &numItems, TRUE) == FAIL)
      return NULL;

   //Scan all of the names out of the property.
   ga_init2(&ga, 1, 100);
   for (CS p = regProp; (Ulong)(p - regProp) < numItems; p++) {
      entry = p;
      while (*p != 0 && !SAFE_isspace(*p))
         p++;
      if (*p != 0) {
         w = None;
         sscanf((char *)entry, "%x", &w);
         if (WindowValid(dpy, (Window)w)) {
            ga_concat(&ga, p + 1);
            ga_concat(&ga, (CS)"\n");
         }
         while (*p != 0)
            p++;
      }
   }
   if (regProp != empty_prop)
      XFree(regProp);
   ga_append(&ga, ZERO);
   return ga.c;
}

/////////////////////////////////////////////////////////////
// Reply stuff

private struct ServerReply *
ServerReplyFind(Window w, enum ServerReplyOp op) {
   struct ServerReply *p;
   struct ServerReply e;
   int      i;

   p = (struct ServerReply *) serverReply.c;
   for (i = 0; i < serverReply.len; i++, p++) {
      if (p->id == w)
          break;
   } 
   if (i >= serverReply.len)
      p = NULL;

   if (p == NULL && op == SROP_Add) {
      if (serverReply.ga_growsize == 0)
          ga_init2(&serverReply, sizeof(struct ServerReply), 1);
      if (ga_grow(&serverReply, 1) == OK) {
          p = ((struct ServerReply *) serverReply.c) + serverReply.len;
          e.id = w;
          ga_init2(&e.strings, 1, 100);
          mch_memmove(p, &e, sizeof(e));
          serverReply.len++;
      }
   } ei (p != NULL && op == SROP_Delete) {
      ga_clear(&p->strings);
      mch_memmove(p, p + 1, (serverReply.len - i - 1) * sizeof(*p));
      serverReply.len--;
   }

   return p;
}

//Convert string to windowid. Issue an error if the id is invalid.
Window
serverStrToWin(CS str) {
   unsigned  id = None;
   sscanf((char *)str, "0x%x", &id);
   if (id == None)
      showErrFmtMsg(_(e_invalid_server_id_used_str), str);
   return (Window)id;
}

//Send a reply string (notification) to client with id "name". Return -1 if the window is invalid
int
serverSendReply(Byte *name, Byte *str) {
   Byte   *property;
   int      res;
   Display   *dpy = X_DISPLAY;
   Window   win = serverStrToWin(name);

   if (commProperty == None && SendInit(dpy) < 0)
      return -2;
   if (!WindowValid(dpy, win))
      return -1;

   int length = STRLEN(str) + 14;
   property = alloc(length + 30);

   sprintf((char *)property, "%cn%c-E %c-n %s%c-w %x", 0, 0, 0, str, 0, (unsigned int)commWindow);
   // Add length of what "%x" resulted in.
   length += STRLEN(property + length);
   res = AppendPropCarefully(dpy, win, commProperty, property, length + 1);
   eeglFree(property);

   return res;
}

private int
WaitForReply(void *p) {
   Window  *w = (Window *) p;
   return ServerReplyFind(*w, SROP_Find) != NULL;
}

//Wait for replies from id (win)
//When "timeout" is non-zero wait up to this many seconds.
//Return 0 and the allocated string in "*str" when a reply is available.
//Return -1 if the window becomes invalid while waiting.
int
serverReadReply(
   Display   *dpy,
   Window   win,
   Byte   **str,
   int      localLoop,
   int      timeout)
{
   int      len;
   Byte   *s;
   struct   ServerReply *p;

   ServerWait(dpy, win, WaitForReply, &win, localLoop, timeout > 0 ? timeout : -1);

   if ((p = ServerReplyFind(win, SROP_Find)) != NULL && p->strings.len > 0) {
      *str = copyStr(p->strings.c);
      len = STRLEN(*str) + 1;
      if (len < p->strings.len) {
          s = (CS) p->strings.c;
          mch_memmove(s, s + len, p->strings.len - len);
          p->strings.len -= len;
      } else {
          // Last string read.  Remove from list
          ga_clear(&p->strings);
          ServerReplyFind(win, SROP_Delete);
      }
      return 0;
    }
    return -1;
}

//Check for replies from id (win).
//Return TRUE and a non-malloc'ed string if there is.  Else return FALSE.
int
serverPeekReply(Display *dpy, Window win, Byte **str) {
   struct ServerReply *p;

   if ((p = ServerReplyFind(win, SROP_Find)) != NULL && p->strings.len > 0) {
      if (str)
         *str = p->strings.c;
      return 1;
   }
   if (!WindowValid(dpy, win))
      return -1;
   return 0;
}


//Initialize the communication channels for sending commands and receiving results.
private int
SendInit(Display *dpy) {
   XErrorHandler old_handler;

   //Create the window used for communication, and set up an event handler for it.
   old_handler = XSetErrorHandler(&x_error_check);
   got_x_error = FALSE;

   if (commProperty == None)
      commProperty = XInternAtom(dpy, "Comm", False);
   if (eeglProperty == None)
      eeglProperty = XInternAtom(dpy, "Eegl", False);
   if (registryProperty == None)
      registryProperty = XInternAtom(dpy, "EeglRegistry", False);

   if (commWindow == None) {
      commWindow = XCreateSimpleWindow(dpy, XDefaultRootWindow(dpy),
               getpid(), 0, 10, 10, 0,
               WhitePixel(dpy, DefaultScreen(dpy)),
               WhitePixel(dpy, DefaultScreen(dpy)));
      XSelectInput(dpy, commWindow, PropertyChangeMask);
      // WARNING: Do not step through this while debugging, it will hangup the X server!
      XGrabServer(dpy);
      DeleteAnyLingerer(dpy, commWindow);
      XUngrabServer(dpy);
   }

   // Make window recognizable as an Eegl window
   XChangeProperty(dpy, commWindow, eeglProperty, XA_STRING,
          8, PropModeReplace, (CS)EEGL_VERSION_SHORT,
         (int)STRLEN(EEGL_VERSION_SHORT) + 1);

   XSync(dpy, False);
   (void)XSetErrorHandler(old_handler);

   return got_x_error ? -1 : 0;
}

//Given a server name, see if the name exists in the registry for a particular display.
//
//If the given name is registered, return the ID of the window associated
//with the name. If the name isn't registered, then return 0.
//
//Side effects:
//  If the registry property is improperly formed, then it is deleted.
//  If "delete" is non-zero, then if the named server is found it is
//  removed from the registry property.
private Window
lookupName(
    Display   *dpy,      // Display whose registry to check.
    CS name,      // Name of a server.
    int delete,   // If non-zero, delete info about name.
    Byte** loose    // Do another search matching -999 if not found
                    // Return result here if a match is found
){
    Byte   *regProp, *entry;
    Byte   *p;
    Ulong   numItems;
    Unt   returnValue;

   //Read the registry property.
   if (GetRegProp(dpy, &regProp, &numItems, FALSE) == FAIL)
      return 0;

   //Scan the property for the desired name.
   returnValue = (Unt)None;
   entry = NULL;   // Not needed, but eliminates compiler warning.
   for (p = regProp; (Ulong)(p - regProp) < numItems; ) {
      entry = p;
      while (*p != 0 && !SAFE_isspace(*p))
         p++;
      if (*p != 0 && caseInsensitiveCompare(name, p + 1) == 0) {
         sscanf((char *)entry, "%x", &returnValue);
         break;
      }
      while (*p != 0)
         p++;
      p++;
   }

   if (loose != NULL && returnValue == (Unt)None && !IsSerialName(name)) {
      for (p = regProp; (Ulong)(p - regProp) < numItems; ) {
         entry = p;
         while (*p != 0 && !SAFE_isspace(*p))
            p++;
         if (*p != 0 && IsSerialName(p + 1) && STRNICMP(name, p + 1, STRLEN(name)) == 0) {
            sscanf((char *)entry, "%x", &returnValue);
            *loose = copyStr(p + 1);
            break;
         }
         while (*p != 0)
            p++;
         p++;
      }
   }

   //Delete the property, if that is desired (copy down the
   //remainder of the registry property to overlay the deleted info, then rewrite the property).
   if (delete && returnValue != (Unt)None) {
      int count;

      while (*p != 0)
         p++;
      p++;
      count = numItems - (p - regProp);
      if (count > 0)
         mch_memmove(entry, p, count);
      XChangeProperty(dpy, RootWindow(dpy, 0), registryProperty, XA_STRING,
           8, PropModeReplace, regProp,
           (int)(numItems - (p - entry)));
      XSync(dpy, False);
   }

   if (regProp != empty_prop)
      XFree(regProp);
   return (Window)returnValue;
}

//Delete any lingering occurrence of window id.  We promise that any
//occurrence is not ours since it is not yet put into the registry (by us)
//
//This is necessary in the following scenario:
//1. There is an old windowid for an exited Eegl in the registry
//2. We get that id for our commWindow but only want to send, not register.
//3. The window will mistakenly be regarded valid because of own commWindow
private void
DeleteAnyLingerer(
   Display   *dpy,   // Display whose registry to check.
   Window   win)   // Window to remove
{
   Byte   *regProp, *entry = NULL;
   Byte   *p;
   Ulong   numItems;
   Unt   wwin;

   //Read the registry property.
   if (GetRegProp(dpy, &regProp, &numItems, FALSE) == FAIL) return;

   // Scan the property for the window id.
   for (p = regProp; (Ulong)(p - regProp) < numItems; ) {
      if (*p != 0) {
         sscanf((char *)p, "%x", &wwin);
         if ((Window)wwin == win) {
               int lastHalf;

            // Copy down the remainder to delete entry
            entry = p;
            while (*p != 0)
               p++;
            p++;
            lastHalf = numItems - (p - regProp);
            if (lastHalf > 0)
               mch_memmove(entry, p, lastHalf);
            numItems = (entry - regProp) + lastHalf;
            p = entry;
            continue;
         }
      }
      while (*p != 0)
         p++;
      p++;
   }

   if (entry) {
      XChangeProperty(dpy, RootWindow(dpy, 0), registryProperty,
            XA_STRING, 8, PropModeReplace, regProp,
            (int)(p - regProp));
      XSync(dpy, False);
   }

   if (regProp != empty_prop)
      XFree(regProp);
}

//Read the registry property.  Delete it when it's formatted wrong.
//Return the property in "regPropp".  "empty_prop" is used when it doesn't exist yet.
//Return OK when successful.
private int
GetRegProp(
   Display   *dpy,
   Byte   **regPropp,
   Ulong   *numItemsp,
   int      domsg)      // When TRUE give error message.
{
   int      result, actualFormat;
   Ulong   bytesAfter;
   Atom   actualType;
   XErrorHandler old_handler;

   *regPropp = NULL;
   old_handler = XSetErrorHandler(&x_error_check);
   got_x_error = FALSE;

   result = XGetWindowProperty(dpy, RootWindow(dpy, 0), registryProperty, 0L,
            (long)MAX_PROP_WORDS, False,
            XA_STRING, &actualType,
            &actualFormat, numItemsp, &bytesAfter,
            regPropp);

   XSync(dpy, FALSE);
   (void)XSetErrorHandler(old_handler);
   if (got_x_error)
      return FAIL;

   if (actualType == None) {
      // No prop yet. Logically equal to the empty list
      *numItemsp = 0;
      *regPropp = empty_prop;
      return OK;
   }

   // If the property is improperly formed, then delete it.
   if (result != Success || actualFormat != 8 || actualType != XA_STRING) {
      if (*regPropp != NULL)
         XFree(*regPropp);
      XDeleteProperty(dpy, RootWindow(dpy, 0), registryProperty);
      if (domsg)
         emsg(_(e_eegl_instance_registry_property_is_badly_formed_deleted));
      return FAIL;
   }
   return OK;
}


//This procedure is invoked by the various X event loops throughout Eegls when a property changes 
//on the communication window.  This procedure reads the property and enqueues command requests 
//and responses. If immediate is true, it runs the event immediately instead of enqueuing it. 
//Immediate can cause unintended behavior and should only be used for code that blocks for a 
//response.
void
serverEventProc(
   Display   *dpy,
   XEvent   *eventPtr,   // Information about event.
   int      immediate)   // Run event immediately. Should mostly be 0.
{
   Byte   *propInfo;
   int      result, actualFormat;
   Ulong   numItems, bytesAfter;
   Atom   actualType;

   if (eventPtr 
      && (eventPtr->xproperty.atom != commProperty 
         || eventPtr->xproperty.state != PropertyNewValue)
   )
      return;

   //Read the comm property and delete it.
   propInfo = NULL;
   result = XGetWindowProperty(dpy, commWindow, commProperty, 0L,
            (long)MAX_PROP_WORDS, True,
            XA_STRING, &actualType,
            &actualFormat, &numItems, &bytesAfter,
            &propInfo);

   // If the property doesn't exist or is improperly formed then ignore it.
   if (result != Success || actualType != XA_STRING || actualFormat != 8) {
      if (propInfo != NULL)
          XFree(propInfo);
      return;
   }
   if (immediate)
      server_parse_message(dpy, propInfo, numItems);
   else
      save_in_queue(propInfo, numItems);
}

// Save X clientserver commands in a queue so that they can be called when Eegl is idle.
private void
save_in_queue(Byte *propInfo, Ulong len) {
   x_queue_T* node = ALLOC_ONE(x_queue_T);
   node->propInfo = propInfo;
   node->len = len;

   if (head.next == NULL)  { // initialize circular queue
      head.next = &head;
      head.prev = &head;
   }

   // insert node at tail of queue
   node->next = &head;
   node->prev = head.prev;
   head.prev->next = node;
   head.prev = node;
}

//Parses queued clientserver messages.
void
server_parse_messages(void) {
   x_queue_T   *node;

   if (!X_DISPLAY)
      return; // cannot happen?
   while (head.next != NULL && head.next != &head) {
      node = head.next;
      head.next = node->next;
      node->next->prev = node->prev;
      server_parse_message(X_DISPLAY, node->propInfo, node->len);
      eeglFree(node);
   }
}

//Returns a non-zero value if there are clientserver messages waiting int the queue.
int
server_waiting(void) {
   return head.next != NULL && head.next != &head;
}

//Prases a single clientserver message. A single message may contain multiple commands.
//"propInfo" will be freed.
private void
server_parse_message(
   Display   *dpy,
   Byte   *propInfo, // A string containing 0 or more X commands
   Ulong   numItems)  // The size of propInfo in bytes.
{
   Byte   *p;
   int      code;

   lo("server_parse_message() numItems: %ld", numItems);

   // Several commands and results could arrive in the property at
   // one time;  each iteration through the outer loop handles a single command or result.
   for (p = propInfo; (Ulong)(p - propInfo) < numItems; ) {
      // Ignore leading NULs; each command or result starts with a
      // ZERO so that no matter how badly formed a preceding command
      // is, we'll be able to tell that a new command/result is starting.
      if (*p == 0) {
          p++;
          continue;
      }

      if ((*p == 'c' || *p == 'k') && p[1] == 0) {
         Window   resWindow;
         Byte   *name, *script, *serial, *end;
         Boole   asKeys = *p == 'k';

         // This is an incoming command from some other application.
         // Iterate over all of its options.  Stop when we reach
         // the end of the property or something that doesn't look like an option.
         p += 2;
         name = NULL;
         resWindow = None;
         serial = (CS)"";
         script = NULL;
         while ((Ulong)(p - propInfo) < numItems && *p == '-') {
            lo("server_parse_message() item: %c, %s", p[-2], p);
            switch (p[1]) {
             case 'r':
               end = skipwhite(p + 2);
               resWindow = 0;
               while (eeIsXDigit(*end)) {
                   resWindow = 16 * resWindow + (Ulong)hex2nr(*end);
                   ++end;
               }
               if (end == p + 2 || *end != ' ')
                   resWindow = None;
               else {
                   p = serial = end + 1;
                   clientWindow = resWindow; // Remember in global
               }
               break;
            case 'n':
               if (p[2] == ' ')
                   name = p + 3;
               break;
            case 's':
               if (p[2] == ' ')
                   script = p + 3;
               break;
            case 'E':
               break;
            }
            while (*p != 0)
                p++;
            p++;
         }

         if (script == NULL || name == NULL)
            continue;

         if (serverName != NULL && caseInsensitiveCompare(name, serverName) == 0) {
            if (asKeys)
               server_to_input_buf(script);
            else {
               Byte      *res;
               res = eval_client_expr_to_string(script);
               if (resWindow != None) {
                  ArrayList    reply;

                  // Initialize the result property.
                  ga_init2(&reply, 1, 100);
                  (void)ga_grow(&reply, 50);
                  sprintf(reply.c, "%cr%c-E %c-s %s%c-r ", 0, 0, 0, serial, 0);
                  reply.len = 14 + STRLEN(serial);

                  // Evaluate the expression and return the result.
                  if (res)
                     ga_concat(&reply, res);
                  else {
                     ga_concat(&reply, (CS)_(e_invalid_expression_received));
                     ga_append(&reply, 0);
                     ga_concat(&reply, (CS)"-c 1");
                  }
                  ga_append(&reply, ZERO);
                  (void)AppendPropCarefully(dpy, resWindow, commProperty, reply.c, reply.len);
                  ga_clear(&reply);
               }
               eeglFree(res);
            }
         }
      } ei (*p == 'r' && p[1] == 0) {
         int          serial, gotSerial;
         Byte       *res;
         PendingCommand  *pcPtr;

         // This is a reply to some command that we sent out.  Iterate
         // over all of its options.  Stop when we reach the end of the
         // property or something that doesn't look like an option.
         p += 2;
         gotSerial = 0;
         res = (CS)"";
         code = 0;
         while ((Ulong)(p - propInfo) < numItems && *p == '-') {
         switch (p[1]) {
         case 'r':
            if (p[2] == ' ')
               res = p + 3;
            break;
         case 'E':
            break;
         case 's':
            if (sscanf((char *)p + 2, " %d", &serial) == 1)
               gotSerial = 1;
            break;
         case 'c':
            if (sscanf((char *)p + 2, " %d", &code) != 1)
               code = 0;
            break;
         }
         while (*p != 0)
             p++;
         p++;
         }

         if (!gotSerial)
            continue;

         // Give the result information to anyone who's waiting for it.
         for (pcPtr = pendingCommands; pcPtr != NULL; pcPtr = pcPtr->nextPtr) {
            if (serial != pcPtr->serial || pcPtr->result != NULL)
                continue;

            pcPtr->code = code;
            res = copyStr(res);
            pcPtr->result = res;
            break;
         }
      } ei (*p == 'n' && p[1] == 0) {
         Window   win = 0;
         unsigned int u;
         int      gotWindow;
         Byte   *str;
         struct   ServerReply *r;

         //This is a (n)otification. Sent with serverreply_send in Vimscript. 
         //Execute any autocommand and save it for later retrieval
         p += 2;
         gotWindow = 0;
         str = (CS)"";
         while ((Ulong)(p - propInfo) < numItems && *p == '-') {
            switch (p[1]) {
            case 'n':
               if (p[2] == ' ')
                  str = p + 3;
               break;
            case 'E':
               break;
            case 'w':
               if (sscanf((char *)p + 2, " %x", &u) == 1) {
                  win = u;
                  gotWindow = 1;
               }
               break;
            }
            while (*p != 0)
               p++;
            p++;
         }

         if (!gotWindow)
            continue;
         if ((r = ServerReplyFind(win, SROP_Add)) != NULL) {
            ga_concat(&(r->strings), str);
            ga_append(&(r->strings), ZERO);
         }
         Byte   winstr[30];

         sprintf((char *)winstr, "0x%x", (unsigned int)win);
         apply_autocmds(EVENT_REMOTEREPLY, winstr, str, TRUE, curBook);
      } else {
         //Didn't recognize this thing. Just skip through the next null character and try again.
         //Even if we get an 'r'(eply) we will throw it away as we never specify (and thus expect) 
         //one
         while (*p != 0)
            p++;
         p++;
      }
    }
    XFree(propInfo);
}

// Append a given property to a given window, but set up an X error handler so that if the append 
// fails this procedure can return an error code rather than having Xlib panic. Return: 0 for OK, 
// -1 for error
private int
AppendPropCarefully(
   Display* dpy,    // Display on which to operate.
   Window window,   // Window whose property is to be modified.
   Atom property,   // Name of property.
   CS value,     // Characters  to append to property.
   int length   // How much to append
){
   XErrorHandler old_handler;

   old_handler = XSetErrorHandler(&x_error_check);
   got_x_error = FALSE;
   XChangeProperty(dpy, window, property, XA_STRING, 8, PropModeAppend, value, length);
   XSync(dpy, False);
   (void) XSetErrorHandler(old_handler);
   return got_x_error ? -1 : 0;
}


// Another X Error handler, just used to check for errors.
private int
x_error_check(Display *dpy UNUSED, XErrorEvent *error_event UNUSED) {
   got_x_error = TRUE;
   return 0;
}

// Check if "str" looks like it had a serial number appended.
// Actually just checks if the name ends in a digit.
private int
IsSerialName(Byte *str) {
   int len = STRLEN(str);

   return (len > 1 && eeIsDigit(str[len - 1]));
}

# if defined(ELAPSED_TIMEVAL)

//Give a message about the elapsed time for opening the X window.
private void
xopen_message(long elapsed_msec) {
   smsg(_("Opening the X display took %ld msec"), elapsed_msec);
}
# endif

//A few functions shared by X11 title and clipboard code.

//X Error handler, otherwise X just exits!  (very rude) -- webb
//private int
//x_error_handler(Display *dpy, XErrorEvent *error_event) {
//   XGetErrorText(dpy, error_event->error_code, (char *)IObuff, IOSIZE);
//   STRCAT(IObuff, _("\nEegl: Got X error\n"));
//
//   // In the GUI we cannot print a message and continue, because no X calls
//   // are allowed here (causes my system to hang).  Silently continuing seems
//   // like the best alternative.  Do preserve files, in case we crash.
//   ml_sync_all(FALSE, FALSE);
//
//   msg((char *)IObuff);
//   return 0;      // NOTREACHED
//}

// Return TRUE when connection to the X server is desired.
private int
x_connect_to_server(void) {
   // No point in connecting if we are exiting or dying.
   if (exiting || v_dying)
      return FALSE;

   if (x_force_connect)
      return TRUE;
   if (x_no_connect)
      return FALSE;
   return TRUE;
}

# ifdef USING_SETJMP
// An X IO Error handler, used to catch error while opening the display.
private int
x_IOerror_check(Display *dpy UNUSED){
   // This function should not return, it causes exit().  Longjump instead.
   LONGJMP(lc_jump_env, 1);
}
#endif

// An X IO Error handler, used to catch terminal errors.
static int xterm_dpy_retry_count = 0;

private int
x_IOerror_handler(Display *dpy UNUSED) {
   xterm_dpy = NULL;
   xterm_dpy_retry_count = 5;  // Try reconnecting five times
   x11WindowG = 0;
   x11DisplayG = NULL;
   xterm_Shell = (Widget)0;

   // This function should not return, it causes exit().  Longjump instead.
   LONGJMP(x_jump_env, 1);
}

//If the X11 connection was lost try to restore it.
//Help when the X11 server was stopped and restarted while Eegl was inactive (e.g. through tmux).
void
may_restore_x11_clipboard(void) {
   // No point in restoring the connecting if we are exiting or dying.
   if (!exiting && !v_dying && xterm_dpy_retry_count > 0) {
      --xterm_dpy_retry_count;

# ifndef LESSTIF_VERSION
      // This has been reported to avoid Eegl getting stuck.
      if (app_context != (XtAppContext)NULL) {
         XtDestroyApplicationContext(app_context);
         app_context = (XtAppContext)NULL;
         x11DisplayG = NULL; // freed by XtDestroyApplicationContext()
      }
# endif

      setup_term_clip();
   }
}

void
c_xrestore(Invocation *invo){
   Unt  arglen;

   if (invo->arg != NULL && (arglen = STRLEN(invo->arg)) > 0) {
      if (xterm_display_allocated)
          eeglFree(xterm_display);
      xterm_display = (char *)copySubstr(invo->arg, arglen);
      xterm_display_allocated = TRUE;
   }
   smsg(_("restoring X11 display %s"), xterm_display == NULL
          ? (char *)mch_getenv((CS)"DISPLAY") : xterm_display);

   clear_xterm_clip();
   x11WindowG = 0;
   xterm_dpy_retry_count = 5;  // Try reconnecting five times
   may_restore_x11_clipboard();
}

//Test if "dpy" and x11WindowG are valid by getting the window title.
//I don't actually want it yet, so there may be a simpler call to use, but
//this will cause the error handler x_error_check() to be called if anything
//is wrong, such as the window pointer being invalid (as can happen when the
//user changes his DISPLAY, but not his WINDOWID) -- webb
private int
test_x11WindowG(Display *dpy) {
   int         (*old_handler)(Display*, XErrorEvent*);
   XTextProperty   text_prop;

   old_handler = XSetErrorHandler(x_error_check);
   got_x_error = FALSE;
   if (XGetWMName(dpy, x11WindowG, &text_prop))
      XFree((void *)text_prop.value);
   XSync(dpy, False);
   (void)XSetErrorHandler(old_handler);

   if (p_verbose > 0 && got_x_error)
      verb_msg(_("Testing the X display failed"));

   return (got_x_error ? FAIL : OK);
}

#endif


#if defined(FEAT_X11) || defined(PROTO)

private int   xterm_trace = -1;   // default: disabled
private int   xterm_button;

Boole
isXtermShellDefined() {
   return xterm_Shell != (Widget)0;
}

// Setup a dummy window for X selections in a terminal.
void
setup_term_clip(void){
   int      z = 0;
   char   *strp = "";
   Widget   AppShell;

   if (!x_connect_to_server())
      return;

   open_app_context();
   if (app_context && xterm_Shell == (Widget)0) {
      int (*oldhandler)(Display*, XErrorEvent*);
# if defined(USING_SETJMP)
      int (*oldIOhandler)(Display*);
# endif
      Elapsed start_tv;

      if (p_verbose > 0)
          ELAPSED_INIT(start_tv);

      // Ignore X errors while opening the display
      oldhandler = XSetErrorHandler(x_error_check);

# if defined(USING_SETJMP)
      // Ignore X IO errors while opening the display
      oldIOhandler = XSetIOErrorHandler(x_IOerror_check);
      mch_startjmp();
      if (SETJMP(lc_jump_env) != 0) {
         mch_didjmp();
         xterm_dpy = NULL;
      } else
# endif
      {
         xterm_dpy = XtOpenDisplay(app_context, xterm_display,
             "eegl_xterm", "Eegl_xterm", NULL, 0, &z, &strp);
         if (xterm_dpy != NULL)
            xterm_dpy_retry_count = 0;
# if defined(USING_SETJMP)
          mch_endjmp();
# endif
      }

# if defined(USING_SETJMP)
      // Now handle X IO errors normally.
      (void)XSetIOErrorHandler(oldIOhandler);
# endif
      // Now handle X errors normally.
      (void)XSetErrorHandler(oldhandler);

      if (xterm_dpy == NULL) {
         if (p_verbose > 0)
            verb_msg(_("Opening the X display failed"));
         return;
      }

      // Catch terminating error of the X server connection.
      (void)XSetIOErrorHandler(x_IOerror_handler);

      if (p_verbose > 0) {
         verbose_enter();
         xopen_message(ELAPSED_FUNC(start_tv));
         verbose_leave();
      }

      // Create a Shell to make converters work.
      AppShell = XtVaAppCreateShell("eegl_xterm", "eegl_xterm",
         applicationShellWidgetClass, xterm_dpy,
         NULL);
      if (AppShell == (Widget)0)
          return;
      xterm_Shell = XtVaCreatePopupShell("EEGL",
         topLevelShellWidgetClass, AppShell,
         XtNmappedWhenManaged, 0,
         XtNwidth, 1,
         XtNheight, 1,
         NULL);
      if (xterm_Shell == (Widget)0)
         return;

      x11_setup_atoms(xterm_dpy);
      x11_setup_selection(xterm_Shell);
      if (x11DisplayG == NULL)
         x11DisplayG = xterm_dpy;

      XtRealizeWidget(xterm_Shell);
      XSync(xterm_dpy, False);
      xterm_update();
   }
   if (isXtermShellDefined()) {
      clip_init(TRUE);
      if (x11WindowG == 0 && (strp = getenv("WINDOWID")) != NULL)
         x11WindowG = (Window)atol(strp);
      // Check if $WINDOWID is valid.
      if (test_x11WindowG(xterm_dpy) == FAIL)
         x11WindowG = 0;
      if (x11WindowG != 0)
         xterm_trace = 0;
   }
}

//Query the xterm pointer and generate mouse termcodes if necessary.
//return TRUE if dragging is active, else FALSE
private int
do_xterm_trace(void) {
   Window      root, child;
   int         root_x, root_y;
   int         win_x, win_y;
   int         row, col;
   Unt      mask_return;
   Byte      buf[50];
   Byte      *strp;
   long      got_hints;
   static Byte   *mouse_code = NULL;
   static Unt   mouse_codelen = 0;
   static Byte   mouse_name[2] = {KS_MOUSE, KE_FILLER};
   static int      prev_row = 0, prev_col = 0;
   static XSizeHints   xterm_hints;

   if (xterm_trace <= 0)
      return FALSE;

   if (xterm_trace == 1) {
      // Get the hints just before tracking starts.  The font size might
      // have changed recently.
      if (!XGetWMNormalHints(xterm_dpy, x11WindowG, &xterm_hints, &got_hints)
         || !(got_hints & PResizeInc)
         || xterm_hints.width_inc <= 1
         || xterm_hints.height_inc <= 1)
      {
          xterm_trace = -1;  // Not enough data -- disable tracing
          return FALSE;
      }

      // Rely on the same mouse code for the duration of this
      mouse_code = find_termcode(mouse_name);
      if (mouse_code != NULL)
          mouse_codelen = STRLEN(mouse_code);
      prev_row = mouseRowG;
      prev_col = mouseColG;
      xterm_trace = 2;

      // Find the offset of the chars, there might be a scrollbar on the
      // left of the window and/or a menu on the top (eterm etc.)
      XQueryPointer(xterm_dpy, x11WindowG, &root, &child, &root_x, &root_y,
               &win_x, &win_y, &mask_return);
      xterm_hints.y = win_y - (xterm_hints.height_inc * mouseRowG)
                  - (xterm_hints.height_inc / 2);
      if (xterm_hints.y <= xterm_hints.height_inc / 2)
          xterm_hints.y = 2;
      xterm_hints.x = win_x - (xterm_hints.width_inc * mouseColG)
                  - (xterm_hints.width_inc / 2);
      if (xterm_hints.x <= xterm_hints.width_inc / 2)
          xterm_hints.x = 2;
      return TRUE;
   }

   if (mouse_code == NULL || mouse_codelen > 45) {
      xterm_trace = 0;
      return FALSE;
   }

   XQueryPointer(xterm_dpy, x11WindowG, &root, &child, &root_x, &root_y,
        &win_x, &win_y, &mask_return);

   row = check_row((win_y - xterm_hints.y) / xterm_hints.height_inc);
   col = check_col((win_x - xterm_hints.x) / xterm_hints.width_inc);
   if (row == prev_row && col == prev_col)
      return TRUE;

   STRCPY(buf, mouse_code);
   strp = buf + mouse_codelen;
   *strp++ = (xterm_button | MOUSE_DRAG) & ~0x20;
   *strp++ = (Byte)(col + ' ' + 1);
   *strp++ = (Byte)(row + ' ' + 1);
   *strp = ZERO;
   add_to_input_buf(buf, strp - buf);

   prev_row = row;
   prev_col = col;
   return TRUE;
}

void
start_xterm_trace(int button) {
   if (x11WindowG == 0 || xterm_trace < 0 || xterm_Shell == (Widget)0)
      return;
   xterm_trace = 1;
   xterm_button = button;
   do_xterm_trace();
}

void
stop_xterm_trace(void) {
   if (xterm_trace < 0)
      return;
   xterm_trace = 0;
}

# if defined(FEAT_X11) || defined(PROTO)
// Destroy the display, window and app_context.  Required for GTK.
void
clear_xterm_clip(void){
   if (isXtermShellDefined()) {
      XtDestroyWidget(xterm_Shell);
      xterm_Shell = (Widget)0;
   }
   if (xterm_dpy) {
      if (x11DisplayG == xterm_dpy)
         x11DisplayG = NULL;
      xterm_dpy = NULL;
   }
}
# endif

//Catch up with GUI or X events.
void
clip_update(void) {
   if (isXtermShellDefined())
      xterm_update();
}

//Catch up with any queued X events.  This may put keyboard input into the
//input buffer, call resize call-backs, trigger timers etc.  If there is
//nothing in the X event queue (& no timers pending), then we return immediately.
void
xterm_update(void) {
   XEvent event;

   for (;;) {
      XtInputMask mask = XtAppPending(app_context);

      if (mask == 0 || eeIsInputBufFull())
          break;

      if (mask & XtIMXEvent) {
         // There is an event to process.
         XtAppNextEvent(app_context, &event);
         {
         XPropertyEvent *e = (XPropertyEvent *)&event;

         if (e->type == PropertyNotify && e->window == commWindow
            && e->atom == commProperty && e->state == PropertyNewValue)
             serverEventProc(xterm_dpy, &event, 0);
         }
         XtDispatchEvent(&event);
      } else {
         // There is something else than an event to process.
         XtAppProcessEvent(app_context, mask);
      }
   }
}

int
clip_xterm_own_selection(ClipBoard *cbd) {
   if (isXtermShellDefined())
      return clip_x11_own_selection(xterm_Shell, cbd);
   return FAIL;
}

void
clip_xterm_lose_selection(ClipBoard *cbd) {
   if (isXtermShellDefined())
      clip_x11_lose_selection(xterm_Shell, cbd);
}

void
clip_xterm_request_selection(ClipBoard *cbd) {
   if (isXtermShellDefined())
      clip_x11_request_selection(xterm_Shell, xterm_dpy, cbd);
}

void
clip_xterm_set_selection(ClipBoard *cbd) {
   clip_x11_set_selection(cbd);
}
#endif

//}}}
//{{{Wayland

#ifdef FEAT_WAYLAND

#include <wayland-client.h>

#ifdef FEAT_WAYLAND
#include "../libs/wayland/wlr-data-control-unstable-v1.h"
#include "../libs/wayland/ext-data-control-v1.h"
#include "../libs/wayland/xdg-shell.h"
#include "../libs/wayland/primary-selection-unstable-v1.h"
#endif

// Struct that represents a seat. (Should be accessed via
// vwl_get_seat()).
typedef struct {
   struct wl_seat  *proxy;
   char       *label;      // Name of seat as text (e.g. seat0, seat1...).
   uint32_t capabilities;  // Bitmask of the capabilites of the seat (pointer, keyboard, touch)
} vwl_seat_T;

// Global objects
typedef struct {
#ifdef FEAT_WAYLAND
   // Data control protocols
   struct zwlr_data_control_manager_v1 *zwlr_data_control_manager_v1;
   struct ext_data_control_manager_v1   *ext_data_control_manager_v1;
   struct wl_data_device_manager   *wl_data_device_manager;
   struct wl_shm         *wl_shm;
   struct wl_compositor      *wl_compositor;
   struct xdg_wm_base         *xdg_wm_base;
   struct zwp_primary_selection_device_manager_v1
  *zwp_primary_selection_device_manager_v1;
#endif
} vwl_global_objects_T;

// Struct wrapper for Wayland display and registry
typedef struct {
   struct wl_display   *proxy;
   int         fd;   // File descriptor for display

   struct {
      struct wl_registry *proxy;
   } registry;
} vwl_display_T;

#ifdef FEAT_WAYLAND

typedef struct {
   struct wl_shm_pool   *pool;
   int         fd;

   struct wl_buffer   *buffer;
   int         available;

   int         width;
   int         height;
   int         stride;
   int         size;
} vwl_buffer_store_T;

typedef struct {
void          *user_data;
void          (*on_focus)(void *data, uint32_t serial);

   struct wl_surface       *surface;
   struct wl_keyboard       *keyboard;

   struct {
      struct xdg_surface  *surface;
      struct xdg_toplevel *toplevel;
   } shell;

   int got_focus;
} vwl_fs_surface_T; // fs = focus steal

// Wayland protocols for accessing the selection
typedef enum {
   VWL_DATA_PROTOCOL_NONE,
   VWL_DATA_PROTOCOL_EXT,
   VWL_DATA_PROTOCOL_WLR,
   VWL_DATA_PROTOCOL_CORE,
   VWL_DATA_PROTOCOL_PRIMARY
} vwl_data_protocol_T;

// DATA RELATED OBJECT WRAPPERS
// These wrap around a proxy and act as a generic container.
// The `data` member is used to pass other needed stuff around such as a
// vwl_clipboard_selection_T pointer.

typedef struct {
   void      *proxy;
   void      *data; // Is not set when a new offer is created on a
                // data_offer event. Only set when listening to a data offer.
   vwl_data_protocol_T protocol;
} vwl_data_offer_T;

typedef struct {
   void      *proxy;
   void      *data;
   vwl_data_protocol_T protocol;
} vwl_data_source_T;

typedef struct {
   void* proxy;
   void* data;
   vwl_data_protocol_T protocol;
} vwl_data_device_T;

typedef struct {
   void* proxy;
   vwl_data_protocol_T protocol;
} vwl_data_device_manager_T;

// LISTENER WRAPPERS

typedef struct {
   void (*data_offer)(vwl_data_device_T *device, vwl_data_offer_T *offer);

   // If the protocol that the data device uses doesn't support a specific
   // selection, then this callback will never be called with that selection.
   void (*selection)(
      vwl_data_device_T *device,
      vwl_data_offer_T *offer,
      WaylandSelection selection);

   // This event is only relevant for data control protocols
   void (*finished)(vwl_data_device_T *device);
} vwl_data_device_Listener;

typedef struct {
   void (*send)(vwl_data_source_T *source, const char *mime_type, int fd);
   void (*cancelled)(vwl_data_source_T *source);
} vwl_data_source_Listener;

typedef struct {
   void (*offer)(vwl_data_offer_T *offer, const char *mime_type);
} vwl_data_offer_Listener;

typedef struct {
   // What selection this refers to
   WaylandSelection      selection;

   // Do not destroy here
   vwl_data_device_manager_T   manager;

   vwl_data_device_T      device;
   vwl_data_source_T      source;
   vwl_data_offer_T      *offer;   // Current offer for the selection

   ArrayList         mime_types;   // Mime types supported by the current offer

   ArrayList         tmp_mime_types;   // Temporary array for mime types when we are receiving
                  // them. When the selection event arrives and it is the
                  // one we want, then copy it over to mime_types

   // To be populated by callbacks from outside this file
   wayland_cb_send_data_func_T          send_cb;
   wayland_cb_selection_cancelled_func_T   cancelled_cb;

   int requires_focus;      // If focus needs to be given to us to work
} vwl_clipboard_selection_T;

// Holds stuff related to the clipboard/selections
typedef struct {
   // Do not destroy here, will be destroyed when vwl_disconnect_display() is called.
   vwl_seat_T         *seat;

   vwl_clipboard_selection_T   regular;
   vwl_clipboard_selection_T   primary;

   vwl_buffer_store_T      *fs_buffer;
} vwl_clipboard_T;

#endif // FEAT_WAYLAND

private int   vwl_display_flush(vwl_display_T *display);
private void   vwl_callback_done(void *data, struct wl_callback *callback,
          uint32_t cb_data);
private int   vwl_display_roundtrip(vwl_display_T *display);
private int   vwl_display_dispatch(vwl_display_T *display);
private int vwl_display_dispatch_any(vwl_display_T *display);

private void   vwl_log_handler(const char *fmt, va_list args);
private int   vwl_connect_display(const char *display);
private void   vwl_disconnect_display(void);

private void vwl_xdg_wm_base_listener_ping(void *data, struct xdg_wm_base *base, uint32_t serial);
private int   vwl_listen_to_registry(void);

private void   vwl_registry_listener_global(
    void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version
);
private void   vwl_registry_listener_global_remove(void *data,
          struct wl_registry *registry,  uint32_t name);

private void   vwl_add_seat(struct wl_seat *seat);
private void   vwl_seat_listener_name(void *data, struct wl_seat *seat,
          const char *name);
private void   vwl_seat_listener_capabilities(void *data, struct wl_seat *seat,
          uint32_t capabilities);
private void   vwl_destroy_seat(vwl_seat_T *seat);

private vwl_seat_T       *vwl_get_seat(const char *label);
private struct wl_keyboard   *vwl_seat_get_keyboard(vwl_seat_T *seat);

#ifdef FEAT_WAYLAND

private int   vwl_focus_stealing_available(void);
private void   vwl_xdg_surface_listener_configure(void *data,
          struct xdg_surface *surface, uint32_t serial);

private void   vwl_bs_buffer_listener_release(void *data,
          struct wl_buffer *buffer);
private void   vwl_destroy_buffer_store(vwl_buffer_store_T *store);
private vwl_buffer_store_T *vwl_init_buffer_store(int width, int height);

private void   vwl_destroy_fs_surface(vwl_fs_surface_T *store);
private int   vwl_init_fs_surface(vwl_seat_T *seat,
          vwl_buffer_store_T *buffer_store,
          void (*on_focus)(void *, uint32_t), void *user_data);

private void   vwl_fs_keyboard_listener_enter(void *data,
          struct wl_keyboard *keyboard, uint32_t serial,
          struct wl_surface *surface, struct wl_array *keys);
private void   vwl_fs_keyboard_listener_keymap(void *data,
          struct wl_keyboard *keyboard, uint32_t format,
          int fd, uint32_t size);
private void   vwl_fs_keyboard_listener_leave(void *data,
          struct wl_keyboard *keyboard, uint32_t serial,
          struct wl_surface *surface);
private void   vwl_fs_keyboard_listener_key(void *data,
          struct wl_keyboard *keyboard, uint32_t serial,
          uint32_t time, uint32_t key, uint32_t state);
private void   vwl_fs_keyboard_listener_modifiers(void *data,
          struct wl_keyboard *keyboard, uint32_t serial,
          uint32_t mods_depressed, uint32_t mods_latched,
          uint32_t mods_locked, uint32_t group);
private void   vwl_fs_keyboard_listener_repeat_info(void *data,
          struct wl_keyboard *keyboard, int32_t rate, int32_t delay);

private void   vwl_gen_data_device_listener_data_offer(void *data,
          void *offer_proxy);
private void   vwl_gen_data_device_listener_selection(void *data,
          void *offer_proxy, WaylandSelection selection,
          vwl_data_protocol_T protocol);

private void   vwl_data_device_destroy(vwl_data_device_T *device, int alloced);
private void   vwl_data_offer_destroy(vwl_data_offer_T *offer, int alloced);
private void   vwl_data_source_destroy(vwl_data_source_T *source, int alloced);

private void   vwl_data_device_add_listener(vwl_data_device_T *device,
          void *data);
private void   vwl_data_source_add_listener(vwl_data_source_T *source,
          void *data);
private void   vwl_data_offer_add_listener(vwl_data_offer_T *offer,
          void *data);

private void   vwl_data_device_set_selection(vwl_data_device_T *device,
          vwl_data_source_T *source, uint32_t serial,
          WaylandSelection selection);
private void   vwl_data_offer_receive(vwl_data_offer_T *offer,
          const char *mime_type, int fd);
private int   vwl_get_data_device_manager(vwl_data_device_manager_T *manager,
          WaylandSelection selection);
private void   vwl_get_data_device(vwl_data_device_manager_T *manager,
          vwl_seat_T *seat, vwl_data_device_T *device);
private void   vwl_create_data_source(vwl_data_device_manager_T *manager,
          vwl_data_source_T *source);
private void   vwl_data_source_offer(vwl_data_source_T *source,
          const char *mime_type);

private void   vwl_clipboard_free_mime_types(
          vwl_clipboard_selection_T *clip_sel);
private int   vwl_clipboard_selection_is_ready(
          vwl_clipboard_selection_T *clip_sel);

private void   vwl_data_device_listener_data_offer(
          vwl_data_device_T *device, vwl_data_offer_T *offer);
private void   vwl_data_offer_listener_offer(vwl_data_offer_T *offer,
          const char *mime_type);
private void   vwl_data_device_listener_selection(vwl_data_device_T *device,
          vwl_data_offer_T *offer, WaylandSelection selection);
private void   vwl_data_device_listener_finished(vwl_data_device_T *device);

private void   vwl_data_source_listener_send(vwl_data_source_T *source,
          const char *mime_type, int fd);
private void   vwl_data_source_listener_cancelled(vwl_data_source_T *source);

private void   vwl_on_focus_set_selection(void *data, uint32_t serial);

private void   wayland_set_display(const char *display);

private vwl_data_device_Listener   vwl_data_device_listener = {
    .data_offer       = vwl_data_device_listener_data_offer,
    .selection       = vwl_data_device_listener_selection,
    .finished       = vwl_data_device_listener_finished
};

private vwl_data_source_Listener   vwl_data_source_listener = {
    .send       = vwl_data_source_listener_send,
    .cancelled       = vwl_data_source_listener_cancelled
};

private vwl_data_offer_Listener    vwl_data_offer_listener = {
    .offer       = vwl_data_offer_listener_offer
};

private struct xdg_wm_base_listener  vwl_xdg_wm_base_listener = {
    .ping       = vwl_xdg_wm_base_listener_ping
};

private struct xdg_surface_listener  vwl_xdg_surface_listener = {
    .configure       = vwl_xdg_surface_listener_configure
};

private struct wl_buffer_listener    vwl_cb_buffer_listener = {
    .release       = vwl_bs_buffer_listener_release
};

private struct wl_keyboard_listener  vwl_fs_keyboard_listener = {
    .enter       = vwl_fs_keyboard_listener_enter,
    .key       = vwl_fs_keyboard_listener_key,
    .keymap       = vwl_fs_keyboard_listener_keymap,
    .leave       = vwl_fs_keyboard_listener_leave,
    .modifiers       = vwl_fs_keyboard_listener_modifiers,
    .repeat_info    = vwl_fs_keyboard_listener_repeat_info
};

#endif // FEAT_WAYLAND

private struct wl_callback_listener  vwl_callback_listener = {
    .done       = vwl_callback_done
};

private struct wl_registry_listener  vwl_registry_listener = {
    .global       = vwl_registry_listener_global,
    .global_remove  = vwl_registry_listener_global_remove
};

private struct wl_seat_listener       vwl_seat_listener = {
    .name       = vwl_seat_listener_name,
    .capabilities   = vwl_seat_listener_capabilities
};

private vwl_display_T          vwl_display;
private vwl_global_objects_T       vwl_gobjects;
private ArrayList             vwl_seats;

#ifdef FEAT_WAYLAND
// Make sure to sync this with vwl_cb_uninit since it memsets this to zero
private vwl_clipboard_T   vwl_clipboard = {
    .regular.selection = WAYLAND_SELECTION_REGULAR,
    .primary.selection = WAYLAND_SELECTION_PRIMARY,
};

// Only really used for debugging/testing purposes in order to force focus
// stealing even when a data control protocol is available.
private int force_fs  = FALSE;
#endif

//Like wl_display_flush but always writes all the data in the buffer to the
//display fd. Returns FAIL on failure and OK on success.
private int
vwl_display_flush(vwl_display_T *display) {
   int ret;

   fd_set       wfds;
   TimeVal  tv;

   FD_ZERO(&wfds);
   FD_SET(display->fd, &wfds);

   tv.tv_sec   = p_wtm / 1000;
   tv.tv_usec   = (p_wtm % 1000) * 1000;

   if (display->proxy == NULL)
      return FAIL;

   //Send the requests we have made to the compositor, until we have written
   //all the data. Poll in order to check if the display fd is writable; if
   //not, then wait until it is and continue writing or until we timeout.
   while (errno = 0, (ret = wl_display_flush(display->proxy)) == -1 && errno == EAGAIN) {
         if (select(display->fd + 1, NULL, &wfds, NULL, &tv) <= 0)
            return FAIL;
      tv.tv_sec   = 0;
      tv.tv_usec   = p_wtm * 1000;
   }
   //Return FAIL on error or timeout
   if ((errno != 0 && errno != EAGAIN) || ret == -1)
      return FAIL;

   return OK;
}

//Called when compositor is done processing requests/events.
private void
vwl_callback_done(void *data, struct wl_callback *callback, uint32_t cb_data UNUSED) {
   *((int*)data) = TRUE;
   wl_callback_destroy(callback);
}

//Like wl_display_roundtrip but polls the display fd with a timeout. Return OK/FAIL
private int
vwl_display_roundtrip(vwl_display_T *display) {
   struct wl_callback   *callback;
   int         ret, done = FALSE;
   TimeVal start, now;

   if (display->proxy == NULL)
      return FAIL;

   //Tell compositor to emit 'done' event after processing all requests we
   //have sent and handling events.
   callback = wl_display_sync(display->proxy);

   if (callback == NULL)
      return FAIL;

   wl_callback_add_listener(callback, &vwl_callback_listener, &done);

   gettimeofday(&start, NULL);

   //Wait till we get the done event (which will set `done` to TRUE), unless we timeout
   while (TRUE) {
      ret = vwl_display_dispatch(display);

      if (done || ret == -1)
         break;

      gettimeofday(&now, NULL);

      if ((now.tv_sec * 1000000 + now.tv_usec) -
         (start.tv_sec * 1000000 + start.tv_usec) >= p_wtm * 1000)
      {
          ret = -1;
          break;
      }
   }

   if (ret == -1) {
      if (!done)
         wl_callback_destroy(callback);
      return FAIL;
   }

   return OK;
}

//Like wl_display_roundtrip but poll the display fd with a timeout. Return
//number of events dispatched on success else -1 on failure.
private int
vwl_display_dispatch(vwl_display_T *display) {
   fd_set          rfds;
   TimeVal  tv;

   FD_ZERO(&rfds);
   FD_SET(display->fd, &rfds);

   tv.tv_sec = p_wtm / 1000;
   tv.tv_usec = (p_wtm % 1000) * 1000;

   if (display->proxy == NULL)
      return -1;

   while (wl_display_prepare_read(display->proxy) == -1) {
      // Dispatch any queued events so that we can start reading
      if (wl_display_dispatch_pending(display->proxy) == -1)
         return -1;
   } 

   // Send any requests before we starting blocking to read display fd
   if (vwl_display_flush(display) == FAIL) {
      wl_display_cancel_read(display->proxy);
      return -1;
   }

   // Poll until there is data to read from the display fd.
   if (select(display->fd + 1, &rfds, NULL, NULL, &tv) <= 0) {
      wl_display_cancel_read(display->proxy);
      return -1;
   }

   // Read events into the queue
   if (wl_display_read_events(display->proxy) == -1)
      return -1;

   // Dispatch those events (call the handlers associated for each event)
   return wl_display_dispatch_pending(display->proxy);
}

// Same as vwl_display_dispatch but poll/select is never called. This is useful
// is poll/select was already called before or if you just want to dispatch any
// events that happen to be waiting to be dispatched on the display fd.
private int
vwl_display_dispatch_any(vwl_display_T *display) {
   if (display->proxy == NULL)
   return -1;

   while (wl_display_prepare_read(display->proxy) == -1) {
      // Dispatch any queued events so that we can start reading
      if (wl_display_dispatch_pending(display->proxy) == -1)
          return -1;
   }

   // Send any requests before we starting blocking to read display fd
   if (vwl_display_flush(display) == FAIL) {
      wl_display_cancel_read(display->proxy);
      return -1;
   }

   // Read events into the queue
   if (wl_display_read_events(display->proxy) == -1)
      return -1;

   // Dispatch those events (call the handlers associated for each event)
   return wl_display_dispatch_pending(display->proxy);
}

// Redirect libwayland logging to use ch_log + emsg instead.
private void
vwl_log_handler(const char *fmt, va_list args) {
   // 512 bytes should be big enough
   CS builder = alloc(512);
   CS prefix = _("wayland protocol error -> ");
   Unt len = STRLEN(prefix);
   copySubstrToAllocation((Byte*)builder, (Byte*)prefix, len);
   eeVsnprintf(builder + len, 4096 - len, fmt, args);

   // Remove newline that libwayland puts
   builder[STRLEN(buf) - 1] = ZERO;

   lo("%s", builder);
   emsg(builder);

   eeglFree(builder);
}

//Connect to the display with name; passing NULL will use libwayland's way of
//getting the display. Additionally get the registry object but will not
//starting listening. Returns OK on sucess and FAIL on failure.
private int
vwl_connect_display(const char *display) {
   if (wayland_no_connect)
      return FAIL;

   // We will get an error if XDG_RUNTIME_DIR is not set.
   if (mch_getenv("XDG_RUNTIME_DIR") == NULL)
      return FAIL;

   // Must set log handler before we connect display in order to work.
   wl_log_set_handler_client(vwl_log_handler);

   vwl_display.proxy = wl_display_connect(display);

   if (vwl_display.proxy == NULL)
      return FAIL;

   wayland_set_display(display);
   vwl_display.fd = wl_display_get_fd(vwl_display.proxy);

   vwl_display.registry.proxy = wl_display_get_registry(vwl_display.proxy);

   if (vwl_display.registry.proxy == NULL) {
      vwl_disconnect_display();
      return FAIL;
   }

   return OK;
}

#define destroy_gobject(object) \
    if (vwl_gobjects.object != NULL) \
    { \
   object##_destroy(vwl_gobjects.object); \
   vwl_gobjects.object = NULL; \
    }

// Disconnect the display and frees up all resources, including all global objects.
private void
vwl_disconnect_display(void) {
   destroy_gobject(ext_data_control_manager_v1)
   destroy_gobject(zwlr_data_control_manager_v1)
   destroy_gobject(wl_data_device_manager)
   destroy_gobject(wl_shm)
   destroy_gobject(wl_compositor)
   destroy_gobject(xdg_wm_base)
   destroy_gobject(zwp_primary_selection_device_manager_v1)

   for (int i = 0; i < vwl_seats.len; i++)
      vwl_destroy_seat(&((vwl_seat_T *)vwl_seats.c)[i]);
   ga_clear(&vwl_seats);
   vwl_seats.len = 0;

   if (vwl_display.registry.proxy != NULL) {
      wl_registry_destroy(vwl_display.registry.proxy);
      vwl_display.registry.proxy = NULL;
   }
   if (vwl_display.proxy != NULL) {
      wl_display_disconnect(vwl_display.proxy);
      vwl_display.proxy = NULL;
   }
}

// Tell the compositor we are still responsive.
private void
vwl_xdg_wm_base_listener_ping(
   void* data UNUSED,
   struct xdg_wm_base *base,
   uint32_t serial
) {
    xdg_wm_base_pong(base, serial);
}

// Start listening to the registry and get initial set of global objects/interfaces.
private int
vwl_listen_to_registry(void) {
   // Only meant for debugging/testing purposes
   CS env = mch_getenv("EEGL_WAYLAND_FORCE_FS");

   if (env != NULL && STRCMP(env, "1") == 0)
      force_fs = TRUE;
   else
      force_fs = FALSE;

   ga_init2(&vwl_seats, sizeof(vwl_seat_T), 1);

   wl_registry_add_listener( vwl_display.registry.proxy, &vwl_registry_listener, NULL);

   if (vwl_display_roundtrip(&vwl_display) == FAIL)
      return FAIL;

   // If we have a suitable data control protocol discard the rest. If we only
   // have wlr data control protocol but its version is 1, then don't discard
   // globals if we also have the primary selection protocol.
   if (!force_fs &&
       (vwl_gobjects.ext_data_control_manager_v1 != NULL ||
        (vwl_gobjects.zwlr_data_control_manager_v1 != NULL &&
         zwlr_data_control_manager_v1_get_version(
        vwl_gobjects.zwlr_data_control_manager_v1) > 1))
   ) {
      destroy_gobject(wl_data_device_manager)
      destroy_gobject(wl_shm)
      destroy_gobject(wl_compositor)
      destroy_gobject(xdg_wm_base)
   } else {
      // Be ready for ping events
      xdg_wm_base_add_listener( vwl_gobjects.xdg_wm_base, &vwl_xdg_wm_base_listener, NULL);
   } 
   return OK;
}

#define SET_GOBJECT(object, min_ver) \
    do { \
   chosen_interface = &object##_interface; \
   object_member = (void*)&vwl_gobjects.object; \
   min_version = min_ver; \
    } while (0)

//Callback for global event, for each global interface the compositor supports.
//Keep in sync with vwl_disconnect_display().
private void
vwl_registry_listener_global(
   void          *data UNUSED,
   struct wl_registry  *registry UNUSED,
   uint32_t       name,
   const char       *interface,
   uint32_t       version
) {

   const struct wl_interface   *chosen_interface = NULL;
void         *proxy;
   uint32_t         min_version;
void         **object_member;

   if (STRCMP(interface, wl_seat_interface.name) == 0) {
      chosen_interface = &wl_seat_interface;
      min_version = 2;
   }
#ifdef FEAT_WAYLAND
   ei (STRCMP(interface, zwlr_data_control_manager_v1_interface.name) == 0)
      SET_GOBJECT(zwlr_data_control_manager_v1, 1);

   ei (STRCMP(interface, ext_data_control_manager_v1_interface.name) == 0)
      SET_GOBJECT(ext_data_control_manager_v1, 1);

   ei (STRCMP(interface, wl_data_device_manager_interface.name) == 0)
      SET_GOBJECT(wl_data_device_manager, 1);

   ei (STRCMP(interface, wl_shm_interface.name) == 0)
      SET_GOBJECT(wl_shm, 1);

   ei (STRCMP(interface, wl_compositor_interface.name) == 0)
      SET_GOBJECT(wl_compositor, 2);

   ei (STRCMP(interface, xdg_wm_base_interface.name) == 0)
      SET_GOBJECT(xdg_wm_base, 1);

   ei (STRCMP(interface, zwp_primary_selection_device_manager_v1_interface.name) == 0)
      SET_GOBJECT(zwp_primary_selection_device_manager_v1, 1);
#endif

   if (chosen_interface == NULL || version < min_version)
      return;

   proxy = wl_registry_bind(vwl_display.registry.proxy, name, chosen_interface, version);

   if (chosen_interface == &wl_seat_interface)
      // Add seat to vwl_seats array, as we can have multiple seats.
      vwl_add_seat(proxy);
   else
      // Hold proxy & name in the vwl_gobject struct
      *object_member = proxy;
}

// Called when a global object is removed, if so, then do nothing. This is to
// avoid a global being removed while it is in the process of being used. Let
// the user call :wlrestore in order to reset everything. Requests to that
// global will just be ignored on the compositor side.
private void
vwl_registry_listener_global_remove(
   void          *data,
   struct wl_registry  *registry,
   uint32_t       name UNUSED)
{
}

// Add a new seat given its proxy to the global grow array
private void
vwl_add_seat(struct wl_seat *seat_proxy) {
   vwl_seat_T *seat;

   if (ga_grow(&vwl_seats, 1) == FAIL)
      return;

   seat = &((vwl_seat_T *)vwl_seats.c)[vwl_seats.len];

   seat->proxy = seat_proxy;

   // Get label and capabilities
   wl_seat_add_listener(seat_proxy, &vwl_seat_listener, seat);

   if (vwl_display_roundtrip(&vwl_display) == FAIL)
      return;

   // Check if label has been allocated
   if (seat->label == NULL)
      return;

   vwl_seats.len++;
}

// Callback for seat text label/name
private void
vwl_seat_listener_name(void* data, struct wl_seat* seat_proxy UNUSED, const char* name) {
   vwl_seat_T *seat = data;
   seat->label = (char *)copyStr((CS)name);
}

// Callback for seat capabilities
private void
vwl_seat_listener_capabilities(
   void      *data,
   struct wl_seat   *seat_proxy UNUSED,
   uint32_t   capabilities
) {
   vwl_seat_T *seat = data;
   seat->capabilities = capabilities;
}

// Destroy/free seat.
private void
vwl_destroy_seat(vwl_seat_T *seat) {
   if (seat->proxy) {
      if (wl_seat_get_version(seat->proxy) >= 5)
         // Helpful for the compositor
         wl_seat_release(seat->proxy);
      else
         wl_seat_destroy(seat->proxy);
      seat->proxy = NULL;
   }
   eeglFree(seat->label);
   seat->label = NULL;
}

// Return a seat with the give name/label. If none exists then NULL is returned.
// If NULL or an empty string is passed as the label then the first available
// seat found is used.
private vwl_seat_T *
vwl_get_seat(const char *label) {
   if ((STRCMP(label, "") == 0 || label == NULL) && vwl_seats.len > 0)
      return &((vwl_seat_T *)vwl_seats.c)[0];

   for (int i = 0; i < vwl_seats.len; i++) {
      vwl_seat_T *seat = &((vwl_seat_T *)vwl_seats.c)[i];
      if (STRCMP(seat->label, label) == 0)
         return seat;
   }
   return NULL;
}

// Get keyboard object from seat and return it. NULL is returned on
// failure such as when a keyboard is not available for seat.
private struct wl_keyboard *
vwl_seat_get_keyboard(vwl_seat_T *seat) {
   if (!(seat->capabilities & WL_SEAT_CAPABILITY_KEYBOARD))
      return NULL;

   return wl_seat_get_keyboard(seat->proxy);
}

// Connect to the Wayland display with given name and binds to global objects
// as needed. If display is NULL then the $WAYLAND_DISPLAY environment variable
// will be used (handled by libwayland). Returns FAIL on failure and OK on success
int
wayland_init_client(const char *display) {
   wayland_set_display(display);

   if (vwl_connect_display(display) == FAIL || vwl_listen_to_registry() == FAIL)
      goto fail;

   wayland_display_fd = vwl_display.fd;

   return OK;
fail:
   // Set v:wayland_display to empty string (but not wayland_display_name)
   wayland_set_display("");
   return FAIL;
}

// Disconnect Wayland client and free up all resources used.
void
wayland_uninit_client(void) {
#ifdef FEAT_WAYLAND
    wayland_cb_uninit();
#endif
    vwl_disconnect_display();

    wayland_set_display("");
}

// TRUE if Wayland display connection is valid and ready.
int
wayland_client_is_connected(int quiet) {
   if (vwl_display.proxy == NULL)
      goto error;

   // Display errors are always fatal
   if (wl_display_get_error(vwl_display.proxy) != 0 || vwl_display_flush(&vwl_display) == FAIL)
      goto error;

   return TRUE;
error:
   if (!quiet)
      emsg(e_wayland_connection_unavailable);
   return FALSE;
}

// Flush requests and process new Wayland events, does not poll the display file descriptor.
int
wayland_client_update(void) {
   return vwl_display_dispatch_any(&vwl_display) == -1 ? FAIL : OK;
}

#ifdef FEAT_WAYLAND

// If globals required for focus stealing method is available.
private int
vwl_focus_stealing_available(void) {
   return (p_wst || force_fs) &&
      vwl_gobjects.wl_compositor != NULL &&
      vwl_gobjects.wl_shm != NULL &&
      vwl_gobjects.xdg_wm_base != NULL;
}

// Configure xdg_surface
private void
vwl_xdg_surface_listener_configure(
   void          *data UNUSED,
   struct xdg_surface  *surface,
   uint32_t       serial)
{
   xdg_surface_ack_configure(surface, serial);
}

// Called when compositor isn't using the buffer anymore, we can reuse it again.
private void
vwl_bs_buffer_listener_release(
   void          *data,
   struct wl_buffer    *buffer UNUSED)
{
   vwl_buffer_store_T *store = data;

   store->available = TRUE;
}

// Destroy a buffer store structure.
private void
vwl_destroy_buffer_store(vwl_buffer_store_T *store) {
   if (store->buffer != NULL)
      wl_buffer_destroy(store->buffer);
   if (store->pool != NULL)
      wl_shm_pool_destroy(store->pool);

   close(store->fd);

   eeglFree(store);
}

// Create an anonymous/temporary file/object and return its file descriptor. Return -1 on error.
private int
mch_create_anon_file(void) {
   int fd = -1;
   const char template[] = "/eeglXXXXXX";

   for (int i = 0; i < 100; i++) {
      mch_get_random((Byte*)template + 4, 6);
      errno = 0;
      fd = shm_open(template, O_CREAT | O_RDWR | O_EXCL, 0600);

      if (fd >= 0 || errno != EEXIST)
         break;
   }
   // Remove object name from namespace
   shm_unlink(template);
    // Last resort
   if (fd == -1) {
      Byte   *tempname;
      // get a name for the temp file
      if ((tempname = eeTempName('w', FALSE)) == NULL) {
         emsg(_(e_cant_get_temp_file_name));
         return -1;
      }
      fd = open((char *)tempname, O_CREAT | O_RDWR | O_EXCL, 0600);
      mch_remove(tempname);
      eeglFree(tempname);
   }
   return fd;
}


// Initialize a buffer and its backing memory pool.
private vwl_buffer_store_T *
vwl_init_buffer_store(int width, int height) {
   int         fd, r;

   if (vwl_gobjects.wl_shm == NULL)
      return NULL;

   vwl_buffer_store_T store = alloc(sizeof(*store));

   store->available = FALSE;

   store->width = width;
   store->height = height;
   store->stride = store->width * 4;
   store->size = store->stride * store->height;

   fd = mch_create_anon_file();
   r = ftruncate(fd, store->size);

   if (r == -1) {
      if (fd >= 0)
          close(fd);
      return NULL;
   }

   store->pool = wl_shm_create_pool(vwl_gobjects.wl_shm, fd, store->size);
   store->buffer = wl_shm_pool_create_buffer(
       store->pool,
       0,
       store->width,
       store->height,
       store->stride,
       WL_SHM_FORMAT_ARGB8888);

   store->fd = fd;

   wl_buffer_add_listener(store->buffer, &vwl_cb_buffer_listener, store);

   if (vwl_display_roundtrip(&vwl_display) == -1) {
      vwl_destroy_buffer_store(store);
      return NULL;
   }

   store->available = TRUE;

   return store;
}

// Destroy a focus stealing store structure.
private void
vwl_destroy_fs_surface(vwl_fs_surface_T *store) {
   if (store->shell.toplevel != NULL)
      xdg_toplevel_destroy(store->shell.toplevel);
   if (store->shell.surface != NULL)
      xdg_surface_destroy(store->shell.surface);
   if (store->surface != NULL)
      wl_surface_destroy(store->surface);
   if (store->keyboard != NULL) {
      if (wl_keyboard_get_version(store->keyboard) >= 3)
         wl_keyboard_release(store->keyboard);
      else
         wl_keyboard_destroy(store->keyboard);
   }
    eeglFree(store);
}

// Create an invisible surface in order to gain focus and call on_focus() with
// serial that was given.
private int
vwl_init_fs_surface(
   vwl_seat_T       *seat,
   vwl_buffer_store_T  *buffer_store,
   void          (*on_focus)(void *, uint32_t),
   void          *user_data
) {
   vwl_fs_surface_T *store;

   if (vwl_gobjects.wl_compositor == NULL || vwl_gobjects.xdg_wm_base == NULL)
      return FAIL;
   if (buffer_store == NULL || seat == NULL)
      return FAIL;

   store = allocZeroed(sizeof(*store));

   // Get keyboard
   store->keyboard = vwl_seat_get_keyboard(seat);

   if (store->keyboard == NULL)
      goto fail;

   wl_keyboard_add_listener(store->keyboard, &vwl_fs_keyboard_listener, store);

   if (vwl_display_dispatch(&vwl_display) == -1)
      goto fail;

   store->surface = wl_compositor_create_surface(vwl_gobjects.wl_compositor);
   store->shell.surface = xdg_wm_base_get_xdg_surface( vwl_gobjects.xdg_wm_base, store->surface);
   store->shell.toplevel = xdg_surface_get_toplevel(store->shell.surface);

   xdg_toplevel_set_title(store->shell.toplevel, "Eegl clipboard");

   xdg_surface_add_listener(store->shell.surface, &vwl_xdg_surface_listener, NULL);

   wl_surface_commit(store->surface);

   store->on_focus = on_focus;
   store->user_data = user_data;
   store->got_focus = FALSE;

   if (vwl_display_roundtrip(&vwl_display) == FAIL)
      goto fail;

   // We may get the enter event early, if we do then we will set `got_focus` to TRUE.
   if (store->got_focus)
      goto early_exit;

   // Book hasn't been released yet, abort. This shouldn't happen but still check for it.
   if (!buffer_store->available)
      goto fail;

   buffer_store->available = FALSE;

   wl_surface_attach(store->surface, buffer_store->buffer, 0, 0);
   wl_surface_damage(store->surface, 0, 0, buffer_store->width, buffer_store->height);
   wl_surface_commit(store->surface);

   {
   // Dispatch events until we receive the enter event. Add a max delay of
   // 'p_wtm' when waiting for it (may be longer depending on how long we
   // poll when dispatching events)
   TimeVal start, now;

   gettimeofday(&start, NULL);

   while (vwl_display_dispatch(&vwl_display) != -1) {
      if (store->got_focus)
         break;

      gettimeofday(&now, NULL);

      if ((now.tv_sec * 1000000 + now.tv_usec) - (start.tv_sec * 1000000 + start.tv_usec)
             >= p_wtm * 1000)
         goto fail;
   }
   }
early_exit:
   vwl_destroy_fs_surface(store);
   vwl_display_flush(&vwl_display);

   return OK;
fail:
   vwl_destroy_fs_surface(store);
   vwl_display_flush(&vwl_display);

   return FAIL;
}

// Called when the keyboard focus is on our surface
private void
vwl_fs_keyboard_listener_enter(
   void      *data,
   struct wl_keyboard   *keyboard UNUSED,
   uint32_t      serial,
   struct wl_surface   *surface UNUSED,
   struct wl_array   *keys UNUSED
) {
   vwl_fs_surface_T *store = data;

   store->got_focus = TRUE;

   if (store->on_focus != NULL)
      store->on_focus(store->user_data, serial);
}

// Dummy functions to handle keyboard events we don't care about.

private void
vwl_fs_keyboard_listener_keymap(
   void* data UNUSED,
   struct wl_keyboard   *keyboard UNUSED,
   uint32_t      format UNUSED,
   int         fd,
   uint32_t      size UNUSED
) {
   close(fd);
}

private void
vwl_fs_keyboard_listener_leave(
   void      *data UNUSED,
   struct wl_keyboard   *keyboard UNUSED,
   uint32_t      serial UNUSED,
   struct wl_surface   *surface UNUSED)
{
}

private void
vwl_fs_keyboard_listener_key(
   void      *data UNUSED,
   struct wl_keyboard   *keyboard UNUSED,
   uint32_t      serial UNUSED,
   uint32_t      time UNUSED,
   uint32_t      key UNUSED,
   uint32_t      state UNUSED)
{
}

private void
vwl_fs_keyboard_listener_modifiers(
    void      *data UNUSED,
    struct wl_keyboard   *keyboard UNUSED,
    uint32_t      serial UNUSED,
    uint32_t      mods_depressed UNUSED,
    uint32_t      mods_latched UNUSED,
    uint32_t      mods_locked UNUSED,
    uint32_t      group UNUSED)
{
}

private void
vwl_fs_keyboard_listener_repeat_info(
   void      *data UNUSED,
   struct wl_keyboard   *keyboard UNUSED,
   int32_t      rate UNUSED,
   int32_t      delay UNUSED)
{
}

#define VWL_CODE_DATA_OBJECT_DESTROY(type) \
do { \
    if (type == NULL || type->proxy == NULL) \
   return; \
    switch (type->protocol) \
    { \
   case VWL_DATA_PROTOCOL_WLR: \
       zwlr_data_control_##type##_v1_destroy(type->proxy); \
       break; \
   case VWL_DATA_PROTOCOL_EXT:  \
       ext_data_control_##type##_v1_destroy(type->proxy); \
       break; \
   case VWL_DATA_PROTOCOL_CORE: \
       wl_data_##type##_destroy(type->proxy); \
       break; \
   case VWL_DATA_PROTOCOL_PRIMARY: \
       zwp_primary_selection_##type##_v1_destroy(type->proxy); \
       break; \
   default: \
       break; \
    } \
    if (alloced) \
   eeglFree(type); \
    else \
   type->proxy = NULL; \
} while (0)

private void
vwl_data_device_destroy(vwl_data_device_T *device, int alloced) {
   VWL_CODE_DATA_OBJECT_DESTROY(device);
}

private void
vwl_data_offer_destroy(vwl_data_offer_T *offer, int alloced) {
   VWL_CODE_DATA_OBJECT_DESTROY(offer);
}

private void
vwl_data_source_destroy(vwl_data_source_T *source, int alloced) {
   VWL_CODE_DATA_OBJECT_DESTROY(source);
}


// Used to pass a vwl_data_offer_T struct from the data_offer event to the offer
// event and to the selection event.
private vwl_data_offer_T *tmp_vwl_offer;

// These functions handle the more complicated data_offer and selection events.

private void
vwl_gen_data_device_listener_data_offer(void *data, void *offer_proxy) {
   vwl_data_device_T *device = data;
   tmp_vwl_offer = alloc(sizeof(*tmp_vwl_offer));
   tmp_vwl_offer->proxy = offer_proxy;
   tmp_vwl_offer->protocol = device->protocol;

   vwl_data_device_listener.data_offer(device, tmp_vwl_offer);
}

private void
vwl_gen_data_device_listener_selection(
   void          *data,
   void          *offer_proxy,
   WaylandSelection selection,
   vwl_data_protocol_T protocol)
{
   if (tmp_vwl_offer == NULL) {
   // Memory allocation failed or selection cleared (data_offer is never
   // sent when selection is cleared/empty).
   vwl_data_offer_T tmp = {
       .proxy = offer_proxy,
       .protocol = protocol
   };

   vwl_data_offer_destroy(&tmp, FALSE);

   // If offer proxy is NULL then we know the selection has been cleared.
   if (offer_proxy == NULL)
       vwl_data_device_listener.selection(data, NULL, selection);
   } else {
      vwl_data_device_listener.selection(data, tmp_vwl_offer, selection);
      tmp_vwl_offer = NULL;
   }
}

// Boilerplate macros. Each just calls its respective generic callback.
#define VWL_FUNC_DATA_DEVICE_DATA_OFFER(device_name, offer_name) \
private void device_name##_listener_data_offer( \
       void *data, struct device_name *device_proxy UNUSED, \
       struct offer_name *offer_proxy) \
{ \
    vwl_gen_data_device_listener_data_offer(data, offer_proxy); \
}
#define VWL_FUNC_DATA_DEVICE_SELECTION( \
   device_name, offer_name, type, selection_type, protocol) \
   private void device_name##_listener_##type( \
      void *data, struct device_name *device_proxy UNUSED, \
      struct offer_name *offer_proxy UNUSED) \
{ \
    vwl_gen_data_device_listener_selection( \
       data, offer_proxy, selection_type, protocol); \
}
#define VWL_FUNC_DATA_DEVICE_FINISHED(device_name) \
private void device_name##_listener_finished( \
       void *data, struct device_name *device_proxy UNUSED) \
{ \
    vwl_data_device_listener.finished(data); \
}
#define VWL_FUNC_DATA_SOURCE_SEND(source_name) \
private void source_name##_listener_send(void *data, \
       struct source_name *source_proxy UNUSED, \
       const char *mime_type, int fd) \
{ \
    vwl_data_source_listener.send(data, mime_type, fd); \
}
#define VWL_FUNC_DATA_SOURCE_CANCELLED(source_name) \
private void source_name##_listener_cancelled(void *data, \
       struct source_name *source_proxy UNUSED) \
{ \
    vwl_data_source_listener.cancelled(data); \
}
#define VWL_FUNC_DATA_OFFER_OFFER(offer_name) \
private void offer_name##_listener_offer(void *data, \
       struct offer_name *offer_proxy UNUSED, \
       const char *mime_type) \
{ \
    vwl_data_offer_listener.offer(data, mime_type); \
}

VWL_FUNC_DATA_DEVICE_DATA_OFFER(
   ext_data_control_device_v1, ext_data_control_offer_v1)
VWL_FUNC_DATA_DEVICE_DATA_OFFER(
   zwlr_data_control_device_v1, zwlr_data_control_offer_v1)
VWL_FUNC_DATA_DEVICE_DATA_OFFER(wl_data_device, wl_data_offer)
VWL_FUNC_DATA_DEVICE_DATA_OFFER(
   zwp_primary_selection_device_v1, zwp_primary_selection_offer_v1)

VWL_FUNC_DATA_DEVICE_SELECTION(
   ext_data_control_device_v1, ext_data_control_offer_v1,
   selection, WAYLAND_SELECTION_REGULAR, VWL_DATA_PROTOCOL_EXT)
VWL_FUNC_DATA_DEVICE_SELECTION(
   zwlr_data_control_device_v1, zwlr_data_control_offer_v1,
   selection, WAYLAND_SELECTION_REGULAR, VWL_DATA_PROTOCOL_WLR)
VWL_FUNC_DATA_DEVICE_SELECTION(
   wl_data_device, wl_data_offer, selection,
   WAYLAND_SELECTION_REGULAR, VWL_DATA_PROTOCOL_CORE)

VWL_FUNC_DATA_DEVICE_SELECTION(
   ext_data_control_device_v1, ext_data_control_offer_v1,
   primary_selection, WAYLAND_SELECTION_PRIMARY, VWL_DATA_PROTOCOL_EXT)
VWL_FUNC_DATA_DEVICE_SELECTION(
   zwlr_data_control_device_v1, zwlr_data_control_offer_v1,
   primary_selection, WAYLAND_SELECTION_PRIMARY, VWL_DATA_PROTOCOL_WLR)
VWL_FUNC_DATA_DEVICE_SELECTION(
   zwp_primary_selection_device_v1, zwp_primary_selection_offer_v1,
   primary_selection, WAYLAND_SELECTION_PRIMARY, VWL_DATA_PROTOCOL_PRIMARY)

VWL_FUNC_DATA_DEVICE_FINISHED(ext_data_control_device_v1)
VWL_FUNC_DATA_DEVICE_FINISHED(zwlr_data_control_device_v1)

VWL_FUNC_DATA_SOURCE_SEND(ext_data_control_source_v1)
VWL_FUNC_DATA_SOURCE_SEND(zwlr_data_control_source_v1)
VWL_FUNC_DATA_SOURCE_SEND(wl_data_source)
VWL_FUNC_DATA_SOURCE_SEND(zwp_primary_selection_source_v1)

VWL_FUNC_DATA_SOURCE_CANCELLED(ext_data_control_source_v1)
VWL_FUNC_DATA_SOURCE_CANCELLED(zwlr_data_control_source_v1)
VWL_FUNC_DATA_SOURCE_CANCELLED(wl_data_source)
VWL_FUNC_DATA_SOURCE_CANCELLED(zwp_primary_selection_source_v1)

VWL_FUNC_DATA_OFFER_OFFER(ext_data_control_offer_v1)
VWL_FUNC_DATA_OFFER_OFFER(zwlr_data_control_offer_v1)
VWL_FUNC_DATA_OFFER_OFFER(wl_data_offer)
VWL_FUNC_DATA_OFFER_OFFER(zwp_primary_selection_offer_v1)

// Listener handlers

// DATA DEVICES
struct zwlr_data_control_device_v1_listener
zwlr_data_control_device_v1_listener = {
    .data_offer       = zwlr_data_control_device_v1_listener_data_offer,
    .selection       = zwlr_data_control_device_v1_listener_selection,
    .primary_selection = zwlr_data_control_device_v1_listener_primary_selection,
    .finished       = zwlr_data_control_device_v1_listener_finished
};

struct ext_data_control_device_v1_listener
ext_data_control_device_v1_listener = {
    .data_offer       = ext_data_control_device_v1_listener_data_offer,
    .selection       = ext_data_control_device_v1_listener_selection,
    .primary_selection = ext_data_control_device_v1_listener_primary_selection,
    .finished       = ext_data_control_device_v1_listener_finished
};

struct wl_data_device_listener wl_data_device_listener = {
    .data_offer       = wl_data_device_listener_data_offer,
    .selection       = wl_data_device_listener_selection,
};

struct zwp_primary_selection_device_v1_listener
zwp_primary_selection_device_v1_listener = {
    .selection   = zwp_primary_selection_device_v1_listener_primary_selection,
    .data_offer       = zwp_primary_selection_device_v1_listener_data_offer
};

// DATA SOURCES
struct zwlr_data_control_source_v1_listener
zwlr_data_control_source_v1_listener = {
    .send       = zwlr_data_control_source_v1_listener_send,
    .cancelled       = zwlr_data_control_source_v1_listener_cancelled
};

struct ext_data_control_source_v1_listener
ext_data_control_source_v1_listener = {
    .send       = ext_data_control_source_v1_listener_send,
    .cancelled       = ext_data_control_source_v1_listener_cancelled
};

struct wl_data_source_listener wl_data_source_listener = {
    .send       = wl_data_source_listener_send,
    .cancelled       = wl_data_source_listener_cancelled
};

struct zwp_primary_selection_source_v1_listener
zwp_primary_selection_source_v1_listener = {
    .send       = zwp_primary_selection_source_v1_listener_send,
    .cancelled       = zwp_primary_selection_source_v1_listener_cancelled,
};

// OFFERS
struct zwlr_data_control_offer_v1_listener
zwlr_data_control_offer_v1_listener = {
    .offer       = zwlr_data_control_offer_v1_listener_offer
};

struct ext_data_control_offer_v1_listener
ext_data_control_offer_v1_listener = {
    .offer       = ext_data_control_offer_v1_listener_offer
};

struct wl_data_offer_listener wl_data_offer_listener = {
    .offer       = wl_data_offer_listener_offer
};

struct zwp_primary_selection_offer_v1_listener
zwp_primary_selection_offer_v1_listener = {
    .offer       = zwp_primary_selection_offer_v1_listener_offer
};

// `type` is also used as the user data
#define VWL_CODE_DATA_OBJECT_ADD_LISTENER(type) \
do { \
    if (type->proxy == NULL) \
   return; \
    type->data = data; \
    switch (type->protocol) \
    { \
   case VWL_DATA_PROTOCOL_WLR: \
       zwlr_data_control_##type##_v1_add_listener( type->proxy, \
          &zwlr_data_control_##type##_v1_listener, type); \
       break; \
   case VWL_DATA_PROTOCOL_EXT: \
       ext_data_control_##type##_v1_add_listener(type->proxy, \
          &ext_data_control_##type##_v1_listener, type); \
       break; \
   case VWL_DATA_PROTOCOL_CORE: \
       wl_data_##type##_add_listener(type->proxy, \
          &wl_data_##type##_listener, type); \
       break; \
   case VWL_DATA_PROTOCOL_PRIMARY: \
       zwp_primary_selection_##type##_v1_add_listener(type->proxy, \
          &zwp_primary_selection_##type##_v1_listener, type); \
       break; \
   default: \
       break; \
    } \
} while (0)

private void
vwl_data_device_add_listener(vwl_data_device_T *device, void *data) {
    VWL_CODE_DATA_OBJECT_ADD_LISTENER(device);
}

private void
vwl_data_source_add_listener(vwl_data_source_T *source, void *data) {
    VWL_CODE_DATA_OBJECT_ADD_LISTENER(source);
}

private void
vwl_data_offer_add_listener(vwl_data_offer_T *offer, void *data) {
    VWL_CODE_DATA_OBJECT_ADD_LISTENER(offer);
}

// Sets the selection using the given data device with the given selection. If the device does
// not support the selection then nothing happens. For data control protocols the serial argument is 
// ignored.
private void
vwl_data_device_set_selection(
   vwl_data_device_T   *device,
   vwl_data_source_T   *source,
   uint32_t       serial,
   WaylandSelection selection)
{
   if (selection == WAYLAND_SELECTION_REGULAR) {
      switch (device->protocol) {
      case VWL_DATA_PROTOCOL_WLR:
         zwlr_data_control_device_v1_set_selection( device->proxy, source->proxy);
         break;
      case VWL_DATA_PROTOCOL_EXT:
         ext_data_control_device_v1_set_selection( device->proxy, source->proxy);
         break;
      case VWL_DATA_PROTOCOL_CORE:
         wl_data_device_set_selection( device->proxy, source->proxy, serial);
         break;
      default:
         break;
      }
   } ei (selection == WAYLAND_SELECTION_PRIMARY) {
      switch (device->protocol) {
      case VWL_DATA_PROTOCOL_WLR:
         zwlr_data_control_device_v1_set_primary_selection( device->proxy, source->proxy);
         break;
      case VWL_DATA_PROTOCOL_EXT:
         ext_data_control_device_v1_set_primary_selection( device->proxy, source->proxy);
         break;
      case VWL_DATA_PROTOCOL_PRIMARY:
         zwp_primary_selection_device_v1_set_selection( device->proxy, source->proxy, serial);
         break;
      default:
         break;
      }
   }
}

// Start receiving data from offer object, which sends the given fd to the
// source client to write into.
private void
vwl_data_offer_receive(vwl_data_offer_T *offer, const char *mime_type, int fd) {
   switch (offer->protocol) {
   case VWL_DATA_PROTOCOL_WLR:
      zwlr_data_control_offer_v1_receive(offer->proxy, mime_type, fd);
      break;
   case VWL_DATA_PROTOCOL_EXT:
      ext_data_control_offer_v1_receive(offer->proxy, mime_type, fd);
      break;
   case VWL_DATA_PROTOCOL_CORE:
      wl_data_offer_receive(offer->proxy, mime_type, fd);
      break;
   case VWL_DATA_PROTOCOL_PRIMARY:
      zwp_primary_selection_offer_v1_receive(offer->proxy, mime_type, fd);
      break;
   default:
      break;
   }
}

#define SET_MANAGER(manager_name, protocol_enum, focus) \
   do { \
   manager->proxy = vwl_gobjects.manager_name; \
   manager->protocol = protocol_enum; \
   return focus; \
   } while (0)

// Get a data device manager that supports the given selection. If none if found
// then the manager protocol is set to VWL_DATA_PROTOCOL_NONE. TRUE is returned
// if the given data device manager requires focus to work else FALSE.
private int
vwl_get_data_device_manager(
   vwl_data_device_manager_T   *manager,
   WaylandSelection       selection)
{
   // Prioritize data control protocols first then try using the focus steal
   // method with the core protocol data objects.
   if (force_fs)
      goto focus_steal;

   // Ext data control protocol supports both selections, try it first
   if (vwl_gobjects.ext_data_control_manager_v1 != NULL)
      SET_MANAGER(ext_data_control_manager_v1, VWL_DATA_PROTOCOL_EXT, FALSE);
   if (vwl_gobjects.zwlr_data_control_manager_v1 != NULL) {
      int ver = zwlr_data_control_manager_v1_get_version(
         vwl_gobjects.zwlr_data_control_manager_v1);

      // version 2 or greater supports the primary selection
      if ((selection == WAYLAND_SELECTION_PRIMARY && ver >= 2)
         || selection == WAYLAND_SELECTION_REGULAR)
          SET_MANAGER(zwlr_data_control_manager_v1,
             VWL_DATA_PROTOCOL_WLR, FALSE);
    }

focus_steal:
   if (vwl_focus_stealing_available()) {
      if (vwl_gobjects.wl_data_device_manager != NULL
         && selection == WAYLAND_SELECTION_REGULAR)
          SET_MANAGER(wl_data_device_manager, VWL_DATA_PROTOCOL_CORE, TRUE);

      ei (vwl_gobjects.zwp_primary_selection_device_manager_v1 != NULL
         && selection == WAYLAND_SELECTION_PRIMARY)
          SET_MANAGER(zwp_primary_selection_device_manager_v1,
             VWL_DATA_PROTOCOL_PRIMARY, TRUE);
   }

    manager->protocol = VWL_DATA_PROTOCOL_NONE;

    return FALSE;
}

// Get a data device that manages the given seat's selection.
private void
vwl_get_data_device(
   vwl_data_device_manager_T   *manager,
   vwl_seat_T          *seat,
   vwl_data_device_T       *device)
{
   switch (manager->protocol) {
   case VWL_DATA_PROTOCOL_WLR:
       device->proxy =
      zwlr_data_control_manager_v1_get_data_device(
         manager->proxy, seat->proxy);
       break;
   case VWL_DATA_PROTOCOL_EXT:
       device->proxy =
      ext_data_control_manager_v1_get_data_device(
         manager->proxy, seat->proxy);
       break;
   case VWL_DATA_PROTOCOL_CORE:
       device->proxy = wl_data_device_manager_get_data_device(
          manager->proxy, seat->proxy);
       break;
   case VWL_DATA_PROTOCOL_PRIMARY:
       device->proxy = zwp_primary_selection_device_manager_v1_get_device(
          manager->proxy, seat->proxy);
       break;
   default:
       device->protocol = VWL_DATA_PROTOCOL_NONE;
       return;
    }
    device->protocol = manager->protocol;
}

private void
vwl_create_data_source( vwl_data_device_manager_T   *manager, vwl_data_source_T* source) {
   switch (manager->protocol) {
   case VWL_DATA_PROTOCOL_WLR:
      source->proxy = zwlr_data_control_manager_v1_create_data_source(manager->proxy);
      break;
   case VWL_DATA_PROTOCOL_EXT:
      source->proxy = ext_data_control_manager_v1_create_data_source(manager->proxy);
      break;
   case VWL_DATA_PROTOCOL_CORE:
      source->proxy = wl_data_device_manager_create_data_source(manager->proxy);
      break;
   case VWL_DATA_PROTOCOL_PRIMARY:
      source->proxy = zwp_primary_selection_device_manager_v1_create_source( manager->proxy);
      break;
   default:
      source->protocol = VWL_DATA_PROTOCOL_NONE;
      return;
   }
   source->protocol = manager->protocol;
}

// Offer a new mime type to be advertised by us to other clients.
private void
vwl_data_source_offer(vwl_data_source_T *source, const char *mime_type) {
   switch (source->protocol) {
   case VWL_DATA_PROTOCOL_WLR:
      zwlr_data_control_source_v1_offer(source->proxy, mime_type);
      break;
   case VWL_DATA_PROTOCOL_EXT:
      ext_data_control_source_v1_offer(source->proxy, mime_type);
      break;
   case VWL_DATA_PROTOCOL_CORE:
      wl_data_source_offer(source->proxy, mime_type);
      break;
   case VWL_DATA_PROTOCOL_PRIMARY:
      zwp_primary_selection_source_v1_offer(source->proxy, mime_type);
      break;
   default:
      break;
   }
}

// Free the mime types arraylists in the given clip_sel struct.
private void
vwl_clipboard_free_mime_types(vwl_clipboard_selection_T *clip_sel) {
   // Don't want to be double freeing
   if (clip_sel->mime_types.c == clip_sel->tmp_mime_types.c) {
      ga_clear_strings(&clip_sel->mime_types);
      ga_init(&vwl_clipboard.primary.tmp_mime_types);
   } else {
      ga_clear_strings(&clip_sel->mime_types);
      ga_clear_strings(&clip_sel->tmp_mime_types);
   }
}

// Setup required objects to interact with Wayland selections/clipboard on given
// seat. Returns OK on success and FAIL on failure.
int
wayland_cb_init(const char *seat) {
   vwl_clipboard.seat = vwl_get_seat(seat);

   if (vwl_clipboard.seat == NULL)
      return FAIL;

   // Get data device managers for each selection. If there wasn't any manager
   // that could be found that supports the given selection, then it will be unavailable.
   vwl_clipboard.regular.requires_focus = vwl_get_data_device_manager(
       &vwl_clipboard.regular.manager,
       WAYLAND_SELECTION_REGULAR);
   vwl_clipboard.primary.requires_focus = vwl_get_data_device_manager(
       &vwl_clipboard.primary.manager,
       WAYLAND_SELECTION_PRIMARY);

   // Initialize shm pool and buffer if core data protocol is available
   if (vwl_focus_stealing_available() &&
          (vwl_clipboard.regular.requires_focus || vwl_clipboard.primary.requires_focus)
   )
      vwl_clipboard.fs_buffer = vwl_init_buffer_store(1, 1);

   // Get data devices for each selection. If one of the above function calls
   // results in an unavailable manager, then the device coming from it will
   // have its protocol set to VWL_DATA_PROTOCOL_NONE.
   vwl_get_data_device( &vwl_clipboard.regular.manager,
       vwl_clipboard.seat,
       &vwl_clipboard.regular.device);
   vwl_get_data_device(
       &vwl_clipboard.primary.manager,
       vwl_clipboard.seat,
       &vwl_clipboard.primary.device);

   // Initialize grow arrays for the offer mime types.
   // I find most applications to have below 10 mime types that they offer.
   ga_init2(&vwl_clipboard.regular.tmp_mime_types, sizeof(char*), 10);
   ga_init2(&vwl_clipboard.primary.tmp_mime_types, sizeof(char*), 10);

   // We dont need to use ga_init2 because tmp_mime_types will be copied over
   // to mime_types anyways.
   ga_init(&vwl_clipboard.regular.mime_types);
   ga_init(&vwl_clipboard.primary.mime_types);

   // Start listening for data offers/new selections. Don't do anything when we
   // get a new data offer other than saving the mime types and saving the data
   // offer. Then when we want the data we use the saved data offer to receive
   // data from it along with the saved mime_types. For each new selection just
   // destroy the previous offer/free mime_types, if any.
   vwl_data_device_add_listener(&vwl_clipboard.regular.device, &vwl_clipboard.regular);
   vwl_data_device_add_listener(&vwl_clipboard.primary.device, &vwl_clipboard.primary);

   if (vwl_display_roundtrip(&vwl_display) == FAIL) {
      wayland_cb_uninit();
      return FAIL;
   }
   clip_init(TRUE);

   return OK;
}

// Free up resources used for Wayland selections. Does not destroy global
// objects such as data device managers.
void
wayland_cb_uninit(void) {
   if (vwl_clipboard.fs_buffer != NULL) {
      vwl_destroy_buffer_store(vwl_clipboard.fs_buffer);
      vwl_clipboard.fs_buffer = NULL;
   }

   // Destroy the current offer if it exists
   vwl_data_offer_destroy(vwl_clipboard.regular.offer, TRUE);
   vwl_data_offer_destroy(vwl_clipboard.primary.offer, TRUE);

   // Destroy any devices or sources
   vwl_data_device_destroy(&vwl_clipboard.regular.device, FALSE);
   vwl_data_device_destroy(&vwl_clipboard.primary.device, FALSE);
   vwl_data_source_destroy(&vwl_clipboard.regular.source, FALSE);
   vwl_data_source_destroy(&vwl_clipboard.primary.source, FALSE);

   // Free mime types
   vwl_clipboard_free_mime_types(&vwl_clipboard.regular);
   vwl_clipboard_free_mime_types(&vwl_clipboard.primary);

   vwl_display_flush(&vwl_display);

   memset(&vwl_clipboard, 0, sizeof(vwl_clipboard));
   vwl_clipboard.regular.selection = WAYLAND_SELECTION_REGULAR;
   vwl_clipboard.primary.selection = WAYLAND_SELECTION_PRIMARY;
}

// If the given selection can be used.
private int
vwl_clipboard_selection_is_ready(vwl_clipboard_selection_T *clip_sel) {
   return clip_sel->manager.protocol != VWL_DATA_PROTOCOL_NONE 
      && clip_sel->device.protocol != VWL_DATA_PROTOCOL_NONE;
}

// Callback for data offer event. Start listening to the given offer immediately
// in order to get mime types.
private void
vwl_data_device_listener_data_offer(
   vwl_data_device_T   *device,
   vwl_data_offer_T    *offer)
{
   vwl_clipboard_selection_T *clip_sel = device->data;

   // Get mime types and save them so we can use them when we want to paste the
   // selection.
   if (clip_sel->source.proxy != NULL)
      // We own the selection, no point in getting mime types
      return;

   vwl_data_offer_add_listener(offer, device->data);
}

// Callback for offer event. Save each mime type given to be used later.
private void
vwl_data_offer_listener_offer(vwl_data_offer_T *offer, const char *mime_type) {
    vwl_clipboard_selection_T *clip_sel = offer->data;

    // Save string into temporary grow array, which will be finalized into the
    // actual grow array if the selection matches with the selection that the device manages.
    ga_copy_string(&clip_sel->tmp_mime_types, (Byte*)mime_type);
}

// Callback for selection event, for either the regular or primary selection.
// Don't try receiving data from the offer, instead destroy the previous offer
// if any and set the current offer to the given offer, along with the
// respective mime types.
private void
vwl_data_device_listener_selection(
   vwl_data_device_T   *device UNUSED,
   vwl_data_offer_T    *offer,
   WaylandSelection selection
) {
   vwl_clipboard_selection_T   *clip_sel = device->data;
   vwl_data_offer_T      *prev_offer = clip_sel->offer;

   // Save offer if it selection and clip_sel match, else discard it
   if (clip_sel->selection == selection)
      clip_sel->offer = offer;
   else {
      // Example: selection event is for the primary selection but this device
      // is only for the regular selection, if so then just discard the offer and tmp_mime_types.
      vwl_data_offer_destroy(offer, TRUE);
      tmp_vwl_offer = NULL;
      ga_clear_strings(&clip_sel->tmp_mime_types);
      return;
   }

   // There are two cases when clip_sel->offer is NULL
   // 1. No one owns the selection
   // 2. We own the selection (we'll just access the register directly)
   if (offer == NULL) {
      // Selection cleared/empty
      ga_clear_strings(&clip_sel->tmp_mime_types);
      clip_sel->offer = NULL;
      goto exit;
   } ei (clip_sel->source.proxy != NULL) {
      // We own the selection, ignore it
      vwl_data_offer_destroy(offer, TRUE);
      ga_clear_strings(&clip_sel->tmp_mime_types);
      clip_sel->offer = NULL;
      goto exit;
   }

exit:
   // Destroy previous offer if any
   vwl_data_offer_destroy(prev_offer, TRUE);
   ga_clear_strings(&clip_sel->mime_types);

   // Copy the grow array over
   clip_sel->mime_types = clip_sel->tmp_mime_types;

   // Clear tmp_mime_types so next data_offer doesn't try to resize/grow it
   // (Don't free it though using ga_clear() because mime_types->c is the same pointer)r
   if (clip_sel->offer != NULL)
      ga_init(&clip_sel->tmp_mime_types);
}

// Callback for finished event. Destroy device and all related objects/resources
// such as offers and mime types.
private void
vwl_data_device_listener_finished(vwl_data_device_T *device) {
   vwl_clipboard_selection_T *clip_sel = device->data;

   vwl_data_device_destroy(&clip_sel->device, FALSE);
   vwl_data_offer_destroy(clip_sel->offer, TRUE);
   vwl_data_source_destroy(&clip_sel->source, FALSE);
   vwl_clipboard_free_mime_types(clip_sel);
}

//Return a pointer to a grow array of mime types that the current offer
//supports sending. If the returned garray has NULL for c or a len of
//0, then the selection is cleared. If focus stealing is required, a surface
//will be created to steal focus first.
ArrayList *
wayland_cb_get_mime_types(WaylandSelection selection) {
   vwl_clipboard_selection_T *clip_sel;

   if (selection == WAYLAND_SELECTION_REGULAR)
      clip_sel = &vwl_clipboard.regular;
   ei (selection == WAYLAND_SELECTION_PRIMARY)
      clip_sel = &vwl_clipboard.primary;
   else
      return NULL;

   if (clip_sel->requires_focus) {
      // We don't care about the on_focus callback since once we gain focus
      // the data offer events will come immediately.
      if (vwl_init_fs_surface(vwl_clipboard.seat,
             vwl_clipboard.fs_buffer, NULL, NULL) == FAIL)
          return NULL;
   } ei (vwl_display_roundtrip(&vwl_display) == FAIL)
      return NULL;

   return &clip_sel->mime_types;
}

// Receive data from the given selection, and return the fd to read data from.
// On failure -1 is returned.
int
wayland_cb_receive_data(const char *mime_type, WaylandSelection selection) {
   vwl_clipboard_selection_T *clip_sel;

   // Create pipe that source client will write to
   int fds[2];

   if (selection == WAYLAND_SELECTION_REGULAR)
      clip_sel = &vwl_clipboard.regular;
   ei (selection == WAYLAND_SELECTION_PRIMARY)
      clip_sel = &vwl_clipboard.primary;
   else
      return -1;

   if (!wayland_client_is_connected(FALSE) || !vwl_clipboard_selection_is_ready(clip_sel))
      return -1;

   if (clip_sel->offer == NULL || clip_sel->offer->proxy == NULL)
      return -1;

   if (pipe(fds) == -1)
      return -1;

   vwl_data_offer_receive(clip_sel->offer, mime_type, fds[1]);

   close(fds[1]); // Close before we read data so that when the source client
         // closes their end we receive an EOF.

   if (vwl_display_flush(&vwl_display) == OK)
      return fds[0];

   close(fds[0]);

   return -1;
}

// Callback for send event. Just call the user callback which will handle it and do the writing stuff.
private void
vwl_data_source_listener_send(
   vwl_data_source_T   *source,
   const char       *mime_type,
   int32_t          fd)
{
   vwl_clipboard_selection_T *clip_sel = source->data;

   if (clip_sel->send_cb != NULL)
      clip_sel->send_cb(mime_type, fd, clip_sel->selection);
   close(fd);
}

// Callback for cancelled event, just call the user callback.
private void
vwl_data_source_listener_cancelled(vwl_data_source_T *source) {
   vwl_clipboard_selection_T *clip_sel = source->data;

   if (clip_sel->send_cb != NULL)
      clip_sel->cancelled_cb(clip_sel->selection);
   vwl_data_source_destroy(source, FALSE);
}

// Set the selection when we gain focus
private void
vwl_on_focus_set_selection(void *data, uint32_t serial) {
    vwl_clipboard_selection_T *clip_sel = data;

    vwl_data_device_set_selection(
       &clip_sel->device,
       &clip_sel->source,
       serial,
       clip_sel->selection);
    vwl_display_roundtrip(&vwl_display);
}

// Become the given selection's owner, and advertise to other clients the mime
// types found in mime_types array. Returns FAIL on failure and OK on success.
int
wayland_cb_own_selection(
   wayland_cb_send_data_func_T      send_cb,
   wayland_cb_selection_cancelled_func_T   cancelled_cb,
   const char            **mime_types,
   int               len,
   WaylandSelection         selection)
{
   vwl_clipboard_selection_T *clip_sel;

   if (selection == WAYLAND_SELECTION_REGULAR)
      clip_sel = &vwl_clipboard.regular;
   ei (selection == WAYLAND_SELECTION_PRIMARY)
      clip_sel = &vwl_clipboard.primary;
   else
      return FAIL;

   if (clip_sel->source.proxy != NULL) {
      if (selection == WAYLAND_SELECTION_PRIMARY)
          // We already own the selection, ignore (only do this for primary selection). We don't 
          // reset the selection because then we would be setting the selection every time the 
          // user moves the visual selection cursor, which is messy and inefficient.
          //
          // Eegl doesn't have a mechanism to only set the selection when the user stops selecting 
          // (such as the user releasing the mouse button in graphical Wayland applications). So 
          // this behavior in Eegl differs from other Wayland applications.
          return OK;
      ei (selection == WAYLAND_SELECTION_REGULAR) {
          // Technically we don't need to do this as we already own the selection, however if a 
          // user yanks text a second time, the text yanked won't appear in their clipboard 
          // manager if they are using one.
          //
          // This can be unexpected behaviour for the user so its probably better to do it this 
          // way. Additionally other Wayland applications seem to set the selection every time.
          //
          // There should be no noticable performance change since its not like this is running 
          // in the background constantly in Eegl, only runs once when the user yanks text to the 
          // system clipboard.
          vwl_data_source_destroy(&clip_sel->source, FALSE);
          vwl_display_flush(&vwl_display);
      } else
          // Shouldn't happen
          return FAIL;
   }

   if (!wayland_client_is_connected(FALSE) || !vwl_clipboard_selection_is_ready(clip_sel))
      return FAIL;

   clip_sel->send_cb = send_cb;
   clip_sel->cancelled_cb = cancelled_cb;

   vwl_create_data_source(&clip_sel->manager, &clip_sel->source);

   vwl_data_source_add_listener(&clip_sel->source, clip_sel);

   // Advertise mime types
   for (int i = 0; i < len; i++)
      vwl_data_source_offer(&clip_sel->source, mime_types[i]);

   if (clip_sel->requires_focus) {
      // Call set_selection later when we gain focus
      if (vwl_init_fs_surface(vwl_clipboard.seat, vwl_clipboard.fs_buffer,
             vwl_on_focus_set_selection, clip_sel) == FAIL)
          goto fail;
   } else {
      vwl_data_device_set_selection(&clip_sel->device,
         &clip_sel->source, 0, selection);
      if (vwl_display_roundtrip(&vwl_display) == FAIL)
          goto fail;
   }

   return OK;
fail:
   vwl_data_source_destroy(&clip_sel->source, FALSE);
   return FAIL;
}

// Disown the given selection, so that we are not the source client that other
// clients receive data from.
void
wayland_cb_lose_selection(WaylandSelection selection) {
   if (selection == WAYLAND_SELECTION_REGULAR)
      vwl_data_source_destroy(&vwl_clipboard.regular.source, FALSE);
   ei (selection == WAYLAND_SELECTION_PRIMARY)
      vwl_data_source_destroy(&vwl_clipboard.primary.source, FALSE);
   vwl_display_flush(&vwl_display);
}

// Return TRUE if the selection is owned by either us or another client.
int
wayland_cb_selection_is_owned(WaylandSelection selection) {
   vwl_display_roundtrip(&vwl_display);

   if (selection == WAYLAND_SELECTION_REGULAR)
      return vwl_clipboard.regular.source.proxy != NULL
          || vwl_clipboard.regular.offer != NULL;
   ei (selection == WAYLAND_SELECTION_PRIMARY)
      return vwl_clipboard.primary.source.proxy != NULL
          || vwl_clipboard.primary.offer != NULL;
   else
      return FALSE;
}

// Return TRUE if the Wayland clipboard/selections are ready to use.
int
wayland_cb_is_ready(void) {
   vwl_display_roundtrip(&vwl_display);

   // Clipboard is ready if we have at least one selection available
   return wayland_client_is_connected(TRUE) &&
       (vwl_clipboard_selection_is_ready(&vwl_clipboard.regular) ||
       vwl_clipboard_selection_is_ready(&vwl_clipboard.primary));
}

// Reload Wayland clipboard, useful if changing seat.
int
wayland_cb_reload(void) {
   // Lose any selections we own
   if (clipboard.owned)
       clip_lose_selection(&clipboard);

   wayland_cb_uninit();

   if (wayland_cb_init((char*)p_wse) == FAIL)
      return FAIL;

   return OK;
}

#endif // FEAT_WAYLAND

private int wayland_ct_restore_count = 0;

// Attempt to restore the Wayland display connection. Returns OK if display
// connection was/is now valid, else FAIL if the display connection is invalid.
int
wayland_may_restore_connection(void) {
   // No point if we still are already connected properly
   if (wayland_client_is_connected(TRUE))
      return OK;

   // No point in restoring the connection if we are exiting or dying.
   if (exiting || v_dying || wayland_ct_restore_count <= 0) {
      wayland_set_display("");
      return FAIL;
   }

   --wayland_ct_restore_count;
   wayland_uninit_client();

   return wayland_init_client(wayland_display_name);
}

// Disconnect then reconnect Wayland connection
void
c_wlrestore(Invocation *invo) {
   char *display;

   if (invo->arg == NULL || STRLEN(invo->arg) == 0)
      // Use current display name if none given
      display = wayland_display_name;
   else
      display = (char*)invo->arg;

   // Return early if shebang is not passed, we are still connected, and if not
   // changing to a new Wayland display.
   if (!invo->forceit && wayland_client_is_connected(TRUE) &&
       (display == wayland_display_name ||
        (wayland_display_name != NULL &&
         STRCMP(wayland_display_name, display) == 0)))
   return;

#ifdef FEAT_WAYLAND
   // Lose any selections we own
   if (clipboard.owned)
      clip_lose_selection(&clipboard);
#endif


   if (display)
      display = (char*)copyStr((Byte*)display);

   wayland_uninit_client();

   // Reset amount of available tries to reconnect the display to 5
   wayland_ct_restore_count = 5;

   if (wayland_init_client(display) == OK) {
      smsg(_("restoring Wayland display %s"), wayland_display_name);

#ifdef FEAT_WAYLAND
      wayland_cb_init((char*)p_wse);
#endif
   } else
      msg(_("failed restoring, lost connection to Wayland display"));

   eeglFree(display);
}

// Set wayland_display_name to display. Note that this allocate a copy of the
// string, unless NULL is passed. If NULL is passed then v:wayland_display is
// set to $WAYLAND_DISPLAY, but wayland_display_name is set to NULL.
private void
wayland_set_display(const char *display) {
   if (!display)
      display = (char*)mch_getenv((Byte*)"WAYLAND_DISPLAY");
   ei (display == wayland_display_name)
      // Don't want to be freeing vwl_display_strname then trying to copy it after.
      goto exit;

   if (display == NULL)
      // $WAYLAND_DISPLAY is not set
      display = "";

   // Leave unchanged if display is empty (but not NULL)
   if (STRCMP(display, "") != 0) {
      eeglFree(wayland_display_name);
      wayland_display_name = (char*)copyStr((Byte*)display);
   }

exit:
   set_EeglVar_string(VV_WAYLAND_DISPLAY, (Byte*)display, -1);
}

#endif // FEAT_WAYLAND

//}}}
