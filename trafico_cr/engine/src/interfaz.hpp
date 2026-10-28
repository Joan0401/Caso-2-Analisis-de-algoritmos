// interfaz.hpp - Contrato JSON entre Python (servidor MCP) y el motor C++.
//
// Solicitud (stdin):  {"tipo": "...", "semilla": 42, "anio": 2027, ...}  (ver README.md)
// Respuesta (stdout): {"ok": true, "tipo": ..., "parametros": {...}, "resultado": {...},
//                      "supuestos": [...], "metricas": {...}}
//                  o  {"ok": false, "error": "...", "sugerencias": [...]}
#pragma once
#include "datastore.hpp"
#include "json.hpp"

using namespace std;

namespace tj {

Json procesarSolicitud(const DataStore& D, const Json& req);

}  // namespace tj
