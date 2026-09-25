# -*- coding: utf-8 -*-
# Genera AurasPuntos.h: valores de efecto de AzerothCore (Spell.dbc 3.3.5) para los hechizos que en el cliente 3.4.3
# llevan SPELL_ATTR8_AURA_POINTS_ON_CLIENT (SpellMisc.Attributes[8] & 0x1000) y no son monturas (esas van por
# MonturasVelocidad.h). El cliente calcula su texto de aura con AuraDataInfo.Points; la pasarela los rellena con esto.
import struct, sys
import db_lib as L
import os, wg_rutas
DBC = os.path.join(wg_rutas.DBC, "Spell.dbc")
SALIDA = sys.argv[1] if len(sys.argv) > 1 else "AurasPuntos.h"
COLS = open(wg_rutas.COLUMNAS_SPELL).read().split()
C = {k: i for i, k in enumerate(COLS)}

attr8 = {e["SpellID"] for e in L.db2("SpellMisc").values() if e["DifficultyID"] == 0 and e["Attributes9"] & 0x1000}
montura = {e["SpellID"] for e in L.db2("SpellEffect").values() if e["DifficultyID"] == 0 and e["EffectAura"] == 78}
EXCLUIR = {48090}   # Demonic Pact: AC lo lanza con CastCustomSpell (10% del poder con hechizos), el valor no está en el DBC
objetivo = attr8 - montura - EXCLUIR

d = open(DBC, "rb").read()
n, c, t, s = struct.unpack_from("<4I", d, 4)
filas = []
for i in range(n):
    r = struct.unpack_from("<%di" % c, d, 20 + i * t)
    if r[0] not in objetivo:
        continue
    f = lambda k: r[C[k]]
    fl = lambda k: struct.unpack("<f", struct.pack("<i", f(k)))[0]
    bp = [f("EffectBasePoints_%d" % k) for k in (1, 2, 3)]
    dado = [f("EffectDieSides_%d" % k) for k in (1, 2, 3)]
    nivel = [fl("EffectRealPointsPerLevel_%d" % k) for k in (1, 2, 3)]
    filas.append((r[0], bp, dado, nivel, f("BaseLevel"), f("MaxLevel"), f("SpellLevel"), f("CumulativeAura")))
def flt(x):
    t = ("%.6f" % x).rstrip("0")
    return (t + "0" if t.endswith(".") else t) + "f"
with open(SALIDA, "w", encoding="utf-8") as o:
    o.write("// Generado por gen_auras_puntos.py desde Spell.dbc 3.3.5: hechizos con SPELL_ATTR8_AURA_POINTS_ON_CLIENT en el\n"
            "// cliente 3.4.3 (sin monturas). {hechizo, BasePoints[3], DieSides[3], RealPointsPerLevel[3], BaseLevel, MaxLevel, SpellLevel, CumulativeAura}\n"
            "#pragma once\n\n"
            "struct PuntosAura { uint32 hechizo; int32 bp[3]; int32 dado[3]; float porNivel[3]; uint16 nivelBase, nivelMax, nivelHechizo, pila; };\n"
            "static PuntosAura const kPuntosAura[] = {\n")
    for sp, bp, dado, nivel, bl, ml, sl, pila in sorted(filas):
        o.write("    { %u, { %d, %d, %d }, { %d, %d, %d }, { %s, %s, %s }, %u, %u, %u, %u },\n" % ((sp,) + tuple(bp) + tuple(dado) + tuple(flt(x) for x in nivel) + (bl, ml, sl, pila)))
    o.write("};\n")
print(len(objetivo), "hechizos con ATTR8 sin montura;", len(filas), "en Spell.dbc 3.3.5 ->", SALIDA)
