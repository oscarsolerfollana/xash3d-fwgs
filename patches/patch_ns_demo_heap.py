# Parche del client.dll de NS 3.3b9: desbordamiento de heap al grabar demo (16/10/2026).
#
# Problema: AvHHud::RecordDemo... (mod/AvHHud.cpp, ~linea 4389, el bloque que guarda el
# estado base en la demo con Demo_WriteBuffer(TYPE_BASESTATE, ...)) calcula theTotalSize
# sin theResearchInfoSize, pero luego escribe el numero de investigaciones (4 bytes) y
# cada AvHResearchInfo (8 bytes). Escribe 4 + 8*n bytes de mas tras el new[]. Solo pasa
# grabando demo; el motor custom graba siempre (cl_autorecord 1), asi que corrompia el heap
# en cada partida (cuelgue al empezar ronda, c000041d).
#
# Arreglo: el calculo del tamano (VA 0x100939cc, 40 bytes) se reescribe sumando lo que
# faltaba; los dos "mov [ebp-x],0" pasan a xor ecx,ecx + mov [..],ecx para que quepa.
#   antes:  total = hives + selected + strlen + 0x20
#   ahora:  total = hives + selected + research + strlen + 0x24
# La demo recibe tambien el tamano bueno (lo lee de [ebp-0x50] despues).
#
# Uso: python patches/patch_ns_demo_heap.py [ruta_client_dll] [salida]
# Verifica los bytes antes de tocar nada; aborta si no cuadran (version distinta).

import sys

PATH = sys.argv[1] if len(sys.argv) > 1 else r"ns\cl_dlls\client.dll"
OUT = sys.argv[2] if len(sys.argv) > 2 else PATH
VA, IMAGE_BASE, TEXT_VMA, TEXT_RAW = 0x100939CC, 0x10000000, 0x1000, 0x400
OFF = VA - IMAGE_BASE - TEXT_VMA + TEXT_RAW

ORIGINAL = bytes.fromhex(
    "8b871c610000"      # mov eax,[edi+0x611c]      selected end
    "2b8718610000"      # sub eax,[edi+0x6118]      selected begin
    "8b4dd0"            # mov ecx,[ebp-0x30]        pie menu name length
    "c1f802"            # sar eax,2
    "83c120"            # add ecx,0x20
    "c745fc00000000"    # mov [ebp-0x4],0
    "c745bc00000000"    # mov [ebp-0x44],0          theCounter
    "8d0486"            # lea eax,[esi+eax*4]       + hives
    "03c1")             # add eax,ecx
PATCHED = bytes.fromhex(
    "8b871c610000"      # mov eax,[edi+0x611c]
    "2b8718610000"      # sub eax,[edi+0x6118]      selected bytes
    "03875c700000"      # add eax,[edi+0x705c]      research end
    "2b8758700000"      # sub eax,[edi+0x7058]      research bytes (8 each)
    "0345d0"            # add eax,[ebp-0x30]        pie menu name length
    "03c6"              # add eax,esi               hives (32 each)
    "83c024"            # add eax,0x24              the 9 ints
    "33c9"              # xor ecx,ecx
    "894dfc"            # mov [ebp-0x4],ecx
    "894dbc")           # mov [ebp-0x44],ecx
assert len(ORIGINAL) == len(PATCHED) == 40

data = bytearray(open(PATH, "rb").read())
actual = bytes(data[OFF:OFF + len(ORIGINAL)])

if actual == PATCHED:
    print("ya estaba parcheado, nada que hacer")
elif actual == ORIGINAL:
    if OUT == PATH:
        open(PATH + ".antes-demo-heap", "wb").write(bytes(data))
    data[OFF:OFF + len(PATCHED)] = PATCHED
    open(OUT, "wb").write(data)
    print("parcheado ok ->", OUT)
else:
    sys.exit("los bytes no cuadran (dll de otra version?), ABORTO: " + actual.hex())
