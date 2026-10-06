# custom-xash — nuestras cosas

Worktree de `local/oscar`. Aquí vive todo lo que **nunca** va a upstream: el cvar
`bunnyhop`, el wallhack, el aimbot, el menú de trucos de la ñ y la instrumentación
de depuración (`dbg_weapondata`, `sv_trace_pushers`, `pm_trace_ladder`).

El cuaderno de la investigación (`ESTADO.md`) y el reparto de los dos entornos
(`ENTORNOS.md`) están en `C:\Users\Oscar\Proyectos\xash3d`. Esa es la copia buena;
el `ESTADO.md` de aquí es el de la rama y se queda atrás.

## Cómo funciona

No hay contenido de juegos en esta carpeta. Los lanzadores llevan
`-rodir C:/Users/Oscar/Proyectos/xash3d`, que monta la carpeta oficial en solo
lectura. Lo que sí hay aquí pisa a lo de allí, y de eso se aprovechan los cuatro
dlls de juego con el cap del bunnyhop quitado:

    valve/dlls/hl.dll
    valve/cl_dlls/client.dll
    cstrike13/dlls/mp.dll
    cstrike13/cl_dlls/client.dll

En la carpeta oficial esos cuatro están sin tocar, byte a byte como los de Steam.

Si alguna vez hay que rehacerlos: copiar el de la carpeta oficial y pasarle
`python patches/patch_bhopcap.py [valve|cs13|todo]` desde aquí.

## Trucos y bunnyhop, activos de serie

`valve/userconfig.cfg` y `cstrike13/userconfig.cfg` los encienden en cada arranque.
Xash ejecuta ese fichero despues de `config.cfg`, asi que mandan siempre, aunque el
config guardado diga otra cosa:

    bunnyhop 1       re-salto automatico (2 = ademas lo cuenta por consola)
    wallhack 0.8     mundo translucido (0.1 a 0.95; 0 lo apaga)
    aimbot 180       cono en grados, 180 = toda la pantalla
    aimbot_head 0    0 = al pecho, 1 = a la cabeza
    sv_novis 1       imprescindible en multijugador: sin el, el servidor no te
                     manda los jugadores fuera de tu PVS y el wallhack no los
                     puede ensenar. Solo cuenta si el servidor es tuyo.

La **enye** (`bind SEMICOLON cheatmenu`) abre el menu numerado: 1 wallhack,
2 aimbot (apagado / pecho / cabeza), 0 cerrar.

Para otro juego basta con crear `custom-xash/<juego>/userconfig.cfg`; no hace falta
copiar el juego entero, se sigue leyendo del `-rodir`.

## Demos con camara libre (como HLTV)

`engine/client/cl_democam.c`: al reproducir una demo se puede seguir a cualquier
jugador, no solo ver lo que vio quien la grabo. No necesita nada del client.dll,
asi que vale para CS 1.3:

    clic izquierdo / derecho   siguiente / anterior jugador
    espacio                    vista grabada -> primera persona -> tercera
    democam_next, democam_prev, democam_mode   lo mismo por consola

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
partida multijugador, de cualquier mod, se graba sola en la carpeta del mod con
fecha y mapa en el nombre y con todos los jugadores dentro. Las de un jugador
(maxplayers 1) y los mapas de fondo del menu no se graban. Para no grabar una
partida, `cl_autorecord 0` en la consola antes de cargar el mapa.

`juego/cs_assault_grabando.bat` juega cs_assault con bots y graba sola la
partida (`cl_autorecord 1`), y `juego/ver_demo.bat` reproduce la ultima. Las
demos de `cl_autorecord` llevan puntos en el nombre: a `playdemo` hay que
pasarle el nombre con `.dem`, si no corta por el primer punto.

Grabar cuesta poco: unos 20-25 MB por hora y casi nada de CPU. Lo delicado es
que la demo solo se cierra bien al desconectar o salir del juego; si el juego se
cuelga o se mata el proceso, esa demo no se puede abrir ("bogus # of directory
entries").

## Controles al ver una demo

`engine/client/cl_democtl.c`, junto a la camara de demo:

    P                pausa
    flechas arriba/abajo       velocidad, de x0.25 a x8
    flechas derecha/izquierda  10 segundos adelante / atras
    RePag / AvPag    1 minuto adelante / atras
    Inicio           al principio
    M                radar: en una esquina, a pantalla completa, quitado

Por consola: `democtl_pause`, `democtl_seek <segundos>`, `democtl_skip
<segundos>`, `democtl_speed <x>`, `democtl_radar <0-2>`.

Ir adelante pone la demo en modo `timedemo` (sin limite de fps), con el sonido a
0 desde `S_GetMasterVolume`, y lee hasta 100 mensajes por fotograma: se salta la
regla de un `usercmd` por fotograma, que es la que lo frenaba (la demo guarda un
`usercmd` por fotograma del cliente y solo ~20 paquetes por segundo). Dos minutos
tardan unos 24 fotogramas. Los mensajes de chat de ese tramo salen de golpe.

Hacia atras no se puede: la demo solo guarda lo que cambia de un momento al
siguiente. Se vuelve a cargar (`playdemo "<nombre>.dem"`) y se avanza hasta el
momento pedido; la camara sigue al mismo jugador.

El radar usa `overviews/<mapa>.txt` y `.bmp`, los de HLTV. El `.bmp` pinta de
verde puro lo que queda fuera del mapa; al cargarlo se cambia por negro. Sin
overview, el radar abarca todo el mapa sobre fondo oscuro. La colocacion es la de
HLTV: sin rotar, +X hacia arriba y +Y a la izquierda; rotado, +X a la derecha y
+Y hacia arriba (comprobado en cs_assault, no rotado: los T salen en el almacen y
los CT en la calle). El equipo se saca del modelo del jugador.

## Ojo

- Las configs son de esta carpeta, no las de la oficial. Los binds y los cvars de
  aquí no se ven allí, y al revés.
- Si añades un lanzador, ponle el `-rodir` o no encontrará los juegos.
- Para medir cualquier cosa que vaya a un issue o a comparar con GoldSrc, usa la
  carpeta oficial. Aquí el motor y los dlls están tocados.
