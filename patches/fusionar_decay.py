# Fusiona Decay: Singularity (decay_single, la conversion a un jugador de lymphoid)
# dentro de Half-Life: Decay (decay, la cooperativa): "Nueva partida" empieza
# Singularity en hts01 y "Multijugador" la campana cooperativa de los dy_*.
#
# - Lo que solo existe en decay_single se copia (mapas hts*, sonidos, modelos...).
# - Lo que existe en los dos con distinto contenido se queda el de decay (modelos,
#   decay.dll, sentences.txt, hud.txt), salvo:
#     decay.wad   se juntan las texturas de los dos (cada uno tiene 44 propias)
#     titles.txt  se anaden los textos que solo tiene Singularity
# - Singularity cambia la pista de CD 28 (Suspense07.mp3) por la suya, pero los dy_*
#   usan la 28 original: la suya pasa a ser la pista 29 (media/cdaudio.txt) y en
#   hts01 y hts10 sus trigger/target_cdaudio pasan de 28 a 29.
# - gameinfo.txt y liblist.gam dejan de ser solo multijugador y arrancan en hts01.
#
# Todo lo que se sustituye va a decay/_antes_fusion/, con la lista de lo anadido en
# anadidos.txt. Para deshacer: python fusionar_decay.py <carpeta juego> --deshacer
#
#   python fusionar_decay.py <carpeta juego>
import os, re, shutil, struct, sys

NO_COPIAR = {  # configuracion del jugador, iconos y cosas de decay_single
	'config.cfg', 'config.cfg.bak', 'opengl.cfg', 'opengl.cfg.bak', 'video.cfg', 'video.cfg.bak',
	'vfs.cfg', 'settings.scr', 'user.scr', 'console_history.txt', 'custom.hpk', 'voice_ban.dt',
	'.xash_id', 'buffer.dat', 'gameinfo.txt', 'liblist.gam', 'game.ico', 'dsingle_big.png',
	'resource/decay_single_spanish.txt',
}
MP3_SINGULARITY = 'Suspense07_singularity.mp3'
PISTA_NUEVA = 29

def rutas(root):
	out = {}
	for dp, dn, fn in os.walk(root):
		dn[:] = [d for d in dn if d.lower() not in ('.fontcache', '_antes_fusion')]
		for f in fn:
			r = os.path.relpath(os.path.join(dp, f), root).replace(os.sep, '/')
			out[r.lower()] = r
	return out

def respaldar(decay, rel):
	src = os.path.join(decay, rel)
	dst = os.path.join(decay, '_antes_fusion', rel)
	if os.path.exists(src) and not os.path.exists(dst):
		os.makedirs(os.path.dirname(dst), exist_ok=True)
		shutil.copy2(src, dst)

def leer_wad(path):
	d = open(path, 'rb').read()
	magic, n, ofs = struct.unpack_from('<4sii', d, 0)
	assert magic == b'WAD3', path
	lumps = []
	for i in range(n):
		pos, disk, size, typ, comp = struct.unpack_from('<iiibb', d, ofs + i * 32)
		name = d[ofs + i * 32 + 16:ofs + i * 32 + 32]
		lumps.append((name.split(b'\0')[0].lower(), name, size, typ, comp, d[pos:pos + disk]))
	return lumps

def juntar_wads(base, extra, out):
	lumps = leer_wad(base)
	tiene = {l[0] for l in lumps}
	nuevos = [l for l in leer_wad(extra) if l[0] not in tiene]
	lumps += nuevos
	data = bytearray(b'\0' * 12)
	info = bytearray()
	for _, name, size, typ, comp, blob in lumps:
		info += struct.pack('<iiibbh', len(data), len(blob), size, typ, comp, 0) + name
		data += blob
	struct.pack_into('<4sii', data, 0, b'WAD3', len(lumps), len(data))
	open(out, 'wb').write(bytes(data + info))
	return len(nuevos)

def anadir_titulos(base, extra):
	"""Anade a base los bloques de extra que no tiene, con las $directivas vigentes."""
	tb = open(base, 'rb').read().decode('cp1252')
	te = open(extra, 'rb').read().decode('cp1252')
	tiene = {k.upper() for k in re.findall(r'^\s*([A-Za-z0-9_]+)\s*\r?\n\s*\{', tb, re.M)}
	estado, salida, n = {}, [], 0
	for m in re.finditer(r'^\s*(\$\w+[^\r\n]*)|^\s*([A-Za-z0-9_]+)\s*\r?\n\s*\{(.*?)\}', te, re.M | re.S):
		if m.group(1):
			estado[m.group(1).split()[0].lower()] = m.group(1).strip()
		elif m.group(2).upper() not in tiene:
			salida += list(estado.values()) + [m.group(2), '{' + m.group(3) + '}', '']
			n += 1
	if n:
		cola = '\r\n\r\n// --- textos de Decay: Singularity (patches/fusionar_decay.py) ---\r\n'
		open(base, 'wb').write((tb.rstrip() + cola + '\r\n'.join(salida)).encode('cp1252'))
	return n

def cambiar_pista(bsp, de, a):
	"""Cambia "health" "de" por "a" en los trigger/target_cdaudio (mismo largo de texto)."""
	b = bytearray(open(bsp, 'rb').read())
	ofs, ln = struct.unpack_from('<ii', b, 4)
	ents = b[ofs:ofs + ln].decode('latin-1')
	n = 0
	def sub(m):
		nonlocal n
		if re.search(r'"classname"\s+"\w+_cdaudio"', m.group(0)):
			nuevo, k = re.subn(r'("health"\s+")%d(")' % de, r'\g<1>%d\2' % a, m.group(0))
			n += k
			return nuevo
		return m.group(0)
	ents2 = re.sub(r'\{[^{}]*\}', sub, ents)
	assert len(ents2) == len(ents)
	b[ofs:ofs + ln] = ents2.encode('latin-1')
	open(bsp, 'wb').write(bytes(b))
	return n

def reescribir_entidades(bsp, cambiar):
	"""Pasa el texto de entidades por cambiar(); si cambia de largo, lo pone al final del fichero."""
	b = bytearray(open(bsp, 'rb').read())
	ofs, ln = struct.unpack_from('<ii', b, 4)
	ents = b[ofs:ofs + ln].rstrip(b'\0').decode('latin-1')
	nuevo = cambiar(ents).encode('latin-1') + b'\0'
	if len(nuevo) <= ln:
		b[ofs:ofs + ln] = nuevo.ljust(ln, b'\0')
	else:
		b += b'\0' * (-len(b) % 4)
		struct.pack_into('<ii', b, 4, len(b), len(nuevo))
		b += nuevo
	open(bsp, 'wb').write(bytes(b))

def mover_escaneres(bsp):
	"""Singularity subio el escaner de retina dentro de su eye_scanner.mdl y lo puso a ras
	de suelo; con el modelo de decay sale en el suelo. Se sube la entidad lo que la sube
	decay en el mismo sitio (hts01 frente a dy_accident1): 46 arriba y 3 hacia la pared."""
	import math
	n = 0
	def ent(m):
		nonlocal n
		e = m.group(0)
		if not re.search(r'"classname"\s+"item_eyescanner"', e):
			return e
		yaw = math.radians(float((re.search(r'"angles"\s+"([^"]*)"', e) or [0, '0 0 0'])[1].split()[1]))
		def org(o):
			x, y, z = (float(v) for v in o.group(2).split())
			x, y, z = x - 3 * math.cos(yaw), y - 3 * math.sin(yaw), z + 46
			return '%s%g %g %g"' % (o.group(1), round(x), round(y), round(z))
		n += 1
		return re.sub(r'("origin"\s+")([^"]*)"', org, e)
	reescribir_entidades(bsp, lambda t: re.sub(r'\{[^{}]*\}', ent, t))
	return n

STEAM_ES = r'C:\Program Files (x86)\Steam\steamapps\common\Half-Life\valve_spanish'

def voces_hl_es(decay, origen=STEAM_ES):
	"""Decay usa muchas voces de Half-Life (cientificos, Barney, holograma...) que no trae
	y que por eso suenan en ingles desde valve. Copia las de la version espanola de Steam
	que no esten ya en decay (las que si estan son el doblaje propio de Decay)."""
	if not os.path.isdir(origen):
		print('no esta', origen, '- voces de Half-Life sin traducir')
		return []
	copiadas = []
	for r in sorted(rutas(os.path.join(origen, 'sound')).values()):
		rel = 'sound/' + r
		dst = os.path.join(decay, rel)
		if os.path.exists(dst):
			continue
		os.makedirs(os.path.dirname(dst), exist_ok=True)
		shutil.copy2(os.path.join(origen, 'sound', r), dst)
		copiadas.append(rel)
	print('voces de Half-Life en castellano: %d' % len(copiadas))
	return copiadas

def editar_gameinfo(path):
	t = open(path, 'rb').read().decode('latin-1')
	t = re.sub(r'^(\s*startmap\s+)"[^"]*"', r'\1"hts01"', t, flags=re.M | re.I)
	t = re.sub(r'^\s*gamemode\s+"multiplayer_only"[^\n]*\n', '', t, flags=re.M | re.I)
	t = re.sub(r'^(\s*type\s+)"Multiplayer"', r'\1"Single"', t, flags=re.M | re.I)
	open(path, 'wb').write(t.encode('latin-1'))

def editar_liblist(path):
	t = open(path, 'rb').read().decode('latin-1')
	t = re.sub(r'^(\s*Startmap\s+)"[^"]*"', r'\1"hts01"', t, flags=re.M | re.I)
	t = re.sub(r'^\s*type\s+"multiplayer_only"[^\n]*\n?', '', t, flags=re.M | re.I)
	open(path, 'wb').write(t.encode('latin-1'))

def deshacer(decay):
	resp = os.path.join(decay, '_antes_fusion')
	for r in open(os.path.join(resp, 'anadidos.txt'), encoding='utf-8').read().split('\n'):
		if r and os.path.exists(os.path.join(decay, r)):
			os.remove(os.path.join(decay, r))
	for r in rutas(resp).values():
		if r != 'anadidos.txt':
			shutil.copy2(os.path.join(resp, r), os.path.join(decay, r))
	shutil.rmtree(resp)
	print('fusion deshecha')

def main():
	if len(sys.argv) < 2:
		sys.exit('uso: python fusionar_decay.py <carpeta juego> [--deshacer]')
	juego = sys.argv[1]
	decay, single = os.path.join(juego, 'decay'), os.path.join(juego, 'decay_single')
	if '--deshacer' in sys.argv:
		return deshacer(decay)
	if '--escaneres' in sys.argv:  # solo este arreglo, sobre una fusion ya hecha sin el
		for m in ('hts01', 'hts03', 'hts09'):
			print('%s: %d escaner(es) de retina subidos' % (m, mover_escaneres(os.path.join(decay, 'maps', m + '.bsp'))))
		return
	if '--voces-hl' in sys.argv:  # solo este paso, sobre una fusion ya hecha sin el
		lista = os.path.join(decay, '_antes_fusion', 'anadidos.txt')
		previas = open(lista, encoding='utf-8').read()
		nuevas = voces_hl_es(decay)
		if nuevas:
			open(lista, 'w', encoding='utf-8').write(previas + '\n' + '\n'.join(nuevas))
		return
	if os.path.exists(os.path.join(decay, '_antes_fusion')):
		sys.exit('ya fusionado (existe decay/_antes_fusion); deshacer primero')
	os.makedirs(os.path.join(decay, '_antes_fusion'))

	A, B = rutas(decay), rutas(single)
	anadidos = []
	for k in sorted(B.keys() - A.keys()):
		r = B[k]
		if k in NO_COPIAR or k == 'media/suspense07.mp3':
			continue
		if k.startswith('save/'):  # las partidas van a la carpeta SAVE que ya usa decay
			r = 'SAVE/' + r.split('/', 1)[1]
		dst = os.path.join(decay, r)
		os.makedirs(os.path.dirname(dst) or decay, exist_ok=True)
		shutil.copy2(os.path.join(single, B[k]), dst)
		anadidos.append(r)
	print('copiados', len(anadidos), 'ficheros que solo tenia Singularity')

	respaldar(decay, 'decay.wad')
	print('decay.wad: +%d texturas' % juntar_wads(os.path.join(decay, 'decay.wad'), os.path.join(single, 'decay.wad'), os.path.join(decay, 'decay.wad')))

	respaldar(decay, 'titles.txt')
	print('titles.txt: +%d textos' % anadir_titulos(os.path.join(decay, 'titles.txt'), os.path.join(single, 'titles.txt')))

	# pista de CD propia de Singularity
	shutil.copy2(os.path.join(single, 'media', 'Suspense07.mp3'), os.path.join(decay, 'media', MP3_SINGULARITY))
	anadidos.append('media/' + MP3_SINGULARITY)
	lista = open(os.path.join(juego, 'valve', 'media', 'cdaudio.txt')).read().split()
	assert len(lista) == PISTA_NUEVA - 1
	open(os.path.join(decay, 'media', 'cdaudio.txt'), 'w').write('\n'.join(lista + [MP3_SINGULARITY]) + '\n')
	anadidos.append('media/cdaudio.txt')
	for m in ('hts01', 'hts10'):
		print('%s: %d pistas 28 -> %d' % (m, cambiar_pista(os.path.join(decay, 'maps', m + '.bsp'), 28, PISTA_NUEVA), PISTA_NUEVA))

	for m in ('hts01', 'hts03', 'hts09'):
		print('%s: %d escaner(es) de retina subidos' % (m, mover_escaneres(os.path.join(decay, 'maps', m + '.bsp'))))

	anadidos += voces_hl_es(decay)

	for f, editar in (('gameinfo.txt', editar_gameinfo), ('liblist.gam', editar_liblist)):
		respaldar(decay, f)
		editar(os.path.join(decay, f))
	# gameinfo.txt tiene que seguir siendo mas nuevo que liblist.gam, o el motor lo regenera
	ahora = os.path.getmtime(os.path.join(decay, 'gameinfo.txt'))
	os.utime(os.path.join(decay, 'liblist.gam'), (ahora - 60, ahora - 60))

	open(os.path.join(decay, '_antes_fusion', 'anadidos.txt'), 'w', encoding='utf-8').write('\n'.join(anadidos))
	print('hecho; copia de lo sustituido en decay/_antes_fusion')

main()
