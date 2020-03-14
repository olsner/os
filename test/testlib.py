#!/usr/bin/env python3
import argparse
import glob
import os
import signal
import subprocess
import sys

"""
A: recv(B)
B: pulse(A, 1)
A: <(pulse, B, 1)

B: pulse(A, 1)
A: recv(B)
A: <(pulse, B, 1)
"""

# Probably want the raw constants here
RECV = 'SYS_RECV'
PULSE = 'MSG_PULSE'
WRITE = 'SYSCALL_WRITE'

DEBUG = False

class Action(object):
    def __init__(self):
        self.id = None
        self.sequence = None
        self.process = None

    def emit(self):
        raise NotImplementedError(self)

    def wait_for_master(self):
        return f"""
puts(\"{self.process} waiting before step {self.id}\");
wait_for_master(M, {self.id});
puts(\"{self.process} starting step {self.id}\");"""

    def send_result(self, *results):
        res = ''
        if DEBUG:
            res += f'printf("{self.process} step {self.id}: {", ".join(["%d"] * len(results))}\\n", {", ".join(map(str, results))});'
        res += f"\nsend{len(results) + 1}(MSG_RESULT, M, {self.id}, {', '.join(map(str, results))});"
        return res

    def expected_result(self):
        return []

    def declarations(self):
        return []

class Result(Action):
    def __init__(self, action, *variables):
        super().__init__()
        self.action = action
        self.variables = variables
        self.expected = None

    def __str__(self):
        return f"expect({{ {', '.join(map(str, self.variables))} }} == {{ {', '.join(map(str, self.expected))} }})"

    def expect(self, *args):
        self.expected = args
        self.action.process.add(self)

    def emit(self):
        return self.send_result(*self.variables)

    def expected_result(self):
        return self.expected

class Syscall(Action):
    """
    A syscall has input-only arguments and a single return value.
    """
    def __init__(self, syscall, *args):
        super().__init__()
        self.syscall = syscall
        self.args = args

    def __str__(self):
        args = ', '.join(map(str, self.args))
        return f"syscall{len(self.args)}({self.syscall}, {args})"

    def emit(self):
        return f"result{self.id} = {self};"

    def result(self):
        return Result(self, f"result{self.id}")

    def declarations(self):
        return (f"result{self.id}",)

class Recv(Action):
    """
    Receive from a named process (None should eventually be supported to receive from anywhere).
    """
    def __init__(self, rcpt, nargs):
        super().__init__()
        self.rcpt = rcpt
        self.nargs = nargs

    def __str__(self):
        return f"recv{self.nargs}(self.proc)"

    def args(self, prefix = "", join = None):
        res = [f"{prefix}arg{self.id}_{i}" for i in range(self.nargs)]
        if join:
            return join.join(res)
        return res

    def declarations(self):
        yield f"result{self.id}"
        yield f"rcpt{self.id} = {self.rcpt}"
        yield from self.args()

    def emit(self):
        return f"result{self.id} = recv{self.nargs}(&rcpt{self.id}, {self.args('&', ', ')});"

    def result(self):
        return Result(self, f"result{self.id}", f"rcpt{self.id}", *self.args())

class Process(object):
    def __init__(self, sequence, name = None):
        self.actions = []
        self.sequence = sequence
        self.name = name or sequence.assign_name()

    def __str__(self):
        return self.name

    def add(self, action):
        self.actions.append(action)
        self.sequence.add(self, action)
        return action

    def recv(self, process, nargs):
        return self.add(Recv(process, nargs)).result()

    def pulse(self, process, bits):
        return self.add(Syscall(PULSE, process, bits)).result()

    def syscall(self, syscall, *args):
        return self.add(Syscall(syscall, *args)).result()

    def emit(self, h):
        if DEBUG:
            print(f'puts("{self.name} started...");', file=h)
        for a in self.actions:
            for decl in a.declarations():
                print(f"uintptr_t {decl};", file=h)
        for a in self.actions:
            print(f"// #{a.id}: {a}", file=h)
            wait = a.wait_for_master()
            if wait: print(wait, file=h)
            print(a.emit(), file=h)
        if DEBUG:
            print(f'puts("{self.name} finished.");', file=h)
        print("abort();", file=h)

class Sequence(object):
    def __init__(self, name = "M"):
        self.processActions = []
        self.counter = 0
        self.name = name

    def assign_name(self):
        self.counter += 1
        return f"proc{self.counter}"

    def add(self, process, action):
        action.id = len(self.processActions)
        action.sequence = self
        action.process = process
        self.processActions.append((process,action))
        #print("Added", action, "to", process)

    def emit(self, h):
        """
        Generate code for the master process on 'h' (a file handle).
        """
        if DEBUG:
            print('puts("Master started...");', file=h)
        for proc, act in self.processActions:
            print(f"\n// {proc}: #{act.id}: {act}", file=h)
            if DEBUG:
                print(f'puts("Starting step {act.id} in {proc}");', file=h)
            print(f"send1(MSG_STEP, {proc}, {act.id});", file=h)
            if act.expected_result():
                expected = (act.id,) + tuple(act.expected_result())
                if DEBUG:
                    print(f'puts("Checking step {act.id}");', file=h)
                self.emit_check(proc, expected, h)
            if DEBUG:
                print(f'puts("Step {act.id}: OK");', file=h)
        print("pass();", file=h)

    def emit_check(self, proc, expected, h):
        argnames = ', '.join([f"arg{i}" for i in range(len(expected))])
        argps = ', '.join([f"&arg{i}" for i in range(len(expected))])
        argfmts = ' '.join(["%lx" for _ in expected])
        print(f"""{{
ipc_dest_t rcpt = {proc};
ipc_arg_t {argnames};
uintptr_t msg = recv{len(expected)}(&rcpt, {argps});
printf("Received %ld from %ld: {argfmts}\\n", msg, rcpt, {argnames});
ASSERT_EQ(MSG_RESULT, msg);
ASSERT_EQ({proc}, rcpt);""", file=h)
        for i,exp in enumerate(expected):
            print(f"ASSERT_EQ({exp}, arg{i});", file=h)
        print("}", file=h)

def strip_ansi(s):
    res = ""
    ESC = '\033'
    while s:
        ix = s.find(ESC)
        if ix < 0:
            res += s
            break
        res += s[:ix]
        s = s[ix:]
        if s[1] == '[':
            # The ESC [ is followed by any number (including none) of "parameter
            # bytes" in the range 0x30–0x3F (ASCII 0–9:;<=>?), then by any number
            # of "intermediate bytes" in the range 0x20–0x2F (ASCII space and
            # !"#$%&'()*+,-./), then finally by a single "final byte" in the range
            # 0x40–0x7E (ASCII @A–Z[\]^_`a–z{|}~)
            for i in range(2, len(s)):
                if ord(s[i]) >= 0x40 and ord(s[i]) < 0x7f:
                    s = s[i + 1:]
                    break
        else:
            s = s[2:]
    return res

def compile_and_run(procs, verbose = False, kernel = "out/kasm/kstart.b"):
    header="""
#include "test_common.h"
"""
    footer="""
void start() {
    __default_section_init();
    proc_main();
}
"""

    OUTDIR = "out"
    TESTOUT = os.path.join(OUTDIR, "test")
    if not os.path.isdir(TESTOUT): os.mkdir(TESTOUT)

    def emit_function(proc):
        print(f"__attribute__((noreturn)) static void {proc.name}_main(void) {{", file=h)
        proc.emit(h)
        print("}", file=h)

    def compile_to(mod, source, procname):
        cross = "toolchain/cross-8.3.0/bin/x86_64-elf-"
        elf = mod.replace(".mod", ".elf")
        # Usually want to split up compile and link for ccache, but for these
        # short programs the linking is what takes most of the time anyway.
        cflags = "-g -Os -march=sandybridge -mno-avx -ffunction-sections -fdata-sections -W -Wall -Wextra -Werror -Wstrict-prototypes -Wmissing-prototypes -Wmissing-include-dirs -Wno-unused -Icuser/include -Itest"
        ldflags = "-nostdlib -T cuser/linker.ld"
        lib_os = ["stdio_raw", "stdlib", "string", "acpi_strtoul", "ctype"]
        lib_os = " ".join([f"{OUTDIR}/cuser/libc/{f}.o" for f in lib_os])
        lib_os += f" {OUTDIR}/cuser/acpica/printf.o"

        if verbose: print(f"Compiling {elf}...")
        subprocess.check_call(f"ccache {cross}gcc -o {elf} {cflags} {ldflags} {source} {lib_os} -Dproc_main={procname}_main", shell=True)
        subprocess.check_call(f"{cross}objcopy -Obinary {elf} {mod}", shell=True)

    def launch(mods):
        # TODO Silence "terminating on signal" printouts. Ideally keeping
        # stderr so unrecognized errors are still displayed.
        cmd = [
            "qemu-system-x86_64",
            '-cpu', 'max',
            '-kernel', kernel,
            '-initrd', ",".join(mods),
            '-debugcon', 'stdio',
            '-display', 'none']
        if verbose: print(' '.join(map(repr, cmd)))
        output_lines = []
        # TODO Add a timeout for the read (set an alarm that sends ourselves SIGINT?)
        with subprocess.Popen(cmd, stdout=subprocess.PIPE) as p:
            for line in p.stdout:
                line = line.decode('iso-8859-1').strip()
                output_lines.append(line)
                if verbose: print(line)
                # Asm kernel outputs either blue background or reset ANSI codes
                # between each character on the debug console, strip that
                # before matching the string.
                line = strip_ansi(line)
                if "FAIL" in line or "PANIC" in line:
                    if not verbose:
                        for l in output_lines: print(l)
                    p.send_signal(signal.SIGINT)
                    return 1
                elif line == "PASS":
                    # Keep running a little bit and check that the VM doesn't
                    # print more stuff, there could be a way to bug things such
                    # that a program just prints PASS and a bunch of other
                    # garbage..
                    p.send_signal(signal.SIGINT)
                    return 0
        print("No status printed. Error in I/O redirection?")
        return 1

    with open(f"{TESTOUT}/temp.c", "w") as h:
        print(header, file=h)
        for i,proc in enumerate(procs):
            print(f"const uintptr_t {proc.name} = {i + 1};", file=h)
        for proc in procs:
            print("\n", file=h)
            emit_function(proc)
        print(footer, file=h)

    mod_files = []
    for proc in procs:
        mod_file = f"{TESTOUT}/temp_{proc.name}.mod"
        compile_to(mod_file, f"{TESTOUT}/temp.c", proc.name)
        mod_files.append(f"{mod_file} {proc.name}")

    return launch(mod_files)

def processes(n):
    M = Sequence()
    return (M,) + tuple([Process(M) for _ in range(n)])

def with_procs(n):
    def wrap(fun):
        def wrapped():
            procs = processes(n)
            fun(*procs)
            return procs
        return wrapped

    return wrap

def find_tests(globs):
    for name,test in globs.items():
        if name.startswith("test_"): yield name,test

def main(globs = globals()):
    kernel_cpp = "kcpp/out/kernel"
    kernel_asm = "out/kasm/kstart.b"

    parser = argparse.ArgumentParser(description='Run tests')
    parser.add_argument('--verbose', action='store_true', default=False, help='More output.')
    parser.add_argument('--debug', action='store_true', default=False, help='More output.')
    parser.add_argument('--cpp', dest='kernel', action='store_const',
                        const=kernel_cpp, default=kernel_asm,
                        help='Select C++ kernel (kcpp/out/kernel)')
    parser.add_argument('--asm', dest='kernel', action='store_const',
                        const=kernel_asm,
                        help='Select Assembly kernel (out/kasm/kstart.b)')
    parser.add_argument('tests', metavar="TEST", type=str, nargs='*',
                        help='Glob(s) matching tests to run')

    args = parser.parse_args()

    if args.kernel == kernel_cpp:
        print("Testing C++ kernel...")
    elif args.kernel == kernel_asm:
        print("Testing Assembly kernel...")
    else:
        print("Testing custom kernel ({args.kernel})...")

    global DEBUG
    DEBUG = args.debug

    def match_test(name):
        if not args.tests: return True
        for pat in args.tests:
            if glob.fnmatch.fnmatch(name, pat):
                return True
        return False

    fails = 0
    passes = 0
    for name,test in find_tests(globs):
        if not match_test(name):
            continue

        procs = test()
        res = compile_and_run(procs, args.verbose, args.kernel)
        if res:
            fails += 1
        else:
            passes += 1
        print(f"{name}: {'FAIL' if res else 'PASS'}")

    if fails:
        print(f"{fails} test(s) failed")
    print(f"{passes} test(s) passed")

    sys.exit(fails != 0)
