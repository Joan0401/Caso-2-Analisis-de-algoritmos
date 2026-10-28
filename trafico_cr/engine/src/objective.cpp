#include "objective.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

using namespace std;

namespace tj {

static int diaIdx(const string& n) {
    static const char* dias[7] = {"lunes", "martes", "miercoles", "jueves", "viernes", "sabado", "domingo"};
    for (int i = 0; i < 7; i++) if (n == dias[i]) return i;
    throw runtime_error("Dia desconocido en validacion: " + n);
}

Objetivo Objetivo::base(const Modelo& M) {
    const DataStore& D = M.D;
    Objetivo O;
    O.tolerancia = D.tolerancia;
    double modelada = 0;
    for (auto& c : D.cantones) modelada += c.vehiculos;
    O.flotaFija = max(0.0, D.flotaTotal - modelada);
    for (const ValPunto& v : D.validacion) {
        Punto p;
        p.id = v.id;
        p.objetivo = v.valor;
        p.peso = v.peso;
        p.idx = 0;
        if (v.metrica == "flota_total") p.tipo = FLOTA;
        else if (v.metrica == "accidentes_anuales") p.tipo = ACC;
        else if (v.metrica == "fraccion_viajes_pico_am") p.tipo = PICO_AM;
        else if (v.metrica == "fraccion_viajes_pico_pm") p.tipo = PICO_PM;
        else if (v.metrica == "fraccion_muertes_dia") { p.tipo = FRAC_DIA; p.idx = diaIdx(v.objetivo); }
        else if (v.metrica == "tpd_arista") {
            p.tipo = TPD;
            p.idx = D.aristaIdx(v.objetivo);
            if (p.idx < 0) throw runtime_error("Validacion con arista desconocida: " + v.objetivo);
        } else throw runtime_error("Metrica de validacion desconocida: " + v.metrica);
        if (p.objetivo <= 0) continue;  // puntos sin dato observado se ignoran
        O.pts.push_back(p);
    }
    return O;
}

double Objetivo::crecimientoArista(const Modelo& M, int e, int anios, double tasa) {
    const DataStore& D = M.D;
    const Arista& a = D.aristas[e];
    double mult = 0;
    for (int nodo : {a.a, a.b}) {
        int c = D.cantonIdx(D.nodos[nodo]);
        mult += c >= 0 ? D.multiplicadorZona(D.cantones[c].zona) : 1.0;
    }
    mult /= 2.0;
    return pow(1.0 + tasa * mult, anios);
}

Objetivo Objetivo::escalar(const Modelo& M, int anios, double tasa, double teleFactor, double flotaObjetivo,
                           double flotaFijaFut) const {
    Objetivo O = *this;
    O.flotaFija = flotaFijaFut;
    double nacional = pow(1.0 + tasa, anios);
    for (Punto& p : O.pts) {
        switch (p.tipo) {
            case FLOTA: p.objetivo = flotaObjetivo; break;
            case ACC: p.objetivo *= nacional * teleFactor; break;
            case TPD: p.objetivo *= crecimientoArista(M, p.idx, anios, tasa) * teleFactor; break;
            default: break;
        }
    }
    return O;
}

void Objetivo::valores(const Modelo& M, const double* A, vector<double>& sim) const {
    sim.resize(pts.size());
    double accTot = 0;
    for (int d = 0; d < 7; d++) accTot += A[M.iAcc + d];
    for (size_t i = 0; i < pts.size(); i++) {
        const Punto& p = pts[i];
        switch (p.tipo) {
            case FLOTA: sim[i] = A[M.iW] / M.D.factorCirc + flotaFija; break;
            case ACC: sim[i] = accTot * 52.1429; break;
            case PICO_AM: sim[i] = A[M.iTrips] > 0 ? A[M.iAm] / A[M.iTrips] : 0; break;
            case PICO_PM: sim[i] = A[M.iTrips] > 0 ? A[M.iPm] / A[M.iTrips] : 0; break;
            case FRAC_DIA: sim[i] = accTot > 0 ? A[M.iAcc + p.idx] / accTot : 0; break;
            case TPD: sim[i] = A[p.idx]; break;
        }
    }
}

Objetivo::Error Objetivo::evaluar(const vector<double>& sim) const {
    Error e;
    double sw = 0;
    for (size_t i = 0; i < pts.size(); i++) {
        e.suma += pts[i].peso * fabs(sim[i] - pts[i].objetivo) / pts[i].objetivo;
        sw += pts[i].peso;
    }
    e.media = sw > 0 ? e.suma / sw : 0;
    e.fitness = 1.0 / (1.0 + e.suma);
    return e;
}

Objetivo::Error Objetivo::evaluarAgregado(const Modelo& M, const double* A) const {
    thread_local vector<double> sim;
    valores(M, A, sim);
    return evaluar(sim);
}

}  // namespace tj
