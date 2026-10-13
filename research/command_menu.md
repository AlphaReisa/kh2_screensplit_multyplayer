# Menú de comandos de batalla (KH2FM Steam) — análisis para un 2º menú (P2)

Todo es análisis estático (Ghidra) salvo lo indicado; el juego no estaba en marcha durante el análisis,
así que **nada de esto está verificado en vivo** (ver "Pendiente / verificar").

## 0. Resumen

- `YS::FIELD_COMMAND` (deriva de `YS::PLAYER_COMMAND`) es **un único objeto estático en .data: 0x142a10620**
  (tamaño ~0xc48, hasta 0x142a11268). Lo construye el inicializador CRT 0x140024560 (atexit -> 0x1405775a0).
  No hay `new` de PLAYER_COMMAND en ninguna parte: el "controlador de jugador 0x142a10620" de NOTES **es** el menú.
- El jugador lo referencia con 3 punteros (PLAYER ctor 0x1403a7a40):
  `obj+0xdc0` = &FIELD_COMMAND (actualización del menú), `obj+0xdd0` = &FIELD_COMMAND (fuente del comando a ejecutar),
  `obj+0xdc8` = &FIELD_COMMAND+0xbe8 (= 0x142a11208, struct de fijación de objetivo/lock-on).
  `obj+0xdb8` = puntero al pad, que PlayerFactory 0x1403a7cb0 pasa **fijo** como `GetPad(0)` (FUN_14039b780(0)).
- El estado del menú es casi todo **por instancia** (dueño en +0xae0, pad en +0xae8). Los objetos de
  dibujo `dk::COMMAND_DRAW` sí dependen de **globales** (instancia única 0x140abc508 + pools).
- Ruta por frame (dentro de ObjUpdate 0x1403bfd30 de cada jugador):
  tipo+0x18 -> 0x1403a8980 -> `FieldCmdUpdate 0x1403fd690(obj->+0xdc0)` -> `PlayerCommand::Update 0x1403b2340`
  (lee pad, rellena +8 con el comando elegido), y luego tipo+0x20 -> `PlayerExec 0x1403a89a0` ->
  `ExecCommand 0x1403d6fc0(obj, (obj->+0xdd0)+8)`.
- **Recomendación**: construir un 2º FIELD_COMMAND con el ctor del juego, ligarlo al Sora P2 (poniendo sus
  +0xdc0/+0xdc8/+0xdd0), y crear un 2º COMMAND_DRAW cambiando un pequeño conjunto de globales de HUD
  (contexto P2) alrededor de las llamadas que lo tocan. Detalles en §5.

## 1. Instancia, creación, dueño y pad

### 1.1 Objeto y clases
| Dirección | Qué |
|---|---|
| 0x142a10620 | instancia única `YS::FIELD_COMMAND` (vtable 0x1405c4c68) |
| 0x1403b1c70 | ctor de PLAYER_COMMAND (base). Único llamador: 0x140024560 (init estático) |
| 0x140024560 | init estático: ctor base, vtable = FIELD_COMMAND, `FUN_1403bda50(+0xb98)`, `(+0xbac)`, `(+0xbc0)`, `FUN_1403dcb30(+0xbe8)` (lock-on), `+0xc30 = 0`, atexit(0x1405775a0) |
| 0x1403a79a0 / 0x1403a79f0 | dtor (PLAYER_COMMAND / FIELD_COMMAND): `FUN_140406570(+0xab0)` |
| 0x142ae80a0 | `g_CurFieldCommand` = &FIELD_COMMAND mientras está ligado (escrito por Bind/Unbind). Solo lo leen 0x1403fd4c0 (deshabilitar menú) y 0x1403fd520 (habilitar), llamados por eventos/scripts |

vtable PLAYER_COMMAND 0x1405c4c30 / FIELD_COMMAND 0x1405c4c68:

| slot | PLAYER_COMMAND | FIELD_COMMAND | función |
|---|---|---|---|
| 0 +0x00 | 0x1403a79a0 | 0x1403a79f0 | dtor |
| 1 +0x08 | 0x1403b26b0 | 0x1403f2990 | Init(this, owner, pad*) (FIELD: base + push menú `+0xbd4`) |
| 2 +0x10 | 0x1403a7990 (nada) | 0x1403f29e0 | Refresh: reconstruye todas las listas (cada frame) |
| 3 +0x18 | 0x1403b1f10 | 0x1403f2470 | Input (cursor/confirmar/cancelar/atajos) |
| 4 +0x20 | 0x1403b1e90 | 0x1403f22e0 | Confirm: decide objetivo y fija el comando, o abre submenú |
| 5 +0x28 | 0x1403b2a30 | 0x1403f3a90 | Term/Reset (vacía pila de menús) |

### 1.2 Ciclo de vida
- Carga de zona: 0x14039ca10 -> 0x1403a7f00 -> `FieldCmdReset 0x1403fd760(&FC)`: +0xc30=0, +0xc38=0, reset lock-on,
  reset reacción, flags, `FUN_1403f27d0` (vacía menús, **+0xae0 = 0**, lista de magias +0xb68 desde la tabla
  de magias cargadas, atajos 0x1409acfa8), +0xc40 = 0xf.
- Salida de zona: 0x14039cac0 -> 0x1403a7f60 -> 0x1403f28c0 (vacía las 0x13 listas).
- PLAYER ctor 0x1403a7a40 (lo ejecuta cada SORA ctor 0x140405030) -> al final `FUN_1403aa850(obj)`:
  cámara (0x140164c60 -> 0x140718cb0), `0x142a10420 = obj` (0x1403a52d0), `obj+0xdc0 = &FC` y
  **`FieldCmdBind 0x1403fd830(&FC, obj)`**:
  ```
  g_CurFieldCommand(0x142ae80a0) = this;
  LockOnBind(this+0xbe8, obj)            // 0x1403dd5b0 -> +0xc20 = obj
  if (obj+0x5c0) HudSetSlot(0, obj)      // 0x140168850 -> 0x140abcfe8[0]
  if (this+0xc38 == 0) vtbl->Init(this, obj, *(obj+0xdb8))   // pad del jugador
  else this+0xae0 = obj;
  this+0xc38 = obj; if (!+0xc40) { +0xb3c|=1; +0x24&=~1; +0xc30|=1; }
  ```
  => **cada Sora construido re-liga el menú único**: por eso el mod restaura +0xae0/+0xc20/+0xc38.
- Destrucción del jugador 0x1403a8f30 / 0x1403a7120 (solo si g_Player != 0): `FieldCmdUnbind 0x1403fd610(obj+0xdc0)`
  (reset reacción y lock-on, `HudClearSlot(0)` 0x140168970, vtbl Term, +0xc38 = 0, `g_CurFieldCommand = 0`), obj+0xdc0 = 0.
- Apertura/cierre visual: `FieldCmdOpen 0x1403fd7e0` (crea/religa COMMAND_DRAW 0x14015e410(this), `0x1403b2b50`
  -> 0x14015bf40 muestra grupos HUD 1 y 2, `FUN_1403c26f0`, +0xc30|=2) y `FieldCmdClose 0x1403fd720`.
  Los llama el update del jugador 0x1403a92c0 (si `obj+0xe00 == 0` -> Open con `obj+0xdc0`, `obj+0xe00 = 1`) y 0x1403aa9e0(obj, modo).

### 1.3 Pad
- `obj+0xdb8` = pad* = parámetro 4 del PLAYER ctor; PlayerFactory 0x1403a7cb0 llama `FUN_14039b780(0)` (GetPad(0)) y lo pasa a
  todos los ctor de jugador. Init copia ese puntero a `FC+0xae8`. **No hay global de "pad del menú"**: el menú lee `*(FC+0xae8)`.
- Con el intercambio de pads 0/1 del mod alrededor de ObjUpdate de P2, un menú cuyo +0xae8 = GetPad(0) lee el mando de P2
  mientras se ejecuta dentro del update de P2. Si se quita el intercambio, poner GetPad(1) (0x140bf3208) en P2+0xdb8 y en +0xae8.
- Bits usados (pad +0 = actual, +8 = pulsado):
  - pulsado `0x4 | 0x200000000` confirmar, `0x8` cancelar, `0x4000` arriba, `0x8000` abajo, `0x10000000` (entrada con submenú,
    abre submenú), `0x200` (en menú tipo atajo: marca), `0x400` **reacción (triángulo)**, `0x10000` alterna menú principal 0 <-> 6.
  - actual `0x800000` (L1 mantenido) -> menú de atajos (lista 5); `0x100` -> comando+0xc |= 1, `0x200` -> |= 4.
  - Atajos: tabla de máscaras 0x1405c4ed8 = {0x2d, 0x1000000, 0x2000000, 0x4000000, 0x8000000} (botones frontales).

### 1.4 Layout de FIELD_COMMAND (offsets desde 0x142a10620)
| off | contenido |
|---|---|
| +0x00 | vtable |
| +0x08 | **comando de salida** {u16 id, u16 tipo(+2), u8 flags(+0xc: 1,2,4,8,0x10), u8 +0xd, objetivo +0x10..0x24} (`FUN_1403d7f00` reset, `FUN_1403d7f30` set). Se pone a 0 al inicio de cada Update |
| +0x24 | flags (1 = deshabilitado, 2, 8 = sin input, 0x10, 0x20, 0x40 = visible) |
| +0x28 + i*0x80 | **19 listas de menú** (i = 0..0x12). Lista: +0 tipo(=i), +8 owner(=FC), +0x10 n entradas, +0x14 entradas[8] de 0xc {u16 flags, u16 cmdId, u8, u8 estado(+5: 5 gris, 6), u8 +6, u8 +7}, +0x74 cursor, +0x78 id seleccionado |
| | i0 +0x28 principal; i1 +0xa8 (magia/estándar, `FUN_1403b1840`); i2 +0x128 objetos (de los datos guardados del dueño: `FUN_1403d74c0(owner)` = save via ficha +0x250); i3 +0x1a8 invocaciones (`FUN_1403c3ef0(1..6)` global); i4 +0x228 selección de miembro del grupo (`FUN_1403d5c50(0..2)`: 0 = **g_Player**, 1..2 = huecos de grupo; objetos en +0xb80[]); i5 +0x2a8 atajos (0x1409acfa8, 4 u16, globales/guardado); i6 +0x328 menú principal alternativo (`FUN_1403f3860`); i7 +0x3a8; i8 +0x428 (grupo/limit, `FUN_1403e2e20/3680`, objetivos en +0xb98+i*0x14); i9 +0x4a8 (`FUN_1403e7b30`); i10 +0x528 (menú externo si +0xbd8); i11 +0x5a8 (cmd 0x1b7); i12 +0x628 (forma 3, tabla 0x14074ebe8) |
| +0x9a8 | puntero a la cima de la pila de menús (pila de punteros a listas en +0xaf0..) |
| +0x9b0 + i*0x24 | 7 comandos "override" (i=1 reacción, 3..6 sustituyen el menú principal) — `FUN_1403b2660` |
| +0xa1c | override activo si u16 +0xa1e != 0 |
| +0xab0 | comando de situación (`FUN_140406390/0x1404060e0`), destruido con 0x140406570 -> 0x14015dc00 (toca COMMAND_DRAW global) |
| +0xae0 | **dueño (jugador)** |
| +0xae8 | **pad*** |
| +0xaf0 | base de la pila de menús (lectura `**(FC+0xaf0)` = tipo del menú raíz; 0xe = modo especial) |
| +0xb10 | gestor de reacción (`*+0xb10 = this`; update 0x140408520, recolección 0x1404081c0 relativa al dueño) |
| +0xb20 | icono 2D de reacción (FUN_14017c570 / FUN_14017bfe0) |
| +0xb3c | flags |
| +0xb40 | **reacción actual** {u16 flags, u16 cmdId (+0xb42), u8, u8 estado (+0xb45, <4 = usable)}, objetivo handle +0xb48, struct objetivo +0xb28 |
| +0xb50/+0xb58/+0xb5c/+0xb60 | flotantes (stick +0xb50 <- pad+0x30, escala/alpha 1.0) |
| +0xb68 | lista de magias (hasta 6 u16 de la tabla de magias cargadas 0x142a23a80, 0-terminada) |
| +0xb76 | (FUN_1403e6d10) |
| +0xb80 | objetivos del menú de grupo [3] |
| +0xbd4 | menú raíz actual (0 o 6) |
| +0xbd8 / +0xbe0 | lista externa (scripts) |
| +0xbe8 | **lock-on** (ctor 0x1403dcb30; registra el primero en `0x142a24e90`), +0xc20 dueño (=+0xbe8+0x38). Modo 2 escribe el objetivo de cámara global 0x140abe540 (0x140176910) |
| +0xc30 | flags (bit0 lock-on activo, bit1 abierto) |
| +0xc38 | dueño ligado (0 = sin ligar; el update no corre) |
| +0xc40 | contador de frames de arranque (0xf tras reset) |

## 2. Lógica por frame y despacho

1. `ObjUpdate 0x1403bfd30(obj)`: tipo+0x18 (SORA: 0x140404a30 -> 0x1403a8980), luego si vivo tipo+0x20 (SORA: 0x140404a40 -> 0x1403a89a0).
2. 0x1403a8980: `if (obj+0xdc0) FieldCmdUpdate 0x1403fd690(obj+0xdc0)`:
   si +0xc38: lock-on update (0x1403dd280 si +0xc30&1), `PlayerCommand::Update 0x1403b2340`, contador +0xc40.
3. `0x1403b2340(this)` (requiere +0xae0 y +0xae8):
   - `+8 = vacío`; `vtbl->Refresh` (0x1403f29e0, reconstruye listas); `ReactionUpdate 0x140408520(this+0xb10)`;
   - si visible (+0x24&0x40): alpha (0x14015dda0 -> **COMMAND_DRAW global**);
   - si dueño vivo y no deshabilitado y `!FUN_1403a57f0(0x142a10500)`:
     - pulsado 0x400 y hay reacción -> `0x1403b22d0(this, cmdTable(+0xb42), obj(+0xb48))` y 0x14015d660 (global draw);
     - si no -> `vtbl->Input` (0x1403f2470): comandos de situación (+0xab0), L1 -> atajos (lista 5),
       `0x1403b1f10` (cursor/confirmar/cancelar), 0x10000 alterna raíz 0/6.
   - confirmar -> `0x1403b21d0` -> `vtbl->Confirm` (0x1403f22e0): si la entrada tiene submenú (`cmd+6 >= 0`) -> push
     (`0x1403b2800`); si no, objetivo (lista 4 -> +0xb80[cursor]; otros -> lock-on `owner+0xdc8`+4 o el propio dueño) y
     `0x1403b22d0(this, entry, target)` = `CmdUsable 0x1403b6950(entry, owner)` + `SetCmd 0x1403d7f30(this+8, id, target)`.
4. `PlayerExec 0x1403a89a0(obj)`: `src = obj+0xdd0`; si `src+8 id != 0` registra; `ExecCommand 0x1403d6fc0(obj, src+8)`.
   **El comando se ejecuta sobre el objeto que lo lee**, no sobre g_Player: el menú no llama a ExecCommand.
5. Tabla de comandos: `CmdLookup 0x1403b68b0(id)` bsearch en `*0x142a161e8`; los comandos de tipo 2 (magia) se resuelven por
   `FUN_1403c5900(i)` = id de la magia cargada i (tabla 0x142a23a80, entrada 0x50, [9]+0x26) — **la lista de magias del menú solo
   contiene magias cargadas**, así que con el menú real el cierre de CastMagic (id no cargado) no puede ocurrir.
6. Usabilidad por entrada: `FUN_140402130(entry, cmd, owner, ...)` / `CmdUsable 0x1403ba070(owner, cmd)` miran el estado del dueño
   (flags +0x18c, +0x638 etc.); PM/coste salen de la ficha del dueño (+0x5c0).

## 3. Dibujo: dk::COMMAND_DRAW

- Instancia **global única `0x140abc508`** (0x670 B). Creada por `CmdDrawCreate 0x14015e410(fieldCmd)`:
  si ya existe solo hace `draw+0x398 = fieldCmd` (¡re-liga!). Si no: `FUN_14015c660` crea pools globales
  (`0x140abc4f0` = 0x34 dk::COMMAND_ONE (entradas), `0x140abc4e8` = 0x30 dk::ReactionDecideSprite, `0x140abc4e0` = dk::SpriteImage
  del layout), alloc 0x670 con el asignador del gestor HUD (*0x140abcd20+0x28 vfunc+8), ctor en línea (= 0x14015a630), sprite raíz
  `dk::Sprite` en **+0x1a0** (layout 0x14b5), `FUN_14016b330(+0x1a0, 0x1c, 0x142)` (=> sprite+0x1c0/+0x1c4 = (28, 322)),
  `HudObjRegister 0x140166950(draw, 0x14b6, 1)` -> **tarea 2D prio 0x14b6 con Hud2DTask 0x1401667d0, grupo 1**, y `FUN_14015c5d0`.
- Lo crea FieldCmdOpen 0x1403fd7e0 (= al aparecer el jugador), y 0x14015dde0/0x14015de90 (cambiar textura). Lo destruye
  `0x14015c090` desde HudTeardown 0x140168b10 (= HudDestroy 0x14016c2b0 + esto + 0x14017a440); la creación del HUD 0x140168b30
  (0x14017a470 + HudCreate 0x14016c3a0) **no** lo crea.
- vtable 0x1405b1c28: [0] 0x1401669f0 nada, [1] **update 0x14015e800**, [2] 0x140166a10 nada, [3] dtor 0x14015a880, [4] 0x140166ed0 (kill: +0x10|=1).
- Estado que lee: `draw+0x398` = PLAYER_COMMAND (pila +0x9a8/+0xaf0, flags +0x24, reacción +0xb42), `+0x3a0/+0x3a8` texturas,
  `+0x3b0` visible, `+0x3b4/+0x3b8/+0x3bc` alpha, `+0x20[4][9]` punteros a COMMAND_ONE por nivel, `+0x174[4]` cuentas,
  `+0x184` tipo raíz.
- Las entradas (COMMAND_ONE, 0x3c8 B) salen del pool 0x140abc4f0 en `0x14015d140`, con `HudObjRegister(.., 0x14b7, grupo 2)` y
  posición **absoluta** `FUN_14016b330(sprite, x = 0x1c + 0x10*nivel, y = 0x184 - 0x16*n + 0x16*i)` (sprite+0x1c0, reescrita cada frame).
- Globales usados por el código de dibujo: `0x140abc508` (instancia), `0x140abc4e0`, `0x140abc4e8`, `0x140abc4f0`,
  `0x140abc4f8[4]` (contadores por nivel, se ponen a 0 al empezar el update), `0x140abc510` (byte de estado),
  constantes 0x140717xxx/0x140718xxx. Muchas funciones usan `0x140abc508` en vez de `this` (0x14015d140, 0x14015bf40/bf00,
  0x14015dda0, 0x14015d660, 0x14015dc00, 0x14015e800...).
- Grupos HUD: `HudObj+0x14` = grupo, `+0x10` flags (2 oculto, 0x20 sin update, 1 matar). Mostrar/ocultar por grupo recorriendo
  *toda* la lista del gestor: 0x140167470 (mostrar), 0x140166bd0 (ocultar), 0x140167280/0x1401673b0 (activar/pausar update),
  0x140166f30 (matar). El menú usa grupo 1 (raíz) y 2 (entradas): **un 2º menú con los mismos grupos se mostraría/ocultaría a la vez**.
- Desplazar en pantalla: raíz -> sprite+0x1c8 (offset propio) del sprite en draw+0x1a0 (= draw+0x368); entradas -> +0x1c8 de cada
  COMMAND_ONE en draw+0x20[nivel][j] (se puede poner tras cada update). +0x1c0 lo reescribe el juego cada frame.
  Ancho HUD 683 a 16:9 -> P2 en la mitad derecha: dx ≈ +341.

## 4. Reacción (triángulo) y atajos (L1 + botón)

- Reacción: en `0x1403b2340`, pulsado 0x400 con reacción válida (+0xb42 != 0, +0xb45 < 4) -> mismo `0x1403b22d0` -> mismo comando +8
  -> mismo `PlayerExec`. Candidatas: `0x1404081c0` todo relativo al dueño (+0xae0, su +0xdc8, su +0xdd0); icono 2D por instancia (+0xb20).
- Atajos: `0x1403f2470`: actual & 0x800000 (L1) -> raíz = lista 5 (atajos; ids en 0x1409acfa8, globales = datos de partida);
  `0x1403b1f10` con tipo de menú 5 dispara la entrada cuyo botón coincide (máscaras 0x1405c4ed8) -> `0x1403b21d0` -> Confirm.
  Todo por el mismo objeto.

## 5. Recomendación: 2º menú independiente para P2

### 5.1 Lógica (FIELD_COMMAND #2)
```
fc2 = alloc(0xc50) (memoria propia del mod, alineada a 16)
FUN_1403b1c70(fc2); *(fc2) = 0x1405c4c68 (vtable FIELD_COMMAND)
FUN_1403bda50(fc2+0xb98); FUN_1403bda50(fc2+0xbac); FUN_1403bda50(fc2+0xbc0)
FUN_1403dcb30(fc2+0xbe8); *(u32*)(fc2+0xc30) = 0               // = 0x140024560 sin la vtable/atexit
FUN_1403fd760(fc2)                                              // reset de zona (lista de magias, etc.)
// "Bind" manual (NO llamar 0x1403fd830: escribe g_CurFieldCommand y el hueco HUD 0)
FUN_1403dd5b0(fc2+0xbe8, P2)
P2+0xdc0 = fc2; P2+0xdd0 = fc2; P2+0xdc8 = fc2+0xbe8; (P2+0xdb8 = pad que corresponda)
vtbl(fc2)->Init(fc2, P2, *(P2+0xdb8))  // 0x1403f2990
fc2+0xc38 = P2; fc2+0xb3c |= 1; fc2+0x24 &= ~1; fc2+0xc30 |= 1; fc2+0xc40 = 0
```
- Momento: justo después del SORA ctor de P2 (en h_SoraCtor, tras restaurar los punteros de P1) — el ctor ya habrá dejado
  P2+0xdc0/+0xdd0/+0xdc8 apuntando al menú de P1. Repetir `FUN_1403fd760` en cada carga de zona (hook 0x1403a7f00) y
  `0x1403f28c0` al salir (hook 0x1403a7f60) si fc2 sobrevive entre zonas (o recrearlo con cada Sora P2).
- Destrucción (P2 destruido con g_Player = 0, como hace el mod): replicar 0x1403fd610 sin sus globales:
  `FUN_140408b90(fc2+0xb10); +0xb3c&=~1; +0x24|=1; +0xc30&=~1; +0xc40=0; FUN_1403dd5a0(fc2+0xbe8); vtbl->Term(fc2) (0x1403f3a90); +0xc38=0`
  y P2+0xdc0 = 0 antes del destroy (para que 0x1403a8f30, si llegara a correr, no toque el de P1).
- El update y la ejecución salen solos: ObjUpdate(P2) -> 0x1403a8980 usa P2+0xdc0 = fc2; 0x1403a89a0 usa P2+0xdd0 = fc2.
  Con el intercambio de pads del mod, fc2 lee el mando 2 vía GetPad(0). P2Commands (ExecCommand directo) y el hook de pads de
  ataque deberían eliminarse: Atacar (id 1) ya sale del menú.

### 5.2 Dibujo (COMMAND_DRAW #2) — "contexto HUD de P2"
Guardar/intercambiar estos globales como un contexto (P1 <-> P2):
`0x140abc508` (instancia), `0x140abc4e0`, `0x140abc4e8`, `0x140abc4f0`, `0x140abc4f8` (16 B), `0x140abc510` (1 B).
- Crear: con el contexto P2 vacío (todo a 0) llamar `0x14015e410(fc2)` -> crea instancia y pools propios de P2; guardar el contexto P2
  y restaurar el de P1. Pools propios => sin agotar el pool de 0x34 COMMAND_ONE de P1.
- Activar el contexto P2 alrededor de: (a) todo ObjUpdate de P2 (ya enganchado: ahí corren 0x1403fd7e0/0x14015dda0/0x14015d660/
  0x14015dc00 con fc2); (b) el update del COMMAND_DRAW de P2 (hook en 0x14015e800, o en Hud2DTask 0x1401667d0 cuando
  `task+0x20 == draw2`); (c) la destrucción (`0x14015c090` bajo contexto P2, y antes que HudTeardown 0x140168b10 destruya el de P1).
- Grupos: remapear 1->g1', 2->g2' (ids libres, p.ej. 0x101/0x102) dentro del contexto P2 en `0x140166950` (registro) y en
  0x140167470 / 0x140166bd0 / 0x140167280 / 0x1401673b0 / 0x140166f30; si no, abrir/cerrar uno afecta al otro.
  (Los grupos de HUD que existan aparte de 1/2 no se han inventariado.)
- Posición: tras cada update del draw de P2, sumar dx a +0x1c8 de draw2+0x1a0 y de cada COMMAND_ONE en draw2+0x20[4][9]
  (y de los ReactionDecideSprite si se ven mal). Pantalla dividida: dx ≈ +341 (ancho 683).

### 5.3 Globales/supuestos de jugador único que el menú consulta
| global | dónde | efecto para P2 |
|---|---|---|
| 0x142a10620 FIELD_COMMAND | PLAYER ctor 0x1403a7a40 (+0xdc0/+0xdc8/+0xdd0), 0x1403aa850 | sustituir en P2 por fc2 |
| 0x142ae80a0 g_CurFieldCommand | 0x1403fd830/0x1403fd610; leído por enable/disable 0x1403fd4c0/0x1403fd520 | eventos/scripts solo deshabilitan P1 -> sincronizar fc2+0x24 bit0 / +0xc30 con fc2 si se quiere |
| 0x142a105d0 g_Player | `FUN_1403d5c50(0)` (lista de objetivos de objetos/cura, i4), varios checks | "usar Poción en ti mismo" de P2 apunta a P1; scripts como 0x140430150 (g_Player->+0xdc0) solo tocan P1 |
| 0x140abcfe8[0] HUD hueco 0 | Bind 0x140168850 / Unbind 0x140168970 | no llamar Bind/Unbind del juego con fc2 |
| 0x140abc508, 0x140abc4e0/4e8/4f0/4f8/510 | todo COMMAND_DRAW | contexto P2 (§5.2) |
| 0x142a10500/04/08 (`FUN_1403a57f0`) | bloqueos globales del menú | compartidos (OK) |
| 0x1409ab580/0x1409ab588 (`FUN_14039a830/7e0`) | "se permite input / batalla" | compartidos (OK) |
| 0x1409acfa8 atajos, 0x1409acf98 | i5 / flags | atajos compartidos con P1 |
| 0x142a23a80 magias cargadas (+0xb68) | Refresh/0x1403f27d0, CmdLookup | compartido; seguro (solo magias cargadas) |
| invocaciones `FUN_1403c3ef0/4720/4200`, grupo `FUN_1403e2e20/3680/FUN_1403c3270` | Refresh | compartido |
| 0x142a24e90 (primer lock-on) / 0x140abe540 (lock-on de cámara) | 0x1403dcb30 / 0x140176910 (modo 2) | el lock-on de P2 movería el objetivo de cámara único -> filtrar modo 2 en fc2 |
| GetPad(0) fijo | PlayerFactory 0x1403a7cb0 -> obj+0xdb8 | ok con intercambio; si no, GetPad(1) |
| 0x14039b780(0) en 0x14015e800 | update del draw | detalle visual menor |

### 5.4 Riesgos
- **Drive / Fusión**: `CmdDrive` -> cambio de forma 0x14040d000 destruye y recrea el SORA: re-ligaría globales de jugador y el menú
  (P2+0xdc0 volvería al de P1). Deshabilitar las entradas Drive (4) / Revert (5) en fc2 (estado +5 = 5) hasta tratarlo.
  El indicador de Drive y la ficha de datos guardados (+0x250) se comparten con P1 (P2 usa los datos guardados de Sora).
- **Invocación**: sustituye miembros del grupo; P2 ocupa el hueco de Goofy -> podría destruir a P2. Deshabilitar para P2.
- **Objetos**: el inventario es el del personaje (datos guardados de Sora = compartido con P1). Objetivo "yo" = g_Player.
- **PM**: por ficha (+0x5c0), P2 ya tiene ficha propia -> OK. Magias: solo cargadas -> no repite el cierre de 14:55.
- **Límites/Party (i8)**: usan los huecos del grupo; con P2 en el hueco de Goofy pueden referirse a sí mismo.
- **Doble update actual**: hoy P2+0xdc0/+0xdd0 apuntan al menú de P1 (el ctor de SORA los pone y el mod no los restaura) ->
  en el update de P2 (pads intercambiados) se vuelve a ejecutar `0x1403fd690(&FC)` con el mando de P2: según el código, **P2
  mueve el cursor compartido y lo que confirme se ejecuta sobre P2** (ExecCommand(P2, FC+8)). Además el lock-on (P2+0xdc8) es el
  de P1. Hay que verificarlo en vivo; si se confirma, ya es un bug del estado actual.
- Pool de COMMAND_ONE (0x34) si se compartiera; con pools propios (§5.2) no.
- Los handlers de evento que deshabilitan "el" menú (0x1403fd4c0 desde 0x1403a37a0, 0x1403a3800, 0x1403a3cb0, 0x1403a41e0,
  0x1403bf8f0, 0x140430690) no afectan a fc2: en cinemáticas P2 seguiría con menú activo -> espejar el bit +0x24&1 de FC a fc2.

## 6. Cierre al lanzar Piro (P1) con Sora P2 presente

- Syscall 0x14041e2c0: `obj = handle(arg0)`; `pos = FUN_1402be970(obj)` = `FUN_1402c2fe0(*(obj+0x30))`; es "posición de un efecto"
  (instancia de efecto creada con `FUN_1402c6b30(contenedor, idx, ...)`; `+0x30` = nodo de partícula; 0x1402be8a0 lo mata y pone +0x30 = 0).
  No es un objeto YS::OBJ: es un **efecto cuya instancia ya fue matada** y cuyo handle seguía en una variable del script de la magia.
- CastMagic 0x1403c5170 pasa al script (FUN_1403e1790(.., 0x14, args)): handle del lanzador, handle del contenedor de efectos
  de la magia (`0x142a23a80 + i*0x50 + 0x18`, inicializado al cargar zona por 0x1403c5a90 con `FUN_1402c6320`, que además lo
  encadena en la lista global 0x140b63608/0x140b63610) y el nivel. Compara `handle(obj+0xbf8) == lanzador`.
- No he encontrado en el SORA/PLAYER ctor una escritura a un global de efectos. Candidatos a revisar:
  1. Efectos por id en un contenedor compartido: `0x1402c6410(contenedor, id)` mata la instancia con ese id; si el update del 2º Sora
     (o su script) para/relanza un efecto con el mismo id en el mismo contenedor, mata el de P1.
  2. El doble update del menú compartido (§5.4): el update de P2 corre el Refresh/Reacción del menú de P1 y el "situation command"
     +0xab0 (0x140406570 -> 0x14015dc00) — podría matar efectos ligados al menú/lanzamiento.
  3. obj+0xde8/+0xdf8 del jugador (efectos propios, matados en 0x1403a92c0/0x1403a9fe0/0x1403a8f30) — si algo copia handles de P2 en P1
     (MoveReferences del cambio en vivo Goofy<->Sora).
  Diagnóstico propuesto: hook en 0x1402be8a0 registrando dirección de retorno + handle del efecto, y en 0x14041e2c0 el handle; ver
  quién mató el efecto que luego se consulta.

## 7. Pendiente / verificar en vivo
- Confirmar que hoy P2+0xdc0 == P2+0xdd0 == 0x142a10620 y que P2 mueve el cursor del menú de P1.
- Confirmar grupos HUD libres para remapear (listar `+0x14` de los objetos del gestor *0x140abcd20).
- Confirmar el efecto de +0x1c8 en COMMAND_ONE (si tienen padre en +0x1e0, el offset va en el padre).
- Significado exacto de las listas i6..i12 y de los bits 0x100/0x200/0x10000/0x10000000.
