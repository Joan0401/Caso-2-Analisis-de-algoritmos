#include "ga.hpp"

#include <algorithm>
#include <cmath>

using namespace std;


namespace tj {

void agregarMetricas(const Modelo& M, const vector<Agente>& pob, vector<double>& A) {
    A.assign(M.NM, 0.0);
    vector<double> m(M.NM);
    for (const Agente& a : pob) {
        M.metricas(a, m.data());
        for (int i = 0; i < M.NM; i++) A[i] += m[i];
    }
}

static double distPrior(const Agente& a) {
    double d = 0;
    for (size_t g = 0; g < a.g.size(); g++) {
        double r = geneMax(static_cast<int>(g)) - geneMin(static_cast<int>(g));
        double x = (a.g[g] - a.g0[g]) / r;
        d += x * x;
    }
    return d / static_cast<double>(a.g.size());
}

ResultadoGA ejecutarGA(const Modelo& M, vector<Agente>& pob, const Objetivo& O, const ConfigGA& cfg, Rng& rng) {
    const int na = static_cast<int>(pob.size());
    const int NM = M.NM;
    ResultadoGA res;

    vector<double> Mx(static_cast<size_t>(na) * NM), A(NM, 0.0);
    for (int i = 0; i < na; i++) {
        M.metricas(pob[i], &Mx[static_cast<size_t>(i) * NM]);
        for (int k = 0; k < NM; k++) A[k] += Mx[static_cast<size_t>(i) * NM + k];
    }
    vector<vector<int>> cohorte(M.NC);
    for (int i = 0; i < na; i++) cohorte[pob[i].canton].push_back(i);

    vector<double> tmp(NM), hijoM(NM), marg(na);
    Objetivo::Error cur = O.evaluarAgregado(M, A.data());
    double priorSum = 0;
    for (const Agente& a : pob) priorSum += distPrior(a);
    double total = cur.suma + cfg.lambdaPrior * priorSum / na;

    const int nHijos = max(1, static_cast<int>(na * cfg.fraccionHijos));
    int gen = 0;
    for (; gen < cfg.generaciones; gen++) {
        if (gen >= cfg.minGeneraciones && cur.media <= cfg.tolMedia) break;

        // aptitud individual = aporte marginal al error del agregado
        for (int i = 0; i < na; i++) {
            for (int k = 0; k < NM; k++) tmp[k] = A[k] - Mx[static_cast<size_t>(i) * NM + k];
            marg[i] = O.evaluarAgregado(M, tmp.data()).suma - cur.suma;
        }
        double frac = cfg.generaciones > 1 ? static_cast<double>(gen) / (cfg.generaciones - 1) : 1.0;
        double sigma = cfg.sigma0 * pow(cfg.sigmaMin / cfg.sigma0, frac);
        vector<char> usado(na, 0);

        for (int h = 0; h < nHijos; h++) {
            int i0 = rng.entero(na);
            const vector<int>& coh = cohorte[pob[i0].canton];
            if (coh.size() < 2) continue;
            auto torneo = [&]() {
                int mejor = coh[rng.entero(static_cast<int>(coh.size()))];
                for (int t = 1; t < cfg.torneo; t++) {
                    int c = coh[rng.entero(static_cast<int>(coh.size()))];
                    if (marg[c] > marg[mejor]) mejor = c;
                }
                return mejor;
            };
            int p1 = torneo(), p2 = torneo();
            int victima = -1;
            for (int c : coh) if (!usado[c] && (victima < 0 || marg[c] < marg[victima])) victima = c;
            if (victima < 0) continue;

            Agente hijo = pob[victima];
            for (size_t g = 0; g < hijo.g.size(); g++) {
                double a = rng.uniforme(0.0, 1.0);
                double v = a * pob[p1].g[g] + (1 - a) * pob[p2].g[g];
                if (rng.u01() < cfg.pMutacion) v += sigma * (geneMax(static_cast<int>(g)) - geneMin(static_cast<int>(g))) * rng.normal();
                hijo.g[g] = max(geneMin(static_cast<int>(g)), min(geneMax(static_cast<int>(g)), v));
            }
            M.metricas(hijo, hijoM.data());
            for (int k = 0; k < NM; k++) tmp[k] = A[k] - Mx[static_cast<size_t>(victima) * NM + k] + hijoM[k];
            Objetivo::Error e1 = O.evaluarAgregado(M, tmp.data());
            res.evaluaciones++;
            double priorNuevo = priorSum - distPrior(pob[victima]) + distPrior(hijo);
            double totalNuevo = e1.suma + cfg.lambdaPrior * priorNuevo / na;
            if (totalNuevo < total) {
                priorSum = priorNuevo;
                total = totalNuevo;
                A = tmp;
                copy(hijoM.begin(), hijoM.end(), Mx.begin() + static_cast<long>(victima) * NM);
                pob[victima] = hijo;
                usado[victima] = 1;
                cur = e1;
            }
        }
    }
    res.generaciones = gen;
    res.fitness = cur.fitness;
    res.errorSuma = cur.suma;
    res.errorMedia = cur.media;
    return res;
}

}  // namespace tj
