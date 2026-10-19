# Genera resource/valve_spanish.txt: los textos de valve_english.txt que salen en el
# menu de Xash (lista de teclas de Controles y opciones de Crear partida). Lo que no
# esta aqui se queda en ingles, porque mainui carga primero el ingles y encima el
# castellano. Sin ¿ ni ¡: la fuente del menu no los tiene.
#
#   python valve_spanish.py <salida.txt>
import sys

ES = [
	("Valve_Listen_MapName", "Mapa"),
	("Valve_Movement_Title", "MOVIMIENTO"),
	("Valve_Move_Forward", "Avanzar"),
	("Valve_Move_Back", "Retroceder"),
	("Valve_Turn_Left", "Girar a la izquierda"),
	("Valve_Turn_Right", "Girar a la derecha"),
	("Valve_Move_Left", "Desplazarse a la izquierda"),
	("Valve_Move_Right", "Desplazarse a la derecha"),
	("Valve_Jump", "Saltar"),
	("Valve_Duck", "Agacharse"),
	("Valve_Swim_Up", "Nadar hacia arriba"),
	("Valve_Swim_Down", "Nadar hacia abajo"),
	("Valve_Look_Up", "Mirar arriba"),
	("Valve_Look_Down", "Mirar abajo"),
	("Valve_Look_Straight_Ahead", "Mirar al frente"),
	("Valve_Strafe_Modifier", "Modificador de desplazamiento"),
	("Valve_Mouse_Look_Modifier", "Modificador de vista con ratón"),
	("Valve_Keyboard_Look_Modifier", "Modificador de vista con teclado"),
	("Valve_Use_Items", "Usar (botones, científicos...)"),
	("Valve_Communication_Title", "COMUNICACIÓN"),
	("Valve_Use_Voice_Communication", "Hablar por voz"),
	("Valve_Chat_Message", "Mensaje de chat"),
	("Valve_Team_Message", "Mensaje al equipo"),
	("Valve_Combat_Title", "COMBATE"),
	("Valve_Primary_Attack", "Ataque principal"),
	("Valve_Secondary_Attack", "Ataque secundario"),
	("Valve_Reload_Weapon", "Recargar arma"),
	("Valve_Walk", "Caminar"),
	("Valve_Flashlight", "Linterna"),
	("Valve_Spray_Logo", "Pintar logotipo"),
] + [("Valve_Weapon_Category_%d" % i, "Categoría de arma %d" % i) for i in (1, 2, 3, 4, 5, 6, 7, 8, 9, 0)] + [
	("Valve_Previous_Weapon", "Arma anterior"),
	("Valve_Next_Weapon", "Arma siguiente"),
	("Valve_Last_Weapon_Used", "Última arma usada"),
	("Valve_Miscellaneous_Title", "VARIOS"),
	("Valve_Display_Scores", "Ver puntuaciones (multijugador)"),
	("Valve_Take_Screen_Shot", "Hacer captura de pantalla"),
	("Valve_Quick_Save", "Guardado rápido"),
	("Valve_Quick_Load", "Carga rápida"),
	("Valve_Pause_Game", "Pausar partida"),
	("Valve_Quit_Game", "Salir del juego"),
	("Valve_Cheer", "Animar"),
	("Valve_Activate_In_Game_GUI", "Activar menú del juego"),
	("Valve_Change_Team", "Cambiar de equipo"),
	("Valve_Change_Class", "Cambiar de clase"),
	("Valve_Use_Special_Skill", "Usar habilidad especial"),
	("Valve_Multiplayer_Decal_Limit", "Límite de calcomanías (multijugador)"),
	("Valve_Valid_Teams", "Equipos válidos (vacío para todos)"),
	("Valve_Frag_Limit", "Límite de muertes"),
	("Valve_Time_Limit", "Límite de tiempo (min.)"),
	("Valve_Falling_Damage", "Daño por caída"),
	("Valve_Normal", "Normal"),
	("Valve_Realistic", "Realista"),
	("Valve_Teamplay", "Por equipos"),
	("Valve_Friendly_Fire", "Fuego amigo"),
	("Valve_Weapons_Stay", "Las armas se quedan"),
	("Valve_Force_Respawn", "Forzar reaparición"),
	("Valve_Footsteps", "Pasos"),
	("Valve_Autocrosshair", "Punto de mira automático"),
	("Valve_Center_Player_Names", "Centrar nombres de jugadores"),
	("Valve_Switch_Weapons_Immediately", "Cambiar de arma al momento"),
	("Valve_Automatic_Screenshots", "Captura automática al acabar la partida"),
	("Valve_Observer_Crosshair", "Punto de mira de observador"),
	("Valve_Hostname", "Nombre del servidor"),
	("Valve_Max_Players", "Máx. jugadores"),
	("Valve_Server_Password", "Contraseña del servidor"),
	("Valve_LowPingPhys", "Física de servidor de ping bajo"),
]

def main():
	if len(sys.argv) != 2:
		sys.exit(__doc__ or "uso: python valve_spanish.py <salida.txt>")
	out = ['"lang"', '{', '"Language" "spanish"', '"Tokens"', '{']
	out += ['"%s"\t"%s"' % kv for kv in ES]
	out += ['}', '}', '']
	# UTF-16 LE con BOM, como los valve_*.txt de Valve
	open(sys.argv[1], 'wb').write(b'\xff\xfe' + '\r\n'.join(out).encode('utf-16-le'))
	print('escritas %d cadenas en %s' % (len(ES), sys.argv[1]))

main()
