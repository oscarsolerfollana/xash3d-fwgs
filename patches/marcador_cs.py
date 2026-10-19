# Marcador de artilleria de Firearms (custom-xash): copia la granada de humo de
# Counter-Strike para el bote en la mano y el que vuela (engine/client/cl_marcador.c,
# engine/server/sv_marcador.c), y crea el sprite del humo volumetrico
# (engine/client/cl_humo.c): varias bocanadas de 256x256, con relieve sombreado
# desde arriba, en un sprite de 32 bits (version 32 del formato, la de Xash).
#
#   python marcador_cs.py
#
# Sin estos ficheros el motor vuelve a la granada de concusion de Firearms; sin el
# sprite, al humo de siempre del client.dll.
import os, shutil, struct, sys
import numpy as np

JUEGO = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'juego')
CS = os.path.join(JUEGO, 'cstrike', 'models')
FA = os.path.join(JUEGO, 'firearms')

TAM = 256
FRAMES = 6


def ruido(rng, n, octavas):
	# fbm de rejillas aleatorias interpoladas, periodico no hace falta
	total = np.zeros((n, n))
	amp, suma = 1.0, 0.0
	for o in range(octavas):
		celdas = 4 * 2 ** o
		g = rng.random((celdas + 1, celdas + 1))
		x = np.linspace(0, celdas, n, endpoint=False)
		i = x.astype(int)
		f = x - i
		f = f * f * (3 - 2 * f)
		a = g[i][:, i] * (1 - f)[None, :] + g[i][:, i + 1] * f[None, :]
		b = g[i + 1][:, i] * (1 - f)[None, :] + g[i + 1][:, i + 1] * f[None, :]
		total += amp * (a * (1 - f)[:, None] + b * f[:, None])
		suma += amp
		amp *= 0.5
	return total / suma


def bocanada(semilla):
	rng = np.random.default_rng(semilla)
	n = TAM
	y, x = np.mgrid[0:n, 0:n] / (n - 1) * 2 - 1
	# muchas bolas pequenas que se suman: las volutas de la bocanada
	dens = np.zeros((n, n))
	for k in range(22):
		cx, cy = rng.normal(0, 0.2, 2)
		r = rng.uniform(0.14, 0.3)
		d2 = ((x - cx) ** 2 + (y - cy) ** 2) / (r * r)
		dens += np.clip(1 - d2, 0, 1) ** 1.5
	dens = np.clip(dens / np.percentile(dens[dens > 0], 90), 0, None)
	# ruido con el dominio deformado: jirones en el borde y detalle dentro
	wx = ruido(rng, n, 3) - 0.5
	wy = ruido(rng, n, 3) - 0.5
	r1 = ruido(rng, n, 6)
	ix = np.clip(((x + wx * 0.4 + 1) / 2 * (n - 1)).astype(int), 0, n - 1)
	iy = np.clip(((y + wy * 0.4 + 1) / 2 * (n - 1)).astype(int), 0, n - 1)
	det = r1[iy, ix]
	dens = dens * (0.6 + 0.8 * det)
	# el borde del sprite siempre a cero
	borde = np.clip((1 - np.sqrt(x * x + y * y)) / 0.3, 0, 1)
	dens = np.clip(dens * borde, 0, None)
	alfa = np.clip(dens * 1.8, 0, 1)
	# sombreado: cada voluta como una cupula, luz de arriba y de cara
	alto = np.sqrt(np.clip(dens, 0, 1.5)) * 14.0
	gy, gx = np.gradient(alto)
	nz = 1.0 / np.sqrt(gx * gx + gy * gy + 1.0)
	# filas de la imagen hacia abajo: la luz viene de y negativa (arriba)
	luz = np.array([0.3, -0.7, 0.65]); luz /= np.linalg.norm(luz)
	l = np.clip((-gx * nz) * luz[0] + (-gy * nz) * luz[1] + nz * luz[2], 0, 1)
	# los huecos entre volutas, mas oscuros: la densidad de alrededor menos la del punto
	from numpy.fft import fft2, ifft2
	k = np.exp(-(x * x + y * y) / 0.01)
	alrededor = np.real(ifft2(fft2(dens) * fft2(np.fft.ifftshift(k / k.sum()))))
	hueco = np.clip(alrededor - dens, 0, 1)
	tono = 0.42 + 0.62 * l - 0.5 * hueco - 0.08 * np.clip(y, 0, 1)
	tono = np.clip(tono, 0, 1)
	rgba = np.zeros((n, n, 4), np.uint8)
	rgba[..., 0] = rgba[..., 1] = rgba[..., 2] = (tono * 255).astype(np.uint8)
	rgba[..., 3] = (alfa * 255).astype(np.uint8)
	return rgba


def sprite(ruta):
	frames = [bocanada(100 + k) for k in range(FRAMES)]
	cab = struct.pack('<4si i f ii i f i', b'IDSP', 32, 2, TAM * 0.71, TAM, TAM, FRAMES, 0.0, 1)
	with open(ruta, 'wb') as f:
		f.write(cab)
		for fr in frames:
			f.write(struct.pack('<i', 0))
			f.write(struct.pack('<iiii', -TAM // 2, TAM // 2, TAM, TAM))
			f.write(fr.tobytes())


def main():
	for de, a in (('v_smokegrenade.mdl', 'v_humo_cs.mdl'), ('w_smokegrenade.mdl', 'w_humo_cs.mdl')):
		src = os.path.join(CS, de)
		if not os.path.isfile(src):
			sys.exit('falta ' + src)
		shutil.copyfile(src, os.path.join(FA, 'models', a))
		print('models/' + a)
	os.makedirs(os.path.join(FA, 'sprites'), exist_ok=True)
	sprite(os.path.join(FA, 'sprites', 'humo_vol.spr'))
	print('sprites/humo_vol.spr')


if __name__ == '__main__':
	main()
