# Rehace en castellano las imagenes del menu de Blue Shift (gfx/shell):
# btns_main.bmp (69 botones x 3 estados de 156x26) y las cabeceras head_*.bmp.
# Imita el estilo original: letra con remates (Rockwell) azul con brillo; el tamano
# se calibra para que "NEW GAME" mida lo mismo que en las imagenes originales.
# Los originales en ingles se guardan en gfx/shell/_original_en/ la primera vez.
#
#   python bshift_menu_es.py <carpeta bshift> [--calibrar salida.png]
import os, shutil, sys
from PIL import Image, ImageDraw, ImageFont, ImageFilter, ImageChops

FONT = 'C:/Windows/Fonts/ROCKB.TTF'

# mismo orden que en btns_main.bmp
BOTONES = [
	'NUEVA PARTIDA', 'REANUDAR PARTIDA', 'ENTRENAMIENTO', 'CONFIGURACIÓN',
	'CARGAR PARTIDA', 'GUARDAR/CARGAR', 'VER LÉAME', 'SALIR',
	'MULTIJUGADOR', 'FÁCIL', 'NORMAL', 'DIFÍCIL',
	'GUARDAR PARTIDA', 'CARGAR PARTIDA', 'CANCELAR', 'OPCIONES DE JUEGO',
	'VÍDEO', 'AUDIO', 'CONTROLES', 'HECHO',
	'INICIO RÁPIDO', 'PREDETERMINADOS', 'ACEPTAR', 'OPCIONES DE VÍDEO',
	'MODOS DE VÍDEO', 'CONTROL AVANZADO', 'COMPRAR HALF-LIFE', 'BORRAR',
	'PARTIDAS INTERNET', 'SALAS DE CHAT', 'PARTIDAS EN LAN', 'PERSONALIZAR',
	'SALTAR', 'SALIR', 'CONECTAR', 'ACTUALIZAR',
	'FILTRO', 'FILTRO', 'CREAR', 'CREAR PARTIDA',
	'SALAS DE CHAT', 'LISTA DE SALAS', 'BUSCAR', 'SERVIDORES',
	'UNIRSE', 'BUSCAR', 'CREAR SALA', 'UNIRSE A PARTIDA',
	'BUSCAR PARTIDAS', 'BUSCAR PARTIDA', 'EMPEZAR PARTIDA', 'INFO. PARTIDA',
	'ACTUALIZAR', 'AÑADIR SERVIDOR', 'DESCONECTAR', 'CONSOLA',
	'CONTROL CONTENIDO', 'ACTUALIZAR', 'VISITAR WON', 'AVANCES',
	'OPC. AVANZADAS', 'WEB INFO 3D', 'PARTIDA PERSONAL.', 'ACTIVAR',
	'INSTALAR', 'SITIO WEB', 'ACTUALIZAR', 'DESACTIVAR',
	'OPC. AVANZADAS',
]

CABECERAS = {
	'advanced': 'CONTROLES AVANZADOS', 'advoptions': 'OPCIONES AVANZADAS',
	'audio': 'AUDIO', 'config': 'CONFIGURACIÓN', 'controls': 'CONTROLES',
	'creategame': 'CREAR PARTIDA', 'createroom': 'CREAR SALA DE CHAT',
	'custom': 'PARTIDA PERSONALIZADA', 'customize': 'PERSONALIZAR',
	'filter': 'FILTRO', 'gameopts': 'OPCIONES DE JUEGO',
	'gore': 'CONTROL DE CONTENIDO', 'inetgames': 'PARTIDAS EN INTERNET',
	'keyboard': 'TECLADO', 'lan': 'PARTIDA EN LAN', 'load': 'CARGAR PARTIDA',
	'multi': 'MULTIJUGADOR', 'newgame': 'NUEVA PARTIDA', 'readme': 'LÉAME',
	'room': 'SALA DE CHAT', 'rooms': 'SALAS DE CHAT', 'save': 'GUARDAR PARTIDA',
	'saveload': 'GUARDAR/CARGAR', 'single': 'UN JUGADOR', 'video': 'VÍDEO',
	'vidmodes': 'MODOS DE VÍDEO', 'vidoptions': 'OPCIONES DE VÍDEO',
}

BW, BH = 156, 26
AZUL_TEXTO = (63, 85, 198)
AZUL_FONDO = (74, 84, 196)
AZUL_CAB = (68, 81, 230)
AZUL_BRILLO = (51, 48, 181)

# medidas del texto (sin halo) de "NEW GAME" en los originales
BTN_REF = (78, 11)    # ancho, alto
CAB_REF = (220, 29)

def mascara(texto, versal=False):
	"""Texto en mascara L a tamano grande, recortado a las mayusculas (sin tildes)."""
	f = ImageFont.truetype(FONT, 100)
	fp = ImageFont.truetype(FONT, 90) if versal else f
	im = Image.new('L', (110 * len(texto) + 40, 220), 0)
	d = ImageDraw.Draw(im)
	x, primera = 20, True
	for ch in texto:
		g = f if (primera or not versal) else fp
		d.text((x, 170), ch, font=g, fill=255, anchor='ls')
		x += g.getlength(ch)
		primera = False
	# la altura de referencia es la de una mayuscula sin tilde
	cap = f.getbbox('H', anchor='ls')
	box = im.getbbox()
	return im.crop((box[0], 0, box[2], im.height)), 170 + cap[1], 170

def escalado(texto, ref, versal, ancho_max):
	"""Escala la mascara para que "NEW GAME" mediria exactamente ref."""
	m_ref, top_ref, base_ref = mascara('NEW GAME', versal)
	sx = ref[0] / m_ref.width
	sy = ref[1] / (base_ref - top_ref)
	m, top, base = mascara(texto, versal)
	w = max(1, round(m.width * sx))
	if w > ancho_max:
		w = ancho_max
	h = round(m.height * sy)
	m = m.resize((w, h), Image.LANCZOS)
	return m, round(top * sy), round(base * sy)

def coloreado(mask, color):
	return Image.composite(Image.new('RGB', mask.size, color), Image.new('RGB', mask.size, (0, 0, 0)), mask)

def pegar(mask, top, size, x, y_cap):
	m = Image.new('L', size, 0)
	m.paste(mask, (x, y_cap - top))
	return m

def boton(texto):
	mask, top, base = escalado(texto, BTN_REF, True, BW - 12)
	m = pegar(mask, top, (BW, BH), 6, 4)
	linea = 4 + (base - top) + 1

	# estado 0: texto azul, con la primera letra subrayada (tecla rapida)
	e0 = coloreado(m, AZUL_TEXTO)
	m1, t1, _ = escalado(texto[0], BTN_REF, True, BW)
	ImageDraw.Draw(e0).line((6, linea, 6 + m1.width - 1, linea), fill=AZUL_TEXTO)

	# estado 1: mancha azul que tapa el texto
	blob = m.filter(ImageFilter.MaxFilter(9)).filter(ImageFilter.GaussianBlur(2))
	e1 = coloreado(blob.point(lambda v: 255 if v > 90 else int(v * 2.5)), AZUL_FONDO)

	# estado 2: texto oscuro sobre una mancha mas ajustada
	halo = m.filter(ImageFilter.MaxFilter(5)).filter(ImageFilter.GaussianBlur(1.5))
	e2 = coloreado(halo.point(lambda v: min(255, int(v * 1.6))), AZUL_FONDO)
	e2 = Image.composite(Image.new('RGB', m.size, (0, 0, 0)), e2, m.point(lambda v: 255 if v > 110 else 0))
	return [e0, e1, e2]

def cabecera(texto, size):
	w, h = size
	mask, top, base = escalado(texto, CAB_REF, False, w - 30)
	m = pegar(mask, top, size, (w - mask.width) // 2, 25)
	brillo = m.filter(ImageFilter.GaussianBlur(6)).point(lambda v: min(255, int(v * 3.0)))
	return ImageChops.lighter(coloreado(brillo, AZUL_BRILLO), coloreado(m.filter(ImageFilter.BoxBlur(0.6)), AZUL_CAB))

def main():
	bshift = sys.argv[1]
	shell = os.path.join(bshift, 'gfx', 'shell')
	orig = os.path.join(shell, '_original_en')
	if '--calibrar' in sys.argv:
		src = Image.open(os.path.join(orig if os.path.isdir(orig) else shell, 'btns_main.bmp')).convert('RGB')
		h = Image.open(os.path.join(orig if os.path.isdir(orig) else shell, 'head_newgame.bmp')).convert('RGB')
		hoja = Image.new('RGB', (BW * 2, BH * 3 + h.height * 4))
		hoja.paste(src.crop((0, 0, BW, BH * 3)), (0, 0))
		for i, e in enumerate(boton('NEW GAME')):
			hoja.paste(e, (BW, i * BH))
		y = BH * 3
		for im in (h, cabecera('NEW GAME', h.size), cabecera('NUEVA PARTIDA', h.size), cabecera('CONFIGURACIÓN', h.size)):
			hoja.paste(im.crop(((h.width - BW * 2) // 2, 0, (h.width + BW * 2) // 2, h.height)), (0, y)); y += h.height
		hoja.resize((hoja.width * 2, hoja.height * 2), Image.NEAREST).save(sys.argv[sys.argv.index('--calibrar') + 1])
		return

	os.makedirs(orig, exist_ok=True)
	for f in os.listdir(shell):
		if f == 'btns_main.bmp' or f.startswith('head_'):
			if not os.path.exists(os.path.join(orig, f)):
				shutil.copy2(os.path.join(shell, f), os.path.join(orig, f))

	src = Image.open(os.path.join(orig, 'btns_main.bmp'))
	assert src.height == BH * 3 * len(BOTONES), (src.height, len(BOTONES))
	hoja = Image.new('RGB', src.size)
	for k, t in enumerate(BOTONES):
		for s, e in enumerate(boton(t)):
			hoja.paste(e, (0, (k * 3 + s) * BH))
	# el original es de 8 bits con paleta
	hoja.quantize(256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE).save(os.path.join(shell, 'btns_main.bmp'))

	for nombre, texto in CABECERAS.items():
		f = 'head_%s.bmp' % nombre
		o = Image.open(os.path.join(orig, f))
		im = cabecera(texto, o.size)
		if o.mode == 'P':
			im = im.quantize(256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
		im.save(os.path.join(shell, f))
	print('hecho:', len(BOTONES), 'botones y', len(CABECERAS), 'cabeceras')

if __name__ == '__main__':
	main()
