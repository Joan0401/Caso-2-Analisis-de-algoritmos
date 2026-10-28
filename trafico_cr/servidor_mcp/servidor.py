"""Servidor MCP (stdio) del motor de trafico. Cada herramienta normaliza fechas/franjas y llama al motor C++."""
import json
import os
import subprocess
from pathlib import Path

try:
    from mcp.server.mcpserver import MCPServer as Servidor  # mcp 2.x
except ImportError:
    from mcp.server.fastmcp import FastMCP as Servidor  # mcp 1.x

from normalizacion import resolver_parametros

RAIZ = Path(__file__).resolve().parent.parent
MOTOR = os.environ.get("TRAFICO_ENGINE", str(RAIZ / "engine" / "build" / "trafico_engine"))
DATOS = os.environ.get("TRAFICO_DATA", str(RAIZ / "data"))

mcp = Servidor("trafico-cr")


def motor(solicitud: dict) -> dict:
    """Contrato: solicitud JSON por stdin -> respuesta JSON por stdout. Nunca lanza excepciones."""
    try:
        p = subprocess.run([MOTOR, "--datos", DATOS], input=json.dumps(solicitud), capture_output=True,
                           text=True, timeout=float(os.environ.get("TRAFICO_TIMEOUT", "180")))
        r = json.loads(p.stdout)
    except FileNotFoundError:
        return {"ok": False, "error": f"Motor no compilado ({MOTOR}). Ejecute: cd engine && make"}
    except (subprocess.TimeoutExpired, json.JSONDecodeError, OSError) as e:
        return {"ok": False, "error": f"Fallo el motor: {e}"}
    if r.get("ok"):
        pv = r.get("resultado", {}).pop("puntos_de_validacion", None)  # lista larga que el LLM no necesita
        if pv is not None:
            r["resultado"]["n_puntos_de_validacion"] = len(pv)
    return r


def consulta(tipo: str, periodo: str = "", franja: str = "", semilla: int = 0, **extra) -> dict:
    try:
        p, expl = resolver_parametros(periodo=periodo or None, franja=franja or None)
    except ValueError as e:
        return {"ok": False, "error": str(e)}
    req = {"tipo": tipo, **p, **{k: v for k, v in extra.items() if v}}
    if semilla:
        req["semilla"] = semilla
    r = motor(req)
    if r.get("ok"):
        r["interpretacion_de_parametros"] = expl
    return r


# Parametros comunes: periodo ("diciembre de 2030", "finales de 2028", "2027", "en 3 anios"),
# franja ("en la manana", "hora pico de la tarde", "de 6 a 9 am"; vacio = todo el dia),
# semilla (entero para reproducir el resultado; 0 = predeterminada).

@mcp.tool()
def lugares() -> dict:
    """Lista los lugares conocidos: puntos de interes (malls, universidades...), cantones, zonas, provincias y rutas."""
    return motor({"tipo": "lugares"})


@mcp.tool()
def vehiculos_hacia(lugar: str, periodo: str, franja: str = "", semilla: int = 0) -> dict:
    """Vehiculos que llegan a un lugar (ej. 'La Sabana', 'Mall San Pedro', un canton) en un periodo y franja. Devuelve media, IC y perfil por hora."""
    return consulta("flujo_destino", periodo, franja, semilla, lugar=lugar)


@mcp.tool()
def vehiculos_entre(origen: str, destino: str, periodo: str, franja: str = "", semilla: int = 0) -> dict:
    """Vehiculos que viajan de un canton a otro (ej. Cartago a San Jose en la manana)."""
    return consulta("flujo_od", periodo, franja, semilla, origen=origen, destino=destino)


@mcp.tool()
def capacidad_via(lugar: str, periodo: str, hacia: str = "", franja: str = "", semilla: int = 0) -> dict:
    """Razon volumen/capacidad (v/c) y probabilidad de saturacion de las vias de acceso a un lugar (opcional 'hacia' otro lugar). v/c>1 = congestion."""
    return consulta("capacidad_acceso", periodo, franja, semilla, lugar=lugar, hacia=hacia)


@mcp.tool()
def impacto_en_congestion(periodo: str, tipo_poi: str = "mall", franja: str = "", semilla: int = 0) -> dict:
    """Ranking de puntos de interes (por defecto malls) segun su impacto en la congestion."""
    return consulta("impacto_pois", periodo, franja, semilla, tipo_poi=tipo_poi)


@mcp.tool()
def flota(periodo: str, nivel: str = "nacional", nombre: str = "", semilla: int = 0) -> dict:
    """Proyeccion de la flota vehicular. nivel: nacional, provincia, zona o canton (con 'nombre')."""
    try:
        p, expl = resolver_parametros(periodo=periodo)
    except ValueError as e:
        return {"ok": False, "error": str(e)}
    r = motor({"tipo": "flota", "anio": p["anio"], "nivel": nivel, "nombre": nombre, "semilla": semilla or 42})
    if r.get("ok"):
        r["interpretacion_de_parametros"] = expl
    return r


@mcp.tool()
def choques(periodo: str, lugar: str = "", franja: str = "", semilla: int = 0) -> dict:
    """Choques esperados y congestion asociada en una ruta, canton o zona (vacio = toda la red)."""
    return consulta("choques", periodo, franja, semilla, lugar=lugar)


if __name__ == "__main__":
    mcp.run()
