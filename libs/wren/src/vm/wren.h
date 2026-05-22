#ifndef wren_h
#define wren_h

//{{{common


// This header contains macros and defines used across the entire Wren
// implementation. In particular, it contains "configuration" defines that
// control how Wren works. Some of these are only used while hacking on Wren
// itself.
//
// This header is *not* intended to be included by code outside of Wren itself.

// Wren pervasively uses the C99 integer types (uint16_t, etc.) along with some
// of the associated limit constants (UINT32_MAX, etc.). The constants are not
// part of standard C++, so aren't included by default by C++ compilers when you
// include <stdint> unless __STDC_LIMIT_MACROS is defined.
#define __STDC_LIMIT_MACROS
#include <stdint.h>

// These flags let you control some details of the interpreter's implementation.
// Usually they trade-off a bit of portability for speed. They default to the
// most efficient behavior.

// Wren uses a NaN-tagged double for its core value
// representation. Otherwise, it uses a larger more conventional struct. The
// former is significantly faster and more compact. The latter is useful for
// debugging and may be more portable.

// If true, the VM's interpreter loop uses computed gotos. See this for more:
// http://gcc.gnu.org/onlinedocs/gcc-3.1.1/gcc/Labels-as-Values.html
// Enabling this speeds up the main dispatch loop a bit, but requires compiler
// support.
// see https://bullno1.com/blog/switched-goto for alternative
// Defaults to true on supported compilers.
#ifndef WREN_COMPUTED_GOTO
  #if defined(_MSC_VER) && !defined(__clang__)
    // No computed gotos in Visual Studio.
    #define WREN_COMPUTED_GOTO 0
  #else
    #define WREN_COMPUTED_GOTO 1
  #endif
#endif

// The VM includes a number of optional modules. You can choose to include
// these or not. By default, they are all available. To disable one, set the
// corresponding `WREN_OPT_<name>` define to `0`.
#ifndef WREN_OPT_META
  #define WREN_OPT_META 1
#endif

#ifndef WREN_OPT_RANDOM
  #define WREN_OPT_RANDOM 1
#endif

// These flags are useful for debugging and hacking on Wren itself. They are not
// intended to be used for production code. They default to off.

// Set this to true to stress test the GC. It will perform a collection before
// every allocation. This is useful to ensure that memory is always correctly
// reachable.
#define WREN_DEBUG_GC_STRESS 0

// Set this to true to log memory operations as they occur.
#define WREN_DEBUG_TRACE_MEMORY 0

// Set this to true to log garbage collections as they occur.
#define WREN_DEBUG_TRACE_GC 0

// Set this to true to print out the compiled bytecode of each function.
#define WREN_DEBUG_DUMP_COMPILED_CODE 0

// Set this to trace each instruction as it's executed.
#define WREN_DEBUG_TRACE_INSTRUCTIONS 0

// The maximum number of module-level variables that may be defined at one time.
// This limitation comes from the 16 bits used for the arguments to
// `CODE_LOAD_MODULE_VAR` and `CODE_STORE_MODULE_VAR`.
#define MAX_MODULE_VARS 65536

// The maximum number of arguments that can be passed to a method. Note that
// this limitation is hardcoded in other places in the VM, in particular, the
// `CODE_CALL_XX` instructions assume a certain maximum number.
#define MAX_PARAMETERS 16

// The maximum name of a method, not including the signature. This is an
// arbitrary but enforced maximum just so we know how long the method name
// strings need to be in the parser.
#define MAX_METHOD_NAME 64

// The maximum length of a method signature. Signatures look like:
//
//     foo        // Getter.
//     foo()      // No-argument method.
//     foo(_)     // One-argument method.
//     foo(_,_)   // Two-argument method.
//     init foo() // Constructor initializer.
//
// The maximum signature length takes into account the longest method name, the
// maximum number of parameters with separators between them, "init ", and "()".
#define MAX_METHOD_SIGNATURE (MAX_METHOD_NAME + (MAX_PARAMETERS * 2) + 6)

// The maximum length of an identifier. The only real reason for this limitation
// is so that error messages mentioning variables can be stack allocated.
#define MAX_VARIABLE_NAME 64

// The maximum number of fields a class can have, including inherited fields.
// This is explicit in the bytecode since `CODE_CLASS` and `CODE_SUBCLASS` take
// a single byte for the number of fields. Note that it's 255 and not 256
// because creating a class takes the *number* of fields, not the *highest
// field index*.
#define MAX_FIELDS 255

// The maximum number of methods the vm can handle.
#define MAX_METHODS 65535

// Use the VM's allocator to allocate an object of [type].
#define ALLOCATE(vm, type)                                                     \
    ((type*)wrenReallocate(vm, NULL, 0, sizeof(type)))

// Use the VM's allocator to allocate an object of [mainType] containing a
// flexible array of [count] objects of [arrayType].
#define ALLOCATE_FLEX(vm, mainType, arrayType, count)                          \
    ((mainType*)wrenReallocate(vm, NULL, 0,                                    \
        sizeof(mainType) + sizeof(arrayType) * (count)))

// Use the VM's allocator to allocate an array of [count] elements of [type].
#define ALLOCATE_ARRAY(vm, type, count)                                        \
    ((type*)wrenReallocate(vm, NULL, 0, sizeof(type) * (count)))

// Use the VM's allocator to free the previously allocated memory at [pointer].
#define DEALLOCATE(vm, pointer) wrenReallocate(vm, pointer, 0, 0)

// The Microsoft compiler does not support the "inline" modifier when compiling
// as plain C.
#if defined( _MSC_VER ) && !defined(__cplusplus)
  #define inline _inline
#endif

// This is used to clearly mark flexible-sized arrays that appear at the end of
// some dynamically-allocated structs, known as the "struct hack".
#if __STDC_VERSION__ >= 199901L
  // In C99, a flexible array member is just "[]".
  #define FLEXIBLE_ARRAY
#else
  // Elsewhere, use a zero-sized array. It's technically undefined behavior,
  // but works reliably in most known compilers.
  #define FLEXIBLE_ARRAY 0
#endif

// Assertions are used to validate program invariants. They indicate things the
// program expects to be true about its internal state during execution. If an
// assertion fails, there is a bug in Wren.
//
// Assertions add significant overhead, so are only enabled in debug builds.
#ifdef DEBUG

  #include <stdio.h>

  #define ASSERT(condition, message)                                           \
      do                                                                       \
      {                                                                        \
        if (!(condition))                                                      \
        {                                                                      \
          fprintf(stderr, "[%s:%d] Assert failed in %s(): %s\n",               \
              __FILE__, __LINE__, __func__, message);                          \
          abort();                                                             \
        }                                                                      \
      } while (false)

  // Indicates that we know execution should never reach this point in the
  // program. In debug mode, we assert this fact because it's a bug to get here.
  //
  // In release mode, we use compiler-specific built in functions to tell the
  // compiler the code can't be reached. This avoids "missing return" warnings
  // in some cases and also lets it perform some optimizations by assuming the
  // code is never reached.
  #define UNREACHABLE()                                                        \
      do                                                                       \
      {                                                                        \
        fprintf(stderr, "[%s:%d] This code should not be reached in %s()\n",   \
            __FILE__, __LINE__, __func__);                                     \
        abort();                                                               \
      } while (false)

#else

  #define ASSERT(condition, message) do { } while (false)

  // Tell the compiler that this part of the code will never be reached.
  #if defined( _MSC_VER )
    #define UNREACHABLE() __assume(0)
  #elif (__GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 5))
    #define UNREACHABLE() __builtin_unreachable()
  #else
    #define UNREACHABLE()
  #endif

#endif

//}}}
//{{{main
// The maximum number of temporary objects that can be made visible to the GC at one time.
#define WREN_MAX_TEMP_ROOTS 8

typedef enum {
  #define OPCODE(name, _) CODE_##name,
  #include "wrenOpcodes.h"
  #undef OPCODE
} Code;

// A handle to a value, basically just a linked list of extra GC roots.
//
// Note that even non-heap-allocated values can be stored here.
struct WrenHandle {
  Value value;

  WrenHandle* prev;
  WrenHandle* next;
};

struct WrenVM {
  ObjClass* boolClass;
  ObjClass* classClass;
  ObjClass* fiberClass;
  ObjClass* fnClass;
  ObjClass* listClass;
  ObjClass* mapClass;
  ObjClass* nullClass;
  ObjClass* numClass;
  ObjClass* objectClass;
  ObjClass* rangeClass;
  ObjClass* stringClass;

  // The fiber that is currently running.
  ObjFiber* fiber;

  // The loaded modules. Each key is an ObjString (except for the main module,
  // whose key is null) for the module's name and the value is the ObjModule
  // for the module.
  ObjMap* modules;
  
  // The most recently imported module. More specifically, the module whose
  // code has most recently finished executing.
  //
  // Not treated like a GC root since the module is already in [modules].
  ObjModule* lastModule;

  // Memory management data:

  // The number of bytes that are known to be currently allocated. Includes all
  // memory that was proven live after the last GC, as well as any new bytes
  // that were allocated since then. Does *not* include bytes for objects that
  // were freed since the last GC.
  size_t bytesAllocated;

  // The number of total allocated bytes that will trigger the next GC.
  size_t nextGC;

  // The first object in the linked list of all currently allocated objects.
  Obj* first;

  // The "gray" set for the garbage collector. This is the stack of unprocessed
  // objects while a garbage collection pass is in process.
  Obj** gray;
  int grayCount;
  int grayCapacity;

  // The list of temporary roots. This is for temporary or new objects that are
  // not otherwise reachable but should not be collected.
  //
  // They are organized as a stack of pointers stored in this array. This
  // implies that temporary roots need to have stack semantics: only the most
  // recently pushed object can be released.
  Obj* tempRoots[WREN_MAX_TEMP_ROOTS];

  int numTempRoots;
  
  // Pointer to the first node in the linked list of active handles or NULL if
  // there are none.
  WrenHandle* handles;
  
  // Pointer to the bottom of the range of stack slots available for use from
  // the C API. During a foreign method, this will be in the stack of the fiber
  // that is executing a method.
  //
  // If not in a foreign method, this is initially NULL. If the user requests
  // slots by calling wrenEnsureSlots(), a stack is created and this is
  // initialized.
  Value* apiStack;

  WrenConfiguration config;
  
  // Compiler and debugger data:

  // The compiler that is currently compiling code. This is used so that heap
  // allocated objects used by the compiler can be found if a GC is kicked off
  // in the middle of a compile.
  Compiler* compiler;

  // There is a single global symbol table for all method names on all classes.
  // Method calls are dispatched directly by index in this table.
  SymbolTable methodNames;
};

// A generic allocation function that handles all explicit memory management.
// It's used like so:
//
// - To allocate new memory, [memory] is NULL and [oldSize] is zero. It should
//   return the allocated memory or NULL on failure.
//
// - To attempt to grow an existing allocation, [memory] is the memory,
//   [oldSize] is its previous size, and [newSize] is the desired size.
//   It should return [memory] if it was able to grow it in place, or a new
//   pointer if it had to move it.
//
// - To shrink memory, [memory], [oldSize], and [newSize] are the same as above
//   but it will always return [memory].
//
// - To free memory, [memory] will be the memory to free and [newSize] and
//   [oldSize] will be zero. It should return NULL.
void* wrenReallocate(WrenVM* vm, void* memory, size_t oldSize, size_t newSize);

// Invoke the finalizer for the foreign object referenced by [foreign].
void wrenFinalizeForeign(WrenVM* vm, ObjForeign* foreign);

// Creates a new [WrenHandle] for [value].
WrenHandle* wrenMakeHandle(WrenVM* vm, Value value);

// Compile [source] in the context of [module] and wrap in a fiber that can
// execute it.
//
// Returns NULL if a compile error occurred.
ObjClosure* wrenCompileSource(WrenVM* vm, const char* module,
                              const char* source, bool isExpression,
                              bool printErrors);

// Looks up a variable from a previously-loaded module.
//
// Aborts the current fiber if the module or variable could not be found.
Value wrenGetModuleVariable(WrenVM* vm, Value moduleName, Value variableName);

// Returns the value of the module-level variable named [name] in the main
// module.
Value wrenFindVariable(WrenVM* vm, ObjModule* module, const char* name);

// Adds a new implicitly declared top-level variable named [name] to [module]
// based on a use site occurring on [line].
//
// Does not check to see if a variable with that name is already declared or
// defined. Returns the symbol for the new variable or -2 if there are too many
// variables defined.
int wrenDeclareVariable(WrenVM* vm, ObjModule* module, const char* name,
                        size_t length, int line);

// Adds a new top-level variable named [name] to [module], and optionally
// populates line with the line of the implicit first use (line can be NULL).
//
// Returns the symbol for the new variable, -1 if a variable with the given name
// is already defined, or -2 if there are too many variables defined.
// Returns -3 if this is a top-level lowercase variable (localname) that was
// used before being defined.
int wrenDefineVariable(WrenVM* vm, ObjModule* module, const char* name,
                       size_t length, Value value, int* line);

// Marks [obj] as a GC root so that it doesn't get collected.
void wrenPushRoot(WrenVM* vm, Obj* obj);

// Removes the most recently pushed temporary root.
void wrenPopRoot(WrenVM* vm);


//This module defines the built-in classes and their primitives methods that
//are implemented directly in C code. Some languages try to implement as much
//of the core module itself in the primary language instead of in the host language.
//
//With Wren, we try to do as much of it in C as possible. Primitive methods
//are always faster than code written in Wren, and it minimizes startup time
//since we don't have to parse, compile, and execute Wren code.
//
//There is one limitation, though. Methods written in C cannot call Wren ones.
//They can only be the top of the callstack, and immediately return. This
//makes it difficult to have primitive methods that rely on polymorphic
//behavior. For example, `System.print` should call `toString` on its argument,
//including user-defined `toString` methods on user-defined classes.

void wrenInitializeCore(WrenVM* vm);

//}}}
//{{{value

#include <stdbool.h>
#include <string.h>

//This defines the built-in types and their core representations in memory.
//Since Wren is dynamically typed, any variable can hold a value of any type,
//and the type can change at runtime. Implementing this efficiently is critical for performance.
//
// The main type exposed by this is [Value]. A C variable of that type is a
// storage location that can hold any Wren value. The stack, module variables,
// and instance fields are all implemented in C as variables of type Value.
//
// The built-in types for booleans, numbers, and null are unboxed: their value
// is stored directly in the Value, and copying a Value copies the value. Other
// types--classes, instances of classes, functions, lists, and strings--are all
// reference types. They are stored on the heap and the Value just stores a
// pointer to it. Copying the Value copies a reference to the same object. The
// Wren implementation calls these "Obj", or objects, though to a user, all
// values are objects.
//
// There is also a special singleton value "undefined". It is used internally
// but never appears as a real value to a user. It has two uses:
//
// - It is used to identify module variables that have been implicitly declared
//   by use in a forward reference but not yet explicitly declared. These only
//   exist during compilation and do not appear at runtime.
//
// - It is used to represent unused map entries in an ObjMap.
//
// There are two supported Value representations. The main one uses a technique
// called "NaN tagging" (explained in detail below) to store a number, any of
// the value types, or a pointer, all inside one double-precision floating
// point number. A larger, slower, Value type that uses a struct to store these
// is also supported, and is useful for debugging the VM.

// These macros cast a Value to one of the specific object types. These do *not*
// perform any validation, so must only be used after the Value has been
// ensured to be the right type.
#define AS_CLASS(value)     ((ObjClass*)AS_OBJ(value))          // ObjClass*
#define AS_CLOSURE(value)   ((ObjClosure*)AS_OBJ(value))        // ObjClosure*
#define AS_FIBER(v)         ((ObjFiber*)AS_OBJ(v))              // ObjFiber*
#define AS_FN(value)        ((ObjFn*)AS_OBJ(value))             // ObjFn*
#define AS_FOREIGN(v)       ((ObjForeign*)AS_OBJ(v))            // ObjForeign*
#define AS_INSTANCE(value)  ((ObjInstance*)AS_OBJ(value))       // ObjInstance*
#define AS_LIST(value)      ((ObjList*)AS_OBJ(value))           // ObjList*
#define AS_MAP(value)       ((ObjMap*)AS_OBJ(value))            // ObjMap*
#define AS_MODULE(value)    ((ObjModule*)AS_OBJ(value))         // ObjModule*
#define AS_NUM(value)       (wrenValueToNum(value))             // double
#define AS_RANGE(v)         ((ObjRange*)AS_OBJ(v))              // ObjRange*
#define AS_STRING(v)        ((ObjString*)AS_OBJ(v))             // ObjString*
#define AS_CSTRING(v)       (AS_STRING(v)->value)               // const char*

// These macros promote a primitive C value to a full Wren Value. There are
// more defined below that are specific to the Nan tagged or other
// representation.
#define BOOL_VAL(boolean) ((boolean) ? TRUE_VAL : FALSE_VAL)    // boolean
#define NUM_VAL(num) (wrenNumToValue(num))                      // double
#define OBJ_VAL(obj) (wrenObjectToValue((Obj*)(obj)))           // Any Obj___*

// These perform type tests on a Value, returning `true` if the Value is of the
// given type.
#define IS_BOOL(value) (wrenIsBool(value))                      // Bool
#define IS_CLASS(value) (wrenIsObjType(value, OBJ_CLASS))       // ObjClass
#define IS_CLOSURE(value) (wrenIsObjType(value, OBJ_CLOSURE))   // ObjClosure
#define IS_FIBER(value) (wrenIsObjType(value, OBJ_FIBER))       // ObjFiber
#define IS_FN(value) (wrenIsObjType(value, OBJ_FN))             // ObjFn
#define IS_FOREIGN(value) (wrenIsObjType(value, OBJ_FOREIGN))   // ObjForeign
#define IS_INSTANCE(value) (wrenIsObjType(value, OBJ_INSTANCE)) // ObjInstance
#define IS_LIST(value) (wrenIsObjType(value, OBJ_LIST))         // ObjList
#define IS_MAP(value) (wrenIsObjType(value, OBJ_MAP))           // ObjMap
#define IS_RANGE(value) (wrenIsObjType(value, OBJ_RANGE))       // ObjRange
#define IS_STRING(value) (wrenIsObjType(value, OBJ_STRING))     // ObjString

// Creates a new string object from [text], which should be a bare C string
// literal. This determines the length of the string automatically at compile
// time based on the size of the character array (-1 for the terminating '\0').
#define CONST_STRING(vm, text) wrenNewStringLength((vm), (text), sizeof(text) - 1)

// Identifies which specific type a heap-allocated object is.
typedef enum {
  OBJ_CLASS,
  OBJ_CLOSURE,
  OBJ_FIBER,
  OBJ_FN,
  OBJ_FOREIGN,
  OBJ_INSTANCE,
  OBJ_LIST,
  OBJ_MAP,
  OBJ_MODULE,
  OBJ_RANGE,
  OBJ_STRING,
  OBJ_UPVALUE
} ObjType;

typedef struct sObjClass ObjClass;

// Base struct for all heap-allocated objects.
typedef struct sObj Obj;
struct sObj {
  ObjType type;
  bool isDark;

  // The object's class.
  ObjClass* classObj;

  // The next object in the linked list of all currently allocated objects.
  struct sObj* next;
};

typedef uint64_t Value;

DECLARE_BUFFER(Value, Value);

// A heap-allocated string object.
struct sObjString {
  Obj obj;

  // Number of bytes in the string, not including the null terminator.
  uint32_t length;

  // The hash value of the string's contents.
  uint32_t hash;

  // Inline array of the string's bytes followed by a null terminator.
  char value[FLEXIBLE_ARRAY];
};

// The dynamically allocated data structure for a variable that has been used
// by a closure. Whenever a function accesses a variable declared in an
// enclosing function, it will get to it through this.
//
// An upvalue can be either "closed" or "open". An open upvalue points directly
// to a [Value] that is still stored on the fiber's stack because the local
// variable is still in scope in the function where it's declared.
//
// When that local variable goes out of scope, the upvalue pointing to it will
// be closed. When that happens, the value gets copied off the stack into the
// upvalue itself. That way, it can have a longer lifetime than the stack
// variable.
typedef struct sObjUpvalue {
  // The object header. Note that upvalues have this because they are garbage
  // collected, but they are not first class Wren objects.
  Obj obj;

  // Pointer to the variable this upvalue is referencing.
  Value* value;

  // If the upvalue is closed (i.e. the local variable it was pointing to has
  // been popped off the stack) then the closed-over value will be hoisted out
  // of the stack into here. [value] will then be changed to point to this.
  Value closed;

  // Open upvalues are stored in a linked list by the fiber. This points to the
  // next upvalue in that list.
  struct sObjUpvalue* next;
} ObjUpvalue;

// The type of a primitive function.
//
// Primitives are similar to foreign functions, but have more direct access to
// VM internals. It is passed the arguments in [args]. If it returns a value,
// it places it in `args[0]` and returns `true`. If it causes a runtime error
// or modifies the running fiber, it returns `false`.
typedef bool (*Primitive)(WrenVM* vm, Value* args);

// TODO: See if it's actually a perf improvement to have this in a separate
// struct instead of in ObjFn.
// Stores debugging information for a function used for things like stack traces.
typedef struct {
  // The name of the function. Heap allocated and owned by the FnDebug.
  char* name;

  // An array of line numbers. There is one element in this array for each
  // bytecode in the function's bytecode array. The value of that element is
  // the line in the source code that generated that instruction.
  IntBuffer sourceLines;
} FnDebug;

// A loaded module and the top-level variables it defines.
//
// While this is an Obj and is managed by the GC, it never appears as a
// first-class object in Wren.
typedef struct {
  Obj obj;

  // The currently defined top-level variables.
  ValueBuffer variables;

  // Symbol table for the names of all module variables. Indexes here directly
  // correspond to entries in [variables].
  SymbolTable variableNames;

  // The name of the module.
  ObjString* name;
} ObjModule;

// A function object. It wraps and owns the bytecode and other debug information
// for a callable chunk of code.
//
// Function objects are not passed around and invoked directly. Instead, they
// are always referenced by an [ObjClosure] which is the real first-class
// representation of a function. This isn't strictly necessary if they function
// has no upvalues, but lets the rest of the VM assume all called objects will
// be closures.
typedef struct {
  Obj obj;
  
  ByteBuffer code;
  ValueBuffer constants;
  
  // The module where this function was defined.
  ObjModule* module;

  // The maximum number of stack slots this function may use.
  int maxSlots;
  
  // The number of upvalues this function closes over.
  int numUpvalues;
  
  // The number of parameters this function expects. Used to ensure that .call
  // handles a mismatch between number of parameters and arguments. This will
  // only be set for fns, and not ObjFns that represent methods or scripts.
  int arity;
  FnDebug* debug;
} ObjFn;

// An instance of a first-class function and the environment it has closed over.
// Unlike [ObjFn], this has captured the upvalues that the function accesses.
typedef struct {
  Obj obj;

  // The function that this closure is an instance of.
  ObjFn* fn;

  // The upvalues this function has closed over.
  ObjUpvalue* upvalues[FLEXIBLE_ARRAY];
} ObjClosure;

typedef struct {
  // Pointer to the current (really next-to-be-executed) instruction in the
  // function's bytecode.
  uint8_t* ip;
  
  // The closure being executed.
  ObjClosure* closure;
  
  // Pointer to the first stack slot used by this call frame. This will contain
  // the receiver, followed by the function's parameters, then local variables
  // and temporaries.
  Value* stackStart;
} CallFrame;

// Tracks how this fiber has been invoked, aside from the ways that can be
// detected from the state of other fields in the fiber.
typedef enum {
  // The fiber is being run from another fiber using a call to `try()`.
  FIBER_TRY,
  
  // The fiber was directly invoked by `runInterpreter()`. This means it's the
  // initial fiber used by a call to `wrenCall()` or `wrenInterpret()`.
  FIBER_ROOT,
  
  // The fiber is invoked some other way. If [caller] is `NULL` then the fiber
  // was invoked using `call()`. If [numFrames] is zero, then the fiber has
  // finished running and is done. If [numFrames] is one and that frame's `ip`
  // points to the first byte of code, the fiber has not been started yet.
  FIBER_OTHER,
} FiberState;

typedef struct sObjFiber {
  Obj obj;
  
  // The stack of value slots. This is used for holding local variables and
  // temporaries while the fiber is executing. It is heap-allocated and grown
  // as needed.
  Value* stack;
  
  // A pointer to one past the top-most value on the stack.
  Value* stackTop;
  
  // The number of allocated slots in the stack array.
  int stackCapacity;
  
  // The stack of call frames. This is a dynamic array that grows as needed but
  // never shrinks.
  CallFrame* frames;
  
  // The number of frames currently in use in [frames].
  int numFrames;
  
  // The number of [frames] allocated.
  int frameCapacity;
  
  // Pointer to the first node in the linked list of open upvalues that are
  // pointing to values still on the stack. The head of the list will be the
  // upvalue closest to the top of the stack, and then the list works downwards.
  ObjUpvalue* openUpvalues;
  
  // The fiber that ran this one. If this fiber is yielded, control will resume
  // to this one. May be `NULL`.
  struct sObjFiber* caller;
  
  // If the fiber failed because of a runtime error, this will contain the
  // error object. Otherwise, it will be null.
  Value error;
  
  FiberState state;
} ObjFiber;

typedef enum {
  // A primitive method implemented in C in the VM. Unlike foreign methods,
  // this can directly manipulate the fiber's stack.
  METHOD_PRIMITIVE,

  // A primitive that handles .call on Fn.
  METHOD_FUNCTION_CALL,

  // A externally-defined C method.
  METHOD_FOREIGN,

  // A normal user-defined method.
  METHOD_BLOCK,
  
  // No method for the given symbol.
  METHOD_NONE
} MethodType;

typedef struct {
  MethodType type;

  // The method function itself. The [type] determines which field of the union is used.
  union {
    Primitive primitive;
    WrenForeignMethodFn foreign;
    ObjClosure* closure;
  } as;
} Method;

DECLARE_BUFFER(Method, Method);

struct sObjClass {
  Obj obj;
  ObjClass* superclass;

  // The number of fields needed for an instance of this class, including all
  // of its superclass fields.
  int numFields;

  // The table of methods that are defined in or inherited by this class.
  // Methods are called by symbol, and the symbol directly maps to an index in
  // this table. This makes method calls fast at the expense of empty cells in
  // the list for methods the class doesn't support.
  //
  // You can think of it as a hash table that never has collisions but has a
  // really low load factor. Since methods are pretty small (just a type and a
  // pointer), this should be a worthwhile trade-off.
  MethodBuffer methods;

  // The name of the class.
  ObjString* name;
  
  // The ClassAttribute for the class, if any
  Value attributes;
};

typedef struct {
  Obj obj;
  uint8_t data[FLEXIBLE_ARRAY];
} ObjForeign;

typedef struct {
  Obj obj;
  Value fields[FLEXIBLE_ARRAY];
} ObjInstance;

typedef struct {
  Obj obj;

  // The elements in the list.
  ValueBuffer elements;
} ObjList;

typedef struct {
   //The entry's key, or UNDEFINED_VAL if the entry is not in use.
   Value key;

   //The value associated with the key. If the key is UNDEFINED_VAL, this will
   //be false to indicate an open available entry or true to indicate a
   //tombstone -- an entry that was previously in use but was then deleted.
   Value value;
} MapEntry;

// A hash table mapping keys to values.
//
// We use something very simple: open addressing with linear probing. The hash
// table is an array of entries. Each entry is a key-value pair. If the key is
// the special UNDEFINED_VAL, it indicates no value is currently in that slot.
// Otherwise, it's a valid key, and the value is the value associated with it.
//
// When entries are added, the array is dynamically scaled by GROW_FACTOR to
// keep the number of filled slots under MAP_LOAD_PERCENT. Likewise, if the map
// gets empty enough, it will be resized to a smaller array. When this happens,
// all existing entries are rehashed and re-added to the new array.
//
// When an entry is removed, its slot is replaced with a "tombstone". This is an
// entry whose key is UNDEFINED_VAL and whose value is TRUE_VAL. When probing
// for a key, we will continue past tombstones, because the desired key may be
// found after them if the key that was removed was part of a prior collision.
// When the array gets resized, all tombstones are discarded.
typedef struct {
  Obj obj;

  // The number of entries allocated.
  uint32_t capacity;

  // The number of entries in the map.
  uint32_t count;

  // Pointer to a contiguous array of [capacity] entries.
  MapEntry* entries;
} ObjMap;

typedef struct {
  Obj obj;

  // The beginning of the range.
  double from;

  // The end of the range. May be greater or less than [from].
  double to;

  // True if [to] is included in the range.
  bool isInclusive;
} ObjRange;

// Nan-tagging. An IEEE 754 double-precision float is a 64-bit value with bits laid out like:
//
// 1 Sign bit
// |11 Exponent bits
// ||         |+------52 Mantissa (i.e. fraction) bits------------+
// ||         ||                                                  |
// S[Exponent-][Mantissa------------------------------------------]
//
// The details of how these are used to represent numbers aren't really
// relevant here as long we don't interfere with them. The important bit is NaN.
//
// An IEEE double can represent a few magical values like NaN ("not a number"),
// Infinity, and -Infinity. A NaN is any value where all exponent bits are set:
//
//  v--NaN bits
// -11111111111----------------------------------------------------
//
// Here, "-" means "doesn't matter". Any bit sequence that matches the above is a NaN. With all of
// those "-", it obvious there are a *lot* of different bit patterns that all mean the same thing.
// NaN tagging takes advantage of this. We'll use those available bit patterns to represent things
// other than numbers without giving up any valid numeric values.
//
// NaN values come in two flavors: "signalling" and "quiet". The former are intended to halt 
// execution, while the latter just flow through arithmetic operations silently. We want the latter.
// Quiet NaNs are indicated by setting the highest mantissa bit:
//
//             v--Highest mantissa bit
// -[NaN      ]1---------------------------------------------------
//
//If all of the NaN bits are set, it's not a number. Otherwise, it is. That leaves all of the 
//remaining bits available for us to play with. We stuff a few different kinds of things here: 
//special singleton values like "true", "false", and "null", and pointers to objects allocated on 
//the heap. We'll use the sign bit to distinguish singleton values from pointers. If
//it's set, it's a pointer.
//
// v--Pointer or singleton?
// S[NaN      ]1---------------------------------------------------
//
//For singleton values, we just enumerate the different values. We'll use the
//low bits of the mantissa for that, and only need a few:
//
//                                                 3 Type bits--v
// 0[NaN      ]1------------------------------------------------[T]
//
//For pointers, we are left with 51 bits of mantissa to store an address. That's more than enough 
//room for a 32-bit address. Even 64-bit machines only actually use 48 bits for addresses, so 
//we've got plenty. We just stuff the address right into the mantissa.
//
//Ta-da, double precision numbers, pointers, and a bunch of singleton values, all stuffed into a 
//single 64-bit sequence. Even better, we don't have to do any masking or work to extract number 
//values: they are unmodified. This means math on numbers is fast.

// A mask that selects the sign bit.
#define SIGN_BIT ((uint64_t)1 << 63)

// The bits that must be set to indicate a quiet NaN.
#define QNAN ((uint64_t)0x7ffc000000000000)

// If the NaN bits are set, it's not a number.
#define IS_NUM(value) (((value) & QNAN) != QNAN)

// An object pointer is a NaN with a set sign bit.
#define IS_OBJ(value) (((value) & (QNAN | SIGN_BIT)) == (QNAN | SIGN_BIT))

#define IS_FALSE(value)     ((value) == FALSE_VAL)
#define IS_NULL(value)      ((value) == NULL_VAL)
#define IS_UNDEFINED(value) ((value) == UNDEFINED_VAL)

// Masks out the tag bits used to identify the singleton value.
#define MASK_TAG (7)

// Tag values for the different singleton values.
#define TAG_NAN       (0)
#define TAG_NULL      (1)
#define TAG_FALSE     (2)
#define TAG_TRUE      (3)
#define TAG_UNDEFINED (4)
#define TAG_UNUSED2   (5)
#define TAG_UNUSED3   (6)
#define TAG_UNUSED4   (7)

// Value -> 0 or 1.
#define AS_BOOL(value) ((value) == TRUE_VAL)

// Value -> Obj*.
#define AS_OBJ(value) ((Obj*)(uintptr_t)((value) & ~(SIGN_BIT | QNAN)))

// Singleton values.
#define NULL_VAL      ((Value)(uint64_t)(QNAN | TAG_NULL))
#define FALSE_VAL     ((Value)(uint64_t)(QNAN | TAG_FALSE))
#define TRUE_VAL      ((Value)(uint64_t)(QNAN | TAG_TRUE))
#define UNDEFINED_VAL ((Value)(uint64_t)(QNAN | TAG_UNDEFINED))

// Gets the singleton type tag for a Value (which must be a singleton).
#define GET_TAG(value) ((int)((value) & MASK_TAG))

// Creates a new "raw" class. It has no metaclass or superclass whatsoever.
// This is only used for bootstrapping the initial Object and Class classes,
// which are a little special.
ObjClass* wrenNewSingleClass(WrenVM* vm, int numFields, ObjString* name);

// Makes [superclass] the superclass of [subclass], and causes subclass to
// inherit its methods. This should be called before any methods are defined
// on subclass.
void wrenBindSuperclass(WrenVM* vm, ObjClass* subclass, ObjClass* superclass);

// Creates a new class object as well as its associated metaclass.
ObjClass* wrenNewClass(WrenVM* vm, ObjClass* superclass, int numFields, ObjString* name);

void wrenBindMethod(WrenVM* vm, ObjClass* classObj, int symbol, Method method);

// Creates a new closure object that invokes [fn]. Allocates room for its
// upvalues, but assumes outside code will populate it.
ObjClosure* wrenNewClosure(WrenVM* vm, ObjFn* fn);

// Creates a new fiber object that will invoke [closure].
ObjFiber* wrenNewFiber(WrenVM* vm, ObjClosure* closure);


// Ensures [fiber]'s stack has at least [needed] slots.
void wrenEnsureStack(WrenVM* vm, ObjFiber* fiber, int needed);

ObjForeign* wrenNewForeign(WrenVM* vm, ObjClass* classObj, size_t size);

// Creates a new empty function. Before being used, it must have code,
// constants, etc. added to it.
ObjFn* wrenNewFunction(WrenVM* vm, ObjModule* module, int maxSlots);

void wrenFunctionBindName(WrenVM* vm, ObjFn* fn, const char* name, int length);

// Creates a new instance of the given [classObj].
Value wrenNewInstance(WrenVM* vm, ObjClass* classObj);

// Creates a new list with [numElements] elements (which are left
// uninitialized.)
ObjList* wrenNewList(WrenVM* vm, uint32_t numElements);

// Inserts [value] in [list] at [index], shifting down the other elements.
void wrenListInsert(WrenVM* vm, ObjList* list, Value value, uint32_t index);

// Removes and returns the item at [index] from [list].
Value wrenListRemoveAt(WrenVM* vm, ObjList* list, uint32_t index);

// Searches for [value] in [list], returns the index or -1 if not found.
int wrenListIndexOf(WrenVM* vm, ObjList* list, Value value);

// Creates a new empty map.
ObjMap* wrenNewMap(WrenVM* vm);

// Validates that [arg] is a valid object for use as a map key. Returns true if
// it is and returns false otherwise. Use validateKey usually, for a runtime error.
// This separation exists to aid the API in surfacing errors to the developer as well.
bool wrenMapIsValidKey(Value arg);

// Looks up [key] in [map]. If found, returns the value. Otherwise, returns
// `UNDEFINED_VAL`.
Value wrenMapGet(ObjMap* map, Value key);

// Associates [key] with [value] in [map].
void wrenMapSet(WrenVM* vm, ObjMap* map, Value key, Value value);

void wrenMapClear(WrenVM* vm, ObjMap* map);

// Removes [key] from [map], if present. Returns the value for the key if found
// or `NULL_VAL` otherwise.
Value wrenMapRemoveKey(WrenVM* vm, ObjMap* map, Value key);

// Creates a new module.
ObjModule* wrenNewModule(WrenVM* vm, ObjString* name);

// Creates a new range from [from] to [to].
Value wrenNewRange(WrenVM* vm, double from, double to, bool isInclusive);

// Creates a new string object and copies [text] into it.
//
// [text] must be non-NULL.
Value wrenNewString(WrenVM* vm, const char* text);

// Creates a new string object of [length] and copies [text] into it.
//
// [text] may be NULL if [length] is zero.
Value wrenNewStringLength(WrenVM* vm, const char* text, size_t length);

// Creates a new string object by taking a range of characters from [source].
// The range starts at [start], contains [count] bytes, and increments by
// [step].
Value wrenNewStringFromRange(WrenVM* vm, ObjString* source, int start,
                             uint32_t count, int step);

// Produces a string representation of [value].
Value wrenNumToString(WrenVM* vm, double value);

// Creates a new formatted string from [format] and any additional arguments
// used in the format string.
//
// This is a very restricted flavor of formatting, intended only for internal
// use by the VM. Two formatting characters are supported, each of which reads
// the next argument as a certain type:
//
// $ - A C string.
// @ - A Wren string object.
Value wrenStringFormat(WrenVM* vm, const char* format, ...);

// Creates a new string containing the UTF-8 encoding of [value].
Value wrenStringFromCodePoint(WrenVM* vm, int value);

// Creates a new string from the integer representation of a byte
Value wrenStringFromByte(WrenVM* vm, uint8_t value);

// Creates a new string containing the code point in [string] starting at byte
// [index]. If [index] points into the middle of a UTF-8 sequence, returns an
// empty string.
Value wrenStringCodePointAt(WrenVM* vm, ObjString* string, uint32_t index);

// Search for the first occurence of [needle] within [haystack] and returns its
// zero-based offset. Returns `UINT32_MAX` if [haystack] does not contain
// [needle].
uint32_t wrenStringFind(ObjString* haystack, ObjString* needle,
                        uint32_t startIndex);

// Creates a new open upvalue pointing to [value] on the stack.
ObjUpvalue* wrenNewUpvalue(WrenVM* vm, Value* value);

// Mark [obj] as reachable and still in use. This should only be called
// during the sweep phase of a garbage collection.
void wrenGrayObj(WrenVM* vm, Obj* obj);

// Mark [value] as reachable and still in use. This should only be called
// during the sweep phase of a garbage collection.
void wrenGrayValue(WrenVM* vm, Value value);

// Mark the values in [buffer] as reachable and still in use. This should only
// be called during the sweep phase of a garbage collection.
void wrenGrayBuffer(WrenVM* vm, ValueBuffer* buffer);

// Processes every object in the gray stack until all reachable objects have
// been marked. After that, all objects are either white (freeable) or black
// (in use and fully traversed).
void wrenBlackenObjects(WrenVM* vm);

// Releases all memory owned by [obj], including [obj] itself.
void wrenFreeObj(WrenVM* vm, Obj* obj);

// Returns the class of [value].
//
// Unlike wrenGetClassInline in wren_vm.h, this is not inlined. Inlining helps
// performance (significantly) in some cases, but degrades it in others. The
// ones used by the implementation were chosen to give the best results in the
// benchmarks.
ObjClass* wrenGetClass(WrenVM* vm, Value value);

// Returns true if [a] and [b] are equivalent. Immutable values (null, bools,
// numbers, ranges, and strings) are equal if they have the same data. All
// other values are equal if they are identical objects.
bool wrenValuesEqual(Value a, Value b);


//}}}
//{{{utils



// Reusable data structures and other utility functions.

// Forward declare this here to break a cycle between wren_utils.h and
// wren_value.h.
typedef struct sObjString ObjString;

// TODO: Change this to use a map.
typedef StringBuffer SymbolTable;

// Initializes the symbol table.
void wrenSymbolTableInit(SymbolTable* symbols);

// Frees all dynamically allocated memory used by the symbol table, but not the
// SymbolTable itself.
void wrenSymbolTableClear(WrenVM* vm, SymbolTable* symbols);

// Adds name to the symbol table. Returns the index of it in the table.
int wrenSymbolTableAdd(WrenVM* vm, SymbolTable* symbols,
                       const char* name, size_t length);

// Adds name to the symbol table. Returns the index of it in the table. Will
// use an existing symbol if already present.
int wrenSymbolTableEnsure(WrenVM* vm, SymbolTable* symbols,
                          const char* name, size_t length);

// Looks up name in the symbol table. Returns its index if found or -1 if not.
int wrenSymbolTableFind(const SymbolTable* symbols,
                        const char* name, size_t length);

void wrenBlackenSymbolTable(WrenVM* vm, SymbolTable* symbolTable);

// Returns the number of bytes needed to encode [value] in UTF-8.
//
// Returns 0 if [value] is too large to encode.
int wrenUtf8EncodeNumBytes(int value);

// Encodes value as a series of bytes in [bytes], which is assumed to be large
// enough to hold the encoded result.
//
// Returns the number of written bytes.
int wrenUtf8Encode(int value, uint8_t* bytes);

// Decodes the UTF-8 sequence starting at [bytes] (which has max [length]),
// returning the code point.
//
// Returns -1 if the bytes are not a valid UTF-8 sequence.
int wrenUtf8Decode(const uint8_t* bytes, uint32_t length);

// Returns the number of bytes in the UTF-8 sequence starting with [byte].
//
// If the character at that index is not the beginning of a UTF-8 sequence,
// returns 0.
int wrenUtf8DecodeNumBytes(uint8_t byte);

// Returns the smallest power of two that is equal to or greater than [n].
int wrenPowerOf2Ceil(int n);

// Validates that [value] is within `[0, count)`. Also allows
// negative indices which map backwards from the end. Returns the valid positive
// index value. If invalid, returns `UINT32_MAX`.
uint32_t wrenValidateIndex(uint32_t count, int64_t value);

//}}}
//{{{primitive


// Binds a primitive method named [name] (in Wren) implemented using C function
// [fn] to `ObjClass` [cls].
#define PRIMITIVE(cls, name, function)                                         \
    do                                                                         \
    {                                                                          \
      int symbol = wrenSymbolTableEnsure(vm,                                   \
          &vm->methodNames, name, strlen(name));                               \
      Method method;                                                           \
      method.type = METHOD_PRIMITIVE;                                          \
      method.as.primitive = prim_##function;                                   \
      wrenBindMethod(vm, cls, symbol, method);                                 \
    } while (false)

// Binds a primitive method named [name] (in Wren) implemented using C function
// [fn] to `ObjClass` [cls], but as a FN call.
#define FUNCTION_CALL(cls, name, function)                                     \
    do                                                                         \
    {                                                                          \
      int symbol = wrenSymbolTableEnsure(vm,                                   \
          &vm->methodNames, name, strlen(name));                               \
      Method method;                                                           \
      method.type = METHOD_FUNCTION_CALL;                                      \
      method.as.primitive = prim_##function;                                   \
      wrenBindMethod(vm, cls, symbol, method);                                 \
    } while (false)

// Defines a primitive method whose C function name is [name]. This abstracts
// the actual type signature of a primitive function and makes it clear which C
// functions are invoked as primitives.
#define DEF_PRIMITIVE(name)                                                    \
    static bool prim_##name(WrenVM* vm, Value* args)

#define RETURN_VAL(value)                                                      \
    do                                                                         \
    {                                                                          \
      args[0] = value;                                                         \
      return true;                                                             \
    } while (false)

#define RETURN_OBJ(obj)     RETURN_VAL(OBJ_VAL(obj))
#define RETURN_BOOL(value)  RETURN_VAL(BOOL_VAL(value))
#define RETURN_FALSE        RETURN_VAL(FALSE_VAL)
#define RETURN_NULL         RETURN_VAL(NULL_VAL)
#define RETURN_NUM(value)   RETURN_VAL(NUM_VAL(value))
#define RETURN_TRUE         RETURN_VAL(TRUE_VAL)

#define RETURN_ERROR(msg)                                                      \
    do                                                                         \
    {                                                                          \
      vm->fiber->error = wrenNewStringLength(vm, msg, sizeof(msg) - 1);        \
      return false;                                                            \
    } while (false)

#define RETURN_ERROR_FMT(...)                                                  \
    do                                                                         \
    {                                                                          \
      vm->fiber->error = wrenStringFormat(vm, __VA_ARGS__);                    \
      return false;                                                            \
    } while (false)

// Validates that the given [arg] is a function. Returns true if it is. If not,
// reports an error and returns false.
bool validateFn(WrenVM* vm, Value arg, const char* argName);

// Validates that the given [arg] is a Num. Returns true if it is. If not,
// reports an error and returns false.
bool validateNum(WrenVM* vm, Value arg, const char* argName);

// Validates that [value] is an integer. Returns true if it is. If not, reports
// an error and returns false.
bool validateIntValue(WrenVM* vm, double value, const char* argName);

// Validates that the given [arg] is an integer. Returns true if it is. If not,
// reports an error and returns false.
bool validateInt(WrenVM* vm, Value arg, const char* argName);

// Validates that [arg] is a valid object for use as a map key. Returns true if
// it is. If not, reports an error and returns false.
bool validateKey(WrenVM* vm, Value arg);

// Validates that the argument at [argIndex] is an integer within `[0, count)`.
// Also allows negative indices which map backwards from the end. Returns the
// valid positive index value. If invalid, reports an error and returns
// `UINT32_MAX`.
uint32_t validateIndex(WrenVM* vm, Value arg, uint32_t count, const char* argName);

// Validates that the given [arg] is a String. Returns true if it is. If not,
// reports an error and returns false.
bool validateString(WrenVM* vm, Value arg, const char* argName);

// Given a [range] and the [length] of the object being operated on, determines
// the series of elements that should be chosen from the underlying object.
// Handles ranges that count backwards from the end as well as negative ranges.
//
// Returns the index from which the range should start or `UINT32_MAX` if the
// range is invalid. After calling, [length] will be updated with the number of
// elements in the resulting sequence. [step] will be direction that the range
// is going: `1` if the range is increasing from the start index or `-1` if the
// range is decreasing.
uint32_t calculateRange(WrenVM* vm, ObjRange* range, uint32_t* length, int* step);

//}}}
//{{{math

#include <math.h>
#include <stdint.h>

// A union to let us reinterpret a double as raw bits and back.
typedef union {
  uint64_t bits64;
  uint32_t bits32[2];
  double num;
} WrenDoubleBits;

#define WREN_DOUBLE_QNAN_POS_MIN_BITS (UINT64_C(0x7FF8000000000000))
#define WREN_DOUBLE_QNAN_POS_MAX_BITS (UINT64_C(0x7FFFFFFFFFFFFFFF))

#define WREN_DOUBLE_NAN (wrenDoubleFromBits(WREN_DOUBLE_QNAN_POS_MIN_BITS))

//}}}
//{{{debugger

// Prints the stack trace for the current fiber.
//
// Used when a fiber throws a runtime error which is not caught.
void wrenDebugPrintStackTrace(WrenVM* vm);

// The "dump" functions are used for debugging Wren itself. Normal code paths
// will not call them unless one of the various DEBUG_ flags is enabled.

// Prints a representation of [value] to stdout.
void wrenDumpValue(Value value);

// Prints a representation of the bytecode for [fn] at instruction [i].
int wrenDumpInstruction(WrenVM* vm, ObjFn* fn, int i);

// Prints the disassembled code for [fn] to stdout.
void wrenDumpCode(WrenVM* vm, ObjFn* fn);

// Prints the contents of the current stack for [fiber] to stdout.
void wrenDumpStack(ObjFiber* fiber);


//}}}
//{{{compiler

typedef struct sCompiler Compiler;

// This module defines the compiler for Wren. It takes a string of source code
// and lexes, parses, and compiles it. Wren uses a single-pass compiler. It
// does not build an actual AST during parsing and then consume that to
// generate code. Instead, the parser directly emits bytecode.
//
// This forces a few restrictions on the grammar and semantics of the language.
// Things like forward references and arbitrary lookahead are much harder. We
// get a lot in return for that, though.
//
// The implementation is much simpler since we don't need to define a bunch of
// AST data structures. More so, we don't have to deal with managing memory for
// AST objects. The compiler does almost no dynamic allocation while running.
//
// Compilation is also faster since we don't create a bunch of temporary data
// structures and destroy them after generating code.

// Compiles [source], a string of Wren source code located in [module], to an
// [ObjFn] that will execute that code when invoked. Returns `NULL` if the
// source contains any syntax errors.
//
// If [isExpression] is `true`, [source] should be a single expression, and
// this compiles it to a function that evaluates and returns that expression.
// Otherwise, [source] should be a series of top level statements.
//
// If [printErrors] is `true`, any compile errors are output to stderr.
// Otherwise, they are silently discarded.
ObjFn* wrenCompile(WrenVM* vm, ObjModule* module, const char* source,
                   bool isExpression, bool printErrors);

// When a class is defined, its superclass is not known until runtime since
// class definitions are just imperative statements. Most of the bytecode for a
// a method doesn't care, but there are two places where it matters:
//
//   - To load or store a field, we need to know the index of the field in the
//     instance's field array. We need to adjust this so that subclass fields
//     are positioned after superclass fields, and we don't know this until the
//     superclass is known.
//
//   - Superclass calls need to know which superclass to dispatch to.
//
// We could handle this dynamically, but that adds overhead. Instead, when a
// method is bound, we walk the bytecode for the function and patch it up.
void wrenBindMethodCode(ObjClass* classObj, ObjFn* fn);

// Reaches all of the heap-allocated objects in use by [compiler] (and all of
// its parents) so that they are not collected by the GC.
void wrenMarkCompiler(WrenVM* vm, Compiler* compiler);

//}}}

#endif
