#include "calendar.hpp"

#include <algorithm>
#include <cmath>

using namespace std;


namespace tj {

int Calendario::diaSemana(int y, int m, int d) {
    // Zeller/Sakamoto: 0 = domingo; se convierte a 0 = lunes
    static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    int yy = m < 3 ? y - 1 : y;
    int w = (yy + yy / 4 - yy / 100 + yy / 400 + t[m - 1] + d) % 7;
    return (w + 6) % 7;
}

int Calendario::diasMes(int y, int m) {
    static const int dm[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return (m == 2 && bisiesto(y)) ? 29 : dm[m - 1];
}

DiaCtx Calendario::contexto(int y, int m, int d, Rng& rng) const {
    DiaCtx c;
    c.anio = y;
    c.mes = m;
    c.dia = d;
    c.dow = diaSemana(y, m, d);
    c.volDia = D_.volDia[c.dow];
    c.volMes = D_.volMes[m - 1];
    bool pago = (d == 1 || d == 14 || d == 15 || d == 16 || d >= 29);
    c.quincena = pago ? D_.factorQuincena : 1.0;
    for (const Feriado& f : D_.feriados) if (f.mes == m && f.dia == d) c.feriado = f.factor;
    c.tipoDia = c.feriado < 1.0 ? 2 : (c.dow < 5 ? 0 : c.dow - 4);
    c.lluvia = rng.bernoulli(D_.pLluvia[m - 1]);
    c.ruido = rng.lognormal(0.0, 0.04);

    for (size_t i = 0; i < D_.eventos.size(); i++) {
        const Evento& e = D_.eventos[i];
        bool activo = false;
        for (auto& f : e.fijas) if (f[0] == m && f[1] == d) activo = true;
        bool enMes = e.meses.empty() || find(e.meses.begin(), e.meses.end(), m) != e.meses.end();
        if (!activo && e.p >= 1.0 && !e.meses.empty()) activo = enMes;
        if (!activo && e.p < 1.0 && enMes) {
            bool diaOk = e.dias.empty() || find(e.dias.begin(), e.dias.end(), c.dow) != e.dias.end();
            if (diaOk) {
                double p = e.p * (e.dias.empty() ? 1.0 : 7.0 / e.dias.size());
                activo = rng.bernoulli(min(1.0, p));
            }
        }
        if (activo) c.eventos.push_back(static_cast<int>(i));
    }
    return c;
}

DiaCtx Calendario::muestrear(Rng& rng, int anio, int mes, const string& tipoDia) const {
    for (int intento = 0; intento < 200; intento++) {
        int m = mes, d;
        if (mes == 0) {
            int n = rng.entero(bisiesto(anio) ? 366 : 365);
            m = 1;
            while (n >= diasMes(anio, m)) { n -= diasMes(anio, m); m++; }
            d = n + 1;
        } else {
            d = 1 + rng.entero(diasMes(anio, mes));
        }
        int dow = diaSemana(anio, m, d);
        if (tipoDia == "laboral" && dow >= 5) continue;
        if (tipoDia == "finde" && dow < 5) continue;
        DiaCtx c = contexto(anio, m, d, rng);
        if (tipoDia == "laboral" && c.feriado < 1.0) continue;
        return c;
    }
    return contexto(anio, mes == 0 ? 1 : mes, 1, rng);
}

}  // namespace tj
