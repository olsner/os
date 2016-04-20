#!/usr/bin/env python

import os
import sys

import ninja_syntax

w = None
OUTDIR = "out"
GRUBDIR = os.path.join(OUTDIR, "grub")

KERNELS = []
MODFILES = []
MODULES = []

def rules():
    w.variable("cc", "ccache gcc")
    w.variable("cxx", "ccache g++")
    w.variable("ld", "ld.bfd")

    w.variable("depflags", "-MP -MMD")
    w.variable("cflags", "$depflags")
    w.variable("cxxflags", "$cflags")
    w.variable("ldflags", "--check-sections --gc-sections")

    w.rule("host_cc_exe", "$cc $cflags -o $out -MF $out.d $in",
        description = "[CC] $out",
        depfile = "$out.d")
    w.rule("host_cxx_exe", "$cxx $cxxflags -o $out -MF $out.d $in",
        description = "[CXX] $out",
        depfile = "$out.d")
    # %.d: %.asm
    w.variable("yasm", "yasm/yasm")
    w.variable("yasmflags", "-Werror -i kasm -i include")

    w.rule("yasm_dep", "$yasm $yasmflags -e -M $in -o $o_out >$out",
        description = "[ASM.d] $out")
    # %.b: %.asm %.d
    w.rule("yasm_bin", "$yasm $yasmflags -f bin $in -o $out -L nasm -l $listfile --mapfile=$mapfile",
        description = "[ASM] $out")
    w.rule("yasm_elf", "$yasm $yasmflags -f elf64 -g dwarf2 $in -o $out -L nasm",
        description = "[ASM] $out")
    w.rule("yasm_size", "echo $in: `stat -c %s $in` bytes")

    w.rule("copy", "cp -u $in $out")

    w.variable("base_user_cflags", "$depflags " +
        "-ffreestanding -g -Os " +
        "-march=native -mno-avx -std=gnu99 " +
        "-ffunction-sections -fdata-sections " +
        "-Wno-unused-function -Wno-unused-parameter -Wstrict-prototypes " +
        "-Werror -W -Wall -Wextra")
    w.variable("user_cflags", "$base_user_cflags")
    w.variable("user_ldflags", "$ldflags")
    w.variable("user_ldscript", "cuser/linker.ld")

    w.rule("user_cc", "$cc $user_cflags -c -o $out -MF $out.d $in",
        description = "[CC] $out",
        depfile = "$out.d")
    # For some reason the same?
    w.rule("user_cxx", "$cc $user_cflags -c -o $out -MF $out.d $in",
        description = "[CC] $out",
        depfile = "$out.d")
    w.rule("user_ld", "$ld $user_ldflags -o $out -T $user_ldscript $in",
        description = "[LD] $out")

    w.rule("lwip_cc", "$cc $user_cflags $lwip_cflags -c -o $out -MF $out.d $in",
        description = "[CC] $out",
        depfile = "$out.d")

    w.variable("objcopy", "objcopy")
    w.rule("objcopy", "$objcopy -O binary $in $out",
        description = "[OBJCOPY] $out")

def mkoutfile(outdir, infile, extension = None):
    base,ext = os.path.splitext(infile)
    if extension: ext = "." + extension
    return os.path.join(outdir, base + ext)

def outfile(infile, extension = None):
    return mkoutfile(OUTDIR, infile, extension)

def grubfile(infile, extension = None):
    return mkoutfile(GRUBDIR, infile, extension)

def yasm_bin(asm, out = None):
    if out is None:
        out = outfile(asm, "b")
    # TODO yasm dep
    w.build(out, "yasm_bin", [asm], variables = {
        "listfile": outfile(asm, "lst"),
        "mapfile": outfile(asm, "map"),
    })
    return out

MOD_ASMFILES = \
    "user/newproc.asm user/gettime.asm user/loop.asm user/shell.asm " + \
    "user/test_puts.asm user/test_xmm.asm " + \
    "kern/console.asm kern/pic.asm kern/irq.asm"
MOD_CFILES = \
    "helloworld.c zeropage.c test_maps.c e1000.c apic.c timer_test.c " + \
    "bochsvga.c fbtest.c acpi_debugger.c"

def user_cc(c, **kwargs):
    o = outfile(c, "o")
    w.build(o, "user_cc", [c], **kwargs)
    return o

def user_ld(main, ofiles):
    elf = outfile(main, "elf")
    w.build(elf, "user_ld", ofiles, implicit = ["$user_ldscript"])
    return elf

def yasm_elf(asm):
    out = outfile(asm, "o")
    # TODO yasm dep
    w.build(out, "yasm_elf", [asm])
    return out

def objcopy(elf, out):
    w.build(out, "objcopy", [elf])
    return out

def modules():
    global MODFILES
    global MODULES
    for asm_mod in MOD_ASMFILES.split():
        base,_ = os.path.splitext(asm_mod)
        MODULES += [base]
        out = yasm_bin(asm_mod)
        mod = grubfile(asm_mod, "mod")
        w.build(mod, "copy", [out])
        MODFILES += [mod]

    printf = yasm_elf("cuser/printf.asm")
    strings = user_cc("cuser/string.c")

    acpica(strings)

    WANT_PRINTF = ["test_maps", "zeropage", "timer_test"]
    WANT_REAL_PRINTF = ["e1000", "apic", "bochsvga", "fbtest"]
    # c -> o -> elf -> mod
    for c_mod in MOD_CFILES.split():
        base,_ = os.path.splitext(c_mod)
        c_mod = os.path.join("cuser", c_mod)
        MODULES += ["cuser/" + base]
        o = user_cc(c_mod)
        deps = []
        if base in WANT_REAL_PRINTF:
            deps += [outfile("cuser/acpica/printf.o"),
                outfile("acpica/source/components/utilities/utclib.o")]
        elif base in WANT_PRINTF:
            deps += [printf]
        elf = user_ld(c_mod, [o] + deps)
        mod = objcopy(elf, grubfile(c_mod, "mod"))
        MODFILES += [mod]

    #lwip()

ACPICA_SOURCES = """
utilities:
    xface xferror xfinit excep debug global alloc clib track decode string
    math cache mutex lock delete object state misc address ownerid error osi
    eval ids copy predef buffer resrc init
tables:
    xface xfload instal utils print fadt find xfroot
events:
    xface glock xfevnt gpeblk event region handler misc gpe rgnini gpeutil sci
    xfregn gpeinit
namespace:
    xfeval access utils load object walk names eval arguments predef alloc
    init parse dump search xfname xfobj prepkg repair repair2 convert
executer:
    utils mutex resnte system dump region prep resop resolv convrt create
    names field store fldio debug oparg1 oparg2 oparg3 oparg6 storen misc
    config storob
debugger:
    xface input utils histry method fileio exec disply cmds names stats
    convert
dispatcher:
    init wscope wstate opcode wload mthdat object utils field wload2 method
    wexec args control
hardware:
    xface acpi gpe pci regs xfsleep esleep sleep valid
parser:
    xface scope utils walk tree opinfo parse opcode args loop object
disassembler:
    walk object utils opcode names buffer deferred resrc resrcs resrcl resrcl2
resources:
    xface create dump info list dumpinfo utils calc memory io irq serial misc
    addr
"""
ACPICA_CORE = "acpica/source/components"

def parse_map(s):
    res = {}
    key = None
    for l in s.split('\n'):
        if not l: continue
        if l[-1] == ':':
            key = l[:-1]
        else:
            assert key is not None
            res.setdefault(key, []).extend(filter(len, l.split()))
    return res

def acpica_cc(src):
    return user_cc(src,
        variables = { 'user_cflags': '$base_user_cflags $acpica_cflags' })

def acpica(strings_o):
    w.variable("acpica_cflags",
        "-Icuser -Icuser/acpica -Iacpica/source/include " +
        "-DACENV_HEADER=\\\"acenv_header.h\\\" -DACPI_FULL_DEBUG " +
        "-fno-strict-aliasing")

    sources = []
    prefixes = {
        'tables': 'tb',
        'namespace': 'ns',
        'hardware': 'hw',
        'parser': 'ps',
        'disassembler': 'dm',
        'dispatcher': 'ds',
        'resources': 'rs',
        'debugger': 'db',
    }
    for d, fs in parse_map(ACPICA_SOURCES).items():
        prefix = prefixes.get(d, d[:2])
        for f in fs:
            sources.append(os.path.join(ACPICA_CORE, d, prefix + f + ".c"))

    sources += [os.path.join("cuser", "acpica", s) for s in
        ["acpica.c","osl.c","malloc.c","pci.c","printf.c"]]

    w.comment("ACPI SOURCES")
    for s in sources:
        w.comment(s)

    objects = [strings_o]
    for s in sources:
        objects.append(acpica_cc(s))

    acpica_elf = user_ld("cuser/acpica", objects)
    mod = objcopy(acpica_elf, grubfile("cuser/acpica.mod"))

    global MODFILES
    MODFILES += [mod]

    return mod

def asm_kernel():
    global KERNELS
    out = yasm_bin("kasm/kstart.asm")
    grub = grubfile("kstart.b", "b")
    w.build(grub, "copy", [out])
    KERNELS += [grub]

def iso_image():
    w.variable("grub_libdir", "/usr/lib/grub/i386-pc/")
    w.variable("grub_modules", "boot multiboot")
    w.rule("grub-mkrescue", "grub-mkrescue --modules=\"$grub_modules\" -d $grub_libdir -o $out $grubdir")
    w.rule("mkgrubcfg", "build/mkgrubcfg.sh $mods > $out")

    global KERNELS
    global MODFILES
    global MODULES

    cfg = grubfile("boot/grub/grub.cfg")
    w.build(cfg, "mkgrubcfg", variables = {
        "mods": " ".join(MODULES),
    })
    iso = outfile("grub.iso")
    w.build(iso, "grub-mkrescue", [ cfg ] + KERNELS + MODFILES,
        variables = { "grubdir": GRUBDIR })
    return iso

def main():
    global w
    w = ninja_syntax.Writer(sys.stdout) # open("build.ninja", "w"))
    w.variable("builddir", OUTDIR)
    w.comment("rules")
    rules()
    w.comment("modules")
    modules()
    w.comment("asm_kernel")
    asm_kernel()
    #utilities()
    w.comment("iso_image")
    w.default(iso_image())
    #subdirs()
    #w.close()
    return 0

if __name__=='__main__':
    sys.exit(main())
