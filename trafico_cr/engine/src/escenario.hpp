// escenario.hpp - Poblacion de conductores calibrada y proyectada a un anio objetivo.
//
// Flujo (se ejecuta en cada consulta, nada se guarda en disco):
//   1. Sembrar poblacion desde los datos (flota por canton, gravedad hacia POI, composicion, teletrabajo).
//   2. Fase 1 del AG: calibrar volumenes y horarios contra los puntos de validacion (sin el de choques).
//   3. Calibrar la tasa de choques por veh-km y fase 2 del AG con el objetivo completo.
//   4. Proyeccion al anio objetivo: W crece con la tasa de flota de la zona, los pesos de destino
//      crecen con los POI y la infraestructura, sube el teletrabajo; luego pocas generaciones del AG
//      re-calibran contra los puntos de validacion proyectados.
#pragma once
#include <array>
#include <string>
#include <vector>

#include "ga.hpp"
#include "model.hpp"
#include "objective.hpp"

using namespace std;

namespace tj {

struct OpcionesCalibracion {
    int gensFase1 = 100, gensFase2 = 80, gensReajuste = 40;
};

struct Escenario {
    int anio = 0, anios = 0;
    double tasa = 0;
    string nombre;
    vector<Agente> pob;
    vector<vector<double>> shares;                // [agente][destino]
    vector<array<array<double, 24>, 3>> hv;  // [agente][tipo de dia][hora]
    vector<double> riesgo, pce;                        // por agente
    vector<double> capMult, demMult;                   // por arista (infraestructura)
    double flotaFija = 0, flotaModelada = 0;
    double teleFut = 0;
    Objetivo obj;
    int gaGeneraciones = 0;
    long gaEvaluaciones = 0;
    double gaFitness = 0, gaErrorMedia = 0, gaErrorSuma = 0;
    vector<string> notas;
};

Escenario construirEscenario(Modelo& M, int anio, double tasa, const string& nombre, uint64_t semilla,
                             const OpcionesCalibracion& opc);

}  // namespace tj
