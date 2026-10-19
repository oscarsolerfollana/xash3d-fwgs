# Tildes y ñ en los textos del HUD (game_text, env_message, titles.txt).
#
#   python fuente_spanish.py [--deshacer]
#
# Si el gfx.wad que carga el juego es el original de Valve, Xash no usa su creditsfont
# sino creditsfont_<con_charset>.fnt de extras.pk3, que con el cp1251 de serie es
# cirilica: la ó sale como "у" y la é como "й" (EVALUACIYN). Los mods sin gfx.wad propio
# heredan el de valve, asi que se les pone el gfx.wad de Steam valve_spanish: no es el
# original, y su creditsfont si tiene áéíóúñü.
# Ademas, el titles.txt de uplink mezcla cp1252 con trozos en UTF-8 ("VarÃ³n"): se
# pasa todo a cp1252, que es lo que pinta el motor con hud_utf8 0.
# Lo sustituido se guarda en <juego>/_fuente_en.
import os, re, shutil, sys

STEAM_GFX = r'C:\Program Files (x86)\Steam\steamapps\common\Half-Life\valve_spanish\gfx.wad'
JUEGO = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'juego')
COPIA = '_fuente_en'
# los que tienen titles.txt en castellano y no traen gfx.wad
SIN_GFX = ['backlife', 'decay', 'decay_single', 'gearbox', 'tfc', 'uplink']


def guardar(juego, rel):
	src = os.path.join(JUEGO, juego, rel)
	dst = os.path.join(JUEGO, juego, COPIA, rel)
	if os.path.exists(dst) or os.path.exists(dst + '.nuevo'):
		return
	os.makedirs(os.path.dirname(dst), exist_ok=True)
	if os.path.exists(src):
		shutil.copy2(src, dst)
	else:
		open(dst + '.nuevo', 'w').close()  # no existia: al deshacer se borra


def deshacer():
	for juego in os.listdir(JUEGO):
		copia = os.path.join(JUEGO, juego, COPIA)
		if not os.path.isdir(copia):
			continue
		for f in os.listdir(copia):
			dst = os.path.join(JUEGO, juego, f[:-6] if f.endswith('.nuevo') else f)
			if f.endswith('.nuevo'):
				if os.path.exists(dst):
					os.remove(dst)
			else:
				shutil.copy2(os.path.join(copia, f), dst)
		shutil.rmtree(copia)
		print(juego, 'deshecho')


def main():
	if '--deshacer' in sys.argv:
		return deshacer()
	for juego in SIN_GFX:
		if not os.path.isdir(os.path.join(JUEGO, juego)):
			continue
		guardar(juego, 'gfx.wad')
		shutil.copy2(STEAM_GFX, os.path.join(JUEGO, juego, 'gfx.wad'))
		print(juego, ': gfx.wad castellano')

	p = os.path.join(JUEGO, 'uplink', 'titles.txt')
	d = open(p, 'rb').read()
	n = len(re.findall(rb'[\xc2-\xf4][\x80-\xbf]+', d))
	if n:
		guardar('uplink', 'titles.txt')
		d = re.sub(rb'[\xc2-\xf4][\x80-\xbf]+', lambda m: m.group().decode('utf-8').encode('cp1252'), d)
		open(p, 'wb').write(d)
	print('uplink: titles.txt, %d caracteres de UTF-8 a cp1252' % n)


main()
