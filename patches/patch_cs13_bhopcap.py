# Parche del limite de velocidad al saltar (bhop cap) de CS 1.3 (28/08/2026).
#
# Problema: el pm_shared de CS trae PM_PreventMegaBunnyJumping: al despegar en
# un salto, si la velocidad supera 1.2 * pmove->maxspeed, la recorta a
# (limite/velocidad) * 0.65. Mata el bunnyhop por encima de ~1.2x. sv_maxspeed
# no ayuda: el limite sale del maxspeed por arma que fija el propio CS.
#
# La funcion esta identica en los dos lados (servidor y prediccion del cliente):
#   mp.dll     VA 0x1004e5d0  (fmul 1.2f en 0x1008d210, 0.65 double en 0x10087c80)
#   client.dll VA 0x10039df0  (fmul 1.2f en 0x1006c600, 0.65 double en 0x10070a48)
#
# Arreglo: ret (0xC3) en el primer byte de la funcion (push ecx -> ret).
# Hay que parchear LOS DOS o la prediccion del cliente va a trompicones.
#
# Uso: python patches/patch_cs13_bhopcap.py  (desde la raiz del repo)
# Verifica los bytes antes de tocar nada; aborta si no cuadran (version distinta).

import sys

# (ruta, offset de fichero (= rva, .text raw==vma en ambos), bytes originales)
TARGETS = [
    (r"cstrike\dlls\mp.dll", 0x4E5D0,
     bytes.fromhex("518b0d8030 0e10d981f4010000d80d10d20810".replace(" ", ""))),
    (r"cstrike\cl_dlls\client.dll", 0x39DF0,
     bytes.fromhex("518b0dd4840c10d981f4010000d80d00c60610")),
]

for path, off, original in TARGETS:
    data = bytearray(open(path, "rb").read())
    actual = bytes(data[off:off + len(original)])
    patched = b"\xc3" + original[1:]

    if actual == patched:
        print(path, "- ya estaba parcheado, nada que hacer")
    elif actual == original:
        open(path + ".orig", "wb").write(bytes(data))
        data[off] = 0xC3
        open(path, "wb").write(data)
        print(path, "- parcheado ok (backup en .orig)")
    else:
        sys.exit(path + " - los bytes no cuadran (version distinta?), ABORTO: " + actual.hex())
