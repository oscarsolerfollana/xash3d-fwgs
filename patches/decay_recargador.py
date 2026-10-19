# Recargador de vida de Decay (item_healthcharger): el brazo se queda girado.
#
# Al acercarse el jugador, el brazo gira hacia el con el controlador 0 del modelo:
#   controlador = m_iRotValue + direccion hacia el jugador
# y m_iRotValue lo pone Spawn() segun pev->angles.y, pero solo reconoce 0, 90, 180
# y 270. Los mapas de Singularity (hts03, hts06, hts08) tienen recargadores con
# "angles" "0 -90 0": se quedan con m_iRotValue = 0, el brazo sale de lado y al
# recogerse se queda perpendicular a la pared (el controlador ya no se toca). Pasa
# igual con la DLL de Singularity y en GoldSrc; no es cosa del motor.
#
# El parche cambia, en el seguimiento del jugador de CMdlWallHealth, el
# "m_iRotValue + direccion" por "direccion - angles.y - 180" normalizado a
# [-180, 180], que es lo mismo para 0/90/180/270 y vale para cualquier angulo.
# Como no depende de m_iRotValue, arregla tambien las partidas ya guardadas.
#
# Copia de la DLL original en dlls/decay.dll.antes-recargador.bak.
#
#   python decay_recargador.py <carpeta decay>
#   python decay_recargador.py <carpeta decay> --deshacer
import os, shutil, struct, sys

BASE = 0x10000000
SITIO = 0x10121e21  # mov eax,[ebp-4] / fild dword [eax+0x94] / fadd dword [ebp-0x18]
CUEVA = 0x10121d00  # relleno int3 entre ChargerThink y la funcion de seguimiento
ORIGINAL = bytes.fromhex('8b45fc db8094000000 d845e8')

# entra con eax = this y ebp = marco de la funcion de seguimiento ([ebp-0x18] = yaw
# hacia el jugador); deja en st0 el angulo del controlador. Sin direcciones
# absolutas: la DLL puede cargarse en otra base.
CODIGO = bytes.fromhex(
	'6800 00b443'    # push 360.0
	'6800 0034c3'    # push -180.0
	'6800 003443'    # push 180.0
	'8b4804'         # mov ecx, [eax+4]            pev
	'd945e8'         # fld dword [ebp-0x18]        yaw hacia el jugador
	'd86154'         # fsub dword [ecx+0x54]       - angles.y
	'd82424'         # fsub dword [esp]            - 180
	# mayor:
	'd81424'         # fcom dword [esp]            st0 > 180 ?
	'dfe0'           # fnstsw ax
	'f6c441'         # test ah, 0x41
	'7506'           # jnz menor
	'd8642408'       # fsub dword [esp+8]          - 360
	'ebf0'           # jmp mayor
	# menor:
	'd8542404'       # fcom dword [esp+4]          st0 < -180 ?
	'dfe0'           # fnstsw ax
	'f6c401'         # test ah, 1
	'7406'           # jz fin
	'd8442408'       # fadd dword [esp+8]          + 360
	'ebef'           # jmp menor
	# fin:
	'83c40c'         # add esp, 12
	'c3'             # ret
)

def parche():
	rel = CUEVA - (SITIO + 3 + 5)
	return bytes.fromhex('8b45fc') + b'\xe8' + struct.pack('<i', rel) + b'\x90' * 4

def offset(d, va):
	pe = struct.unpack_from('<I', d, 0x3c)[0]
	nsec = struct.unpack_from('<H', d, pe + 6)[0]
	sec = pe + 24 + struct.unpack_from('<H', d, pe + 20)[0]
	for i in range(nsec):
		vsize, rva, rawsize, raw = struct.unpack_from('<4I', d, sec + i * 40 + 8)
		if rva <= va - BASE < rva + max(vsize, rawsize):
			return va - BASE - rva + raw
	raise ValueError(hex(va))

def main():
	if len(sys.argv) < 2:
		print(__doc__ or 'python decay_recargador.py <carpeta decay> [--deshacer]')
		sys.exit(1)
	dll = os.path.join(sys.argv[1], 'dlls', 'decay.dll')
	bak = dll + '.antes-recargador.bak'
	if '--deshacer' in sys.argv:
		if not os.path.exists(bak):
			sys.exit('No hay copia: ' + bak)
		shutil.copy2(bak, dll)
		print('Restaurada', dll)
		return
	d = bytearray(open(dll, 'rb').read())
	s, c = offset(d, SITIO), offset(d, CUEVA)
	if d[s:s + 12] == parche() and d[c:c + len(CODIGO)] == CODIGO:
		print('Ya estaba parcheada:', dll)
		return
	if d[s:s + 12] != ORIGINAL or d[c:c + len(CODIGO)] != b'\xcc' * len(CODIGO):
		sys.exit('decay.dll no es la esperada (%s), no se toca' % dll)
	if not os.path.exists(bak):
		shutil.copy2(dll, bak)
	d[s:s + 12] = parche()
	d[c:c + len(CODIGO)] = CODIGO
	open(dll, 'wb').write(d)
	print('Parcheada', dll)

if __name__ == '__main__':
	main()
