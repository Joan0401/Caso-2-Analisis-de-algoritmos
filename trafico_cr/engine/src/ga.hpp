// ga.hpp - Algoritmo genetico que genera y calibra el comportamiento de los conductores.
//
// Poblacion: cada individuo es un Agente (perfil de conducta de W vehiculos de un canton).
// Poblacion inicial: sembrada con los datos (flota por canton, modelo gravitacional hacia los POI,
//   composicion de la flota, teletrabajo actual), no uniforme aleatoria.
// Aptitud: fitness = 1 / (1 + sum_i w_i |sim_i - obs_i| / obs_i) del agregado de toda la poblacion.
//   La aptitud individual es la contribucion marginal del agente: cuanto sube el error del agregado
//   si se le retira (coevolucion cooperativa).
// Seleccion: torneo dentro del canton. Cruce: mezcla aritmetica de genes. Mutacion: gaussiana con
//   sigma decreciente. Reemplazo: el hijo reemplaza al agente de peor aporte del canton si mejora
//   el error global + la penalizacion por alejarse de los genes de anclaje (elitismo implicito).
// Regularizacion: la aptitud total = error + lambda * distancia media a los genes de anclaje, para que el AG
//   ajuste lo minimo necesario y la solucion no derive entre soluciones equivalentes (no identificables).
#pragma once
#include <vector>

#include "model.hpp"
#include "objective.hpp"

using namespace std;

namespace tj {

struct ConfigGA {
    int generaciones = 150;
    int minGeneraciones = 10;
    double sigma0 = 0.08, sigmaMin = 0.008;
    double tolMedia = 0.015;  // parada anticipada si el error relativo medio baja de este valor
    int torneo = 3;
    double pMutacion = 0.3;
    double fraccionHijos = 0.5;
    double lambdaPrior = 30.0;  // peso de la distancia a los genes de anclaje (regularizacion)
};

struct ResultadoGA {
    int generaciones = 0;
    long evaluaciones = 0;
    double fitness = 0, errorSuma = 0, errorMedia = 0;
};

ResultadoGA ejecutarGA(const Modelo& M, vector<Agente>& pob, const Objetivo& O, const ConfigGA& cfg, Rng& rng);

// Metricas agregadas de toda la poblacion.
void agregarMetricas(const Modelo& M, const vector<Agente>& pob, vector<double>& A);

}  // namespace tj
