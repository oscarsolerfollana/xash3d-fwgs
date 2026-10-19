# Repara los mapas que Xash no carga (revision del 10-10-2026, juego/REVISION_MAPAS.md).
#
# - Sven Co-op (26 mapas): la lista de entidades acaba con un byte basura (0xB7) antes
#   del NUL. GoldSrc lo salta (su COM_Parse compara con char con signo) y Xash no:
#   "SV_LoadFromFile: found ... when expecting {". Los bytes >= 0x80 que estan fuera de
#   comillas pasan a ser espacios.
# - cstrike world-trade-center: en la entidad del *110, 'on"\n}\n{\n"' quedo machacado
#   por 'lags" "1"' (mismos 9 bytes): "closing brace without data". Se repone.
#
# Ningun fichero cambia de tamano. Los originales van a <juego>/_mapas_originales/<mod>/.
# Para deshacer: python reparar_mapas.py <carpeta juego> --deshacer
#
#   python reparar_mapas.py <carpeta juego>
import os, shutil, struct, sys

WTC_MAL = b'"classname" "func_buttlags" "1"model" "*110"'
WTC_BIEN = b'"classname" "func_button"\n}\n{\n"model" "*110"'

def lump_entidades(data):
	for i in (0, 1):  # Blue Shift cambia de sitio entidades y planos
		ofs, size = struct.unpack_from('<ii', data, 4 + i * 8)
		if data[ofs:ofs + size].lstrip().startswith(b'{'):
			return ofs, size
	raise ValueError('no encuentro las entidades')

def basura_fuera_de_comillas(ent):
	q, out = False, []
	for i, b in enumerate(ent):
		if b == 0x22:
			q = not q
		elif not q and b >= 0x80:
			out.append(i)
	return out

def reparar(data):
	ofs, size = lump_entidades(data)
	ent = bytearray(data[ofs:ofs + size])
	cambios = []
	for i in basura_fuera_de_comillas(ent):
		cambios.append('byte 0x%02X fuera de comillas' % ent[i])
		ent[i] = 0x20
	if WTC_MAL in ent:
		assert len(WTC_MAL) == len(WTC_BIEN)
		p = ent.index(WTC_MAL)
		ent[p:p + len(WTC_MAL)] = WTC_BIEN
		cambios.append('entidad *110 reconstruida')
	return data[:ofs] + bytes(ent) + data[ofs + size:], cambios

def main():
	root = sys.argv[1]
	copias = os.path.join(root, '_mapas_originales')
	if '--deshacer' in sys.argv:
		for mod in os.listdir(copias):
			for f in os.listdir(os.path.join(copias, mod)):
				shutil.copy2(os.path.join(copias, mod, f), os.path.join(root, mod, 'maps', f))
				print('restaurado', mod, f)
		return
	for mod in sorted(os.listdir(root)):
		maps = os.path.join(root, mod, 'maps')
		if mod.startswith('_') or not os.path.isdir(maps):
			continue
		for f in sorted(os.listdir(maps)):
			if not f.lower().endswith('.bsp'):
				continue
			path = os.path.join(maps, f)
			data = open(path, 'rb').read()
			try:
				nuevo, cambios = reparar(data)
			except (ValueError, struct.error):
				continue
			if not cambios:
				continue
			os.makedirs(os.path.join(copias, mod), exist_ok=True)
			copia = os.path.join(copias, mod, f)
			if not os.path.exists(copia):
				shutil.copy2(path, copia)
			assert len(nuevo) == len(data)
			open(path, 'wb').write(nuevo)
			print('%s/%s: %s' % (mod, f, ', '.join(sorted(set(cambios)))))

main()
