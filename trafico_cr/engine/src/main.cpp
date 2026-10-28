// main.cpp - Ejecutable del motor. Lee una solicitud JSON por stdin y escribe la respuesta JSON por stdout.
//
//   trafico_engine --datos ../data < solicitud.json
//   trafico_engine --datos ../data --solicitud solicitud.json [--indentar]
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "datastore.hpp"
#include "interfaz.hpp"

using namespace std;
using namespace tj;

int main(int argc, char** argv) {
    string datos = "../data", archivo;
    bool indentar = false;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--datos") && i + 1 < argc) datos = argv[++i];
        else if (!strcmp(argv[i], "--solicitud") && i + 1 < argc) archivo = argv[++i];
        else if (!strcmp(argv[i], "--indentar")) indentar = true;
        else if (!strcmp(argv[i], "--ayuda")) {
            puts("Uso: trafico_engine [--datos DIR] [--solicitud ARCHIVO.json] [--indentar]\nLee la solicitud JSON de stdin si no se da archivo.");
            return 0;
        }
    }
    string txt;
    if (!archivo.empty()) {
        ifstream f(archivo);
        if (!f) { puts("{\"ok\":false,\"error\":\"No se pudo abrir el archivo de solicitud\"}"); return 1; }
        stringstream ss;
        ss << f.rdbuf();
        txt = ss.str();
    } else {
        stringstream ss;
        ss << cin.rdbuf();
        txt = ss.str();
    }
    Json out;
    try {
        DataStore D = DataStore::cargar(datos);
        Json req = Json::parse(txt);
        out = procesarSolicitud(D, req);
    } catch (const exception& e) {
        out = Json::object();
        out.set("ok", false);
        out.set("error", string("No se pudo procesar la solicitud: ") + e.what());
    }
    string s = out.dump(indentar ? 2 : -1);
    fwrite(s.data(), 1, s.size(), stdout);
    fputc('\n', stdout);
    return out["ok"].boolean() ? 0 : 2;
}
