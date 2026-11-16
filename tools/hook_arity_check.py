#!/usr/bin/env python3
"""rehatched-nx: checks Renut's Direct3D hook mappings by argument count.

For every BANJO_RENUT_HOOK(sub_X, function) it compares how many of the argument registers r3..r10 the guest function
sub_X reads before writing them or making its first call (from the generated code's disassembly comments, one pass in
address order: an approximation) with the number of parameters of the hook function. A guest function that reads
more argument registers than the hook takes, or a hook that takes more than the guest reads, is a likely wrong
address (this port found CreateTexture hooked as CreateSurface and InsertFence hooked as InsertCallback this way).
Float arguments (f1..) and raw hooks are not checked.

  python3 tools/hook_arity_check.py [--all]
"""
import glob
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GEN = os.path.join(ROOT, 'app', 'generated')
HOOKS = os.path.join(ROOT, 'app', 'src', 'renut_engine', 'gpu')

ARG_REGS = ['r%d' % i for i in range(3, 11)]
# Mnemonics whose operands are all sources.
ALL_SOURCES = re.compile(r'^(st[a-z]*|cmp[a-z]*|tw[a-z]*|td[a-z]*|mt[a-z]*|dcb[a-z]*|b[a-z]*|icbi|sync|lwsync|eieio)$')


def guest_bodies():
    bodies = {}
    for path in glob.glob(os.path.join(GEN, '*_recomp.*.cpp')):
        text = open(path, errors='replace').read()
        for m in re.finditer(r'^DEFINE_REX_FUNC\(sub_([0-9A-F]{8})\) \{\n(.*?)^\}', text, re.M | re.S):
            asm = []
            lines = m.group(2).split('\n')
            for i, l in enumerate(lines):
                if not l.strip().startswith('// '):
                    continue
                ins = l.strip()[3:]
                # a bl to the save / restore helpers is part of the prologue, not a call
                nxt = ' '.join(lines[i + 1:i + 3])
                if ins.startswith('bl ') and re.search(r'__(save|rest)(gpr|fpr|vmx)', nxt):
                    continue
                asm.append(ins)
            bodies[m.group(1)] = asm
    return bodies


def args_read(asm):
    written, read = set(), set()
    for line in asm:
        parts = line.split(None, 1)
        if not parts:
            continue
        op = parts[0].rstrip('.')
        if op in ('bl', 'bctrl', 'blrl') or (op.startswith('b') and op.endswith('l') and op not in ('bclr',)):
            break
        if op in ('blr',):
            break
        ops = re.findall(r'\br\d+\b', parts[1]) if len(parts) > 1 else []
        if ALL_SOURCES.match(op):
            srcs, dsts = ops, []
        else:
            dsts, srcs = ops[:1], ops[1:]
            # update forms (lwzu, stwu...) write the base too; mr. etc fine
        for r in srcs:
            if r in ARG_REGS and r not in written:
                read.add(r)
        for r in dsts:
            written.add(r)
    if not read:
        return 0
    return max(ARG_REGS.index(r) for r in read) + 1


def hook_params():
    params = {}
    for path in glob.glob(os.path.join(HOOKS, '**', '*.cpp'), recursive=True):
        text = open(path, errors='replace').read()
        for m in re.finditer(r'^(?:template\s*<[^>]*>\s*)?(?:static\s+)?(?:inline\s+)?(?:[\w:<>]+)\s+(\w+)\s*\(([^)]*)\)\s*\{', text, re.M):
            name, plist = m.group(1), m.group(2).strip()
            if plist in ('', 'void'):
                n = 0
            elif 'PPCContext' in plist:
                continue
            else:
                n = len([p for p in plist.split(',') if p.strip()])
                if re.search(r'\b(float|double)\b', plist):
                    n = -1  # float arguments: not checked
            params.setdefault(name, n)
    return params


def main():
    bodies = guest_bodies()
    params = hook_params()
    show_all = '--all' in sys.argv
    hooks = []
    for path in sorted(glob.glob(os.path.join(HOOKS, 'd3d', 'hooks', '*.cpp'))):
        for m in re.finditer(r'^BANJO_RENUT_HOOK(?:_SHARED)?\(sub_([0-9A-F]{8}),\s*(\w+)(?:<[^>]*>)?\);\s*(?://\s*(\S+))?',
                             open(path).read(), re.M):
            hooks.append((os.path.basename(path), m.group(1), m.group(2), m.group(3) or m.group(2)))
    bad = 0
    for f, addr, func, name in hooks:
        if addr not in bodies:
            print('%-12s sub_%s %-45s no generated function' % (f, addr, name))
            bad += 1
            continue
        g = args_read(bodies[addr])
        h = params.get(func)
        if h is None or h < 0:
            if show_all:
                print('%-12s sub_%s %-45s guest %d, hook ?' % (f, addr, name, g))
            continue
        flag = g > h or (h > g and h - g >= 2)
        if flag:
            bad += 1
        if flag or show_all:
            print('%-12s sub_%s %-45s guest reads %d args, hook takes %d%s' % (f, addr, name, g, h, '  <-- CHECK' if flag else ''))
    print('%d hooks, %d to check' % (len(hooks), bad))


if __name__ == '__main__':
    main()
