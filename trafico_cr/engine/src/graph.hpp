// graph.hpp - Grafo reducido de la red vial (nodos = cantones, aristas = corredores).
#pragma once
#include <limits>
#include <vector>

#include "datastore.hpp"

using namespace std;

namespace tj {

class Grafo {
public:
    explicit Grafo(const DataStore& D) : D_(D), n_(static_cast<int>(D.nodos.size())), adj_(n_) {
        for (size_t e = 0; e < D.aristas.size(); e++) {
            const Arista& a = D.aristas[e];
            if (a.a == a.b) continue;  // lazos (circunvalacion) no se usan para enrutar
            adj_[a.a].push_back({a.b, static_cast<int>(e)});
            adj_[a.b].push_back({a.a, static_cast<int>(e)});
        }
    }

    int nodos() const { return n_; }

    // Tiempo de viaje libre de una arista (minutos).
    double minutos(int e) const { return D_.aristas[e].km / D_.aristas[e].vel * 60.0; }

    // Dijkstra desde src por tiempo libre. dist en minutos; previo[v] = {arista, nodoAnterior}.
    void caminosMinimos(int src, vector<double>& dist, vector<int>& prevArista, vector<int>& prevNodo) const {
        const double INF = numeric_limits<double>::infinity();
        dist.assign(n_, INF);
        prevArista.assign(n_, -1);
        prevNodo.assign(n_, -1);
        vector<char> hecho(n_, 0);
        dist[src] = 0;
        for (int it = 0; it < n_; it++) {
            int u = -1;
            for (int v = 0; v < n_; v++) if (!hecho[v] && (u < 0 || dist[v] < dist[u])) u = v;
            if (u < 0 || dist[u] == INF) break;
            hecho[u] = 1;
            for (auto& pr : adj_[u]) {
                double nd = dist[u] + minutos(pr.second);
                if (nd < dist[pr.first]) {
                    dist[pr.first] = nd;
                    prevArista[pr.first] = pr.second;
                    prevNodo[pr.first] = u;
                }
            }
        }
    }

    // Aristas del camino minimo src -> dst (vacio si src == dst o no hay camino).
    vector<int> camino(int src, int dst) const {
        vector<double> d;
        vector<int> pa, pn;
        caminosMinimos(src, d, pa, pn);
        vector<int> out;
        if (d[dst] == numeric_limits<double>::infinity()) return out;
        for (int v = dst; v != src && v >= 0; v = pn[v]) out.push_back(pa[v]);
        return vector<int>(out.rbegin(), out.rend());
    }

private:
    const DataStore& D_;
    int n_;
    vector<vector<pair<int, int>>> adj_;
};

}  // namespace tj
