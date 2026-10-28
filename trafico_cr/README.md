# Plataforma de predicción de flota vehicular y tráfico en Costa Rica

Simulación: los datos de `data/` son **supuestos** razonables, no mediciones oficiales. Sirven para comparar escenarios.

```
data/*.json        Fuente de datos (solo datos). 9 archivos.
engine/            Motor C++17: algoritmo genético + simulador probabilístico + interfaz JSON.
servidor_mcp/      Python: servidor.py (MCP, ~100 líneas), chat.py (~100 líneas), normalizacion.py.
tests/             pytest (normalización, servidor, chat con LLM falso).
```

## Arquitectura

```
Usuario -> chat.py (LLM) -> servidor.py (MCP, herramientas) -> motor.py -> subproceso trafico_engine (C++)
                                  |  normaliza fechas/franjas        stdin: solicitud JSON / stdout: respuesta JSON
                                  v  errores útiles, sin excepciones
```

Módulos C++ (`engine/src`): `datastore` (carga JSON), `model` (agentes, rutas, métricas), `objective` (función objetivo),
`ga` (algoritmo genético), `escenario` (calibración + proyección a un año), `simulator` (un día), `montecarlo`
(bucle de convergencia e IC), `calendar`, `consultas` (resolución de lugares), `interfaz` (contrato JSON), `rng`, `json`.

## Compilar y ejecutar

```bash
cd engine && make            # produce engine/build/trafico_engine  (g++ >= 9)
pip install -r requirements.txt
pytest -q                    # 37 pruebas
```

Variables: `TRAFICO_ENGINE` (ruta del binario), `TRAFICO_DATA` (carpeta de datos), `TRAFICO_TIMEOUT` (segundos, 180).

Motor directo:
```bash
echo '{"tipo":"flujo_destino","lugar":"La Sabana","anio":2027,"semilla":42}' | engine/build/trafico_engine --datos data
```

### Chat con LLM gratuito o muy barato

Cualquier API compatible con OpenAI que soporte *tool calling*:

| Proveedor | Configuración |
|---|---|
| Groq (plan gratuito) | `LLM_PRESET=groq LLM_API_KEY=...` |
| OpenRouter (modelos `:free`) | `LLM_PRESET=openrouter LLM_API_KEY=...` |
| Google Gemini (capa gratuita) | `LLM_PRESET=gemini LLM_API_KEY=...` |
| Ollama (local, gratis) | `LLM_PRESET=ollama` (modelo con herramientas, p. ej. `llama3.1`) |

```bash
python servidor_mcp/chat.py                        # interactivo
python servidor_mcp/chat.py -p "¿Cuántos vehículos entrarán a La Sabana en 2027?"
python servidor_mcp/chat.py -t capacidad_via '{"lugar":"Rotonda de la Hispanidad","hacia":"Mall San Pedro","periodo":"diciembre de 2030"}'   # herramienta directa, sin LLM
```
El servidor MCP también se puede usar desde otro cliente MCP: `python servidor_mcp/servidor.py` (stdio).
El *system prompt* (en `chat.py`) obliga a: no inventar cifras, dar IC y nivel, y mencionar supuestos y limitaciones.

## Herramientas MCP

`lugares`, `vehiculos_hacia`, `vehiculos_entre`, `capacidad_via`, `impacto_en_congestion`, `flota`, `choques`. Parámetros simples en texto: `periodo` ("finales de 2028"), `franja` ("en la mañana"), `semilla`.
Todas devuelven JSON estructurado. Normalización (`normalizacion.py`): «finales de 2028» → 2028/12, «en 3 años» → año actual+3,
«en la mañana» → 5–12 h, «hora pico de la tarde» → 16–19 h, «entre semana» → laboral. La interpretación se devuelve en
`interpretacion_de_parametros`. Lugar desconocido → `{"ok":false,"error":...,"sugerencias":[...]}`.

## Contrato JSON Python ↔ C++

Una solicitud por stdin, una respuesta por stdout (código de salida 0 si `ok`, 2 si no).

Solicitud:
```json
{"tipo":"flujo_od","origen":"Cartago","destino":"San José","anio":2028,"mes":0,
 "hora_inicio":5,"hora_fin":12,"tipo_dia":"laboral","escenario":"base",
 "semilla":42,"confianza":0.95,"ci_rel":0.05,"tolerancia":0.05,"max_iter":300,"max_ms":20000,
 "generaciones_ga":null,"calibraciones":3}
```
`tipo`: `flujo_destino` (`lugar`), `flujo_od` (`origen`,`destino`), `capacidad_acceso` (`lugar`,`hacia`), `impacto_pois` (`tipo_poi`),
`choques` (`lugar`), `flota` (`nivel`,`nombre`), `lugares`. Todos los campos salvo `tipo` son opcionales.

Respuesta:
```json
{"ok":true,"tipo":"...","parametros":{...},
 "resultado":{"estimaciones":[{"nombre":"","unidad":"","media":0,"ic_inferior":0,"ic_superior":0,"desviacion_diaria":0,"n_muestras":0}],
              "perfil_horario":{...},"error_validacion_maximo_por_punto":{...}},
 "supuestos":["..."],
 "metricas":{"ga_generaciones":0,"ga_mejor_fitness":0,"muestras_dias":0,"iteraciones_bucle":0,
             "error_validacion_final":0,"ic_relativo_maximo_logrado":0,"convergio":true,"tiempo_ms":0,"semilla":0,...}}
```
o `{"ok":false,"error":"...","sugerencias":[...]}`. No se escriben ni leen resultados precalculados: cada consulta calibra y simula.

## Algoritmo genético

- **Individuo (agente):** perfil de conducta que representa *W* vehículos de un cantón de origen (≈267 agentes, 6–48 por cantón; tipo auto/moto/carga/bus).
- **Genes:** prob. de salir, viajes por salida, proporción local/lejana, hora media y desvío, rigidez horaria, fin de semana, velocidad, agresividad, teletrabajo y 22 pesos de destino hacia los puntos de interés.
- **Población inicial sembrada** con un modelo gravitacional a partir de los datos (peso·exp(−min/45)), no uniforme.
- **Fitness** `1/(1+Σ wᵢ·|sim−obs|/obs)` sobre los puntos de validación (flota, accidentes, picos AM/PM, fracciones por día, TPD por tramo). Coevolución cooperativa: el fitness individual es su contribución marginal al error agregado.
- **Operadores:** torneo dentro del cantón, cruce aritmético, mutación gaussiana con σ decreciente, reemplazo del peor sólo si mejora el error + regularización hacia la semilla (λ=30).
- **Fases:** 100 gen. → calibración de la tasa de choques por veh-km → 80 gen. → proyección al año consultado (crecimiento de flota, teletrabajo, pesos de POI, nueva infraestructura) → 40 gen. de reajuste.

## Simulador probabilístico y clasificación Monte Carlo / Las Vegas

Cada día simulado muestrea: fecha y tipo de día, lluvia, eventos, viajes (Poisson), destinos (binomial secuencial), hora del viaje, enrutamiento (Dijkstra por tiempo libre), choques (Poisson por tramo-hora, con reducción de capacidad de duración lognormal), v/c con factores de auto equivalente (moto 0,4; carga 2,5; bus 2,0) y demora BPR.

`montecarlo.cpp` itera en lotes de 20 días hasta que **error de validación ≤ tolerancia** y el **IC (t de Student) ≤ ci_rel**.

- **Clasificación:** es *Las Vegas respecto del criterio de validación* (el bucle sólo termina al cumplir el criterio; la respuesta no es "casi correcta" sino que se verifica) pero con **presupuesto acotado** (mín. 60 días, máx. 300 iteraciones / 20 s). Si se agota el presupuesto se degrada a **Monte Carlo** y `convergio=false`. La estimación en sí es Monte Carlo (media muestral con error estadístico decreciente ∝ 1/√n).
- **Reproducibilidad:** RNG xoshiro256** con `mezclar(semilla, flujo)`; flujos independientes para inicialización, AG, MC y réplicas. La misma semilla da salida idéntica; el reloj es sólo una guarda de seguridad.
- **Réplicas:** `calibraciones` = K (def. 3) poblaciones calibradas independientemente; se combinan con IC t-Student, de modo que la incertidumbre de calibración entra en el IC.

## Supuestos y límites (importante)

- Los datos son inventados/aproximados; ninguna cifra es oficial. Los TPD objetivo de algunos tramos (E01, E03, E05, E07, E10, E12, E21) se ajustaron para ser consistentes con la topología de la red simulada.
- `convergio` es **por calibración**. El `ic_relativo_maximo_logrado` combinado incluye la incertidumbre entre calibraciones y puede superar el 5 % pedido (consultas OD de franjas pequeñas: ±15 %).
- Los conteos origen–destino son viajes casa→destino de los residentes del cantón de origen.
- Error de validación medio ≈ 2–3 %, máximo por punto hasta ≈ 10–12 % (se reporta en `error_validacion_maximo_por_punto`).
- Una consulta típica tarda ≈ 0,5–1 s por calibración.
