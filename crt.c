/*
 * Copyright (c) 1999-2008 Apple Inc. All rights reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
 * 
 * Portions Copyright (c) 1999 Apple Computer, Inc.  All Rights
 * Reserved.  This file contains Original Code and/or Modifications of
 * Original Code as defined in and that are subject to the Apple Public
 * Source License Version 1.1 (the "License").  You may not use this file
 * except in compliance with the License.  Please obtain a copy of the
 * License at http://www.apple.com/publicsource and read it before using
 * this file.
 * 
 * The Original Code and all software distributed under the License are
 * distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE OR NON- INFRINGEMENT.  Please see the
 * License for the specific language governing rights and limitations
 * under the License.
 * 
 * @APPLE_LICENSE_HEADER_END@
 */
/*
 * The common startup code.  This code is if'ed with the 'C' preprocessor
 * macros __DYNAMIC__ and GCRT.  It is used to create
 * the following files when compiled with the following macros defined:
 *
 *  File      Dedined Macros	   Purpose
 * crt1.o	__DYNAMIC__	    startup for programs compiled -dynamic
 * gcrt1.o	__DYNAMIC__, GCRT   profiling startup, programs compiled -dynamic
 *
 * crt0.o			    startup for programs compiled -static 
 * 
 */

#include <stddef.h>

/*
 * Global data definitions (initialized data).
 */
int           NXArgc = 0;
const char**  NXArgv = NULL;
const char**  environ = NULL;
const char*   __progname = NULL;

/* Real basename-of-argv[0] helper, matching Apple's own crt_basename()
 * further down this file (real logic, just copied up here since that
 * copy lives inside the `#if __DYNAMIC__ && OLD_LIBSYSTEM_SUPPORT` dead
 * block below -- see this file's own "not needed for executables
 * targeting 10.5 or later" comment: neither macro is ever defined for
 * this project's static/no-dyld target, so NOTHING in that whole block,
 * including _start() itself, is ever compiled in. That block was the
 * ONLY code in this file that ever wrote __progname/NXArgc/NXArgv, which
 * meant those three globals were permanently NULL/0 for every process on
 * this target, always -- a real, universal bug (found 2026-08-20 via a
 * real QEMU A/B instrumentation test: a spawned child's own argv[0], read
 * directly in its own main(), was already correct, but getprogname()
 * still read back "" from this file's own never-written __progname
 * global -- see docs/roadmap.md/docs/historic/roadmap.md Phase 4 for the
 * full transcript-backed writeup). Real Apple dyld would populate these
 * three via ProgramVars (see crt_externs.c's real _program_vars_init(),
 * also unreachable here for the same no-dyld reason) before ever
 * reaching this static target's entry point; this project's own
 * start.s (arm64) already does the equivalent one-off real store for
 * `environ` (Phase 4.6 item 2) -- this is the same real fix for the
 * remaining three globals, called from start.s right after that
 * existing environ store. */
static const char *
crt0_basename(const char *path)
{
    const char *s;
    const char *last = path;

    for (s = path; *s != '\0'; s++) {
        if (*s == '/') last = s + 1;
    }

    return last;
}

void
crt_init_program_vars(int argc, const char **argv)
{
    NXArgc = argc;
    NXArgv = argv;
    __progname = (argv != NULL && argv[0] != NULL) ? crt0_basename(argv[0]) : "";
}

/* Real dyld responsibility this project's static/no-dyld target has to
 * take over itself, same class of gap as crt_init_program_vars() above
 * (see its own header comment) -- found 2026-08-22 building the first
 * tool (ifconfig) that uses `__attribute__((constructor))`
 * (network_cmds/ifconfig.tproj's af_register()/clone_setcallback()
 * self-registration pattern): a real QEMU crash (`ifconfig -a`
 * immediately EXC_CORPSE_NOTIFY, zero output) traced to this project's
 * whole static-link strategy (`-nostdlib -static -e start`, real
 * LC_UNIXTHREAD entry, no LC_MAIN/crt1.o) meaning nothing ever walks
 * `__DATA,__mod_init_func` -- on a real dynamic executable dyld does
 * this (see crt.c's own dead `_dyld_make_delayed_module_initializer_calls`
 * call further down, unreachable here for the same no-`__DYNAMIC__`
 * reason as everything else in that block), but a fully static Mach-O
 * with no dyld loses constructor support entirely unless something
 * walks the section itself. `section$start$__DATA$__mod_init_func`/
 * `section$end$__DATA$__mod_init_func` are real, standard ld64-synthesized
 * boundary symbols (always defined, even as an empty zero-length range
 * when a binary has no `__mod_init_func` entries at all -- confirmed
 * real ld64 behavior, not assumed) -- this is the same idiom real
 * minimal/static libc startup code (e.g. musl's __libc_start_main) uses
 * for the identical reason. Safe to call unconditionally from every
 * tool's start.s; a no-op for every prior tool (none use constructors),
 * real fix for ifconfig's af_inet/af_link/ifmedia/ifclone registration. */
extern void (*__crt_mod_init_func_start[])(void) __asm("section$start$__DATA$__mod_init_func");
extern void (*__crt_mod_init_func_end[])(void) __asm("section$end$__DATA$__mod_init_func");

void
crt_run_static_initializers(void)
{
    for (void (**f)(void) = __crt_mod_init_func_start; f < __crt_mod_init_func_end; f++) {
        (*f)();
    }
}

#if ADD_PROGRAM_VARS
extern void* __dso_handle;
struct ProgramVars
{
    void*           mh;
    int*            NXArgcPtr;
    const char***   NXArgvPtr;
    const char***   environPtr;
    const char**    __prognamePtr;
};
__attribute__((used))  static struct ProgramVars pvars 
__attribute__ ((section ("__DATA,__program_vars")))  = { &__dso_handle, &NXArgc, &NXArgv, &environ, &__progname };

#endif
 

/*
 * This file is not needed for executables targeting 10.5 or later
 * start calls main() directly.
 */
#if __DYNAMIC__ && OLD_LIBSYSTEM_SUPPORT
/*
 * The following symbols are reference by System Framework symbolicly (instead
 * of through undefined references (to allow prebinding). To get strip(1) to
 * know these symbols are not to be stripped they need to have the
 * REFERENCED_DYNAMICALLY bit (0x10) set.  This would have been done automaticly
 * by ld(1) if these symbols were referenced through undefined symbols.
 * The catch_exception_raise symbol is special in that the Mach API specifically
 * requires that the library call into the user program for its implementation.
 * Therefore, we need to create a common definition and make sure the symbol
 * doesn't get stripped.
 */
asm(".desc _NXArgc, 0x10");
asm(".desc _NXArgv, 0x10");
asm(".desc _environ, 0x10");
asm(".desc __mh_execute_header, 0x10");
#if defined(__ppc__) || defined(__i386__)
asm(".comm _catch_exception_raise, 4");
asm(".desc _catch_exception_raise, 0x10");
asm(".comm _catch_exception_raise_state, 4");
asm(".desc _catch_exception_raise_state, 0x10");
asm(".comm _catch_exception_raise_state_identity, 4");
asm(".desc _catch_exception_raise_state_identity, 0x10");
asm(".comm _do_mach_notify_dead_name, 4");
asm(".desc _do_mach_notify_dead_name, 0x10");
asm(".comm _do_seqnos_mach_notify_dead_name, 4");
asm(".desc _do_seqnos_mach_notify_dead_name, 0x10");
asm(".comm _do_mach_notify_no_senders, 4");
asm(".desc _do_mach_notify_no_senders, 0x10");
asm(".comm _do_seqnos_mach_notify_no_senders, 4");
asm(".desc _do_seqnos_mach_notify_no_senders, 0x10");
asm(".comm _do_mach_notify_port_deleted, 4");
asm(".desc _do_mach_notify_port_deleted, 0x10");
asm(".comm _do_seqnos_mach_notify_port_deleted, 4");
asm(".desc _do_seqnos_mach_notify_port_deleted, 0x10");
asm(".comm _do_mach_notify_send_once, 4");
asm(".desc _do_mach_notify_send_once, 0x10");
asm(".comm _do_seqnos_mach_notify_send_once, 4");
asm(".desc _do_seqnos_mach_notify_send_once, 0x10");
asm(".comm _clock_alarm_reply, 4");
asm(".desc _clock_alarm_reply, 0x10");
asm(".comm _receive_samples, 4");
asm(".desc _receive_samples, 0x10");
#endif /* __ppc__ || __i386__ */
asm(".desc ___progname, 0x10");

/*
 * Common data definitions.  If the routines in System Framework are not pulled
 * into the executable then the static linker will allocate these as common
 * symbols.  The code in here tests the value of these are non-zero to know if
 * the routines in System Framework got pulled in and should be called.  The
 * first two are pointers to functions.  The second two use just the symbol
 * itself.  In the later case we are using the symbol with two different 'C'
 * types.  To make it as clean as possible the 'C' type declared is that of the
 * external function.  The common symbol is declared with an asm() and the code
 * casts the function name to a pointer to an int and then indirects through
 * the pointer to see if the value is not zero to know the function got linked
 * in.  Then the code uses a pointer in the data area to the function to call
 * the function.  The pointer in the data area is needed on various RISC
 * architectutes like the PowerPC to avoid a relocation overflow error when
 * linking programs with large data area.
 */
extern int (*mach_init_routine)(void);
extern int (*_cthread_init_routine)(void);
#if !__DYNAMIC__
asm(".comm __cplus_init, 4");
extern void _cplus_init(void);
#endif
#if __DYNAMIC__ && __ppc__
asm(".comm ___darwin_gcc3_preregister_frame_info, 4");
extern void __darwin_gcc3_preregister_frame_info (void);
static void (*pointer_to__darwin_gcc3_preregister_frame_info)(void) =
	__darwin_gcc3_preregister_frame_info;
#endif

/*
 * Prototypes for routines that are called.
 */
extern int main(int argc, const char* argv[], const char* envp[], const char* apple[]);
extern void exit(int status) __attribute__ ((noreturn));
extern int atexit(void (*fcn)(void));
static const char* crt_basename(const char* path);

#if GCRT
extern void moninit(void);
static void _mcleanup(void);
extern void monitor(char *lowpc,char *highpc,char *buf,int bufsiz,int nfunc);
#endif /* GCRT */

#if __DYNAMIC__
extern int _dyld_func_lookup(const char *dyld_func_name,unsigned long *address);
extern void __keymgr_dwarf2_register_sections (void);
#endif /* __DYNAMIC__ */

#if __DYNAMIC__ && __ppc__ 
static void _call_objcInit(void);
#endif

extern int errno;

/*
 * _start() is called from the machine dependent assembly entry point "start:" .
 * It takes care of setting up the stack so 'C' routines can be called and
 * passes argc, argv and envp to here.
 */
__private_extern__
void
_start(int argc, const char* argv[], const char* envp[])
{
    const char** apple;
#if __DYNAMIC__
    void (*term)(void);
    void (*init)(void);
#endif

	// initialize global variables 
	NXArgc = argc;
	NXArgv = argv;
	environ = envp;
	__progname = ((argv[0] != NULL) ? crt_basename(argv[0]) : "");
	// see start.s for how "apple" parameter follow envp
	for(apple = envp; *apple != NULL; ++apple) { /* loop */ }
	++apple;
	
	// initialize libSystem
	if ( mach_init_routine != 0 )
	    (void) mach_init_routine();
	if ( _cthread_init_routine != 0 )
	    (*_cthread_init_routine)();

#ifdef __DYNAMIC__
	__keymgr_dwarf2_register_sections ();
#endif

#if __ppc__ && __DYNAMIC__
       /* Call a ppc GCC 3.3-specific function (in libgcc.a) to
          "preregister" exception frame info, meaning to set up the
          dyld hooks that do the actual registration.  */
       if ( *((int *)pointer_to__darwin_gcc3_preregister_frame_info) != 0 )
           pointer_to__darwin_gcc3_preregister_frame_info ();
#endif

#if !__DYNAMIC__
        if(*((int *)_cplus_init) != 0)
            _cplus_init();
#endif

#ifdef __DYNAMIC__
	/*
	 * Call into dyld to run all initializers. This must be done 
	 * after mach_init()
	 */
        _dyld_func_lookup("__dyld_make_delayed_module_initializer_calls",
                          (unsigned long *)&init);
        init();
#endif

#if __DYNAMIC__ && __ppc__ 
        _call_objcInit();
#endif

#ifdef GCRT
	atexit(_mcleanup);
	moninit();
#endif

#ifdef __DYNAMIC__
	/*
	 * If the dyld we are running with supports module termination routines
	 * for all types of images then register the function to call them with
	 * atexit().
	 */
        _dyld_func_lookup("__dyld_mod_term_funcs", (unsigned long *)&term);
        if ( term != 0 )
	    atexit(term);
#endif

	// clear errno, so main() starts fresh
	errno = 0;

	// call main() and return to exit()
	exit(main(argc, argv, envp, apple));
}

#if GCRT
/*
 * For profiling the routine _mcleanup gets registered with atexit so monitor(0)
 * gets called.
 */
static
void
_mcleanup(
void)
{
	monitor(0,0,0,0,0);
}
#endif /* GCRT */

static 
const char *
crt_basename(const char *path)
{
    const char *s;
    const char *last = path;

    for (s = path; *s != '\0'; s++) {
        if (*s == '/') last = s+1;
    }

    return last;
}

#if __DYNAMIC__ && __ppc__ 
static 
int
crt_strbeginswith(const char *s1, const char *s2)
{
    int i;

    for (i = 0; ; i++) {
        if (s2[i] == '\0') return 1;
        else if (s1[i] != s2[i]) return 0;
    }
}

/*
 * Look for a function called _objcInit() in any library whose name 
 * starts with "libobjc", and call it if one exists. This is used to 
 * initialize the Objective-C runtime on Mac OS X 10.3 and earlier.  
 * This is completely unnecessary on Mac OS X 10.4 and later.
 */
static
void
_call_objcInit(void)
{
    unsigned int i, count;

    unsigned int (*_dyld_image_count_fn)(void);
    const char *(*_dyld_get_image_name_fn)(unsigned int image_index);
    const void *(*_dyld_get_image_header_fn)(unsigned int image_index);
    const void *(*NSLookupSymbolInImage_fn)(const void *image, const char *symbolName, unsigned int options);
    void *(*NSAddressOfSymbol_fn)(const void *symbol);

    // Find some dyld functions.
    _dyld_func_lookup("__dyld_image_count", 
                      (unsigned long *)&_dyld_image_count_fn);
    _dyld_func_lookup("__dyld_get_image_name", 
                      (unsigned long *)&_dyld_get_image_name_fn);
    _dyld_func_lookup("__dyld_get_image_header", 
                      (unsigned long *)&_dyld_get_image_header_fn);
    _dyld_func_lookup("__dyld_NSLookupSymbolInImage", 
                      (unsigned long *)&NSLookupSymbolInImage_fn);
    _dyld_func_lookup("__dyld_NSAddressOfSymbol", 
                      (unsigned long *)&NSAddressOfSymbol_fn);

    // If any of the dyld functions don't exist, assume we're 
    // on a post-Panther dyld and silently do nothing.
    if (!_dyld_image_count_fn) return;
    if (!_dyld_get_image_name_fn) return;
    if (!_dyld_get_image_header_fn) return;
    if (!NSLookupSymbolInImage_fn) return;
    if (!NSAddressOfSymbol_fn) return;

    // Search for an image whose library name starts with "libobjc".
    count = (*_dyld_image_count_fn)();
    for (i = 0; i < count; i++) {
        const void *image;
        const char *path = (*_dyld_get_image_name_fn)(i);
        const char *base = crt_basename(path);
        if (!crt_strbeginswith(base, "libobjc")) continue;

        // Call _objcInit() if library exports it.
        if ((image = (*_dyld_get_image_header_fn)(i))) {
            const void *symbol;
            // 4 == NSLOOKUPSYMBOLINIMAGE_OPTION_RETURN_ON_ERROR
            if ((symbol = (*NSLookupSymbolInImage_fn)(image,"__objcInit",4))) {
                void (*_objcInit_fn)(void) = 
                    (void(*)(void))(*NSAddressOfSymbol_fn)(symbol);
                if (_objcInit_fn) {
                    (*_objcInit_fn)();
                    break;
                }
            }
        }
    }
}

#endif /* __DYNAMIC__ && __ppc__ */

#endif /* __DYNAMIC__ && OLD_LIBSYSTEM_SUPPORT */
