#include "interfaz.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

#include "consultas.hpp"
#include "escenario.hpp"
#include "montecarlo.hpp"

using namespace std;

namespace tj {

namespace {

const char* CLASIFICACION =
    "Bucle tipo Las Vegas respecto a la funcion objetivo (repite lotes de dias simulados hasta que el error de "
    "validacion <= tolerancia y el intervalo de confianza es suficientemente angosto) con presupuesto acotado "
    "(Monte Carlo si se agota: convergio=false).";

struct Opciones {
    uint64_t semilla = 12345;
    ConfigMC mc;
    OpcionesCalibracion cal;
    string escenario = "base";
    double tasa = 0.035;
};

Opciones leerOpciones(const DataStore& D, const Json& req) {
    Opciones o;
    if (req.has("semilla") && !req["semilla"].isNull()) o.semilla = static_cast<uint64_t>(llround(req["semilla"].num(12345)));
    o.mc.confianza = req["confianza"].num(0.95);
    if (o.mc.confianza <= 0.5 || o.mc.confianza >= 0.999) throw ErrorConsulta("confianza debe estar entre 0.5 y 0.999.");
    o.mc.ciRel = req["ci_rel"].num(0.05);
    if (o.mc.ciRel <= 0.001 || o.mc.ciRel > 1) throw ErrorConsulta("ci_rel debe estar entre 0.001 y 1.");
    o.mc.tolerancia = req.has("tolerancia") && !req["tolerancia"].isNull() ? req["tolerancia"].num() : -1;
    o.mc.maxIter = max(1, req["max_iter"].integer(300));
    o.mc.maxMs = static_cast<long>(req["max_ms"].num(20000));
    if (req.has("generaciones_ga") && !req["generaciones_ga"].isNull()) {
        int g = max(1, req["generaciones_ga"].integer(100));
        o.cal.gensFase1 = g;
        o.cal.gensFase2 = max(1, static_cast<int>(g * 0.8));
        o.cal.gensReajuste = max(1, static_cast<int>(g * 0.4));
    }
    o.escenario = req["escenario"].str("base");
    if (o.escenario == "bajo") o.tasa = D.tasaBajo;
    else if (o.escenario == "base") o.tasa = D.tasaBase;
    else if (o.escenario == "alto") o.tasa = D.tasaAlto;
    else throw ErrorConsulta("escenario debe ser 'bajo', 'base' o 'alto'.");
    return o;
}

Json estJson(const Estimando& e, const Estadistica& s) {
    Json j = Json::object();
    j.set("nombre", e.nombre);
    j.set("unidad", e.unidad);
    j.set("media", s.media);
    j.set("ic_inferior", s.lo);
    j.set("ic_superior", s.hi);
    j.set("desviacion_diaria", s.desv);
    j.set("n_muestras", s.n);
    return j;
}

Json supuestosBase(const DataStore& D, const Opciones& o, const Escenario& S) {
    Json a = Json::array();
    a.push("Todos los datos de entrada (flota por canton, perfiles horarios, TPD, eventos, pesos de atraccion, "
           "coordenadas) son supuestos de simulacion, no mediciones oficiales.");
    char buf[256];
    snprintf(buf, sizeof buf, "Crecimiento anual de la flota (escenario %s): %.1f%%, multiplicado por el factor de cada zona "
                  "(GAM %.1f, Zona Norte %.1f, Pacifico Norte %.1f).",
                  o.escenario.c_str(), o.tasa * 100, D.multiplicadorZona("GAM"), D.multiplicadorZona("Zona Norte"),
                  D.multiplicadorZona("Pacífico Norte"));
    a.push(string(buf));
    if (S.anios != 0) {
        snprintf(buf, sizeof buf, "Teletrabajo proyectado: %.1f%% de los trabajadores (hoy %.1f%%).", S.teleFut * 100, D.teleActual * 100);
        a.push(string(buf));
        a.push("Proyeccion: la poblacion de conductores se escala con el crecimiento de flota y de los puntos de interes y el AG la re-calibra contra los puntos de validacion proyectados.");
    }
    for (const string& n : S.notas) a.push(n);
    a.push("Las razones v/c usan capacidad de 1700 veh/h por carril y por sentido, 60% del flujo en el sentido dominante en horas pico y equivalentes de auto (moto 0.4, carga pesada 2.5, bus 2.0).");
    a.push("Los choques se muestrean como Poisson por arista y hora; un choque puede reducir la capacidad durante un tiempo lognormal (mediana 50 min).");
    a.push("La incertidumbre reportada es la variabilidad entre dias simulados (calendario, clima, eventos, comportamiento); el escenario de crecimiento se fija en la consulta.");
    return a;
}

Json metricasJson(const Escenario& S, const ResultadoMC& R, const Opciones& o, double tiempoTotalMs) {
    Json m = Json::object();
    m.set("ga_generaciones", S.gaGeneraciones);
    m.set("ga_evaluaciones", S.gaEvaluaciones);
    m.set("ga_mejor_fitness", S.gaFitness);
    m.set("ga_error_relativo_medio", S.gaErrorMedia);
    m.set("muestras_dias", R.muestras);
    m.set("dias_validacion", R.diasValidacion);
    m.set("iteraciones_bucle", R.iteraciones);
    m.set("error_validacion_final", R.errorMedia);
    m.set("error_validacion_suma_ponderada", R.errorSuma);
    m.set("tolerancia_objetivo", R.tolerancia);
    m.set("objetivo_cumplido", R.objOk);
    m.set("nivel_confianza", o.mc.confianza);
    m.set("ic_relativo_objetivo", o.mc.ciRel);
    m.set("ic_relativo_maximo_logrado", R.ciRelMax);
    m.set("ic_cumplido", R.ciOk);
    m.set("convergio", R.convergio);
    m.set("limite_tiempo_alcanzado", R.limiteTiempo);
    m.set("tiempo_ms", tiempoTotalMs);
    m.set("semilla", static_cast<double>(o.semilla));
    m.set("algoritmo", CLASIFICACION);
    return m;
}

// ---- flota: Monte Carlo sobre tasas de crecimiento (no necesita simular dias) ----
Json procesarFlota(Modelo& M, const Json& req, const Opciones& o, const chrono::steady_clock::time_point& t0) {
    const DataStore& D = M.D;
    Json par = Json::object();
    par.set("tipo", "flota");
    int anio = req["anio"].integer(D.anioBase);
    if (anio < D.anioBase - 5 || anio > D.anioBase + D.horizonteMax)
        throw ErrorConsulta("El anio debe estar entre " + to_string(D.anioBase - 5) + " y " + to_string(D.anioBase + D.horizonteMax) + ".");
    par.set("anio", anio);
    string nivel = normalizar(req["nivel"].str("nacional"));
    string nombre = req["nombre"].str();
    double base = 0, mult = 1;
    string etiqueta = "Costa Rica";
    Json extraNota = Json::array();
    if (nivel == "nacional" || nivel == "pais") {
        base = D.flotaTotal;
        nivel = "nacional";
    } else if (nivel == "provincia") {
        const Provincia* P = nullptr;
        for (const Provincia& p : D.provincias) if (normalizar(p.nombre) == normalizar(nombre)) P = &p;
        if (!P) {
            vector<string> s;
            for (auto& p : D.provincias) s.push_back(p.nombre);
            throw ErrorConsulta("Provincia desconocida: '" + nombre + "'.", s);
        }
        base = P->participacion * D.flotaTotal;
        etiqueta = P->nombre;
        double w = 0, ms = 0;
        for (const Canton& c : D.cantones) if (c.provincia == P->nombre) { w += c.vehiculos; ms += c.vehiculos * D.multiplicadorZona(c.zona); }
        mult = w > 0 ? ms / w : 1.0;
    } else if (nivel == "zona") {
        string zn;
        for (const string& z : D.zonas) if (normalizar(z) == normalizar(nombre)) zn = z;
        if (zn.empty()) throw ErrorConsulta("Zona desconocida: '" + nombre + "'.", D.zonas);
        for (const Canton& c : D.cantones) if (c.zona == zn) base += c.vehiculos;
        mult = D.multiplicadorZona(zn);
        etiqueta = zn;
        extraNota.push("La flota de una zona suma solo los cantones modelados de esa zona.");
    } else if (nivel == "canton") {
        Lugar L = resolverLugar(M, nombre);
        if (L.tipo != Lugar::CANTON) throw ErrorConsulta("Canton desconocido: '" + nombre + "'.", sugerirLugares(M, nombre));
        base = D.cantones[L.idx[0]].vehiculos;
        mult = D.multiplicadorZona(D.cantones[L.idx[0]].zona);
        etiqueta = L.nombre;
    } else {
        throw ErrorConsulta("nivel debe ser 'nacional', 'provincia', 'zona' o 'canton'.");
    }
    par.set("nivel", nivel);
    par.set("nombre", etiqueta);
    par.set("escenario_crecimiento", req.has("escenario") ? o.escenario : "triangular(bajo, base, alto)");

    Escenario S = construirEscenario(M, D.anioBase, D.tasaBase, "base", o.semilla, o.cal);

    Rng rng(mezclar(o.semilla, 3));
    const int years = anio - D.anioBase;
    const double z = cuantilNormal(0.5 + o.mc.confianza / 2.0);
    long n = 0;
    double m1 = 0, s1 = 0;
    int iter = 0;
    bool ciOk = false;
    const int lote = 100;
    while (true) {
        for (int b = 0; b < lote; b++) {
            double tasa = req.has("escenario") ? o.tasa : rng.triangular(D.tasaBajo, D.tasaBase, D.tasaAlto);
            double f = 1.0;
            for (int y = 0; y < abs(years); y++) f *= 1.0 + tasa * mult + 0.01 * rng.normal();
            double v = years >= 0 ? base * f : base / f;
            n++;
            double d = v - m1;
            m1 += d / n;
            s1 += d * (v - m1);
        }
        iter++;
        double sd = n > 1 ? sqrt(s1 / (n - 1)) : 0;
        double half = z * sd / sqrt(static_cast<double>(n));
        ciOk = half <= o.mc.ciRel * fabs(m1) || sd == 0;
        if ((ciOk && n >= 200) || iter >= o.mc.maxIter) break;
    }
    double sd = n > 1 ? sqrt(s1 / (n - 1)) : 0;
    double half = z * sd / sqrt(static_cast<double>(n));
    Json est = Json::array();
    Estimando e1{"flota_registrada", "vehiculos", 0}, e2{"flota_circulante", "vehiculos", 0};
    est.push(estJson(e1, {m1, sd, m1 - half, m1 + half, n}));
    double fc = D.factorCirc;
    est.push(estJson(e2, {m1 * fc, sd * fc, (m1 - half) * fc, (m1 + half) * fc, n}));
    Json res = Json::object();
    res.set("estimaciones", est);
    res.set("flota_base_anio_" + to_string(D.anioBase), base);
    if (nivel == "nacional") {
        Json prov = Json::array();
        for (const Provincia& p : D.provincias) {
            Json o2 = Json::object();
            o2.set("provincia", p.nombre);
            o2.set("flota_registrada_media", m1 * p.participacion);
            prov.push(o2);
        }
        res.set("por_provincia_proporcional", prov);
    }

    Json sup = Json::array();
    sup.push("La flota por provincia y canton es un reparto supuesto de la flota nacional; no es dato oficial.");
    char buf[200];
    snprintf(buf, sizeof buf, "Tasa anual de crecimiento: triangular(%.1f%%, %.1f%%, %.1f%%) x factor de zona %.2f, con ruido anual de 1 punto porcentual.",
                  D.tasaBajo * 100, D.tasaBase * 100, D.tasaAlto * 100, mult);
    sup.push(string(buf));
    for (size_t i = 0; i < extraNota.size(); i++) sup.push(extraNota.at(i));
    sup.push("La flota circulante es la registrada por el factor de marchamos pagados (" + to_string(fc).substr(0, 6) + ").");

    Json m = Json::object();
    m.set("ga_generaciones", S.gaGeneraciones);
    m.set("ga_evaluaciones", S.gaEvaluaciones);
    m.set("ga_mejor_fitness", S.gaFitness);
    m.set("ga_error_relativo_medio", S.gaErrorMedia);
    m.set("muestras_dias", 0);
    m.set("muestras_tasas", n);
    m.set("iteraciones_bucle", iter);
    m.set("error_validacion_final", S.gaErrorMedia);
    m.set("tolerancia_objetivo", S.obj.tolerancia);
    m.set("objetivo_cumplido", S.gaErrorMedia <= S.obj.tolerancia);
    m.set("nivel_confianza", o.mc.confianza);
    m.set("ic_relativo_objetivo", o.mc.ciRel);
    m.set("ic_cumplido", ciOk);
    m.set("convergio", ciOk && S.gaErrorMedia <= S.obj.tolerancia);
    m.set("tiempo_ms", chrono::duration<double, milli>(chrono::steady_clock::now() - t0).count());
    m.set("semilla", static_cast<double>(o.semilla));
    m.set("algoritmo", CLASIFICACION);

    Json out = Json::object();
    out.set("ok", true);
    out.set("tipo", "flota");
    out.set("parametros", par);
    out.set("resultado", res);
    out.set("supuestos", sup);
    out.set("metricas", m);
    return out;
}

}  // namespace

Json procesarSolicitud(const DataStore& D, const Json& req) {
    const auto t0 = chrono::steady_clock::now();
    try {
        const string tipo = req["tipo"].str();
        Modelo M(D);
        if (tipo == "lugares") {
            Json out = Json::object();
            out.set("ok", true);
            out.set("tipo", "lugares");
            out.set("resultado", catalogoLugares(M));
            return out;
        }
        Opciones o = leerOpciones(D, req);
        if (tipo == "flota") return procesarFlota(M, req, o, t0);

        Json par = Json::object();
        Consulta C = construirConsulta(M, req, par);  // valida antes de calcular nada
        par.set("escenario_crecimiento", o.escenario);
        par.set("tasa_anual_flota", o.tasa);

        // Se calibran K poblaciones independientes con el AG (soluciones equivalentes distintas) y se
        // combinan para incluir en el intervalo la incertidumbre de calibracion.
        const int K = max(1, min(10, req["calibraciones"].integer(3)));
        par.set("calibraciones", K);
        vector<ResultadoMC> reps;
        Escenario S;
        long gaGen = 0, gaEval = 0;
        double gaFit = 0, gaErr = 0;
        for (int r = 0; r < K; r++) {
            uint64_t sr = K == 1 ? o.semilla : mezclar(o.semilla, 100 + static_cast<uint64_t>(r));
            Escenario Sr = construirEscenario(M, C.anio, o.tasa, o.escenario, sr, o.cal);
            gaGen += Sr.gaGeneraciones;
            gaEval += Sr.gaEvaluaciones;
            gaFit += Sr.gaFitness / K;
            gaErr += Sr.gaErrorMedia / K;
            reps.push_back(ejecutarMC(M, Sr, C, o.mc, sr));
            if (r == 0) S = Sr;
        }
        double cvCalib = 0;
        ResultadoMC R = combinarReplicas(reps, C, o.mc, cvCalib);
        S.gaGeneraciones = static_cast<int>(gaGen);
        S.gaEvaluaciones = gaEval;
        S.gaFitness = gaFit;
        S.gaErrorMedia = gaErr;

        Json res = Json::object();
        Json est = Json::array();
        for (size_t i = 0; i < C.est.size(); i++) est.push(estJson(C.est[i], R.est[i]));
        if (C.tipo == "impacto_pois") {
            vector<size_t> ord(C.est.size());
            for (size_t i = 0; i < ord.size(); i++) ord[i] = i;
            sort(ord.begin(), ord.end(), [&](size_t a, size_t b) { return R.est[a].media > R.est[b].media; });
            Json rank = Json::array();
            int pos = 1;
            for (size_t i : ord) {
                Json r = estJson(C.est[i], R.est[i]);
                r.set("posicion", pos++);
                rank.push(r);
            }
            res.set("ranking_impacto_en_congestion", rank);
        } else {
            res.set("estimaciones", est);
        }
        if (!C.serieNombre.empty()) {
            Json s = Json::object();
            s.set("nombre", C.serieNombre);
            s.set("unidad", C.serieUnidad);
            Json v = Json::array();
            for (double x : R.serie) v.push(x);
            s.set("valores_hora_0_a_23", v);
            res.set("perfil_horario", s);
        }
        if (C.tipo == "choques") {
            double dias = C.mes == 0 ? (Calendario::bisiesto(C.anio) ? 366 : 365) : Calendario::diasMes(C.anio, C.mes);
            res.set("choques_en_el_periodo_estimados", R.est[0].media * dias);
            res.set("dias_del_periodo", dias);
        }

        {
            Json vp = Json::array();
            for (size_t i = 0; i < S.obj.pts.size(); i++) {
                Json q = Json::object();
                q.set("id", S.obj.pts[i].id);
                q.set("observado_o_proyectado", S.obj.pts[i].objetivo);
                q.set("simulado", R.simValidacion[i]);
                q.set("error_relativo", fabs(R.simValidacion[i] - S.obj.pts[i].objetivo) / S.obj.pts[i].objetivo);
                vp.push(q);
            }
            res.set("puntos_de_validacion", vp);
            double emax = 0;
            for (size_t i = 0; i < vp.size(); i++) emax = max(emax, vp.at(i)["error_relativo"].num());
            res.set("error_validacion_maximo_por_punto", emax);
        }
        Json out = Json::object();
        out.set("ok", true);
        out.set("tipo", C.tipo);
        out.set("parametros", par);
        out.set("resultado", res);
        Json sup = supuestosBase(D, o, S);
        if (C.tipo == "flujo_od" || C.tipo == "flujo_destino")
            sup.push("Los viajes se modelan en sentido casa->destino desde los cantones modelados; los regresos estan incluidos en los viajes por vehiculo y dia (2.4) y no se cuentan como viajes inversos.");
        sup.push("Intervalo de confianza: combina la variabilidad entre dias simulados y entre " + to_string(K) + " poblaciones calibradas por el AG (incertidumbre de calibracion).");
        out.set("supuestos", sup);
        double ms = chrono::duration<double, milli>(chrono::steady_clock::now() - t0).count();
        Json met = metricasJson(S, R, o, ms);
        met.set("calibraciones_ga", K);
        met.set("variacion_entre_calibraciones", cvCalib);
        out.set("metricas", met);
        return out;
    } catch (const ErrorConsulta& e) {
        Json j = Json::object();
        j.set("ok", false);
        j.set("error", string(e.what()));
        Json s = Json::array();
        for (auto& x : e.sugerencias) s.push(x);
        j.set("sugerencias", s);
        return j;
    } catch (const exception& e) {
        Json j = Json::object();
        j.set("ok", false);
        j.set("error", string("Error interno del motor: ") + e.what());
        return j;
    }
}

}  // namespace tj
