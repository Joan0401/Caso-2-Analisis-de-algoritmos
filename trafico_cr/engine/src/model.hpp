// model.hpp - Estructura estatica del modelo: agentes (perfiles de conductores), rutas y metricas esperadas.
//
// Un Agente es un perfil de comportamiento que representa a W vehiculos de un canton de origen.
// Su "cromosoma" (genes) decide: cuanto sale, cuando, adonde va, si viaja lejos, a que velocidad
// conduce, que tan agresivo es (riesgo de choque) y si hace teletrabajo.
#pragma once
#include <array>
#include <vector>

#include "datastore.hpp"
#include "graph.hpp"
#include "rng.hpp"

using namespace std;

namespace tj {

enum Gen {
    G_PSAL = 0,   // probabilidad de que el vehiculo salga en un dia habil
    G_VIAJES,     // viajes (de ida) por salida
    G_LOCAL,      // fraccion de viajes dentro del propio canton
    G_LEJANO,     // fraccion de viajes lejanos (otro canton sin punto de interes especifico)
    G_HMEDIA,     // hora media de salida (h)
    G_HDESV,      // desviacion de la hora de salida (h)
    G_RIGIDEZ,    // peso de la hora propia frente al perfil horario nacional
    G_FINDE,      // multiplicador de uso en fin de semana
    G_VEL,        // velocidad relativa al limite (1 = limite)
    G_AGRES,      // agresividad (0-1) -> riesgo de choque
    G_TELE,       // probabilidad de teletrabajo
    G_NFIJOS      // los genes siguientes son los pesos de destino hacia cada POI
};

double geneMin(int i);
double geneMax(int i);

struct Agente {
    int canton = 0;  // canton de origen
    int tipo = 0;    // 0 auto, 1 moto, 2 carga pesada, 3 bus
    double W = 0;    // vehiculos que representa
    vector<double> g;   // genes actuales
    vector<double> g0;  // genes de anclaje (a priori): sembrados con datos o calibracion previa
};

struct RutaArista { int e; double w; };

class Modelo {
public:
    explicit Modelo(const DataStore& D);

    const DataStore& D;
    Grafo G;
    int NC, NP, NE, ND;  // cantones, POIs, aristas, clases de destino (NP + 1 local + NC lejanos)

    // Layout del vector de metricas agregadas de un agente: [tpd por arista | viajes | viajes pico AM |
    // viajes pico PM | accidentes por dia de semana (7) | W]
    int iTrips, iAm, iPm, iAcc, iW, NM;

    // Rutas: ruta[origen][destino] = aristas cargadas con peso. destino: 0..NP-1 POI, NP local, NP+1+c lejano.
    vector<vector<vector<RutaArista>>> ruta;
    vector<vector<double>> minutos;   // tiempo libre origen -> destino (min)
    vector<vector<double>> kmRed;     // km recorridos sobre la red modelada
    vector<vector<double>> farPeso;   // pesos normalizados de destinos lejanos
    vector<array<double, 24>> poiPico;
    array<double, 24> rf;                  // riesgo relativo por hora
    array<double, 4> pceTipo{1.0, 0.4, 2.5, 2.0};
    double volMesMedio = 1, quincenaMedio = 1, lluviaMedia = 1;
    double rkm = 1;  // choques por veh-km en condiciones de referencia (se calibra al sembrar)

    int destCanton(int origen, int k) const {
        if (k < NP) return D.pois[k].cantonIdx;
        if (k == NP) return origen;
        return k - NP - 1;
    }

    vector<Agente> sembrar(Rng& rng);
    void compartir(const Agente& a, vector<double>& s) const;
    void horaAgente(const Agente& a, int tipoDia, double* out24) const;
    void metricas(const Agente& a, double* out) const;
    double riesgoAgente(const Agente& a) const;
    void calibrarRkm(const vector<Agente>& pob);

private:
    void construirRutas();
};

}  // namespace tj
