#include "escenario.hpp"

#include <algorithm>
#include <cmath>

using namespace std;


namespace tj {

static double acotar(double v, double lo, double hi) { return max(lo, min(hi, v)); }

Escenario construirEscenario(Modelo& M, int anio, double tasa, const string& nombre, uint64_t semilla,
                             const OpcionesCalibracion& opc) {
    const DataStore& D = M.D;
    Rng rngInit(mezclar(semilla, 1)), rngGA(mezclar(semilla, 2));
    Escenario S;
    S.anio = anio;
    S.anios = anio - D.anioBase;
    S.tasa = tasa;
    S.nombre = nombre;

    // 1-3: poblacion base sembrada con datos y calibrada con el AG
    S.pob = M.sembrar(rngInit);
    Objetivo base = Objetivo::base(M);
    Objetivo sinAcc = base;
    sinAcc.pts.clear();
    for (const auto& p : base.pts) if (p.tipo != Objetivo::ACC) sinAcc.pts.push_back(p);

    ConfigGA c1;
    c1.generaciones = opc.gensFase1;
    c1.tolMedia = 0.01;
    ResultadoGA r = ejecutarGA(M, S.pob, sinAcc, c1, rngGA);
    S.gaGeneraciones += r.generaciones;
    S.gaEvaluaciones += r.evaluaciones;

    M.calibrarRkm(S.pob);
    ConfigGA c2;
    c2.generaciones = opc.gensFase2;
    c2.sigma0 = 0.05;
    c2.tolMedia = 0.01;
    r = ejecutarGA(M, S.pob, base, c2, rngGA);
    S.gaGeneraciones += r.generaciones;
    S.gaEvaluaciones += r.evaluaciones;
    S.obj = base;
    S.flotaFija = base.flotaFija;

    // 4: proyeccion al anio objetivo
    const int years = S.anios;
    double teleFactor = 1.0;
    S.teleFut = D.teleActual;
    if (years != 0) {
        for (Agente& a : S.pob) {
            double f = pow(1.0 + tasa * D.multiplicadorZona(D.cantones[a.canton].zona), years);
            a.W *= f;
            a.g[G_TELE] = acotar(a.g[G_TELE] + D.teleAumento * years, 0.0, 0.5);
        }
        S.teleFut = acotar(D.teleActual + D.teleAumento * years, 0.0, 0.5);
        teleFactor = (1.0 - S.teleFut * D.shareTrabajo) / (1.0 - D.teleActual * D.shareTrabajo);

        // pesos de destino: crecimiento del POI segun su zona e infraestructura de acceso
        vector<double> multPoi(M.NP, 1.0);
        for (int k = 0; k < M.NP; k++) {
            const Poi& p = D.pois[k];
            double zm = D.multiplicadorZona(D.cantones[p.cantonIdx].zona);
            double m = pow(1.0 + D.poiCrec * zm, years);
            for (const Infra& f : D.infra)
                if (anio >= f.anio && find(p.aristas.begin(), p.aristas.end(), f.arista) != p.aristas.end())
                    m *= f.fdem;
            multPoi[k] = m;
        }
        for (Agente& a : S.pob) {
            double mx = 1e-12;
            for (int k = 0; k < M.NP; k++) {
                a.g[G_NFIJOS + k] *= multPoi[k];
                mx = max(mx, a.g[G_NFIJOS + k]);
            }
            if (mx > 1.0) for (int k = 0; k < M.NP; k++) a.g[G_NFIJOS + k] /= mx;
        }

        double modelada = 0;
        for (const Agente& a : S.pob) modelada += a.W / D.factorCirc;
        double fijaFut = S.flotaFija * pow(1.0 + tasa, years);
        S.flotaFija = fijaFut;
        S.obj = base.escalar(M, years, tasa, teleFactor, modelada + fijaFut, fijaFut);

        for (Agente& a : S.pob) a.g0 = a.g;  // la proyeccion se ancla a la poblacion base calibrada
        ConfigGA c3;
        c3.generaciones = opc.gensReajuste;
        c3.sigma0 = 0.03;
        c3.tolMedia = 0.01;
        r = ejecutarGA(M, S.pob, S.obj, c3, rngGA);
        S.gaGeneraciones += r.generaciones;
        S.gaEvaluaciones += r.evaluaciones;
    }
    vector<double> A;
    agregarMetricas(M, S.pob, A);
    Objetivo::Error e = S.obj.evaluarAgregado(M, A.data());
    S.gaFitness = e.fitness;
    S.gaErrorMedia = e.media;
    S.gaErrorSuma = e.suma;
    for (const Agente& a : S.pob) S.flotaModelada += a.W / D.factorCirc;

    // precalculos por agente para el simulador
    const size_t na = S.pob.size();
    S.shares.resize(na);
    S.hv.resize(na);
    S.riesgo.resize(na);
    S.pce.resize(na);
    for (size_t i = 0; i < na; i++) {
        M.compartir(S.pob[i], S.shares[i]);
        for (int t = 0; t < 3; t++) M.horaAgente(S.pob[i], t, S.hv[i][t].data());
        S.riesgo[i] = M.riesgoAgente(S.pob[i]);
        S.pce[i] = M.pceTipo[S.pob[i].tipo];
    }
    S.capMult.assign(M.NE, 1.0);
    S.demMult.assign(M.NE, 1.0);
    for (const Infra& f : D.infra) {
        if (anio >= f.anio) {
            S.capMult[f.arista] *= f.fcap;
            S.demMult[f.arista] *= f.fdem;
            S.notas.push_back("Infraestructura activa: " + f.nombre + " (desde " + to_string(f.anio) + ")");
        }
    }
    return S;
}

}  // namespace tj
