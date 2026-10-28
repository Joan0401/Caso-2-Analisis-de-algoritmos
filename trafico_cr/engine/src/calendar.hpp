// calendar.hpp - Calendario: dia de la semana, feriados, eventos, lluvia y muestreo de fechas.
#pragma once
#include <string>
#include <vector>

#include "datastore.hpp"
#include "rng.hpp"

using namespace std;

namespace tj {

struct DiaCtx {
    int anio = 0, mes = 1, dia = 1;
    int dow = 0;        // 0 = lunes ... 6 = domingo
    int tipoDia = 0;    // 0 laboral, 1 sabado, 2 domingo o feriado (perfil horario)
    double volDia = 1, volMes = 1, quincena = 1, feriado = 1, ruido = 1;
    bool lluvia = false;
    vector<int> eventos;  // indices en DataStore::eventos activos ese dia
};

class Calendario {
public:
    explicit Calendario(const DataStore& D) : D_(D) {}

    static int diaSemana(int y, int m, int d);  // 0 = lunes
    static int diasMes(int y, int m);
    static bool bisiesto(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

    // Contexto estocastico de una fecha concreta (lluvia, eventos aleatorios, ruido de demanda).
    DiaCtx contexto(int y, int m, int d, Rng& rng) const;

    // Fecha aleatoria dentro del anio (mes = 0) o del mes, filtrando por tipo de dia:
    // "cualquiera" | "laboral" | "finde".
    DiaCtx muestrear(Rng& rng, int anio, int mes, const string& tipoDia) const;

private:
    const DataStore& D_;
};

}  // namespace tj
