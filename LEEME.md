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
    sv_novis 1       solo en cstrike13, o el wallhack no ensena nada nuevo

La **enye** (`bind SEMICOLON cheatmenu`) abre el menu numerado: 1 wallhack,
2 aimbot (apagado / pecho / cabeza), 0 cerrar.

Para otro juego basta con crear `custom-xash/<juego>/userconfig.cfg`; no hace falta
copiar el juego entero, se sigue leyendo del `-rodir`.

## Ojo

- Las configs son de esta carpeta, no las de la oficial. Los binds y los cvars de
  aquí no se ven allí, y al revés.
- Si añades un lanzador, ponle el `-rodir` o no encontrará los juegos.
- Para medir cualquier cosa que vaya a un issue o a comparar con GoldSrc, usa la
  carpeta oficial. Aquí el motor y los dlls están tocados.
