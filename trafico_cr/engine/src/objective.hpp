// objective.hpp - Funcion objetivo basada en puntos de validacion.
//
//   suma  = sum_i w_i * |sim_i - obs_i| / obs_i
//   fitness (aptitud del AG) = 1 / (1 + suma)
//   media = suma / sum_i w_i        (error relativo medio ponderado; se compara con la tolerancia)
#pragma once
#include <string>
#include <vector>

#include "model.hpp"

using namespace std;

namespace tj {

class Objetivo {
public:
    enum Tipo { FLOTA, ACC, PICO_AM, PICO_PM, FRAC_DIA, TPD };
    struct Punto {
        Tipo tipo;
        int idx;          // arista o dia de semana segun el tipo
        double objetivo;  // valor observado (o proyectado) contra el que se compara
        double peso;
        string id;
    };
    struct Error { double suma = 0, media = 0, fitness = 0; };

    vector<Punto> pts;
    double tolerancia = 0.05;
    double flotaFija = 0;  // flota registrada fuera de los cantones modelados (vehiculos)

    static Objetivo base(const Modelo& M);

    // Objetivo para un anio futuro: escala flota, accidentes y TPD con el crecimiento por zona.
    // flotaObjetivo: flota nacional proyectada; flotaFijaFut: parte no modelada proyectada.
    Objetivo escalar(const Modelo& M, int anios, double tasa, double teleFactor, double flotaObjetivo,
                     double flotaFijaFut) const;

    // Valores simulados de cada punto a partir de las metricas agregadas de la poblacion.
    void valores(const Modelo& M, const double* A, vector<double>& sim) const;
    Error evaluar(const vector<double>& sim) const;
    Error evaluarAgregado(const Modelo& M, const double* A) const;

    static double crecimientoArista(const Modelo& M, int e, int anios, double tasa);
};

}  // namespace tj
