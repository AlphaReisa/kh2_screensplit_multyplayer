# KH2-Coop — dos Soras en pantalla dividida (local)

Mod para **KINGDOM HEARTS II FINAL MIX** (Steam, colección 1.5+2.5, versión "Steam 1.0.0.10";
exe SHA1 `2548f1191fe6587a34947952763ce6bb4f3b5c1a`) que añade un segundo jugador local con
pantalla dividida.

## Estado
- Pantalla dividida con dos cámaras completas (escenario y personajes por cámara).
- Jugador 2 controlando a Goofy (mover, atacar, saltar; IA desactivada mientras juega).
- Segundo `YS::SORA` real creado por el motor en el hueco de Goofy (modo cooperativo), con su propia
  ficha de batalla (HP/PM), su mando y su ataque.

Detalles técnicos y direcciones: [research/NOTES.md](research/NOTES.md).

## Estructura
| Ruta | Contenido |
|---|---|
| `src/kh2coop/loader.cpp` | `dinput8.dll` proxy: reenvía DirectInput8 y recarga en caliente el núcleo |
| `src/kh2coop/core.cpp` | núcleo: instala módulos y reparte el tick por frame |
| `src/kh2coop/common.*` | registro, traducción de direcciones, hooks con firma, teclas, `kh2coop.ini` |
| `src/kh2coop/game.*` | interfaz con el motor (offsets con nombre, funciones del juego, codec de handles) |
| `src/kh2coop/split.cpp` | pantalla dividida |
| `src/kh2coop/input.cpp` | entrada del jugador 2 y mando 1 del motor |
| `src/kh2coop/party.cpp` | segundo Sora |
| `src/kh2coop/friend.cpp` | Goofy controlado por el jugador 2 |
| `src/kh2coop/hud.cpp` | HUD |
| `src/kh2coop/debug.cpp` | diagnóstico (solo `build.bat debug`) |
| `src/tools/` | herramientas de análisis en vivo (Python/PowerShell) |
| `research/` | notas, scripts de Ghidra (`q.sh`, `scripts/*.java`) |

## Compilar e instalar
Requiere Visual Studio Build Tools 2022 (MSVC x64). MinHook es un submódulo:
`git submodule update --init`.

```
src\kh2coop\build.bat loader   :: build\dinput8.dll  -> copiar a la carpeta del juego (una vez)
src\kh2coop\build.bat          :: build\kh2coop_core.dll (el cargador lo recarga en caliente)
src\kh2coop\build.bat debug    :: núcleo con diagnóstico
```
El cargador lee la ruta del núcleo de `kh2coop_core_path.txt` (carpeta del juego).
Configuración opcional en `kh2coop.ini` (carpeta del juego):
```
[keys]
split=F8
coop_mode=F6
repair=F7
[coop]
second_sora=0
```

## Plan (hacerlo bien, sin apaños)
1. [x] Reestructurar el código en módulos.
2. [ ] Mando 2 por la vía nativa: rellenar el raw del pad 1 con la conversión del motor; aislar del P1.
3. [ ] Aparición estable del Sora 2 (config persistente, Formas, cinemáticas, muerte); globales explícitos.
4. [ ] Barra HP/PM nativa para el jugador 2 (GAUGE_PLAYER real).
5. [ ] Menú de comandos nativo para el jugador 2 (magia, objetos, invocaciones, Formas).
6. [ ] Comandos de reacción del jugador 2.
- [ ] Cámara 2 propia (órbita independiente siguiendo a P2), no copia trasladada de la cámara 1.

No se incluyen binarios del juego, volcados ni partidas guardadas.
