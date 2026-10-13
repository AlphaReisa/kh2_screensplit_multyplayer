# Notas de ingeniería inversa — KH2FM Steam (ImageBase 0x140000000)

Herramienta: `research/q.sh cmds.txt out.txt` (Ghidra headless, comandos `dec|dis|xref|callers <addr>`).
Volcados: `strings.tsv`, `imports.tsv`, `vtables.tsv` (RTTI completo, 2373 vtables).

## Jerarquía de objetos (RTTI, namespace YS)
OBJ → STDOBJ → BTLOBJ → PARTY → PLAYER → SORA  (también MICKEY, TRPLAYER, NMPLAYER, LMPLAYER…)
PARTY → FRIEND (Donald/Goofy/aliados), IA en `kn::Friend`. SUMMON también deriva de FRIEND.
Cada clase tiene un objeto "tipo" estático `YS::<NIVEL>::VTABLE<T>`.

## Jugador (suposición de jugador único)
- `PLAYER::ctor` = 0x1403a7a40 → `g_Player = this` en **0x142a105d0** (176 referencias en el exe).
  - this+0x5C0 (`[0x170]` dwords) = puntero a ficha de batalla (FUN_1403c0620)
  - this+0xDE0 (`[0x378]`) = tipo/forma de jugador (param_3)
- `SORA::ctor` = 0x140405030 → `g_Sora = this` en **0x142ae9a28** (3 refs).
- Otros escritores de g_Player: 0x1403a8140, 0x1403a7120, 0x1403a80e0, 0x1403a8f30 (cambios de forma/dtor?).

## Cámara
- Array de cámaras (0x80 bytes c/u): 0x140ac1020 (activo) / 0x140ac1b50 (copia), índice **0x140ac1838**.
  - `GetCamera(char)` 0x14019ce40, `GetCamIndex` 0x14019ce80, `SetCamIndex` 0x14019ddd0, `SetDualFlag` 0x14019ddc0 (flag 0x140ac183c)
  - Proyección: banco 0x140ac1840 / 0x140ac1220, stride 0x184 → `GetProj(char)` 0x14019cfc0
  - Init cámaras: 0x14019d050 (inicializa índice 0 y 1)
  - Update/commit de cámara activa: 0x14019cba0
- `kn::Camera` vtable 0x1405b26d8.

## Render: lista de tareas con prioridad
`AddTask(queue, prio, fn)` = 0x1401506b0.
- **Modo simple** (cola 3): 0x140162f20 — 6000 setup, 0x1771 prep, 30000 cam commit, 41000, 43000 **DrawWorld 0x140170450**, 49999, 50000, 0xea61, 65000, 71000.
- **Modo doble cámara** (cola 5): 0x140162c50 — mismo pipeline 2 veces; entre medias
  `0x140163520 = SetCamIndex(0)`, `0x140163530 = SetCamIndex(1)`, 90000 captura framebuffer (0x140153430),
  0x16012 composición (0x140172120). Se usa para **fundido entre cámaras** (controlador 0x140163110,
  salida a modo simple 0x1401631e0, alpha en 0x140abcc50/54).
  → Base para split-screen: forzar modo 5, cámara 1 siguiendo a P2, y sustituir la mezcla por viewport izq/der.
- DrawWorld 0x140170450: mapa (0x1401a5490 con GetCamera(1)/GetProj(1)) + objetos 0x140188770 (ordenados por profundidad).

## Entrada
- XInputGetState usado en 0x140133970, 0x140134d10. DirectInput8Create en 0x1401335b0.
- kh2lib: `Input = 0xBF31A0`, `Cntrl = 0x2A171E8` (addr relativas a base).

## Render API
- Importa d3d11 y d3d12 (D3D11CreateDevice 0x14013ef00, D3D12CreateDevice 0x14010e860). Capa que traduce GS de PS2 (strings GS_SET_SCISSOR, applyViewport, applyScissorRect 0x1401121d0).

## Confirmado en vivo (13-10-2026, `src/tools/kh2mem.py`)
- Backend gráfico: **DX12** (d3d12core.dll cargada).
- g_Player == g_Sora → objeto en pool estático del exe (ej. 0x142521cd0; varía según sala).
- Layout objeto YS::OBJ:
  - +0x00 u32 handle de tipo: `type = (base & ~0xFFFFFF) | (h & 0xFFFFFF)`; `*(type)` = vftable del objeto tipo
  - +0x10..0x6F matriz rot/escala (+0x30 = escala 1,1,1,1)
  - +0x70 float4 **posición** (x,y,z,1)
- Tipos vistos: Sora `VTABLE<YS::SORA>` 0x140750300, Donald/Goofy `kn::Friend::VTABLE<kn::Friend>` 0x14071d7c0,
  armas `VTABLE<YS::WEAPON>` 0x14074e4a8, SAVEPOINT 0x140752580, TREASURE_BOX 0x14074fec0.

## Split-screen (core v0.9, 13-10-2026)
Funciona: modo doble cámara (crossfade 0x140163440 con duración enorme + tick congelado), composición
recortada (SpriteBuild 0x14015f940: w/2, u1=mid -> captura = pase cam0 = mitad izquierda),
proyección fuera de eje por pase (+0x00 GS, +0x84/+0xc4/+0x104 clip: M[i][0] += k*M[i][3], k=∓0.5),
cámara 1 = cámara 0 trasladada (P2-P1) escrita en CamCommit(0x14019cba0) cuando idx==1,
índice de cámara a 0 tras la composición (la lógica escribe ranura 0).
Orden de pases en cola 5: pase A = cam1 (se dibuja, se captura en 90000), pase B = cam0; composite al final.

RESUELTO (v0.10) el escenario compartido: el mapa va por cadena DMA GS/VU que se interpreta al
INICIO de la siguiente sección (FlushDma 0x1401a0460(1) -> SetRendererCamIdx 0x140126b10(GetCamIndex)
-> intérprete DMA 0x1401aded0). El renderer de PC guarda su índice de cámara en renderer+0xe530
(renderer = 0x14011c150()). Como al inicio de sección el índice era 0, la cadena de ambos pases se
dibujaba con la cámara 0. Fix: hook FlushDma, poner kCamIndex = índice del último pase comprometido
mientras se ejecuta. No hacen falta ni las sombras del mapa ni el NOP de 0x1401a5337.
Personajes van por vía directa (por pase) y por eso siempre salieron bien.

Teclas debug core: F8 split, F7 girar cam1 180°, F6 prep con idx1, F5 traza MapDraw 1 frame,
F4 romper vista de MapDraw en pase idx (-1/0/1), F3/F2 watchpoints hardware ranura 0/1 con traza de pila,
F1 modos de composición, F9 flush DMA manual, F10 activar/desactivar fix de índice del renderer.

## Control P2 (core v0.11, 13-10-2026)
- Personajes: true pos obj+0x670 (se copia a +0x70 en BuildWorldMatrix 0x1403ccb90), dir/yaw obj+0x680 (yaw +0x68c),
  yaw objetivo +0x7a8 (gira a velocidad +0x134 en 0x1403b4780), acción actual handle obj+0xc (YS::ACTION_*).
- Cerebro IA: obj+0xdf8 -> kn::Goofy (vtable 0x1405b42d8) / kn::Donald. Goofy = 0x14253ca20 en esta sala.
- Interfaz de movimiento común: obj+0x10 vec4 = (dir.x, 0, dir.z, intensidad 0..1).
  MoveUpdate 0x1403d6870 -> yaw objetivo = atan2(dir). ACTION_IDLE (locomoción, vtable 0x1405cceb8):
  slot7 0x140422520 elige andar/correr por intensidad; slot8 0x140422350 velocidad = norm(dir)*w*speed.
  La IA reescribe +0x10 entre MoveUpdate y la acción -> se aplica la entrada P2 antes de las 3 funciones.
- Física: 0x1403b89a0 suma velocidades -> integrador con colisión 0x1403b9090.
- Entrada P2: XInputGetState(1) (mando 2) o teclas I/J/K/L; relativo a la cámara que muestra a P2.

## Botones P2 + IA (core v0.12)
- IA de compañero: FriendAI 0x1401b15f0(obj): comportamiento actual = puntero a función en obj+0xe00
  (lo que mueve/actúa). Mientras P2 controla se pone a NULL durante la llamada.
- Teletransporte de recuperación junto a Sora: 0x1401b7750 elige hueco (tabla 0x140ad6ae8), 0x1403b6100 teleporta.
  Para P2 -> devolver -1.
- Comandos: ExecCommand 0x1403d6fc0(obj, cmd{int16 id,...}); tabla en *0x142a161e8 (+4 count, +8 entradas 0x30,
  bsearch por id). id 1 = Atacar (tipo 1), 2 = Magia, 3 = Objeto, 4 = Fusión... Cola de la IA en obj+0xea8.
- Acciones: tabla de singletons 0x140750d30 (0 IDLE, 1 JUMP, 2 HANG, 3 DUMMY, 4 BLOW, 5 FLY, ...),
  GetAction 0x140409d20, SetAction 0x1403b4da0(obj, idx).
- Los objetos se recrean al cambiar de zona: g_p2 se valida cada frame (IsGoofy: tipo kn::Friend + cerebro
  kn::Goofy) y se re-escanea si no vale (v0.13).
- Mapeo P2: stick izq mover, A atacar, B saltar, Back devolver a IA. Teclado: IJKL, U, O, F12.

## Segundo Sora real (core v0.16-0.19)
- Spawn genérico: SpawnObject 0x1403df930(id,...) por tipo de objentry (+4): 0 jugador -> PlayerFactory 0x1403a7cb0
  (alloc 0xe18 + SORA ctor 0x140405030), 1 compañero -> 0x1403c2f00 (kn::Friend). Cambio de Forma = 0x14040d000 (recrea SORA).
- IDs simbólicos: 0x236 jugador (SoraObjId 0x140405190(forma)), 0x237/0x238 huecos de grupo ->
  PartySlotObjId 0x1403c32e0(slot) = FUN_1403e2e50(partyTable[slot]&0x1f). Hueco 1 = Goofy, 2 = Donald (aquí).
- Modo Sora P2 (F6, aplica al cargar zona): hueco de Goofy -> id de Sora => 2º YS::SORA por la vía normal.
- El ctor de SORA pisa g_Player 0x142a105d0, g_Sora 0x142ae9a28 y además: objetivo de cámara 0x140718cb0,
  controlador de jugador 0x142a11100 {obj, pad*,...}. Fix: snapshot de .data antes del ctor y restaurar todo qword
  que pase P1->P2 (y 120 frames después). F7 = arreglo manual de esos dos para un P2 ya existente.
- Mandos: el juego tiene 2 (GetPad 0x14039b780 = 0x140bf31a0 + i*0x68; raw 0x140bf3270 / 0x140bf32c8, +0x4c=1 conectado).
  PadUpdate 0x14039bf00. Pad: +0 cur, +8 pressed, +0x10 released, +0x18 repeat, +0x30 stick(x,y(-adelante),0,mag), +0x50 flags.
  Teclado: Espacio=0x01000108 (saltar), Enter=0x08000004 (confirmar), R=0x800 (R1), F=0x02000400.
- P2: se sintetiza el pad 1 (si la capa de PC no lo alimenta) y en ObjUpdate 0x1403bfd30 del Sora P2 se intercambian
  pad0/pad1. Atacar = ExecCommand(P2, cmd 1), una vez por pulsación (sin búfer; los rechazos eran rebotes contra barreras) (el ataque normal va por el menú de comandos de P1).

## Fichas de batalla, HUD y magia (v0.21-0.26)
- Fichas: pool 80 x 0x278 en 0x142a17290, libres en 0x142a23810 (cuenta 0x142a23950). StatusSlotFor 0x1403c04e0(charId)
  reutiliza la ficha del mismo personaje -> el 2º Sora compartía HP/PM. Fix: durante el ctor P2 pedir id 15 (libre) y
  reetiquetar (+0x260 = charId, +0x250 = handle datos guardados 0x1403a14e0(0x1409abda0,id)); init stats 0x1403c1d90(slot,0).
  Ficha: HP +0/+4, PM +0x180/+0x184, dueño +0x268. F7 aplica esto en caliente.
- HUD: dk::GAUGE_PLAYER (vt 0x1405b2100, 0x45e0 B, instancia 0x142578b20) muestra obj en +0x40 -> se fuerza a P1.
  Tareas 2D: Hud2DTask 0x1401667d0(task{+0x18 manager,+0x20 obj}). Clonar el gauge NO funciona (comparte sub-objetos,
  rompe el original, no se dibuja) -> HUD de P2 propio (overlay) pendiente.
- Objetos destruidos siguen en el pool: "vivo" = enlazado en la lista activa (cabeza 0x142a171c8, siguiente handle +0xa90).
- MAGIA: CastMagic 0x1403c5170 busca la magia en la tabla de 6 magias cargadas 0x142a23a80 (entradas 0x50, [9]->datos,
  id en +0x26); si no está -> NULL -> CRASH (14:55, ExecCommand(P2, 0x31)). Validar IDs antes de lanzar.

## Entrada nativa P2 y cambio en vivo Goofy <-> Sora (v0.35-0.36)
- Capa de entrada PC: gestor *0x14079cf00; InputUpdate 0x140105810 llena un puerto (dev+0x18+i*0x44) por GamePad
  (0x1408bb2a8+i*0x140, usuario XInput +0x10; GamePadRead 0x140134d10(gp, puerto)) y mueve el primer puerto con
  entrada al puerto 0. Puerto: +0 botones orden PS2, +2..+5 sticks, +0x1f estado (1 conectado, 2 no), +0x40 usuario.
- Steam Input crea usuarios XInput virtuales duplicados dentro del juego (un mando = 2 usuarios, que pueden ir
  desfasados unos frames). El reparto cambia entre arranques.
- Unión P2: LB+RB 1 s. Usuarios de P2 en reposo para la capa (hook XInputGetState); puerto 1 <- GamePadRead del
  mando P2. Vibración: el juego reescribe la vibración cada frame -> hook XInputSetState mientras dura la nuestra.
- Objetos: grupo de aparición +0x9e8, entrada +0x9f0 (id +0, pos +4, rot +0x10). SpawnFromEntry 0x1403fe590(entry,
  grupo); SpawnFromEntryAt 0x1403fe650(entry, grupo, pos). Destruir: tipo (handle en obj+0) vtable +0x38.
- Cambio de Forma 0x14040d000 = plantilla: destruir, crear, mover referencias (0x1403bd2e0, 0x1403ab070,
  0x1402c4480, 0x1403d06e0, 0x1403ac320, 0x1403e49b0, 0x14053dae0), re-enlazar tabla de grupo 0x1401b7f70.
- Tabla de grupo 0x142a239b0[2] (amigos; índice en obj+0xdd0): el ctor de Friend 0x1403c2db0 toma el primer hueco
  libre; el destroy de Friend (0x1403c3600) NO lo limpia (solo al cargar zona, 0x1403c3390).
- Destroy de SORA 0x140404bb0: g_Sora=0; 0x1403a8f30: si g_Player!=0 -> g_Player=0, objetivo cámara=0,
  0x142a10420=0, desmonta el controlador de jugador ÚNICO (obj+0xdc0 = 0x142a10620, 0x1403fd610) -> P1 sin control.
  Fix: destruir el Sora P2 con g_Player=0 y restaurar g_Player/g_Sora.
- Globales que el ctor de SORA enlaza: g_Player, g_Sora, 0x140718cb0, 0x140abcfe8 (tabla HUD [0]), 0x142a10420,
  controlador 0x142a10620 +0xae0/+0xc20/+0xc38 (dueño). Se restauran por lista explícita (no diff de .data: el diff
  restauró la cabeza de la lista de objetos 0x142a171c8 y colgó el juego).
- HUD: GAUGE_PLAYER +0x40 = jugador; tabla 0x140abcfe8[i] (0 jugador, 1-2 amigos) limpiada por 0x140168970(i).

## HUD de P2 (v0.37)
- HudCreate 0x14016c3a0 / HudDestroy 0x14016c2b0 sobre el gestor *0x140abcd20 (tareas +0, lista de objetos +8 con
  siguiente handle en obj+8, asignador +0x28: vfunc +8 alloc(size,0), +0x10 free). Piezas: SpriteImage 0x140abd240,
  GAUGE_PLAYER 0x140abd248 (ctor 0x14016b6f0, Init 0x14017f270(gauge, jugador) -> tarea 2D id 3), GAUGE_FRIEND
  0x140abd250/258, GAUGE_ENEMY 0x140abd260. HudDestroy las destruye con vtable +0x18(obj,1).
- Sprite (update 0x14016abf0): pantalla = layout + propio +0x1c8 + heredado +0x1c0 (reconstruido desde el padre
  +0x1e0; intacto si no hay padre). Las máscaras del anillo (SECTOR_MASK/CIRCLE_MASK, draw 0x14017e700) se colocan
  con el heredado +0x1c0 de su ancla -> mover el indicador con el +0x1c0 del sprite raíz (gauge+0x1d40).
- Ancho del HUD: 512 a 4:3 -> 683 a 16:9; media pantalla = 341.
- Unión: los duplicados de Steam se detectan por haber mantenido LB+RB >= 30 de los 60 frames.
- PENDIENTE: cierre al lanzar Piro (P1) con Sora P2 presente: VM de scripts 0x14041e2c0 pide la posición de un
  objeto (FUN_1402be970 -> +0x30 nulo, objeto ya destruido con 0x1402be8a0).

## Menú de comandos de P2 (v0.38) — ver research/command_menu.md
- El "controlador de jugador" 0x142a10620 ES el menú (YS::FIELD_COMMAND único). P2 tiene el suyo (ctor del juego),
  ligado a mano (P2+0xdc0/+0xdd0/+0xdc8). Contexto de dibujo de P2 = globales 0x140abc4e0..0x140abc510, intercambiados
  durante el update de P2, las tareas 2D de sus piezas (grupos 0x101/0x102) y el teardown.
- Liberación de entradas COMMAND_ONE (vtable[0] 0x14015b420) empuja al free list del pool *actual*: se hace en el
  contexto del pool al que pertenece la entrada (si no, corrompía el pool de P1 -> cierres con Objeto).
- Objetivos de objetos = id de personaje (obj+8 -> objentry +0x4c); PartyMemberById 0x1403d5c70 y PartyMember
  0x1403d5c50 (0 = g_Player) responden P2 en el contexto de P2.
- Movimiento: stick -> mundo con la cámara actual (0x1403a7fb0, GetCamera 0x14019ce40 = banco[índice]): durante el
  update de P2, la ranura 1 = cámara 2 guardada tras su commit e índice 1.
- PENDIENTE: scripts de magia/ataque consultan efectos ya muertos (0x14041e2c0 -> 0x1402be970, +0x30 = 0); la build
  debug lo esquiva y registra. Barra de PM de P2 se rellena sola.

## Pendiente
- [ ] Confirmar en vivo g_Player / g_Sora y la party (Donald/Goofy) en memoria
- [ ] Qué backend usa el juego (DX11 o DX12) en tu PC
- [ ] Probar forzar modo doble cámara (cola 5) sin fundido
