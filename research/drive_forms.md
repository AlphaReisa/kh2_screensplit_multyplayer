# Fusiones (Drive Forms) y Revertir — KH2FM Steam, análisis para un 2º Sora (P2)

Análisis estático (Ghidra) + lecturas en vivo puntuales (13-10-2026, solo lectura). Direcciones = VAs de Ghidra.
Formas: 0 normal, 1 Valor, 2 Sabiduría, 3 Límite, 4 Maestra, 5 Final, 6 Anti (7, 10, 0xc existen como "formas" de
jugador especiales; su objentry está en la misma tabla, no identificadas).

## 0. Resumen

- **Arranque**: Drive/Revertir son comandos de batalla tipo 4/5. `ExecCommand 0x1403d6fc0(obj, cmd)` -> (tipos no 1/2/3/0x16d)
  `tipo+0x138` -> SORA `0x140404fd0` -> **`SoraCmdExec 0x140405440(obj, cmdEntry)`** -> **`FormChangeRequest 0x14040ce60(obj, form)`**
  -> ctx (0x38 B) `FormCtxCtor 0x14040caa0` -> tarea corrutina `0x14040cf10 -> FormChangeTask 0x14040d000(ctx, task)`.
  Todo opera sobre **el objeto que ejecuta el comando** (el que lee su `obj+0xdd0`), no sobre g_Player; los supuestos de jugador
  único están en los globales que tocan esas funciones y en el ctor/destroy de SORA.
- **Revertir automático** (indicador de forma a 0): `SORA tipo+0x20 0x140404a40` (tras PlayerExec) -> `0x14040ce60(obj, 0)`.
- **Forma actual = un byte global de la partida `0x1409acdd4`** (no por objeto). `0x14040ce60` lo compara y lo escribe.
- **Una sola fusión a la vez** de forma nativa: ctx único `0x142ae9fa8`, cerrojos globales `0x142a10504` bit1 (tarea viva) y
  `0x142a10508` bit0 (hay un Sora en forma), lista única de miembros absorbidos `0x142ae9788`, y el gestor de recursos solo
  mantiene un modelo de forma (prioridad 5000, `0x14039cca0(5000)` envejece el anterior).
- **Absorción**: solo sobre la tabla de amigos `0x142a239b0[0..1]` (objetos kn::Friend). **El Sora P2 no está en esa tabla**
  (verificado en vivo: `[0]=0` (hueco de Goofy vaciado), `[1]=Donald`) => las formas de P1 **nunca absorben a P2**.
  Pero el requisito de grupo `0x1404005e0` exige a Goofy (charId 3) para Valor y ambos huecos para Maestra/Final/Anti =>
  **con P2 presente P1 solo puede usar Sabiduría y Límite**.
- **BUG ya presente para P1**: `StatusSlotFor(1)` (0x1403c04e0) devuelve la *primera* ficha no libre con charId 1 por orden de
  pool; la de P2 también tiene charId 1 (el mod la re-etiqueta). En vivo la ficha de P2 (…2e30) está antes que la de P1 (…3598) =>
  **una fusión de P1 cura a P2, pone el indicador de forma en la ficha de P2, y el nuevo Sora-forma de P1 se queda con la ficha
  de P2** (P1 y P2 compartirían HP/PM/Drive y la ficha propia de P1 se libera).
- **Hoy P2 puede fusionarse** (el menú propio fc2 no deshabilita Drive) y eso rompe todo: el ctor del nuevo SORA no pasa por la
  rama P2 del hook (solo `g_pendingP2`), se convierte en "el jugador"; antes, el destroy del Sora P2 (con g_Player != 0) desmonta
  los globales de P1 y hace `Unbind` de fc2 tocando `g_CurFieldCommand` y el hueco HUD 0 de P1.

## 1. Quién arranca una fusión / reversión

### 1.1 Comando Drive (tipo 4) y Revertir (tipo 5)
- Menú (FIELD_COMMAND Refresh 0x1403f29e0): entrada principal = `4` (Drive) si `!FUN_1403aa4f0(owner)` (no en forma) o `5`
  (Revertir) si sí; usa `owner = FC+0xae0` (=> por menú/objeto). Submenú de formas: lista `FC+0x1a8`, formas `FUN_1403c3ef0(1..6)`
  (tabla de comandos 0x1405c5d08), obtenidas `FUN_1403c4720` (datos de partida, compartidos), requisito de grupo `0x1404005e0`.
  El submenú entero se vacía si `0x142a10504 bit1` o `0x142a10508 bit0` (globales).
- Usabilidad (`FUN_140401d60(cmd, owner)`; también `0x1403a9ea0` = `0x1403b6a10 && CmdUsable 0x1403ba070 && 0x140401d60 < 4`):
  - tipo 4: `!0x142a10508 bit0 && !0x142a10504`, owner vivo, `owner+0x6cb & 1`, **`0x1404005e0(cmd) == 0`** (grupo),
    `owner+0xde0 != 7 && != 0xc`, Límite: `!FUN_1403a3a90()`, `!(0x142a11400 & 0x40000)`, coste `cmd+0x1c <= ficha(owner)+0x1b1`.
  - tipo 5: `FUN_140400330()` (ningún miembro absorbido sigue en la tabla de amigos) y `!0x142a10504`.
  - por forma: `FUN_1403b6cb0(cmd, owner+0xde0)` = bit `form` de la máscara `cmd+0x2c` (datos de la tabla de comandos).
- Ejecución: `PlayerExec 0x1403a89a0(obj)` -> `ExecCommand 0x1403d6fc0(obj, obj->+0xdd0 +8)` -> `tipo+0x138` (SORA vtable
  0x1405cba28 +0x138 = 0x140404fd0) -> `0x140405440(obj, cmdEntry)`:
  ```
  case 4 (Drive):  form = AntiFormRoll 0x140421d60(obj, cmd+4 /*forma pedida*/);
                   DriveSpend 0x1403c2780(obj+0x5c0, FormData 0x1403b6920(form)+0x1c /*coste*/);   // ficha del objeto
                   0x1403e69a0(form)  // contador de usos 0x1409ad884[form] (estadística de la partida)
                   FormChangeRequest 0x14040ce60(obj, form)
  case 5 (Revert): FormChangeRequest(obj, 0); obj+0x120 |= 0x100
  ```
- Anti/Final forzada `0x140421d60(obj, form)`: contador anti en datos de partida (`0x1403a1410(0x1409abda0, 6)+4`, global),
  `0x142ae5760` (parámetros), RNG `0x140783ca0`, flag de historia `0x14039a980(0x4842)`, `FUN_1403d7470(obj)`, coste Final contra
  `obj+0x5c0 +0x1b1` (del objeto). Devuelve 6 (Anti), 5 (Final forzada) o la pedida. Global solo el contador (bien compartido).

### 1.2 FormChangeRequest 0x14040ce60(obj, form)
```
if (form == g_Form /*0x1409acdd4*/) return;              // !! global
ctx = new 0x38; FormCtxCtor 0x14040caa0(ctx, obj, form, 0)
if (form == 0) ctx+0x30 = 1                               // revertir: al final restaura absorbidos
else { SetAction 0x1403b4da0(obj,0); motion 0x1403c86a0(obj+0x158, 0x34); obj+0x20 = obj+0x28 = 0;
       AbsorbParty 0x140400b20(form) }                    // !! tabla de amigos global
obj+0x120 |= 0x100; g_Form = form
```
Otros que crean ctx: `0x14040cd40(obj)` = ctx con `g_Form` (lo usa 0x140404a40 si `obj+0xde0 == 0xc`), `0x14040cd80(obj)` =
`g_Form = 0` + ctx forma 0 (revertir forzado). Matar la tarea: `0x14040cdd0` (desde 0x1403bf8f0, eventos).

### 1.3 Contexto (0x38 B) — FormCtxCtor 0x14040caa0
| off | contenido |
|---|---|
| +0x00 | objeto Sora que cambia (viejo) |
| +0x08 | int forma destino |
| +0x0c | objentry id (0 -> `SoraObjId 0x140405190(form)` = `0x142a252f0 + 0x16 + form*2`) |
| +0x10 | flags: 1 = `FUN_1403ba9a0(obj)` (estado especial: aire/acción), 2 = restaurar pose (+0xe0..+0x100, +0x20), 4 = referencias movidas, 8 = objeto nuevo creado |
| +0x18 | efecto de pantalla (`0x1403f6f80(0)`, lista global 0x142ae76c0) |
| +0x20 | **ficha de estado retenida** (solo forma 1..6): `0x1403c04c0(1)` -> `0x1403c0620(1, ?)` -> **`StatusSlotFor(1)`** (refcount++) |
| +0x28 | tarea (gestor 0x142a17180, prioridad 1, pila 0x2000, fn 0x14040cf10, finalizador task+0x68 = 0x14040cf20) |
| +0x30 | byte "revertir" |

El ctor además: si ya hay ctx `0x142ae9fa8` **mata su tarea** (`0x14014f680(0x142a17180, old+0x28)`); `0x142ae9fa8 = ctx`;
`0x142a10504 |= 2`; si hay ficha, añade tarea `0x14040ce00` (cada frame `+6 HP` `0x1403c0c30(ctx+0x20, 6)` y `+6 PM`
`0x1403c0cd0(...,6)` a esa ficha mientras dura; lee el ctx por el global `0x142ae9fa8`).

### 1.4 FormChangeTask 0x14040d000(ctx, task) (corrutina; espera con 0x140150060)
1. Si forma 1..6: `0x142a10508 |= 1`; efecto de pantalla (ctx+0x18, `0x1403f76e0` color 0x30); `0x140150080(task, t)`;
   filtro de pantalla completa `0x142ae7c88` (0x1403f7850/0x1403f7880 -> 0x14015f1d0/f160/f1e0/f1a0/f170(1));
   **recursos**: `0x14039e340(task)` (esperar cargas), **`0x14039cca0(5000)`** (envejece recursos con prioridad >= 5000 = el
   modelo de forma anterior), **`0x1403e0630(form)`** = `0x1403e0660(SoraObjId(form), 5000, 4)` (carga modelo de forma + armas en
   el grupo 4 = jugador), `0x14039d460(task)` (GC + esperar). Espera mientras el objeto viva, el motion no acabe
   (`0x1403c7e70(obj+0x158)`) y `0x140400840()` (algún miembro de la tabla de amigos "ocupado").
2. Guarda pose (`obj+0xe0..+0x100`, `obj+0x20`). Si `obj+0xdc8` -> `LockOnReset 0x1403dd5a0`, `obj+0xdc8 = 0`.
3. **Destroy**: `tipo+0x38` (llamada en 0x14040d250, retorno 0x14040d254) = SORA `0x140404bb0`:
   `g_Sora 0x142ae9a28 = 0`; `0x1403a8f30(obj)`: **si g_Player != 0**: `g_Player = 0`, `CamTarget 0x140718cb0 = 0` (0x140164cf0),
   `0x142a10420 = 0` (0x1403a52d0), `FieldCmdUnbind 0x1403fd610(obj+0xdc0)` (=> `g_CurFieldCommand 0x142ae80a0 = 0`,
   `HudClearSlot(0)` 0x140168970 si el dueño tiene ficha, Term, +0xc38=0), `obj+0xdc0 = 0`, efecto `obj+0xdf8` (0x1402be8a0);
   siempre `0x1403d6770(obj)` (destroy genérico; escribe la ficha a la partida `0x1403c2120`).
4. **Crear**: `alloc 0xe18`; **`SORA ctor 0x140405030(mem, 0x1403dfeb0(ctx+0xc), form, *(old+0xdb8) /*pad*/, pos(old), yaw old+0x68c)`**
   — llamada en **0x14040d2ac, dirección de retorno 0x14040d2b1**. `ctx+0x10 |= 8`.
5. Copia `+0xd1c/+0xd20` viejo->nuevo; `nuevo+0xe10` = 1 (animación de entrada), 2 (revertir con pose restaurada, si
   `ctx+0x10&1` y forma 0: `tipo+0x20`...), 3 (si `old+0x9b8 bit2`).
6. Si ctx+0x20: **`FormGaugeStart 0x1403c17c0(ficha, form, nivel)`** (nivel = `0x1403a1410(0x1409abda0, form)+2`, datos de partida
   de la forma): `+0x1af = 2`, `+0x1b3 = nivel+3`, `+0x1b4 = +0x1b8 = (nivel+3)*k` (Anti: nivel `0x142ae5760+0x8c - 2`), y repone
   HP/PM; `0x1403c07e0(ficha)` (refcount--). `ctx+0x20 = 0`.
7. **Mover referencias** viejo->nuevo: `0x1403bd2e0` (dueños +0xbf8 y tabla 0x142a163f0), `0x1403ab070` (lista 0x142a112b8, +0x10),
   `0x1402c4480` (efectos: contenedores 0x140b63608, +0x50), `0x1403d06e0` (lista 0x142a24b40, +0x10), `0x1403ac320` (0x142a11478+0x50),
   `0x1403e49b0` (objetos con +0x5d8==2, +0x600), `0x14053dae0` (+0x6a0); **`0x1401b7f70`** (re-enlaza 0x140ad6a18 = {g_Player,
   amigo1, amigo2} y cerebros de amigos; `amigo+0xe0c` = índice). `ctx+0x10 |= 4`.
8. Espera a que el viejo salga de la lista (`0x1403bfb70`). Si revertir (`ctx+0x30`): GC `0x14039d460`, espera `0x140400c50`.
9. Finalizador `0x14040cf20` (también `0x14040cc00`): quita el filtro (`0x1403f7900`), libera ficha si quedó, **si revertir:
   `RestoreParty 0x140400440()`**, `0x142a10504 &= ~2`, si no se creó objeto `0x142a10508 &= ~1`, `0x142ae9fa8 = 0`, free ctx.

### 1.5 Fin del indicador / Anti
- `SORA tipo+0x20 0x140404a40(type, obj)` (= 0x140405320): tras `PlayerExec`, si `!(obj+0x9b8&4)`, `obj+0xd48 == 0`,
  `!(obj+0x18c & 0x4000)`, ficha y **`ficha+0x1b4 <= 0`**:
  - en forma (`0x1403aa4f0`: charId 1 y `obj+0xde0` en 1..6) y cmd 5 usable -> **`0x14040ce60(obj, 0)`**;
  - `obj+0xde0 == 0xc` -> `0x14040cd40(obj)` (ctx con `g_Form`);
  - si no, `0x142a24edc == 2` (invocación) -> cmd 0x13c -> `0x1403de010` (fin de invocación; mismo indicador +0x1b4).
- Consumo del indicador de forma: por golpe `0x1403a7480 / 0x1403a8800` (según `ficha+0x1af`: 1 = carga Drive
  `0x1403a9cf0 -> 0x1403c0ad0` (+0x1b0 décimas, +0x1b1 barras, máx +0x1b2), 2 = forma `0x1403c0b50(ficha, -x)`, 3 = invocación),
  y por tiempo en el update del jugador `0x1403a92c0 -> 0x1403c0b50`. Anti (`dueño+0xde0 == 6`) invierte el signo. Todo vía
  `obj+0x5c0` => **por ficha / por objeto**.

## 2. Absorción y restauración del grupo

- **Tabla de amigos** `0x142a239b0[2]` (acceso `0x1403c3270(i)`, búsqueda por charId `0x1403c3280(id)`). La escribe el ctor de
  kn::Friend 0x1403c2db0 (primer hueco libre), la limpia el destroy de amigo `0x1401b0060 -> 0x1403c3470` / `0x1403c2870` y la
  carga de zona `0x1403c3390`. **El SORA no se registra** => el Sora P2 nunca está ahí.
- **Qué hueco toma cada forma: no hay tabla de datos**, está codificado por charId (2 Donald, 3 Goofy) en tres funciones iguales:
  `AbsorbParty 0x140400b20(form)`, `DriveRequirement 0x1404005e0(cmd)` y `IsAbsorbedByForm 0x140400410(form, charId)`:
  Valor(1) -> charId 3 (Goofy); Sabiduría(2) -> charId 2 (Donald); Límite(3) -> nadie; Maestra/Final/Anti(4,5,6) -> todos.
- `AbsorbParty(form)`: para i = 0..1 de la tabla, si el miembro aplica -> `AbsorbOne 0x140400990(m, t)`:
  `m2 = 0x1403c3280(charId(m))`; si vive: fundido `0x1403b7810(m2+0xa08, 1, 0, t)` y **`m2+0x9b8 |= 0x10`** (absorbido; el amigo
  se elimina solo tras el fundido — inferido: Revertir solo es usable cuando `0x140400330` ya no lo encuentra en la tabla);
  añade `idx = 0x1403e31c0(charId)` (1->0, 2->1, 3->2, 0xe->0, otro->3) a **`0x142ae9788[]`, cuenta `0x142ae9790`**.
  Variantes: `0x140400a30(t)` (todos; invocaciones 0x1403de460, 0x1403fcec0), `0x140400bb0(charId, t)` (fábrica de amigos
  0x1403c2f00: al cargar zona no crea al amigo si la forma `g_Form` / invocación `0x1409acdd5` lo tiene absorbido), Límites
  `0x1403d8b40 -> 0x140400990`. Vaciar lista `0x140400830`.
- `RestoreParty 0x140400440()` (al terminar Revertir, fin de invocación 0x1403de080, 0x1403d8f40, 0x140414dd0, 0x1404157d0):
  para cada idx de la lista, si **g_Player** vive, busca el hueco s=1..2 de la config de grupo
  (`0x1409acde4[mundo 0x140717008]` bytes &0x1f; en vivo mundo 0x12: `00 02 01 03` => hueco1 = Goofy, hueco2 = Donald) con ese idx
  y crea **un kn::Friend nuevo** `0x1403c2fc0(objentry 0x142a252f0+0x10+idx*2, …)` (alloc 0xfa8 + `0x1401b06e0`) **junto a
  g_Player**, con fundido de entrada. Luego `0x142ae9790 = 0`.
- Con P2 en el hueco de Goofy:
  - P1 en forma: la absorción recorre la tabla y **no ve a P2** => nunca lo oculta ni lo destruye. Maestra/Final solo absorben a
    Donald; Valor a nadie. Restaurar no crea Goofy (nunca se registró).
  - Pero `DriveRequirement 0x1404005e0` devuelve 2 (no usable) si falta un miembro requerido (hueco vacío => charId 0) —
    Valor (Goofy), Maestra/Final/Anti (los dos) — salvo `0x1403a3990() > 1.0` (multiplicador del área `*0x142a0ff68+0x6a8`) o Límite.
    => en coop P1 pierde Valor/Maestra/Final.
  - Para excluir a un miembro: no hay dato; hay que enganchar `0x1404005e0` (requisito) y/o `0x140400b20` (absorción) (y
    `0x140400410` si se quiere que el amigo aparezca al cargar zona con P1 en forma).

## 3. Globales de jugador único que tocan fusión / absorción / indicadores

| dirección | qué es | qué hace la fusión de P2 a P1 (sin arreglos) |
|---|---|---|
| 0x1409acdd4 `g_Form` (byte, datos de partida) | forma actual "del jugador" | P2 lo compara/escribe: con P1 normal, P2 no puede **revertir** (0 == g_Form => no-op cada frame con el indicador vacío); el Drive de P2 deja g_Form = forma de P2 => P1 aparecería en esa forma al cargar zona (`SpawnObject 0x1403df930`/`0x1403dfeb0(0x236/0x23b)`/precarga `0x1403e0460`), la fábrica de amigos 0x1403c2f00 suprimiría amigos, `0x140419290` (objentry 0x23c por forma) |
| 0x142ae9fa8 | ctx de fusión actual | ctor nuevo mata la tarea anterior; la tarea de curación 0x14040ce00 lee el ctx por aquí |
| 0x142a10504 bit1 | tarea de fusión viva | bloquea Drive/Revertir/submenú (y otros comandos 0x37/0x7f, 0x51…) de **ambos** menús mientras dura |
| 0x142a10508 bit0 | "hay un Sora en forma" (lo ponen 0x14040d000 y 0x140405630; lo quita el destroy de un Sora en forma `SORA tipo+0x30 0x140404b70`/0x1404052e0 y el finalizador) | P2 en forma => P1 no puede fusionarse ni invocar (y viceversa); si P2 revierte/desaparece lo limpia aunque P1 siga en forma |
| 0x142ae9788[] / 0x142ae9790 | miembros absorbidos | compartida: la de P2 la restauraría Revertir de cualquiera |
| 0x142a239b0[2] | tabla de amigos (absorción / requisitos) | P2 no está: ni absorbible ni cuenta como requisito |
| 0x142a105d0 g_Player | | ctor del SORA de P2 lo pisa; destroy del P2 con g_Player != 0 lo pone a 0; RestoreParty aparece junto a g_Player |
| 0x142ae9a28 g_Sora | | ctor lo pisa; destroy SORA 0x140404bb0 lo pone a 0 **siempre** |
| 0x140718cb0 objetivo de cámara (+ bloque 0x140718c60..: 0x140718ca8 modo, 0x140718ca4, `0x140165a70(&0x140718c60)` reset, `0x14019dda0(0)`) | `CamBind 0x140164c60` desde `0x1403aa850` | el ctor de P2 **resetea el controlador de cámara de P1** además de cambiar el objetivo; destroy lo pone a 0 |
| 0x142a10420 | "puntero de control" (0x1403a52d0) | ctor -> P2; destroy -> 0 |
| 0x142a10620 FIELD_COMMAND de P1 (+0xae0, +0xc20, +0xc38) y 0x142ae80a0 g_CurFieldCommand | `0x1403aa850 -> FieldCmdBind 0x1403fd830` | el ctor de P2 re-liga el menú de P1 a P2; destroy hace Unbind del menú de `obj+0xdc0` (fc2) **pero** pone g_CurFieldCommand = 0 y HudClearSlot(0) |
| 0x140abcfe8[0] hueco HUD 0 | HudSetSlot(0) 0x140168850 en Bind / HudClearSlot(0) 0x140168970 en Unbind | P2 en el hueco del jugador / hueco vaciado |
| dk::GAUGE_PLAYER 0x140abd248 +0x40 | jugador del indicador | (el mod ya lo fuerza a P1; el de P2 lo re-liga hud::Frame a `P2Sora()`) |
| fichas: StatusSlotFor 0x1403c04e0(1) | ficha por charId (1ª por orden de pool) | **devuelve la de P2 o la de P1 según el orden** (ver §4) |
| 0x1409abda0 + 0x1038..0x1043 (`ficha+0x26c` raíz de partida) | Drive guardado: +0x1b0 décimas, +0x1b1 barras, +0x1b2 máx, +0x1b3 nivel forma, +0x1b4/+0x1b8 indicador forma | `0x1403c2120` (escribir ficha) / `0x1403c1d90` (leer) de **cualquier** ficha charId 1/0xe lo comparten; también `0x142a23960 = ficha+8` |
| Datos de personaje Sora `0x1403a14e0(0x1409abda0, 1)` (`ficha+0x250`) | HP/PM guardados (+4..+7, +0xc/+0xd) | la ficha de P2 apunta al de Sora: se mezclan en recargas de zona (último escritor gana) |
| `0x1403a1410(0x1409abda0, form)` (`ficha+0x254`) | nivel/EXP de forma | compartido (progresión común, aceptable) |
| 0x1409ad884[form] | contador de usos | estadística común (OK) |
| contador Anti `0x1403a1410(save,6)+4`, RNG 0x140783ca0 | | común (OK) |
| recursos: lista 0x1429f33c8, prioridad 5000 = modelo de forma | `0x14039cca0(5000)`, `0x1403e0660(..,5000,4)`, GC `0x14039d460` | una 2ª forma simultánea envejecería/liberaría el modelo de la forma de P1 |
| 0x142ae7c88 filtro de pantalla, 0x142ae76c0 lista de fundidos | efectos visuales de la fusión | pantalla completa (las dos mitades) |
| 0x142a114a0 + i*0x40 (vía 0x1403ad480) | override de recurso 0xc44f en Anti (SORA ctor: forma 6 -> 0x1403ad610; otra -> `0x1403ad5e0(0x1d)` lo borra) | todo ctor de SORA de P2 (aparición o fusión) borra el de un P1 en Anti |
| 0x142a24edc, 0x1409acdd5 | invocación activa | comparten el indicador +0x1b4 y la lista de absorbidos |

## 4. Fichas y estadísticas durante una forma

- No hay charId distinto por forma: el objentry de cada forma (0x55 Valor, 0x56 Sabiduría, 0x95d Límite, 0x57 Maestra, 0x58 Final,
  0x59 Anti; tabla 0x142a252f0+0x16+form*2, leída en vivo) da charId 1 (`objentry+0x4c`). El ctor de PLAYER hace
  `ficha = 0x1403c0620(charId=1, form)`: `StatusSlotFor(1)` (reutiliza la ficha con charId 1: refcount +0x264++ y
  `0x1403c2120` escritura a la partida), `+0x26c = handle(0x1409abda0)` (raíz Drive), `+0x254 = handle(registro de la forma)`,
  `0x1403c1d90(ficha, 0)` (re-inicializa stats desde la partida y el registro de forma), `+0x268 = obj`, `+0x1af = 0` (SORA ctor
  luego 1 normal / 3 invocación / 4).
- La ficha **sobrevive al cambio** porque ctx+0x20 tiene una referencia (`0x1403c04c0(1)`), y el nuevo Sora la recupera por charId.
- Drive (barras) y forma (indicador) viven **en la ficha** (+0x1af..+0x1c0) => por objeto en tiempo de ejecución; se sincronizan con
  la raíz de partida global solo en `0x1403c2120`/`0x1403c1d90` (reutilización de ficha, destroy de objeto, carga de zona).
  - Gasto: `0x1403c2780(ficha, coste)` (+0x1b1 -= coste). Inicio de forma: `0x1403c17c0`. Fin: `+0x1b4 <= 0` => revertir.
- Con la ficha propia de P2 (charId 1 re-etiquetado): **ambas fichas responden a StatusSlotFor(1)** -> el bug de §0. Arreglo: que el
  hook de StatusSlotFor oculte la ficha del *otro* jugador durante la búsqueda (ver §5.1 paso 1).

## 5. Recomendación

### 5.1 (a) Fusión nativa para P2: la misma tarea del juego, con contexto P2
Ningún parche de código del juego; solo ganchos con "contexto P2" (como cmdmenu/magic):

1. **StatusSlotFor 0x1403c04e0 (hook existente)** — para `charId == 1` con P2 vivo: ocultar temporalmente la ficha del otro
   jugador (`otra+0x260 = 0`, llamar al original, restaurar). "Contexto P2" = `g_inP2Update` (ObjUpdate de P2: aquí corren
   `0x14040caa0` y su `0x1403c04c0(1)`) o `g_inP2FormCtor` (paso 3). Fuera de él se oculta la de P2 => **arregla también el
   Drive de P1**. Se mantiene la vía original de reutilización (refcount, 0x1403c2120, 0x1403c1d90).
2. **Forma actual por jugador**: el mod guarda `g_p2Form` (byte) e intercambia `0x1409acdd4 <-> g_p2Form` en la rama P2 de
   `h_ObjUpdate` (junto a SwapPads/cmdmenu::Enter). Ahí se ejecutan Drive/Revertir (`0x140405440`), el revertir automático
   (`0x140404a40`) y `0x14040cd40`. La tarea `0x14040d000` usa `ctx+8` (no g_Form), así que no hace falta más. Las cargas de zona
   ven solo la de P1. Al cargar zona `g_p2Form = 0` (P2 aparece normal: `PartySlotObjId` devuelve `SoraObjId(0)`), o devolver
   `SoraObjId(g_p2Form)` si se quiere conservar la forma.
3. **SORA ctor 0x140405030 (hook existente)** — nueva rama "fusión de P2":
   `_ReturnAddress() == 0x14040d2b1 && (ctx = *(void**)0x142ae9fa8) && ctx[0] == g_p2Sora`.
   - `g_inP2FormCtor = true` durante el original (ficha de P2 por el paso 1).
   - Mejor que reparar después: **gancho en `PlayerBindGlobals 0x1403aa850(obj)`** (cámara 0x140164c60, 0x142a10420,
     `obj+0xdc0`, FieldCmdBind) que, si `obj` es un Sora de P2 (aparición o fusión), **no llame al original** => no se resetea la
     cámara de P1 ni se re-liga su menú/hueco HUD. (También lo llaman 0x1403a8140/0x1403aab40 con objetos que no son P2.)
   - Tras el original: restaurar solo `g_Player 0x142a105d0` (PLAYER ctor) y `g_Sora 0x142ae9a28` (SORA ctor) a P1; copiar del
     viejo `+0x9e8/+0x9f0` (grupo/entrada de aparición; necesarios para SoraToGoofy/SpawnLike); `cmdmenu::BindP2(new)`
     (en una fusión basta Init sin `FieldCmdReset 0x1403fd760`, como hace el juego con P1); `g_p2Sora = new`. El pad viene de
     `old+0xdb8` (el ctor lo recibe). El GAUGE_PLAYER de P2 se re-liga solo (hud::Frame compara con `P2Sora()`); la ficha es la
     misma. Los movedores de referencias y `0x1401b7f70` los ejecuta la propia tarea.
   - Sustituye el bucle `RepairP1Pointers` de 120 frames para los ctor de P2 (si se engancha 0x1403aa850 también en la aparición).
4. **SORA destroy 0x140404bb0 (hook nuevo, `(type, obj)`)** — si `obj == g_p2Sora`: no tocar g_Sora; reproducir 0x1403a8f30 sin
   globales: `cmdmenu::UnbindP2(obj)` (Unbind de fc2 sin g_CurFieldCommand ni HUD 0), si `obj+0xdf8` -> `0x1402be8a0`, `obj+0xdf8 = 0`;
   y `0x1403d6770(obj)`. Vale para fusión, cambio Goofy<->Sora (quita el truco `g_Player = 0`) y descarga de zona.
5. **Grupo**: P2 no está en la tabla de amigos, así que:
   - `AbsorbParty 0x140400b20(form)` (hook nuevo): en contexto P2 -> no hacer nada (las formas de P2 no absorben a Donald) — o
     dejarlo si se quiere que Sabiduría/Maestra/Final de P2 absorban a Donald (RestoreParty lo devolvería junto a P1).
   - `DriveRequirement 0x1404005e0(cmd)` (hook nuevo): en contexto P2 -> 0 (sin requisito, coherente con no absorber).
6. **Serialización**: dejar los cerrojos globales => una sola forma a la vez entre P1 y P2 (comportamiento nativo: el submenú se
   vacía para el otro). Es lo que permite reusar ctx único, lista de absorbidos y el modelo de forma único sin riesgo.

### 5.2 (b) Que las formas de P1 no absorban a P2 (y sigan disponibles)
- Ya no lo absorben (P2 fuera de `0x142a239b0`) — mantener eso: **no** registrar nunca al Sora P2 en la tabla de amigos.
- Requisito: hook `0x1404005e0` en contexto P1 con P2 presente: considerar cubierto el miembro que P2 sustituye (Goofy, charId 3,
  hueco 1 de la config `0x1409acde4`): Valor usable si P2 vive; Maestra/Final/Anti si además está Donald. La absorción seguirá
  tocando solo a Donald. Si se quiere que Valor "absorba" a P2: no recomendado (destruiría a P2: RestoreParty crea kn::Friend).
- Fichas: paso 5.1-1 (imprescindible ya hoy).

### 5.3 Detección en el hook del ctor de SORA
| origen | condición |
|---|---|
| aparición de P2 (zona / Goofy->Sora) | `g_pendingP2` (PartySlotObjId) como ahora; o retorno dentro de PlayerFactory 0x1403a7cb0 + `g_pendingP2` |
| fusión/reversión de P2 | retorno `0x14040d2b1` y `(*(uintptr_t**)0x142ae9fa8)[0] == g_p2Sora` |
| fusión de P1 | retorno `0x14040d2b1` y ctx[0] == g_Player (rama original, sin cambios) |

## 6. Riesgos / preguntas abiertas
- **Dos formas simultáneas** (P1 y P2) requieren: `0x142a10508 bit0` por jugador (intercambio en update de P2 + en la tarea y en
  0x140405630 / 0x140404b70 / finalizador), lista de absorbidos por jugador, y sobre todo **dos modelos de forma residentes**:
  `0x14039cca0(5000)` + GC liberarían el modelo de la forma del otro. Sin verificar presupuesto de memoria. Recomendado: fase 1 =
  una forma a la vez.
- Cámara/efectos de la transformación: el motion 0x34/0x35 y sus scripts podrían usar g_Player o el objetivo de cámara; no
  verificado. El filtro 0x142ae7c88 y el fundido son de pantalla completa (afectan a las dos mitades).
- `0x142a10504` bloquea también el menú de P1 mientras P2 se transforma (y viceversa), y comandos 0x37/0x7f/0x51.
- Mezcla de datos guardados: la ficha de P2 tiene `+0x250` = registro de Sora y `+0x26c` = raíz Drive de la partida; los
  `0x1403c2120` de cualquiera escriben ahí. En una fusión el orden escribir->leer conserva los valores de cada uno, pero en recargas de
  zona el último en escribir gana (P1 podría heredar HP/PM/Drive de P2). Separarlo del todo exige registros propios de P2 con
  handle válido (memoria del heap del juego 0x140152430, en un banco de 0x142b0d720) y re-apuntar `+0x250/+0x26c` tras cada
  `0x1403c0620` (que los sobrescribe para charId 1).
- `0x142a114a0` (override Anti): los ctor de SORA de P2 lo borran; con P1 en Anti, la aparición de P2 lo rompería.
- Si P2 cambia de zona en forma: aparece normal, la ficha nueva lee el indicador de la raíz común; `0x142a10508` debería
  limpiarse en el destroy (0x140404b70 es `SORA tipo+0x30`; confirmar que la descarga de zona lo llama).
- `0x1403c04c0(1)` en el ctx ctor: el 2º argumento (form para `+0x254`) va en EDX sin inicializar explícitamente; sin efecto
  visible, sin confirmar.
- Formas 7, 10, 0xc (objentry 0x318, 0x5f9, 0x5fa): rama `0x14040cd40` para 0xc; no identificadas.
- El amigo absorbido se autodestruye tras el fundido (`+0x9b8 & 0x10`, alpha +0xa08): inferido de `0x140400330`/RestoreParty, no
  localizado el código exacto.
- Todo estático salvo: orden de fichas (P2 …2e30 antes que P1 …3598), tabla de amigos `[0]=0 [1]=Donald`, config de grupo
  `00 02 01 03`, `g_Form = 0`, tabla de objentries de forma.
