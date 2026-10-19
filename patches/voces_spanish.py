# Pone en castellano las voces de los juegos de juego/ con el doblaje oficial de Steam
# (Half-Life/valve_spanish y cstrike_spanish). Lo sustituido se guarda en
# <juego>/_voces_en, con la misma ruta; --deshacer lo devuelve a su sitio.
#
#   python voces_spanish.py [--deshacer]
#
# - valve: todas las voces de valve_spanish/sound. Su sentences.txt manda las frases de
#   soldados, megafonia y HEV a sound/sentences/*.wav; las 4 frases que el castellano no
#   trae (HG_SUCKS, HG_CIVVIES, FUEL_OFF, HEV_DEAD1) se quedan en ingles al final.
# - cstrike: radio y rehenes de cstrike_spanish.
# - mods sobre valve (gearbox, cl, pov, rewolf): en su sentences.txt solo cambian las
#   frases que son identicas a las de Half-Life en ingles; las propias del mod no se
#   tocan. Los .wav del mod que son copia exacta del ingles de HL pasan al castellano.
#   Si el sentences.txt va dentro de pak0.pak (pov), se escribe suelto, que tiene
#   prioridad sobre el pak.
# bshift y decay ya llevan el doblaje; Op4 y Gunman no tienen version en castellano.
import hashlib, os, re, shutil, struct, sys

STEAM = r'C:\Program Files (x86)\Steam\steamapps\common\Half-Life'
JUEGO = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'juego')
COPIA = '_voces_en'
MODS = ['gearbox', 'cl', 'pov', 'rewolf']
# erratas del sentences.txt castellano de Steam: el motor toma como directorio todo
# hasta la ultima '/' de la frase, y estas lo rompen
ERRATAS = {
	'C2A4_STER': 'sentences/bizwarn C2A4_ster',      # era 'bizwarn sentences/C2A4_ster'
	'C3A2_5': 'sentences/bizwarn bizwarn C3A2_5',    # era '... bizwarn /C3A2_5'
}


def rutas(raiz):
	out = {}
	for d, _, fs in os.walk(raiz):
		for f in fs:
			p = os.path.join(d, f)
			out[os.path.relpath(p, raiz).replace(os.sep, '/').lower()] = p
	return out


def md5(p):
	return hashlib.md5(open(p, 'rb').read()).hexdigest()


def guardar(juego, rel):
	"""Copia el original antes de pisarlo (solo la primera vez)."""
	src = os.path.join(JUEGO, juego, rel)
	dst = os.path.join(JUEGO, juego, COPIA, rel)
	if os.path.exists(src) and not os.path.exists(dst):
		os.makedirs(os.path.dirname(dst), exist_ok=True)
		shutil.copy2(src, dst)
	elif not os.path.exists(src) and not os.path.exists(dst + '.nuevo'):
		os.makedirs(os.path.dirname(dst), exist_ok=True)
		open(dst + '.nuevo', 'w').close()  # no existia: al deshacer se borra


def copiar(juego, origen):
	n = 0
	for rel, p in sorted(rutas(origen).items()):
		if rel == 'sentences.txt':
			continue
		dst = os.path.join(JUEGO, juego, 'sound', rel)
		if os.path.exists(dst) and md5(dst) == md5(p):
			continue
		guardar(juego, 'sound/' + rel)
		os.makedirs(os.path.dirname(dst), exist_ok=True)
		shutil.copy2(p, dst)
		n += 1
	return n


def frases(texto):
	d = {}
	for l in texto.splitlines():
		s = l.strip()
		if not s or s.startswith('//'):
			continue
		p = s.split(None, 1)
		if len(p) == 2:
			d[p[0].upper()] = ' '.join(p[1].split())
	return d


def faltan_wav(defi, existe):
	"""Los .wav de una frase que no estan, con las reglas de VOX_GetDirectory y
	VOX_ParseString del motor: el directorio llega hasta la ultima '/' de toda la
	frase (sin la primera, si empieza por '/') y las palabras se cortan por espacio,
	coma y punto, sin lo que va entre parentesis."""
	defi = defi.lstrip('/')
	i = defi.rfind('/')
	carpeta, resto = (defi[:i + 1], defi[i + 1:]) if i >= 0 else ('vox/', defi)
	palabras = re.split(r'[ ,.]+', re.sub(r'\([^)]*\)', '', resto))
	if re.search(r'[^\s,.][,.]\S', resto):  # coma pegada a palabra: dice _comma
		palabras.append('_comma')
	return [carpeta + p + '.wav' for p in palabras if p and (carpeta + p + '.wav').lower() not in existe]


def leer_pak(pak, nombre):
	with open(pak, 'rb') as f:
		cab = f.read(12)
		if cab[:4] != b'PACK':
			return None
		off, ln = struct.unpack('<ii', cab[4:])
		f.seek(off)
		for e in [f.read(64) for _ in range(ln // 64)]:
			if e[:56].split(b'\0')[0].decode('latin1').lower() == nombre:
				o, s = struct.unpack('<ii', e[56:])
				f.seek(o)
				return f.read(s)
	return None


def sentences_mod(juego, en, es):
	p = os.path.join(JUEGO, juego, 'sound', 'sentences.txt')
	if os.path.exists(p):
		datos = open(p, 'rb').read()
	else:
		pak = os.path.join(JUEGO, juego, 'pak0.pak')
		datos = leer_pak(pak, 'sound/sentences.txt') if os.path.exists(pak) else None
	if datos is None:
		return 0
	lineas, n = [], 0
	for l in datos.decode('latin1').splitlines():
		p_ = l.split(None, 1)
		k = p_[0].upper() if p_ and not l.strip().startswith('//') else None
		if k and len(p_) == 2 and en.get(k) == ' '.join(p_[1].split()) and k in es and es[k] != en[k]:
			l = k + ' ' + es[k]
			n += 1
		lineas.append(l)
	if n:
		guardar(juego, 'sound/sentences.txt')
		os.makedirs(os.path.dirname(p), exist_ok=True)
		open(p, 'wb').write(('\r\n'.join(lineas) + '\r\n').encode('latin1'))
	return n


def wavs_mod(juego, en_wav, es_dir):
	"""Los .wav del mod que son el ingles de HL tal cual."""
	n = 0
	es_wav = rutas(es_dir)
	for rel, p in rutas(os.path.join(JUEGO, juego, 'sound')).items():
		if rel in es_wav and rel in en_wav and rel != 'sentences.txt' and md5(p) == en_wav[rel]:
			guardar(juego, 'sound/' + rel)
			shutil.copy2(es_wav[rel], p)
			n += 1
	return n


def deshacer():
	for juego in ['valve', 'cstrike'] + MODS:
		copia = os.path.join(JUEGO, juego, COPIA)
		if not os.path.isdir(copia):
			continue
		for rel, p in rutas(copia).items():
			if rel.endswith('.nuevo'):
				dst = os.path.join(JUEGO, juego, rel[:-6])
				if os.path.exists(dst):
					os.remove(dst)
			else:
				shutil.copy2(p, os.path.join(JUEGO, juego, rel))
		shutil.rmtree(copia)
		print(juego, 'devuelto al ingles')


def main():
	if '--deshacer' in sys.argv:
		return deshacer()
	es_dir = os.path.join(STEAM, 'valve_spanish', 'sound')
	valve = os.path.join(JUEGO, 'valve', 'sound')
	# el ingles de HL tal y como esta ahora (o como estaba, si ya se aplico)
	en_txt = os.path.join(JUEGO, 'valve', COPIA, 'sound', 'sentences.txt')
	if not os.path.exists(en_txt):
		en_txt = os.path.join(valve, 'sentences.txt')
	en_wav = {}
	for rel, p in rutas(valve).items():
		g = os.path.join(JUEGO, 'valve', COPIA, 'sound', rel)
		en_wav[rel] = md5(g if os.path.exists(g) else p)
	en = frases(open(en_txt, 'rb').read().decode('latin1'))
	es_bytes = open(os.path.join(es_dir, 'sentences.txt'), 'rb').read()
	es = frases(es_bytes.decode('latin1'))

	print('valve: %d voces' % copiar('valve', es_dir))
	# las alarmas del HEV en castellano piden sentences/buzz.wav, que Steam no trae;
	# es el zumbido de fvox, sin voz
	buzz = os.path.join(valve, 'sentences', 'buzz.wav')
	if not os.path.exists(buzz):
		guardar('valve', 'sound/sentences/buzz.wav')
		shutil.copy2(os.path.join(valve, 'fvox', 'buzz.wav'), buzz)

	# frases del castellano de Steam que apuntan a .wav que no existen (el aviso del
	# tren C1A0_START, C1A3_1, C3A2_5...): sin arreglo posible, mejor la inglesa que
	# el silencio
	existe = set(rutas(valve))
	es.update(ERRATAS)
	rotas = [k for k in es if faltan_wav(es[k], existe) and k in en and not faltan_wav(en[k], existe)]
	for k in rotas:
		es[k] = en[k]
	faltan = [k for k in en if k not in es]
	lineas = []
	for l in es_bytes.decode('latin1').splitlines():
		p_ = l.split(None, 1)
		if p_ and p_[0].upper() in ERRATAS and not l.strip().startswith('//'):
			l = p_[0] + ' ' + ERRATAS[p_[0].upper()]
		elif p_ and p_[0].upper() in rotas and not l.strip().startswith('//'):
			lineas.append('// en ingles: a la castellana le falta algun .wav')
			l = p_[0] + ' ' + en[p_[0].upper()]
		lineas.append(l)
	lineas += ['', '// Sin version en castellano: se quedan las inglesas']
	lineas += [k + ' ' + en[k] for k in faltan]
	guardar('valve', 'sound/sentences.txt')
	open(os.path.join(valve, 'sentences.txt'), 'wb').write(('\r\n'.join(lineas) + '\r\n').encode('latin1'))
	print('valve: sentences.txt en castellano (%d frases, %d en ingles: %s)' % (
		len(es) + len(faltan), len(rotas) + len(faltan), ' '.join(rotas + faltan)))

	print('cstrike: %d voces' % copiar('cstrike', os.path.join(STEAM, 'cstrike_spanish', 'sound')))

	for m in MODS:
		print('%s: %d frases, %d voces' % (m, sentences_mod(m, en, es), wavs_mod(m, en_wav, es_dir)))


main()
