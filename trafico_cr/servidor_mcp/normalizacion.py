"""Normalizacion de parametros: convierte fechas y franjas relativas del lenguaje natural en valores explicitos.

Ejemplos:
    "finales de 2028"          -> anio 2028, mes 12
    "diciembre de 2030"        -> anio 2030, mes 12
    "en 3 anios"               -> anio (hoy + 3)
    "en la manana"             -> franja 5 a 12
    "hora pico de la tarde"    -> franja 16 a 19
    "de 6 a 9 am"              -> franja 6 a 9
"""
from __future__ import annotations

import re
import unicodedata
from datetime import date, timedelta
from typing import Optional

MESES = {
    "enero": 1, "febrero": 2, "marzo": 3, "abril": 4, "mayo": 5, "junio": 6, "julio": 7, "agosto": 8,
    "septiembre": 9, "setiembre": 9, "octubre": 10, "noviembre": 11, "diciembre": 12,
}


def normalizar_texto(texto: str) -> str:
    """Minusculas, sin tildes y con espacios simples."""
    t = unicodedata.normalize("NFD", texto.lower())
    t = "".join(c for c in t if unicodedata.category(c) != "Mn")
    return re.sub(r"\s+", " ", t).strip()


def _sumar_meses(d: date, n: int) -> date:
    m = d.month - 1 + n
    return date(d.year + m // 12, m % 12 + 1, 1)


def interpretar_periodo(texto: str, hoy: Optional[date] = None) -> dict:
    """Devuelve {'anio': int|None, 'mes': int|None, 'explicacion': str} a partir de un texto de periodo."""
    hoy = hoy or date.today()
    t = normalizar_texto(texto or "")
    anio: Optional[int] = None
    mes: Optional[int] = None
    nota = []

    m = re.search(r"\b(20\d{2})\b", t)
    if m:
        anio = int(m.group(1))
    for nombre, n in MESES.items():
        if re.search(rf"\b{nombre}\b", t):
            mes = n
            break

    if anio is None:
        if re.search(r"\b(este|presente|actual) ano\b", t):
            anio = hoy.year
        elif re.search(r"\b(proximo ano|ano que viene|ano entrante|siguiente ano)\b", t):
            anio = hoy.year + 1
        elif re.search(r"\bano pasado\b", t):
            anio = hoy.year - 1
        else:
            m = re.search(r"\b(?:en|dentro de|de aqui a)\s+(\d{1,2})\s+anos?\b", t)
            if m:
                anio = hoy.year + int(m.group(1))
                nota.append(f"'en {m.group(1)} anios' se tomo como {anio} (hoy es {hoy.isoformat()})")
        if anio is None:
            m = re.search(r"\b(?:en|dentro de)\s+(\d{1,3})\s+mes(?:es)?\b", t)
            if m:
                d = _sumar_meses(hoy, int(m.group(1)))
                anio, mes = d.year, d.month
            elif re.search(r"\b(este mes|mes actual)\b", t):
                anio, mes = hoy.year, hoy.month
            elif re.search(r"\b(proximo mes|mes que viene|mes entrante)\b", t):
                d = _sumar_meses(hoy, 1)
                anio, mes = d.year, d.month
            elif re.search(r"\bmes pasado\b", t):
                d = _sumar_meses(hoy, -1)
                anio, mes = d.year, d.month
            elif re.search(r"\b(proxima semana|semana que viene)\b", t):
                d = hoy + timedelta(days=7)
                anio, mes = d.year, d.month
            elif re.search(r"\b(esta semana|hoy)\b", t):
                anio, mes = hoy.year, hoy.month
            elif "manana" in t and not re.search(r"(la|esta) manana|manana (por|en|a) la", t):
                d = hoy + timedelta(days=1)
                anio, mes = d.year, d.month

    if mes is None and anio is not None:
        if re.search(r"\b(finales|final|fines|fin|cierre)\b", t):
            mes = 12
            nota.append("'finales de anio' se tomo como diciembre")
        elif re.search(r"\b(principios|inicio|inicios|comienzos|comienzo)\b", t):
            mes = 1
            nota.append("'principios de anio' se tomo como enero")
        elif re.search(r"\b(mediados|mitad)\b", t):
            mes = 6
            nota.append("'mediados de anio' se tomo como junio")

    if anio is None and mes is not None:
        anio = hoy.year if mes >= hoy.month else hoy.year + 1
        nota.append(f"no se indico el anio; se asumio {anio}")

    expl = f"texto '{texto}' -> anio={anio}, mes={mes if mes is not None else 'todo el anio'}"
    if nota:
        expl += " (" + "; ".join(nota) + ")"
    return {"anio": anio, "mes": mes, "explicacion": expl}


def _a_24h(h: int, ampm: Optional[str]) -> int:
    if ampm:
        a = ampm.replace(" ", "").replace(".", "")
        if a == "pm" and h < 12:
            return h + 12
        if a == "am" and h == 12:
            return 0
    return h


def interpretar_franja(texto: str) -> dict:
    """Devuelve {'hora_inicio': int|None, 'hora_fin': int|None, 'explicacion': str}."""
    t = normalizar_texto(texto or "")
    if not t:
        return {"hora_inicio": None, "hora_fin": None, "explicacion": "sin franja: todo el dia"}

    m = re.search(
        r"(\d{1,2})(?::\d{2})?\s*(a\.? ?m\.?|p\.? ?m\.?)?\s*(?:a|y|-|hasta|al)\s*(?:las\s*)?(\d{1,2})(?::\d{2})?\s*(a\.? ?m\.?|p\.? ?m\.?)?",
        t,
    )
    if m and (re.search(r"\b(de|entre|desde)\b", t) or ":" in t or "-" in t or m.group(2) or m.group(4)):
        h0, h1 = int(m.group(1)), int(m.group(3))
        ap0, ap1 = m.group(2), m.group(4)
        ap_ini = ap0 or (ap1 if h0 <= h1 else None)  # 'de 6 a 9 am': ambas horas son am
        h0, h1 = _a_24h(h0, ap_ini), _a_24h(h1, ap1)
        if h1 <= h0 and not ap1:
            h1 += 12
        if 0 <= h0 < h1 <= 24:
            return {"hora_inicio": h0, "hora_fin": h1, "explicacion": f"franja explicita {h0}:00 a {h1}:00"}

    if re.search(r"\b(todo el dia|dia completo|24 horas)\b", t):
        return {"hora_inicio": 0, "hora_fin": 24, "explicacion": "todo el dia"}
    pico = re.search(r"\bhoras? pico\b|\bhora punta\b|\btaco\b|\bpresa\b", t)
    manana = re.search(r"(la|esta|de) manana|\bmanana\b|\bam\b|a\.m\.", t)
    tarde = re.search(r"\b(tarde|pm|p\.m\.)\b", t)
    noche = re.search(r"\bnoche\b", t)
    if pico:
        if manana and not tarde:
            return {"hora_inicio": 6, "hora_fin": 9, "explicacion": "hora pico de la manana (6:00 a 9:00)"}
        if tarde or noche:
            return {"hora_inicio": 16, "hora_fin": 19, "explicacion": "hora pico de la tarde (16:00 a 19:00)"}
        return {"hora_inicio": 6, "hora_fin": 19,
                "explicacion": "hora pico sin precisar: se uso 6:00 a 19:00 (incluye ambas horas pico)"}
    if re.search(r"\bmadrugada\b", t):
        return {"hora_inicio": 0, "hora_fin": 5, "explicacion": "madrugada (0:00 a 5:00)"}
    if re.search(r"\bmediodia\b", t):
        return {"hora_inicio": 11, "hora_fin": 14, "explicacion": "mediodia (11:00 a 14:00)"}
    if manana:
        return {"hora_inicio": 5, "hora_fin": 12, "explicacion": "manana (5:00 a 12:00)"}
    if noche:
        return {"hora_inicio": 19, "hora_fin": 24, "explicacion": "noche (19:00 a 24:00)"}
    if tarde:
        return {"hora_inicio": 12, "hora_fin": 19, "explicacion": "tarde (12:00 a 19:00)"}
    return {"hora_inicio": None, "hora_fin": None, "explicacion": f"no se reconocio la franja '{texto}': todo el dia"}


def interpretar_tipo_dia(texto: str) -> Optional[str]:
    t = normalizar_texto(texto or "")
    if re.search(r"\b(entre semana|dias habiles|dia habil|laboral(?:es)?|lunes a viernes|dias de semana)\b", t):
        return "laboral"
    if re.search(r"\b(fin de semana|fines de semana|sabados?|domingos?|finde)\b", t):
        return "finde"
    return None


def resolver_parametros(
    anio: Optional[int] = None,
    mes: Optional[int] = None,
    periodo: Optional[str] = None,
    hora_inicio: Optional[int] = None,
    hora_fin: Optional[int] = None,
    franja: Optional[str] = None,
    tipo_dia: Optional[str] = None,
    hoy: Optional[date] = None,
) -> tuple[dict, list[str]]:
    """Combina valores explicitos y textos libres. Los numeros explicitos tienen prioridad.

    Devuelve (parametros, explicaciones). Lanza ValueError con un mensaje util si algo es invalido.
    """
    expl: list[str] = []
    if periodo:
        p = interpretar_periodo(periodo, hoy)
        expl.append(p["explicacion"])
        if anio is None:
            anio = p["anio"]
        if mes is None:
            mes = p["mes"]
    if franja:
        f = interpretar_franja(franja)
        expl.append(f["explicacion"])
        if hora_inicio is None and hora_fin is None:
            hora_inicio, hora_fin = f["hora_inicio"], f["hora_fin"]
    if tipo_dia is None and (periodo or franja):
        tipo_dia = interpretar_tipo_dia(f"{periodo or ''} {franja or ''}")
        if tipo_dia:
            expl.append(f"tipo de dia '{tipo_dia}' deducido del texto")
    if anio is None:
        raise ValueError("Falta el anio (o un periodo como 'diciembre de 2030', 'finales de 2028', 'en 3 anios').")
    if mes is not None and not 0 <= mes <= 12:
        raise ValueError(f"El mes debe estar entre 1 y 12 (recibido {mes}).")
    h0 = 0 if hora_inicio is None else hora_inicio
    h1 = 24 if hora_fin is None else hora_fin
    if not (0 <= h0 < h1 <= 24):
        raise ValueError(f"Franja horaria invalida ({h0} a {h1}): use 0 <= hora_inicio < hora_fin <= 24.")
    if tipo_dia not in (None, "cualquiera", "laboral", "finde"):
        raise ValueError("tipo_dia debe ser 'cualquiera', 'laboral' o 'finde'.")
    out = {"anio": int(anio), "mes": int(mes or 0), "hora_inicio": int(h0), "hora_fin": int(h1),
           "tipo_dia": tipo_dia or "cualquiera"}
    return out, expl
