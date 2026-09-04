# Parche del limite de velocidad al saltar (bhop cap). 05/09/2026.
#
# Problema: el pm_shared del SDK trae PM_PreventMegaBunnyJumping, que se llama
# desde PM_Jump: al despegar, si la velocidad supera FACTOR * pmove->maxspeed,
# la recorta a (limite/velocidad) * 0.65. Eso pone techo al bunnyhop.
# El FACTOR es 1.7 en Half-Life y 1.2 en CS. Como la funcion se compila en el
# dll del mod, no hay nada que tocar en el motor.
#
# sv_maxspeed no sirve de atajo: en CS el limite sale del maxspeed por arma que
# fija el propio mod, y en HL subirlo te haria correr mas por el suelo tambien.
#
# La funcion esta identica en los dos lados (servidor y prediccion del cliente)
# y con el mismo prologo en los cuatro dlls:
#   51                push ecx
#   8b 0d <pmove>     mov  ecx, [pmove]
#   d9 81 f4 01 00 00 fld  dword ptr [ecx+0x1F4]   ; pmove->maxspeed
#   d8 0d <factor>    fmul dword ptr [1.7f / 1.2f]
#
# Arreglo: ret (0xC3) en el primer byte. Hay que parchear LOS DOS lados de cada
# juego o la prediccion del cliente va a trompicones.
#
# OJO: valve/dlls/hl.dll es byte a byte la de Steam, y las comparaciones
# Xash vs GoldSrc se apoyan en eso (secciones 18 y 19 de ESTADO.md). Parcheada
# ya no lo es. Volver atras con --revert antes de comparar nada.
#
# Uso, desde la raiz del repo:
#   python patches/patch_bhopcap.py [valve|cs13|todo] [--revert]

import os
import sys

# juego -> [(ruta, offset de fichero, bytes originales del prologo)]
TARGETS = {
    "valve": [
        (os.path.join("valve", "dlls", "hl.dll"), 0x6BDC0,
         "518b0dd4290e10d981f4010000d80da40a0b10"),
        (os.path.join("valve", "cl_dlls", "client.dll"), 0x369A0,
         "518b0dac7e9a01d981f4010000d80d08969601"),
    ],
    "cs13": [
        (os.path.join("cstrike13", "dlls", "mp.dll"), 0x4E5D0,
         "518b0d80300e10d981f4010000d80d10d20810"),
        (os.path.join("cstrike13", "cl_dlls", "client.dll"), 0x39DF0,
         "518b0dd4840c10d981f4010000d80d00c60610"),
    ],
}

args = [a for a in sys.argv[1:] if not a.startswith("-")]
revert = "--revert" in sys.argv
which = args[0] if args else "todo"

if which == "todo":
    targets = TARGETS["valve"] + TARGETS["cs13"]
elif which in TARGETS:
    targets = TARGETS[which]
else:
    sys.exit("uso: python patches/patch_bhopcap.py [valve|cs13|todo] [--revert]")

for path, off, orig_hex in targets:
    original = bytes.fromhex(orig_hex)
    patched = b"\xc3" + original[1:]

    if not os.path.exists(path):
        print(path, "- no existe, me lo salto")
        continue

    data = bytearray(open(path, "rb").read())
    actual = bytes(data[off:off + len(original)])

    if revert:
        if actual == original:
            print(path, "- ya estaba sin parchear")
        elif actual == patched:
            data[off] = original[0]
            open(path, "wb").write(bytes(data))
            print(path, "- revertido ok")
        else:
            sys.exit(path + " - bytes raros, ABORTO: " + actual.hex())
        continue

    if actual == patched:
        print(path, "- ya estaba parcheado")
    elif actual == original:
        if not os.path.exists(path + ".orig"):
            open(path + ".orig", "wb").write(bytes(data))
        data[off] = 0xC3
        open(path, "wb").write(bytes(data))
        print(path, "- parcheado ok (backup en .orig)")
    else:
        sys.exit(path + " - los bytes no cuadran (version distinta?), ABORTO: " + actual.hex())
