#include "montecarlo.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

using namespace std;


namespace tj {

namespace {

struct Welford {
    long n = 0;
    double mean = 0, m2 = 0;
    void add(double x) {
        n++;
        double d = x - mean;
        mean += d / n;
        m2 += d * (x - mean);
    }
    double desv() const { return n > 1 ? sqrt(m2 / (n - 1)) : 0.0; }
};

// Acumulador de los puntos de validacion, calculados sobre dias muestreados de todo el anio.
struct AcumVal {
    long n = 0;
    vector<double> tpd;
    double crashExp = 0, am = 0, pm = 0, tot = 0, crashDow[7] = {0, 0, 0, 0, 0, 0, 0};
    long nDow[7] = {0, 0, 0, 0, 0, 0, 0};

    explicit AcumVal(int NE) : tpd(NE, 0.0) {}

    void agregar(const ResultadoDia& R, int NE) {
        n++;
        for (int e = 0; e < NE; e++)
            for (int h = 0; h < 24; h++) tpd[e] += R.flowBase[static_cast<size_t>(e) * 24 + h];
        crashExp += R.crashExpBase;
        for (int h = 0; h < 24; h++) {
            tot += R.tripsHora[h];
            if (h >= 6 && h <= 8) am += R.tripsHora[h];
            if (h >= 16 && h <= 18) pm += R.tripsHora[h];
        }
        crashDow[R.ctx.dow] += R.crashExpBase;
        nDow[R.ctx.dow]++;
    }

    void valores(const Objetivo& O, const Escenario& S, vector<double>& sim) const {
        sim.assign(O.pts.size(), 0.0);
        double md[7], sumMd = 0;
        for (int d = 0; d < 7; d++) {
            md[d] = nDow[d] > 0 ? crashDow[d] / nDow[d] : 0.0;
            sumMd += md[d];
        }
        for (size_t i = 0; i < O.pts.size(); i++) {
            const Objetivo::Punto& p = O.pts[i];
            switch (p.tipo) {
                case Objetivo::FLOTA: sim[i] = S.flotaModelada + S.flotaFija; break;
                case Objetivo::ACC: sim[i] = n > 0 ? crashExp / n * 365.0 : 0.0; break;
                case Objetivo::PICO_AM: sim[i] = tot > 0 ? am / tot : 0.0; break;
                case Objetivo::PICO_PM: sim[i] = tot > 0 ? pm / tot : 0.0; break;
                case Objetivo::FRAC_DIA: sim[i] = sumMd > 0 ? md[p.idx] / sumMd : 0.0; break;
                case Objetivo::TPD: sim[i] = n > 0 ? tpd[p.idx] / n : 0.0; break;
            }
        }
    }
};

}  // namespace

ResultadoMC ejecutarMC(const Modelo& M, const Escenario& S, const Consulta& C, const ConfigMC& cfg, uint64_t semilla) {
    const auto t0 = chrono::steady_clock::now();
    auto ms = [&]() {
        return chrono::duration<double, milli>(chrono::steady_clock::now() - t0).count();
    };

    Rng rng(mezclar(semilla, 3));
    Calendario cal(M.D);
    Simulador sim(M, S);
    const bool compartido = (C.mes == 0 && C.tipoDia == "cualquiera");
    const double z = cuantilNormal(0.5 + cfg.confianza / 2.0);
    const double tol = cfg.tolerancia >= 0 ? cfg.tolerancia : S.obj.tolerancia;

    const size_t ne = C.est.size();
    vector<Welford> W(ne);
    vector<double> serieSum(24, 0.0), v(ne), serie(24);
    AcumVal val(M.NE);
    ResultadoDia R, RV;
    vector<double> simVal;

    ResultadoMC out;
    out.tolerancia = tol;

    while (true) {
        for (int b = 0; b < cfg.lote; b++) {
            DiaCtx ctx = cal.muestrear(rng, C.anio, C.mes, C.tipoDia);
            sim.dia(ctx, rng, R);
            fill(v.begin(), v.end(), 0.0);
            fill(serie.begin(), serie.end(), 0.0);
            C.extraer(R, v, serie);
            for (size_t i = 0; i < ne; i++) W[i].add(v[i]);
            for (int h = 0; h < 24; h++) serieSum[h] += serie[h];
            if (compartido) {
                val.agregar(R, M.NE);
            } else {
                DiaCtx cv = cal.muestrear(rng, C.anio, 0, "cualquiera");
                sim.dia(cv, rng, RV);
                val.agregar(RV, M.NE);
            }
        }
        out.iteraciones++;
        const long n = W.empty() ? 0 : W[0].n;
        val.valores(S.obj, S, simVal);
        Objetivo::Error err = S.obj.evaluar(simVal);
        out.errorSuma = err.suma;
        out.errorMedia = err.media;
        out.objOk = err.media <= tol;

        out.ciOk = n >= cfg.minDias;
        out.ciRelMax = 0;
        for (size_t i = 0; i < ne; i++) {
            double half = z * W[i].desv() / sqrt(static_cast<double>(n));
            double rel = fabs(W[i].mean) > 1e-12 ? half / fabs(W[i].mean) : (half > 0 ? 1e9 : 0);
            if (half > C.est[i].tolAbs) out.ciRelMax = max(out.ciRelMax, rel);
            if (half > cfg.ciRel * fabs(W[i].mean) && half > C.est[i].tolAbs) out.ciOk = false;
        }
        if (n >= cfg.minDias && out.objOk && out.ciOk) { out.convergio = true; break; }
        if (out.iteraciones >= cfg.maxIter) break;
        if (ms() > cfg.maxMs) { out.limiteTiempo = true; break; }
    }

    out.simValidacion = simVal;
    out.muestras = W.empty() ? 0 : W[0].n;
    out.diasValidacion = val.n;
    out.est.resize(ne);
    for (size_t i = 0; i < ne; i++) {
        double sd = W[i].desv();
        double half = z * sd / sqrt(static_cast<double>(max<long>(1, W[i].n)));
        out.est[i] = {W[i].mean, sd, W[i].mean - half, W[i].mean + half, W[i].n};
    }
    out.serie.resize(24);
    for (int h = 0; h < 24; h++) out.serie[h] = out.muestras > 0 ? serieSum[h] / out.muestras : 0.0;
    out.tiempoMs = ms();
    return out;
}

ResultadoMC combinarReplicas(const vector<ResultadoMC>& reps, const Consulta& C, const ConfigMC& cfg, double& cv) {
    cv = 0;
    const size_t K = reps.size();
    if (K == 1) return reps[0];
    ResultadoMC out = reps[0];
    const double t = cuantilT(0.5 + cfg.confianza / 2.0, static_cast<int>(K) - 1);
    const size_t ne = C.est.size();
    out.ciRelMax = 0;
    out.ciOk = true;
    for (size_t i = 0; i < ne; i++) {
        double m = 0, sdw = 0;
        long n = 0;
        for (const auto& r : reps) { m += r.est[i].media; sdw += r.est[i].desv * r.est[i].desv; n += r.est[i].n; }
        m /= K;
        double sb = 0;
        for (const auto& r : reps) sb += (r.est[i].media - m) * (r.est[i].media - m);
        sb = sqrt(sb / (K - 1));
        double half = t * sb / sqrt(static_cast<double>(K));
        out.est[i] = {m, sqrt(sdw / K), m - half, m + half, n};
        if (i == 0 && fabs(m) > 1e-12) cv = sb / fabs(m);
        if (half > C.est[i].tolAbs) out.ciRelMax = max(out.ciRelMax, fabs(m) > 1e-12 ? half / fabs(m) : 1e9);
        if (half > cfg.ciRel * fabs(m) && half > C.est[i].tolAbs) out.ciOk = false;
    }
    for (size_t k = 1; k < K; k++) {
        const ResultadoMC& r = reps[k];
        for (int h = 0; h < 24; h++) out.serie[h] += r.serie[h];
        for (size_t j = 0; j < out.simValidacion.size(); j++) out.simValidacion[j] += r.simValidacion[j];
        out.muestras += r.muestras;
        out.diasValidacion += r.diasValidacion;
        out.iteraciones += r.iteraciones;
        out.errorSuma += r.errorSuma;
        out.errorMedia += r.errorMedia;
        out.objOk = out.objOk && r.objOk;
        out.convergio = out.convergio && r.convergio;
        out.limiteTiempo = out.limiteTiempo || r.limiteTiempo;
        out.tiempoMs += r.tiempoMs;
    }
    for (int h = 0; h < 24; h++) out.serie[h] /= K;
    for (double& x : out.simValidacion) x /= K;
    out.errorSuma /= K;
    out.errorMedia /= K;
    return out;
}

}  // namespace tj
