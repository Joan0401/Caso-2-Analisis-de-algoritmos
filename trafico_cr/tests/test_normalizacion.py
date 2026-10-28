"""Pruebas de la normalizacion de fechas, franjas y parametros."""
import os
import sys
from datetime import date

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "servidor_mcp"))

import pytest

from normalizacion import (interpretar_franja, interpretar_periodo, interpretar_tipo_dia,
                           resolver_parametros)

HOY = date(2026, 10, 27)


@pytest.mark.parametrize("texto,anio,mes", [
    ("diciembre de 2030", 2030, 12),
    ("finales de 2028", 2028, 12),
    ("a finales del 2028", 2028, 12),
    ("principios de 2029", 2029, 1),
    ("mediados de 2027", 2027, 6),
    ("2027", 2027, None),
    ("el proximo año", 2027, None),
    ("este año", 2026, None),
    ("dentro de 3 años", 2029, None),
    ("en 6 meses", 2027, 4),
    ("el próximo mes", 2026, 11),
    ("este mes", 2026, 10),
    ("hoy", 2026, 10),
    ("marzo", 2027, 3),
])
def test_periodo(texto, anio, mes):
    r = interpretar_periodo(texto, HOY)
    assert r["anio"] == anio
    assert r["mes"] == mes


@pytest.mark.parametrize("texto,h0,h1", [
    ("en la mañana", 5, 12),
    ("esta mañana", 5, 12),
    ("por la tarde", 12, 19),
    ("en la noche", 19, 24),
    ("de madrugada", 0, 5),
    ("hora pico de la mañana", 6, 9),
    ("hora pico de la tarde", 16, 19),
    ("de 6 a 9 am", 6, 9),
    ("entre las 7 y las 10", 7, 10),
    ("de 5pm a 8pm", 17, 20),
    ("18:00-20:00", 18, 20),
    ("todo el día", 0, 24),
])
def test_franja(texto, h0, h1):
    r = interpretar_franja(texto)
    assert (r["hora_inicio"], r["hora_fin"]) == (h0, h1)


def test_tipo_dia():
    assert interpretar_tipo_dia("entre semana en la mañana") == "laboral"
    assert interpretar_tipo_dia("los fines de semana") == "finde"
    assert interpretar_tipo_dia("cualquier dia") is None


def test_resolver_prioriza_numeros_explicitos():
    p, _ = resolver_parametros(anio=2031, periodo="diciembre de 2030", hora_inicio=7, hora_fin=9, franja="en la tarde", hoy=HOY)
    assert p["anio"] == 2031 and p["mes"] == 12
    assert (p["hora_inicio"], p["hora_fin"]) == (7, 9)


def test_resolver_deduce_tipo_dia_y_franja():
    p, expl = resolver_parametros(periodo="finales de 2028", franja="entre semana en la mañana", hoy=HOY)
    assert p == {"anio": 2028, "mes": 12, "hora_inicio": 5, "hora_fin": 12, "tipo_dia": "laboral"}
    assert expl


def test_resolver_errores_utiles():
    with pytest.raises(ValueError, match="anio"):
        resolver_parametros()
    with pytest.raises(ValueError, match="Franja"):
        resolver_parametros(anio=2027, hora_inicio=10, hora_fin=8)
    with pytest.raises(ValueError, match="mes"):
        resolver_parametros(anio=2027, mes=13)
