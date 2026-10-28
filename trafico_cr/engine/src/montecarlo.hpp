// montecarlo.hpp - Estimador probabilistico por muestreo con criterio de parada por funcion objetivo.
//
// Clasificacion: el bucle se comporta como un algoritmo de tipo Las Vegas respecto al criterio de
// validacion (repite lotes de dias aleatorios hasta que el error contra los puntos de validacion es
// <= tolerancia Y el intervalo de confianza de cada estimacion es suficientemente angosto), pero con un
// presupuesto acotado (max_iter / max_ms). Si el presupuesto se agota antes, el resultado se entrega
// como Monte Carlo (tiempo acotado, respuesta posiblemente imprecisa) y se marca convergio = false.
#pragma once
#include <functional>
#include <string>
#include <vector>

#include "simulator.hpp"

using namespace std;

namespace tj {

struct Estimando {
    string nombre, unidad;
    double tolAbs = 0;  // si el semi-ancho del IC es menor que esto, se considera convergido
};

struct Consulta {
    string tipo;
    int anio = 0, mes = 0;
    string tipoDia = "cualquiera";
    vector<Estimando> est;
    string serieNombre, serieUnidad;  // perfil horario promedio opcional (24 valores)
    // llena v (un valor por estimando) y serie (24 valores) a partir de un dia simulado
    function<void(const ResultadoDia&, vector<double>&, vector<double>&)> extraer;
};

struct ConfigMC {
    double confianza = 0.95, ciRel = 0.05, tolerancia = -1;  // tolerancia < 0 => la de los datos
    int lote = 20, minDias = 60, maxIter = 300;
    long maxMs = 20000;
};

struct Estadistica { double media = 0, desv = 0, lo = 0, hi = 0; long n = 0; };

struct ResultadoMC {
    vector<Estadistica> est;
    vector<double> serie;
    vector<double> simValidacion;  // valor simulado de cada punto de validacion al terminar
    long muestras = 0, diasValidacion = 0;
    int iteraciones = 0;
    double errorSuma = 0, errorMedia = 0, tolerancia = 0, ciRelMax = 0;
    bool objOk = false, ciOk = false, convergio = false, limiteTiempo = false;
    double tiempoMs = 0;
};

ResultadoMC ejecutarMC(const Modelo& M, const Escenario& S, const Consulta& C, const ConfigMC& cfg, uint64_t semilla);

// Combina replicas independientes (cada una con su propia poblacion calibrada por el AG). El IC combinado
// usa la variacion entre replicas (t de Student), asi incluye la incertidumbre de calibracion.
// cvCalibracion = desviacion entre replicas / media del primer estimando.
ResultadoMC combinarReplicas(const vector<ResultadoMC>& reps, const Consulta& C, const ConfigMC& cfg, double& cvCalibracion);

}  // namespace tj
