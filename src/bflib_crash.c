/******************************************************************************/
// Bullfrog Engine Emulation Library - for use to remake classic games like
// Syndicate Wars, Magic Carpet or Dungeon Keeper.
/******************************************************************************/
/** @file bflib_crash.c
 *     Program failure handling system.
 * @par Purpose:
 *     Installs handlers to capture crashes; makes backtrace and clean exit.
 * @par Comment:
 *     None.
 * @author   Tomasz Lis
 * @date     09 Nov 2010 - 11 Nov 2010
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#if !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif

#include "pre_inc.h"
#include "kfx/renderer/RendererManager.h"
#include "bflib_crash.h"
#include <signal.h>
#include <stdarg.h>
#define BF_POSIX_CRASH 1
#if defined(BF_POSIX_CRASH)
#include <execinfo.h>
#include <ucontext.h>
#include <unistd.h>
#include <dlfcn.h>
#endif
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <stdarg.h>
#include <string.h>
#include <stdbool.h>
#include <inttypes.h>
#include "bflib_basics.h"
#include "bflib_video.h"
#include "post_inc.h"

/******************************************************************************/
static const char* sigstr(int s)
{
  switch(s)
    {
    case SIGINT : return "Interrupt (ANSI)";
    case SIGILL : return "Illegal instruction (ANSI)";
    case SIGABRT : return "Abort (ANSI)";
    case SIGFPE : return "Floating-point exception (ANSI)";
    case SIGSEGV : return "Segmentation violation (ANSI)";
    case SIGTERM : return "Termination (ANSI)";
#if defined(BF_POSIX_CRASH)
    case SIGHUP : return "Hangup (POSIX)";
    case SIGQUIT : return "Quit (POSIX)";
    case SIGTRAP : return "Trace trap (POSIX)";
    case SIGBUS : return "BUS error (4.2 BSD)";
    case SIGKILL : return "Kill, unblockable (POSIX)";
    case SIGUSR1 : return "User-defined signal 1 (POSIX)";
    case SIGUSR2 : return "User-defined signal 2 (POSIX)";
    case SIGPIPE : return "Broken pipe (POSIX)";
    case SIGALRM : return "Alarm clock (POSIX)";
    case SIGCHLD : return "Child status has changed (POSIX)";
    case SIGCONT : return "Continue (POSIX)";
    case SIGSTOP : return "Stop, unblockable (POSIX)";
    case SIGTSTP : return "Keyboard stop (POSIX)";
    case SIGTTIN : return "Background read from tty (POSIX)";
    case SIGTTOU : return "Background write to tty (POSIX)";
    case SIGURG : return "Urgent condition on socket (4.2 BSD)";
    case SIGXCPU : return "CPU limit exceeded (4.2 BSD)";
    case SIGXFSZ : return "File size limit exceeded (4.2 BSD)";
    case SIGVTALRM : return "Virtual alarm clock (4.2 BSD)";
    case SIGPROF : return "Profiling alarm clock (4.2 BSD)";
    case SIGWINCH : return "Window size change (4.3 BSD, Sun)";
    case SIGIO : return "I/O now possible (4.2 BSD)";
#ifdef SIGSYS
    case SIGSYS : return "Bad system call";
#endif
#ifdef SIGSTKFLT
    case SIGSTKFLT : return "Stack fault";
#endif
#ifdef SIGPWR
    case SIGPWR : return "Power failure restart (System V)";
#endif
#else
    case SIGBREAK : return "Ctrl-Break (Win32)";
#endif
    }
  return "unknown signal";
}

void exit_handler(void)
{
    LbErrorLog("Application exit called.\n");
}

void ctrl_handler(int sig_id)
{
    signal(sig_id, SIG_DFL);
    LbErrorLog("Failure signal: %s.\n",sigstr(sig_id));
    RendererResetScreen(true);
    LbErrorLogClose();
    raise(sig_id);
}


#if defined(BF_POSIX_CRASH)
static const char *posix_sigcode_str(int sig_id, int si_code)
{
    switch (sig_id)
    {
    case SIGSEGV:
        switch (si_code)
        {
        case SEGV_MAPERR: return "address not mapped to object";
        case SEGV_ACCERR: return "invalid permissions for mapped object";
        default: return "unknown segv code";
        }
    case SIGFPE:
        switch (si_code)
        {
        case FPE_INTDIV: return "integer divide by zero";
        case FPE_INTOVF: return "integer overflow";
        case FPE_FLTDIV: return "floating-point divide by zero";
        case FPE_FLTOVF: return "floating-point overflow";
        case FPE_FLTUND: return "floating-point underflow";
        case FPE_FLTRES: return "floating-point inexact result";
        case FPE_FLTINV: return "floating-point invalid operation";
        case FPE_FLTSUB: return "subscript out of range";
        default: return "unknown fpe code";
        }
    case SIGILL:
        switch (si_code)
        {
        case ILL_ILLOPC: return "illegal opcode";
        case ILL_ILLOPN: return "illegal operand";
        case ILL_ILLADR: return "illegal addressing mode";
        case ILL_ILLTRP: return "illegal trap";
        case ILL_PRVOPC: return "privileged opcode";
        case ILL_PRVREG: return "privileged register";
        case ILL_COPROC: return "coprocessor error";
        case ILL_BADSTK: return "internal stack error";
        default: return "unknown ill code";
        }
#ifdef SIGBUS
    case SIGBUS:
        switch (si_code)
        {
        case BUS_ADRALN: return "invalid address alignment";
        case BUS_ADRERR: return "nonexistent physical address";
        case BUS_OBJERR: return "object-specific hardware error";
        default: return "unknown bus code";
        }
#endif
    default:
        return "unknown signal code";
    }
}

static void log_posix_context(void *context)
{
#if defined(__linux__) && defined(__x86_64__)
    ucontext_t *uctx = (ucontext_t *)context;
#if defined(REG_RIP) && defined(REG_RSP)
    LbErrorLog("Context RIP=%p RSP=%p.\n",
        (void *)uctx->uc_mcontext.gregs[REG_RIP],
        (void *)uctx->uc_mcontext.gregs[REG_RSP]);
#else
    (void)uctx;
#endif
#elif defined(__linux__) && defined(__i386__)
    ucontext_t *uctx = (ucontext_t *)context;
    LbErrorLog("Context EIP=%p ESP=%p.\n",
        (void *)uctx->uc_mcontext.gregs[REG_EIP],
        (void *)uctx->uc_mcontext.gregs[REG_ESP]);
#elif defined(__linux__) && defined(__aarch64__)
    ucontext_t *uctx = (ucontext_t *)context;
    LbErrorLog("Context PC=%p SP=%p.\n",
        (void *)uctx->uc_mcontext.pc,
        (void *)uctx->uc_mcontext.sp);
#else
    (void)context;
#endif
}

static void write_stderr_line(const char *line, size_t line_len)
{
    ssize_t written = write(STDERR_FILENO, line, line_len);
    (void)written;
}

static void _backtrace_posix(int depth)
{
    void *frames[64];
    int max_frames = (depth > (int)(sizeof(frames) / sizeof(frames[0])))
        ? (int)(sizeof(frames) / sizeof(frames[0]))
        : depth;
    int count = backtrace(frames, max_frames);

    if (count > 0)
    {
        backtrace_symbols_fd(frames, count, STDERR_FILENO);

        char **symbols = backtrace_symbols(frames, count);
        if (symbols != NULL)
        {
            int printed_idx = 0;
            for (int idx = 0; idx < count; idx++)
            {
                Dl_info info;
                if (dladdr(frames[idx], &info) != 0)
                {
                    if (info.dli_sname != NULL)
                    {
                        if ((strcmp(info.dli_sname, "_backtrace_posix") == 0)
                         || (strcmp(info.dli_sname, "ctrl_handler_posix") == 0)
                         || (strcmp(info.dli_sname, "write_stderr_line") == 0)
                         || (strcmp(info.dli_sname, "log_posix_context") == 0))
                        {
                            continue;
                        }
                    }

                    const char *module_name = (info.dli_fname != NULL) ? info.dli_fname : "[unknown module]";
                    const char *symbol_name = (info.dli_sname != NULL) ? info.dli_sname : "[unknown symbol]";
                    if ((info.dli_sname == NULL) && (symbols[idx] != NULL))
                    {
                        symbol_name = symbols[idx];
                    }
                    uintptr_t symbol_addr = (uintptr_t)info.dli_saddr;
                    uintptr_t frame_addr = (uintptr_t)frames[idx];
                    uintptr_t displacement = 0;
                    if (symbol_addr != 0 && frame_addr >= symbol_addr)
                    {
                        displacement = frame_addr - symbol_addr;
                    }

                    LbJustLog("[#%-2d] %-20s : %-36s [%p+0x%" PRIxPTR "]\n",
                        printed_idx, module_name, symbol_name, frames[idx], displacement);
                    printed_idx++;
                }
                else
                {
                    LbJustLog("[#%-2d] %s\n", printed_idx, symbols[idx]);
                    printed_idx++;
                }
            }
            free(symbols);
        }
    }
}

static void ctrl_handler_posix(int sig_id, siginfo_t *info, void *context)
{
    const void *fault_addr = (info != NULL) ? info->si_addr : NULL;

    {
        static const char crash_msg[] = "KeeperFX fatal signal received\n";
        write_stderr_line(crash_msg, sizeof(crash_msg) - 1);
    }

    if (sig_id == SIGSEGV)
    {
        static const char segv_msg[] = "Signal: SIGSEGV\n";
        write_stderr_line(segv_msg, sizeof(segv_msg) - 1);
    }
    else if (sig_id == SIGABRT)
    {
        static const char abrt_msg[] = "Signal: SIGABRT\n";
        write_stderr_line(abrt_msg, sizeof(abrt_msg) - 1);
    }
    else if (sig_id == SIGFPE)
    {
        static const char fpe_msg[] = "Signal: SIGFPE\n";
        write_stderr_line(fpe_msg, sizeof(fpe_msg) - 1);
    }

    LbErrorLog("Failure signal: %s (%d).\n", sigstr(sig_id), sig_id);
    if (info != NULL)
    {
        LbErrorLog("Signal code: %d (%s).\n", info->si_code, posix_sigcode_str(sig_id, info->si_code));
    }
    LbErrorLog("Fault address: %p.\n", fault_addr);
    log_posix_context(context);
    _backtrace_posix(16);

    // Do NOT reset the screen / tear down the renderer/SDL from inside a signal handler: that
    // work is not async-signal-safe and can deadlock or re-fault when the crash happened inside
    // the graphics driver, SDL, or the allocator — which would suppress the crash report
    // entirely. The async-signal-safe signal info and backtrace_symbols_fd output were already
    // written to stderr above. The process is terminating, so the OS/compositor reclaims the
    // video context anyway. (Upstream calls RendererResetScreen(true) here.)
    LbErrorLogClose();

    signal(sig_id, SIG_DFL);
    raise(sig_id);
}

static void install_posix_handler(int sig_id)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = ctrl_handler_posix;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_SIGINFO | SA_RESETHAND;
    {
        int rc = sigaction(sig_id, &sa, NULL);
        if (rc != 0)
        {
            fprintf(stderr,
                "LbErrorParachuteInstall: sigaction failed for signal %d (%s); "
                "crash handler not installed for this signal.\n",
                sig_id, sigstr(sig_id));
#ifndef NDEBUG
            assert(rc == 0);
#endif
        }
    }
}
#endif

void LbErrorParachuteInstall(void)
{
#if defined(BF_POSIX_CRASH)
    install_posix_handler(SIGINT);
    install_posix_handler(SIGILL);
    install_posix_handler(SIGABRT);
    install_posix_handler(SIGFPE);
    install_posix_handler(SIGSEGV);
    install_posix_handler(SIGTERM);
    install_posix_handler(SIGHUP);
    install_posix_handler(SIGQUIT);
#ifdef SIGSYS
    install_posix_handler(SIGSYS);
#endif
#ifdef SIGBUS
    install_posix_handler(SIGBUS);
#endif
#ifdef SIGTRAP
    install_posix_handler(SIGTRAP);
#endif
#else
    signal(SIGINT,ctrl_handler);
    signal(SIGILL,ctrl_handler);
    signal(SIGABRT,ctrl_handler);
    signal(SIGFPE,ctrl_handler);
    signal(SIGSEGV,ctrl_handler);
    signal(SIGTERM,ctrl_handler);
#if defined(BF_POSIX_CRASH)
    signal(SIGHUP,ctrl_handler);
    signal(SIGQUIT,ctrl_handler);
#ifdef SIGSYS
    signal(SIGSYS,ctrl_handler);
#endif
#else
    signal(SIGBREAK,ctrl_handler);
#endif
#endif
    atexit(exit_handler);
}

void LbErrorParachuteUpdate(void)
{
}
/******************************************************************************/
