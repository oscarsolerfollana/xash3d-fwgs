# Xash3D + Firearms — estado de la investigación

Última actualización: 2026-08-26, mañana (sesión larga: NS, EvoBot, bug physent)

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

**Prueba A — el sprint: HECHA y superada (26/08).** Sprint de mantener bien, sin
atascos, binds intactos, bípode cerrando. Lo que queda de ella es decidir cómo llevarla
a upstream (sección 10): lo más valioso es comentar la causa raíz en el **#1371**.

**Prueba B — solo el hunk del cliente: ya solo opcional.** El PR #2722 fue aprobado con
los dos commits, así que nadie pide justificar el del servidor. Si algún día apetece por
curiosidad: revertir el hunk de `sv_frame.c` dejando el de `cl_main.c` (quitar
`svs.maxclients == 1 &&` de la condición de `sv_frame.c:849`), recompilar y ver si el
bípode sigue arreglado. Ojo: recompilar pisa el dll instalado.

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
