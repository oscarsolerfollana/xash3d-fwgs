# custom-xash — nuestras cosas

Worktree de `local/oscar`. Aquí vive todo lo que **nunca** va a upstream: el cvar
`bunnyhop`, el wallhack, el aimbot, el menú de trucos de la ñ y la instrumentación
de depuración (`dbg_weapondata`, `sv_trace_pushers`, `pm_trace_ladder`).

El cuaderno de la investigación (`ESTADO.md`) y el reparto de los dos entornos
(`ENTORNOS.md`) están en `C:\Users\Oscar\Proyectos\xash3d`.

## Cómo está repartido

En la raíz solo hay código. El juego compilado (release) vive en `juego/`, que
git ignora salvo los dos `userconfig.cfg`:

    juego/xash3d.exe, xash.dll, ref_gl.dll, menu.dll...      el motor
    juego/valve, cstrike (CS 1.3), gearbox, ns, firearms    los juegos

Se arranca con `juego/xash3d.exe -game <juego> -fullscreen`. Después de compilar,
copiar los binarios nuevos a `juego/`.

Con MinGW, `xash3d.exe` se enlaza con `-mwindows` (`game_launch/wscript`, desde el
11/10): si no, sale como programa de consola y abre una ventana de cmd junto al
juego.

Los cuatro dlls de juego llevan el cap del bunnyhop quitado:

    juego/valve/dlls/hl.dll
    juego/valve/cl_dlls/client.dll
    juego/cstrike/dlls/mp.dll
    juego/cstrike/cl_dlls/client.dll

En la carpeta oficial esos cuatro están sin tocar, byte a byte como los de Steam.

Si alguna vez hay que rehacerlos: copiar el de la carpeta oficial y, desde
`juego/`, pasarle `python ../patches/patch_bhopcap.py [valve|cs13|todo]`.

## Trucos y bunnyhop, apagados al arrancar

`bunnyhop`, `wallhack`, `aimbot` y `aimbot_head` ya no se guardan en
`config.cfg`: cada arranque empiezan a 0. Se encienden desde el menu de trucos o
a mano por consola:

    bunnyhop 1       re-salto automatico (2 = ademas lo cuenta por consola)
    wallhack 0.8     mundo translucido (0.1 a 0.95; 0 lo apaga)
    aimbot 180       cono en grados, 180 = toda la pantalla
    aimbot_head 0    0 = al pecho, 1 = a la cabeza

El aimbot solo apunta mientras se mantiene **Shift izquierdo** o **Alt
izquierdo**. Los derechos no cuentan.

El menu (`bind SEMICOLON cheatmenu`, la **enye**) solo se abre con
`sv_cheats 999`. Si sv_cheats cambia, se cierra solo. En un servidor ajeno el
cliente ve sv_cheats como 0 o 1, asi que alli no sale. Opciones: 1 wallhack,
2 aimbot (apagado / pecho / cabeza), 3 bunnyhop, 0 o Esc cierra (la enye
tambien). Mientras esta abierto se come todas las teclas: ni el 1 cambia de
arma ni nada hasta cerrarlo.

`juego/valve/userconfig.cfg` deja `sv_novis 1` (imprescindible en multijugador
para que el wallhack ensene a los jugadores fuera de tu PVS; solo cuenta si el
servidor es tuyo) y el bind de la enye.

## Demos con camara libre (como HLTV)

`engine/client/cl_democam.c`: al reproducir una demo se puede seguir a cualquier
jugador, no solo ver lo que vio quien la grabo. No necesita nada del client.dll,
asi que vale para CS 1.3:

    clic izquierdo / derecho   siguiente / anterior jugador
    espacio                    primera persona -> tercera persona -> vista libre
    rueda                      en primera persona: zoom hacia el centro
    raton / rueda              en tercera persona: girar la camara / acercarla
    vista libre                raton: mirar; WASD: moverse; E / Ctrl: subir / bajar;
                               Mayus: mas rapido; clic: volver a seguir a un jugador
    democam_next, democam_prev, democam_mode   lo mismo por consola
    democam_zoom <factor>      zoom por consola (en primera persona)

La demo empieza en primera persona sobre quien la grabo, que es la vista grabada
tal cual; los clics le incluyen al recorrer los jugadores. La tercera persona
sale detras del jugador y luego se gira con el raton (vista libre). El raton no
mueve nunca la vista grabada: antes el client.dll la giraba un fotograma y la
demo la devolvia a su sitio, se veia como un amago.

**Vista libre.** El tercer modo vuela por todo el mapa como un espectador,
atravesando paredes. Empieza donde estaba la vista y se mueve con las teclas que
tenga asignadas el movimiento del jugador (`+forward`, `+back`, `+moveleft`,
`+moveright`; `+use` o `+moveup` sube, `+duck` o `+movedown` baja, `+speed` va
x3), hacia donde se mira, a 400 unidades por segundo en tiempo real: tambien con
la demo en pausa o a camara lenta. No hay mira ni arma en mano. Un clic vuelve a
primera persona sobre el siguiente jugador.

El client.dll no lee el raton mientras se ve una demo (el de CS 1.3 lo lee con
la API de Windows y solo al mover al jugador), asi que lo lee el motor: en cada
fotograma mira cuanto se ha movido desde el centro de la ventana y lo vuelve a
centrar. Asi gira la camara de tercera persona y la vista libre (con
`sensitivity`, `m_yaw` y `m_pitch`). Al ver una demo la rueda es solo de la
camara (no saca el menu de armas) y las teclas de chat (`messagemode`,
`messagemode2`) no hacen nada.

**Zoom en primera persona.** La rueda estrecha el campo de vision hacia el
centro de la pantalla (hasta x8, pasos de x1,25); con zoom no se dibuja el arma
en mano, como con una mira telescopica. Al cambiar de jugador o de modo se quita.

**Vida, chaleco, balas y dinero del jugador seguido.** El juego solo se los
manda a su dueno, asi que una demo normal solo tiene los de quien grabo. El
servidor de custom-xash copia los mensajes `Health`, `Battery`, `ArmorType`,
`Money`, `CurWeapon` y `AmmoX` de cada jugador a los clientes con
`cl_demostats 1` (por defecto, va en el userinfo), en un mensaje propio del
motor, `XDStats` (jugador, que mensaje era y sus bytes), y cada 2 s vuelve a
mandarlos todos, de jugador en jugador, para las demos que empiezan tarde o
saltan atras. `XDStats` se registra al arrancar el mapa, para que este en la
cabecera de la demo. Al seguir a alguien, el client.dll recibe los suyos en vez
de los del que grabo y su propio HUD los pinta; los del que grabo se guardan y
se le devuelven al volver a su vista. Al empezar a grabar, la demo recibe los
ultimos de todos y la lista de armas (`WeaponList`, que CS solo manda al entrar
y sin la cual no pinta las balas). Solo con servidor custom-xash; las demos
antiguas siguen mostrando los del que grabo.

El arma en mano es la diestra: CS 1.3 trae cada una dos veces, `v_ak47.mdl`
(zurda) y `v_ak47_r.mdl` (diestra), y usa la `_r`.

Para que la demo tenga a todos hay que grabarla con `sv_novis 1` en un servidor
propio: si no, el servidor solo manda los jugadores cercanos. Ademas
`cl_demo.c` ahora mete en la demo los datos (nombre, modelo) de los que ya
estaban jugando al empezar a grabar; antes salian sin nombre.

En primera persona se ve el arma del jugador seguido. La demo no la guarda, asi
que se deduce: el arma en mano es la `v_` de la que lleva en el modelo
(`p_ak47.mdl` -> `v_ak47.mdl`), y sus animaciones salen de la del cuerpo del
jugador. CS pone `ref_shoot_*` / `ref_reload_*` en el torso y cada disparo
vuelve a empezar esa secuencia desde el frame 0 (el `animtime` no sirve: el
servidor lo renueva en cada frame; el frame llega redondeado y baila 1-2, por
eso solo cuenta una bajada de mas de 4). En el arma se busca la secuencia por
nombre: `shoot*` (o `slash`, `throw`, `pressbutton`), `reload` (o `insert` en
las escopetas), `draw` al cambiar de arma, `idle` al acabar; si el arma tiene
variantes `_unsil` se usan esas, que es como salen sin silenciador. Limites: la
escopeta recarga un cartucho, al agacharse disparando puede salir un disparo de
mas, y en rafagas largas (20 paquetes por segundo) alguno de menos.

El silenciador de la M4A1 y la USP: ponerlo o quitarlo no se ve (esa animacion
solo la ve quien lo hace), pero cada disparo dice si iba puesto. En CS 1.3 es el
`bparam1` del evento `events/m4a1.sc` y el `bparam2` de `events/usp.sc`
(comprobado con dos Xash: uno disparando sin, con y otra vez sin silenciador y el
otro con `cl_trace_events 1`). `cl_events.c` pasa cada evento por
`CL_DemoCamEvent`, y desde el primer disparo con silenciador el arma en mano usa
las secuencias sin `_unsil`. El PODBot lleva la M4A1 siempre con silenciador.

Cosas que ya traia Xash y aqui solo se usan: `record`/`playdemo`,
`cl_autorecord` y `sv_novis`.

Desde el 07/10 `sv_novis` y `cl_autorecord` valen 1 por defecto en el motor: toda
partida multijugador, de cualquier mod, se graba sola en la subcarpeta `demos/`
del mod (desde el 11/10; antes, en la carpeta del mod) con fecha y mapa en el nombre y con todos los jugadores dentro. Las de un jugador
(maxplayers 1) y los mapas de fondo del menu no se graban. Para no grabar una
partida, `cl_autorecord 0` en la consola antes de cargar el mapa.

`juego/cs_assault_grabando.bat` juega cs_assault con bots y graba sola la
partida (`cl_autorecord 1`), y `juego/ver_demo.bat` reproduce la ultima. Las
demos de `cl_autorecord` llevan puntos en el nombre: a `playdemo` hay que
pasarle el nombre con `.dem`, si no corta por el primer punto, y con la carpeta:
`playdemo demos/2026-10-11_03.20.11_cs_militia_flood.dem`.

Grabar cuesta poco: unos 20-25 MB por hora y casi nada de CPU. Lo delicado es
que la demo solo se cierra bien al desconectar o salir del juego; si el juego se
cuelga o se mata el proceso, esa demo no se puede abrir ("bogus # of directory
entries").

## Controles al ver una demo

`engine/client/cl_democtl.c`, junto a la camara de demo:

    P                pausa
    flecha derecha   avance rapido: x2, x4, x8, x16 (rebobinando, vuelve a x1)
    flecha izquierda rebobinar: x1, x2, x4, x8, x16
    flecha arriba    hacia x1: desde camara lenta sube un paso; desde
                     avance rapido o rebobinado, directo a x1; nunca
                     pasa de x1
    flecha abajo     mas lento, hasta x0.1 (rebobinando: rebobina mas lento)
    RePag / AvPag    1 minuto adelante / atras
    Inicio           al principio
    M                radar: en una esquina, a pantalla completa, quitado

Por consola: `democtl_pause`, `democtl_seek <segundos>`, `democtl_skip
<segundos>`, `democtl_speed <x>` (negativo rebobina), `democtl_radar <0-2>`.

Ir adelante pone la demo en modo `timedemo` (sin limite de fps), con el sonido a
0 desde `S_GetMasterVolume`, y lee hasta 400 mensajes por fotograma: se salta la
regla de un `usercmd` por fotograma, que es la que lo frenaba (la demo guarda un
`usercmd` por fotograma del cliente y solo ~20 paquetes por segundo). Dos minutos
tardan unos 24 fotogramas. Los mensajes de chat de ese tramo salen de golpe.

**Rebobinar** no relee la demo: mientras se reproduce (o se avanza con RePag),
cada paquete deja en memoria una instantanea de las entidades, en forma compacta
(solo lo que hace falta para dibujarlas: posicion, angulos, modelo, animacion,
arma, efectos...) y solo si cambiaron, con una completa cada 5 s. Unos 2 MB para
dos minutos. Rebobinar dibuja esas instantaneas hacia atras, interpoladas, en
lugar de las del ultimo paquete (`CL_DemoCtlRewindLink` en `CL_EmitEntities`), y
tambien la vista grabada. La demo se queda quieta y no llega nada al client.dll:
sin fundidos acumulados, sin chat repetido, sin sonido. Se puede rebobinar todo
lo que ya ha pasado por pantalla. La primera version releia la demo en cada paso
y quedaba fatal: el client.dll recibia de nuevo todo el tramo en cada fotograma.

**Saltar atras** (RePag, o volver a x1 tras rebobinar) si relee: la demo solo se
lee hacia delante, pero se puede retomar desde cualquier actualizacion completa
del servidor. Cada una que aparece se apunta como punto de salto; se salta a la
anterior sin recargar el mapa (`CL_DemoJumpTo`: reposiciona el fichero, invalida
los fotogramas previos y limpia efectos y sonidos) y se avanza rapido hasta el
momento. Mientras se avanza rapido se descartan los fundidos, sacudidas, chat,
avisos de muertes, daño y radio (`CL_DemoCtlSkipUserMsg`). Toda demo empieza con
una actualizacion completa, y al grabar el cliente pide una cada 5 s
(`CL_DemoKeyframeDue`: ese paquete no manda `clc_delta`), asi que en demos
grabadas desde el 07/10 cada salto relee como mucho 5 s. Solo si el punto queda
en otra seccion de la demo (cambio de mapa) se vuelve a cargar con `playdemo`.

**Camara lenta.** A x0.5, x0.25 y x0.1 la camara iba a golpes, por cosas del
motor que a x1 no se notan:
- `CL_AdjustClock` corrige el reloj del cliente a rafagas, hasta `cl_fixtimerate`
  (7,5 ms) unos fotogramas y nada otros. A x0.1 el reloj avanza ~1,7 ms por
  fotograma, asi que el ritmo cambiaba de golpe. En demos ya no corrige a
  rafagas: lo guia con una fraccion de la desviacion en cada fotograma
  (`DEMO_CLOCK_STEER_TIME`). Y `CL_LerpPoint` ya no planta el reloj en el ultimo
  paquete cuando dos mensajes traen la misma hora.
- La vista grabada sale de la prediccion, que en una demo avanza a rafagas.
  `CL_DemoCtlSmoothView` la pone, y `CL_DemoCtlSmoothPlayer` pone a los jugadores,
  en una curva (Hermite) por los paquetes de alrededor, `cl_interp` por detras del
  reloj, promediada en una ventana de 60 ms: algunos paquetes traen una hora que
  no cuadra con su posicion, y una curva que pase justo por los puntos hereda el
  tiron. Sin curva si salta mas de 128 unidades (reaparecer).
Medido a x0.1 corriendo: el paso de la camara cambia de forma gradual (0,35 ->
0,42 unidades por fotograma al acelerar), ningun fotograma quieto ni saltos.

**Muertes.** Cada `DeathMsg` de la demo (en CS 1.3: asesino, victima y cabeza, un
byte cada uno, y el nombre del arma) se guarda con su momento, y las ultimas se
listan arriba a la derecha durante 6 s, como las dibuja CS: asesino, icono del
arma y de tiro a la cabeza (`d_<arma>` y `d_headshot` de `sprites/hud.txt`, en el
naranja del HUD; verde si es del mismo equipo), victima, los nombres con el color
de su equipo y la calavera si no lo mato nadie. Hasta 4 lineas, debajo de las
lineas de estado de los controles. Al ir con
su momento salen tambien rebobinando, saltando y con el radar a pantalla
completa. El client.dll ya no recibe `DeathMsg` al ver demos, para no repetirlas.

El radar usa `overviews/<mapa>.txt` y `.bmp`, los de HLTV. El `.bmp` pinta de
verde puro lo que queda fuera del mapa; al cargarlo se cambia por negro. Sin
overview, el radar abarca todo el mapa sobre fondo oscuro. La colocacion es la de
HLTV: sin rotar, +X hacia arriba y +Y a la izquierda; rotado, +X a la derecha y
+Y hacia arriba (comprobado en cs_assault, no rotado: los T salen en el almacen y
los CT en la calle). El equipo se saca del modelo del jugador.

## Ojo

- Las configs son de esta carpeta, no las de la oficial. Los binds y los cvars de
  aquí no se ven allí, y al revés.
- Para medir cualquier cosa que vaya a un issue o a comparar con GoldSrc, usa la
  carpeta oficial. Aquí el motor y los dlls están tocados.
