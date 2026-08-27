# Parche del menu T de NS (y todo sprite del cliente dibujado dentro de VGUI)
# en vgui_support.dll (28/08/2026).
#
# Problema: dentro del paint() de un panel VGUI, GoldSrc deja una traslacion GL
# real al origen del panel activa (su pushMakeCurrent), y los dibujos SPR_* del
# cliente (menu radial de la T: FadingImageLabel / PieNode con SPR_DrawHoles) la
# heredan. El vgui_support de Xash lleva esa traslacion SOLO en software
# (_translateX/Y del surface, offset 0x24/0x28), asi que los SPR_* del cliente
# caen en el origen absoluto (esquina). La caja del panel (camino VGUI normal) y
# el minimapa (TriAPI, arreglado aparte) no lo notan.
#
# Arreglo (paridad GoldSrc, en dos mitades):
#   - Motor (limpio, commiteado): engfunc VGUI_SetTranslate en el slot 0x80 de
#     vguiapi_t guarda el translate del panel; VGui_Paint marca in_paint;
#     SPR_DrawGeneric lo suma a las coords mientras VGUI pinta.
#   - vgui_support (aqui): pushMakeCurrent reporta el translate del panel recien
#     activado llamando g_api->SetTranslate(_translateX,_translateY). Como no se
#     puede recompilar vgui_support con MinGW (ABI C++ de vgui.dll vc6), parche
#     binario.
#
# Hook: pushMakeCurrent (VA 0x10008000) escribe this->_translateX/Y en
# 0x100080e6/0x100080f0 (ebx=this). En 0x100080f3 (ebx aun = this) se reemplaza
# la instruccion `mov ebp,[edi*4+0x100ac5d8]` (7 bytes) por un jmp a un code cave
# al final de .text (VA 0x1007f34a, relleno de 00). El cave:
#     mov eax,[0x100ac36c]        ; g_api
#     push [ebx+0x28]             ; _translateY
#     push [ebx+0x24]             ; _translateX
#     call [eax+0x80]             ; g_api->SetTranslate(x,y)  (cdecl)
#     add esp,8
#     mov ebp,[edi*4+0x100ac5d8]  ; instruccion desplazada
#     jmp 0x100080fa              ; back
# SetTranslate (cdecl) preserva ebx/edi (this y el indice), asi que push sigue.
#
# NOTA: la copia out-of-line de SetupPaintState en 0x100074c0 esta MUERTA (0
# referencias); hookearla ahi no hacia nada. El codigo vivo esta inline en
# pushMakeCurrent, que es lo que se hookea aqui.
#
# Afecta NS y Firearms (comparten vgui_support.dll). Usar SIEMPRE con el xash.dll
# que trae el slot 0x80. Backup en .orig.
#
# Uso: python patches/patch_vgui_tmenu.py   (desde la raiz del repo)

import sys

PATH = sys.argv[1] if len(sys.argv) > 1 else "vgui_support.dll"

HOOK_OFF = 0x74f3     # VMA 0x100080f3
HOOK_ORIG = bytes.fromhex("8b2cbdd8c50a10")       # mov ebp,[edi*4+0x100ac5d8]
HOOK_NEW  = bytes.fromhex("e9527207009090")       # jmp 0x1007f34a ; nop ; nop

CAVE_OFF = 0x7e74a    # VMA 0x1007f34a (fin de .text, relleno de 00)
CAVE_ORIG = bytes(32)
CAVE_NEW  = bytes.fromhex(
    "a16cc30a10"        # mov eax, ds:0x100ac36c   (g_api)
    "ff7328"            # push [ebx+0x28]          (_translateY)
    "ff7324"            # push [ebx+0x24]          (_translateX)
    "ff9080000000"      # call [eax+0x80]          (g_api->SetTranslate)
    "83c408"            # add esp, 8
    "8b2cbdd8c50a10"    # mov ebp,[edi*4+0x100ac5d8]   (desplazada)
    "e9908df8ff"        # jmp 0x100080fa          (back)
)

assert len(HOOK_NEW) == len(HOOK_ORIG) == 7
assert len(CAVE_NEW) == 32

data = bytearray(open(PATH, "rb").read())
hook = bytes(data[HOOK_OFF:HOOK_OFF + 7])
cave = bytes(data[CAVE_OFF:CAVE_OFF + 32])

if hook == HOOK_NEW and cave == CAVE_NEW:
    print("ya estaba parcheado, nada que hacer")
elif hook == HOOK_ORIG and cave == CAVE_ORIG:
    open(PATH + ".orig", "wb").write(bytes(data))
    data[HOOK_OFF:HOOK_OFF + 7] = HOOK_NEW
    data[CAVE_OFF:CAVE_OFF + 32] = CAVE_NEW
    open(PATH, "wb").write(data)
    print("parcheado ok (backup en .orig)")
else:
    sys.exit("los bytes no cuadran (version distinta?), ABORTO:\n  hook=%s\n  cave=%s"
             % (hook.hex(), cave.hex()))
