# Plataforma de predicción de flota vehicular y tráfico en Costa Rica

Sistema que responde preguntas sobre tráfico («¿cuántos vehículos entrarán a La Sabana en 2027?», «¿qué malls generarán más congestión a finales de 2028?») combinando un **algoritmo genético** que genera el comportamiento de los conductores, un **simulador probabilístico por muestreo** que itera hasta cumplir una función objetivo, y un **servidor MCP** que conecta todo con un LLM.

> Los datos de [`data/`](data) son **supuestos de simulación**, no mediciones oficiales. Las cifras sirven para comparar escenarios.

## Contenido

1. [Arquitectura](#arquitectura)
2. [Técnicas algorítmicas](#técnicas-algorítmicas)
3. [Mapa de archivos clave](#mapa-de-archivos-clave)
4. [Cómo ejecutarlo](#cómo-ejecutarlo)
5. [Contrato JSON Python ↔ C++](#contrato-json-python--c)
6. [Supuestos y limitaciones](#supuestos-y-limitaciones)
7. [Problemas frecuentes](#problemas-frecuentes)

## Arquitectura

```
Usuario ─► chat.py ─► LLM (Groq / Gemini / Claude / Ollama)
              │            │ elige una herramienta y sus parámetros
              ▼            ▼
          servidor.py (MCP) ── normalizacion.py  («finales de 2028» → 2028/12)
              │
              │ subprocess: JSON por stdin / JSON por stdout
              ▼
        trafico_engine (C++)
          1. carga data/*.json
          2. algoritmo genético: calibra a los conductores
          3. simulador Monte Carlo: itera hasta cumplir la función objetivo
          4. devuelve estimación + IC + supuestos + métricas
```

El LLM **no calcula nada**: solo decide qué herramienta llamar y redacta la respuesta con el JSON que recibe. El punto exacto donde se invoca el C++ es la función `motor()` de [`servidor.py`](servidor_mcp/servidor.py). Cada consulta calibra y simula desde cero: no se guardan resultados precalculados.

## Técnicas algorítmicas

### 1. Fuente de datos

Nueve archivos JSON con solo datos: flota por cantón, zonas, crecimiento, riesgo de choques, perfiles horarios, calendario y eventos, puntos de interés, red vial y puntos de validación. Se cargan en memoria en [`datastore.cpp`](engine/src/datastore.cpp).

### 2. Algoritmo genético de conductores

Archivos: [`ga.cpp`](engine/src/ga.cpp), [`model.cpp`](engine/src/model.cpp), [`escenario.cpp`](engine/src/escenario.cpp).

- **Individuo (agente):** un perfil de comportamiento que representa *W* vehículos de un cantón de origen. Hay unos 267 agentes (6 a 48 por cantón), con tipo auto, moto, carga o bus.
- **Genes:** probabilidad de salir cada día, viajes por salida, proporción de viajes locales y lejanos, hora media y desvío, rigidez horaria, uso en fin de semana, velocidad, agresividad (que sube el riesgo de choque), teletrabajo y 22 pesos de destino hacia los puntos de interés.
- **Población inicial sembrada** (no uniforme): modelo gravitacional con peso·exp(−tiempo/45 min), tomado de los datos.
- **Fitness:** `1 / (1 + Σ wᵢ · |sim − obs| / obs)` sobre los puntos de validación.
- **Coevolución cooperativa:** el fitness de cada individuo es su contribución marginal al error del conjunto.
- **Operadores:** selección por torneo dentro del cantón, cruce aritmético, mutación gaussiana con σ decreciente y reemplazo del peor individuo solo si mejora el error total.
- **Regularización:** el error incluye una penalización por alejarse de la semilla (`lambdaPrior`), para que la calibración no se desvíe entre ejecuciones.
- **Fases por consulta:** 100 generaciones → calibración de la tasa de choques por veh-km → 80 generaciones → proyección al año consultado (crecimiento de flota, teletrabajo, pesos de POI, nueva infraestructura) → 40 generaciones de reajuste.
- **Rutas:** las calcula el simulador con Dijkstra ([`graph.hpp`](engine/src/graph.hpp)); no hay un gen de elección de ruta.

### 3. Simulación probabilística y función objetivo

Archivos: [`simulator.cpp`](engine/src/simulator.cpp), [`montecarlo.cpp`](engine/src/montecarlo.cpp), [`objective.cpp`](engine/src/objective.cpp).

- **Un día simulado** muestrea fecha y tipo de día, lluvia, eventos (feriados, conciertos, partidos), viajes (Poisson), destinos (binomial secuencial), hora de cada viaje, ruta, choques (Poisson por tramo-hora que pueden reducir la capacidad) y congestión.
- **Congestión:** razón volumen/capacidad (v/c) con factores de auto equivalente (moto 0,4; carga 2,5; bus 2,0) y demora con la función BPR `t₀·0,15·(v/c)⁴`.
- **Función objetivo:** error relativo ponderado contra puntos de validación de flota, accidentes, picos AM y PM, fracciones por día y tránsito diario por tramo.
- **Iteración hasta cumplir:** se simulan lotes de 20 días y se repite hasta que el error de validación sea ≤ la tolerancia (5 % por defecto) **y** el intervalo de confianza sea suficientemente angosto. Mínimo 60 días, máximo 300 iteraciones o 20 s.
- **Clasificación:** es un algoritmo **tipo Las Vegas respecto al criterio de validación** (solo termina al cumplirlo, o lo declara), pero con presupuesto acotado. Si el presupuesto se agota, degrada a **Monte Carlo** y devuelve `convergio=false`. La estimación en sí es Monte Carlo: media muestral con error que baja con 1/√n.
- **Incertidumbre de calibración:** se corren K=3 calibraciones independientes del algoritmo genético y se combinan con un IC t de Student, por lo que el intervalo incluye también esa variabilidad.

### 4. Reproducibilidad y métricas

- **Semillas:** generador xoshiro256** ([`rng.hpp`](engine/src/rng.hpp)) con flujos independientes para inicialización, algoritmo genético, Monte Carlo y réplicas. La misma semilla da el mismo resultado exacto. El reloj es solo una guarda de seguridad.
- **Métricas devueltas:** generaciones del algoritmo genético, mejor fitness, muestras, iteraciones, error final, intervalo de confianza logrado, `convergio` y tiempo.

### 5. Servidor MCP y normalización

[`servidor.py`](servidor_mcp/servidor.py) expone 7 herramientas: `lugares`, `vehiculos_hacia`, `vehiculos_entre`, `capacidad_via`, `impacto_en_congestion`, `flota` y `choques`. [`normalizacion.py`](servidor_mcp/normalizacion.py) convierte fechas relativas («finales de 2028», «en 3 años», «el próximo mes») y franjas («en la mañana», «hora pico de la tarde», «de 6 a 9 am») en parámetros explícitos. Un lugar desconocido devuelve `{"ok": false, "sugerencias": [...]}` en lugar de fallar. El *system prompt* de [`chat.py`](servidor_mcp/chat.py) exige no inventar cifras, dar el intervalo de confianza y mencionar supuestos.

## Mapa de archivos clave

| Qué | Archivo |
|---|---|
| Algoritmo genético | [`engine/src/ga.cpp`](engine/src/ga.cpp) |
| Modelo de agentes y rutas | [`engine/src/model.cpp`](engine/src/model.cpp) |
| Calibración y proyección por año | [`engine/src/escenario.cpp`](engine/src/escenario.cpp) |
| Función objetivo | [`engine/src/objective.cpp`](engine/src/objective.cpp) |
| Simulador de un día | [`engine/src/simulator.cpp`](engine/src/simulator.cpp) |
| Bucle Monte Carlo / Las Vegas | [`engine/src/montecarlo.cpp`](engine/src/montecarlo.cpp) |
| Contrato JSON del motor | [`engine/src/interfaz.cpp`](engine/src/interfaz.cpp) |
| Resolución de lugares | [`engine/src/consultas.cpp`](engine/src/consultas.cpp) |
| Servidor MCP | [`servidor_mcp/servidor.py`](servidor_mcp/servidor.py) |
| Chat de consola | [`servidor_mcp/chat.py`](servidor_mcp/chat.py) |
| Normalización de fechas | [`servidor_mcp/normalizacion.py`](servidor_mcp/normalizacion.py) |
| Datos | [`data/`](data) |
| Pruebas | [`tests/`](tests) |

## Cómo ejecutarlo

### Requisitos

- **Python 3.10 o más nuevo** (python.org; en Windows marque «Add Python to PATH»).
- **g++ con soporte C++17.**
  - Windows: instale [MSYS2](https://www.msys2.org), abra su terminal «MSYS2 UCRT64» y ejecute `pacman -S mingw-w64-ucrt-x86_64-gcc`; agregue `C:\msys64\ucrt64\bin` al PATH y reabra la terminal.
  - Linux: `sudo apt install g++`. macOS: `xcode-select --install`.
- Una **clave de API de un LLM** (ver paso 4). Para probar sin LLM no hace falta.

### Pasos

1. **Instalar dependencias de Python** (dentro de la carpeta del proyecto):
   ```
   python -m pip install -r requirements.txt
   ```
2. **Compilar el motor C++** (una sola vez):
   - Windows: `compilar.bat`
   - Linux/macOS: `sh compilar.sh`

   Debe aparecer `engine/build/trafico_engine` (con `.exe` en Windows).
3. **Probar sin LLM** (confirma que el motor y el servidor funcionan):
   ```
   python servidor_mcp/chat.py -t lugares
   python servidor_mcp/chat.py -t flota "{\"periodo\":\"2030\"}"
   ```
   En PowerShell use comillas simples por fuera: `-t flota '{"periodo":"2030"}'`.
4. **Elegir un LLM y definir variables de entorno** (valen solo en esa terminal):

   | Proveedor | `LLM_PRESET` | Clave | Costo |
   |---|---|---|---|
   | Groq | `groq` | console.groq.com | Capa gratuita |
   | Google Gemini | `gemini` | aistudio.google.com | Capa gratuita |
   | Claude | `claude` | platform.claude.com | De pago (la suscripción Pro no incluye la API) |
   | Ollama | `ollama` | no requiere | Gratis, local |

   PowerShell:
   ```
   $env:LLM_PRESET="gemini"
   $env:LLM_API_KEY="su_clave"
   $env:LLM_MODEL="gemini-2.5-flash"
   ```
   Linux/macOS: `export LLM_PRESET=gemini LLM_API_KEY=su_clave LLM_MODEL=gemini-2.5-flash`

   `LLM_MODEL` es opcional y tiene prioridad sobre el modelo del preset. Los nombres de modelos gratuitos cambian con el tiempo: si recibe un error 404, revise el nombre vigente en la documentación del proveedor. También puede usar cualquier API compatible con OpenAI con `LLM_BASE_URL`, `LLM_MODEL` y `LLM_API_KEY`.
5. **Chatear:**
   ```
   python servidor_mcp/chat.py                      # interactivo ('salir' para terminar)
   python servidor_mcp/chat.py -p "¿Cuántos vehículos entrarán a La Sabana en 2027?"
   ```
   El chat muestra entre corchetes cada herramienta que el LLM invoca.

**Alternativa sin `chat.py`:** el servidor MCP funciona con cualquier cliente MCP, por ejemplo Claude Desktop. Agregue en su `claude_desktop_config.json` un servidor con `"command": "python"` y `"args": ["<ruta>/servidor_mcp/servidor.py"]`.

### Pruebas

```
python -m pytest -q tests
```
Las pruebas del servidor y del chat usan el motor real y un servidor LLM falso; se omiten si el motor no está compilado. Variables opcionales: `TRAFICO_ENGINE` (ruta del binario), `TRAFICO_DATA` (carpeta de datos) y `TRAFICO_TIMEOUT` (segundos, 180 por defecto).

### Motor directo

```
echo {"tipo":"flujo_destino","lugar":"La Sabana","anio":2027,"semilla":42} | engine/build/trafico_engine --datos data
```

## Contrato JSON Python ↔ C++

Una solicitud por stdin y una respuesta por stdout. Código de salida 0 si `ok`, 2 si no.

Solicitud (todos los campos salvo `tipo` son opcionales):
```json
{"tipo":"flujo_od","origen":"Cartago","destino":"San José","anio":2028,"mes":0,
 "hora_inicio":5,"hora_fin":12,"tipo_dia":"laboral","escenario":"base",
 "semilla":42,"confianza":0.95,"ci_rel":0.05,"tolerancia":0.05,"max_iter":300,"max_ms":20000,
 "calibraciones":3}
```
Valores de `tipo`: `flujo_destino`, `flujo_od`, `capacidad_acceso`, `impacto_pois`, `choques`, `flota`, `lugares`.

Respuesta:
```json
{"ok":true,"tipo":"flujo_od","parametros":{},
 "resultado":{"estimaciones":[{"nombre":"","unidad":"","media":0,"ic_inferior":0,"ic_superior":0,
                               "desviacion_diaria":0,"n_muestras":0}],
              "perfil_horario":{},"error_validacion_maximo_por_punto":{}},
 "supuestos":["..."],
 "metricas":{"ga_generaciones":0,"ga_mejor_fitness":0,"muestras_dias":0,"iteraciones_bucle":0,
             "error_validacion_final":0,"ic_relativo_maximo_logrado":0,"convergio":true,
             "tiempo_ms":0,"semilla":0}}
```
En caso de error: `{"ok":false,"error":"...","sugerencias":["..."]}`.

## Supuestos y limitaciones

- Los datos son inventados o aproximados. Los objetivos de tránsito diario de algunos tramos (E01, E03, E05, E07, E10, E12, E21) se ajustaron para ser consistentes con la red simulada.
- `convergio` se evalúa **por calibración**. El IC combinado incluye también la incertidumbre entre calibraciones y puede superar el 5 % pedido (por ejemplo, consultas origen-destino en franjas cortas: alrededor de ±15 %).
- Los conteos origen-destino son viajes de ida, de casa al destino, de los residentes del cantón de origen.
- Error de validación medio de 2 a 3 %; el máximo por punto llega a 10–12 % y se reporta en `error_validacion_maximo_por_punto`.
- Una consulta tarda cerca de 0,5 a 1 s por calibración.
- Las respuestas del LLM dependen del modelo elegido: el prompt le prohíbe inventar cifras, pero eso no se puede garantizar por completo.

## Problemas frecuentes

| Síntoma | Causa y solución |
|---|---|
| `No module named 'mcp'` | Falta instalar: `python -m pip install -r requirements.txt` (use el mismo `python` con que corre el chat). |
| `Motor no compilado` | Falta el paso 2, o ejecutó el compilador fuera de la carpeta del proyecto. |
| `g++` no se reconoce | Instale g++ y agréguelo al PATH; reabra la terminal. |
| `Defina LLM_PRESET ...` | Las variables no están definidas en esa terminal. |
| `403 Forbidden` | Use la versión actual de `chat.py` (envía un `User-Agent`); si persiste, revise la clave o la red. |
| `404 model_not_found` | El modelo no existe o su cuenta no tiene acceso: cambie `LLM_MODEL`. |
| `429` | Límite de peticiones de la capa gratuita: espere un minuto. |

