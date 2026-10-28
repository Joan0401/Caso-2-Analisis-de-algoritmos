#include "simulator.hpp"

#include <algorithm>
#include <cmath>

using namespace std;


namespace tj {

Simulador::Simulador(const Modelo& M, const Escenario& S) : M_(M), S_(S) {
    const DataStore& D = M.D;
    for (const char* id : {"E04", "E18", "E20", "E21", "E12"}) {
        int e = D.aristaIdx(id);
        if (e >= 0) playas_.push_back(e);
    }
    for (int e = 0; e < M.NE; e++) if (D.aristas[e].tipo == "autopista") autopistas_.push_back(e);
    for (int k = 0; k < M.NP; k++) if (D.pois[k].tipo == "mall") malls_.push_back(k);
}

static inline bool enVentana(int h, int ini, int dur) { return dur >= 24 || ((h - ini + 24) % 24) < dur; }
static inline double dirFactor(int h) { return ((h >= 6 && h <= 9) || (h >= 16 && h <= 19)) ? 0.6 : 0.5; }

void Simulador::dia(const DiaCtx& ctx, Rng& rng, ResultadoDia& R) const {
    const DataStore& D = M_.D;
    const int NC = M_.NC, ND = M_.ND, NP = M_.NP, NE = M_.NE;
    const size_t nOK = static_cast<size_t>(NC) * ND;

    R.ctx = ctx;
    R.M.assign(nOK * 24, 0.0);
    R.flowBase.assign(static_cast<size_t>(NE) * 24, 0.0);
    R.pcef.assign(static_cast<size_t>(NE) * 24, 0.0);
    R.cap.assign(static_cast<size_t>(NE) * 24, 0.0);
    R.vc.assign(static_cast<size_t>(NE) * 24, 0.0);
    R.crashes.assign(NE, 0.0);
    R.congEv.assign(NE, 0.0);
    fill(R.tripsHora, R.tripsHora + 24, 0.0);
    R.crashExpBase = 0;

    // ---- 1. comportamiento de los conductores: cuantos viajes y hacia donde ----
    vector<double> cnt(nOK, 0.0), pcs(nOK, 0.0), rsk(nOK, 0.0), Ho(static_cast<size_t>(NC) * 24, 0.0), No(NC, 0.0);
    const double volCtx = ctx.volMes * ctx.quincena * ctx.feriado * ctx.ruido;
    for (size_t i = 0; i < S_.pob.size(); i++) {
        const Agente& a = S_.pob[i];
        const double* g = a.g.data();
        double fAg = ctx.volDia * (ctx.dow >= 5 ? g[G_FINDE] : 1.0 - g[G_TELE] * D.shareTrabajo);
        double T = a.W * g[G_PSAL] * g[G_VIAJES] * fAg * volCtx * rng.lognormal(0.0, 0.03);
        double N = round(T + sqrt(T) * rng.normal());
        if (N <= 0) continue;
        const vector<double>& s = S_.shares[i];
        int last = -1;
        for (int k = 0; k < ND; k++) if (s[k] > 0) last = k;
        double rem = N, remShare = 1.0;
        for (int k = 0; k < ND; k++) {
            if (s[k] <= 0) continue;
            double c = (k == last) ? rem : rng.binomial(rem, min(1.0, s[k] / remShare));
            rem -= c;
            remShare = max(1e-9, remShare - s[k]);
            size_t idx = static_cast<size_t>(a.canton) * ND + k;
            cnt[idx] += c;
            pcs[idx] += c * S_.pce[i];
            rsk[idx] += c * S_.riesgo[i];
        }
        const auto& hv = S_.hv[i][ctx.tipoDia];
        for (int h = 0; h < 24; h++) Ho[static_cast<size_t>(a.canton) * 24 + h] += N * hv[h];
        No[a.canton] += N;
    }

    // ---- 2. distribucion horaria y carga de la red ----
    vector<double> riskDia(NE, 0.0), flowDia(NE, 0.0), ratioPce(nOK, 1.0);
    for (int o = 0; o < NC; o++) {
        for (int k = 0; k < ND; k++) {
            size_t idx = static_cast<size_t>(o) * ND + k;
            double c = cnt[idx];
            if (c <= 0) continue;
            ratioPce[idx] = pcs[idx] / c;
            for (int h = 0; h < 24; h++) {
                double base = No[o] > 0 ? Ho[static_cast<size_t>(o) * 24 + h] / No[o] : 1.0 / 24.0;
                double m = k < NP ? c * (0.6 * base + 0.4 * M_.poiPico[k][h]) : c * base;
                R.M[idx * 24 + h] = m;
                R.tripsHora[h] += m;
            }
            for (const RutaArista& r : M_.ruta[o][k]) {
                riskDia[r.e] += r.w * rsk[idx];
                flowDia[r.e] += r.w * c;
                for (int h = 0; h < 24; h++) {
                    double v = r.w * R.M[idx * 24 + h];
                    R.flowBase[static_cast<size_t>(r.e) * 24 + h] += v;
                    R.pcef[static_cast<size_t>(r.e) * 24 + h] += v * ratioPce[idx];
                }
            }
        }
    }
    R.flow = R.flowBase;

    // ---- 3. eventos del dia ----
    vector<double> capF(static_cast<size_t>(NE) * 24, 1.0);
    for (int ei : ctx.eventos) {
        const Evento& ev = D.eventos[ei];
        vector<int> ks;
        if (ev.poi.compare(0, 3, "POI") == 0) {
            int k = D.poiIdx(ev.poi);
            if (k >= 0) ks.push_back(k);
        } else if (ev.poi == "malls") {
            ks = malls_;
        } else if (ev.poi == "escuelas") {
            for (int k = 0; k < NP; k++) ks.push_back(k);
        }
        if (!ks.empty()) {
            // demanda hacia los POI afectados: se agrega la diferencia y se carga en las rutas
            for (int k : ks) for (int o = 0; o < NC; o++) {
                size_t idx = static_cast<size_t>(o) * ND + k;
                if (cnt[idx] <= 0) continue;
                for (int h = 0; h < 24; h++) {
                    if (!enVentana(h, ev.ini, ev.dur)) continue;
                    double d = R.M[idx * 24 + h] * (ev.mult - 1.0);
                    R.M[idx * 24 + h] += d;
                    for (const RutaArista& r : M_.ruta[o][k]) {
                        R.flow[static_cast<size_t>(r.e) * 24 + h] += r.w * d;
                        R.pcef[static_cast<size_t>(r.e) * 24 + h] += r.w * d * ratioPce[idx];
                    }
                }
            }
        } else if (ev.poi == "corredor_playas") {
            for (int e : playas_) for (int h = 0; h < 24; h++) if (enVentana(h, ev.ini, ev.dur)) {
                R.flow[static_cast<size_t>(e) * 24 + h] *= ev.mult;
                R.pcef[static_cast<size_t>(e) * 24 + h] *= ev.mult;
            }
        } else if (ev.poi == "cualquiera") {
            int e = rng.entero(NE);
            for (int h = 0; h < 24; h++) if (enVentana(h, ev.ini, ev.dur)) {
                R.flow[static_cast<size_t>(e) * 24 + h] *= ev.mult;
                R.pcef[static_cast<size_t>(e) * 24 + h] *= ev.mult;
            }
        } else if (ev.poi == "rutas_clave" && !autopistas_.empty()) {
            int e = autopistas_[rng.entero(static_cast<int>(autopistas_.size()))];
            for (int h = 0; h < 24; h++) if (enVentana(h, ev.ini, ev.dur)) capF[static_cast<size_t>(e) * 24 + h] = 0.25;
        }
    }
    for (int e = 0; e < NE; e++) for (int h = 0; h < 24; h++) {
        R.flow[static_cast<size_t>(e) * 24 + h] *= S_.demMult[e];
        R.pcef[static_cast<size_t>(e) * 24 + h] *= S_.demMult[e];
    }

    // ---- 4. choques y congestion ----
    const double sevVol = D.sevDia[ctx.dow] / D.volDia[ctx.dow];
    const double rainR = ctx.lluvia ? D.lluviaRiesgo : 1.0;
    for (int e = 0; e < NE; e++) {
        const Arista& ar = D.aristas[e];
        double ratioR = flowDia[e] > 1e-9 ? riskDia[e] / flowDia[e] : 1.0;
        double k0 = ar.km * M_.rkm * ratioR * sevVol;
        double pc = ar.tipo == "autopista" ? D.pCong[0] : D.pCong[1];
        double loss = min(0.9, D.perdidaCap * sqrt(2.0 / max(1, ar.carriles)));
        for (int h = 0; h < 24; h++) {
            size_t ix = static_cast<size_t>(e) * 24 + h;
            R.crashExpBase += R.flowBase[ix] * k0 * M_.rf[h] * M_.lluviaMedia;
            double lam = R.flow[ix] * k0 * M_.rf[h] * rainR;
            double n = rng.poisson(lam);
            if (n <= 0) continue;
            R.crashes[e] += n;
            for (int j = 0; j < static_cast<int>(n); j++) {
                if (!rng.bernoulli(pc)) continue;
                double dur = rng.lognormal(log(D.congMediana / 60.0), D.congSigma);
                int fin = min(24, h + static_cast<int>(ceil(dur)));
                for (int t = h; t < fin; t++) capF[static_cast<size_t>(e) * 24 + t] = min(capF[static_cast<size_t>(e) * 24 + t], 1.0 - loss);
                R.congEv[e] += 1;
            }
        }
    }

    // ---- 5. capacidad efectiva y razon volumen/capacidad ----
    for (int e = 0; e < NE; e++) {
        double capBase = D.aristas[e].cap * S_.capMult[e] * (ctx.lluvia ? 0.92 : 1.0);
        for (int h = 0; h < 24; h++) {
            size_t ix = static_cast<size_t>(e) * 24 + h;
            R.cap[ix] = capBase * capF[ix];
            R.vc[ix] = R.pcef[ix] * dirFactor(h) / R.cap[ix];
        }
    }
}

}  // namespace tj
