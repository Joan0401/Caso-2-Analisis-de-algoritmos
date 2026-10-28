// rng.hpp - Generador pseudoaleatorio reproducible (xoshiro256**) y muestreadores.
// Misma semilla + mismos parametros => misma secuencia => mismo resultado.
#pragma once
#include <cmath>
#include <cstdint>

using namespace std;

namespace tj {

inline uint64_t splitmix64(uint64_t& x) {
    uint64_t z = (x += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

// Mezcla una semilla con un identificador de flujo para obtener flujos independientes.
inline uint64_t mezclar(uint64_t semilla, uint64_t flujo) {
    uint64_t x = semilla ^ (flujo * 0xD6E8FEB86659FD93ULL);
    return splitmix64(x);
}

class Rng {
public:
    explicit Rng(uint64_t semilla = 1) { reiniciar(semilla); }

    void reiniciar(uint64_t semilla) {
        uint64_t x = semilla;
        for (int i = 0; i < 4; i++) s_[i] = splitmix64(x);
    }

    uint64_t siguiente() {
        const uint64_t res = rotl(s_[1] * 5, 7) * 9;
        const uint64_t t = s_[1] << 17;
        s_[2] ^= s_[0];
        s_[3] ^= s_[1];
        s_[1] ^= s_[2];
        s_[0] ^= s_[3];
        s_[2] ^= t;
        s_[3] = rotl(s_[3], 45);
        return res;
    }

    double u01() { return static_cast<double>(siguiente() >> 11) * (1.0 / 9007199254740992.0); }
    double uniforme(double a, double b) { return a + (b - a) * u01(); }
    int entero(int n) { return n <= 1 ? 0 : static_cast<int>(u01() * n) % n; }
    bool bernoulli(double p) { return u01() < p; }

    double normal() {
        double u1 = u01();
        if (u1 < 1e-300) u1 = 1e-300;
        double u2 = u01();
        return sqrt(-2.0 * log(u1)) * cos(6.283185307179586 * u2);
    }
    double lognormal(double mu, double sigma) { return exp(mu + sigma * normal()); }

    // Triangular(min, moda, max)
    double triangular(double lo, double moda, double hi) {
        double u = u01();
        double f = (moda - lo) / (hi - lo);
        if (u < f) return lo + sqrt(u * (hi - lo) * (moda - lo));
        return hi - sqrt((1 - u) * (hi - lo) * (hi - moda));
    }

    double poisson(double lambda) {
        if (lambda <= 0) return 0;
        if (lambda < 30) {
            double L = exp(-lambda), p = 1;
            int k = 0;
            do { k++; p *= u01(); } while (p > L);
            return k - 1;
        }
        double v = round(lambda + sqrt(lambda) * normal());
        return v < 0 ? 0 : v;
    }

    // Binomial(n, p): exacta para n pequeno, aproximada (normal/Poisson) para n grande.
    double binomial(double n, double p) {
        if (n <= 0 || p <= 0) return 0;
        if (p >= 1) return n;
        if (n <= 40) {
            int c = 0;
            for (int i = 0; i < static_cast<int>(n); i++) c += u01() < p;
            return c;
        }
        double var = n * p * (1 - p);
        if (var >= 25) {
            double v = round(n * p + sqrt(var) * normal());
            return v < 0 ? 0 : (v > n ? n : v);
        }
        if (p < 0.5) { double v = poisson(n * p); return v > n ? n : v; }
        double v = poisson(n * (1 - p));
        return v > n ? 0 : n - v;
    }

    int categorica(const double* w, int n, double total) {
        double r = u01() * total, acc = 0;
        for (int i = 0; i < n; i++) {
            acc += w[i];
            if (r < acc) return i;
        }
        return n - 1;
    }

private:
    static uint64_t rotl(uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }
    uint64_t s_[4];
};

// Cuantil de la normal estandar (Acklam). Para intervalos de confianza.
inline double cuantilNormal(double p) {
    static const double a[] = {-3.969683028665376e+01, 2.209460984245205e+02, -2.759285104469687e+02,
                               1.383577518672690e+02, -3.066479806614716e+01, 2.506628277459239e+00};
    static const double b[] = {-5.447609879822406e+01, 1.615858368580409e+02, -1.556989798598866e+02,
                               6.680131188771972e+01, -1.328068155288572e+01};
    static const double c[] = {-7.784894002430293e-03, -3.223964580411365e-01, -2.400758277161838e+00,
                               -2.549732539343734e+00, 4.374664141464968e+00, 2.938163982698783e+00};
    static const double d[] = {7.784695709041462e-03, 3.224671290700398e-01, 2.445134137142996e+00,
                               3.754408661907416e+00};
    if (p <= 0) return -8;
    if (p >= 1) return 8;
    double q, r;
    if (p < 0.02425) {
        q = sqrt(-2 * log(p));
        return (((((c[0] * q + c[1]) * q + c[2]) * q + c[3]) * q + c[4]) * q + c[5]) /
               ((((d[0] * q + d[1]) * q + d[2]) * q + d[3]) * q + 1);
    }
    if (p > 1 - 0.02425) {
        q = sqrt(-2 * log(1 - p));
        return -(((((c[0] * q + c[1]) * q + c[2]) * q + c[3]) * q + c[4]) * q + c[5]) /
               ((((d[0] * q + d[1]) * q + d[2]) * q + d[3]) * q + 1);
    }
    q = p - 0.5;
    r = q * q;
    return (((((a[0] * r + a[1]) * r + a[2]) * r + a[3]) * r + a[4]) * r + a[5]) * q /
           (((((b[0] * r + b[1]) * r + b[2]) * r + b[3]) * r + b[4]) * r + 1);
}

// Cuantil de la t de Student (expansion de Cornish-Fisher; suficiente para IC con pocas replicas).
inline double cuantilT(double p, int df) {
    double z = cuantilNormal(p), z3 = z * z * z, z5 = z3 * z * z, z7 = z5 * z * z;
    double g1 = (z3 + z) / 4.0, g2 = (5 * z5 + 16 * z3 + 3 * z) / 96.0, g3 = (3 * z7 + 19 * z5 + 17 * z3 - 15 * z) / 384.0;
    return z + g1 / df + g2 / (double(df) * df) + g3 / (double(df) * df * df);
}

}  // namespace tj
