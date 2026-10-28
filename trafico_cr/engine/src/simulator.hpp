// simulator.hpp - Simulador probabilistico de un dia de trafico.
//
// Cada dia muestreado: contexto del calendario (fecha, feriado, lluvia, eventos) + comportamiento de la
// poblacion de conductores del AG (viajes ~ Poisson, destinos ~ multinomial, horas segun el genoma) +
// choques ~ Poisson por arista-hora que pueden generar congestion (reduccion de capacidad).
#pragma once
#include <vector>

#include "calendar.hpp"
#include "escenario.hpp"
#include "model.hpp"
#include "rng.hpp"

using namespace std;

namespace tj {

struct ResultadoDia {
    DiaCtx ctx;
    vector<double> M;         // [origen][destino][hora] vehiculos (incluye efecto de eventos)
    vector<double> flowBase;  // [arista][hora] vehiculos sin eventos ni infraestructura (validacion)
    vector<double> flow;      // [arista][hora] vehiculos (con eventos e infraestructura)
    vector<double> pcef;      // [arista][hora] equivalentes de auto (UCP)
    vector<double> cap;       // [arista][hora] capacidad efectiva (veh/h/sentido)
    vector<double> vc;        // [arista][hora] razon volumen/capacidad del sentido dominante
    vector<double> crashes;   // [arista] choques muestreados
    vector<double> congEv;    // [arista] eventos de congestion por choque
    double tripsHora[24];          // viajes base por hora (sin eventos)
    double crashExpBase = 0;       // choques esperados en condiciones de referencia (validacion)
};

class Simulador {
public:
    Simulador(const Modelo& M, const Escenario& S);
    void dia(const DiaCtx& ctx, Rng& rng, ResultadoDia& R) const;
    static int idxM(const Modelo& M, int o, int k, int h) { return (o * M.ND + k) * 24 + h; }

private:
    const Modelo& M_;
    const Escenario& S_;
    vector<int> playas_, autopistas_, malls_;
};

}  // namespace tj
