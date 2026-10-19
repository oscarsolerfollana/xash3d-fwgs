# Convierte waypoints de PODBot (.pwf, version 6 o 7) en un grafo de YaPB (.graph).
#
# YaPB solo usa data/graph/<mapa>.graph: si no lo tiene, lo intenta bajar de
# yapb.jeefo.net y si tampoco esta, genera uno con su analizador, y no mira los
# .pwf. En mapas con puertas el analizador no pasa por ellas: en cs_siege_apc dejo
# la base T aislada (los terroristas no salian de ella) aunque el cs_siege_apc.pwf
# de PODBot (Count Floyd) conecta todo el mapa.
#
# Copia los waypoints tal cual (flags, radio, direcciones de campeo, conexiones y
# distancias); YaPB calcula luego la visibilidad (.vis) y la experiencia (.pmx).
# Si ya hay un .graph se guarda como .graph.antes-pwf.bak.
#
#   python yapb_pwf_a_graph.py <carpeta cstrike> <mapa>
import os, shutil, struct, sys

MAGIA = 0x59415042  # 'BPAY'
VERSION = 2
OPCIONES = 200      # las de un grafo guardado por YaPB 4.4
TAM_NODO = 220

def varint(n):
	# EncodeMod de ULZ (no es LEB128): el lector suma cada byte entero << 7*i
	out = bytearray()
	while n >= 128:
		n -= 128
		out.append(128 + (n & 127))
		n >>= 7
	out.append(n)
	return bytes(out)

def ulz_sin_comprimir(datos):
	# un unico token ULZ: todo literal menos los 4 ultimos bytes (la visibilidad del
	# ultimo nodo, ceros), que se copian del nodo anterior. YaPB no acepta que el
	# bloque acabe en literales: tiene que terminar con una copia.
	assert datos[-4:] == datos[-4 - TAM_NODO:-TAM_NODO]
	return bytes([7 << 5]) + varint(len(datos) - 4 - 7) + datos[:-4] + struct.pack('<H', TAM_NODO)

def leer_pwf(ruta):
	d = open(ruta, 'rb').read()
	magia, version, n = d[:8], *struct.unpack_from('<2i', d, 8)
	if magia != b'PODWAY!\0' or version not in (6, 7):
		sys.exit('%s: no es un .pwf de PODBot v6/v7' % ruta)
	tam = (len(d) - 80) // n
	nodos = []
	for k in range(n):
		o = 80 + k * tam
		num, flags = struct.unpack_from('<2i', d, o)
		origen = struct.unpack_from('<3f', d, o + 8)
		radio, cx0, cy0, cx1, cy1 = struct.unpack_from('<5f', d, o + 20)
		indices = struct.unpack_from('<8h', d, o + 40)
		distancias = struct.unpack_from('<8i', d, o + 56)
		nodos.append((flags, origen, radio, (cx0, cy0, 0.0), (cx1, cy1, 0.0), indices, distancias))
	return nodos

def nodo_graph(k, flags, origen, radio, inicio, fin, indices, distancias):
	b = struct.pack('<2i', k, flags) + struct.pack('<9f', *origen, *inicio, *fin)
	b += struct.pack('<3f', radio, 0.0, 0.0)  # radio, luz, display
	for i, dist in zip(indices, distancias):
		if i < 0:
			i, dist = -1, 0
		b += struct.pack('<3fiHh', 0.0, 0.0, 0.0, dist, 0, i)
	b += struct.pack('<2H', 0, 0)  # visibilidad
	assert len(b) == TAM_NODO
	return b

def main():
	if len(sys.argv) != 3:
		sys.exit('python yapb_pwf_a_graph.py <carpeta cstrike> <mapa>')
	cs, mapa = sys.argv[1], sys.argv[2]
	datos = os.path.join(cs, 'addons', 'yapb', 'data')
	nodos = leer_pwf(os.path.join(datos, 'pwf', mapa + '.pwf'))
	bruto = b''.join(nodo_graph(k, *n) for k, n in enumerate(nodos))
	comp = ulz_sin_comprimir(bruto)
	cab = struct.pack('<6i', MAGIA, VERSION, OPCIONES, len(nodos), len(comp), len(bruto))
	tam_mapa = os.path.getsize(os.path.join(cs, 'maps', mapa + '.bsp'))
	ext = b'PODBot pwf'.ljust(32, b'\0') + struct.pack('<i', tam_mapa) + b''.ljust(32, b'\0')
	ruta = os.path.join(datos, 'graph', mapa + '.graph')
	if os.path.exists(ruta) and not os.path.exists(ruta + '.antes-pwf.bak'):
		shutil.move(ruta, ruta + '.antes-pwf.bak')
	open(ruta, 'wb').write(cab + comp + ext)
	print('%s: %d nodos' % (ruta, len(nodos)))

if __name__ == '__main__':
	main()
