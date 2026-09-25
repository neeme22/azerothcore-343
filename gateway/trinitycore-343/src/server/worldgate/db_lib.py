# -*- coding: utf-8 -*-
"""Utilidades del agente 3 (comparación DBC 3.3.5 <-> DB2 3.4.3). Solo lectura.

- db2(tabla)        -> {id: {columna: valor}}  lee ref/datos343/dbc/enUS/<tabla>.db2 (WDC3/4/5) con los nombres de columna
                       de TrinityCore 3.4.3 (DB2LoadInfo.h, orden = columnas de tc343_hotfixes) y el formato de DB2Metadata.h.
                       Soporta ID fuera de línea, datos de relación (padre fuera de línea), tabla de copias, secciones
                       cifradas (a ceros: se saltan) y tablas dispersas (offset map).
- dbc(nombre, fmt)  -> lista de tuplas leídas de server/data/dbc/<nombre>.dbc (WDBC 3.3.5)
- sql(consulta, bd) -> filas (listas de str/None) desde la MySQL indicada con WG_DB_HOST/WG_DB_PORT, solo SELECT.
- hotfix_cols(t)    -> columnas de la tabla tc343_hotfixes.<t>
"""
import os, re, struct, subprocess, functools

import wg_rutas
DB2_DIR = wg_rutas.DB2_DIR
DBC_DIR = wg_rutas.DBC
TC = os.path.join(wg_rutas.TC_SRC, "DataStores")
MYSQL = wg_rutas.MYSQL
SALIDA = wg_rutas.SALIDA

# Modo referencia (variable de entorno DB_REF=1): AzerothCore 3.3.5a SIN modificar.
#   DBC  -> docs/analysis/ref_dbc (extraídos de los MPQ de Blizzard de ChromieCraft con db_ref_extrae.py)
#   BD   -> docs/analysis/db_ref_acore_base.sqlite (volcados base de A:\azerothcore-wotlk-master, db_ref_sqlite.py)
#   Salidas con prefijo db_ref_ en vez de db_.
REF = bool(os.environ.get("DB_REF"))
if REF:
    DBC_DIR = SALIDA + r"\ref_dbc"


def ruta(nombre):
    """Ruta de un fichero de salida/entrada de los scripts; en modo referencia, db_x -> db_ref_x."""
    if REF and nombre.startswith("db_") and not nombre.startswith(("db_ref_", "db_spell_dbc_columnas")):
        nombre = "db_ref_" + nombre[3:]
    return os.path.join(SALIDA, nombre)

_TAM = {"FT_BYTE": 1, "FT_SHORT": 2, "FT_INT": 4, "FT_LONG": 8, "FT_FLOAT": 4, "FT_STRING": 0, "FT_STRING_NOT_LOCALIZED": 0}


@functools.lru_cache(None)
def _meta():
    txt = open(TC + r"\DB2Metadata.h", encoding="utf-8", errors="replace").read()
    out = {}
    for m in re.finditer(r"struct (\w+)Meta\s*\{(.*?)\n\};", txt, re.S):
        cuerpo = m.group(2)
        campos = [(t, int(n), s == "true") for t, n, s in re.findall(r"\{\s*(FT_\w+),\s*(\d+),\s*(true|false)\s*\}", cuerpo)]
        ins = re.search(r"Instance\{\s*(\d+),\s*(-?\d+),\s*(\d+),\s*(\d+),\s*(0x[0-9A-Fa-f]+),\s*Fields,\s*(-?\d+)\s*\}", cuerpo)
        if not ins:
            continue
        out[m.group(1)] = dict(fdid=int(ins.group(1)), index=int(ins.group(2)), nfields=int(ins.group(3)),
                               nfile=int(ins.group(4)), layout=int(ins.group(5), 16), parent=int(ins.group(6)), campos=campos)
    return out


@functools.lru_cache(None)
def _loadinfo():
    txt = open(TC + r"\DB2LoadInfo.h", encoding="utf-8", errors="replace").read()
    out = {}
    for m in re.finditer(r"struct (\w+)LoadInfo\s*\{(.*?)\n\};", txt, re.S):
        cuerpo = m.group(2)
        cols = [(s == "true", t, n) for s, t, n in re.findall(r"\{\s*(true|false),\s*(FT_\w+),\s*\"(\w+)\"\s*\}", cuerpo)]
        meta = re.search(r"&(\w+)Meta::Instance", cuerpo)
        out[m.group(1)] = (cols, meta.group(1) if meta else m.group(1))
    return out


def esquema(tabla):
    """Devuelve (meta, grupos) con grupos = [(nombre_base, [columnas], tipo, con_signo, en_fichero)] en orden de fichero."""
    cols, metan = _loadinfo()[tabla]
    meta = _meta()[metan]
    grupos = []
    i = 0
    if meta["index"] == -1:
        grupos.append(("ID", ["ID"], "FT_INT", False, "id"))
        i = 1
    for k, (t, n, s) in enumerate(meta["campos"]):
        nombres = [c[2] for c in cols[i:i + n]]
        i += n
        base = re.sub(r"\d+$", "", nombres[0]) if n > 1 else nombres[0]
        donde = "fichero"
        if k == meta["parent"] and meta["nfields"] > meta["nfile"]:
            donde = "relacion"
        grupos.append((base, nombres, t, s, donde))
    if i != len(cols):
        raise ValueError("%s: %d columnas en LoadInfo y %d en Meta" % (tabla, len(cols), i))
    return meta, grupos


def _cabecera(b):
    magic = b[:4]
    if magic == b"WDC5":
        o = 4 + 4 + 128
    elif magic in (b"WDC4", b"WDC3"):
        o = 4
    else:
        raise ValueError("formato %r" % magic)
    v = struct.unpack_from("<9IHH7I", b, o)
    o += struct.calcsize("<9IHH7I")
    claves = ("n_rec", "n_fields", "rec_size", "st_size", "table_hash", "layout_hash", "min_id", "max_id", "locale",
              "flags", "id_index", "total_fields", "bitpacked_off", "lookup_cols", "fsi_size", "common_size",
              "pallet_size", "n_sections")
    return dict(zip(claves, v)), o


def table_hash(tabla):
    b = open(os.path.join(DB2_DIR, tabla + ".db2"), "rb").read(512)
    return _cabecera(b)[0]["table_hash"]


_BITS = {"FT_BYTE": 8, "FT_SHORT": 16, "FT_INT": 32, "FT_LONG": 64}


def _conv(v, t, signo, ancho):
    """Recorta el valor al tamaño del tipo (el pallet guarda 32 bits con basura arriba en campos de 8/16) y le da signo."""
    if t == "FT_FLOAT":
        return struct.unpack("<f", struct.pack("<I", v & 0xFFFFFFFF))[0]
    if v < 0:
        return v
    tb = _BITS.get(t, 32)
    ancho = min(ancho, tb) if ancho else 0
    if ancho:
        v &= (1 << ancho) - 1
        if signo and v >> (ancho - 1) & 1:
            v -= 1 << ancho
    return v


@functools.lru_cache(None)
def db2(tabla, fichero=None):
    meta, grupos = esquema(tabla)
    b = open(os.path.join(DB2_DIR, (fichero or tabla) + ".db2"), "rb").read()
    h, o = _cabecera(b)
    secs = []
    for _ in range(h["n_sections"]):
        secs.append(struct.unpack_from("<Q8I", b, o))
        o += 40
    fs = [struct.unpack_from("<hH", b, o + 4 * k) for k in range(h["n_fields"])]
    o += 4 * h["n_fields"]
    info = [struct.unpack_from("<HHIIIII", b, o + 24 * k) for k in range(h["total_fields"])]
    o += 24 * h["total_fields"]
    pallet_ofs, acc = [], 0
    for (ob, sb, add, tipo, v1, v2, v3) in info:
        pallet_ofs.append(o + acc)
        if tipo in (3, 4):
            acc += add
    o += h["pallet_size"]
    common = {}
    for i, (ob, sb, add, tipo, v1, v2, v3) in enumerate(info):
        if tipo == 2:
            common[i] = ({struct.unpack_from("<I", b, o + k * 8)[0]: struct.unpack_from("<I", b, o + k * 8 + 4)[0]
                          for k in range(add // 8)}, v1)
            o += add
    en_fichero = [g for g in grupos if g[4] == "fichero"]
    if len(en_fichero) != len(info):
        raise ValueError("%s: %d campos en fichero según Meta y %d en el db2" % (tabla, len(en_fichero), len(info)))
    id_nombre = "ID"
    id_inline_idx = meta["index"] if meta["index"] >= 0 else None
    padre = next((g for g in grupos if g[4] == "relacion"), None)
    disperso = bool(h["flags"] & 1)
    filas = {}
    # bloque virtual de TrinityCore: registros de todas las secciones seguidos de todas las tablas de cadenas
    virt_rec = sum(s_[2] for s_ in secs) * h["rec_size"]
    tramos, acc_st = [], 0
    for s_ in secs:
        tramos.append((virt_rec + acc_st, s_[1] + s_[2] * h["rec_size"], s_[3]))
        acc_st += s_[3]

    def cadena(pos_virt):
        for ini, fis, tam in tramos:
            if ini <= pos_virt < ini + tam:
                q = fis + pos_virt - ini
                return b[q:b.index(b"\0", q)].decode("utf-8", "replace")
        return "?"
    rec_global = 0
    for (tact, fofs, s_rec, s_st, recs_end, idl_size, rel_size, offmap_n, copy_n) in secs:
        rec_base = rec_global
        rec_global += s_rec
        if not s_rec:
            continue
        if disperso:
            p = recs_end
        else:
            p = fofs + s_rec * h["rec_size"] + s_st
        ids = list(struct.unpack_from("<%dI" % (idl_size // 4), b, p)) if idl_size else None
        p += idl_size
        copias = [struct.unpack_from("<II", b, p + k * 8) for k in range(copy_n)]
        p += copy_n * 8
        offmap = [struct.unpack_from("<IH", b, p + k * 6) for k in range(offmap_n)]
        p += offmap_n * 6
        rel = {}
        if rel_size:
            n_rel = struct.unpack_from("<I", b, p)[0]
            for k in range(n_rel):
                fid, ridx = struct.unpack_from("<II", b, p + 12 + k * 8)
                rel[ridx] = fid
            p += rel_size
        offmap_ids = list(struct.unpack_from("<%dI" % offmap_n, b, p)) if (disperso and offmap_n) else None
        if tact:
            # sección cifrada: si está a ceros, fuera
            if disperso:
                if not any(b[fofs:recs_end]):
                    continue
            elif not any(b[fofs:fofs + s_rec * h["rec_size"]]):
                continue
        for r in range(s_rec):
            fila = {}
            if disperso:
                ofs, tam = offmap[r]
                q = ofs
                for (base, nombres, t, s, donde) in en_fichero:
                    for nm in nombres:
                        if t in ("FT_STRING", "FT_STRING_NOT_LOCALIZED"):
                            e = b.index(b"\0", q)
                            fila[nm] = b[q:e].decode("utf-8", "replace")
                            q = e + 1
                        else:
                            n = _TAM[t]
                            v = int.from_bytes(b[q:q + n], "little")
                            fila[nm] = _conv(v, t, s, n * 8)
                            q += n
                rid = offmap_ids[r] if offmap_ids else (ids[r] if ids else None)
            else:
                rec_ofs = fofs + r * h["rec_size"]
                entero = int.from_bytes(b[rec_ofs:rec_ofs + h["rec_size"]] + b"\0" * 8, "little")
                for i, ((ob, sb, add, tipo, v1, v2, v3), (base, nombres, t, s, donde)) in enumerate(zip(info, en_fichero)):
                    n = len(nombres)
                    if tipo == 0:
                        ancho = sb // n
                        crudos = [(entero >> (ob + k * ancho)) & ((1 << ancho) - 1) for k in range(n)]
                    elif tipo in (1, 5):
                        ancho = v2
                        crudos = []
                        for k in range(n):
                            v = (entero >> (ob + k * ancho)) & ((1 << ancho) - 1)
                            if tipo == 5 and v >> (ancho - 1):
                                v -= 1 << ancho
                            crudos.append(v)
                        ancho = 0
                    elif tipo == 2:
                        crudos, ancho = [None] * n, 32
                    else:
                        idx = (entero >> ob) & ((1 << v2) - 1)
                        k_arr = v3 if tipo == 4 else 1
                        crudos = [struct.unpack_from("<I", b, pallet_ofs[i] + (idx * k_arr + k) * 4)[0] for k in range(k_arr)]
                        ancho = 32
                    for k, (nm, v) in enumerate(zip(nombres, crudos)):
                        if v is None:
                            fila[nm] = None
                        elif t in ("FT_STRING", "FT_STRING_NOT_LOCALIZED"):
                            if v:
                                fila[nm] = cadena((rec_base + r) * h["rec_size"] + ob // 8 + k * 4 + v)
                            else:
                                fila[nm] = ""
                        else:
                            anc = ancho if tipo == 0 else (0 if tipo in (1, 5) else 32)
                            fila[nm] = _conv(v, t, s, anc)
                if ids:
                    rid = ids[r]
                elif id_inline_idx is not None:
                    rid = fila[en_fichero[id_inline_idx][1][0]]
                else:
                    rid = None
            for i, ((ob, sb, add, tipo, v1, v2, v3), (base, nombres, t, s, donde)) in enumerate(zip(info, en_fichero)):
                if tipo == 2:
                    d, defecto = common[i]
                    v = d.get(rid, defecto)
                    fila[nombres[0]] = _conv(v, t, s, 32)
            fila[id_nombre] = rid
            if padre:
                fila[padre[1][0]] = rel.get(r, 0)
            filas[rid] = fila
        for nuevo, viejo in copias:
            if viejo in filas:
                f = dict(filas[viejo])
                f[id_nombre] = nuevo
                filas[nuevo] = f
    return filas


@functools.lru_cache(None)
def db2_ids(fichero):
    """IDs de cualquier DB2 (sin esquema): lista de IDs, o el campo índice en línea, más la tabla de copias."""
    b = open(os.path.join(DB2_DIR, fichero + ".db2"), "rb").read()
    h, o = _cabecera(b)
    secs = [struct.unpack_from("<Q8I", b, o + 40 * k) for k in range(h["n_sections"])]
    o += 40 * h["n_sections"] + 4 * h["n_fields"]
    info = [struct.unpack_from("<HHIIIII", b, o + 24 * k) for k in range(h["total_fields"])]
    o += 24 * h["total_fields"]
    pallet_ofs, acc = [], 0
    for x in info:
        pallet_ofs.append(o + acc)
        if x[3] in (3, 4):
            acc += x[2]
    ids = set()
    disperso = bool(h["flags"] & 1)
    for (tact, fofs, s_rec, s_st, recs_end, idl_size, rel_size, offmap_n, copy_n) in secs:
        if not s_rec:
            continue
        datos_vacios = not any(b[fofs:(recs_end if disperso else fofs + s_rec * h["rec_size"])])
        if tact and datos_vacios:
            continue
        p = recs_end if disperso else fofs + s_rec * h["rec_size"] + s_st
        if idl_size:
            ids.update(struct.unpack_from("<%dI" % (idl_size // 4), b, p))
        else:
            ob, sb, add, tipo, v1, v2, v3 = info[h["id_index"]]
            for r in range(s_rec):
                e = int.from_bytes(b[fofs + r * h["rec_size"]:fofs + (r + 1) * h["rec_size"]] + b"\0" * 8, "little")
                if tipo == 0:
                    ids.add((e >> ob) & ((1 << sb) - 1))
                elif tipo in (1, 5):
                    ids.add((e >> ob) & ((1 << v2) - 1))
                elif tipo == 3:
                    ids.add(struct.unpack_from("<I", b, pallet_ofs[h["id_index"]] + ((e >> ob) & ((1 << v2) - 1)) * 4)[0])
        p += idl_size
        for k in range(copy_n):
            ids.add(struct.unpack_from("<I", b, p + k * 8)[0])
    return ids


def listfile():
    """ruta en minúsculas con / -> FileDataID (listfile de wow.export)."""
    ruta = os.path.expandvars(r"%LOCALAPPDATA%\wow.export\User Data\Default\casc\listfile\listfile.txt")
    lf = {}
    for l in open(ruta, encoding="utf8", errors="replace"):
        i = l.find(";")
        if i > 0:
            lf[l[i + 1:].strip().lower()] = int(l[:i])
    return lf


@functools.lru_cache(None)
def enums_efectos_auras():
    """Traducción de números de efecto y aura 3.3.5 (AC) -> 3.4.3 (TC) por nombre."""
    def en(path, pref):
        t = open(path, encoding="utf-8", errors="replace").read()
        return {m.group(1): int(m.group(2)) for m in re.finditer(r"\b(%s\w+)\s*=\s*(\d+)" % pref, t)}
    ac_e = en(RAIZ + r"\source\src\server\shared\SharedDefines.h", "SPELL_EFFECT_")
    tc_e = en(RAIZ + r"\ref\trinitycore-343\src\server\game\Miscellaneous\SharedDefines.h", "SPELL_EFFECT_")
    ac_a = en(RAIZ + r"\source\src\server\game\Spells\Auras\SpellAuraDefines.h", "SPELL_AURA_")
    tc_a = en(RAIZ + r"\ref\trinitycore-343\src\server\game\Spells\Auras\SpellAuraDefines.h", "SPELL_AURA_")
    # renombres conocidos (mismo concepto, otro nombre en TC 3.4.3)
    alias_a = {"SPELL_AURA_MOD_INCREASE_FLIGHT_SPEED": "SPELL_AURA_MOD_INCREASE_VEHICLE_FLIGHT_SPEED",
               "SPELL_AURA_MOD_FLIGHT_SPEED_ALWAYS": "SPELL_AURA_MOD_VEHICLE_SPEED_ALWAYS",
               "SPELL_AURA_MOD_FLIGHT_SPEED_NOT_STACKING": "SPELL_AURA_MOD_FLIGHT_SPEED_NOT_STACK",
               "SPELL_AURA_MOD_IGNORE_TARGET_RESIST_MODIFIERS": "SPELL_AURA_MOD_IGNORE_TARGET_RESIST",
               "SPELL_AURA_MOD_DAMAGE_FROM_CASTER": "SPELL_AURA_MOD_SPELL_DAMAGE_FROM_CASTER"}

    def mapa(ac, tc, alias):
        inv_ac = {v: k for k, v in ac.items()}
        m, dudosos = {}, {}
        for v, n in inv_ac.items():
            n2 = alias.get(n, n)
            if n2 in tc:
                m[v] = tc[n2]
            else:
                m[v] = v
                inv_tc = {vv: kk for kk, vv in tc.items()}
                if inv_tc.get(v, n) != n:
                    dudosos[v] = (n, inv_tc.get(v))
        return m, dudosos
    me, de = mapa(ac_e, tc_e, {})
    ma, da = mapa(ac_a, tc_a, alias_a)
    return me, de, ma, da


def dbc(nombre, ruta=None):
    """Filas crudas de un DBC 3.3.5: devuelve (filas_int, filas_float, cadena(ofs))."""
    d = open(ruta or os.path.join(DBC_DIR, nombre + ".dbc"), "rb").read()
    assert d[:4] == b"WDBC"
    n, c, t, s = struct.unpack_from("<4I", d, 4)
    st = 20 + n * t

    def cad(o):
        e = d.index(b"\0", st + o)
        return d[st + o:e].decode("utf-8", "replace")
    ints = [struct.unpack_from("<%di" % c, d, 20 + i * t) for i in range(n)]
    flts = [struct.unpack_from("<%df" % c, d, 20 + i * t) for i in range(n)]
    return ints, flts, cad


def sql(consulta, bd="acore_world"):
    if REF and bd == "acore_world":
        import sqlite3
        db = sqlite3.connect(os.path.join(SALIDA, "db_ref_acore_base.sqlite"))
        try:
            return [[None if x is None else str(x) for x in f] for f in db.execute(consulta.replace("`", '"'))]
        finally:
            db.close()
    return _sql_mysql(consulta, bd)


def _sql_mysql(consulta, bd):
    r = subprocess.run(wg_rutas.mysql_args("-B", "-N", "--default-character-set=utf8mb4", bd,
                        "-e", consulta], capture_output=True)
    if r.returncode:
        raise RuntimeError(r.stderr.decode("utf-8", "replace"))
    out = []
    for l in r.stdout.decode("utf-8", "replace").replace("\r\n", "\n").split("\n"):
        if not l:
            continue
        out.append([None if x == "NULL" else x.replace("\\t", "\t").replace("\\n", "\n").replace("\\\\", "\\") for x in l.split("\t")])
    return out


def hotfix_cols(t):
    return [r[0] for r in sql("SELECT COLUMN_NAME FROM information_schema.COLUMNS WHERE TABLE_SCHEMA='tc343_hotfixes' AND TABLE_NAME='%s' "
                              "ORDER BY ORDINAL_POSITION" % t, "information_schema")]
