* Koren tube models for the AC30C2 references (N. Koren, "Improved vacuum tube models for SPICE"),
* ngspice B-sources, same equations as source/dsp/TriodeModel.h.
.model DX D(IS=1N RS=1 CJO=10PF TT=1N)

.subckt TRIODE 1 2 3 params: MU=100 EX=1.4 KG1=1060 KP=600 KVB=300 VCT=0 RGI=2000 CCG=1.6p CGP=1.7p CCP=0.46p
B1 1 3 I = {2*pwr(max((max(v(1,3),0)/KP)*ln(1+exp(min(KP*(1/MU+(v(2,3)+VCT)/sqrt(KVB+max(v(1,3),0)^2)),50))),1e-12),EX)/KG1}
C1 2 3 {CCG}
C2 2 1 {CGP}
C3 1 3 {CCP}
R1 2 5 {RGI}
D3 5 3 DX
.ends TRIODE

* 12AX7 (original Koren parameters, RCA capacitances)
.subckt 12AX7 1 2 3
X1 1 2 3 TRIODE MU=100 EX=1.4 KG1=1060 KP=600 KVB=300 VCT=0 RGI=2000 CCG=1.6p CGP=1.7p CCP=0.46p
.ends 12AX7

* EL84 / 6BQ5: A G2 G1 K. Fitted to the Philips data at Va = Vg2 = 250 V, Vg1 = -7.3 V
* (Ia 48 mA, Ig2 5.5 mA, gm 11.3 mA/V, ra 38k, mu g2-g1 19).
.subckt EL84 1 2 3 4
B1 1 4 I = {2*pwr(max((max(v(2,4),1)/200)*ln(1+exp(min((1/19+v(3,4)/max(v(2,4),1))*200,50))),1e-12),1.35)/630*atan(max(v(1,4),0)/49)}
B2 2 4 I = {pwr(max(v(2,4)/19+v(3,4),0),1.35)/2000}
C1 3 4 10.8p
C2 1 3 0.5p
C3 1 4 6.5p
R1 3 5 1000
D3 5 4 DX
.ends EL84
