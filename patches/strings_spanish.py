# Traduce las descripciones del menu que cada mod trae en gfx/shell/strings.lst.
#
# mainui carga las traducciones en este orden: gameui, valve, mainui y por ultimo
# <mod>_<idioma>.txt. El mainui_spanish.txt general (valve/resource) trae su propio
# StringsList_N, que pisa los textos del strings.lst del mod; y si el texto lleva %s
# ni siquiera se sustituye ("Aprende a jugar a %s" en CS). Este script escribe los
# StringsList_N de cada mod en resource/<mod>_spanish.txt, que se carga el ultimo.
# Si el fichero ya existe (decay, ns traen uno de Steam) el bloque se mete entre
# las marcas de abajo y se reemplaza al volver a ejecutar.
# Gunman (rewolf) ya trae el strings.lst en castellano (cp1252): se copia tal cual.
# Con ¿ y ¡: el menu.dll de custom-xash los dibuja (FontManager.cpp, rango desde U+00A1).
#
#   python strings_spanish.py <carpeta juego>
import os, re, sys

INICIO = '// --- strings.lst en castellano (patches/strings_spanish.py) ---'
FIN = '// --- fin strings.lst ---'

DECAY = {
	530: "Elegir otro juego",
	188: "Volver a la partida",
	189: "Empezar una partida nueva",
	190: "Empezar el entrenamiento",
	191: "Cargar una partida guardada",
	192: "Cargar o guardar la partida",
	193: "Configurar",
	194: "Ver el archivo de ayuda",
	196: "Salir de Decay",
	198: "Empezar una partida cooperativa de Decay",
	235: "¿Seguro que quieres salir?",
	236: "¿Seguro que quieres abandonar la partida?",
	234: "Si empiezas el entrenamiento se acabará la partida actual.",
	338: "CD no válido",
	340: "Mete el CD del juego en el lector",
}

MODS = {
	'cstrike': {
		189: " ",
		190: "Aprende a jugar a Counter-Strike y prepárate para los combates de verdad.",
		191: "Carga una partida guardada de la misión de entrenamiento.",
		192: "Guarda tu avance en la misión de entrenamiento o carga una partida anterior.",
		193: "Cambia la configuración de vídeo, audio y controles de Counter-Strike.",
		194: "Lee el readme.txt de Half-Life.",
		196: "Salir de Counter-Strike.",
		198: "Conéctate y juega a Counter-Strike con gente de todo el mundo.",
		323: "Desactiva los efectos gráficos. Si se activa, no se podrá jugar a Counter-Strike.",
		324: "Descarga la última versión de Half-Life: Counter-Strike.",
		398: "Si el servidor lo permite, esta opción te ayuda a apuntar a los enemigos.",
		391: "Marca esta casilla y escribe una contraseña para desactivar las imágenes no aptas para "
			"jugadores jóvenes y el multijugador. Para cambiar este ajuste hará falta la contraseña. "
			"IMPORTANTE: si se activa, no se podrá jugar a Counter-Strike.",
		400: "Lee el manual oficial de Counter-Strike y aprende a jugar.",
		416: "Visita Counter-Strike.net",
		426: "Consigue las últimas actualizaciones y noticias oficiales, y toda la red de CS.",
		503: "Desactivado.",
		530: "Juega o descarga otros mods, o vuelve a Half-Life.",
	},
	'decay': {**DECAY, 400: "Abrir el manual del juego"},
	'decay_single': {**DECAY, 400: "Visita la página oficial de este mod."},
	'firearms': {
		400: "Descubre más sobre Firearms.",
		530: "A los traidores se les dispara nada más verlos.",
		188: "Vuelve a la batalla.",
		189: " ", 190: " ", 191: " ", 192: " ",
		193: "Cambia tu configuración.",
		194: "Ver las notas de la versión.",
		196: "¿Rendirte? Un soldado nunca se rinde.",
		198: "Adelante, a repartir leña.",
		235: "Eso es... Huye como un cobarde.",
		236: "¿Adónde crees que vas, soldado?",
		234: " ",
		338: "No se ha podido validar Firearms",
		340: "Ehh... ¡Prueba a meter el CD, pedazo de inútil!",
	},
	'gearbox': {
		400: "Descubre más sobre los juegos de Gearbox Software.",
		530: "Elige otro juego, gusano.",
		188: "Vuelta a la misión actual.",
		189: "Empieza una misión nueva, soldado.",
		190: "A por un poco de instrucción básica.",
		191: "Carga una misión guardada.",
		192: "Carga o guarda una misión en curso.",
		193: "¿Quieres trastear con tu configuración, escoria?",
		194: "Infórmate de la situación actual, hijo.",
		196: "Un buen soldado nunca abandona a su pelotón.",
		198: "Prepárate para el combate. ¡Esto no es un simulacro!",
		235: "¿Vas a desertar, hijo? ¡Abandonarás la misión actual!",
		236: "¿Vas a desertar, hijo?",
		234: "Si vas al campamento de instrucción, se acabará tu misión actual.",
		338: "No se ha podido validar Opposing Force",
		340: "Mete el CD de Opposing Force",
	},
	'ns': {
		400: "Descubre más sobre Natural Selection.",
		530: "Jugar a otro juego.",
		188: "Vuelve a la partida actual.",
		189: " ",
		190: "Entrenamiento. Mejor hazlo antes de jugar en línea.",
		191: " ", 192: " ",
		193: "Cambia tu configuración.",
		194: "Ver las notas de la versión.",
		196: "Salir.",
		198: "Multijugador. Empieza tu viaje.",
		235: "¿Seguro que quieres salir?",
		236: "¿Te vas? La mente colmena te espera...",
		234: "¿Entrenamiento? ¿Con la que está cayendo?",
		338: "No se ha podido validar Natural Selection",
		340: "Mete el CD de Half-Life",
	},
	'SvenCoop': {
		189: " ", 190: " ", 191: " ", 192: " ", 197: " ",
		193: "Configura los controles y las opciones de vídeo y sonido.",
		194: "Ver el archivo Léeme de Half-Life.",
		196: "¿Así que crees que puedes irte tan fácilmente, eh?",
		198: "Crea o únete a una partida de Sven Co-op en red local, en Internet o tú solo.",
		236: "¿Acaso deseas marcharte con tan presuroso abandono?",
		323: "Cómo quitar la sangre y los restos (mirar, pero no tocar).",
		391: "Si activas el control parental, Sven Co-op NO funcionará. Para quitar la sangre y los "
			"restos, usa las opciones de violencia del menú de comandos. (Recuerda asignar una tecla "
			"al menú de comandos en las opciones de controles.)",
		398: "No recomendado",
		400: "Visita la web de Sven Co-op",
		402: "www.svencoop.com",
		410: "Modelos de jugador con más polígonos. No recomendado",
		416: "La web de Sven Co-op",
		417: "www.svencoop.com",
		426: "Visita la web de Sven Co-op",
		503: "Elige un modelo de jugador",
		530: "Cambiar a otro mod",
		200: "Para jugar un mapa tú solo, crea una partida en red local.",
		201: "Para jugar en cooperativo la campaña de Half-Life,",
		202: "empieza una partida con el mapa singleplayermaps.bsp",
		203: "¡Pulsa Cancelar para volver al menú principal!",
	},
}

def leer_strings_lst(path):
	"""Pares (numero, texto) de un strings.lst en cp1252."""
	texto = open(path, 'rb').read().decode('cp1252')
	return [(int(n), s) for n, s in re.findall(r'^\s*(\d+)\s+"([^"]*)"', texto, re.M)]

def bloque(cadenas):
	lineas = [INICIO]
	for n, s in sorted(cadenas.items()):
		s = s.replace('\\', '\\\\')
		lineas.append('"StringsList_%d"\t"%s"' % (n, s))
	lineas.append(FIN)
	return lineas

def escribir(juego, mod, cadenas):
	path = os.path.join(juego, mod, 'resource', '%s_spanish.txt' % mod)
	nuevas = bloque(cadenas)
	if os.path.exists(path):
		raw = open(path, 'rb').read()
		assert raw[:2] == b'\xff\xfe', path + ': se esperaba UTF-16 LE'
		lineas = raw[2:].decode('utf-16-le').split('\r\n')
		if INICIO in lineas:
			del lineas[lineas.index(INICIO):lineas.index(FIN) + 1]
		# antes de las dos llaves que cierran "Tokens" y "lang"
		cierres = [i for i, l in enumerate(lineas) if l.strip() == '}']
		lineas[cierres[-2]:cierres[-2]] = nuevas
	else:
		os.makedirs(os.path.dirname(path), exist_ok=True)
		lineas = ['"lang"', '{', '"Language" "spanish"', '"Tokens"', '{'] + nuevas + ['}', '}', '']
	open(path, 'wb').write(b'\xff\xfe' + '\r\n'.join(lineas).encode('utf-16-le'))
	print('%s: %d cadenas' % (path, len(cadenas)))

def main():
	if len(sys.argv) != 2:
		sys.exit('uso: python strings_spanish.py <carpeta juego>')
	juego = sys.argv[1]
	for mod, cadenas in MODS.items():
		lst = os.path.join(juego, mod, 'gfx', 'shell', 'strings.lst')
		# avisa de textos del strings.lst que se quedarian sin traducir (las URL y rutas no)
		for n, s in leer_strings_lst(lst):
			if n not in cadenas and not re.search(r'www\.|http|\\', s):
				print('  aviso: %s %d sin traducir: %s' % (mod, n, s))
		escribir(juego, mod, cadenas)
	escribir(juego, 'rewolf', dict(leer_strings_lst(os.path.join(juego, 'rewolf', 'gfx', 'shell', 'strings.lst'))))

main()
