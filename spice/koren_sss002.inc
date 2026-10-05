* Koren tube models as used by the SSS #002 reconstruction (Koren_Tubes.INC, N. Koren / colganr/LTSpiceCircuits),
* rewritten with ngspice B-sources.
.model DX D(IS=1N RS=1 CJO=10PF TT=1N)

.subckt TRIODE 1 2 3 params: MU=100 EX=1.4 KG1=1060 KP=600 KVB=300 VCT=0 RGI=2000 CCG=2.3p CGP=2.4p CCP=0.9p
B1 1 3 I = {2*pwr(max((max(v(1,3),0)/KP)*ln(1+exp(min(KP*(1/MU+(v(2,3)+VCT)/sqrt(KVB+max(v(1,3),0)^2)),50))),1e-12),EX)/KG1}
C1 2 3 {CCG}
C2 2 1 {CGP}
C3 1 3 {CCP}
R1 2 5 {RGI}
D3 5 3 DX
.ends TRIODE

.subckt 5751 1 2 3
X1 1 2 3 TRIODE MU=84.75 EX=1.291 KG1=1137.3 KP=415.40 KVB=1515.2 VCT=0.40 RGI=2000 CCG=2.3p CGP=2.4p CCP=0.9p
.ends 5751

.subckt 7025 1 2 3
X1 1 2 3 TRIODE MU=102.50 EX=1.409 KG1=1598.8 KP=813.82 KVB=44.9 VCT=0.50 RGI=2000 CCG=2.3p CGP=2.4p CCP=0.9p
.ends 7025

* A G2 G1 C
.subckt 6L6GC 1 2 3 4
B1 1 4 I = {2*pwr(max((max(v(2,4),1)/30.98)*ln(1+exp(min((1/9.88+v(3,4)/max(v(2,4),1))*30.98,50))),1e-12),1.442)/1686.6*atan(max(v(1,4),0)/19.4)}
B2 2 4 I = {pwr(max(v(2,4)/9.88+v(3,4),0),1.442)/4500}
C1 3 4 10p
C2 1 3 0.6p
C3 1 4 6.5p
R1 3 5 1000
D3 5 4 DX
.ends 6L6GC
