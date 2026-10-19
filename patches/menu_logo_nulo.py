"""menu.dll (mainui): no cerrarse al elegir un logo que no se puede leer.

CMenuPlayerSetup::UpdateLogo hace CBMP::LoadFile( "logos/<nombre>.bmp" ) y lee la cabecera
sin mirar si ha devuelto NULL; si el logo de la lista no se puede abrir, Sys_Crash
(c0000005 en UpdateLogo+0x1fe). Se desvia ese punto a un hueco al final de .text: si es NULL,
sigue por el camino de "logo no coloreable" (el mismo que para un PNG) en vez de leerlo.

  python menu_logo_nulo.py ruta/menu.dll   (deja menu.dll.antes-logo.bak)
"""
import os, shutil, struct, sys

SITIO = 0x6e2437fc       # mov %eax,%edi ; mov 0x4(%eax),%eax  (tras call CBMP::LoadFile)
ORIGINAL = bytes.fromhex('89c78b4004')
SIGUE = 0x6e243801       # cmpw $0x8,0x1c(%eax)
NO_COLOREABLE = 0x6e24375d   # camino de un PNG: espera ecx = this+0x4104, edx = this+0x4120
HUECO = 0x6e29c7d0       # fin de .text, entre VirtualSize y SizeOfRawData (ceros)


def secciones(d):
    pe = struct.unpack_from('<I', d, 0x3c)[0]
    base = struct.unpack_from('<I', d, pe + 52)[0]
    opt = struct.unpack_from('<H', d, pe + 20)[0]
    for i in range(struct.unpack_from('<H', d, pe + 6)[0]):
        o = pe + 24 + opt + 40 * i
        vs, va, rs, ra = struct.unpack_from('<4I', d, o + 8)
        yield o, d[o:o + 8].rstrip(b'\0'), base + va, vs, rs, ra


def off(d, va):
    for o, n, v, vs, rs, ra in secciones(d):
        if v <= va < v + rs:
            return va - v + ra
    raise SystemExit('%#x fuera de las secciones' % va)


def rel(desde, hacia):
    return struct.pack('<i', hacia - desde)


def main():
    ruta = sys.argv[1]
    d = bytearray(open(ruta, 'rb').read())
    s = off(d, SITIO)
    salto = b'\xe9' + rel(SITIO + 5, HUECO)
    if d[s:s + 5] == salto:
        print('ya parcheado')
        return
    if d[s:s + 5] != ORIGINAL:
        raise SystemExit('menu.dll no es la version esperada')

    c = HUECO
    cueva = bytearray()
    cueva += bytes.fromhex('89c7')                    # mov %eax,%edi
    cueva += bytes.fromhex('85c0')                    # test %eax,%eax
    cueva += bytes.fromhex('7408')                    # jz nulo (+8)
    cueva += bytes.fromhex('8b4004')                  # mov 0x4(%eax),%eax
    cueva += b'\xe9' + rel(c + len(cueva) + 5, SIGUE)  # jmp SIGUE
    cueva += bytes.fromhex('8b54241c')                # nulo: mov 0x1c(%esp),%edx
    cueva += b'\xe9' + rel(c + len(cueva) + 5, NO_COLOREABLE)

    # .text: el hueco tiene que estar dentro de los datos del fichero y a ceros; se amplia
    # VirtualSize para que el cargador lo meta en memoria
    for o, n, v, vs, rs, ra in secciones(d):
        if n == b'.text':
            fin = HUECO + len(cueva)
            if not (v + vs <= HUECO and fin <= v + rs):
                raise SystemExit('el hueco no esta al final de .text')
            h = off(d, HUECO)
            if any(d[h:h + len(cueva)]):
                raise SystemExit('el hueco no esta libre')
            struct.pack_into('<I', d, o + 8, fin - v)
            break

    bak = ruta + '.antes-logo.bak'
    if not os.path.exists(bak):
        shutil.copy2(ruta, bak)
    d[h:h + len(cueva)] = cueva
    d[s:s + 5] = salto
    open(ruta, 'wb').write(d)
    print('UpdateLogo: logo ilegible -> sin color, en vez de cerrarse')


if __name__ == '__main__':
    main()
