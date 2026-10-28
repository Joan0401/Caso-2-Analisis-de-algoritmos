// datastore.hpp - Carga en memoria de la fuente de datos (JSON) al iniciar el motor.
// Es la unica lectura de disco: nada calculado se guarda ni se consulta despues.
#pragma once
#include <array>
#include <map>
#include <string>
#include <vector>

#include "json.hpp"

using namespace std;

namespace tj {

// Minusculas, sin tildes, solo letras/digitos/espacios (para comparar nombres de lugares).
string normalizar(const string& in);

struct Canton {
    string nombre, provincia, zona;
    double vehiculos = 0;    // flota registrada (vehiculos)
    double circulantes = 0;  // vehiculos que circulan (vehiculos)
    double poblacion = 0;    // habitantes
    int nodo = -1;           // indice del nodo en la red vial
};

struct Provincia {
    string nombre;
    double participacion = 0;  // fraccion de la flota nacional
};

struct Poi {
    string id, nombre, tipo, canton;
    double lat = 0, lon = 0, peso = 0;
    int cantonIdx = -1, nodo = -1;
    vector<int> aristas;  // indices de aristas de acceso
    vector<int> picos;    // horas pico (0-23)
    vector<string> alias;
};

struct Arista {
    string id, ruta, tipo;  // tipo: autopista | ruta_nacional
    int a = -1, b = -1;
    double km = 0;       // km
    int carriles = 1;    // carriles por sentido
    double cap = 0;      // capacidad (vehiculos/hora/sentido)
    double tpd = 0;      // transito promedio diario semilla (vehiculos/dia)
    double vel = 60;     // velocidad libre (km/h)
};

struct Feriado { string nombre; int mes = 1, dia = 1; double factor = 1; };

struct Evento {
    string id, nombre, tipo, poi;
    double p = 0, mult = 1, radio = 0;
    int dur = 1, ini = 0;
    vector<int> meses, dias;
    vector<array<int, 2>> fijas;
};

struct Infra { string id, nombre; int arista = -1; int anio = 0; double fcap = 1, fdem = 1; };

struct ValPunto { string id, metrica, objetivo; double valor = 0, peso = 1; };

struct DataStore {
    // flota
    double flotaTotal = 0, marchamos = 0, factorCirc = 1;
    vector<Provincia> provincias;
    vector<Canton> cantones;
    // zonas
    vector<string> zonas;
    // crecimiento
    int anioBase = 2025, horizonteMax = 15;
    double inscripciones = 0, tasaBajo = 0.02, tasaBase = 0.035, tasaAlto = 0.05;
    map<string, double> multZona;
    array<double, 4> composicion{0.574, 0.25, 0.12, 0.056};  // auto, moto, carga, bus
    double teleActual = 0.1, teleAumento = 0.005, poiCrec = 0.02;
    vector<Infra> infra;
    // riesgo
    double accAnuales = 47000;
    array<double, 7> sevDia{};
    array<double, 4> riesgoTipo{1, 4, 1.5, 0.8};
    array<double, 5> riesgoFranja{1.8, 1.0, 0.8, 1.3, 1.5};  // madrugada, pico_am, valle, pico_pm, noche
    double lluviaRiesgo = 1.4, lluviaVel = 0.85;
    double pCong[3] = {0.55, 0.35, 0.15};  // autopista, ruta_nacional, calle
    double congMediana = 50, congSigma = 0.6, perdidaCap = 0.5;
    // perfiles
    array<double, 24> perfil[3];
    array<double, 7> volDia{};
    array<double, 12> volMes{};
    double factorQuincena = 1, viajesVehDia = 2.4, shareTrabajo = 0.35;
    // calendario
    vector<Feriado> feriados;
    array<double, 12> pLluvia{};
    vector<Evento> eventos;
    // red y puntos
    double capCarril = 1700;
    vector<string> nodos;
    vector<Arista> aristas;
    vector<Poi> pois;
    // validacion
    double tolerancia = 0.05;
    vector<ValPunto> validacion;

    static DataStore cargar(const string& dir);

    int nodoIdx(const string& n) const;
    int cantonIdx(const string& nombre) const;
    int aristaIdx(const string& id) const;
    int poiIdx(const string& id) const;
    // Multiplicador de crecimiento de una zona (1.0 si no existe).
    double multiplicadorZona(const string& zona) const;
};

}  // namespace tj
