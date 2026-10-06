#!/usr/bin/env python3
"""
Generates ngspice reference data for the AC30C2 model and writes tests/Ac30SpiceReference.h.

Two circuits, both from the factory schematics (VOX R&D UK, ISS3a) with the full Koren models
(inter-electrode capacitances, grid diodes) of spice/koren_ac30.inc:

  preamp: input jack -> V1 (shared cathode) -> Normal / Top Boost networks -> V2a -> V2b cathode
          follower -> Top Boost stack -> U1B mixer (ideal op-amp)          -> H1, THD of the U1B output
  phase inverter: C45 -> V3 long-tailed pair -> C50/C51 -> Tone Cut -> R105/R112 -> Master -> EL84
          grids (differential)                                                   -> H1 of the grid drive

The supply rails are the model's own DC solution (Ac30Tests prints them), so the comparison checks
the circuit modelling (Miller approximation of V2a, Newton solvers, networks) rather than the supply.
Requires ngspice on PATH.
"""
import math, os, re, subprocess, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, '..', 'tests', 'Ac30SpiceReference.h')
INC = os.path.join(HERE, 'koren_ac30.inc')

# Model DC rails (B+3, B+4, B+5) at the default operating point
B3, B4, B5 = 291.9, 267.4, 245.9

def pot_lin(k): return min(max(k / 10.0, 1e-5), 0.99999)
def pot_audio(k): return min(max(pot_lin(k) ** (math.log(0.1) / math.log(0.5)), 1e-5), 0.99999)

def run(netlist, node, freq):
    with tempfile.NamedTemporaryFile('w', suffix='.cir', delete=False) as f:
        f.write(netlist)
        name = f.name
    out = subprocess.run(['ngspice', '-b', name], capture_output=True, text=True).stdout
    os.unlink(name)
    thd = float(re.search(r'THD:\s*([0-9.eE+-]+)', out).group(1))
    h1 = None
    for line in out.splitlines():
        p = line.split()
        if len(p) >= 3 and p[0] == '1' and abs(float(p[1]) - freq) < 1e-3 * freq:
            h1 = float(p[2])
    if h1 is None:
        raise RuntimeError(out[-2000:])
    return h1, thd

def tran(freq, node):
    period = 1.0 / freq
    settle = 0.4
    return f""".options reltol=1e-4 abstol=1e-12 vntol=1e-7
.control
tran {period / 400} {settle + 20 * period} {settle} {period / 400}
fourier {freq} {node}
.endc
.end
"""

def preamp_netlist(c, amp, freq):
    n, t = pot_audio(c['normal']), pot_audio(c['tb'])
    tr, b = pot_audio(c['treble']), pot_audio(c['bass'])
    normal_in = c['input'] == 'normal'
    return f"""* AC30C2 preamp reference
.include {INC}
Vb5 b5 0 {B5}
Vb4 b4 0 {B4}
Vin in 0 SIN(0 {amp} {freq})
RsT {'0' if normal_in else 'in'} gT 38k
RsN {'in' if normal_in else '0'} gN 38k
C13 gT k1 120p
XV1T pT gT k1 12AX7
XV1N pN gN k1 12AX7
R14 b5 pT 220k
R12 b5 pN 100k
R16 k1 0 1.5k
C11 k1 0 22u
C7 pN na 47n
R13 na nb 330k
C8 na nb 120p
RvnA nb nw {500e3 * (1 - n)}
RvnB nw 0 {500e3 * n}
R37 nw 0 470k
C9 pT tt 470p
RvtA tt tw {500e3 * (1 - t)}
RvtB tw 0 {500e3 * t}
C15 tt tw 120p
XV2A pA tw kA 12AX7
R32 b4 pA 100k
R26 kA 0 1.5k
C18 kA 0 22u
XV2B b4 pA kB 12AX7
R21 kB 0 56k
C84 kB 0 1n
C23 kB st 56p
RtrA st sw {1e6 * (1 - tr)}
RtrB sw jj {1e6 * tr}
R19 kB aa 100k
C28 aa jj 22n
Rbass jj bn {1e6 * b}
C38 aa bn 22n
R47 bn 0 10k
C25 sw o1 220n
R34 o1 pp 330k
R35 pp 0 120k
Bout out 0 V = {{v(pp) * (1 + 820/470) - v(nw) * (820/470)}}
""" + tran(freq, 'v(out)')

def pi_netlist(c, amp, freq):
    rcut = 100 + 220e3 * (1 - pot_lin(c['cut']))
    rmaster = 10 + 500e3 * pot_audio(c['master'])
    return f"""* AC30C2 phase inverter / Tone Cut / Master reference
.include {INC}
Vb3 b3 0 {B3}
Vin in 0 SIN(0 {amp} {freq})
C45 in gA 100n
R57 gA tt 1meg
R63 gB tt 1meg
C48 gB 0 100n
R55 kk tt 1.2k
R60 tt 0 47k
R67 b3 pA 100k
R70 b3 pB 100k
C42 pA pB 47p
XV3A pA gA kk 12AX7
XV3B pB gB kk 12AX7
C50 pA oA 100n
C51 pB oB 100n
Rcut oB cm {rcut}
C80 cm oA 4.7n
R112 oA opP 10k
R105 oB opM 10k
Rmaster opP opM {rmaster}
R118 opP qq 220k
R115 qq opM 220k
R114 qq 0 1meg
Rdepth qq 0 10
R113 opP 0 1meg
R116 opM 0 1meg
RsP opP eP 1.65k
RsM opM eM 1.65k
CeP eP 0 25p
CeM eM 0 25p
Bdiff dd 0 V = {{v(eP) - v(eM)}}
""" + tran(freq, 'v(dd)')

PREAMP_CONFIGS = [
    dict(name='tb_default', input='tb', normal=0, tb=5, treble=6, bass=5),
    dict(name='tb_bright_hot', input='tb', normal=0, tb=8, treble=8, bass=2),
    dict(name='normal', input='normal', normal=7, tb=5, treble=5, bass=5),
]
PREAMP_CASES = [(0.01, 100.0), (0.01, 500.0), (0.01, 2000.0), (0.01, 6000.0), (0.1, 1000.0), (0.5, 1000.0)]

PI_CONFIGS = [
    dict(name='cut0_master10', cut=0, master=10),
    dict(name='cut5_master6', cut=5, master=6),
    dict(name='cut10_master3', cut=10, master=3),
]
PI_CASES = [(0.1, 100.0), (0.1, 1000.0), (0.1, 5000.0)]

pre_rows, pi_rows = [], []
for ci, c in enumerate(PREAMP_CONFIGS):
    for amp, freq in PREAMP_CASES:
        h1, thd = run(preamp_netlist(c, amp, freq), 'v(out)', freq)
        pre_rows.append((ci, amp, freq, h1, thd))
        print('preamp', c['name'], amp, freq, h1, thd)
for ci, c in enumerate(PI_CONFIGS):
    for amp, freq in PI_CASES:
        h1, thd = run(pi_netlist(c, amp, freq), 'v(dd)', freq)
        pi_rows.append((ci, amp, freq, h1, thd))
        print('pi', c['name'], amp, freq, h1, thd)

with open(OUT, 'w') as f:
    f.write('// Generated by spice/gen_ac30_refs.py (ngspice) - do not edit.\n#pragma once\n\n')
    f.write(f'inline constexpr double kAc30RefB3 = {B3}, kAc30RefB4 = {B4}, kAc30RefB5 = {B5};\n\n')
    f.write('struct Ac30PreampConfig { const char* name; bool normalInput; float normal, topBoost, treble, bass; };\n')
    f.write('struct Ac30PiConfig { const char* name; float toneCut, master; };\n')
    f.write('struct Ac30Case { int config; float amplitude, frequency; double fundamental, thdPercent; };\n\n')
    f.write('inline constexpr Ac30PreampConfig kAc30PreampConfigs[] = {\n')
    for c in PREAMP_CONFIGS:
        f.write(f"    {{ \"{c['name']}\", {str(c['input'] == 'normal').lower()}, {float(c['normal'])}f, {float(c['tb'])}f, "
                f"{float(c['treble'])}f, {float(c['bass'])}f }},\n")
    f.write('};\n\ninline constexpr Ac30Case kAc30PreampCases[] = {\n')
    for ci, amp, freq, h1, thd in pre_rows:
        f.write(f'    {{ {ci}, {amp}f, {freq}f, {h1!r}, {thd!r} }},\n')
    f.write('};\n\ninline constexpr Ac30PiConfig kAc30PiConfigs[] = {\n')
    for c in PI_CONFIGS:
        f.write(f"    {{ \"{c['name']}\", {float(c['cut'])}f, {float(c['master'])}f }},\n")
    f.write('};\n\ninline constexpr Ac30Case kAc30PiCases[] = {\n')
    for ci, amp, freq, h1, thd in pi_rows:
        f.write(f'    {{ {ci}, {amp}f, {freq}f, {h1!r}, {thd!r} }},\n')
    f.write('};\n')
