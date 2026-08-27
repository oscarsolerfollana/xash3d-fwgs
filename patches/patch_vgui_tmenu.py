# Parche del menu T de NS (y todo sprite del cliente dibujado dentro de VGUI)
# en vgui_support.dll (28/08/2026).
#
# Problema: dentro del paint() de un panel VGUI, GoldSrc deja una traslacion GL
# real al origen del panel activa (su pushMakeCurrent), y los dibujos SPR_* del
# cliente (menu radial de la T: FadingImageLabel / PieNode con SPR_DrawHoles) la
# heredan. El vgui_support de Xash lleva esa traslacion SOLO en software
# (_translateX/Y en InitVertex), asi que los SPR_* del cliente caen en el origen
# absoluto (esquina superior izquierda). La caja del panel (camino VGUI normal)
# y el minimapa (TriAPI, arreglado aparte) no lo notan.
#
# Arreglo (paridad GoldSrc, en dos mitades):
#   - Motor: nueva engfunc VGUI_SetTranslate (slot 0x80 de vguiapi_t) guarda el
#     translate del panel; SPR_DrawGeneric lo suma a las coords mientras VGUI
#     pinta (VGui_GetPaintTranslate). Ver commits del engine.
#   - vgui_support (aqui): SetupPaintState (VA 0x100074c0, thiscall, this=ecx)
#     reporta el translate llamando g_api->SetTranslate(_translateX,_translateY)
#     al final. Como no se puede recompilar vgui_support con MinGW (ABI C++ de
#     vgui.dll vc6), se parchea el binario.
#
# Hook: el epilogo de SetupPaintState en VA 0x1000752b (pop ebx; ret 4; int3)
# se redirige a un code cave al final de .text (VA 0x1007f34a, 182 bytes de 00),
# que hace: pop ebx; mov eax,[g_api]; push [ecx+0x28]; push [ecx+0x24];
# call [eax+0x80]; add esp,8; ret 4.
#   g_api global = 0x100ac36c (visto en InitAPI).
#   SetTranslate = campo 0x80 de vguiapi_t (tras TextInput en 0x7c).
#
# Afecta a NS y Firearms (comparten vgui_support.dll). Solo AÑADE un reporte del
# translate; el motor solo lo aplica a SPR_* durante VGui_Paint. Usar SIEMPRE con
# el xash.dll que trae el slot 0x80, o el call indirecto peta.
#
# Uso: python patches/patch_vgui_tmenu.py   (desde la raiz del repo)

import sys

PATH = sys.argv[1] if len(sys.argv) > 1 else "vgui_support.dll"

HOOK_OFF = 0x692b     # VMA 0x1000752b
HOOK_ORIG = bytes.fromhex("5bc20400cc")
HOOK_NEW  = bytes.fromhex("e91a7e0700")  # jmp 0x1007f34a

CAVE_OFF = 0x7e74a    # VMA 0x1007f34a (fin de .text, relleno)
CAVE_ORIG = bytes(24) # 24 ceros
# pop ebx / mov eax,[g_api] / push [ecx+0x28] / push [ecx+0x24]
# / call [eax+0x80] / add esp,8 / ret 4
CAVE_NEW  = bytes.fromhex("5b a16cc30a10 ff7128 ff7124 ff9080000000 83c408 c20400".replace(" ", ""))

assert len(HOOK_NEW) == len(HOOK_ORIG) == 5
assert len(CAVE_NEW) == 24

data = bytearray(open(PATH, "rb").read())
hook = bytes(data[HOOK_OFF:HOOK_OFF + 5])
cave = bytes(data[CAVE_OFF:CAVE_OFF + 24])

if hook == HOOK_NEW and cave == CAVE_NEW:
    print("ya estaba parcheado, nada que hacer")
elif hook == HOOK_ORIG and cave == CAVE_ORIG:
    open(PATH + ".orig", "wb").write(bytes(data))
    data[HOOK_OFF:HOOK_OFF + 5] = HOOK_NEW
    data[CAVE_OFF:CAVE_OFF + 24] = CAVE_NEW
    open(PATH, "wb").write(data)
    print("parcheado ok (backup en .orig)")
else:
    sys.exit("los bytes no cuadran (version distinta?), ABORTO:\n  hook=%s\n  cave=%s"
             % (hook.hex(), cave.hex()))
