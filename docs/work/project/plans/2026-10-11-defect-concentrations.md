# Task 100: equilibrium defect concentrations (Domain)

Pure arithmetic on task/96 formation energies. No UI/IO, dependencies, jobs or scan framework.
Existing premake source/test wildcards include the files after project regeneration.

## Equations and sources

Energies are eV; E_F is from VBM, T is kelvin, densities are cm^-3. Chemical potentials supplied
here are **absolute** eV/atom, not Delta mu. All charge states/configurations contribute, including
metastable ones: the formation-energy lower envelope is not a population filter.

\[
 A_{Xq}=E_f(X^q;0,\mu),\quad E_f=A_{Xq}+qE_F,\quad
 c_{Xq}=N_{\mathrm{sites},X}g_{Xq}\exp[-(A_{Xq}+qE_F)/(k_BT)].
\]

`g` = orientation times spin/electronic degeneracy, default 1. Count orientations once, in sites
or degeneracy. Per-defect totals sum all states, without an occupancy denominator or cap.
`BuildConcentrationDefects` reuses `FormationEnergy(...,0)`; it does not duplicate this formula.

Sources in the supplied `scratchpad/txt/` extraction:

- `Van de Walle - First-principles calculations for defects and impurities.txt`, JAP 95, 3851
  (2004), Sec. II.A, Eq. (1): site density/configurations; Sec. II.B.7: self-consistent populations.
- `Persson - n-type doping.txt`, PRB 72, 035211 (2005), Sec. C, ?Self-consistent determination of
  concentrations?, Eq. (12): Boltzmann defects and Fermi-Dirac band integration. Appendix band-edge
  and band-filling corrections belong in the already-corrected energies, not a second correction here.
- `Freysoldt - First principles calculations.txt`, RMP 86, 253 (2014), Secs. II.A.1, II.B.2?3:
  configurational entropy, electron reservoir and neutrality. `Pohl, Albe - Intrinsic point
  defects.txt`, PRB 87, 245203 (2013), Sec. B, ?Point defect formation energies? (around line 320):
  quantitative neutrality includes all coexisting defects.

Use **aggregate DOS masses**, in units of m_0, including all valleys/hole bands but excluding spin:

\[
 N_{c,v}=2(2\pi m_{e,h}^{\mathrm{DOS}}k_BT/h^2)^{3/2},\quad
 F_{1/2}(\eta)=\frac{2}{\sqrt\pi}\int_0^\infty\frac{\sqrt{x}}{1+e^{x-\eta}}dx,
\]
\[
 n=N_c F_{1/2}((E_F-E_g)/k_BT),\qquad p=N_v F_{1/2}(-E_F/k_BT).
\]

Convert SI N_c,v from m^-3 to cm^-3 by 10^-6. Normalization:
[NIST DLMF 25.12.14](https://dlmf.nist.gov/25.12.E14),
[Kim, Wang and Lundstrom, *Notes on Fermi-Dirac Integrals*, Secs. 1?2, Eqs. (8), (10)?(11)](https://arxiv.org/pdf/0811.0116).
Numerics: Simpson after x=u^2 (1024 intervals, tail at max(eta,0)+40); below eta=-16 use
`exp(eta)*(1-exp(eta)/2^(3/2)+exp(2*eta)/3^(3/2))`; above eta=64 use
`4*eta^(3/2)/(3*sqrt(pi))*(1+pi^2/(8*eta^2)+7*pi^4/(640*eta^4)+31*pi^6/(3072*eta^6))`.
Series/Sommerfeld limits: Kim et al., Sec. 4. Independent 40-digit polylogarithm checks at eta
-30, -16.0001, -16, -5, 0, 5, 30, 64, 64.0001, 80, 1000 gave relative errors below 9e-13.

Alternatively, separate tabulated valence/conduction DOS bands include spin, in states/(eV cm^3):

\[
 n=\int_{E_g}^{\infty}D_c(E)f(E)dE,\quad
 p=\int_{-\infty}^{0}D_v(E)[1-f(E)]dE,\quad f(E)=[1+e^{(E-E_F)/kT}]^{-1}.
\]

Each band needs two or more strictly ordered, finite, nonnegative samples and nonzero weight;
valence E <= 0, conduction E >= E_g. DOS is piecewise linear and zero outside the supplied range.
No interpolation across the gap. Integrate filled regions exactly and the Fermi edge with
8-point Gauss-Legendre panels <= 2 kT; truncate each empty segment tail at 40 kT from the larger
of its low edge and E_F. Reflect valence energies to evaluate holes without subtracting f from 1.
Independent scaled high-precision constant/ramped DOS checks at 1/300/3000 K gave log-density
errors below 5e-12. DOS shape/coverage still limits accuracy and attainable carrier density.

\[
 R(E_F)=\sum_{Xq}q c_{Xq}+p-n+N_{\mathrm{fixed}}=0.
\]

N_fixed is **fixed ionized charge**, donors positive, default 0; it is not a partially ionized
fixed dopant total. R decreases with E_F: d(q*c)/dE_F=-q^2*c/kT, dp/dE_F<0, dn/dE_F>0.
Expand [-1,E_g+1] eV up to 64 times, then bisect up to 256 iterations. Use log densities and the
signed sum divided by the largest |q*c|, p, n or |N_fixed|; convergence requires magnitude <=1e-10.
This keeps neutrality meaningful at 1 K even when output densities underflow to zero. Output
overflow fails explicitly; finite DOS unable to compensate N_fixed fails bracketing.

`DSC_1.txt`, ?Charge neutrality requires? (~line 246), gives the single nonlinear E_F equation.
`FP_W8.txt`, Sec. 1, ?Polprzewodnik skompensowany?, distinguishes fixed donor totals and ionization.
The intrinsic nondegenerate check is E_F=E_g/2+(3/4)kT*ln(m_h/m_e), n=p=sqrt(N_c*N_v)*exp(-E_g/(2kT));
these formulas are limiting cases, not identities for degenerate bands. The shallow-donor test
uses a (+/0) level 5 meV below CBM and A_+ chosen for n~N_D; analytically
`n_B^2=N_sites*N_c*exp(-(A_++E_g)/kT)`. A separate check uses N_fixed for fully ionized donors.
No required population equation was changed or fixed-total occupation law added implicitly.

## Inputs, defaults and API

- `ConcentrationDefectInput`: existing charge states, sites/cm^3, optional per-state degeneracies
  (empty = all 1). `BuildConcentrationDefects` returns intercepts or an error for missing mu,
  inconsistent names or invalid numbers. `ConcentrationDefect` accepts precomputed intercepts.
- `EffectiveMassBands` / `TabulatedDosBands`: explicit carrier inputs; zero mass defaults are
  invalid, so the domain never silently chooses a material. Zero sites disables a species;
  negative sites and nonpositive degeneracies are invalid.
- `SolveDefectConcentrations`: positive gap, T in [1,3000] K, optional signed fixed charge. Output:
  T, E_F, n, p, indexed per-state populations/charges, ordered defect totals, relative residual,
  converged flag/error. Check convergence before using populations.
- `SolveDefectConcentrationGrid`: plain Cartesian loops over T, absolute mu and N_fixed, the latter
  fastest; rebuild intercepts once per (T,mu). Keep one indexed result even for failed points.

Diamond vacancy/substitutional default sites: `bulk_atoms/volume_Angstrom3 * 1e24`, from the actual
bulk cell (eight atoms at a=3.567 Angstrom give ~1.76e23 cm^-3). Interstitial/complex multiplicities
need user input. Use the selected `FormationReference.gap`. Suggested panel T window: 300?2000 K.
A **provisional editable** diamond mass preset is m_e=0.46 m_0, m_h=1.25 m_0, from
[Alasio et al., Phys. Rev. Applied 21, 054043 (2024), Sec. VII.B, Table II](https://link.aps.org/accepted/10.1103/PhysRevApplied.21.054043).
As aggregate masses these give N_c(300)=7.829e18, N_v(300)=3.507e19 cm^-3, scaling as (T/300)^(3/2).
Open physics question before a UI preset: confirm the electron mass's valley normalization
against the underlying DOS; diamond has six conduction valleys
([*Ultrafast room-temperature valley manipulation in silicon and diamond*, band discussion](https://pmc.ncbi.nlm.nih.gov/articles/PMC12176610/)).
For a per-valley mass use m_aggregate=6^(2/3)*m_single, equivalently N_c times six. Never multiply
an aggregate mass twice. Bulk DOS normalized by cell volume is preferable; no uncertain diamond
mass/valley count is hardcoded here.

## Limits, later UI and export

Not modelled: T-dependent gap/masses, vibrational entropy, non-dilute interactions/site exclusion,
kinetics, frozen-in totals, band bending or quasi-Fermi levels. Negative formation energies follow
the dilute law and may exceed site density: the future panel must warn, not silently cap.
Entropy: `Freysoldt ...`, Sec. II.A. Kinetics/capture rather than equilibrium formation terms:
`Alkauskas - Tutorial Defects in semiconductors-Combining experiment and theory.txt`, Secs. I/III.C;
`FP_W10.txt`, Sec. 2 (SRH); `DSC_3.txt` (DLTS and capture/emission).

Frozen-in later needs one extra public function, `SolveFrozenInDefectConcentrations`, taking
growth-solution totals and measurement T. Set `c_Xq=C_X_growth*w_q/sum_r(w_r)` with
`w_q=g_q*exp(-(A_q+q*E_F)/kT)` in log space; reuse carrier integrals and the neutrality search.
This freezes species totals while charges reequilibrate. Sources: `Persson ...`, Sec. C,
room-temperature paragraph (800 K totals, 300 K carriers/charge ratios), and `DSC_1.txt`,
?How to spoil the acceptor? Hydrogen and MgGa? / frozen-in discussion. Complex reactions need
additional conservation constraints.

Panel inputs: editable sites, degeneracies, carrier model, T window and signed fixed doping.
Combined linked plots: log c(T) (states/totals/n/p) and E_F(T); c/E_F versus Delta mu on the line
between two vertices; log-c/E_F maps over T x Delta mu with fixed-doping slices. Interpolate every
species together, `DeltaMu(lambda)=(1-lambda)*vertexA+lambda*vertexB`, then use
`AbsoluteChemicalPotentials`. With only one diamond vertex, use a validated user endpoint.
Failed points appear as gaps with errors. Frozen-in mode waits for its extra domain function.

TSV (future IO/Presentation, one row per state, repeating point data): `temperature_index`,
`mu_index`, `fixed_charge_index`, `T_K`, `lambda`, `delta_mu_<species>_eV`, `mu_<species>_eV`,
`N_fixed_cm-3`, `E_F_from_VBM_eV`, `n_cm-3`, `p_cm-3`, `defect_index`, `defect`,
`charge_state_index`, `q`, `A_q_eV`, `N_sites_cm-3`, `g`, `c_q_cm-3`, `c_total_cm-3`,
`relative_neutrality_residual`, `converged`, `error`. Emit one row with blank defect fields for
intrinsic/failed points. Export raw densities; escape tabs/newlines in labels/errors.

Nine GoogleTests cover analytic limits, neutrality, scaling, both carrier models, grid failures,
1 K underflow, bracket expansion, invalid inputs and overflow. No build/GoogleTest execution here.
Arithmetic checks above do not validate compiled C++. Graphify lacked FormationEnergy; graph
refresh is deferred because writes are restricted to these four deliverables.
