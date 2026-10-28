"""Chat de consola: LLM (API compatible con OpenAI) + herramientas del servidor MCP.

  LLM_PRESET=groq|openrouter|gemini|ollama  (o LLM_BASE_URL + LLM_MODEL)   LLM_API_KEY=...
  python chat.py                      chat interactivo
  python chat.py -p "pregunta"        una pregunta
  python chat.py -t flota '{"periodo":"2030"}'    llama una herramienta sin LLM
"""
import asyncio
import json
import os
import sys
import urllib.error
import urllib.request
from datetime import date
from pathlib import Path

from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client

PRESETS = {"gemini": ("https://generativelanguage.googleapis.com/v1beta/openai", "gemini-2.0-flash")}

SISTEMA = f"""Eres un analista de trafico de Costa Rica. Respondes en espanol, breve y claro. Hoy es {date.today()}.
Reglas: (1) Toda cifra debe venir de una herramienta; nunca inventes numeros. Si no hay herramienta o falla, dilo.
(2) Da siempre la estimacion con su intervalo de confianza y advierte si 'convergio' es false o el IC es amplio.
(3) Menciona los supuestos devueltos (escenario, datos simulados, como interpretaste fechas) y que son proyecciones.
(4) Pasa fechas y franjas tal como las dijo el usuario en 'periodo' y 'franja'. Si un lugar no se reconoce, usa las sugerencias."""


def llm_config():
    base, modelo = PRESETS.get(os.environ.get("LLM_PRESET", ""), ("", ""))
    base, modelo = os.environ.get("LLM_BASE_URL", base), os.environ.get("LLM_MODEL", modelo)
    if not base or not modelo:
        sys.exit("Defina LLM_PRESET (groq|openrouter|gemini|ollama) y LLM_API_KEY. Vea README.md.")
    return base.rstrip("/"), modelo, os.environ.get("LLM_API_KEY", "")


def llamar_llm(cfg, mensajes, herramientas):
    base, modelo, clave = cfg
    cuerpo = {"model": modelo, "messages": mensajes, "tools": herramientas, "temperature": 0.2}
    cab = {"Content-Type": "application/json", "User-Agent": "trafico-chat/1.0", **({"Authorization": f"Bearer {clave}"} if clave else {})}
    req = urllib.request.Request(base + "/chat/completions", json.dumps(cuerpo).encode(), cab)
    try:
        with urllib.request.urlopen(req, timeout=120) as r:
            return json.load(r)["choices"][0]["message"]
    except urllib.error.HTTPError as e:  # muestra el motivo que da el proveedor
        raise RuntimeError(f"El proveedor LLM respondio {e.code}: {e.read().decode('utf-8', 'replace')[:300]}") from None


def texto(res):
    return "\n".join(c.text for c in res.content if getattr(c, "text", None))


async def responder(cfg, sesion, herramientas, historial, pregunta):
    historial.append({"role": "user", "content": pregunta})
    for _ in range(6):
        msg = await asyncio.to_thread(llamar_llm, cfg, historial, herramientas)
        historial.append({k: v for k, v in msg.items() if v is not None})
        if not msg.get("tool_calls"):
            return msg.get("content") or ""
        for ll in msg["tool_calls"]:
            nombre, args = ll["function"]["name"], json.loads(ll["function"].get("arguments") or "{}")
            print(f"  [{nombre} {json.dumps(args, ensure_ascii=False)}]", file=sys.stderr)
            historial.append({"role": "tool", "tool_call_id": ll["id"],
                              "content": texto(await sesion.call_tool(nombre, args))})
    return "No pude completar la consulta."


async def main():
    args = sys.argv[1:]
    aqui = Path(__file__).resolve().parent
    cfg = None if args[:1] == ["-t"] else llm_config()  # falla con mensaje claro antes de abrir el servidor
    servidor = StdioServerParameters(command=sys.executable, args=[str(aqui / "servidor.py")], env=dict(os.environ), cwd=str(aqui))
    async with stdio_client(servidor) as (r, w), ClientSession(r, w) as sesion:
        await sesion.initialize()
        if args[:1] == ["-t"]:  # herramienta directa, sin LLM
            print(texto(await sesion.call_tool(args[1], json.loads(args[2]) if len(args) > 2 else {})))
            return
        herr = [{"type": "function", "function": {"name": t.name, "description": t.description or "",
                 "parameters": getattr(t, "inputSchema", None) or getattr(t, "input_schema", None)}}
                for t in (await sesion.list_tools()).tools]
        hist = [{"role": "system", "content": SISTEMA}]
        if args[:1] == ["-p"]:
            print(await responder(cfg, sesion, herr, hist, args[1]))
            return
        print(f"Trafico Costa Rica ({cfg[1]}). Escriba su pregunta ('salir' para terminar).")
        while True:
            try:
                q = input("\n> ").strip()
            except (EOFError, KeyboardInterrupt):
                break
            if q.lower() in ("salir", "exit"):
                break
            if q:
                try:
                    print("\n" + await responder(cfg, sesion, herr, hist, q))
                except Exception as e:
                    print(f"Error: {e}", file=sys.stderr)


if __name__ == "__main__":
    asyncio.run(main())