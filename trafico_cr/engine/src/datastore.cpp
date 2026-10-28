#include "datastore.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>

using namespace std;


namespace tj {

string normalizar(const string& in) {
    string out;
    for (size_t i = 0; i < in.size(); i++) {
        unsigned char c = static_cast<unsigned char>(in[i]);
        if (c == 0xC3 && i + 1 < in.size()) {
            unsigned char d = static_cast<unsigned char>(in[++i]);
            switch (d) {
                case 0xA1: case 0x81: out += 'a'; break;
                case 0xA9: case 0x89: out += 'e'; break;
                case 0xAD: case 0x8D: out += 'i'; break;
                case 0xB3: case 0x93: out += 'o'; break;
                case 0xBA: case 0x9A: case 0xBC: case 0x9C: out += 'u'; break;
                case 0xB1: case 0x91: out += 'n'; break;
                default: break;
            }
            continue;
        }
        if (c >= 'A' && c <= 'Z') out += static_cast<char>(c - 'A' + 'a');
        else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) out += static_cast<char>(c);
        else if (!out.empty() && out.back() != ' ') out += ' ';
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

static Json leer(const string& dir, const string& archivo) {
    string ruta = dir + "/" + archivo;
    ifstream f(ruta);
    if (!f) throw runtime_error("No se pudo abrir el archivo de datos: " + ruta);
    stringstream ss;
    ss << f.rdbuf();
    try {
        return Json::parse(ss.str());
    } catch (const exception& e) {
        throw runtime_error(archivo + ": " + e.what());
    }
}

template <size_t N>
static void llenar(array<double, N>& dst, const Json& j) {
    for (size_t i = 0; i < N; i++) dst[i] = j.at(i).num(0);
}

DataStore DataStore::cargar(const string& dir) {
    DataStore D;

    // 01 flota
    Json j = leer(dir, "01_flota_cantonal.json");
    D.flotaTotal = j["total_flota_registrada"].num();
    D.marchamos = j["total_marchamos_pagados"].num();
    D.factorCirc = j["factor_circulante"].num(1);
    for (auto& p : j["provincias"].a) D.provincias.push_back({p["provincia"].str(), p["participacion_flota"].num()});
    for (auto& c : j["cantones"].a) {
        Canton k;
        k.nombre = c["canton"].str();
        k.provincia = c["provincia"].str();
        k.zona = c["zona"].str();
        k.vehiculos = c["vehiculos"].num();
        k.circulantes = c["circulantes"].num();
        k.poblacion = c["poblacion"].num();
        D.cantones.push_back(k);
    }

    // 02 zonas
    j = leer(dir, "02_zonas_estrategicas.json");
    for (auto& z : j["zonas"].a) D.zonas.push_back(z.str());

    // 03 crecimiento
    j = leer(dir, "03_crecimiento_proyecciones.json");
    D.inscripciones = j["inscripciones_anuales"].num();
    D.anioBase = j["anio_base"].integer(2025);
    D.tasaBajo = j["tasa_crecimiento_flota"]["bajo"].num();
    D.tasaBase = j["tasa_crecimiento_flota"]["base"].num();
    D.tasaAlto = j["tasa_crecimiento_flota"]["alto"].num();
    D.horizonteMax = j["horizonte_max_anios"].integer(15);
    for (auto& p : j["multiplicador_crecimiento_zona"].o) D.multZona[p.first] = p.second.num(1);
    D.composicion = {j["composicion_flota"]["automovil"].num(), j["composicion_flota"]["motocicleta"].num(),
                     j["composicion_flota"]["carga_pesada"].num(), j["composicion_flota"]["bus"].num()};
    D.teleActual = j["teletrabajo"]["fraccion_actual"].num();
    D.teleAumento = j["teletrabajo"]["aumento_anual"].num();
    D.poiCrec = j["crecimiento_poi_anual"].num();
    for (auto& x : j["infraestructura"].a) {
        Infra f;
        f.id = x["id"].str();
        f.nombre = x["nombre"].str();
        f.arista = -2;  // se resuelve tras cargar la red (guardamos el id en nombre temporal)
        f.anio = x["anio_inicio"].integer();
        f.fcap = x["factor_capacidad"].num(1);
        f.fdem = x["factor_demanda"].num(1);
        f.id = x["id"].str() + "|" + x["arista"].str();
        D.infra.push_back(f);
    }

    // 04 riesgo
    j = leer(dir, "04_riesgo_choques.json");
    D.accAnuales = j["accidentes_anuales"].num();
    {
        static const char* dias[7] = {"lunes", "martes", "miercoles", "jueves", "viernes", "sabado", "domingo"};
        for (int d = 0; d < 7; d++) D.sevDia[d] = j["severidad_relativa_dia"][dias[d]].num(1);
    }
    {
        static const char* tp[4] = {"automovil", "motocicleta", "carga_pesada", "bus"};
        for (int i = 0; i < 4; i++) D.riesgoTipo[i] = j["riesgo_tipo_vehiculo"][tp[i]].num(1);
        static const char* fr[5] = {"madrugada", "pico_am", "valle", "pico_pm", "noche"};
        for (int i = 0; i < 5; i++) D.riesgoFranja[i] = j["riesgo_franja"][fr[i]].num(1);
    }
    D.lluviaRiesgo = j["lluvia"]["mult_riesgo"].num(1);
    D.lluviaVel = j["lluvia"]["mult_velocidad"].num(1);
    D.pCong[0] = j["p_choque_causa_congestion"]["autopista"].num();
    D.pCong[1] = j["p_choque_causa_congestion"]["ruta_nacional"].num();
    D.pCong[2] = j["p_choque_causa_congestion"]["calle_cantonal"].num();
    D.congMediana = j["duracion_congestion_min"]["mediana"].num(50);
    D.congSigma = j["duracion_congestion_min"]["sigma"].num(0.6);
    D.perdidaCap = j["perdida_capacidad_choque"].num(0.5);

    // 05 perfiles
    j = leer(dir, "05_perfiles_horarios.json");
    llenar(D.perfil[0], j["perfil_laboral"]);
    llenar(D.perfil[1], j["perfil_sabado"]);
    llenar(D.perfil[2], j["perfil_domingo"]);
    {
        static const char* dias[7] = {"lunes", "martes", "miercoles", "jueves", "viernes", "sabado", "domingo"};
        for (int d = 0; d < 7; d++) D.volDia[d] = j["volumen_dia_semana"][dias[d]].num(1);
    }
    llenar(D.volMes, j["volumen_mes"]);
    D.factorQuincena = j["factor_quincena"].num(1);
    D.viajesVehDia = j["viajes_por_vehiculo_dia"].num(2.4);
    D.shareTrabajo = j["motivos"]["trabajo"].num(0.35);

    // 06 calendario
    j = leer(dir, "06_calendario_eventos.json");
    for (auto& f : j["feriados"].a)
        D.feriados.push_back({f["nombre"].str(), f["mes"].integer(), f["dia"].integer(), f["factor_volumen"].num(1)});
    llenar(D.pLluvia, j["p_lluvia_mes"]);
    for (auto& e : j["eventos"].a) {
        Evento v;
        v.id = e["id"].str();
        v.nombre = e["nombre"].str();
        v.tipo = e["tipo"].str();
        v.poi = e["poi"].str();
        v.p = e["p_dia"].num();
        v.mult = e["mult_trafico"].num(1);
        v.radio = e["radio_km"].num();
        v.dur = e["duracion_h"].integer(1);
        v.ini = e["hora_inicio"].integer();
        for (auto& m : e["meses"].a) v.meses.push_back(m.integer());
        for (auto& m : e["dias_semana"].a) v.dias.push_back(m.integer());
        for (auto& m : e["fechas_fijas"].a) v.fijas.push_back({m.at(0).integer(), m.at(1).integer()});
        D.eventos.push_back(v);
    }

    // 08 red vial (antes que los POI porque estos referencian aristas)
    j = leer(dir, "08_red_vial.json");
    D.capCarril = j["capacidad_carril_veh_h"].num(1700);
    for (auto& n : j["nodos"].a) D.nodos.push_back(n.str());
    for (auto& e : j["aristas"].a) {
        Arista a;
        a.id = e["id"].str();
        a.ruta = e["ruta"].str();
        a.tipo = e["tipo"].str();
        a.a = D.nodoIdx(e["desde"].str());
        a.b = D.nodoIdx(e["hasta"].str());
        if (a.a < 0 || a.b < 0) throw runtime_error("Arista " + a.id + " con nodo desconocido");
        a.km = e["longitud_km"].num();
        a.carriles = e["carriles_sentido"].integer(1);
        a.cap = e["capacidad_veh_h"].num();
        a.tpd = e["tpd_semilla"].num();
        a.vel = e["vel_libre_kmh"].num(60);
        D.aristas.push_back(a);
    }
    for (auto& c : D.cantones) {
        c.nodo = D.nodoIdx(c.nombre);
        if (c.nodo < 0) throw runtime_error("El canton " + c.nombre + " no esta en la red vial");
    }
    for (auto& f : D.infra) {
        size_t bar = f.id.find('|');
        string arista = f.id.substr(bar + 1);
        f.id = f.id.substr(0, bar);
        f.arista = D.aristaIdx(arista);
        if (f.arista < 0) throw runtime_error("Infraestructura " + f.id + " con arista desconocida " + arista);
    }

    // 07 puntos de interes
    j = leer(dir, "07_puntos_interes.json");
    for (auto& p : j["pois"].a) {
        Poi q;
        q.id = p["id"].str();
        q.nombre = p["nombre"].str();
        q.tipo = p["tipo"].str();
        q.canton = p["canton"].str();
        q.lat = p["lat"].num();
        q.lon = p["lon"].num();
        q.peso = p["peso_atraccion"].num();
        q.cantonIdx = D.cantonIdx(q.canton);
        q.nodo = D.nodoIdx(p["nodo"].str());
        if (q.cantonIdx < 0 || q.nodo < 0) throw runtime_error("POI " + q.id + " con canton o nodo desconocido");
        for (auto& a : p["aristas_acceso"].a) {
            int k = D.aristaIdx(a.str());
            if (k < 0) throw runtime_error("POI " + q.id + " con arista desconocida " + a.str());
            q.aristas.push_back(k);
        }
        for (auto& h : p["horas_pico"].a) q.picos.push_back(h.integer());
        for (auto& a : p["alias"].a) q.alias.push_back(normalizar(a.str()));
        q.alias.push_back(normalizar(q.nombre));
        D.pois.push_back(q);
    }

    // 09 validacion
    j = leer(dir, "09_validacion.json");
    D.tolerancia = j["tolerancia_objetivo"].num(0.05);
    for (auto& p : j["puntos"].a)
        D.validacion.push_back({p["id"].str(), p["metrica"].str(), p["objetivo"].str(), p["valor"].num(), p["peso"].num(1)});

    return D;
}

int DataStore::nodoIdx(const string& n) const {
    for (size_t i = 0; i < nodos.size(); i++) if (nodos[i] == n) return static_cast<int>(i);
    return -1;
}
int DataStore::cantonIdx(const string& nombre) const {
    for (size_t i = 0; i < cantones.size(); i++) if (cantones[i].nombre == nombre) return static_cast<int>(i);
    return -1;
}
int DataStore::aristaIdx(const string& id) const {
    for (size_t i = 0; i < aristas.size(); i++) if (aristas[i].id == id) return static_cast<int>(i);
    return -1;
}
int DataStore::poiIdx(const string& id) const {
    for (size_t i = 0; i < pois.size(); i++) if (pois[i].id == id) return static_cast<int>(i);
    return -1;
}
double DataStore::multiplicadorZona(const string& zona) const {
    auto it = multZona.find(zona);
    return it == multZona.end() ? 1.0 : it->second;
}

}  // namespace tj
