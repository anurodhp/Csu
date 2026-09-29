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
 *
 * DAR-161 (iokit repo, 2026-08-31): real Apple's own crt.c/crt1.o is
 * simply never linked into a modern (10.5+, non-OLD_LIBSYSTEM_SUPPORT)
 * dynamic executable at all -- dyld supplies NXArgc/NXArgv/environ/
 * __progname itself via ProgramVars before ever reaching main(). This
 * project's start.s links Csu unconditionally, even into genuinely
 * dynamically-linked (LC_LOAD_DYLINKER) executables, purely to carry a
 * real LC_UNIXTHREAD past a real kernel bug (see build_dyld_test_exec.sh's
 * own header comment: dyld's real load_dylinker() handoff overwrites this
 * binary's own thread state/PC before start.s's `start:` label ever runs,
 * so none of that code executes for a real dynamic target regardless).
 * Before this fix, crt.c's unconditional STRONG definitions below still
 * shadowed the real, canonical dyld-owned copies at STATIC LINK TIME: any
 * dynamically-linked binary's own code that reads `environ`/`NXArgc`/
 * `NXArgv`/`__progname` directly (not through getenv()/setenv(), which
 * route through a separate real accessor) got bound to THIS translation
 * unit's own always-NULL/0 local storage instead of the real one
 * dyld's libdyldGlue.o exports from libdyld.dylib -- confirmed via `nm -m`
 * on tools/userland_staging/dyld_test_exec.macho showing `(__DATA,
 * __common) external _environ` etc. (a LOCAL definition) rather than an
 * import/bind, even though `xcrun dyld_info -exports` on libdyld.dylib
 * shows a real, distinct, separately-live `_environ`/`_NXArgc`/`_NXArgv`/
 * `___progname` at different addresses.
 *
 * Fix mechanism: a NEW, project-owned macro, `CRT_DYNAMIC_LINKING`, NOT
 * real Apple's own `__DYNAMIC__` (tried first, reverted -- see below).
 * When a build script compiles this file with `-DCRT_DYNAMIC_LINKING`
 * (genuinely dynamically-linked consumers only), these four globals
 * become plain `extern` references instead of local definitions, so any
 * direct read/write of them (including this file's own
 * now-dead-for-dynamic-targets crt_init_program_vars()/start.s environ
 * store, see start.s's own matching `#if !CRT_DYNAMIC_LINKING` guard)
 * binds against the real dyld-owned copy via a normal chained-fixups
 * bind, exactly like every other cross-image symbol reference in this
 * project's dylib stack. The default (macro undefined) preserves
 * today's local-storage behavior for every existing static consumer,
 * unchanged -- same shape as round 11's `XLOCALE_STATE_EXPORTED` macro
 * gate on `xlocale_private.h` (default hidden/unexported, only widened
 * where actually intended).
 *
 * First attempt reused real Apple's own `__DYNAMIC__` macro instead of
 * inventing a new one (reasoning: it's already used throughout the rest
 * of this file for exactly this static-vs-dynamic distinction, and round
 * 11's own precedent -- macro-gate a shared file's behavior -- doesn't
 * dictate the macro's NAME). This was WRONG and caught before landing on
 * any consumer: `echo | clang -isysroot <iPhoneOS SDK> -target
 * arm64-apple-ios14.4 -dM -E - | grep __DYNAMIC__` shows clang predefines
 * `__DYNAMIC__=1` UNCONDITIONALLY for this triple, regardless of
 * `-static`/`-dynamic` -- a real, confirmed fact about this project's
 * pinned Xcode-12/iPhoneOS-SDK toolchain, not the "PIC vs non-PIC
 * codegen flag from the ppc/i386 era" meaning the rest of this file's
 * existing `__DYNAMIC__` guards were written against. Confirmed via a
 * real, minimal repro: `clang ... -c crt.c -o crt_static_test.o` (the
 * exact flags every one of the ~46 real STATIC consumers already uses,
 * zero -D flags added) then `nm crt_static_test.o` showed `U _NXArgc`/
 * `U _NXArgv`/`U ___progname` -- i.e. the `#if !__DYNAMIC__` gate as
 * first written would have silently flipped EVERY existing static
 * consumer into extern/import mode too, which cannot resolve at a fully
 * static `-static` link (no dylib, no chained fixups) -- a real, would-be
 * regression across this entire project's static binary set, caught by
 * testing the "unaffected" claim directly rather than assuming it.
 */
#if !CRT_DYNAMIC_LINKING
int           NXArgc = 0;
const char**  NXArgv = NULL;
const char**  environ = NULL;
const char*   __progname = NULL;
#else
extern int           NXArgc;
extern const char**  NXArgv;
extern const char**  environ;
extern const char*   __progname;
#endif

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

/* DAR-164 fix (2026-09-01): real dyld guarantees libSystem_initializer
 * installs a valid TPIDRRO_EL0/TSD base BEFORE any __mod_init_func
 * constructor runs for a real dynamic executable -- this project's
 * static/no-dyld path had no equivalent ordering guarantee, and it bit
 * for real: Libc/locale/xlocale.c's own real __xlocale_init_constructor
 * (itself a __mod_init_func entry, added 2026-08-27 for an earlier,
 * unrelated bug) calls real pthread_key_init_np(), which -- for any
 * consumer linking this project's real libpthread_tsd.c tier -- needs a
 * valid TPIDRRO_EL0 (_pthread_lock_lock -> os_unfair_lock_lock_with_
 * options -> _os_lock_owner_get_self -> _os_tsd_get_direct, real TSD
 * slot 3/offset 0x18). Real, QEMU-confirmed SIGSEGV (fault addr exactly
 * 0x18) found root-causing DAR-164 (login's post-exec crash): giving
 * pthread_main_thread_bootstrap() itself a high constructor priority
 * was tried first and did NOT work -- confirmed via a real otool -l:
 * this project's own __mod_init_func entries land in plain link order,
 * not priority order (ld64/this custom crt_run_static_initializers()
 * loop above neither sorts nor honors the numeric priority; xlocale's
 * object simply linked first). The only real, order-independent fix is
 * to run this BEFORE the __mod_init_func loop at all, from crt.c
 * itself -- the same real dyld guarantee, just implemented in the one
 * place this target's own equivalent of "process bootstrap" already
 * lives (this function). Declared weak_import (matching this SAME
 * file's own established mach_init_routine/_cthread_init_routine
 * optional-hook idiom below, adapted to a plain weak function rather
 * than a weak pointer variable since pthread_main_thread_bootstrap is a
 * real, directly-callable function, not one that goes through an
 * indirect pointer slot) so every OTHER static consumer that does not
 * link libpthread.a at all (pwtool, most of tools/userland_staging's
 * simple_cmds binaries) is completely unaffected -- the call below
 * becomes a real no-op for them, not a new hard dependency. */
/* DAR-426 (iokit repo): libsystem_pthread.dylib no longer exports
 * pthread_main_thread_bootstrap (a no-op since DAR-416's real
 * __pthread_init; iokit 77cc1e1). A dynamic consumer (CRT_DYNAMIC_LINKING)
 * never runs this function -- start.s:298 gates its only call site on
 * !CRT_DYNAMIC_LINKING and dyld runs the __mod_init_func constructors
 * itself -- but the dead function was still compiled and its weak_import
 * reference made every dynamic link fail "Undefined symbols:
 * _pthread_main_thread_bootstrap". Compile it (and the reference) out for
 * dynamic consumers; static consumers are unchanged. */
#if !CRT_DYNAMIC_LINKING
extern int pthread_main_thread_bootstrap(void) __attribute__((weak_import));

void
crt_run_static_initializers(void)
{
    /* Called through a `volatile` local, NOT as a direct
     * `pthread_main_thread_bootstrap()` call. Real bug found+fixed
     * 2026-09-01 (DAR-153) in the version this replaces, whose own
     * comment above claimed "every OTHER static consumer that does not
     * link libpthread.a at all ... is completely unaffected -- the call
     * below becomes a real no-op for them". That was not true: a direct
     * call compiles to a real `bl`, and weak_import is a dylib-binding
     * concept with no meaning in a fully `-static` link, so ld64 first
     * rejects the link outright ("Undefined symbols ...") and then, if
     * the symbol is forced undefined with -U, rejects it again with
     * "b(l) ARM64 branch out of range ... to _pthread_main_thread_
     * bootstrap (0x00000000)". Both failures reproduced for real
     * (build_libplatform.sh); 12 of this project's build scripts link
     * this file without libpthread.a and every one of them was broken by
     * it -- the DAR-164 change was only ever exercised against
     * consumers that do link libpthread.a.
     *
     * Routing the call through a volatile local forces clang to
     * materialize the address (a GOT load, which for an absent
     * weak_import is a real zero word) and then `blr` it, so the null
     * guard actually gets a chance to run -- which is what makes this a
     * genuine optional hook, and what the "weak pointer variable"
     * mach_init_routine/_cthread_init_routine idiom below has always
     * relied on. Static consumers still need -U for the symbol itself
     * (see build_libplatform.sh's own comment); consumers that do link
     * libpthread.a are unchanged, direct or indirect. */
    int (* volatile bootstrap)(void) = pthread_main_thread_bootstrap;
    if (bootstrap) {
        (void)bootstrap();
    }
    for (void (**f)(void) = __crt_mod_init_func_start; f < __crt_mod_init_func_end; f++) {
        (*f)();
    }
}
#endif /* !CRT_DYNAMIC_LINKING */

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
