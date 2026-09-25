# -*- coding: utf-8 -*-
# Genera MonturasVelocidad.h desde Spell.dbc 3.3.5:
#   kVelMontura: {hechizo, % tierra (aura 32), % vuelo (aura 207)} de cada aura 78 (montura)
#   kMonturaHermana: {hechizo, otro hechizo de montura con el mismo nombre}. AC monta las monturas que escalan
#   (Invencible 72286, Jinete sin cabeza 48025, Corcel celestial 75614...) con variantes internas (72281-72284...) que
#   no existen en el cliente 3.4.3; la pasarela enseña el aura con la hermana que sí existe
import struct, sys
import os, wg_rutas
DBC = os.path.join(wg_rutas.DBC, "Spell.dbc")
SALIDA = sys.argv[1] if len(sys.argv) > 1 else "MonturasVelocidad.h"
C_BP, C_AURA, C_NOMBRE = 80, 95, 136   # EffectBasePoints_1, EffectAura_1, Name_Lang_enUS (db_spell_dbc_columnas.txt, base 0)
d = open(DBC, "rb").read()
n, c, t, s = struct.unpack_from("<4I", d, 4)
cadenas = 20 + n * t
def nombre(off):
    fin = d.index(b"\0", cadenas + off)
    return d[cadenas + off:fin].decode("utf-8", "replace")
filas, por_nombre = [], {}
for i in range(n):
    r = struct.unpack_from("<%di" % c, d, 20 + i * t)
    au = r[C_AURA:C_AURA + 3]
    if 78 not in au: continue
    bp = [x + 1 for x in r[C_BP:C_BP + 3]]
    g = max([b for a, b in zip(au, bp) if a == 32] or [-1])
    f = max([b for a, b in zip(au, bp) if a == 207] or [-1])
    filas.append((r[0], g, f))
    por_nombre.setdefault(nombre(r[C_NOMBRE]), []).append(r[0])
pares = sorted((a, b) for ids in por_nombre.values() if len(ids) > 1 for a in ids for b in sorted(ids) if a != b)
with open(SALIDA, "w", encoding="utf-8") as o:
    o.write("// Generado por gen_monturas.py desde Spell.dbc 3.3.5 (no editar a mano)\n#pragma once\n\n")
    o.write("// {hechizo, % tierra, % vuelo} (-1 = sin ese efecto)\n")
    o.write("struct VelMontura { uint32 hechizo; int16 tierra, vuelo; };\nstatic VelMontura const kVelMontura[] = {\n")
    for sp, g, f in sorted(filas):
        o.write("    { %u, %d, %d },\n" % (sp, g, f))
    o.write("};\n\n// {hechizo, hechizo de montura con el mismo nombre} (en orden de preferencia)\n")
    o.write("struct MonturaHermana { uint32 hechizo, hermana; };\nstatic MonturaHermana const kMonturaHermana[] = {\n")
    for a, b in pares:
        o.write("    { %u, %u },\n" % (a, b))
    o.write("};\n")
print(len(filas), "monturas,", len(pares), "hermanas ->", SALIDA)
