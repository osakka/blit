/* Blit's crash backtrace (docs/toolchain.md, tools/tcc-link.sh).

   tcc compiles Blit and the system linker links it, so tcc's own -bt
   runtime is gone; this takes its place. On SIGSEGV, SIGBUS, SIGILL, SIGFPE
   or SIGABRT it writes to standard error the signal, the faulting address
   and one line per frame -- its address and, when the address lies inside
   a symbol the program or a library exports (tcc-link.sh links programs
   -rdynamic; Blit's own functions are mostly static and are not), that
   function and the offset into it. Then, where binutils is installed, it
   runs `addr2line -f -e <program>` on the program's frames, which gives
   each one's function, file and line from tcc's debug information (kept
   in the file); without it the addresses are for that command later.
   Then the signal's default action ends the process as before (a core,
   where they are kept).

   The frames are walked by their frame pointers: tcc keeps them in every
   function (a C library function compiled without them ends the walk
   there; the frame that called it was the last one Blit's). The walk
   stops at a pointer that goes backwards, is misaligned or leaps more
   than 16 MiB, so a corrupt chain ends the trace rather than the handler.
   Only write(2), dladdr1(3), readlink(2), fork(2), execve(2) and
   waitpid(2) are called; the handler runs on its own
   stack, so a stack overflow is reported too (on the thread that
   installed it; other threads report on their own stack).

   Header-only and static, included once by each program's main file. */
#ifndef BLIT_CRASH_H
#define BLIT_CRASH_H

#include <dlfcn.h>
#include <elf.h>
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <stdint.h>
#include <string.h>
#include <ucontext.h>
#include <unistd.h>

#define BLIT_CRASH_FRAMES 32

/* Set at install: the program's own load base (for which frames are its
   own) and its path (for addr2line), read while it is safe to. */
static void *blit_crash_program_base;
static char blit_crash_program[4096];

static void blit_crash_text(const char *text)
{
    size_t length = strlen(text);
    while (length > 0) {
        ssize_t wrote = write(2, text, length);
        if (wrote <= 0)
            return;
        text += wrote;
        length -= (size_t)wrote;
    }
}

static void blit_crash_hex(uintptr_t value)
{
    char digits[2 + 2 * sizeof(uintptr_t) + 1];
    int at = (int)sizeof(digits) - 1;
    digits[at] = '\0';
    do {
        digits[--at] = "0123456789abcdef"[value & 15u];
        value >>= 4;
    } while (value != 0 && at > 2);
    digits[--at] = 'x';
    digits[--at] = '0';
    blit_crash_text(digits + at);
}

static void blit_crash_decimal(unsigned long value)
{
    char digits[24];
    int at = (int)sizeof(digits) - 1;
    digits[at] = '\0';
    do {
        digits[--at] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value != 0 && at > 0);
    blit_crash_text(digits + at);
}

/* One frame: `  #k 0xADDR function+0xOFF (object)`, the function only
   when the address lies inside it: dladdr alone names the nearest exported
   symbol before it, which for a static function is another one. Returns
   whether the frame is the program's own (for addr2line). */
static int blit_crash_frame(int index, uintptr_t address)
{
    Dl_info info;
    /* elf.h's, not link.h's ElfW (tcc cannot read link.h's __int128). */
#if UINTPTR_MAX > 0xffffffffu
    const Elf64_Sym *symbol = NULL;
#else
    const Elf32_Sym *symbol = NULL;
#endif
    blit_crash_text("  #");
    blit_crash_decimal((unsigned long)index);
    blit_crash_text(" ");
    blit_crash_hex(address);
    memset(&info, 0, sizeof(info));
    if (dladdr1((void *)address, &info, (void **)&symbol, RTLD_DL_SYMENT) != 0) {
        if (info.dli_sname != NULL && info.dli_saddr != NULL && symbol != NULL &&
            address - (uintptr_t)info.dli_saddr < (uintptr_t)symbol->st_size) {
            blit_crash_text(" ");
            blit_crash_text(info.dli_sname);
            blit_crash_text("+");
            blit_crash_hex(address - (uintptr_t)info.dli_saddr);
        }
        if (info.dli_fname != NULL) {
            blit_crash_text(" (");
            blit_crash_text(info.dli_fname);
            blit_crash_text(")");
        }
    }
    blit_crash_text("\n");
    /* The program's own: the object blit_crash_install is in. */
    return info.dli_fbase != NULL && info.dli_fbase == blit_crash_program_base;
}

/* The program's frames through addr2line, which reads tcc's debug
   information in the file: function, file and line each. Run in a child
   (fork and execve are safe here), its output to standard error, waited
   for; nothing when binutils is not installed. */
static void blit_crash_lines(const uintptr_t *addresses, int count)
{
    static char text[BLIT_CRASH_FRAMES][2 + 2 * sizeof(uintptr_t) + 1];
    static char *argv[5 + BLIT_CRASH_FRAMES];
    static char *const environment[] = { NULL };
    static const char addr2line[] = "/usr/bin/addr2line";
    pid_t child;
    int i, n = 0;
    if (count <= 0 || blit_crash_program[0] == '\0' || access(addr2line, X_OK) != 0)
        return;
    argv[n++] = (char *)"addr2line";
    argv[n++] = (char *)"-f";
    argv[n++] = (char *)"-e";
    argv[n++] = blit_crash_program;
    for (i = 0; i < count && i < BLIT_CRASH_FRAMES; ++i) {
        uintptr_t value = addresses[i];
        int at = (int)sizeof(text[i]) - 1, start;
        text[i][at] = '\0';
        do {
            text[i][--at] = "0123456789abcdef"[value & 15u];
            value >>= 4;
        } while (value != 0 && at > 2);
        text[i][--at] = 'x';
        text[i][--at] = '0';
        /* Left-aligned in its slot: argv points at the first digit. */
        start = at;
        argv[n++] = text[i] + start;
    }
    argv[n] = NULL;
    blit_crash_text("blit: the program's frames, function then file:line (addr2line):\n");
    child = fork();
    if (child == 0) {
        dup2(2, 1);
        execve(addr2line, argv, environment);
        _exit(127);
    }
    if (child > 0)
        while (waitpid(child, NULL, 0) < 0 && errno == EINTR) {}
}

static void blit_crash_handler(int signal_number, siginfo_t *info, void *context)
{
    const ucontext_t *uc = context;
    uintptr_t pc, frame, own[BLIT_CRASH_FRAMES];
    int k, owned = 0;
#if defined(__x86_64__)
    pc = (uintptr_t)uc->uc_mcontext.gregs[REG_RIP];
    frame = (uintptr_t)uc->uc_mcontext.gregs[REG_RBP];
#else
    pc = 0;
    frame = 0;
    (void)uc;
#endif
    blit_crash_text("blit: fatal signal ");
    blit_crash_decimal((unsigned long)signal_number);
    blit_crash_text(signal_number == SIGSEGV ? " (SIGSEGV)" : signal_number == SIGBUS ? " (SIGBUS)" :
                    signal_number == SIGILL ? " (SIGILL)" : signal_number == SIGFPE ? " (SIGFPE)" :
                    signal_number == SIGABRT ? " (SIGABRT)" : "");
    blit_crash_text(", address ");
    blit_crash_hex(info != NULL ? (uintptr_t)info->si_addr : 0);
    blit_crash_text("; backtrace (addr2line -f -e <program> <address> for file and line):\n");
    if (blit_crash_frame(0, pc))
        own[owned++] = pc;
    for (k = 1; k < BLIT_CRASH_FRAMES && frame != 0 && (frame & 7u) == 0; ++k) {
        uintptr_t next = ((const uintptr_t *)frame)[0];
        uintptr_t back = ((const uintptr_t *)frame)[1];
        if (back == 0)
            break;
        /* The return address: the call's own line is just before it. */
        if (blit_crash_frame(k, back - 1) && owned < BLIT_CRASH_FRAMES)
            own[owned++] = back - 1;
        if (next <= frame || next - frame > (uintptr_t)16 << 20)
            break;
        frame = next;
    }
    blit_crash_lines(own, owned);
    /* SA_RESETHAND made the action the default again: returning re-runs
       the faulting instruction, which now ends the process; an abort is
       raised again. */
    if (signal_number == SIGABRT)
        raise(SIGABRT);
}

static void blit_crash_install(void)
{
    static char alternate[64 * 1024];
    static const int signals[] = { SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT };
    struct sigaction action;
    stack_t stack;
    Dl_info self;
    ssize_t length;
    size_t i;
    memset(&self, 0, sizeof(self));
    if (dladdr((void *)blit_crash_install, &self) != 0)
        blit_crash_program_base = self.dli_fbase;
    length = readlink("/proc/self/exe", blit_crash_program, sizeof(blit_crash_program) - 1);
    blit_crash_program[length > 0 ? length : 0] = '\0';
    stack.ss_sp = alternate;
    stack.ss_size = sizeof(alternate);
    stack.ss_flags = 0;
    sigaltstack(&stack, NULL);
    memset(&action, 0, sizeof(action));
    action.sa_sigaction = blit_crash_handler;
    action.sa_flags = SA_SIGINFO | SA_RESETHAND | SA_ONSTACK;
    sigemptyset(&action.sa_mask);
    for (i = 0; i < sizeof(signals) / sizeof(signals[0]); ++i)
        sigaction(signals[i], &action, NULL);
}

#endif
