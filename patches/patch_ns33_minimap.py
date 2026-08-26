# Parche del minimapa de comandante para NS 3.3b9 sobre Xash3D (27/08/2026).
#
# Problema: GoldSrc deja una traslacion GL al origen del panel activa durante el
# paint() de VGUI; el vgui_support de Xash la lleva en software, asi que los
# dibujos TriAPI del mod (minimapa de comandante) caen en el origen absoluto de
# la pantalla. NS resta el offset del panel (AvHSpriteSetVGUIOffset) contando
# con esa traslacion; sin ella, las coordenadas absolutas ya son correctas.
#
# Arreglo: neutralizar AvHSpriteSetVGUIOffset para que guarde siempre (0,0).
#   VA 0x100bcd30:  mov eax,[esp+4] -> xor eax,eax; nop; nop  (x2 con esp+8)
# Solo para la copia de Xash (ns33/) - el NS de Steam NECESITA los offsets.
#
# Uso: python patches/patch_ns33_minimap.py [ruta_client_dll]
# Verifica los bytes antes de tocar nada; aborta si no cuadran (version distinta).

import sys

PATH = sys.argv[1] if len(sys.argv) > 1 else r"ns33\cl_dlls\client.dll"
VA, IMAGE_BASE, TEXT_VMA, TEXT_RAW = 0x100BCD30, 0x10000000, 0x1000, 0x400
OFF = VA - IMAGE_BASE - TEXT_VMA + TEXT_RAW

ORIGINAL = bytes.fromhex("8b442404a3ccbe1d108b442408a3d0be1d10c3")
PATCHED  = bytes.fromhex("31c09090a3ccbe1d1031c09090a3d0be1d10c3")

data = bytearray(open(PATH, "rb").read())
actual = bytes(data[OFF:OFF + len(ORIGINAL)])

if actual == PATCHED:
    print("ya estaba parcheado, nada que hacer")
elif actual == ORIGINAL:
    open(PATH + ".orig", "wb").write(bytes(data))
    data[OFF:OFF + len(PATCHED)] = PATCHED
    open(PATH, "wb").write(data)
    print("parcheado ok (backup en .orig)")
else:
    sys.exit("los bytes no cuadran (dll de otra version?), ABORTO: " + actual.hex())
