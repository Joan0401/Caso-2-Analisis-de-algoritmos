"""Integracion: herramientas MCP -> motor C++ real, y chat con un LLM falso. Requiere el motor compilado (o TRAFICO_ENGINE)."""
import asyncio
import json
import sys
import threading
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path

import pytest

RAIZ = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(RAIZ / "servidor_mcp"))
import servidor  # noqa: E402

if not Path(servidor.MOTOR).exists():
    pytest.skip("motor C++ no compilado", allow_module_level=True)


def test_lugar_desconocido():
    r = servidor.vehiculos_hacia("Marte", "2027")
    assert r["ok"] is False and r["sugerencias"]


def test_periodo_invalido():
    assert servidor.vehiculos_hacia("La Sabana", "")["ok"] is False


def test_semilla_reproducible():
    a, b, c = (servidor.flota("2030", semilla=s) for s in (7, 7, 8))
    assert a["ok"] and a["resultado"] == b["resultado"] != c["resultado"]


def test_metricas_y_supuestos():
    r = servidor.vehiculos_hacia("La Sabana", "2027", semilla=1)
    assert r["ok"] and r["supuestos"] and r["metricas"]["semilla"] == 1


def test_ranking_y_capacidad():
    assert servidor.impacto_en_congestion("finales de 2028", semilla=3)["ok"]
    assert servidor.capacidad_via("Rotonda de la Hispanidad", "diciembre de 2030", hacia="Mall San Pedro")["ok"]


class Falso(BaseHTTPRequestHandler):
    visto = []

    def do_POST(self):
        cuerpo = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        Falso.visto.append(cuerpo)
        if any(m["role"] == "tool" for m in cuerpo["messages"]):
            msg = {"role": "assistant", "content": "respuesta final"}
        else:
            msg = {"role": "assistant", "content": None, "tool_calls": [{"id": "c1", "type": "function", "function": {
                "name": "flota", "arguments": json.dumps({"periodo": "2030", "semilla": 5})}}]}
        out = json.dumps({"choices": [{"message": msg}]}).encode()
        self.send_response(200)
        self.send_header("Content-Length", str(len(out)))
        self.end_headers()
        self.wfile.write(out)

    def log_message(self, *a):
        pass


def test_chat_con_llm_falso(monkeypatch, capsys):
    srv = HTTPServer(("127.0.0.1", 0), Falso)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    monkeypatch.setenv("LLM_BASE_URL", f"http://127.0.0.1:{srv.server_port}/v1")
    monkeypatch.setenv("LLM_MODEL", "falso")
    monkeypatch.setattr(sys, "argv", ["chat.py", "-p", "cuantos carros en 2030?"])
    import chat
    asyncio.run(chat.main())
    srv.shutdown()
    assert "respuesta final" in capsys.readouterr().out
    assert "nunca inventes" in Falso.visto[0]["messages"][0]["content"].lower()
    tool = [m for m in Falso.visto[1]["messages"] if m["role"] == "tool"][0]
    assert json.loads(tool["content"])["ok"] is True


def test_herramienta_directa(monkeypatch, capsys):
    monkeypatch.setattr(sys, "argv", ["chat.py", "-t", "lugares"])
    import chat
    asyncio.run(chat.main())
    assert json.loads(capsys.readouterr().out)["ok"] is True
