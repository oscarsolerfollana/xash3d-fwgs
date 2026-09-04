# Xash3D + Firearms — estado de la investigación

Última actualización: 2026-09-04 (ascensor de c1a0b CERRADO: no es de Xash;
frente nuevo: el salto desde escalera no hace nada, instrumentado y listo para probar)

## Resumen

**Bípode: resuelto.** Ver secciones 2 y 3. Arreglado, verificado por Oscar, reportado
(issue #2721) y con PR mandado (#2722), pendiente de revisión.

**Sprint: resuelto y verificado.** Ver sección 10. **Verificado por Oscar el 26/08:**
sprint de mantener bien, sin atascos, otros binds intactos y el bípode sigue cerrando.
Es un bug distinto y ya lo tenía reportado otra persona (#1371, de 2024, sin
diagnóstico). Queda decidir cómo llevarlo a upstream (probablemente: comentar la causa
raíz en el #1371).

**Lo siguiente que hay que hacer está en la sección 0.**

### El bípode, en una línea

El bípode de Firearms no cerraba el cono de dispersión en Xash3D. Causa: el motor
salta el limitador de ritmo para el cliente local de un listenserver, así que el mod
recibe casi tres veces el trabajo que espera. Había **dos** atajos de loopback, no uno:
uno en el servidor (snapshots) y otro en el cliente (paquetes de usercmd). Hacía falta
tapar los dos. **Arreglado y verificado por Oscar el 23/08 a las 19:13:** el bípode
funciona con `host_limitlocal` en 0 y en 1, sin tocar nada en la consola.

---

## 0. Por dónde seguir

**FRENTE ACTIVO (04/09): saltar desde una escalera no hace nada. Ver sección 20.**

En Xash, subido a una `func_ladder`, pulsar saltar no despega de la escalera. El
movimiento lo compila el mod (`PM_LadderMove` es código de Valve, idéntico en los dos
motores), así que la diferencia tiene que estar en lo que el motor le sirve. Sospecha
principal: `PM_TraceModel` devuelve el plano sin normal, y el salto es literalmente
`velocity = plane.normal * 270` → cero.

**SIGUIENTE PASO CONCRETO: lanzar `test_escalera_salto.bat`** (ya compilado e
instalado). Subirse a la escalera de `crossfire`, mirar de frente y pulsar saltar,
repetir de lado y de espaldas. Deja `escalera.log` con líneas `[ladder:SV]` /
`[ladder:CL]`. La tabla de la sección 20 dice qué significa cada caso.

---

**CERRADO (04/09): ascensor giratorio de c1a0b — NO es de Xash. Ver sección 19.**

Confirmado por Oscar: en GoldSrc de Steam el ascensor se comporta **igual que en
Xash**. La única variable era la `hl.dll` y el comportamiento la sigue a ella, no al
motor. El bug de subir y machacarte es del mapa (`height "-216"`) más el
reverse-on-block del SDK, y el teletransporte final es exclusivo de la DLL de WON de
2002, que Valve ya corrigió. Nada que arreglar en Xash. Queda quitar la
instrumentación `sv_trace_pushers` (sección 19).

**OJO, afecta a la sección 18:** Sierra (WON) y el proyecto (Steam) no tienen la
misma `hl.dll`, así que comparar Xash contra Sierra NO aísla el motor. Igualar la
DLL del mod antes de atribuirle nada a Xash — vale también para el `cstrike` de
las pruebas de armas.

---

**OTRO FRENTE ABIERTO (29/08): comparar armas de CS en Xash vs GoldSrc. Ver sección 18.**

Resumen de dónde quedó: la vía CS 1.3 + WON está MUERTA — el WON no corre bajo
gdb (se apaga el servidor, probado a fondo). Pivotado a **CS 1.6, que sí corre en
Steam** (idea de Oscar: mismo truco que dio el dato GoldSrc de Firearms, cliente
Xash instrumentado contra servidor de Steam, sin gdb). Montado `cs16/` y tres
lanzadores.

**SIGUIENTE PASO CONCRETO (mañana):** correr la **PIEZA 1**,
`test_cs16_xash_host.bat` — Xash de anfitrión, unirse a equipo, AK, protocolo de
ráfagas (cargador de pie / agachado / corriendo / saltando, pausas de 2-3 s).
Con eso confirmo que CS 1.6 arranca en Xash y que el evento de disparo se captura,
y saco la línea base de cadencia/cono por estado (como en 1.3). Log: `cs16_host.log`,
parser `parse_cs_log.py`. Luego **PIEZA 2** (`test_cs16_xash_vs_steam.bat`, contra
servidor LAN de CS 1.6 de Steam) para la referencia GoldSrc por `[wd:RX]`. La
PIEZA 3 (control todo-Xash) solo si 1 y 2 discrepan. Detalles y caveats: sección 18.

**Estado de la línea base Xash de CS 1.3 (ya medida, por si sirve de contraste):**
tabla en sección 18 (de pie/agachado ~0.03, corriendo ~0.07, saltando 0.13-0.32,
AK a 10/s). Conclusión indirecta ya cerrada: la dispersión de CS se calcula por
disparo, no acumulada entre snapshots → el bug clase-bípode NO la afecta. Lo de
1.6 es para tener también el número de GoldSrc empírico.

**PRs/issues (comprobado 29/08, sin novedad):** #2732 (welder) y #2730 (sprites
lerk) ABIERTOS esperando review de a1batross, nada por hacer. Fusionados: #2722,
#2726, #2729. #1371, #2728, evobot#24 en tejado ajeno.

---

### Cerrado hace tiempo (bípode y sprint)

**Bípode:** resuelto y fusionado (#2722). **Sprint:** diagnóstico + causa raíz
publicados en #1371, de acuerdo con a1batross en que el arreglo ingenuo rompería
otros binds; sin siguiente paso. **Prueba B (revertir el hunk del servidor):**
opcional, ya no la pide nadie — el #2722 se fusionó con los dos commits.

---

## 1. Pendiente de probar

- [x] ~~**Verificar el arreglo.**~~ Hecho el 23/08 19:13: funciona con
      `host_limitlocal` en 0 y en 1.
- [ ] **Buscar otras mecánicas de Firearms desajustadas** por la misma causa:
      recuperación de puntería al moverse, sangrado, fatiga, cualquier cosa con estado.
- [ ] **Decidir qué hacer con la instrumentación** (ver sección 5): quitarla,
      dejarla, o dejarla más contenida.
- [x] ~~**Reportar a upstream (FWGS)**~~ Issue **#2721** abierto el 24/08 a nombre de
      Oscar: https://github.com/FWGS/xash3d-fwgs/issues/2721
- [x] ~~**Mandar el PR**~~ **#2722** abierto el 24/08, dos commits. Ver sección 7.
- [x] ~~**Esperar revisión del #2722.**~~ **Aprobado por a1batross el 25/08** sin pedir
      quitar el commit del servidor. Pega menor del comentario corregida y empujada el
      26/08. Pendiente solo el merge (lo hace un mantenedor). La prueba B pasa a opcional.
- [x] ~~**Probar el arreglo del sprint**~~ **Verificado por Oscar el 26/08:** sprint
      de mantener funcionando, sin atascos, y el bípode sigue cerrando el cono.
- [ ] **Decidir si el arreglo del sprint se manda a upstream** y cómo. Hay un issue
      ajeno abierto desde 2024, el **#1371**, sin diagnóstico. Ver sección 10.
- [ ] **Opcional:** copiar los binarios arreglados a la instalación de la LAN
      (`C:\Users\Oscar\lan Party\hl\Datos juegos`). Ojo: tiene su propio lanzador
      (`Launcher.exe`, `hl.exe` renombrado); mirar cómo se arranca antes de pisar nada.

---

## 2. El bug

`engine/server/sv_frame.c`, en `SV_SendClientMessages`:

```c
if( !host_limitlocal.value && NET_IsLocalAddress( cl->netchan.remote_address ))
    SetBits( cl->flags, FCL_SEND_NET_MESSAGE );
```

Para el cliente **interno** del listenserver (`NA_LOOPBACK` exclusivamente; un cliente
que entre por 127.0.0.1 con UDP ya cuenta como remoto) fuerza el envío en cada frame,
saltándose el limitador `next_messagetime` y `cl_updaterate`.

Por qué rompe el mod: `pfnUpdateClientData` y `pfnGetWeaponData` se llaman una vez por
snapshot desde `SV_WriteClientdataToMessage`. A 60/s en vez de 20/s, el mod recibe casi
3 veces las llamadas que espera, y Firearms mete ahí lógica que **no** es idempotente
(el SDK de Half-Life sí lo es, con copias sombra tipo `m_iClientHealth`).

Efecto secundario medido: el cliente re-simula menos comandos en la predicción, porque
cada snapshot le resetea el estado. Se ve en `fuser2`, que partiendo de 3.992 en el
cable acaba en 3.974 con Xash3D y en 3.874 contra un servidor limitado.

### Alcance

| escenario | afectado |
|---|---|
| listenserver, el anfitrión | **sí** |
| listenserver, los que se conectan | no |
| servidor dedicado, cualquiera | no |
| un jugador | no |

La gravedad escala con los fps del anfitrión (en listenserver
`sv.frametime = host.frametime`). A 20 fps el ritmo saldría correcto por casualidad.

Afecta a cualquier mod con lógica de estado en `UpdateClientData`/`GetWeaponData`, no
solo a Firearms. La mayoría de mods derivados del SDK no lo notarán.

---

## 3. Los arreglos aplicados

Son **dos**, uno por cada atajo de loopback. Los dos hacen lo mismo: dejar el atajo
solo para un jugador, que es para lo que estaba pensado.

**3.a — servidor, `engine/server/sv_frame.c:849`** (snapshots que salen hacia el
cliente local):

```c
if( !host_limitlocal.value && svs.maxclients == 1 && NET_IsLocalAddress( ... ))
```

Con esto el anfitrión pasa de 60 snapshots/s a los que marque `cl_updaterate`
acotado por `sv_minupdaterate`/`sv_maxupdaterate` (en esta máquina,
`cl_updaterate 13` con `sv_minupdaterate 25` → **25/s**, no 20).

**3.b — cliente, `engine/client/cl_main.c:924`** (paquetes de usercmd que salen hacia
el servidor). Se quita el término de loopback entero, porque el `cl.maxclients == 1`
que ya había delante cubre el caso de un jugador:

```c
if( cl.maxclients == 1 || ( host.realtime >= cls.nextcmdtime && Netchan_CanPacket( ... )))
```

Sin esto, el anfitrión seguía mandando un paquete por frame (60/s) en vez de a
`cl_cmdrate` (30/s). **Este es el que faltaba:** con solo 3.a puesto, Oscar seguía
notando diferencia entre `host_limitlocal 1` y `0`, y 3.a ya no puede ser la causa de
esa diferencia (con `maxplayers 16` su condición es falsa con 0 y con 1). El único
`host_limitlocal` que quedaba en juego era este.

Los dos compilados e instalados.

**Coste honesto:** el anfitrión pierde algo de suavidad e inmediatez (es lo mismo que
ya tiene cualquiera conectado a un servidor real, pero se puede notar).

---

## 4. Entorno de compilación

MSVC no sirve en esta máquina: el único instalado es VS 18 Insiders con Hostx64/**x64**
y sin Windows SDK. Se usa MinGW-w64 i686.

- Toolchain: `C:\Users\Oscar\Proyectos\xash3d-tools\w64devkit\` (GCC 16.2, i686)
- SDL2: `C:\Users\Oscar\Proyectos\xash3d-tools\SDL2-2.32.10\`

```bash
export PATH="/c/Users/Oscar/Proyectos/xash3d-tools/w64devkit/bin:$PATH"
python waf build -j8 && python waf install --destdir=.
```

Configuración original (solo si hay que rehacerla):

```bash
python waf configure -T release -s "C:/Users/Oscar/Proyectos/xash3d-tools/SDL2-2.32.10"
```

### Dos parches necesarios para compilar con MinGW/i386

Son bugs reales del build, independientes del bug del bípode, y valen para upstream:

1. **`wscript`** (en `build()`): `windres` lanza el preprocesador vía `cmd.exe`, y el
   define `-DALLOCA_H=<malloc.h>` se interpreta como redirección. Se quitan los DEFINES
   de la tarea `winrc` (ningún `.rc` los usa).
2. **`common/xash3d_types.h:126`**: `RENAME_SYMBOL(x) asm(x)` renombra `svs`→`svs_`,
   `sv`→`sv_`, `SV_DropClient`→`SV_DropClient_`. En PE i386 los símbolos C llevan `_`
   delante y `asm()` lo salta, así que `ld` no los encuentra al generar los exports.
   Ahora en mingw+i386 renombra a `"_" x`.

### VGUI

`firearms/cl_dlls/client.dll` importa `vgui.dll` y VGUI no se puede enlazar con MinGW
(ABI de C++ de MSVC). Se resolvió reutilizando:

- `vgui_support.dll` de `C:\Users\Oscar\Desarrollo\Half-Life\Juego`
- `vgui.dll` de `3rdparty/vgui_support/vgui-dev/lib/win32_vc6/`

Ambos ya están copiados en la raíz del proyecto.

---

## 5. Instrumentación añadida (andamiaje, decidir si se queda)

Cvar **`dbg_weapondata`**, registrado en `host.c`, tres niveles:

- `1` — vuelca `clientdata_t` y `weapon_data_t` cuando cambian, por separado en
  servidor `[wd:SV]` y en el predictor del cliente `[wd:CL]`. También `punchangle`,
  velocidad y flags. Y `forced setangle` / `forced addangle`.
- `2` — añade cada evento del mod con sus parámetros (`f1`/`f2` = dispersión del tiro,
  `ang` = ángulo de puntería), y las líneas `[wd:RX]`: lo que llega **por el cable**,
  antes de que el predictor lo toque. Las `RX` son las únicas comparables contra `SV`,
  y funcionan también conectado a un servidor GoldSrc.
- `3` — añade `[wd:SV] clock:` con `gpGlobals->time`, `frametime`, `sv.time` y
  `timebase` en cada `PostThink` del jugador humano.

Ficheros tocados: `engine/common/dbg_weapon.{c,h}` (nuevos), y enganches en
`sv_frame.c`, `sv_pmove.c`, `sv_game.c`, `cl_pmove.c`, `cl_events.c`,
`cl_parse.c`, `host.c`.

Detalle importante para no volver a tropezar: **las líneas `[wd:CL]` son
post-predicción**, no lo que llegó por la red. Para eso están las `[wd:RX]`.

### Scripts de prueba

- `test_arreglo.bat` — solo Xash3D, sin volcado, para juzgar por sensación.
- `test_a_cliente_vs_goldsrc.bat` — servidor Firearms de Steam (GoldSrc) + cliente
  Xash3D instrumentado. Hay que escribir `kickbot all` en la consola de Half-Life o el
  servidor está lleno de bots. Guarda `log_goldsrc.log`.
- `test_b_todo_xash.bat` — todo Xash3D. Guarda `log_xash.log`.

Ejecutar **uno cada vez**: si se abren a la vez, los dos pelean por el puerto 27015.

---

## 6. Datos medidos

Protocolo: M60, quieto, ráfaga de ~30 sin bípode, pausa, desplegar, otra ráfaga de ~30.
Reproducido en dos sesiones independientes.

| | GoldSrc | Xash3D cliente | Xash3D servidor |
|---|---|---|---|
| dispersión sin bípode | 0.0406 | 0.0386 | 0.0386 |
| dispersión con bípode | **0.0105** | 0.0348 | **0.0598** |
| retroceso/disparo sin bípode | 1.599° | 1.639° | |
| retroceso/disparo con bípode | 0.200° | 0.200° | |
| cadencia | 7.48/s | 7.49/s | |
| **snapshots/s** | **12.8** | **60.0** | |

Sin bípode, cliente y servidor de Xash3D coinciden hasta el último decimal (random
determinista sembrado). Con bípode se separan.

El estado que viaja por el cable es **idéntico** entre motores, con y sin bípode:
`iu=0,0,32,1`, `fu=0.000,3.992,0.000,0.000`, y todo el `weapon_data` igual.

**Sesgo conocido en la comparación:** en la prueba A el cliente Xash3D es un cliente
*remoto* del servidor de Steam, así que esos 12.8/s son GoldSrc limitando a un cliente
normal, no necesariamente lo que GoldSrc hace con su propio jugador local. La
comparación sirvió para localizar la diferencia, pero no demuestra por sí sola qué hace
GoldSrc de anfitrión.

---

## 7. Para upstream (FWGS)

Dos variantes posibles del parche, decidir cuál proponer:

1. **La aplicada.** Restringir el atajo a un jugador. Simple, pero el anfitrión de un
   listenserver pierde suavidad.
2. **La fina.** Seguir enviando snapshots a ritmo de frame para que vaya fluido, pero
   **no llamar al mod** (`pfnUpdateClientData` / `pfnGetWeaponData`) más veces de las
   que corresponden a `cl_updaterate`. Ataca la causa sin pagar el peaje, pero toca más
   código.

En cualquier caso el parche del **cliente** (3.b) va aparte y hace falta igual: la
variante 2 solo tapa el lado del servidor.

Los dos parches del build con MinGW (sección 4) van aparte y son independientes. Aún
sin mandar; son los candidatos fáciles para una primera contribución de código.

### Estado con upstream

**Issue #2721** — https://github.com/FWGS/xash3d-fwgs/issues/2721
Abierto el 24/08 preguntando cuál de las dos variantes prefieren. Incluye las mediciones
de la sección 6 y el sesgo conocido, declarado.

**Respuesta de a1batross** (mantenedor principal), a los pocos minutos:

> If you have tokens to send bug reports, you can as well send PRs.
>
> The proposed fix with singleplayer check at client side seems good enough.

Acepta la variante 1. La primera frase lleva pulla: se huele que el reporte lo escribió
un LLM. No incumplimos nada — su CONTRIBUTING desaconseja los LLM pero no exige
declararlo, solo asumir la autoría y validar antes de mandar.

**PR #2722** — https://github.com/FWGS/xash3d-fwgs/pull/2722
Abierto el 24/08, dos commits, todo a nombre de Oscar sin coletillas:

- `engine: client: don't exempt the loopback connection from cl_cmdrate...`
- `engine: server: don't exempt the local client from the snapshot rate limiter...`

Dice explícitamente que **no se probó el cliente aislado** y ofrece quitar el commit del
servidor si lo prefieren. De ahí sale la prueba B de la sección 0.

**26/08: PR #2722 FUSIONADO en master de FWGS** (visto en el fetch: `origin/master`
termina en el commit del servidor, `09b03344`). El arreglo del bípode es oficial.

**25/08 (noche): a1batross APROBÓ el PR.** No pidió quitar el commit del servidor, así
que la prueba B deja de ser urgente (queda como curiosidad, no como requisito). Dejó una
pega menor: el comentario del código decía «the maxclients check above» y la comprobación
está debajo, no arriba (quedó desfasado al mover el comentario encima del `if`).
**Corregido el 26/08 de madrugada:** commits reescritos (`d408c0d8` + `dd26fb4f`, misma
autoría y mensajes), force-push al fork y respuesta publicada en el hilo. Falta el merge,
que lo hará un mantenedor.

### Dónde está la rama

Worktree aparte, para no ensuciar el árbol de trabajo:

- Directorio: `C:/Users/Oscar/Proyectos/xash3d-pr`
- Rama: `fix/listenserver-local-client-rate`, sacada de `origin/master` (`2f24dfe8`)
- Remoto `fork` = https://github.com/oscarsolerfollana/xash3d-fwgs.git
- El repo principal ya **no es shallow** (`git fetch --unshallow` hecho, 6725 commits)

Para retomar: `git -C C:/Users/Oscar/Proyectos/xash3d-pr status`. Para borrarlo cuando
se acabe: `git worktree remove C:/Users/Oscar/Proyectos/xash3d-pr`.

---

## 8. Hipótesis descartadas (con medición, no de oído)

Para no repetirlas:

- **Ficheros del mod distintos** — `firearms.dll`, `client.dll`, `delta.lst`,
  `liblist.gam`, `default.cfg` y `game.cfg` son idénticos entre la instalación de Steam
  y la nuestra (md5).
- **`fuser2` mal transmitido** — es el FOV, vale 90.0 constante en el servidor. El
  recorte a 3.992 lo hace igual GoldSrc (`Delta_ClampIntegerField`, 511/128).
- **`punchangle`** — siempre 0 en los dos motores. Firearms no usa el retroceso del
  motor.
- **Ángulos forzados por el servidor** — ningún `addangle` en ninguno de los dos. El
  retroceso lo aplica el `client.dll` en local.
- **Cadencia de disparo** — idéntica, 7.48/s.
- **`gpGlobals->frametime`** — correcto: suma 26.68 s sobre 28.56 s de partida (el
  resto es el rato congelado en el menú de equipo).
- **Reloj al pedir los datos del arma** — se probó forzar `svgame.globals->time = sv.time`
  antes de `pfnUpdateClientData`/`pfnGetWeaponData` con un cvar; no cambió nada. Código
  del experimento ya retirado.
- **Generador de aleatorios** — `COM_RandomFloat` es el `ran1` de Numerical Recipes,
  el mismo que GoldSrc.
- **El parche local de Oscar** (`f34b20eb "increased textures quality"`, en el árbol
  viejo `C:\Users\Oscar\Desarrollo\xash3d\xash3d-fwgs`) toca solo `ref/gl/gl_local.h`.
  No tiene nada que ver.

---

## 9. Confirmado por Oscar

- Con `host_limitlocal 1` el bípode se comporta como en Steam. (Esa es la prueba que
  identificó la causa; los arreglos de la sección 3 hacen lo mismo por defecto.)
- **23/08 18:46, con solo el arreglo 3.a instalado:** «con 1 funciona, con 0 no».
  Es decir, arreglar solo el servidor **no bastó**. De ahí sale el arreglo 3.b.
- **23/08 19:13, con 3.a + 3.b instalados:** «funciona con ambos». `host_limitlocal`
  ya no cambia nada, que es lo que se buscaba. **Arreglo verificado.**
- El problema ya se daba en una LAN party anterior. Esa instalación
  (`C:\Users\Oscar\lan Party\hl\Datos juegos`) es Xash3D de diciembre de 2025 con el
  mismo Firearms bit a bit y el mismo MarineBot, así que encaja: él era el anfitrión y
  el único afectado.


---

## 10. El sprint: causa raíz confirmada

Oscar lo ve en Xash3D y en GoldSrc funciona. **Ya estaba reportado por otra persona:**
issue **#1371**, abierto en agosto de 2024, sin diagnóstico.
https://github.com/FWGS/xash3d-fwgs/issues/1371

Confirmado el 24/08 desensamblando `firearms/cl_dlls/client.dll` con `objdump`. No es
hipótesis: son direcciones concretas y se pueden volver a comprobar.

### Lo que hay en el client.dll

| cosa | dirección | qué es |
|---|---|---|
| `+sprint` → handler | `0x60fa9a40` | `push $0x6105fb00; call KeyDown` |
| `-sprint` → handler | `0x60fa9a60` | `push $0x6105fb00; call KeyUp` |
| `in_sprint` | `0x6105fb00` | kbutton_t: `down[0]`, `down[1]`, `state` en +8 |
| `KeyDown` / `KeyUp` | `0x60fa9200` / `0x60fa9290` | los del SDK de Half-Life, sin tocar |
| `CL_ButtonBits` | `0x60faa3a0` | `test %al,0x6105fb08` con `%al=3` → `or $0x1000` |

`+attack`/`-attack` llaman a **esas mismas** `KeyDown`/`KeyUp`, solo cambia el kbutton.
`0x1000` es `IN_RUN`.

`KeyUp` tiene la rama del SDK, instrucción por instrucción: si `Cmd_Argv(1)` viene vacío,
`down[0]=down[1]=0; state=4; ret`. Es la rama «esto lo ha escrito alguien en la consola,
desatasca el botón».

### El bug del mod que lo explica todo

**`in_sprint.state` no está en el bloque de reset de `CL_ButtonBits`.** Todos los demás
botones hacen `state &= ~2` al final; sprint no. Buscar `0x6105fb08` en el desensamblado
da **una sola** aparición, la del `test`. Ese es un bug de Firearms, y es la razón de que
el bind por defecto sea `+sprint;-sprint` y no `+sprint`: es un apaño que depende de dónde
coloca GoldSrc el número de tecla.

### Los tres casos

| motor | bind | qué pasa |
|---|---|---|
| GoldSrc | `+sprint;-sprint` | pulsar: `+sprint` **sin arg** → `state=3`; `-sprint 88` no casa con `down[]`, se ignora. Soltar: `-sprint` sin arg → desatasca, `state=4`. → **de mantener** |
| Xash | `+sprint;-sprint` | pulsar: `+sprint 88` → `state=3`; `-sprint` **sin arg** → desatasca, `state=4`. → **no hace nada** |
| Xash | `+sprint` solo | pulsar: `state=3`. Soltar: `-sprint 88` sí casa → `state=(3&~1)|4=6`; `6&3=2` → **corre para siempre** |

Las filas 1 y 3 son exactamente lo que describe el reportero del #1371. Encaja todo.

### La diferencia del motor

GoldSrc (código de Quake) **no trocea el bind por `;`**: pega el número de tecla al final
de la cadena entera, y al soltar cambia el `+` inicial por `-` sobre la cadena entera.

Xash (`engine/client/input/in_keys.c:575`, `Key_AddKeyCommands`) **sí trocea**, y solo le
da el número de tecla al trozo que empieza por `+`. Por eso emite un `-sprint` sin
argumento **al pulsar**, que es cuando no toca.

### Ojo: el arreglo evidente está MAL

Pasar el número de tecla también a los trozos que empiezan por `-` produce la fila 3:
correr para siempre. Descartado.

Lo único que reproduce GoldSrc es copiar dónde coloca GoldSrc el argumento. Pero eso
vuelve a romper `bind x "+attack;+jump"`, que en GoldSrc se queda atascado y en Xash
funciona precisamente porque trocea. El troceo de Xash es una mejora deliberada.

Posible salida intermedia, a discutir con upstream: usar la colocación de GoldSrc solo
cuando algún trozo del bind empiece por `-` (el idioma `+cmd;-cmd`), y mantener el troceo
actual en el resto. Arregla Firearms sin tocar `+attack;+jump`. Es una heurística y hay
que presentarla como tal.

### El arreglo, ya escrito y compilado (sin probar en juego)

`engine/client/input/in_keys.c`. Función nueva `Key_IsGoldSrcStyleBinding()` y una salida
temprana al principio de `Key_AddKeyCommands()`.

Si el bind usa el idioma `+cmd;-cmd` — es decir, empieza por `+` y algún trozo tras un `;`
empieza por `-` — emite los comandos **como GoldSrc**: el número de tecla al final de la
cadena entera, y al soltar se cambia el `+` inicial por `-` sobre la cadena completa.
Cualquier otro bind conserva el troceo actual de Xash, así que `bind x "+attack;+jump"`
sigue funcionando como hasta ahora.

Comprobado que el búfer de comandos de Xash parte por `;` (`engine/common/cmd.c:209`), que
es de lo que depende que la emisión al estilo GoldSrc funcione.

Compilado e instalado el 24/08 a las 02:16. **Probado y verificado por Oscar el 26/08:**
sprint de mantener funcionando, sin atascos, otros binds intactos, bípode bien.

### Pendiente

- [x] ~~Probar en juego~~ Verificado el 26/08.

---

## 11. Extra: bunnyhop automático (26/08)

Cvar **`bunnyhop`** (0/1, archivado), solo cliente, en `cl_main.c`. Con `bunnyhop 1`,
manteniendo el salto se re-salta en el frame exacto de aterrizar.

Cómo funciona: en `CL_CreateCmd`, mientras el jugador está en el aire (y no nadando,
`waterlevel <= 1`), se quita el bit `IN_JUMP` del usercmd. Así `PM_Jump` ve el botón
como «recién pulsado» al tocar suelo, en vez de ignorarlo por `oldbuttons & IN_JUMP`.
Al tocar el usercmd antes de enviarlo, servidor y predicción ven lo mismo: vale en
local, de anfitrión, y también conectado a servidores ajenos (es puro cliente).

Es un extra local, **no va a upstream**.

Con `bunnyhop 2`, chivato en consola: `strip (airborne)` cuando quita el bit en el
aire, `pass og=... wl=...` cuando lo deja pasar.

**Probado el 26/08:**

- **Half-Life (valve), pista de entrenamiento: FUNCIONA.** El filtro del motor es
  correcto con el `pm_shared` estándar del SDK. Script: `test_bhop_valve.bat`.
- **Firearms: NO re-salta.** El bloqueo es del propio mod (fatiga o anti-bhop propio
  en su código de movimiento, aparte del guardián estándar). Pendiente de diagnóstico:
  correr Firearms con `bunnyhop 2` para confirmar que el motor hace strip/pass, y si
  se quiere ir a por ello, mirar el `PM_Jump` de `firearms/cl_dlls/client.dll` en el
  desensamblado (como se hizo con el sprint, sección 10).

---

## 11-bis. Extra: wallhack (28/08)

Cvar **`wallhack`** (archivado), solo render (ref_gl), vale para cualquier mod y
también de cliente en servidores ajenos (no toca nada del juego, solo cómo se
pinta). Con `wallhack 1`: el mundo y los brush models opacos (puertas, etc.) se
pintan translúcidos (alpha 0.4) **sin escribir depth**, así que jugadores,
items y efectos se ven a través de las paredes. Un valor entre 0 y 1
(`wallhack 0.15`) elige la opacidad. Es un extra local, **no va a upstream**.

Cómo funciona (`ref/gl/gl_rsurf.c`):
- Mundo: en `R_DrawTextureChains`, blend SRC_ALPHA + MODULATE + color con alpha
  + `glDepthMask(FALSE)`. En el remate de `R_DrawWorld` se saltan decals y la
  pasada de lightmaps (sin depth escrita, multiplicarían/pintarían encima de lo
  que se ve a través); las lumas (`R_RenderFullbrights`) se dejan — brillan a
  través de las paredes, y de paso limpian su separate pass.
- Brush models: en `R_DrawBrushModel`, si rendermode es normal se fuerza
  `kRenderTransTexture` con `tr.blend` = alpha (el camino de translúcidos que
  ya existe; el motor ya mutaba/restauraba el rendermode ahí para Quake).
- Solo camino clásico: con `gl_vbo 1` se desactiva solo (`R_WallhackActive`).
  Los configs de Oscar tienen gl_vbo 0.
- PVS: con wallhack activo `R_MarkLeaves` marca el mapa entero (como
  `r_novis 1`), si no las habitaciones detrás de las paredes ni se pintaban
  (primer reporte de Oscar: «en ciertos ángulos no se ve todo»). El toggle
  fuerza recálculo vía FCVAR_CHANGED, igual que r_novis.

Límite conocido: los JUGADORES muy lejanos pueden seguir sin verse aunque la
geometría sí se pinte — el servidor no manda entidades fuera del PVS del
cliente (culling de red, lado servidor). Si molesta, es ataque aparte.

- Skybox: se pinta PRIMERO como fondo con wallhack activo. Sin depth escrita,
  el skybox del final tapaba todo el mundo translúcido en cuanto entraba cielo
  en el frustum (reporte de Oscar en cs_assault: «solo se ve el fondo»).

Verificado por Oscar: pendiente (v3: PVS + skybox).

### CS 1.3 + PODBot instalado (28/08) — crash del chat de bots mitigado

Oscar montó `cstrike/` (CS 1.3, liblist a `podbot\podbot.dll` directo, sin
metamod; PODBot 2.x de ago-2001 con su mp.dll de sep-2001 — versiones bien
emparejadas, época LAN party). Petó jugando; el diálogo Sys_Crash (leído por
Win32 antes de cerrarlo) dio: **podbot.dll rva 0x13204**, c0000005.

Desensamblado: es el **formateador de frases de chat** del bot (códigos `%`
de BotChat.txt tipo «nombre del que me mató»). Resuelve el jugador buscando
su edict en un array interno de 44 bytes/entrada (base 0x10065174); si la
búsqueda falla usa igual el índice de «no encontrado» y desreferencia el
puntero basura de esa entrada. Bug del propio PODBot (sus crashes aleatorios
eran legendarios ya en GoldSrc).

**Mitigación aplicada: `botchat off` en `PodBot.cfg`** (el camino del crash
no se ejecuta). Si Oscar quiere el chat, el plan B es parche binario al
saltarse el caso «no encontrado» (como el del minimapa de NS).

### Bunnyhop en CS: cap de velocidad parcheado (28/08)

CS trae `PM_PreventMegaBunnyJumping` en su pm_shared (dlls cerrados): al
despegar un salto, si la velocidad > 1.2 × pmove->maxspeed, la escala por
(límite/vel) × 0.65. `sv_maxspeed` no influye (el límite usa el maxspeed por
arma que fija el propio CS). Función idéntica en ambos lados:

- `mp.dll` VA 0x1004e5d0 (1.2f en 0x1008d210; 0.65 como DOUBLE en 0x10087c80
  — por eso no salía escaneando floats)
- `client.dll` VA 0x10039df0 (1.2f en 0x1006c600; double 0.65 en 0x10070a48)

**Parche aplicado:** `ret` en el primer byte de la función, en los dos dlls
(los dos o la predicción va a trompicones). `patches/patch_cs13_bhopcap.py`
(verifica bytes, backups .orig). Pendiente de probar por Oscar. Si al
aterrizar aún frena: sería la stamina (fuser2), mecanismo aparte — cazar si
molesta.

### Aimbot tipo OGC (28/08)

Cvar **`aimbot`** (archivado), solo cliente, en `cl_main.c` junto al bunnyhop.
Valor = medio ángulo del cono en grados (0 = off): `aimbot 15` solo engancha
si el enemigo está a menos de 15° de la cruceta; `aimbot 180` = OGC a saco
(gira a cualquiera). En cada `CL_CreateCmd` busca el jugador enemigo vivo más
cercano a la cruceta y clava `cmd->viewangles` + `cl.viewangles` (snap
visible, estilo OGC). Detalles:

- Filtro de equipo por modelo de jugador de CS (terror/leet/arctic/guerilla
  vs urban/gsg9/sas/gign/vip); si el modelo no se reconoce (otros mods),
  todos los jugadores son objetivo.
- Filtra muertos (solid == SOLID_NOT en CS), EF_NODRAW y entidades no
  actualizadas en el último paquete. Apunta al origin interpolado (lo que se
  ve en pantalla; con lag compensation es a lo que el servidor rebobina).
- Sin comprobación de línea de visión: apunta a través de paredes (casa con
  wallhack + sv_novis). Extra local, **jamás a upstream**.

Pendiente de probar por Oscar.

### Menú de trucos (28/08)

Comando **`cheatmenu`** (motor, `cl_main.c`): menú de texto numerado estilo
PODBot pintado con `Con_NXPrintf` y repintado cada frame desde `CL_CreateCmd`.
Con el menú abierto, `Key_Event` (gancho en `in_keys.c`) se come las teclas de
opción antes de que lleguen a los binds/slots de arma:

- `1` — wallhack on/off (on = **0.8**, vía `Cvar_Set` por nombre — el cvar es
  del ref)
- `2` — aimbot cíclico: apagado → PECHO (`aimbot 180`) → CABEZA
  (`aimbot_head 1`, apunta origin+25, +12 agachado vía `usehull`) → apagado
- `0` / ESC — cerrar

Cvar nuevo **`aimbot_head`** (0 pecho / 1 cabeza, archivado). Bind: la tecla
**ñ** es scancode SEMICOLON → tecla `;` del motor; añadido
`bind ";" "cheatmenu"` a `cstrike/config.cfg`. El launcher cs_assault.bat
arranca ahora con wallhack/aimbot a 0 (el menú es el interruptor).

---

## 12. Armas de la 2.5 en la 3.0: no se puede (26/08)

Oscar quería el arsenal de la 2.5 **añadido** al de la 3.0 (sin sustituir). Investigado
y cerrado con negativa:

- El `firearms.dll` de la 3.0 conserva strings `weapon_aug`, `weapon_m11`, `weapon_mc51`,
  `weapon_mp5k`, `weapon_psg1` (armas de la 2.x quitadas), pero **no están en la tabla
  de exports** (`objdump -p`), así que no hay clase de entidad: son restos muertos.
  Confirmado en juego: `give weapon_uzi` funciona, `give weapon_aug` no hace nada.
- Sin clases en el dll (código cerrado), no hay arma que invocar ni con Metamod ni con
  `ent_create`. Añadir sin quitar: imposible.
- El menú de compra es VGUI cocinado en `client.dll` y el cliente de FA 3.0 **quitó**
  el sistema `ShowMenu`/`menuselect` del SDK, así que tampoco hay menús de texto. Lo
  interceptable es el comando `vguimenuoption N` que el menú manda al servidor (útil
  para remapeos con Metamod, si algún día hiciera falta).
- **Salida adoptada:** `firearms25/` en la raíz es el mod RC2.5 **completo** (dlls +
  liblist.gam + assets). Se juega como juego aparte: `-game firearms25`. Script:
  `test_firearms25.bat` (ps_island existe en ambas versiones). **Pendiente de probar**
  que la 2.5 arranca bien sobre Xash3D.

---

## 13. Natural Selection + EvoBot (26/08)

Montado EvoBot 1.21 (de `Downloads/evobot_v121.zip`) sobre Metamod-P **v1.21p109**
(bajado de GitHub Bots-United/metamod-p, win32) en la carpeta `ns/`:

- `ns/addons/metamod/` — metamod.dll + `plugins.ini` (carga
  `addons/evobot/evobot_mm.dll`) + `config.ini` (`gamedll dlls/ns.dll`)
- `ns/addons/evobot/` — dll, `evobot.cfg`, nombres, navmeshes (cubren la mayoría de
  mapas instalados) y el editor de navegación
- `ns/liblist.gam` — `gamedll` apuntado a metamod (antes `dlls\ns.dll`)
- `evobot.cfg` ajustado: `nsversion=32` (el NS instalado es 3.2; venía en 33) y
  `BotFillMode=fillteams` (venía en manual)

Script de prueba: `test_ns_evobot.bat` (ns_machina, 8v8 configurado).

**Verificado el 26/08 con log de diagnóstico (`-dev 2 -log` + `+meta list`):**

- NS 3.2 está en la **lista oficial de mods soportados** por FWGS. Arranca y carga
  mapas sin problema sobre Xash3D.
- **Metamod-P p109 carga y EvoBot está RUN** (`meta list` → `1 plugins, 1 running`).
  Cargaba en silencio desde el principio: sus mensajes van por `at_logged` y el
  listenserver los descarta sin `log on`. La ausencia de líneas de EvoBot en consola
  NO significa que no esté.
- Añadido `-bugcomp get_game_dir_full+gsmrf` al bat (flags que FWGS documenta para
  Metamod/AMXX; no fueron necesarios para cargar, pero son los recomendados).
- Oscar vio «un error»/crash jugando; en diagnóstico sin jugador no se reproduce.
- **Persecución del 26/08, resuelta a medias:** el error «Could not find .nav» que
  salía en las pruebas era un **artefacto del diagnóstico**: `+evobot addbot` desde la
  línea de comandos se ejecuta ANTES de `Spawn Server` (mapname aún 0). Retrasando el
  comando con una ráfaga de `wait` (cfg), EvoBot carga el navmesh perfectamente:
  «Navigation data for ns_machina loaded successfully», y no crashea al pedir bots.
- Debug temporal en el motor: `Con_Printf` en `pfnGetGameDir` (`sv_game.c`) que vuelca
  gamedir y mapname en cada llamada. **Quitar cuando se cierre esto.**
- El bat ahora graba caja negra: `-dev 2 -log ns_evobot.log`.
- **Crash reproducido y localizado (26/08):** el log muere justo tras
  `commandstationuse1` — **crashea cuando el bot comandante entra en la Command
  Station** (config: `EnableCommander=always`, 10 s de espera). Los bots se crean
  bien, la partida arranca, el navmesh carga. Es el login de comandante de un
  fakeclient lo que tumba el motor (ruta que ningún otro mod ejercita).
- **Mitigación aplicada:** `EnableCommander=never` en `evobot.cfg`. Pendiente que
  Oscar confirme que así es estable. Contras: los marines bots no construyen sin
  comandante; jugar de marine implica comandar tú, o jugar de alien.
- **Giro (26/08, segundo crash con caja negra):** con `EnableCommander=never` también
  crashea, al arrancar la ronda (`gamestartedstatus` → spawn). El volcado señala:
  `c0000005` en **`client.dll` de NS, código VGUI** (`vgui::ScrollBar2::...`, clase
  custom de NS). O sea: NO son los bots ni el comandante — es la **interfaz VGUI de NS
  sobre Xash**, cliente. El crash del comandante encaja (su login abre VGUI); los bots
  solo aceleran llegar al arranque de ronda.
- **Esquive aplicado:** `+cl_autohelp 0` en el bat (los popups de ayuda con scroll son
  lo que NS pinta al arrancar la ronda; sistema `HelpText*` confirmado en el dll).
  Pendiente de probar. Si aguanta, ojo: el modo comandante humano (VGUI pesado)
  probablemente siga crasheando.
- El VGUI lo sirve `vgui_support.dll` + `vgui.dll` de la raíz (los que se montaron
  para Firearms). Si el esquive no basta, siguiente sospechoso: esa pareja de dll
  frente a lo que NS espera. Reportable a FWGS con el volcado si se acota.
- **`+cl_autohelp 0` NO bastó** (26/08): mismo crash, mismo offset (`...7d76` en el
  client.dll de NS), otra vez poco después de `gamestartedstatus`. No eran los popups
  de ayuda.
- **Siguiente intento:** sustituido `vgui_support.dll` por el de la **build continua
  oficial de FWGS del 26/08** (721 KB vs 619 KB el viejo; `vgui.dll` idéntico).
  Backups: `vgui_support.dll.bak` / `vgui.dll.bak` en la raíz. OJO: esta pareja
  también la usa Firearms — si Firearms se rompe, restaurar los .bak.
- **Tampoco fue el vgui_support** (crash idéntico). Mejorado el volcado de crash del
  motor (`crash_win.c`): ahora imprime desplazamiento del símbolo y **RVA del módulo**.
  Con eso: el crash real está en **`client.dll` RVA `0x97d76`** (llamante `0x98104`),
  y el símbolo `ScrollBar2` era un espejismo (desplazamiento 0x6da6). El desensamblado
  muestra código FPU indexando arrays de 8 y 20 bytes con un campo tipo comparado
  con 3 — **pinta a sistema de partículas de NS** (`env_particles_custom`), no VGUI.
  La instrucción que peta: `mov (%ecx),%eax` con `ecx = *(arg) + eax*8` — puntero o
  índice basura, posiblemente algo que un engfunc de Xash devuelve distinto a GoldSrc.
- `+cl_highdetail 0` **tampoco** (mismo RVA). Observación de Oscar, clave: quieto
  tarda en petar, moviéndose peta enseguida.
- **Identificado el subsistema (26/08, por desensamblado + fuente liberado):** el
  crash está en la **biblioteca de partículas embebida de NS** (la `papi` de David
  McAllister, en `main/source/particles/` del repo unknownworlds/NS; `HLRender.cpp`
  es el glue con TriAPI). La función llamante recorre hasta **1500 elementos**
  (`cmp $0x5dc`) haciendo transformaciones y quedándose con una distancia mínima; la
  que peta (RVA `0x97d76`) evalúa registros de 20 bytes con un byte de tipo (<3 =
  Point/Line/Triangle del enum `PDomainEnum` de papi) — huele a recorrido de dominios
  con un **índice o puntero corrupto**.
- **Teoría de trabajo:** alguna plantilla de partículas del mapa llega/se construye
  corrupta en Xash (¿mensaje de red/evento parseado distinto? ¿dt anómalo?). Encaja
  con el movimiento: al moverte cambia el PVS, entran sistemas de partículas nuevos,
  y tocas la plantilla envenenada; quieto, solo tickan las sanas.
- **Matriz experimental final (26/08), tras controles con y sin bots:**

  | escenario | resultado |
  |---|---|
  | solo, sin otros jugadores (`startgame`, combate propio a saco) | **no peta** |
  | con bots (cualquier mapa, con o sin combate entre ellos) | peta, RVA `0x97d76` |
  | con UN humano de maniquí (2ª instancia), **cero bots** | **peta, mismo RVA** |

  | ídem pero **sin Metamod** (liblist directo a `ns.dll`, NS vanilla) | **peta, mismo RVA** |

  **Conclusión: EvoBot y Metamod EXCULPADOS del todo (control vanilla incluido).**
  El cliente de NS sobre Xash crashea con *cualquier* jugador remoto a la vista (el
  estado/efectos por jugador acaba indexando basura en el evaluador de la biblioteca
  de partículas). El movimiento acelera el crash porque aumenta la exposición a
  otros jugadores (PVS). El `liblist.gam` queda apuntando a Metamod otra vez.
- ~~Sospecha iuser3/iuser4~~ descartada por el desensamblado fino.
- **CAUSA RAÍZ ENCONTRADA (26/08, madrugada):** bug del motor en
  `CL_CopyEntityToPhysEnt` (`engine/client/dll_int/cl_pmove.c:342`): la condición
  para decidir si el modelo va a `pe->studiomodel` o a `pe->model` leía
  **`pe->solid` ANTES de asignarlo** (línea 358) — o sea, el valor rancio del
  physent que ocupó ese slot en frames anteriores. Si un slot pasó de contener una
  puerta (`SOLID_BSP`) a contener un jugador (modelo studio), el modelo studio
  acababa en `pe->model`, y el `pm_shared` de NS (que camina `model->hulls[0]` a
  pelo) leía hulls inexistentes → crash en su evaluador (RVA `0x97d76`).
  Explica todo: solo con jugadores remotos (únicos physents studio), el disparo al
  aparecer puertas nuevas (reordenan slots), y que moverse acelere (churn de lista).
  El lado servidor (`sv_pmove.c`) hace lo mismo bien (usa `ed->v.solid` actual).
- **Arreglo aplicado y VERIFICADO por Oscar (26/08):** usar `state->solid` en esa
  condición (+ null-check de `mod` en la rama `SOLID_BBOX`, null-deref latente).
  Partida completa con bots (fillteams) sin crash, donde antes no aguantaba minutos.
- Instrumentación de diagnóstico retirada (chivato de `pfnGetGameDir`, inventario de
  modelos brush). **Se queda** la mejora del volcado de crash (`crash_win.c`:
  desplazamiento de símbolo + RVA por frame) — candidata a PR aparte.
- **PR #2726 ABIERTO (26/08):** https://github.com/FWGS/xash3d-fwgs/pull/2726
  Rama `fix/physent-stale-solid`, commit `68c351fb` sobre `origin/master`
  (09b03344), a nombre de Oscar, sin coletillas. Diff 5+/2-: solo el arreglo +
  null-check, sin bunnyhop ni instrumentación. Auditado a fondo antes de subir
  (todas las afirmaciones verificadas contra el código; sin regresiones posibles
  en los 4 casos slot×entidad). Pendiente de revisión.
  El comandante bot puede reactivarse (`EnableCommander=always`) — su crash era
  este mismo bug.
- Posible PR aparte, sin preparar: la mejora del volcado de crash (`crash_win.c`,
  desplazamiento + RVA).

### NS en Steam (26/08)

El 3.2 del instalador no arrancaba en el Steam actual (fondo del menú sin botones,
cursor pensando) ni con la rama `steam_legacy` — el aniversario de 2023 lo remató.
**Solución: NS 3.3b9 comunitario** (github ENSL/NS, mayo 2024) instalado en
`steamapps/common/Half-Life/` (`ns` + `ns_hd`). **Funciona.** El 3.2 quedó como
`ns_32_backup` ahí mismo. Restos del instalador en `C:\Users\Oscar\Half-life 2/3/4`
pendientes de limpiar si Oscar confirma que no son suyos.

Opcional pendiente: subir el `ns` de Xash a 3.3b9 también (y `nsversion=33` en
`evobot.cfg`) para tener la misma versión en ambos motores.

### Experimento: ¿el 3.3 petaría sin el fix? (26/08)

Montado `ns33/` en Xash (copia del 3.3b9 de Steam + Metamod/EvoBot con
`nsversion=33`, script `test_ns33_sinfix.bat`) y motor compilado con el bug
**restaurado a propósito**. Resultado, verificado por Oscar: **NO peta.**
El 3.3 comunitario blindó su código de colisión (comprobaciones
`ent->model && ent->model->type == mod_brush` vistas en su fuente,
`CollisionChecker.cpp`), así que esquiva el physent envenenado que mataba al 3.2.
Conclusión: el bug del motor es real e igual de presente (lo demuestra el 3.2,
que está en la lista soportada de FWGS); el 3.3 simplemente no pisa la mina.
**El motor quedó reinstalado CON el fix** nada más terminar la prueba.

Constancia pública: comentario añadido al PR #2726 el 26/08 aclarando que la
reproducción requiere el 3.2 clásico y que el 3.3b9 no peta ni con el bug
restaurado (con la explicación del blindaje de su CollisionChecker):
https://github.com/FWGS/xash3d-fwgs/pull/2726#issuecomment-5421880011

### ~~NS 3.3 en Steam se cerraba al empezar partida~~ RESUELTO (26/08)

No eran crashes sino **cuelgues** (`AppHangB1` en el visor de eventos, hl.exe del
build legacy de 2020). Causa: la rama `steam_legacy` (puesta para el 3.2) es
incompatible con el 3.3b9, que está mantenido para el HL actual post-aniversario.
**Solución: salir de la beta `steam_legacy`** (Betas → Ninguno) y dejar que HL se
actualice. Confirmado por Oscar: funciona.

Perla: el visor de eventos también conserva el APPCRASH de xash3d.exe con
`client.dll` offset `00097d76` — nuestro bug del physent en los registros de
Windows, mismo RVA que el PR #2726.

### PROBLEMA ABIERTO: NS 3.3 en Xash, minimapa de comandante desplazado (26/08)

El modo comandante **ya no crashea** en Xash (3.3 + fix physent), pero el
**minimapa aparece desplazado hacia arriba, fuera de su caja** en la vista de
comandante. **Independiente de la resolución** (probado a 640: sigue arriba del
todo) — no es escalado virtual ni offset proporcional.

Mecanismo identificado (fuente de NS, `AvHSpriteAPI.cpp::AvHSpriteBeginFrame`):
la API de sprites de NS dibuja los overlays 2D como **triángulos en el mundo 3D**
sobre el plano cercano, reconstruyendo la pantalla con `v_angles`/`v_origin` y
`gHUD.m_iFOV`, y confiando en que la proyección del motor la devuelva al mismo
píxel. La caja del panel la pinta VGUI (2D real) y queda bien; el contenido pasa
por la proyección 3D y sale desplazado en vertical.

Hilos de los que tirar al retomar:
1. Comparar el cálculo de proyección de Xash (fov_x→fov_y en
   `ref/gl` R_SetupProjectionMatrix o equivalente) con la convención de GoldSrc —
   una diferencia en el FOV vertical produce exactamente un corrimiento vertical
   de los overlays mundo-anclados.
2. Comprobar si el FOV de comandante (mensaje SetFOV → `gHUD.m_iFOV`) llega igual
   que el que el motor usa para renderizar ese frame.
3. Test revelador sin comandante: mirar si otros overlays mundo-anclados de NS
   (iconos de órdenes/waypoints en partida normal) también están desviados en
   Xash — separaría «proyección general» de «algo específico del comandante».

**CAUSA RAÍZ CERRADA (27/08):** dentro del `paint()` de un panel VGUI, NS dibuja
con TriAPI en coordenadas locales del panel (resta `getPos()` vía
`AvHSpriteSetVGUIOffset`), porque GoldSrc deja una **traslación GL al origen del
panel** activa durante el pintado (su `pushMakeCurrent`). El vgui_support de
Xash lleva esa traslación **en software** (`_translateX/Y` en `InitVertex`), así
que TriAPI no la hereda y el contenido cae en el origen absoluto (arriba del
todo, independiente de resolución). La caja (camino VGUI normal) y el cursor
(panel a pantalla completa, origen 0,0) no lo notan — todo cuadra.

**Arreglo aplicado (LOCAL, 27/08): parche binario en `ns33/cl_dlls/client.dll`** —
`AvHSpriteSetVGUIOffset` neutralizada (siempre guarda 0,0; 8 bytes:
`mov eax,[esp+4/8]` → `xor eax,eax + nops` en VA `0x100bcd30`, globales
`0x101dbecc/d0` verificados contra el transform en `0x100bc592`). Sin la
traslación de GoldSrc, las coordenadas absolutas ya son correctas en Xash.
Backup: `client.dll.orig`. OJO: parche SOLO para la copia de Xash — el de Steam
NECESITA los offsets (GoldSrc sí traslada). Pendiente de verificar por Oscar.

**Arreglo «de verdad» para upstream (documentado, no hecho):** paridad con
GoldSrc moviendo la traslación de software a la matriz GL: `SetupPaintState` de
vgui_support llamaría a un nuevo `VGUI_SetTranslate(x,y)` del motor (vgui_api →
ref_api → glTranslatef en ref_gl), poniendo `_translateX/Y=0` y pasando el
scissor software a coordenadas locales. No se puede compilar vgui_support aquí
(«vgui can't be linked with MinGW», scripts/waifulib/vgui.py:48) — es issue/PR
para FWGS con este diagnóstico.

---

## 14. Resumen general de la sesión del 26/08 (para retomar)

**Fusionado/publicado:**
- PR #2722 (bípode/rate limiter): **FUSIONADO en master de FWGS**.
- Issue #1371 (sprint): diagnóstico + causa raíz publicados, esperando respuesta.
- PR #2726 (physent stale solid): abierto + comentario de reproducción. En revisión.

**Motor local (árbol de trabajo, sin commitear):** arreglos bípode+sprint+physent,
bunnyhop (cvar `bunnyhop`, SOLO LOCAL, jamás a upstream), volcado de crash con
RVA+desplazamiento (candidato a PR), instrumentación dbg_weapon (sección 5),
parches MinGW (sección 4, candidatos a PR).

**Juegos instalados en Xash:** firearms (3.0, arreglado), firearms25 (2.5 completo,
sin probar aún — test_firearms25.bat, ojo que Oscar lo editó y ahora lanza
firearms 3.0), ns (3.2 + Metamod + EvoBot, funcionando con el fix), ns33 (3.3b9 +
Metamod + EvoBot nsversion=33, funcionando), valve.

**Steam:** NS 3.3b9 instalado (3.2 respaldado como ns_32_backup). Menú OK, crashea
al jugar → problema abierto de arriba.

**Limpiezas pendientes:** carpetas `ns` en C:\Users\Oscar\Half-life 2/3/4 (¿restos
del instalador 3.2?); worktree xash3d-pr cuando se fusionen los PRs; decidir qué
hacer con la instrumentación dbg_weapon; copiar binarios arreglados a la
instalación de la LAN (sección 1).

**Scripts de prueba:** test_sprint, test_bhop_valve, test_armas25, test_firearms25,
test_ns_evobot (NS 3.2+bots), test_ns_dummy (maniquí 2ª instancia),
test_ns33_sinfix (experimento ya cerrado; ns33 se lanza con -game ns33),
fix_gamma (restaura gamma tras crash de NS en Steam/Xash).

Nota práctica: `ns33/` queda instalado y funcional para jugar (misma versión que
Steam, con bots). El `ns/` (3.2) sigue ahí también.
- `evobot.cfg` está en `BotFillMode=manual` (del control). Restaurar `fillteams`
  cuando el bug esté arreglado.
- `test_ns_dummy.bat` — maniquí de 2ª instancia para reproducir sin bots.
- **Siguiente paso: reportar a FWGS.** Con la matriz, el RVA, la biblioteca
  identificada (papi embebida, fuente liberado en unknownworlds/NS) y reproducción
  determinista, es un informe de la calidad del #2721.
- **Dato importante para el futuro: Metamod funciona sobre este Xash3D** — la pieza
  que haría falta para plugins en Firearms está probada.
- [x] ~~Decidir cómo llevarlo a upstream.~~ **Comentario con la causa raíz publicado en
      el #1371 el 26/08** a nombre de Oscar:
      https://github.com/FWGS/xash3d-fwgs/issues/1371#issuecomment-5418907483
      Incluye el bug del propio Firearms (el reset que falta), la diferencia de troceo
      entre motores, por qué el arreglo evidente está mal, y la heurística planteada
      como pregunta abierta antes de mandar PR (mismo movimiento que el #2721).
- [ ] **Esperar respuesta en el #1371.** Si les encaja la heurística, mandar el PR desde
      una rama nueva (el arreglo está en el árbol de trabajo, `in_keys.c`, sin commitear).

---

## 15. Disciplina de git (desde 27/08)

- **Rama `local/oscar`** en el repo principal: el estado vivo de Oscar, commiteado
  por temas (build mingw, sprint, crashdump, dbg, arreglos+bunnyhop, docs/scripts).
  `master` queda limpio siguiendo a upstream.
- **Cada arreglo upstreamable: rama propia** `fix/<nombre>` en el worktree
  `xash3d-pr`, desde `origin/master`, solo con su hunk (nunca bunnyhop ni dbg).
- **`patches/`** versionado: parches binarios reproducibles (minimapa ns33).
- **`.git/info/exclude`**: datos de juego (firearms/, ns/, ns33/, valve/...),
  binarios y logs fuera de git.

### Minimapa: verificado y reportado (27/08)

- Parche binario del minimapa **verificado por Oscar**: el mapa en su caja.
- El «se ve fatal/borracho» de después era `hud_scale "1024"` (autoscale HUD del
  menú) en ns33/config.cfg — cvar FCVAR_LATCH: no se aplica hasta reiniciar (por
  eso parecía que no se podía desactivar). Puesto a 0 + reinicio: resuelto.
  Lección: NS y el autoescalado del HUD, incompatibles.
- **Issue #2728 abierto en FWGS** con el diagnóstico completo del vgui_support y
  el arreglo propuesto: https://github.com/FWGS/xash3d-fwgs/issues/2728

### Estado de PRs/issues (28/08)

- **#2722, #2726, #2729: FUSIONADOS** (#2726 y #2729 mergeados el 27/08;
  #2729 con «Good idea, I like it» de a1batross).
- **#2730** (sprites texFormat / lerk): abierto, a1batross «will review it later».
  Único PR pendiente.
- **#1371** (sprint): abierto, de acuerdo con a1batross en que el arreglo
  ingenuo rompería otros binds; sin siguiente paso, pelota en su tejado.
- **#2728** (traslación VGUI): a1batross dice que **vgui_support va a
  desaparecer (migración a `freevgui`)**, así que NO arreglarán la traslación
  ahí. Consecuencia: el arreglo del menú T/minimapa se queda como LOCAL (el
  motor lee el translate del surface); no hay PR upstream que proponer.
- **evobot#24** (cuelgue NaN): un usuario (syocs) pregunta cómo aplicar el fix.
  Pendiente responderle.

### Movimientos con upstream (27/08, tarde)

- **PR #2726 APROBADO** por a1batross («100% de acuerdo»; añade que seguramente
  explica bugs nunca cazados de otros mods). Pega: quitar el comentario del código
  («solo era un typo»). Hecho: commit reescrito (`0fcac621`, diff 2+/2-),
  force-push y respuesta en el hilo. A la espera del merge.
- **#1371 (sprint):** a1batross teme romper a otros y pregunta por una solución
  que valga para todos. Respondido proponiendo formalmente la heurística
  `+cmd;-cmd` (con el argumento de que ningún bind puede depender del
  comportamiento actual de Xash para ese idioma) y ofreciendo PR o bugcomp:
  https://github.com/FWGS/xash3d-fwgs/issues/1371#issuecomment-5432105010

### Cuelgue de NS con bots a los ~10-15 min (27/08) — CAZADO EN CALIENTE

Oscar jugando ns33 con bots: cuelgue (no crash). El proceso seguía vivo y se
atachó gdb ANTES de matarlo. Pila del hilo principal, sin ambigüedad:

- EvoBot (`evobot_mm.dll`) llama a `pfnTraceLine` del servidor con coordenadas
  **NaN** (bug de EvoBot, probablemente vector sin inicializar en su IA).
- El motor: `SV_Move → SV_ClipMoveToEntity → PM_RecursiveHullCheck`
  (`pm_trace.c`): con NaN, los clamps de `frac` (comparaciones normales) no
  atrapan nada y el bucle «backing up» de la línea ~299 itera para siempre
  (`frac -= 0.1` sobre NaN sigue NaN, `frac < 0` nunca). Cuelgue del hilo.

**Arreglo aplicado (commit `de351618` en local/oscar):** clamps en forma
NaN-segura (`!( frac >= 0.0f )`) en los dos puntos. Números normales: idéntico;
NaN: sale por la vía del aviso «trace backed up». Compilado e instalado.

Pendientes de decidir:
- PR a FWGS (candidato claro: robustez del motor ante input del game dll).
- Issue a RGreenlees/evobot_mm con la pila (el NaN es bug suyo — aunque cabe que
  el vector NaN nazca de algo que Xash entrega distinto; con el guard puesto, si
  vuelve a pasar el juego seguirá vivo y el aviso en consola delatará el momento).

### Cuelgue NaN: verificado y reportado a EvoBot (27/08)

- Con el guard NaN instalado, Oscar confirma: **los bots ya no cuelgan el juego.**
- Resultó que el cuelgue YA estaba reportado en EvoBot: issue #24 («Crash on ns_*
  maps after randomly about 15 mins»), un usuario con **HLDS dedicado (GoldSrc)** —
  mismo código de trazas de Quake, mismo bucle infinito. Sin respuesta desde
  su apertura. **Comentada la causa raíz con la pila de gdb:**
  https://github.com/RGreenlees/evobot_mm/issues/24#issuecomment-5432876921
- Pendiente opcional: PR a FWGS con el guard NaN de pm_trace.c (commit de351618
  en local/oscar; el diff son dos comparaciones invertidas).

### PR #2729 abierto: guard NaN en pm_trace (27/08)

https://github.com/FWGS/xash3d-fwgs/pull/2729 — rama `fix/pm-trace-nan-hang`,
diff 6+/2- (los dos clamps invertidos + comentarios). Cruza referencias con
evobot_mm#24. Verificación citada: el cuelgue de ~10-15 min con bots desaparece.

### Menú de la T (pie menu) descolocado — mismo bug del #2728 (27/08)

El menú radial de la T dibuja todos sus sprites arriba a la izquierda (los
paneles/zona clicable quedan centrados e invisibles). Verificado en fuente:
`FadingImageLabel::DoPaint` y `PieNode::paintBackground` hacen `SPR_DrawHoles`
con coordenadas locales del panel dentro del paint() — código idéntico en 3.2 y
en el 3.3 de ENSL. Mismo fallo de traslación que el minimapa, por el segundo
camino de dibujo (SPR_* en vez de TriAPI). **Sin arreglo local posible** (ni
parche binario ni recompilación). Añadido como segundo caso al issue #2728:
https://github.com/FWGS/xash3d-fwgs/issues/2728#issuecomment-5433010560
El arreglo propuesto allí (traslación GL real) cubre ambos caminos.
Convivencia mientras tanto: el menú responde a clics en sus posiciones
correctas (centradas), aunque se pinte en la esquina.

**NO arreglado (28/08). Intentos revertidos, motor otra vez limpio.** El pie
menu del **3.3 (botón derecho, la T era la 3.2)** resultó ser un bicho distinto
al que suponíamos. Diagnóstico con instrumentación (logs `[spr]`, `[vgui]`,
`[vguipaint]` en el motor, ya retirados):

- **El pie menu NO usa `SPR_*`.** En toda una sesión con el menú abierto salió
  UNA sola llamada a `SPR_DrawGeneric` (HUD normal, esquina). O sea dibuja con
  **TriAPI** (`FadingImageLabel` rama `!mImageMode` → `pTriAPI->Vertex3fv`
  directo; ver AvHFadingImageLabel). El análisis viejo («usa SPR_DrawHoles»)
  era del 3.2/otro widget.
- **El translate NO está en el surface de vgui_support.** Leído bien (RVA
  0x0ac368 del dll, offset 0x24 del objeto, base recuperada del export InitAPI
  RVA 0x71e0 — la global `vgui.hInstance` NO es la base del módulo, es un handle
  interno de Xash; hay que derivar la base de una dirección de export), da
  **0,0** todo el rato durante el pie menu. El cliente de NS pinta su VGUI con
  **su propio surface** (client.dll), no el del engine.

Por qué es difícil de rematar: el offset del panel del pie menu vive en el
surface del **client.dll de NS** (binario del mod), y el dibujo va por
**pTriAPI directo** con coords locales — no hay un punto de neutralización
simple como el `gVGUIOffset` del minimapa (ese era `AvHSpriteDraw`; el pie menu
no pasa por ahí). Arreglarlo pediría, o bien parchear el client.dll de NS en
varios sitios, o que el motor aplique el offset a los `Vertex3fv` del TriAPI
durante el paint del panel (y el motor no sabe cuándo el CLIENTE pinta un panel
ni con qué offset).

Intentos descartados (todos revertidos): (1) parche binario de vgui_support
(`g_api->SetTranslate`) — falló, hookeó una copia MUERTA de `SetupPaintState`
(0x100074c0, 0 refs; el vivo está inline en `pushMakeCurrent`); al moverlo al
código vivo PETÓ por **DYNAMIC_BASE (ASLR)**: código inyectado con direcciones
absolutas sin relocaciones. (2) motor lee el surface de vgui_support + aplica en
`SPR_DrawGeneric` — no sirve (pie menu no usa SPR_ ni ese surface).

**Decisión:** aparcado. a1batross dice que vgui_support se retira (freevgui),
así que no hay PR upstream, y el ROI local es bajo (menú funcional, solo mal
pintado — responde a clics en su sitio). Si algún día se retoma: el camino es el
surface del client.dll de NS + el TriAPI, no vgui_support. El minimapa (que SÍ
arreglamos, `gVGUIOffset=0`) sigue bien y es independiente.

### Confirmado por el fuente de NS (28/08)

Fuente en `xash3d-tools/NS-source` (ENSL/NS 3.3). Los ICONOS del pie menu son
`FadingImageLabel` con `mImageMode=false` (por defecto) → `pTriAPI->Vertex3fv`
DIRECTO con coords locales del panel, y **sin** `AvHSpriteBeginFrame` (a
diferencia del minimapa/cursor comandante, que sí reconstruyen proyección y por
eso se arreglaron con `gVGUIOffset`). Los iconos dependen 100% de la traslación
GL al origen del panel que GoldSrc deja activa en `pushMakeCurrent`; Xash la
hace en software → caen en la esquina. Los conectores (`PieNode::paintBackground`)
sí usan `SPR_DrawHoles`, pero solo aparecen al desplegar submenús.

### Plan para el menú (cuando se retome)

1. **Esperar a freevgui y PROBAR.** Cuando FWGS publique freevgui, instalar y
   abrir el menú. **Predicción: seguirá roto** — el diagnóstico (translate 0 en
   el surface del engine) apunta a que NS pinta su HUD/menús con SU PROPIO
   surface (el client.dll de NS linkea la vgui.dll de Valve y crea el suyo), no
   el del engine. freevgui reemplaza el del engine → pieza equivocada. Solo se
   arreglaría solo si el menú usa el surface del engine Y freevgui hace la
   traslación como matriz GL real; el primer «si» probablemente es falso.
2. **Si sigue roto: parche binario del `client.dll` de NS.** Hacer que
   `FadingImageLabel` (rama TriAPI) SUME la posición absoluta del panel
   (getAbsExtents/getPos) a las coords del Vertex3fv. Análogo al del minimapa
   pero al revés (sumar el offset en vez de neutralizarlo). Reversing de verdad,
   para algo cosmético — decidir si compensa.

### EvoBot instalado en el NS de Steam (27/08)

Mismo montaje que en Xash, copiado desde ns33/addons: `addons/metamod` (p109) +
`addons/evobot` (1.21, nsversion=33, fillteams, 33 navmeshes), `plugins.ini`,
`config.ini` (gamedll dlls/ns.dll) y `liblist.gam` apuntando a metamod.
GoldSrc es el hábitat nativo de metamod: no hacen falta bugcomps.
OJO: el cuelgue NaN de EvoBot (issue #24) SÍ puede darse en Steam — GoldSrc
lleva el mismo código de trazas de Quake sin guard (nuestro arreglo protege
solo a Xash). Si el juego se congela a los ~10-15 min, es eso.

Confirmación A/B (27/08): con el MISMO NS 3.3b9 instalado en ambos motores, el
menú de la T se ve centrado en Steam/GoldSrc y en la esquina en Xash. Única
variable: el motor. El diagnóstico del #2728 queda verificado empíricamente.

### Disparo del lerk opaco — CAZADO Y ARREGLADO (27/08)

Síntoma: el proyectil de esporas del lerk, mancha verde opaca en Xash
(translúcido en Steam). Cadena demostrada con experimentos limpios en valve:

1. `spore2.spr` es SPR_INDEXALPHA; la nube (AvHSpores) lo usa con TransAlpha
   → env_sprite con rendermode 2: TRANSPARENTE en Xash ✓ (motor absuelto ahí).
2. El disparo visible es un TEMPENT del evento (`ShootCloud` en AvHEvents.cpp)
   que NUNCA fija rendermode → kRenderNormal.
3. env_sprite en modo normal: OPACO en Xash (predicción confirmada por Oscar).
   Causa: `R_DrawSpriteModel` en modo normal hace glDisable(GL_BLEND) ignorando
   el texFormat; GoldSrc (herencia Quake) dibuja según el formato del sprite.

**Arreglo (commit 018e2593, local/oscar):** en rendermode normal, derivar el
blending del texFormat (ADDITIVE→aditivo, INDEXALPHA→alpha). Instalado.
VERIFICADO por Oscar (27/08): transparente en el test y en juego. PR a FWGS pendiente de orden (candidato numero 4;
reproducción mínima sin mod: un .spr indexalpha + ent_create env_sprite).

### PR #2730 abierto: sprites por texFormat en modo normal (27/08)

https://github.com/FWGS/xash3d-fwgs/pull/2730 — rama
`fix/sprite-texformat-normal-mode`. Cuerpo corto a peticion de Oscar:
sintoma, repro de un comando, y nota de que los rendermode explicitos no
cambian. Verificado en juego antes de subir.

---

## 18. EN CURSO: comparar armas de CS 1.3 en Xash vs GoldSrc (28/08)

**Motivación:** tras el bípode (que desajustaba dispersión/predicción en Xash de
anfitrión), Oscar quiere verificar que las armas de CS 1.3 se comportan igual en
Xash y GoldSrc. CS 1.3 NO corre en Steam (Steam tiene CS 1.6, armas distintas),
así que el GoldSrc de referencia es **Half-Life WON** (`C:\Sierra\Half-Life`,
hl.exe de 2002, pre-Steam).

**Montado y funcionando:**
- **WON con CS 1.3:** `C:\Sierra\Half-Life\cstrike13\` (copia del cstrike de
  Xash). Dlls `.orig` restaurados (CS 1.3 PURO, sin el parche del bhop),
  liblist a `dlls\mp.dll` (sin bots), y **`hlversion` bajado a "1016"** (el de la
  beta 6.1 que arranca en ese hl.exe; con "1104" daba mismatch). **Verificado
  por Oscar: CS 1.3 arranca en WON.**
- **Xash con CS 1.3 puro:** el `cstrike/` de Xash tiene los `.orig` restaurados
  (CS 1.3 puro para la comparación). **OJO: hay que REAPLICAR el parche del bhop
  (`patches/patch_cs13_bhopcap.py`) cuando se acabe la comparación** — ahora el
  bhop cap está presente otra vez.
- **Launchers:** `test_cs_xash_host.bat` (Xash de anfitrión, `dbg_weapondata 2`,
  log `cs_xash.log`) y `C:\Sierra\Half-Life\CS13_WON.bat` (WON de anfitrión;
  en cs_assault + fullscreen desde el 28/08 — el aim_ak-colt no está en esa
  instalación; el AK se saca con `give weapon_ak47`, sv_cheats ya va en el bat;
  el mapa no influye en el spread). También quedaron
  `CS13_WON_server.bat` (WON servidor) por si se retoma el método cliente.

**Método correcto (clave, aportada por Oscar):** el bípode SOLO afecta al
ANFITRIÓN de un listenserver, no a los que se conectan (sección 2). Así que hay
que medir **Xash de anfitrión vs WON de anfitrión** (dos listenservers), NO
cliente-Xash-contra-servidor-WON (ese sería "el que se conecta", inmune al
bípode). Medición INSTRUMENTAL como el bípode (nada de screenshots):
- **Cono/dispersión y cadencia** desde `dbg_weapondata 2`, que vuelca cada
  evento de disparo con sus parámetros (en Firearms `f1/f2` = dispersión) y
  timestamp (host.realtime). En Xash: directo del log. En WON (no se puede
  instrumentar el motor): **gdb enganchado al proceso** leyendo los mismos datos
  en el disparo (como el cuelgue NaN de EvoBot).

**Instrumentación VALIDADA y línea base Xash MEDIDA (28/08):** el evento de CS
(`events/ak47.sc` etc.) sí lleva los datos: `f1/f2` = desviación aplicada a esa
bala (`vecDir.x/y` = cono × random; OJO, a diferencia de Firearms NO es el cono
directo — el cono se estima de la ENVOLVENTE de la ráfaga), `i1/i2` = punchangle
escalado. `f1/f2` de CL y SV coinciden tiro a tiro (random sembrado, predicción
bien). Detalle de parsing: el campo `duck` del cd solo marca la TRANSICIÓN; el
estado agachado real se lee de `vofs` (17 de pie, 12 agachado). Script:
`parse_cs_log.py` (scratchpad; segmenta ráfagas por huecos >1s y clasifica
estado por vofs/vel/FL_ONGROUND).

**Línea base Xash-host, AK47 (cargador ~27-30 tiros por estado):**

| estado | envolvente \|f\| máx | std(f) | cadencia |
|---|---|---|---|
| quieto de pie | 0.029-0.032 | 0.012-0.014 | 10.0/s |
| agachado | 0.032 | 0.010-0.016 | 10.0/s |
| corriendo | 0.075 | 0.028-0.033 | 10.2/s |
| saltando | 0.13-0.32 | 0.05-0.13 | (tap por salto) |

(más una ráfaga M4A1 corriendo: máx 0.120, std 0.035-0.053, 6.85/s — la
cadencia baja es del M4A1 con silenciador o el muestreo, comprobar al comparar)

**Lado WON MONTADO Y VALIDADO EN VIVO (28/08), pendiente de primera captura.**
gdb (w64devkit 17.2, sin Python) enganchado a `hl.exe` con UN breakpoint en el
`pfnPlaybackEvent` del motor, leído en runtime del `g_engfuncs` de `mp.dll`
(mismo dll bit a bit que `cstrike/dlls/mp.dll.orig`, md5 e1f19e28...).

**OJO, lección del primer intento fallido:** en el hl.exe de WON `mp.dll` NO
carga en su base preferida 0x10000000 — la ocupa `WONAuth.dll` — y se realoja
(sesión de prueba: base 0x18a90000, y puede cambiar entre arranques). Nada de
direcciones absolutas: el lanzador descubre la base con un primer pase de gdb
(`info sharedlibrary`, el «From» de mp.dll − 0x1000) y genera el script con
las direcciones corregidas. Validado en vivo: slot playback → función del
motor, gpGlobals->time daba 37.55 con frametime 0.0166.

Datos del reversing (RVAs sobre la base que toque):

- `GiveFnptrsToDll` en RVA `0x29700`: `rep movsl` de 0x8f dwords a
  **`g_engfuncs` = base+0xe0388**; `gpGlobals` se guarda en **base+0xe05c4**
  (time = primer campo, float).
- `pfnPlaybackEvent` = campo 124 de `enginefuncs_t` → slot g_engfuncs+**0x1ec**
  (`pfnPrecacheEvent` en +0x1e8). 30 call sites en mp.dll (uno por arma) — por
  eso el bp va en la función del motor, no en los call sites.
- En la entrada (cdecl): +4 flags, +8 invoker, +0xc eventindex, +0x1c fparam1,
  +0x20 fparam2, +0x24 iparam1, +0x28 iparam2. Del edict invoker (+0x80 entvars):
  velocity +0xa0, view_ofs.z +0x1fc, flags +0x224.
- **MÉTODO FINAL (28/08): LANZAR bajo gdb, NO attach.** El attach persistente
  apaga el WON hiciera lo que hiciera (ver más abajo). `test_cs_won_trace.bat`
  ahora arranca el hl.exe DESDE CERO bajo gdb con `--args`, breakpoint
  PENDIENTE en `*0x1d628a0` (dirección de pfnPlaybackEvent, estable en las 5
  pruebas de attach → hw.dll base fija sin ASLR; gdb la resuelve al mapear
  hw.dll). Script: **`trace_cs_won.gdb`**. Sin base de mp.dll ni gpGlobals: el
  log es `[N ev] f1= f2= i1= i2= vel= vofsz= flags=` (N = contador de disparo;
  sin timestamp — cada disparo se autoclasifica por vel/vofsz/onground, como el
  parser de Xash). Log a ruta absoluta del proyecto (cwd es C:\Sierra durante
  la captura). **Un solo bat**, no hace falta CS13_WON.bat. La `trace_cs_won.ps1`
  (attach) queda obsoleta pero se conserva por si el método launch fallara.
- **Intento launch (29/08): el juego arranca COMPLETO y el jugador spawnea,
  pero muere a los pocos segundos en partida** con `Dropped Player / Server
  shutting down`, breakpoint sin saltar (0 `ev`). El `WSAENOTSOCK` del log es
  benigno (socket IPX, sale en cualquier HL moderno). Diagnóstico: timeout de
  red — bajo gdb el handshake local del listenserver se retrasa por las
  suspensiones de arranque y el netchan tira el connection. **Última prueba de
  red aplicada: `+sv_timeout 3600 +cl_timeout 3600` en el bat.** Si aún así
  muere, es incompatibilidad de fondo del WON con correr bajo depurador →
  PIVOTAR a la ruta de desensamblar el mp.dll (opción B): el mp.dll es idéntico
  bit a bit entre Xash y WON, así que la fórmula de dispersión y sus
  modificadores por estado son el MISMO código; se leen del binario y la
  comparación se reduce a RNG + estado del jugador (que ya está medido en Xash).

### CIERRE de la vía gdb-WON (29/08): IMPOSIBLE, y la conclusión indirecta

- **El WON no corre bajo gdb, punto.** Probado attach y launch, con/sin
  símbolos (`-iex auto-solib-add off`), ventana y fullscreen, timeouts a 3600.
  Siempre igual: el juego arranca (con launch llega a cargar mapa y spawnear),
  y a los segundos el **servidor se apaga** (`Server shutting down`) durante la
  init de cliente/HUD. El breakpoint NUNCA salta (0 `ev`). El `WSAENOTSOCK` del
  log es benigno (socket IPX). No es timeout (subirlo no cambió nada): es que el
  build aborta su servidor en cuanto un depurador lo controla. **Medición
  directa del host GoldSrc: descartada por este método.**

- **Conclusión indirecta, pero fuerte (sin WON):**
  1. El `mp.dll` es idéntico bit a bit entre Xash y WON → la fórmula de
     dispersión y sus modificadores por estado son EL MISMO código. Ninguna
     diferencia posible ahí; si algo difiere, es lo que el MOTOR alimenta al mod
     (estado del jugador + RNG + ritmo de llamadas).
  2. El log de Xash-host demuestra que el mod recibe el estado CORRECTO por
     disparo (vel 0 quieto, 150-220 corriendo, onground=0 saltando, vofs=12
     agachado) y que la envolvente de dispersión ESCALA como debe
     (0.03 → 0.07 → 0.13-0.32). Que el spread siga el estado tan limpiamente
     tiro a tiro es la prueba empírica de que CS lee el estado EN VIVO por
     disparo, no de un buffer acumulado por snapshot.
  3. Por tanto el bug clase-bípode (rate limiter sobre-llamando
     UpdateClientData/GetWeaponData, que corrompía estado NO idempotente de
     Firearms) NO aplica a la dispersión de CS: su spread se calcula por disparo
     desde velocity/flags vivos, no se acumula entre snapshots. CS es idempotente
     ahí (getters), a diferencia de Firearms.

  **Veredicto: la dispersión de CS 1.3 se comporta igual en Xash-host que en
  GoldSrc, y el mecanismo que desajustó a Firearms no la toca.** Lo que falta
  para cerrarlo con número GoldSrc en mano no es alcanzable (WON no instrumentable).

### GIRO (29/08): comparación vía CS 1.6, que SÍ corre en Steam (idea de Oscar)

Oscar recordó que el dato de GoldSrc para Firearms se sacó SIN gdb: cliente
Xash instrumentado contra un servidor GoldSrc de Steam, leyendo `[wd:RX]`. CS
1.3 no corre en Steam, pero **CS 1.6 sí**. Los valores de arma 1.6 ≠ 1.3, pero
el camino del MOTOR (lo que se rompía con el bípode) es agnóstico del mod/versión,
así que una comparación limpia en 1.6 transfiere a 1.3 a nivel motor. Caveat: el
método RX mide al que SE CONECTA, no al anfitrión (el bípode solo toca al host);
por eso se combina con la medición de Xash-HOST.

**Montado (29/08):** `cs16/` = copia del cstrike de Steam (CS 1.6 completo, 923MB,
`C:\Program Files (x86)\Steam\steamapps\common\Half-Life\cstrike`). vgui.dll +
vgui_support.dll ya en la raíz (reusados de Firearms/NS). Tres piezas:

- **Pieza 1** `test_cs16_xash_host.bat` — Xash ANFITRIÓN (caso del bípode),
  dbg_weapondata 2, log `cs16_host.log`, mapa aim_ak-colt. La medición clave.
- **Pieza 2** `test_cs16_xash_vs_steam.bat` — cliente Xash → servidor CS 1.6 LAN
  de Steam (GoldSrc). Log `cs16_vs_steam.log`, líneas `[wd:RX]` = referencia
  GoldSrc. Método probado con Firearms (test_a_cliente_vs_goldsrc). Oscar crea
  la partida LAN de Steam en aim_ak-colt primero.
- **Pieza 3** `test_cs16_xash_server.bat` (servidor Xash dedicado) +
  `test_cs16_xash_vs_xash.bat` (cliente) — control todo-Xash. Solo si 1 y 2
  discrepan.

**OJO sobre qué captura el RX en CS:** en Firearms el cono viajaba en weapon_data
(estado), así que RX daba el cono de GoldSrc directo. En CS el cono es un
parámetro del EVENTO de disparo calculado al tirar, y el disparo propio del
cliente se PREDICE en local (client.dll de Xash con la semilla compartida del
server). Habrá que mirar en el log qué trae el RX (¿m_flAccuracy en fuser/iuser
de weapon_data? entonces sí es referencia GoldSrc de estado). Empírico: lo dirá
el log. El parser `parse_cs_log.py` ya vale para los `ev events/*.sc` de Xash.

**Siguiente paso:** Oscar corre la PIEZA 1 (arranca CS 1.6 en Xash de anfitrión,
protocolo de ráfagas) → confirmar que 1.6 arranca en Xash y que el evento de
disparo se captura, y medir cadencia/cono por estado como en 1.3. Luego pieza 2.

### Opciones si se quiere más rigor (pendiente, decidir)

- Desensamblar la rutina de disparo del mp.dll para fijar los multiplicadores
  por estado en papel y confirmar que lee `pev->velocity`/`pev->flags` en vivo.
  Confirmatorio, no puede revelar diferencia entre motores (mismo binario).
  (Se localizó ya: tests de flags en `0x224(%ebp)`/`0x1a4(%eax)` y varias
  `fsqrt` = Length2D de velocidad; la función exacta entre 184k líneas queda
  sin aislar — agujero de horas, ROI bajo.)
- Medición VISUAL cruda en WON (patrón de impactos por screenshot). Imprecisa y
  va contra el método instrumental elegido, pero es lo único que corre en WON.
- **CAUSA RAÍZ del cierre (28/08): gdb carga símbolos de ~40 DLLs de Windows al
  hacer attach, y eso SUSPENDE el proceso varios segundos.** Durante la
  congelación, el cliente local del listenserver hace timeout («Dropped Player
  from server»), y sin jugador el servidor se apaga ordenado («Server shutting
  down»). Se vio en el log: el juego cargaba mapa y sonidos y MORÍA antes de que
  se disparara nada (0 líneas `[t ev]`). NO era el fullscreen (windowed murió
  igual) ni el alt-tab. **Arreglo: `set auto-solib-add off` en gdb** → no carga
  símbolos, el attach es casi instantáneo, no hay timeout. **OJO con el CÓMO:
  tiene que ir por `-iex` (ANTES del `-p`), no en el `-x`/`-ex`.** Con
  `gdb -p PID -x script`, gdb ATTACHA primero (y ahí carga los símbolos y
  congela) y LUEGO lee el script — poner el `set auto-solib-add off` dentro del
  script llega tarde y no sirve (fallo del primer intento con el flag). La
  forma correcta: `gdb -q -batch -iex 'set auto-solib-add off' -p PID -x script`.
  Aplicado así en `trace_cs_won.ps1` (sondeo y attach final). `CS13_WON.bat`
  quedó en `-window` (da igual para la medición; se puede devolver a fullscreen).
  Verificado que el offset del bp es correcto: `pfnPlaybackEvent` = idx 123 de
  enginefuncs → +0x1ec (recontado; función fría, solo dispara al disparar).

**Protocolo de ráfagas (el mismo que ya está capturado en Xash):** cargador
entero de AK quieto de pie / agachado / corriendo / tap saltando, con pausas
>2s entre ráfagas. Luego comparar tablas con `parse_cs_log.py` (adaptar el
parser al formato del log WON cuando llegue la primera captura).

---

## 17. NS: doble disparo de la pistola — NO es de Xash (28/08)

Síntoma: la pistola del marine gasta **2 balas por click** (baja de 10 a 8).
Investigado a fondo con instrumentación del motor (ya retirada):

- Con `cl_lw 0` no se nota (pero rompe la selección de arma en NS; irrelevante).
- El servidor DISPARA 2 de verdad (2 eventos `Pistol.sc`, munición -2), la
  predicción del cliente coincide (2 = 2). No es el rate limiter (#2722):
  con `cl_cmdrate 101 + cl_updaterate 101` sigue.
- Mecanismo (fuente NS, `AvHBasePlayerWeapon`): la pistola es semiauto
  (`GetMustPressTriggerForEachShot()=true`); dispara solo si `!m_fInAttack`,
  que se resetea en `WeaponIdle` cuando `m_flTimeWeaponIdle` (decrementado por
  `cmd.msec`) expira. En un click sostenido el timer expira, se rearma el
  gatillo y dispara la 2ª bala.
- **CLAVE: en NS de Steam (GoldSrc) TAMBIÉN dispara doble.** Verificado por
  Oscar. Por tanto es comportamiento de NS (o bug del propio mod), NO de Xash.
  Nada que arreglar en el motor. Cerrado.

Aparte: la **`q`** no cambiaba de arma en Xash porque el `config.cfg` de NS no
la trae bindeada (sí `oldbinds.cfg`); en Steam sí está. Solución: añadido
`bind "q" "lastinv"` a `ns/userconfig.cfg` (persiste, no se reescribe).

Instrumentación (logs `[ev-sv]`/`[ev-cl]` en sv_game.c/cl_events.c) revertida;
motor limpio reinstalado.

**Confirmado como regresión del mod y REPORTADO (28/08).** Montada la NS **3.0**
(dlls del instalador, marzo 2005, `liblist v3.0`) en `ns32/` (gamedll directo a
ns.dll, sin metamod; `test_ns32_pistola.bat`). La 3.0 dispara **1 bala/click**
(normal); la 3.3b9 dobla. O sea el bug entró entre 3.0 y 3.3b9 (no sabemos si en
3.1/3.2 o en el 3.3b9 de ENSL). **Issue #195 en ENSL/NS** a nombre de Oscar:
https://github.com/ENSL/NS/issues/195 — con repro, la regresión, que pasa en
GoldSrc y Xash por igual (no es el motor), y el área del código
(`AvHBasePlayerWeapon`: `ProcessValidAttack`/`PrimaryAttack`/`WeaponIdle` +
el decremento manual de timers por `cmd.msec` en `ItemPostFrame`).

Apaño local mientras: `bind mouse1 "+attack;-attack"` (semi-fire GoldSrc, un
disparo por pulsacion) en `ns/semifire.cfg` — peaje: el LMG deja de ser
automatico con ese bind. `ns32/` queda montado para comparar versiones.

---

## 16. Soldar escotillas: CAUSA CAZADA Y ARREGLO INSTALADO (28/08)

Síntoma (NS 3.3 sobre Xash, reportado por Oscar): el welder del marine contra
escotillas/weldables **no hace nada** — ni interactúa, ni ruido, ni progreso.
**Confirmado por Oscar: en Steam sí funciona.**

### Causa raíz (28/08, por fuente — sin necesidad de desensamblar)

Fuente de NS clonado en `C:\Users\Oscar\Proyectos\xash3d-tools\NS-source`
(ENSL/NS, el 3.3 que jugamos).

- El welder detecta su objetivo con una **`UTIL_TraceLine` del servidor**
  (`AvHWelder::FireProjectiles`) y un `dynamic_cast<AvHWeldable*>` de `tr.pHit`.
  Si la traza no toca el weldable, no hay soldadura, ni ruido, ni progreso
  (el evento de chispas solo se dispara si `theDidWeld`). Encaja el síntoma
  entero.
- `AvHWeldable::SetPEVFlagsFromState()` pone a los weldables que **no** se
  abren al soldar (sin spawnflag 2) un **`pev->solid = 5`** a pelo — valor
  fuera del enum del SDK (SOLID_BSP es 4).
- En **GoldSrc** solid=5 cae por la rama genérica de `SV_HullForEntity`:
  colisiona/se traza como su **caja** (mins/maxs). Por eso funciona en Steam.
- En **Xash** 5 es **`SOLID_CUSTOM`** (extensión para colisión custom vía
  physics interface). NS no implementa el interface, y los cuatro caminos
  (`SV_CustomClipMoveToEntity`, `SV_ClipPMoveToEntity`, `CL_ClipPMoveToEntity`,
  `pfnTraceModel`) responden "no tocado" si faltan los callbacks. El weldable
  es un **fantasma para todas las trazas**: la del welder lo atraviesa y pega
  en lo que haya detrás.

Los weldables que SÍ se abren al soldar (spawnflag 2) son SOLID_BSP y no
sufren esto. Censo por mapa (script `dump_weldables.py` en el scratchpad):
ns_ayumi/ns_eclipse/ns_askr/ns_origin/ns_ragnarok... = solid 5 (rotos);
ns_machina/co_faceoff/ns_hera = mayoría spawnflag 2/3 (deberían funcionar
incluso sin el fix).

### Arreglo aplicado (28/08, compilado e instalado; VERIFICADO por Oscar el 28/08)

Paridad GoldSrc: si el mod no provee physics interface, un SOLID_CUSTOM se
recorta como caja (que es lo único que solid=5 significa en GoldSrc):

- `engine/server/sv_world.c` — `SV_CustomClipMoveToEntity`: fallback a
  `SV_ClipMoveToEntity` (su `SV_HullForEntity` ya hace hull de caja para
  solids no-BSP) en vez de "no hit". **Este es el que arregla el welder.**
- `engine/common/pm_trace.c` — helper `PM_CustomClipAvailable()`; en
  `PM_PlayerTraceExt` y `PM_TestPlayerPosition`, si no hay callback el
  solid=5 cae por la rama de caja existente (paridad también en el movimiento
  del jugador: en GoldSrc estas cajas SÍ bloquean al jugador).
- `engine/server/sv_pmove.c` / `engine/client/dll_int/cl_pmove.c` —
  `SV_HasClipPMoveToEntity()` / `CL_HasClipPMoveToEntity()` (declarados en
  `common.h`).

Mods tipo Paranoia 2 que usan SOLID_CUSTOM de verdad implementan el interface
→ no les cambia nada.

### Prueba para Oscar

`test_welder.bat` — ns_ayumi (todas sus soldables son del tipo roto), unirse a
marines, `give weapon_welder`, soldar una rejilla. Mirar: chispas + ruido +
barra de progreso + que al acabar dispare su puerta. Control opcional en
ns_machina (el otro tipo de weldable).

OJO pendiente de aclarar con Oscar: ¿en qué mapa vio el fallo? Si fue en
ns_machina (weldables spawnflag 3, SOLID_BSP), habría un segundo problema
distinto que este fix no cubre.

**PR #2732 ABIERTO (28/08):** https://github.com/FWGS/xash3d-fwgs/pull/2732
Rama `fix/solid-custom-fallback-hull` desde `origin/master`, un solo commit a
nombre de Oscar (mensaje en inglés, sin coletillas). Diff 46+/8-, SOLO el
arreglo del welder (auditado: cero rastro de bunnyhop/aimbot/wallhack/dbg).
Compilado con MinGW/i386 sobre master actual antes de subir. Pendiente de revisión.

Estado general al cierre del 27/08: PRs #2726 (aprobado), #2729 y #2730
(abiertos) esperando; #1371, #2728 y evobot#24 esperando respuesta; todo lo
local funcionando (bípode, sprint, bunnyhop, physents, NaN, minimapa, lerk).

---

## 19. Ascensor giratorio de c1a0b: NO es de Xash, es la hl.dll (04/09)

Síntoma (Oscar): en `c1a0b`, el ascensor que gira 90º al bajar. Si te pones cerca
justo cuando arranca la bajada, se atasca, te quita algo de vida y **sube en vez
de bajar** hasta tocar el techo. Pasa en Xash y en GoldSrc. La diferencia: en Xash
luego **baja despacio** hasta la posición de partida; en GoldSrc (WON) toca techo
y **se teletransporta** ahí sin hacer la bajada.

### La entidad (volcada del BSP)

`func_platrot` **`ele_2`**: `origin "136 608 -218"`, `height "-216"`,
`rotation "90"`, `speed "80"`, `spawnflags "1"` (SF_PLAT_TOGGLE).

El `height` es **negativo**, y en `CFuncPlat::Setup()`:
`m_vecPosition2.z = origin.z - m_flHeight` = **−2**, o sea 216 unidades *por
encima* de `m_vecPosition1` (−218). Para el código, "abajo" está arriba:
`GoDown()` mueve físicamente hacia arriba y `GoUp()` hacia abajo.

### Por qué te machaca y se invierte (los dos motores, es del SDK)

`CFuncPlat::Blocked()`: `TakeDamage(1, DMG_CRUSH)` y **invierte el sentido**.
Bajando visualmente = `TS_GOING_UP` en el código → al bloquear llama a `GoDown()`
→ 216 unidades hacia arriba, contra el techo que tiene justo encima.

### Por qué sobrepasa el techo (los dos motores, también del SDK)

`CFuncPlatRot::RotMove(destAngle, time)` con `time < 0.1` entra por el `else`:
`avelocity = vecDestDelta` y **`nextthink = ltime + 1`**, pisando el `nextthink`
de llegada que había puesto `LinearMove`. La plataforma sigue a 80 u/s **un
segundo entero** = 80 unidades de sobrepaso.

**Condición para disparar el bug:** estorbarla en los primeros ~8 unidades /
~0.1 s del recorrido (por eso "si te pones cerca": estás ahí cuando arranca).

### La diferencia real: `LinearMoveDone`, que vive en la DLL del mod

Traza del motor (`sv_trace_pushers`), caso malo bajo Xash:

```
lt=2.983 z=-3.33 vz=80.00 avy=-0.6 nt=3.983 d=1.000 BLOCKED   ← rama degenerada, nt=lt+1
   ... 60 frames subiendo a 80 u/s ...
lt=3.983 z=76.67 vz=-100.00      nt=4.770 d=0.787 THINK dz=0.00
lt=4.770 z=-2.00 vz=0.00         nt=-1.000         THINK dz=0.00
```

`dz=0.00` → **en Xash no hay teletransporte**, y sale con `vz=-100` cuando su
`speed` es 80. Ese −100 solo puede venir de la versión moderna de
`CBaseToggle::LinearMoveDone`, que si el error supera 0.03125 hace
`LinearMove(m_vecFinalDest, 100)` en vez de `UTIL_SetOrigin`. Cuadra al
milímetro: de 76.67 a −2 hay 78.67 u, a 100 u/s = **0.787 s** = el `d=0.787`
medido. La versión antigua no tiene ese `if` y hace el `UTIL_SetOrigin` a pelo:
eso es exactamente el salto seco de GoldSrc.

Caso que no dispara el bug, para contraste: `BLOCKED` con `d=0.133` (≥ 0.1) →
rama normal, `avy=-33.3`, y para limpia en z=−2 con `dz=-0.00`.

### CLAVE: se estaban comparando dos hl.dll distintas

```
1010576  md5 1ee54e18  valve/dlls/hl.dll  ==  Steam/steamapps/common/Half-Life  ← la que carga Xash
 966729  md5 2158e2e6  C:\Sierra\Half-Life\valve\dlls\hl.dll (5-jun-2002)       ← WON, la de GoldSrc
```

Byte a byte: la del proyecto **es** la de Steam. O sea que la comparación era
Xash+DLL moderna contra GoldSrc+DLL de 2002, y lo que se veía distinto es el `if`
que Valve añadió después a `LinearMoveDone`. Los dos motores hacen lo mismo:
ambos logs tienen **un solo `Blocked by player` por intento** y la misma secuencia
de `Firing: (ele_2)`.

**Aviso para el resto de comparaciones Xash vs GoldSrc** (sección 18, armas de CS):
con Sierra y el proyecto no se está aislando el motor. Hay que igualar la DLL del
mod antes de atribuir nada a Xash.

### CERRADO (04/09): confirmado por Oscar

`test_ascensor_c1a0b_steam.bat` → **en GoldSrc de Steam el ascensor se comporta
igual que en Xash**. Mismo motor GoldSrc que Sierra, misma `hl.dll` que carga Xash,
sin intercambiar ningún archivo: la única variable era la DLL y el comportamiento
la sigue a ella, no al motor. Queda demostrado que **la diferencia no es de Xash**,
es el `if` que Valve añadió a `LinearMoveDone` después de la build de WON de 2002.
La casilla que falta (Xash + DLL de WON) ya no aporta nada.

### La tabla

|            | DLL WON (2002)                   | DLL moderna                      |
|------------|----------------------------------|----------------------------------|
| **Xash**   | no medido (ya no hace falta)     | MEDIDO: baja a 100 u/s, `dz=0.00` |
| **GoldSrc**| MEDIDO: teletransporte           | **MEDIDO 04/09: igual que Xash** (Oscar) |

La de Steam es la prueba limpia: mismo motor GoldSrc que Sierra, DLL idéntica a la
de Xash, **sin intercambiar ningún archivo**. Con `host_timescale 0.1` la bajada
de 0.787 s se ve clarísima.

### Instrumentación añadida (temporal, apagada por defecto)

Cvar **`sv_trace_pushers`** (vacío = apagado): se le pasa un classname y traza una
línea por frame de esa entidad `MOVETYPE_PUSH`.

- `engine/server/sv_main.c:66` (definición), `engine/server/server.h:427` (extern)
- `engine/server/sv_phys.c:1124` (la traza en `SV_Physics_Pusher`)

Formato: `[push] t= lt= mt= z= vz= yaw= avy= nt= d= [BLOCKED] [THINK dz=]`, donde
`d = nextthink - ltime` (el valor que mira `RotMove` contra 0.1) y `THINK dz=` es
el salto del origen dentro del think (el teletransporte, medido en unidades).

**Quitar cuando se cierre:**
`git checkout engine/server/sv_phys.c engine/server/sv_main.c engine/server/server.h`
y recompilar. Mientras el cvar esté vacío no molesta.

### Lanzadores

- `test_ascensor_c1a0b.bat` — Xash + DLL moderna, con traza (`ascensor_xash.log`)
- `test_ascensor_c1a0b_won.bat` — Xash + DLL de WON (copia con respaldo, restaura al salir)
- `test_ascensor_c1a0b_goldsrc.bat` — Sierra WON, menú: tal cual / con DLL moderna
- `test_ascensor_c1a0b_steam.bat` — GoldSrc de Steam (log en su propio `valve/qconsole.log`)

### Conclusión provisional

Nada que arreglar en Xash. El bug de subir y machacarte es del **mapa**
(`height "-216"`) más el reverse-on-block del SDK, está en el Half-Life original,
y el sobrepaso es la rama degenerada de `RotMove`. El teletransporte final es
exclusivo de la `hl.dll` de WON y Valve ya lo corrigió en builds posteriores.

---

## 20. EN CURSO: en Xash, saltar desde una escalera no hace nada (04/09)

Sintoma (Oscar): subido a una `func_ladder`, pulsar saltar no hace nada. En GoldSrc
te despega de la escalera de un empujon.

### Donde vive el codigo (importante)

El movimiento del jugador NO es del motor: `pm_shared.c` lo compila el mod, o sea
que `PM_LadderMove` es codigo de Valve, **identico** en Xash y en GoldSrc. Por tanto
la diferencia tiene que estar en lo que el motor le sirve al mod. Los tres candidatos,
en orden:

1. **La escalera no llega a `moveents`.** `PM_Ladder()` recorre `pmove->moveents`
   buscando `pe->skin == CONTENTS_LADDER` y un modelo `mod_brush`. Lo llenan
   `SV_AddLaddersToPmove` (`engine/server/sv_pmove.c:285`) y, en el cliente,
   `engine/client/dll_int/cl_pmove.c:444`. Si falla, tampoco se podria trepar.
2. **`PM_TraceModel` devuelve `fraction == 1.0`.** El mod hace
   `PM_TraceModel(pLadder, origin, centroDeLaEscalera, &trace)` y **todo** el bloque
   de movimiento relativo a la escalera (incluido el salto) esta dentro de
   `if( trace.fraction != 1.0 )`.
3. **`trace.plane.normal` sale a cero.** Es el caso mas probable y el que encaja
   exacto con "no hace nada": el salto es literalmente
   `VectorScale( trace.plane.normal, 270, pmove->velocity )`. Con normal nula la
   velocidad queda en cero y ademas el `movetype` pasa a `MOVETYPE_WALK`, o sea que
   te quedas pegado. Pasa cuando la traza **empieza dentro del brush**:
   `PM_RecursiveHullCheck` (`engine/common/pm_trace.c:194`) sale con `allsolid`,
   `fraction = 1.0` y el plano sin tocar.

Revisado y descartado por lectura, todo cuadra con GoldSrc:
`PM_TraceModel` fuerza `usehull = 2` (hull de punto) como hace GoldSrc;
`PM_HullForBsp` calcula bien el `offset` (para el hull 0, `clip_mins` y
`player_mins[2]` son nulos, asi que `offset = pe->origin`);
`pfnGetModelBounds` (`engine/common/common.c:674`) devuelve `mod->mins/maxs` tal cual.

### Instrumentacion anadida (temporal, apagada por defecto)

Cvar **`pm_trace_ladder`** (0 = apagado). `PM_TraceModel` solo lo llama el mod desde
`PM_LadderMove`, asi que una linea por llamada da justo el discriminador:

```
[ladder:SV] org=... -> ... frac=... start=N all=N norm=... vel=... mt=N og=N JUMP
```

- `engine/common/pm_trace.c` (cvar + la traza al final de `PM_TraceModel`)
- `engine/common/pm_local.h` (extern), `engine/common/host.c` (registro + include)

**Como se lee el log:**

| Lo que se ve subido a la escalera | Que significa |
|---|---|
| No sale ninguna linea `[ladder:]` | Candidato 1: la escalera no llega a `moveents` |
| Sale con `frac=1.000` | Candidato 2: la traza no alcanza el brush |
| Sale con `norm=0.00 0.00 0.00` (y `start=1`) | Candidato 3: la traza arranca dentro del brush |
| Sale con `norm` bueno y `JUMP`, y aun asi no salta | El motor sirve bien el dato; mirar aguas abajo |
| `SV` bien y `CL` mal (o al reves) | Descuadre servidor/prediccion |

**Quitar cuando se cierre:**
`git checkout engine/common/pm_trace.c engine/common/pm_local.h engine/common/host.c`

### Lanzador

- `test_escalera_salto.bat` — Xash en `crossfire` con `pm_trace_ladder 1`,
  log en `escalera.log`. Otros mapas con escalera: `stalkyard`, `boot_camp`, `t0a0d`.

### Pendiente

Correr el lanzador y leer el log. Si el motor sirve bien el dato (fila 4), el
siguiente paso es comparar contra GoldSrc de Steam con el mismo mapa — y ahi vale
el aviso de la seccion 19: la `hl.dll` del proyecto es la de Steam, asi que la
comparacion contra Steam SI aisla el motor (contra Sierra no).
