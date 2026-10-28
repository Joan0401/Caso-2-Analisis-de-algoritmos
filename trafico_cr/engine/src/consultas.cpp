#include "consultas.hpp"

#include <algorithm>
#include <cmath>

using namespace std;


namespace tj {

static bool contienePalabras(const string& hay, const string& aguja) {
    if (aguja.empty()) return false;
    return (" " + hay + " ").find(" " + aguja + " ") != string::npos;
}

string nombreTipoLugar(Lugar::Tipo t) {
    switch (t) {
        case Lugar::POI: return "punto_de_interes";
        case Lugar::CANTON: return "canton";
        case Lugar::ARISTAS: return "ruta";
        case Lugar::ZONA: return "zona";
        case Lugar::PROVINCIA: return "provincia";
        case Lugar::NACIONAL: return "pais";
        default: return "desconocido";
    }
}

Lugar resolverLugar(const Modelo& M, const string& texto) {
    const DataStore& D = M.D;
    const string t = normalizar(texto);
    Lugar L;
    if (t.empty()) return L;
    if (t == "nacional" || t == "costa rica" || t == "pais" || t == "todo el pais") {
        L.tipo = Lugar::NACIONAL;
        L.nombre = "Costa Rica";
        return L;
    }
    for (size_t e = 0; e < D.aristas.size(); e++)
        if (normalizar(D.aristas[e].id) == t) {
            L.tipo = Lugar::ARISTAS; L.idx = {static_cast<int>(e)}; L.nombre = D.aristas[e].ruta; return L;
        }
    for (size_t k = 0; k < D.pois.size(); k++)
        if (normalizar(D.pois[k].id) == t) {
            L.tipo = Lugar::POI; L.idx = {static_cast<int>(k)}; L.nombre = D.pois[k].nombre; return L;
        }
    for (size_t c = 0; c < D.cantones.size(); c++)
        if (normalizar(D.cantones[c].nombre) == t) {
            L.tipo = Lugar::CANTON; L.idx = {static_cast<int>(c)}; L.nombre = D.cantones[c].nombre; return L;
        }
    for (size_t k = 0; k < D.pois.size(); k++)
        for (const string& a : D.pois[k].alias)
            if (a == t) {
                L.tipo = Lugar::POI; L.idx = {static_cast<int>(k)}; L.nombre = D.pois[k].nombre; return L;
            }
    for (const string& z : D.zonas)
        if (normalizar(z) == t) { L.tipo = Lugar::ZONA; L.nombre = z; return L; }
    for (const Provincia& p : D.provincias)
        if (normalizar(p.nombre) == t) { L.tipo = Lugar::PROVINCIA; L.nombre = p.nombre; return L; }

    // rutas por nombre ("ruta 1", "ruta 27")
    if (t.size() >= 6 && t.compare(0, 5, "ruta ") == 0) {
        for (size_t e = 0; e < D.aristas.size(); e++)
            if (contienePalabras(normalizar(D.aristas[e].ruta), t)) L.idx.push_back(static_cast<int>(e));
        if (!L.idx.empty()) { L.tipo = Lugar::ARISTAS; L.nombre = texto; return L; }
    }
    // coincidencia parcial con POI (alias mas largo)
    int mejor = -1;
    size_t mejorLen = 0;
    for (size_t k = 0; k < D.pois.size(); k++)
        for (const string& a : D.pois[k].alias) {
            if (a.size() < 4) continue;
            if ((contienePalabras(t, a) || (t.size() >= 4 && contienePalabras(a, t))) && a.size() > mejorLen) {
                mejor = static_cast<int>(k);
                mejorLen = a.size();
            }
        }
    if (mejor >= 0) { L.tipo = Lugar::POI; L.idx = {mejor}; L.nombre = D.pois[mejor].nombre; return L; }
    for (size_t c = 0; c < D.cantones.size(); c++) {
        string n = normalizar(D.cantones[c].nombre);
        if (contienePalabras(t, n) || (t.size() >= 4 && contienePalabras(n, t))) {
            L.tipo = Lugar::CANTON; L.idx = {static_cast<int>(c)}; L.nombre = D.cantones[c].nombre; return L;
        }
    }
    return L;
}

vector<string> sugerirLugares(const Modelo& M, const string& texto) {
    const DataStore& D = M.D;
    vector<string> cand, out;
    for (auto& p : D.pois) cand.push_back(p.nombre);
    for (auto& c : D.cantones) cand.push_back(c.nombre);
    for (auto& z : D.zonas) cand.push_back(z);
    string t = normalizar(texto);
    vector<string> tokens;
    size_t i = 0;
    while (i < t.size()) {
        size_t j = t.find(' ', i);
        if (j == string::npos) j = t.size();
        if (j - i >= 3) tokens.push_back(t.substr(i, j - i));
        i = j + 1;
    }
    for (const string& c : cand) {
        string n = normalizar(c);
        for (const string& tk : tokens)
            if (n.find(tk) != string::npos) { out.push_back(c); break; }
        if (out.size() >= 8) break;
    }
    if (out.empty()) for (size_t k = 0; k < cand.size() && out.size() < 8; k += 3) out.push_back(cand[k]);
    return out;
}

Json catalogoLugares(const Modelo& M) {
    const DataStore& D = M.D;
    Json j = Json::object();
    Json pois = Json::array();
    for (auto& p : D.pois) {
        Json o = Json::object();
        o.set("id", p.id);
        o.set("nombre", p.nombre);
        o.set("tipo", p.tipo);
        o.set("canton", p.canton);
        pois.push(o);
    }
    j.set("puntos_de_interes", pois);
    Json cant = Json::array();
    for (auto& c : D.cantones) {
        Json o = Json::object();
        o.set("canton", c.nombre);
        o.set("provincia", c.provincia);
        o.set("zona", c.zona);
        cant.push(o);
    }
    j.set("cantones", cant);
    Json zonas = Json::array();
    for (auto& z : D.zonas) zonas.push(z);
    j.set("zonas", zonas);
    Json provs = Json::array();
    for (auto& p : D.provincias) provs.push(p.nombre);
    j.set("provincias", provs);
    Json rutas = Json::array();
    for (auto& a : D.aristas) {
        Json o = Json::object();
        o.set("id", a.id);
        o.set("ruta", a.ruta);
        o.set("desde", D.nodos[a.a]);
        o.set("hasta", D.nodos[a.b]);
        o.set("carriles_por_sentido", a.carriles);
        rutas.push(o);
    }
    j.set("rutas", rutas);
    return j;
}

// ---------------------------------------------------------------------------------------------

static Lugar exigirLugar(const Modelo& M, const Json& req, const char* campo, bool obligatorio) {
    string txt = req[campo].str();
    if (txt.empty()) {
        if (obligatorio) throw ErrorConsulta(string("Falta el parametro '") + campo + "'.", sugerirLugares(M, ""));
        return Lugar();
    }
    Lugar L = resolverLugar(M, txt);
    if (L.tipo == Lugar::NINGUNO)
        throw ErrorConsulta("No reconozco el lugar '" + txt + "'. Use un nombre de la lista de lugares.", sugerirLugares(M, txt));
    return L;
}

static vector<char> mascaraEntrada(const Modelo& M, const Lugar& L) {
    const DataStore& D = M.D;
    vector<char> m(static_cast<size_t>(M.NC) * M.ND, 0);
    for (int o = 0; o < M.NC; o++)
        for (int k = 0; k < M.ND; k++) {
            int dc = M.destCanton(o, k);
            bool v = false;
            switch (L.tipo) {
                case Lugar::POI: v = (k == L.idx[0]); break;
                case Lugar::CANTON: v = (dc == L.idx[0] && o != L.idx[0]); break;
                case Lugar::ZONA: v = D.cantones[dc].zona == L.nombre && D.cantones[o].zona != L.nombre; break;
                case Lugar::PROVINCIA: v = D.cantones[dc].provincia == L.nombre && D.cantones[o].provincia != L.nombre; break;
                default: break;
            }
            m[static_cast<size_t>(o) * M.ND + k] = v;
        }
    return m;
}

static int cantonDe(const Modelo& M, const Lugar& L, const char* campo) {
    if (L.tipo == Lugar::CANTON) return L.idx[0];
    if (L.tipo == Lugar::POI) return M.D.pois[L.idx[0]].cantonIdx;
    throw ErrorConsulta(string("El parametro '") + campo + "' debe ser un canton o un punto de interes.");
}

static vector<int> aristasDe(const Modelo& M, const Lugar& L) {
    const DataStore& D = M.D;
    vector<int> r;
    switch (L.tipo) {
        case Lugar::POI: r = D.pois[L.idx[0]].aristas; break;
        case Lugar::ARISTAS: r = L.idx; break;
        case Lugar::CANTON:
            for (int e = 0; e < M.NE; e++)
                if (D.aristas[e].a == D.cantones[L.idx[0]].nodo || D.aristas[e].b == D.cantones[L.idx[0]].nodo) r.push_back(e);
            break;
        case Lugar::ZONA:
        case Lugar::PROVINCIA:
            for (int e = 0; e < M.NE; e++)
                for (int nodo : {D.aristas[e].a, D.aristas[e].b}) {
                    int c = D.cantonIdx(D.nodos[nodo]);
                    if (c < 0) continue;
                    const string& v = L.tipo == Lugar::ZONA ? D.cantones[c].zona : D.cantones[c].provincia;
                    if (v == L.nombre) { r.push_back(e); break; }
                }
            break;
        case Lugar::NACIONAL:
            for (int e = 0; e < M.NE; e++) r.push_back(e);
            break;
        default: break;
    }
    sort(r.begin(), r.end());
    r.erase(unique(r.begin(), r.end()), r.end());
    return r;
}

Consulta construirConsulta(const Modelo& M, const Json& req, Json& par) {
    const DataStore& D = M.D;
    Consulta C;
    C.tipo = req["tipo"].str();
    const Modelo* mp = &M;

    int anio = req["anio"].integer(D.anioBase);
    if (anio < D.anioBase - 5 || anio > D.anioBase + D.horizonteMax)
        throw ErrorConsulta("El anio debe estar entre " + to_string(D.anioBase - 5) + " y " +
                            to_string(D.anioBase + D.horizonteMax) + " (horizonte maximo del modelo).");
    int mes = req["mes"].integer(0);
    if (mes < 0 || mes > 12) throw ErrorConsulta("El mes debe estar entre 1 y 12 (0 = todo el anio).");
    int h0 = req.has("hora_inicio") ? req["hora_inicio"].integer() : 0;
    int h1 = req.has("hora_fin") ? req["hora_fin"].integer() : 24;
    if (h0 < 0 || h1 > 24 || h0 >= h1) throw ErrorConsulta("La franja horaria debe cumplir 0 <= hora_inicio < hora_fin <= 24.");
    string td = req["tipo_dia"].str("cualquiera");
    if (td != "cualquiera" && td != "laboral" && td != "finde")
        throw ErrorConsulta("tipo_dia debe ser 'cualquiera', 'laboral' o 'finde'.");
    C.anio = anio;
    C.mes = mes;
    C.tipoDia = td;
    par.set("tipo", C.tipo);
    par.set("anio", anio);
    par.set("mes", mes);
    par.set("hora_inicio", h0);
    par.set("hora_fin", h1);
    par.set("tipo_dia", td);

    auto enV = [h0, h1](int h) { return h >= h0 && h < h1; };
    auto lugarJson = [](const Lugar& L) {
        Json o = Json::object();
        o.set("nombre", L.nombre);
        o.set("tipo", nombreTipoLugar(L.tipo));
        return o;
    };

    if (C.tipo == "flujo_destino") {
        Lugar L = exigirLugar(M, req, "lugar", true);
        if (L.tipo == Lugar::ARISTAS || L.tipo == Lugar::NACIONAL)
            throw ErrorConsulta("Para contar vehiculos que llegan use un punto de interes, canton, zona o provincia. Para rutas use capacidad_acceso.");
        par.set("lugar", lugarJson(L));
        vector<char> mask = mascaraEntrada(M, L);
        C.est = {{"vehiculos_en_franja", "vehiculos", 0.0}, {"vehiculos_hora_pico", "vehiculos/hora", 0.0}};
        C.serieNombre = "llegadas_por_hora";
        C.serieUnidad = "vehiculos/hora";
        C.extraer = [mp, mask, enV](const ResultadoDia& R, vector<double>& v, vector<double>& serie) {
            for (int o = 0; o < mp->NC; o++)
                for (int k = 0; k < mp->ND; k++) {
                    if (!mask[static_cast<size_t>(o) * mp->ND + k]) continue;
                    for (int h = 0; h < 24; h++) serie[h] += R.M[Simulador::idxM(*mp, o, k, h)];
                }
            double mx = 0;
            for (int h = 0; h < 24; h++) {
                if (enV(h)) v[0] += serie[h];
                if (enV(h)) mx = max(mx, serie[h]);
            }
            v[1] = mx;
        };
    } else if (C.tipo == "flujo_od") {
        Lugar Lo = exigirLugar(M, req, "origen", true), Ld = exigirLugar(M, req, "destino", true);
        int co = cantonDe(M, Lo, "origen"), cd = cantonDe(M, Ld, "destino");
        par.set("origen", D.cantones[co].nombre);
        par.set("destino", D.cantones[cd].nombre);
        C.est = {{"vehiculos_en_franja", "vehiculos", 0.0}, {"vehiculos_hora_pico", "vehiculos/hora", 0.0}};
        C.serieNombre = "viajes_por_hora";
        C.serieUnidad = "vehiculos/hora";
        C.extraer = [mp, co, cd, enV](const ResultadoDia& R, vector<double>& v, vector<double>& serie) {
            for (int k = 0; k < mp->ND; k++) {
                if (mp->destCanton(co, k) != cd) continue;
                if (k > mp->NP && k - mp->NP - 1 == co) continue;
                for (int h = 0; h < 24; h++) serie[h] += R.M[Simulador::idxM(*mp, co, k, h)];
            }
            double mx = 0;
            for (int h = 0; h < 24; h++) if (enV(h)) { v[0] += serie[h]; mx = max(mx, serie[h]); }
            v[1] = mx;
        };
    } else if (C.tipo == "capacidad_acceso" || C.tipo == "choques") {
        const bool cap = C.tipo == "capacidad_acceso";
        Lugar L = exigirLugar(M, req, "lugar", cap);
        if (L.tipo == Lugar::NINGUNO) { L.tipo = Lugar::NACIONAL; L.nombre = "Costa Rica"; }
        Lugar Lh = exigirLugar(M, req, "hacia", false);
        vector<int> edges = aristasDe(M, L);
        if (Lh.tipo != Lugar::NINGUNO) {
            vector<int> e2 = aristasDe(M, Lh);
            edges.insert(edges.end(), e2.begin(), e2.end());
            sort(edges.begin(), edges.end());
            edges.erase(unique(edges.begin(), edges.end()), edges.end());
            par.set("hacia", lugarJson(Lh));
        }
        if (edges.empty()) throw ErrorConsulta("El lugar '" + L.nombre + "' no tiene aristas viales modeladas.");
        par.set("lugar", lugarJson(L));
        Json ar = Json::array();
        for (int e : edges) ar.push(D.aristas[e].id);
        par.set("aristas_evaluadas", ar);
        if (cap) {
            C.est = {{"vc_maximo_en_franja", "razon_v/c", 0.02},
                     {"vc_medio_en_franja", "razon_v/c", 0.02},
                     {"p_saturacion_v/c>1", "probabilidad", 0.03},
                     {"p_v/c>0.85", "probabilidad", 0.03},
                     {"vehiculos_dia_por_arista", "vehiculos/dia", 0.0}};
            C.serieNombre = "vc_maximo_por_hora";
            C.serieUnidad = "razon_v/c";
            C.extraer = [mp, edges, enV](const ResultadoDia& R, vector<double>& v, vector<double>& serie) {
                double mx = 0, suma = 0;
                int cnt = 0;
                for (int e : edges) {
                    double flujo = 0;
                    for (int h = 0; h < 24; h++) {
                        double vc = R.vc[static_cast<size_t>(e) * 24 + h];
                        serie[h] = max(serie[h], vc);
                        flujo += R.flow[static_cast<size_t>(e) * 24 + h];
                        if (enV(h)) { mx = max(mx, vc); suma += vc; cnt++; }
                    }
                    v[4] += flujo / edges.size();
                }
                v[0] = mx;
                v[1] = cnt ? suma / cnt : 0;
                v[2] = mx > 1.0 ? 1.0 : 0.0;
                v[3] = mx > 0.85 ? 1.0 : 0.0;
                (void)mp;
            };
        } else {
            C.est = {{"choques_por_dia", "choques/dia", 0.05},
                     {"eventos_congestion_por_dia", "eventos/dia", 0.03},
                     {"p_congestion_por_choque", "probabilidad", 0.03},
                     {"horas_saturadas_por_dia", "arista-horas/dia", 0.1}};
            C.serieNombre = "";
            C.extraer = [mp, edges, enV](const ResultadoDia& R, vector<double>& v, vector<double>&) {
                for (int e : edges) {
                    v[0] += R.crashes[e];
                    v[1] += R.congEv[e];
                    for (int h = 0; h < 24; h++) if (enV(h) && R.vc[static_cast<size_t>(e) * 24 + h] > 1.0) v[3] += 1;
                }
                v[2] = v[1] > 0 ? 1.0 : 0.0;
                (void)mp;
            };
        }
    } else if (C.tipo == "impacto_pois") {
        string tp = normalizar(req["tipo_poi"].str("mall"));
        vector<int> ks;
        for (int k = 0; k < M.NP; k++)
            if (tp == "todos" || tp.empty() || normalizar(D.pois[k].tipo) == tp) ks.push_back(k);
        if (ks.empty()) throw ErrorConsulta("No hay puntos de interes del tipo '" + tp + "'. Tipos: mall, universidad, entretenimiento, aeropuerto, turismo, gobierno, hospital, puerto, empresa, todos.");
        par.set("tipo_poi", tp.empty() ? "todos" : tp);
        Json lista = Json::array();
        for (int k : ks) {
            C.est.push_back({D.pois[k].nombre, "vehiculo-horas de demora/dia", 0.5});
            lista.push(D.pois[k].nombre);
        }
        par.set("pois_evaluados", lista);
        vector<double> t0(M.NE);
        for (int e = 0; e < M.NE; e++) t0[e] = D.aristas[e].km / D.aristas[e].vel * 60.0;
        C.extraer = [mp, ks, t0, enV](const ResultadoDia& R, vector<double>& v, vector<double>&) {
            for (size_t i = 0; i < ks.size(); i++) {
                int k = ks[i];
                double dem = 0;
                for (int o = 0; o < mp->NC; o++) {
                    for (const RutaArista& r : mp->ruta[o][k]) {
                        for (int h = 0; h < 24; h++) {
                            if (!enV(h)) continue;
                            double vc = min(3.0, R.vc[static_cast<size_t>(r.e) * 24 + h]);
                            double demora = t0[r.e] * 0.15 * vc * vc * vc * vc;  // minutos por vehiculo (BPR)
                            dem += r.w * R.M[Simulador::idxM(*mp, o, k, h)] * demora / 60.0;
                        }
                    }
                }
                v[i] = dem;
            }
        };
    } else {
        throw ErrorConsulta("Tipo de consulta desconocido: '" + C.tipo +
                            "'. Validos: flujo_destino, flujo_od, capacidad_acceso, impacto_pois, choques, flota, lugares.");
    }
    return C;
}

}  // namespace tj
