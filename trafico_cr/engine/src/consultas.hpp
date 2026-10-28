// consultas.hpp - Resolucion de lugares y construccion de las consultas (estimandos) del motor.
#pragma once
#include <stdexcept>
#include <string>
#include <vector>

#include "json.hpp"
#include "montecarlo.hpp"

using namespace std;

namespace tj {

struct ErrorConsulta : runtime_error {
    vector<string> sugerencias;
    ErrorConsulta(const string& m, vector<string> s = {}) : runtime_error(m), sugerencias(move(s)) {}
};

struct Lugar {
    enum Tipo { NINGUNO, POI, CANTON, ARISTAS, ZONA, PROVINCIA, NACIONAL } tipo = NINGUNO;
    vector<int> idx;  // indice del POI / canton, o de las aristas
    string nombre;    // nombre canonico (para ZONA/PROVINCIA el nombre de la zona/provincia)
};

Lugar resolverLugar(const Modelo& M, const string& texto);
vector<string> sugerirLugares(const Modelo& M, const string& texto);
string nombreTipoLugar(Lugar::Tipo t);

// Catalogo de lugares validos (para la herramienta listar_lugares).
Json catalogoLugares(const Modelo& M);

// Construye la consulta a partir de la solicitud JSON. Lanza ErrorConsulta con mensajes utiles.
// "parametros" recibe los parametros normalizados que se devuelven al cliente.
Consulta construirConsulta(const Modelo& M, const Json& req, Json& parametros);

}  // namespace tj
