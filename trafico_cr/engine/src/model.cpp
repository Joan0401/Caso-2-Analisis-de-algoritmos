#include "model.hpp"

#include <algorithm>
#include <cmath>

using namespace std;


namespace tj {

static const double SEMANAS = 52.1429;

double geneMin(int i) {
    static const double lo[G_NFIJOS] = {0.3, 1.0, 0.05, 0.0, 4.0, 0.6, 0.0, 0.5, 0.7, 0.0, 0.0};
    return i < G_NFIJOS ? lo[i] : 0.0;
}
double geneMax(int i) {
    static const double hi[G_NFIJOS] = {1.0, 5.0, 0.85, 0.15, 20.0, 4.0, 0.9, 1.5, 1.3, 1.0, 0.5};
    return i < G_NFIJOS ? hi[i] : 1.0;
}

Modelo::Modelo(const DataStore& d) : D(d), G(d) {
    NC = static_cast<int>(D.cantones.size());
    NP = static_cast<int>(D.pois.size());
    NE = static_cast<int>(D.aristas.size());
    ND = NP + 1 + NC;
    iTrips = NE;
    iAm = NE + 1;
    iPm = NE + 2;
    iAcc = NE + 3;
    iW = NE + 10;
    NM = NE + 11;

    double s = 0;
    for (double v : D.volMes) s += v;
    volMesMedio = s / 12.0;
    quincenaMedio = 1.0 + (D.factorQuincena - 1.0) * 7.0 / 30.4;
    double pl = 0;
    for (double v : D.pLluvia) pl += v;
    lluviaMedia = 1.0 + (D.lluviaRiesgo - 1.0) * pl / 12.0;

    for (int h = 0; h < 24; h++) {
        int franja = (h <= 5) ? 0 : (h <= 9 ? 1 : (h <= 15 ? 2 : (h <= 19 ? 3 : 4)));
        rf[h] = D.riesgoFranja[franja];
    }
    poiPico.resize(NP);
    for (int p = 0; p < NP; p++) {
        double tot = 0;
        for (int h = 0; h < 24; h++) {
            double v = 0.02;
            for (int pk : D.pois[p].picos) v += exp(-0.5 * pow((h - pk) / 1.2, 2));
            poiPico[p][h] = v;
            tot += v;
        }
        for (int h = 0; h < 24; h++) poiPico[p][h] /= tot;
    }
    construirRutas();
}

void Modelo::construirRutas() {
    ruta.assign(NC, vector<vector<RutaArista>>(ND));
    minutos.assign(NC, vector<double>(ND, 0));
    kmRed.assign(NC, vector<double>(ND, 0));
    farPeso.assign(NC, vector<double>(NC, 0));

    for (int o = 0; o < NC; o++) {
        vector<double> dist;
        vector<int> pa, pn;
        int src = D.cantones[o].nodo;
        G.caminosMinimos(src, dist, pa, pn);
        auto camino = [&](int dst) {
            vector<int> out;
            if (!isfinite(dist[dst])) return out;
            for (int v = dst; v != src && v >= 0; v = pn[v]) out.push_back(pa[v]);
            reverse(out.begin(), out.end());
            return out;
        };
        for (int k = 0; k < ND; k++) {
            vector<RutaArista>& r = ruta[o][k];
            if (k < NP) {
                const Poi& p = D.pois[k];
                vector<int> cam = camino(p.nodo);
                minutos[o][k] = isfinite(dist[p.nodo]) ? dist[p.nodo] : 600;
                for (int e : cam) r.push_back({e, 1.0});
                // ultimo tramo: si el camino no toca ninguna arista de acceso, se reparte por capacidad
                bool toca = false;
                for (int e : cam) for (int a : p.aristas) if (a == e) toca = true;
                if (!toca && !p.aristas.empty()) {
                    double cap = 0;
                    for (int a : p.aristas) cap += D.aristas[a].cap;
                    for (int a : p.aristas) r.push_back({a, D.aristas[a].cap / cap});
                }
            } else if (k > NP) {
                int c = k - NP - 1;
                if (c == o) continue;
                vector<int> cam = camino(D.cantones[c].nodo);
                minutos[o][k] = isfinite(dist[D.cantones[c].nodo]) ? dist[D.cantones[c].nodo] : 600;
                for (int e : cam) r.push_back({e, 1.0});
            }
            double km = 0;
            for (auto& x : r) km += x.w * D.aristas[x.e].km;
            kmRed[o][k] = km;
        }
        double tot = 0;
        for (int c = 0; c < NC; c++) {
            if (c == o) continue;
            double w = D.cantones[c].poblacion * exp(-minutos[o][NP + 1 + c] / 60.0);
            farPeso[o][c] = w;
            tot += w;
        }
        for (int c = 0; c < NC; c++) farPeso[o][c] = tot > 0 ? farPeso[o][c] / tot : 0;
    }
}

vector<Agente> Modelo::sembrar(Rng& rng) {
    vector<Agente> pob;
    auto clamp = [](double v, double lo, double hi) { return max(lo, min(hi, v)); };
    for (int c = 0; c < NC; c++) {
        int n = static_cast<int>(lround(D.cantones[c].circulantes / 4500.0));
        n = max(6, min(48, n));
        double W = D.cantones[c].circulantes / n;
        for (int j = 0; j < n; j++) {
            Agente a;
            a.canton = c;
            a.W = W;
            double u = (j + 0.5) / n, acc = 0;
            a.tipo = 3;
            for (int t = 0; t < 4; t++) {
                acc += D.composicion[t];
                if (u * (D.composicion[0] + D.composicion[1] + D.composicion[2] + D.composicion[3]) < acc) { a.tipo = t; break; }
            }
            a.g.assign(G_NFIJOS + NP, 0);
            double psal = clamp(0.80 * (1 + 0.05 * rng.normal()), 0.3, 1.0);
            a.g[G_PSAL] = psal;
            a.g[G_VIAJES] = clamp(D.viajesVehDia / psal * (1 + 0.05 * rng.normal()), 1.0, 5.0);
            a.g[G_LOCAL] = clamp(0.65 + 0.05 * rng.normal(), 0.05, 0.85);
            a.g[G_LEJANO] = clamp(0.03 + 0.01 * rng.normal(), 0.0, 0.15);
            a.g[G_HMEDIA] = clamp(7.0 + 0.8 * rng.normal(), 4.0, 20.0);
            a.g[G_HDESV] = clamp(1.3 + 0.2 * rng.normal(), 0.6, 4.0);
            a.g[G_RIGIDEZ] = clamp(0.25 + 0.05 * rng.normal(), 0.0, 0.9);
            a.g[G_FINDE] = clamp(1.0 + 0.05 * rng.normal(), 0.5, 1.5);
            a.g[G_VEL] = clamp(1.0 + 0.05 * rng.normal(), 0.7, 1.3);
            a.g[G_AGRES] = clamp(0.4 + 0.1 * rng.normal(), 0.0, 1.0);
            a.g[G_TELE] = clamp(D.teleActual + 0.02 * rng.normal(), 0.0, 0.5);
            // pesos de destino sembrados con un modelo gravitacional: atraccion * exp(-tiempo/45min)
            double mx = 1e-12;
            vector<double> w(NP);
            for (int k = 0; k < NP; k++) {
                w[k] = D.pois[k].peso * exp(-minutos[c][k] / 45.0) * rng.lognormal(0, 0.15);
                mx = max(mx, w[k]);
            }
            for (int k = 0; k < NP; k++) a.g[G_NFIJOS + k] = clamp(w[k] / mx, 0.0, 1.0);
            a.g0 = a.g;
            pob.push_back(a);
        }
    }
    return pob;
}

void Modelo::compartir(const Agente& a, vector<double>& s) const {
    s.assign(ND, 0.0);
    const double* g = a.g.data();
    double pl = g[G_LOCAL], pj = g[G_LEJANO];
    double rest = max(0.0, 1.0 - pl - pj), sum = 1e-12;
    for (int k = 0; k < NP; k++) sum += g[G_NFIJOS + k];
    for (int k = 0; k < NP; k++) s[k] = rest * g[G_NFIJOS + k] / sum;
    s[NP] = pl;
    for (int c = 0; c < NC; c++) s[NP + 1 + c] = pj * farPeso[a.canton][c];
}

void Modelo::horaAgente(const Agente& a, int tipoDia, double* out) const {
    double r = a.g[G_RIGIDEZ], hm = a.g[G_HMEDIA] + (tipoDia > 0 ? 3.0 : 0.0), hs = a.g[G_HDESV];
    double gs[24], tot = 0;
    for (int h = 0; h < 24; h++) {
        gs[h] = exp(-0.5 * pow((h - hm) / hs, 2));
        tot += gs[h];
    }
    if (tot < 1e-12) tot = 1;
    for (int h = 0; h < 24; h++) out[h] = (1 - r) * D.perfil[tipoDia][h] + r * gs[h] / tot;
}

double Modelo::riesgoAgente(const Agente& a) const {
    return D.riesgoTipo[a.tipo] * (0.6 + 0.8 * a.g[G_AGRES]) * a.g[G_VEL] * a.g[G_VEL];
}

void Modelo::metricas(const Agente& a, double* m) const {
    fill(m, m + NM, 0.0);
    thread_local vector<double> s;
    compartir(a, s);
    const double* g = a.g.data();
    const int o = a.canton;

    double f[7], sumF = 0;
    for (int d = 0; d < 7; d++) {
        f[d] = D.volDia[d] * (d >= 5 ? g[G_FINDE] : (1.0 - g[G_TELE] * D.shareTrabajo));
        sumF += f[d];
    }
    const double Tbase = a.W * g[G_PSAL] * g[G_VIAJES] * volMesMedio * quincenaMedio;
    const double Tmean = Tbase * sumF / 7.0;
    m[iTrips] = Tmean;

    double kmAg = 0, sPoi = 0;
    for (int k = 0; k < ND; k++) {
        if (s[k] <= 0) continue;
        for (auto& r : ruta[o][k]) m[r.e] += Tmean * s[k] * r.w;
        kmAg += s[k] * kmRed[o][k];
        if (k < NP) sPoi += s[k];
    }

    double fam[3], fpm[3], rhm[3];
    for (int t = 0; t < 3; t++) {
        double hv[24];
        horaAgente(a, t, hv);
        double am = hv[6] + hv[7] + hv[8], pm = hv[16] + hv[17] + hv[18], rr = 0;
        for (int h = 0; h < 24; h++) rr += hv[h] * rf[h];
        double amP = 0, pmP = 0;
        for (int k = 0; k < NP; k++) {
            const auto& pp = poiPico[k];
            amP += s[k] * (0.6 * am + 0.4 * (pp[6] + pp[7] + pp[8]));
            pmP += s[k] * (0.6 * pm + 0.4 * (pp[16] + pp[17] + pp[18]));
        }
        fam[t] = amP + (1 - sPoi) * am;
        fpm[t] = pmP + (1 - sPoi) * pm;
        rhm[t] = rr;
    }
    const double risk = riesgoAgente(a);
    for (int d = 0; d < 7; d++) {
        int t = d < 5 ? 0 : d - 4;
        double Td = Tbase * f[d];
        m[iAm] += Td * fam[t] / 7.0;
        m[iPm] += Td * fpm[t] / 7.0;
        m[iAcc + d] = Td * kmAg * risk * rhm[t] * lluviaMedia * (D.sevDia[d] / D.volDia[d]) * rkm;
    }
    m[iW] = a.W;
}

void Modelo::calibrarRkm(const vector<Agente>& pob) {
    rkm = 1.0;
    vector<double> m(NM);
    double total = 0;
    for (const Agente& a : pob) {
        metricas(a, m.data());
        for (int d = 0; d < 7; d++) total += m[iAcc + d];
    }
    rkm = D.accAnuales / (SEMANAS * total);
}

}  // namespace tj
