# Pone en castellano lo que Counter-Life (cl) trae propio y en ingles. Lo que hereda de
# valve ya lo estaba (menu, voces de Half-Life: voces_spanish.py; fuente: fuente_spanish.py).
#
#   python cl_spanish.py <carpeta juego> [--deshacer]
#
# - titles.txt: los textos que son iguales a los de Half-Life en ingles se cambian por los
#   del titles.txt de Steam valve_spanish; los que Counter-Life cambio (entrenamiento con
#   dinero, cuchillo) y los suyos propios (menus de compra) se traducen aqui. Las posiciones
#   y colores ($position...) se quedan como los tenia el mod. Se escribe en cp1252.
# - gfx/shell/kb_act.lst, settings.scr, user.scr: lista de teclas y opciones (texto directo,
#   no claves #Valve_*, como en Sven Co-op).
# - sound/radio: las 43 voces son las de CS en ingles; se ponen las castellanas de cstrike
#   (las que voces_spanish.py ya puso alli), solo si el .wav del mod es identico al ingles.
# - maps/clc_nuke.bsp: el game_text "GAME OVER". La lista de entidades crece, asi que se
#   escribe al final del fichero y se apunta a ella desde la cabecera.
# - dlls/cl.dll: los avisos de compra ("You have insufficient funds!"...). Se reescriben en
#   el sitio, con la misma longitud o menos (el resto, ceros). Sin tildes: salen por el
#   centerprint, que usa la fuente de consola (cp1251).
# - sprites/640hud*.spr: las tarjetas de armas del HUD llevan la ranura escrita (PRIMARY,
#   SECONDARY, MELEE...). Se borra la etiqueta rellenando cada fila con su color de fondo y
#   se escribe en castellano con Century Gothic cursiva, calibrada para que PRIMARIA mida
#   como PRIMARY. Las de 320 no llevan texto.
#
# Los originales se guardan en cl/_castellano_en/ con la misma ruta; --deshacer los devuelve.
import glob, hashlib, os, re, shutil, struct, sys
from collections import Counter

STEAM = r'C:\Program Files (x86)\Steam\steamapps\common\Half-Life'
FONT = 'C:/Windows/Fonts/GOTHICBI.TTF'
COPIA = '_castellano_en'

# --- titles.txt: entradas que Counter-Life cambio o anadio --------------------------------
TITULOS = {
	'HZMEDKIT': 'PARA UTILIZAR LOS BOTIQUINES PULSA "USAR".\nPUEDES COMPRAR ARMAS Y MUNICIÓN\nPULSANDO LA TECLA CORRESPONDIENTE.\nAQUÍ SOLO PUEDES COMPRAR SALUD.',
	'HZCROWBAR': 'ACÉRCATE HASTA EL CUCHILLO.\nPULSA "ATAQUE1" PARA CORTAR OBJETOS.\nPULSA "ATAQUE2" PARA APUÑALAR OBJETOS.',
	'HZWEAPON': '¡NO LE HAGAS CASO! EN COUNTER-LIFE LAS ARMAS\nSON AHORA FAJOS DE BILLETES.\nCADA FAJO VALE 250 $.',
	'HZARMOR': 'PULSA "USAR" PARA ABRIR EL MENÚ DE COMPRA.\nDESDE AHÍ, COMPRA LAS ARMAS QUE QUIERAS.\nAQUÍ SOLO PUEDES COMPRAR BLINDAJE.',
	'SPEC_MODE1': 'Modo seguir cámara cerrado',
	'SPEC_MODE2': 'Modo seguir cámara abierto',
	'SPEC_MODE3': 'Modo vagar abierto',
	'LATENCY': 'Laten.',
	'SPEC_HELP': 'INTRO para jugar  DISPARO para el siguiente objetivo  DISPARO ALT. para el anterior  SALTO para cambiar de modo',
	'SPEC_HELP2': 'Tus mensajes de texto solo los ven los demás espectadores',
	'BUY': 'Comprar\n\n1. Pistola\n2. Escopeta\n3. Subfusil\n4. Fusil\n5. Ametralladora\n\n6. Equipo\n\n7. Munición\n8. Armas de CS:CZ\n\n0. Salir',
	'BUYPISTOL': 'Comprar pistola                          $   Precio\n\n1. H&K USP .45 Tactical                500\n2. Glock18 Select Fire                  400\n3. Desert Eagle .50AE                   650\n4. SIG P228                                    600\n5. Dual Beretta 96G Elite            1000\n6. FN FiveSeven                            750\n\n0. Salir',
	'BUYSHOTGUN': 'Comprar escopeta                        $   Precio\n\n1. Benelli M3 Super90                  1700\n2. Benelli M4 Super90                  3000\n\n0. Salir',
	'BUYSUBMACHINEGUN': 'Comprar subfusil                          $   Precio\n\n1. H&K MP5-Navy                               1500\n2. Steyr Tactical Machine Pistol        1250\n3. FN P90                                            2350\n4. Ingram Mac10                                 1400\n5. H&K UMP45                                     1700\n\n\n0. Salir',
	'BUYRIFLE': 'Comprar fusil de asalto                 $   Precio\n\n1. AK-47                                       2500\n2. Sig SG-552 Commando            3500\n3. Colt M4A1 Carbine                   3100\n4. Steyr Aug                                 3500\n\n\nComprar fusil de francotirador\n\n5. Steyr Scout                               2750\n6. AI Arctic Warfare/Magnum      4750\n7. H&K G3/SG-1                                5000\n8. Sig SG-550                                  4200\n\n0. Salir',
	'BUYMACHINEGUN': 'Comprar ametralladora                  $   Precio\n\n1. FN M249 Para                              5750\n\n0. Salir',
	'BUYCSCZ': 'Comprar fusiles de CS:CZ               $   Precio\n\n1. Galil\n2. FAMAS\n\n0. Salir',
	'BUYEQUIP1': 'Comprar equipo                           $   Precio\n\n1. Chaleco de kevlar                       650\n2. Granada explosiva                     300\n3. Todas las granadas                    300 (cada una)\n4. Gafas de visión nocturna         1250\n\n0. Salir',
	'BUYEQUIP2': 'Comprar equipo                           $   Precio\n\n1. Botiquín                                      10 (cada uno)\n2. Granada explosiva                     300\n3. Todas las granadas                    300 (cada una)\n4. Gafas de visión nocturna         1250\n\n0. Salir',
	'BUYAMMO': 'Comprar munición\n\n1. Un cargador\n2. Munición completa\n\n0. Salir',
}

# --- kb_act.lst, settings.scr, user.scr ---------------------------------------------------
MENU = {
	# kb_act.lst
	'Communication': 'Comunicación',
	'Chat message': 'Mensaje de chat',
	'Team chat': 'Mensaje al equipo',
	'Display multiplayer scores': 'Ver puntuaciones',
	'Menu': 'Menú',
	**{'Menu Choice %d' % i: 'Opción %d del menú' % i for i in range(10)},
	'Movement': 'Movimiento',
	'Move forward': 'Avanzar',
	'Move back': 'Retroceder',
	'Turn left': 'Girar a la izquierda',
	'Turn right': 'Girar a la derecha',
	'Move left (strafe)': 'Desplazarse a la izquierda',
	'Move right (strafe)': 'Desplazarse a la derecha',
	'Jump': 'Saltar',
	'Duck': 'Agacharse',
	'Walk': 'Caminar',
	'Swim up': 'Nadar hacia arriba',
	'Swim down': 'Nadar hacia abajo',
	'Look up': 'Mirar arriba',
	'Look down': 'Mirar abajo',
	'Look straight ahead': 'Mirar al frente',
	'Strafe modifier': 'Modificador de desplazamiento',
	'Mouse look modifier': 'Modificador de vista con ratón',
	'Keyboard look modifier': 'Modificador de vista con teclado',
	'Action Commands': 'Acciones',
	'Primary attack': 'Ataque principal',
	'Secondary attack': 'Ataque secundario',
	'Reload weapon': 'Recargar arma',
	'Drop Weapon': 'Soltar arma',
	'Previous weapon': 'Arma anterior',
	'Next weapon': 'Arma siguiente',
	'Last used weapon': 'Última arma usada',
	'Enable NightVision Goggles': 'Gafas de visión nocturna',
	'Use item (buttons, scientists, ...)': 'Usar (botones, científicos...)',
	'Misc Commands': 'Otros',
	'Show Bank Account': 'Ver dinero en el banco',
	'Flashlight': 'Linterna',
	'Spray logo': 'Pintar logotipo',
	'Take screen shot': 'Hacer captura de pantalla',
	'Quick save': 'Guardado rápido',
	'Quick load': 'Carga rápida',
	'Pause game': 'Pausar partida',
	'Quit game': 'Salir del juego',
	'Cheating (for l*sers)': 'Trucos (para tramposos)',
	'Money cheat': 'Truco: dinero',
	'No money cheat': 'Truco: sin dinero',
	# settings.scr (Crear partida)
	'Max amount of money': 'Dinero máximo',
	'Starting amount of money': 'Dinero inicial',
	'Buytime Limit (Sec.)': 'Tiempo de compra (s)',
	'Valid teams, leave blank for all': 'Equipos válidos (en blanco: todos)',
	'Frag Limit': 'Límite de frags',
	'Time Limit (Min.)': 'Límite de tiempo (min)',
	'Falling Damage': 'Daño por caída',
	'Normal': 'Normal',
	'Realistic': 'Realista',
	'Teamplay': 'Por equipos',
	'Friendly Fire': 'Fuego amigo',
	'Weapons Stay': 'Las armas no desaparecen',
	'Force Respawn': 'Reaparición automática',
	'Footsteps': 'Pasos',
	'Allow monsters': 'Permitir monstruos',
	'Autocrosshair': 'Punto de mira automático',
	'Low ping server physics': 'Física para ping bajo',
	# user.scr (opciones de Counter-Life)
	'Buy menu limit': 'Tiempo del menú de compra',
	'Max money limit': 'Dinero máximo',
	"Monster's health uses skill.cfg": 'Salud de los monstruos según skill.cfg',
	'No': 'No',
	'Yes': 'Sí',
	'Kill monsters to get/deduct cash': 'Ganar o perder dinero al matar monstruos',
	'Use custom colours for HUD': 'Colores propios en el HUD',
	'How long the shell stays': 'Tiempo que duran los casquillos',
	'Multiplayer decal limit': 'Límite de calcomanías en multijugador',
}

# --- cl.dll -------------------------------------------------------------------------------
DLL = {
	'You lost money for killing an ally': 'Pierdes dinero por matar aliados',
	'Bank: $%i\n': 'Saldo $%i\n',
	"You can't use this yet!\n": 'No puedes usarlo ahora\n',
	'You do not need ammunition for this!\n': 'Esto no necesita balas!\n',
	'You already have one\n': 'Ya tienes uno\n',
	'You cannot carry anymore\n': 'Ya no te cabe nada\n',
	'You already have 15 HEGrenades!\n': 'Ya tienes 15 granadas HE!\n',
	'You already have a Machine Gun\n': 'Ya tienes una ametralladora\n',
	'You already have a Sniper Rifle\n': 'Ya tienes un fusil de tirador\n',
	'You already have an Assault Rifle\n': 'Ya tienes un fusil de asalto\n',
	'You already have a Sub-Machine Gun\n': 'Ya tienes un subfusil\n',
	'You already have a Shotgun\n': 'Ya tienes una escopeta\n',
	'You already have a Handgun\n': 'Ya tienes una pistola\n',
	'You have insufficient funds!\n': 'No tienes suficiente dinero!\n',
}

# --- sprites ------------------------------------------------------------------------------
ETIQUETA = {'PRIMARY': 'PRIMARIA', 'SECONDARY': 'SECUNDARIA', 'MELEE': 'CUCHILLO', 'C4': 'C4',
	'HE GRENADE': 'GRANADA HE', 'FLASHBANG': 'CEGADORA', 'SG GRENADE': 'HUMO'}
RANURA = {'knife': 'MELEE', 'hegrenade': 'HE GRENADE', 'flashbang': 'FLASHBANG', 'smokegrenade': 'SG GRENADE',
	'c4': 'C4', 'usp': 'SECONDARY', 'glock18': 'SECONDARY', 'deagle': 'SECONDARY', 'p228': 'SECONDARY',
	'elite': 'SECONDARY', 'fiveseven': 'SECONDARY', 'shield': 'SECONDARY'}   # el resto, PRIMARY
# (el escudo usa la tarjeta de la Desert Eagle)


def guardar(cl, rel):
	"""Copia el original a _castellano_en la primera vez."""
	dst = os.path.join(cl, COPIA, rel)
	if not os.path.exists(dst):
		os.makedirs(os.path.dirname(dst), exist_ok=True)
		shutil.copy2(os.path.join(cl, rel), dst)


def original(cl, rel):
	"""Ruta del original (el guardado si ya se paso el script antes)."""
	copia = os.path.join(cl, COPIA, rel)
	return copia if os.path.exists(copia) else os.path.join(cl, rel)


# --- titles.txt ---------------------------------------------------------------------------
def leer_titulos(path):
	lineas = open(path, encoding='cp1252').read().replace('\r', '').split('\n')
	textos, i = {}, 0
	while i < len(lineas):
		clave = lineas[i].strip()
		if clave and not clave.startswith(('//', '$')) and i + 1 < len(lineas) and lineas[i + 1].strip() == '{':
			j = i + 2
			while j < len(lineas) and lineas[j].strip() != '}':
				j += 1
			textos[clave.upper()] = '\n'.join(lineas[i + 2:j])
			i = j
		i += 1
	return textos


def titulos(cl):
	rel = 'titles.txt'
	en = leer_titulos(os.path.join(STEAM, 'valve', 'titles.txt'))
	es = leer_titulos(os.path.join(STEAM, 'valve_spanish', 'titles.txt'))
	lineas = open(original(cl, rel), encoding='cp1252').read().replace('\r', '').split('\n')
	salida, i, n, faltan = [], 0, 0, []
	while i < len(lineas):
		clave = lineas[i].strip()
		if clave and not clave.startswith(('//', '$')) and i + 1 < len(lineas) and lineas[i + 1].strip() == '{':
			j = i + 2
			while j < len(lineas) and lineas[j].strip() != '}':
				j += 1
			texto = '\n'.join(lineas[i + 2:j])
			k = clave.upper()
			if k in TITULOS:
				texto = TITULOS[k]
			elif k in en and k in es and texto.strip() == en[k].strip():
				texto = es[k]
			else:
				faltan.append(clave)
			salida += [lineas[i], '{', texto]
			n += 1
			i = j
			continue
		salida.append(lineas[i])
		i += 1
	guardar(cl, rel)
	open(os.path.join(cl, rel), 'w', encoding='cp1252', newline='\r\n').write('\n'.join(salida))
	print('titles.txt: %d textos' % n + ('; SIN TRADUCIR: ' + ', '.join(faltan) if faltan else ''))


# --- menus --------------------------------------------------------------------------------
def menus(cl):
	for rel in ['gfx/shell/kb_act.lst', 'settings.scr', 'user.scr']:
		texto = open(original(cl, rel), encoding='cp1252').read()
		faltan = []

		def cambiar(m):
			s = m.group(2)
			if s in MENU:
				s = MENU[s]
			elif re.search('[a-z]', s) and not re.match(r'^[a-z_0-9]+$', s) and s[0] not in '+#=':
				faltan.append(s)   # los nombres de cvar se quedan
			return '%s"%s"%s' % (m.group(1), s, m.group(3))

		if rel.endswith('.lst'):
			# solo la segunda columna: "comando"   "descripcion"
			patron = r'^(\s*"[^"]*"\s+)"([^"]*)"()'
		else:
			# las descripciones van solas en su linea; los valores de las listas, "texto" "valor"
			patron = r'^(\s*)"([^"]*)"(\s*$|\s+"\d+")'
		texto = re.sub(patron, cambiar, texto, flags=re.M)
		guardar(cl, rel)
		open(os.path.join(cl, rel), 'w', encoding='cp1252', newline='').write(texto)
		print('%s%s' % (rel, '; SIN TRADUCIR: ' + ', '.join(faltan) if faltan else ''))


# --- radio --------------------------------------------------------------------------------
def md5(p):
	return hashlib.md5(open(p, 'rb').read()).hexdigest()


def radio(cl, juego):
	cs = os.path.join(juego, 'cstrike')
	n = 0
	for wav in sorted(glob.glob(os.path.join(cl, 'sound', 'radio', '*.wav'))):
		nombre = os.path.basename(wav)
		rel = 'sound/radio/' + nombre
		ingles = os.path.join(cs, '_voces_en', 'sound', 'radio', nombre)
		castellano = os.path.join(cs, 'sound', 'radio', nombre)
		if not (os.path.exists(ingles) and os.path.exists(castellano)):
			continue
		if md5(original(cl, rel)) != md5(ingles):
			continue
		guardar(cl, rel)
		shutil.copy2(castellano, wav)
		n += 1
	print('radio: %d voces' % n)


# --- mapa ---------------------------------------------------------------------------------
def mapa(cl):
	rel = 'maps/clc_nuke.bsp'
	d = bytearray(open(original(cl, rel), 'rb').read())
	ofs, tam = struct.unpack_from('<ii', d, 4)
	ents = bytes(d[ofs:ofs + tam])
	nuevas = ents.replace(b'"message" "GAME OVER"', '"message" "FIN DE LA PARTIDA"'.encode('cp1252'))
	if nuevas == ents:
		print(rel + ': no se encontro "GAME OVER"')
		return
	d += b'\0' * (-len(d) % 4)
	struct.pack_into('<ii', d, 4, len(d), len(nuevas))
	d += nuevas
	guardar(cl, rel)
	open(os.path.join(cl, rel), 'wb').write(d)
	print(rel + ': GAME OVER -> FIN DE LA PARTIDA')


# --- cl.dll -------------------------------------------------------------------------------
def dll(cl):
	rel = 'dlls/cl.dll'
	d = bytearray(open(original(cl, rel), 'rb').read())
	n = 0
	for en, es in DLL.items():
		a, b = en.encode('ascii'), es.encode('ascii')
		assert len(b) <= len(a), es
		patron = b'\0' + a + b'\0'
		pos = d.find(patron)
		if pos < 0 or d.find(patron, pos + 1) >= 0:
			print('cl.dll: no se encuentra una sola vez: %r' % en)
			continue
		d[pos + 1:pos + 1 + len(a)] = b + b'\0' * (len(a) - len(b))
		n += 1
	guardar(cl, rel)
	open(os.path.join(cl, rel), 'wb').write(d)
	print('cl.dll: %d mensajes' % n)


# --- sprites ------------------------------------------------------------------------------
def lum(c):
	return (c[0] * 299 + c[1] * 587 + c[2] * 114) // 1000


class Sprite:
	def __init__(self, path):
		from PIL import Image
		self.data = bytearray(open(path, 'rb').read())
		version, = struct.unpack_from('<i', self.data, 4)
		assert self.data[:4] == b'IDSP' and version == 2, path
		o = 40
		n, = struct.unpack_from('<h', self.data, o)
		self.pal = bytes(self.data[o + 2:o + 2 + n * 3])
		o += 2 + n * 3
		w, h = struct.unpack_from('<ii', self.data, o + 12)
		self.pixels = o + 20
		self.img = Image.frombytes('P', (w, h), bytes(self.data[self.pixels:self.pixels + w * h]))
		self.img.putpalette(self.pal)
		self.cache = {}

	def color(self, i):
		return tuple(self.pal[i * 3:i * 3 + 3])

	def cercano(self, rgb):
		if rgb not in self.cache:
			self.cache[rgb] = min(range(len(self.pal) // 3), key=lambda i: sum((self.pal[i * 3 + k] - rgb[k]) ** 2 for k in range(3)))
		return self.cache[rgb]

	def bytes(self):
		self.data[self.pixels:self.pixels + len(self.img.tobytes())] = self.img.tobytes()
		return bytes(self.data)


def caja_etiqueta(spr, x, y, w, h):
	"""Caja de la etiqueta, abajo a la izquierda de la tarjeta. Una columna es de texto si tiene
	claros en la franja y no justo encima (encima solo hay arma); huecos de hasta 12 px."""
	px = spr.img.load()
	claro = lambda cx, cy: lum(spr.color(px[cx, cy])) > 120
	y0, y1 = y + 30, y + h - 3
	cols = [cx for cx in range(x + 4, x + w - 4)
		if any(claro(cx, cy) for cy in range(y0, y1)) and not any(claro(cx, cy) for cy in range(y0 - 3, y0 - 1))]
	if not cols or cols[0] > x + 60:   # la de las Elite va entre las dos pistolas
		return None
	fin = cols[0]
	for c in cols:
		if c - fin > 12:
			break
		fin = c
	filas = [cy for cy in range(y0, y1) if any(claro(cx, cy) for cx in range(cols[0], fin + 1))]
	return cols[0], min(filas), fin, max(filas)


class Letra:
	SS, ALTO, TRAZO, ESPACIADO = 4, 8, 1, 1.2

	def __init__(self):
		from PIL import ImageFont
		f = ImageFont.truetype(FONT, 100)
		caja = f.getbbox('H')
		self.font = ImageFont.truetype(FONT, int(100 * self.ALTO * self.SS / (caja[3] - caja[1])))
		m = self.pintar('PRIMARY')
		caja = m.getbbox()
		self.escala = 56 / ((caja[2] - caja[0]) / self.SS)   # PRIMARY mide 56 px en los originales

	def pintar(self, texto):
		from PIL import Image, ImageDraw
		f, ss = self.font, self.SS
		espacio = self.ESPACIADO * f.size / 10
		m = Image.new('L', (int(sum(f.getlength(c) + espacio for c in texto)) + 16 * ss, (self.ALTO + 8) * ss), 0)
		d, x, arriba = ImageDraw.Draw(m), 4 * ss, 4 * ss - f.getbbox('H')[1]
		for c in texto:
			d.text((x, arriba), c, font=f, fill=255, stroke_width=self.TRAZO, stroke_fill=255)
			x += f.getlength(c) + espacio
		return m

	def mascaras(self, texto):
		from PIL import Image, ImageFilter
		m = self.pintar(texto)
		c, ss = m.getbbox(), self.SS
		m = m.crop((c[0] - 2 * ss, c[1] - 2 * ss, c[2] + 2 * ss, c[3] + 2 * ss))
		a = m.resize((max(1, round(m.width / ss * self.escala)), round(m.height / ss)), Image.LANCZOS)
		return a, a.filter(ImageFilter.MaxFilter(3))


def reetiquetar(spr, letra, x, y, w, h, texto):
	caja = caja_etiqueta(spr, x, y, w, h)
	if not caja:
		return False
	x0, y0, x1, y1 = caja
	px = spr.img.load()
	tinta = max((spr.color(px[cx, cy]) for cx in range(x0, x1 + 1) for cy in range(y0, y1 + 1)), key=lum)
	# borrar: cada fila toma su color de fondo (el fondo son lineas horizontales)
	bx0, bx1 = x0 - 2, x1 + 3
	for cy in range(y0 - 2, y1 + 3):
		fondo = [px[cx, cy] for cx in range(x + 3, x + w - 3) if not bx0 <= cx <= bx1 and lum(spr.color(px[cx, cy])) < 90]
		color = Counter(fondo).most_common(1)[0][0] if fondo else px[x + 3, cy]
		for cx in range(bx0, bx1 + 1):
			px[cx, cy] = color
	# escribir: borde oscuro alrededor y la letra con el color de la original
	a, borde = letra.mascaras(texto)
	for j in range(a.height):
		for i in range(a.width):
			X, Y = x0 - 2 + i, y0 - 2 + j
			if not (x <= X < x + w - 2 and y <= Y < y + h - 1):
				continue
			base, o, t = spr.color(px[X, Y]), borde.getpixel((i, j)) / 255, a.getpixel((i, j)) / 255
			c = tuple(int(base[k] * (1 - o * 0.8)) for k in range(3))
			px[X, Y] = spr.cercano(tuple(int(c[k] * (1 - t) + tinta[k] * t) for k in range(3)))
	return True


def sprites(cl):
	tarjetas = {}
	for txt in sorted(glob.glob(os.path.join(cl, 'sprites', 'weapon_*.txt'))):
		arma = os.path.basename(txt)[7:-4]
		for linea in open(txt, encoding='cp1252'):
			p = linea.split()
			if len(p) == 7 and p[0] in ('weapon', 'weapon_s') and p[1] == '640':
				# una tarjeta puede ser de varias armas: se reetiqueta una sola vez
				tarjetas.setdefault(p[2], {})[tuple(map(int, p[3:7]))] = ETIQUETA[RANURA.get(arma, 'PRIMARY')]
	letra = Letra()
	n = 0
	for nombre, cajas in sorted(tarjetas.items()):
		rel = 'sprites/%s.spr' % nombre
		spr = Sprite(original(cl, rel))
		hechas = sum(reetiquetar(spr, letra, *caja, texto) for caja, texto in sorted(cajas.items()))
		if hechas:
			guardar(cl, rel)
			open(os.path.join(cl, rel), 'wb').write(spr.bytes())
			n += hechas
	print('sprites: %d tarjetas' % n)


def deshacer(cl):
	copia = os.path.join(cl, COPIA)
	if not os.path.isdir(copia):
		sys.exit('no hay nada que deshacer')
	for raiz, _, ficheros in os.walk(copia):
		for f in ficheros:
			src = os.path.join(raiz, f)
			shutil.copy2(src, os.path.join(cl, os.path.relpath(src, copia)))
	shutil.rmtree(copia)
	print('restaurado')


def main():
	args = [a for a in sys.argv[1:] if not a.startswith('--')]
	if len(args) != 1:
		sys.exit('uso: python cl_spanish.py <carpeta juego> [--deshacer]')
	juego = args[0]
	cl = os.path.join(juego, 'cl')
	if '--deshacer' in sys.argv:
		return deshacer(cl)
	titulos(cl)
	menus(cl)
	radio(cl, juego)
	mapa(cl)
	dll(cl)
	sprites(cl)


if __name__ == '__main__':
	main()
