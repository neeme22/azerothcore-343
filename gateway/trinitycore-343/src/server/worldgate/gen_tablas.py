# -*- coding: utf-8 -*-
# Genera Tablas.h: enums 3.3.5 (AzerothCore) -> 3.4.3 (TrinityCore) emparejados por nombre.
import re, os
import wg_rutas
AC = wg_rutas.AC_SRC
TC = wg_rutas.TC_SRC
def enum(path, name):
    t = open(path, encoding="utf8", errors="ignore").read()
    m = re.search(r"enum\s+(class\s+)?" + name + r"\b[^{]*\{(.*?)\};", t, re.S)
    body = re.sub(r"//.*", "", m.group(2))
    out, val = {}, -1
    for part in body.split(","):
        part = part.strip()
        if not part: continue
        mm = re.match(r"([A-Za-z0-9_]+)\s*(=\s*(.+))?$", part, re.S)
        if not mm: continue
        n = mm.group(1)
        if mm.group(3) is not None:
            v = mm.group(3).strip()
            try: val = int(eval(v, {}, {k: x for k, x in out.items()}))
            except Exception: continue
        else: val += 1
        out[n] = val
    return out
def tabla(nombre, ac, tc, defecto, n=None):
    inv = {}
    for k, v in ac.items(): inv.setdefault(v, k)
    mx = max(inv) if n is None else n - 1
    filas = []
    for i in range(mx + 1):
        k = inv.get(i)
        filas.append(tc.get(k, tc[defecto]) if k else tc[defecto])
    s = "static int32 const %s[%d] = {\n" % (nombre, len(filas))
    for i in range(0, len(filas), 16): s += "    " + ", ".join(str(x) for x in filas[i:i+16]) + ",\n"
    return s + "};\n"
out = "// Generado por gen_tablas.py: códigos 3.3.5 (AC) -> 3.4.3 (TC) emparejados por nombre\n#pragma once\n\n"
ac = enum(os.path.join(AC, r"..\shared\SharedDefines.h"), "SpellCastResult")
tc = enum(os.path.join(TC, r"Miscellaneous\SharedDefines.h"), "SpellCastResult")
out += tabla("kSpellCastResult", ac, tc, "SPELL_FAILED_UNKNOWN")
AI = os.path.join(AC, r"Entities\Item\Item.h")
TI = os.path.join(TC, r"Entities\Item\ItemDefines.h")
for nombre, en, defecto in (("kInventoryResult", "InventoryResult", "EQUIP_ERR_CLIENT_LOCKED_OUT"),
                            ("kBuyResult", "BuyResult", "BUY_ERR_CANT_FIND_ITEM"),
                            ("kSellResult", "SellResult", "SELL_ERR_UNK")):
    a2, t2 = enum(AI, en), enum(TI, en)
    if en == "InventoryResult":                      # nombres renombrados, números conservados (Blizzard añade al final)
        out += "static int32 const kInventoryResult[256] = {\n    " + ", ".join(str(i) for i in range(256)) + "\n};\n"
    else:
        out += tabla(nombre, a2, t2, defecto)
    print(en, "AC", len(a2), "TC", len(t2), "sin pareja:", [k for k in a2 if k not in t2][:12])
open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "Tablas.h"), "w", encoding="utf8").write(out)
print("SpellCastResult AC", len(ac), "TC", len(tc), "sin pareja:", [k for k in ac if k not in tc][:20])
