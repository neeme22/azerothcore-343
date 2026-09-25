# -*- coding: utf-8 -*-
"""Genera (NO aplica) hotfix_extendedcost_ac.sql: ItemExtendedCost del cliente 3.4.3 con los precios de AzerothCore
original (ItemExtendedCost.dbc 3.3.5 de data\\dbc del servidor).

Mapeo 3.3.5 (AC DBCStructure.h:1223, fmt "niiiiiiiiiiiiiix") -> 3.4.3 (TC DB2Structure.h:2155, item_extended_cost):
  0  ID                        -> ID
  1  reqhonorpoints            -> CurrencyID/CurrencyCount (1901 Honor Points)
  2  reqarenapoints            -> CurrencyID/CurrencyCount (1900 Arena Points)
  3  reqarenaslot              -> ArenaBracket
  4-8  reqitem[5]              -> ficha de moneda (mon_monedas.md §3) -> CurrencyID/Count; resto -> ItemID1..5
  9-13 reqitemcount[5]         -> ItemCount1..5 / CurrencyCount
  14 reqpersonalarenarating    -> RequiredArenaRating
  15 ItemPurchaseGroup         -> (sin columna; AC no lo lee: 'x' en el fmt)
  -- Flags, MinFactionID, MinReputation, RequiredAchievement: no existen en 3.3.5 -> 0 (AC no los exige)
Orden de monedas: honor, arena y fichas en el orden de reqitem (como el DB2 de Classic, p. ej. 2444).

Salida en la BD de hotfixes (WG_HOTFIX_DB): item_extended_cost VerifiedBuild = -343 (gana a las 12 filas VB 52237 de Blizzard);
hotfix_data Id = 906000000 + ID, UniqueId = CRC32 de la fila (TableHash 0xBB858355).
Por defecto: todos los ID del DBC cuya fila 3.4.3 esperada no coincide con el DB2 del cliente (incluidos los que el
cliente no tiene). --solo-vendedores: solo los que usan npc_vendor/game_event_npc_vendor de AzerothCore.
Escribe hotfix_extendedcost_ac_informe.json. Solo lectura (SELECT).
"""
import collections, json, os, struct, sys, zlib
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import db_lib as L

import wg_rutas
DBC = os.path.join(wg_rutas.DBC, "ItemExtendedCost.dbc")
BD_AC, BD_HF = wg_rutas.WORLD_DB, wg_rutas.HOTFIX_DB
VB, PUSH0, HASH = -343, 906000000, 0xBB858355
HONOR, ARENA = 1901, 1900
FICHA = {29434: 42, 41596: 61, 43016: 81, 40752: 101, 40753: 102, 20560: 121, 20559: 122, 29024: 123, 42425: 124,
         20558: 125, 43589: 126, 43228: 161, 37836: 201, 45624: 221, 44990: 241, 47241: 301, 47395: 321, 49426: 341}
SALIDA = os.path.join(L.SALIDA, "hotfix_extendedcost_ac.sql")
COLS = ["ID", "RequiredArenaRating", "ArenaBracket", "Flags", "MinFactionID", "MinReputation", "RequiredAchievement"] + \
       ["ItemID%d" % k for k in range(1, 6)] + ["ItemCount%d" % k for k in range(1, 6)] + \
       ["CurrencyID%d" % k for k in range(1, 6)] + ["CurrencyCount%d" % k for k in range(1, 6)]


def dbc():
    b = open(DBC, "rb").read()
    _, n, f, rs, _ = struct.unpack_from("<4s4I", b, 0)
    return {struct.unpack_from("<I", b, 20 + i * rs)[0]: struct.unpack_from("<%dI" % f, b, 20 + i * rs) for i in range(n)}


def fila_343(v):
    """Fila 3.4.3 a partir de la 3.3.5 v (16 campos)."""
    r = dict.fromkeys(COLS, 0)
    r["ID"] = v[0]
    r["ArenaBracket"] = v[3]
    r["RequiredArenaRating"] = v[14]
    mon = []
    if v[1]:
        mon.append((HONOR, v[1]))
    if v[2]:
        mon.append((ARENA, v[2]))
    obj = []
    for k in range(5):
        it, n = v[4 + k], v[9 + k]
        if not it:
            continue
        if it in FICHA:
            mon.append((FICHA[it], n))
        else:
            obj.append((it, n))
    assert len(mon) <= 5 and len(obj) <= 5, v
    for k, (it, n) in enumerate(obj):
        r["ItemID%d" % (k + 1)], r["ItemCount%d" % (k + 1)] = it, n
    for k, (c, n) in enumerate(mon):
        r["CurrencyID%d" % (k + 1)], r["CurrencyCount%d" % (k + 1)] = c, n
    return r


def canon(r):
    """Comparación sin orden de huecos."""
    return (r["RequiredArenaRating"], r["ArenaBracket"], r["Flags"], r["MinFactionID"], r["MinReputation"],
            r["RequiredAchievement"],
            tuple(sorted((r["ItemID%d" % k], r["ItemCount%d" % k]) for k in range(1, 6) if r["ItemID%d" % k])),
            tuple(sorted((r["CurrencyID%d" % k], r["CurrencyCount%d" % k]) for k in range(1, 6) if r["CurrencyID%d" % k])))


def main():
    solo_vend = "--solo-vendedores" in sys.argv
    d = dbc()
    cli = L.db2("ItemExtendedCost")
    usados = collections.Counter()
    for r in L.sql("SELECT ExtendedCost, COUNT(*) FROM npc_vendor WHERE ExtendedCost <> 0 GROUP BY ExtendedCost UNION ALL "
                   "SELECT ExtendedCost, COUNT(*) FROM game_event_npc_vendor WHERE ExtendedCost <> 0 GROUP BY ExtendedCost", BD_AC):
        usados[int(r[0])] += int(r[1])
    blizz = {int(r[0]) for r in L.sql("SELECT ID FROM item_extended_cost WHERE VerifiedBuild > 0", BD_HF)}
    filas, informe, ejemplos = [], collections.OrderedDict(), []
    cats = collections.Counter()
    for i in sorted(d):
        if solo_vend and i not in usados:
            continue
        nueva = fila_343(d[i])
        c = cli.get(i)
        if c is not None and canon(nueva) == canon(c):
            continue
        if c is None:
            cats["falta en el cliente"] += 1
        else:
            dif = []
            if canon(nueva)[7] != canon(c)[7]:
                dif.append("monedas")
            if canon(nueva)[6] != canon(c)[6]:
                dif.append("objetos")
            if canon(nueva)[:6] != canon(c)[:6]:
                dif.append("requisitos")
            cats["+".join(dif)] += 1
        filas.append(nueva)
        if len(ejemplos) < 60:
            ejemplos.append({"ID": i, "usos_vendedor": usados.get(i, 0),
                             "ac_335": {"honor": d[i][1], "arena": d[i][2], "bracket": d[i][3], "rating": d[i][14],
                                        "objetos": [(d[i][4 + k], d[i][9 + k]) for k in range(5) if d[i][4 + k]]},
                             "cliente": None if c is None else {k: c[k] for k in COLS if c[k]},
                             "hotfix": {k: nueva[k] for k in COLS if nueva[k]}})
    out = ["-- Generado por docs/analysis/gen_hotfix_extendedcost_ac.py. NO APLICADO.",
           "-- ItemExtendedCost con los precios de AzerothCore (ItemExtendedCost.dbc 3.3.5). BD: la de hotfixes de la pasarela.",
           "-- %d filas (VerifiedBuild = %d), hotfix_data Id = %d + ID. Deshacer: las dos DELETE de abajo." % (len(filas), VB, PUSH0),
           "SET SESSION sql_mode='';", "START TRANSACTION;",
           "DELETE FROM hotfix_data WHERE Id >= %d AND Id < %d;" % (PUSH0, PUSH0 + 1000000),
           "DELETE FROM item_extended_cost WHERE VerifiedBuild = %d;" % VB]
    if filas:
        out.append("INSERT INTO item_extended_cost (%s,VerifiedBuild) VALUES\n%s;" % (
            ",".join(COLS), ",\n".join("(" + ",".join(str(f[k]) for k in COLS) + ",%d)" % VB for f in filas)))
        hd = []
        for f in filas:
            uid = zlib.crc32(repr([f[k] for k in COLS]).encode()) & 0xFFFFFFFF
            hd.append("(%d,%d,%d,%d,1,0)" % (PUSH0 + f["ID"], uid, HASH, f["ID"]))
        out.append("INSERT INTO hotfix_data (Id,UniqueId,TableHash,RecordId,Status,VerifiedBuild) VALUES\n%s;" % ",\n".join(hd))
    out.append("COMMIT;")
    salida = SALIDA if "--salida" not in sys.argv else sys.argv[sys.argv.index("--salida") + 1]
    open(salida, "w", encoding="utf-8", newline="\n").write("\n".join(out) + "\n")
    ids = [f["ID"] for f in filas]
    informe["filas_dbc_335"] = len(d)
    informe["filas_db2_cliente"] = len(cli)
    informe["extendedcost_usados_por_vendedores"] = len(usados)
    informe["filas_hotfix"] = len(filas)
    informe["de_ellas_usadas_por_vendedores"] = sum(1 for i in ids if i in usados)
    informe["ids_usados_con_hotfix"] = [i for i in ids if i in usados]
    informe["ids_sin_uso_con_hotfix"] = [i for i in ids if i not in usados]
    informe["por_tipo"] = dict(cats)
    informe["pisan_filas_blizzard_vb52237"] = sorted(set(ids) & blizz)
    informe["usados_sin_fila_en_dbc"] = sorted(i for i in usados if i not in d)
    informe["tamano_contenido_bytes"] = len(filas) * 70    # 2+1+1+1+4+1+20+10+10+20 = 70 bytes por registro (WriteRecord)
    informe["ejemplos"] = ejemplos
    json.dump(informe, open(os.path.join(L.SALIDA, "hotfix_extendedcost_ac_informe.json"), "w", encoding="utf-8"),
              ensure_ascii=False, indent=1)
    print(json.dumps({k: v for k, v in informe.items() if k != "ejemplos"}, ensure_ascii=False, indent=1))


if __name__ == "__main__":
    main()
