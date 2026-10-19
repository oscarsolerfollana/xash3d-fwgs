# Lista de teclas del menu de Sven Co-op (gfx/shell/kb_act.lst) en castellano.
# A diferencia del de valve, que usa claves #Valve_*, el de Sven Co-op trae el texto
# en ingles directamente, asi que se traduce el fichero (en cp1252, como el de Gunman).
# El original se guarda en SvenCoop/_menu_en/kb_act.lst.
# El resto del menu: strings_spanish.py (descripciones), ui_language "spanish" en
# vfs.cfg y render_picbutton_text 1 en gameinfo.txt.
#
#   python svencoop_spanish.py <carpeta juego>
import os, re, shutil, sys

ES = {
	"Sven Co-op Commands": "Comandos de Sven Co-op",
	"Command Menu (Important)": "Menú de comandos (importante)",
	"Call for a medic or medic NPC": "Pedir un médico (jugador o PNJ)",
	"Yell to warn other players": "Gritar para avisar a los demás",
	"Commit suicide (use if stuck)": "Suicidarse (si te quedas atascado)",
	"Drop current weapon": "Soltar el arma actual",
	"Drop ammo": "Soltar munición",
	"Toggle third-person viewmode": "Cambiar a vista en tercera persona",
	"Use Voice Communication": "Hablar por voz",
	"Map briefing / Server MOTD": "Instrucciones del mapa / mensaje del servidor",
	"Voting Menu": "Menú de votaciones",
	"MP3 Menu": "Menú de MP3",
	"-----Basic MP3 commands:-----": "-----Comandos básicos de MP3:-----",
	"Play": "Reproducir",
	"Next track": "Pista siguiente",
	"Previous track": "Pista anterior",
	"Stop / Turn off MP3 player": "Parar / apagar el reproductor de MP3",
	"----Optional NPC Commands:----": "----Órdenes a los PNJ (opcional):----",
	"Return to me": "Volver conmigo",
	"Stop and find cover": "Parar y ponerse a cubierto",
	"Attack my target": "Atacar a mi objetivo",
	"Half-life Standard Commands": "Comandos estándar de Half-Life",
	"Move forward": "Avanzar",
	"Move back": "Retroceder",
	"Turn left": "Girar a la izquierda",
	"Turn right": "Girar a la derecha",
	"Move left (strafe)": "Desplazarse a la izquierda",
	"Move right (strafe)": "Desplazarse a la derecha",
	"Jump": "Saltar",
	"Duck": "Agacharse",
	"Swim up": "Nadar hacia arriba",
	"Swim down": "Nadar hacia abajo",
	"Look up": "Mirar arriba",
	"Look down": "Mirar abajo",
	"Look straight ahead": "Mirar al frente",
	"Strafe modifier": "Modificador de desplazamiento",
	"Mouse look modifier": "Modificador de vista con ratón",
	"Keyboard look modifier": "Modificador de vista con teclado",
	"Primary attack": "Ataque principal",
	"Secondary attack": "Ataque secundario",
	"Reload weapon": "Recargar arma",
	"Use item (buttons, scientists, ...)": "Usar (botones, científicos...)",
	"Walk": "Caminar",
	"Flashlight": "Linterna",
	"Spray logo": "Pintar logotipo",
	"Previous weapon": "Arma anterior",
	"Next weapon": "Arma siguiente",
	"Last used weapon": "Última arma usada",
	"Display multiplayer scores": "Ver puntuaciones",
	"Chat message": "Mensaje de chat",
	"Team message": "Mensaje al equipo",
	"Take screen shot": "Hacer captura de pantalla",
	"Quick save": "Guardado rápido",
	"Quick load": "Carga rápida",
	"Pause game": "Pausar partida",
	"Quit game": "Salir del juego",
}


def main():
	if len(sys.argv) != 2:
		sys.exit('uso: python svencoop_spanish.py <carpeta juego>')
	sven = os.path.join(sys.argv[1], 'SvenCoop')
	lst = os.path.join(sven, 'gfx', 'shell', 'kb_act.lst')
	orig = os.path.join(sven, '_menu_en', 'kb_act.lst')
	if not os.path.exists(orig):
		os.makedirs(os.path.dirname(orig), exist_ok=True)
		shutil.copy2(lst, orig)
	salida, faltan = [], []
	for l in open(orig, 'rb').read().decode('cp1252').split('\r\n'):
		m = re.match(r'^(\s*"[^"]*"\s+")([^"]*)(".*)$', l)
		if m and m.group(2) and not set(m.group(2)) <= set('= '):
			texto = re.sub(r'Weapon category (\d)', r'Categoría de arma \1', m.group(2))
			texto = ES.get(texto, texto)
			if texto == m.group(2):
				faltan.append(texto)
			l = m.group(1) + texto + m.group(3)
		salida.append(l)
	open(lst, 'wb').write('\r\n'.join(salida).encode('cp1252'))
	print('kb_act.lst en castellano' + (' (sin traducir: %s)' % faltan if faltan else ''))


main()
