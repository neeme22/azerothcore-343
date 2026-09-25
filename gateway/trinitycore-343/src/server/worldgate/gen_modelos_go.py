# -*- coding: utf-8 -*-
# Modelos de GameObject custom para el cliente 3.4.3: filas de GameObjectDisplayInfo por hotfix (tc343_hotfixes, VerifiedBuild = 0)
# y su anuncio en hotfix_data (Id >= 900000000, lo que la pasarela manda en SMSG_AVAILABLE_HOTFIXES).
# El 3.3.5 guarda la ruta del modelo (GameObjectDisplayInfo.dbc); el 3.4.3 quiere FileDataID: se busca en el listfile de wow.export.
# Solo sirve cuando el modelo es un fichero de Blizzard que también trae el cliente 3.4.3; los modelos propios no existen allí.
# Uso: python gen_modelos_go.py   (lee go_modelos_faltan_base.txt que deja "worldgate --custom" antes de haber filas propias)
import os, struct, subprocess
import os, wg_rutas
DBC = os.path.join(wg_rutas.DBC, "GameObjectDisplayInfo.dbc")
EXE = os.environ.get("WG_EXE_DIR", ".")   # carpeta con worldgate.exe
FALTAN = EXE + r"\go_modelos_faltan_base.txt"
LISTFILE = os.path.expandvars(r"%LOCALAPPDATA%\wow.export\User Data\Default\casc\listfile\listfile.txt")
MYSQL = wg_rutas.MYSQL
SALIDA = EXE + r"\hotfix_modelos_go.sql"
HASH = 0x6D100DCB                 # GameObjectDisplayInfo
PRIMER_PUSH = 900000000           # hotfix_data.Id propios (Cfg::PrimerHotfixPropio)

def leer_dbc(ruta):
    d = open(ruta, "rb").read()
    assert d[:4] == b"WDBC"
    n, campos, tam, tstr = struct.unpack_from("<4I", d, 4)
    base = 20; cadenas = base + n * tam
    filas = {}
    for i in range(n):
        o = base + i * tam
        v = struct.unpack_from("<%dI" % campos, d, o)
        fl = struct.unpack_from("<%df" % campos, d, o)
        off = v[1]
        fin = d.index(b"\0", cadenas + off)
        ruta_m = d[cadenas + off:fin].decode("latin-1")
        filas[v[0]] = (ruta_m, fl[12:18], v[18] if campos > 18 else 0)
    return filas

faltan = [int(l) for l in open(FALTAN) if l.strip()]
dbc = leer_dbc(DBC)
fdid = {}
with open(LISTFILE, encoding="utf8", errors="replace") as f:
    for l in f:
        i = l.find(";")
        if i > 0: fdid[l[i + 1:].strip().lower()] = int(l[:i])

sql = ["SET SESSION sql_mode='';",
       "DELETE FROM gameobject_display_info WHERE VerifiedBuild = 0;",
       "DELETE FROM hotfix_data WHERE Id >= %d AND TableHash = %d;" % (PRIMER_PUSH, HASH)]
ok = 0; sin_ruta = 0; sin_fichero = []
push = PRIMER_PUSH + 1000000      # bloque de ids para GameObjectDisplayInfo
for gid in faltan:
    if gid not in dbc: sin_ruta += 1; continue
    ruta, caja, efecto = dbc[gid]
    r = ruta.replace("\\", "/").lower()
    for ext in (".mdx", ".mdl"):
        if r.endswith(ext): r = r[:-len(ext)] + ".m2"
    if r not in fdid: sin_fichero.append((gid, ruta)); continue
    sql.append("INSERT INTO gameobject_display_info (ID, ModelName, GeoBoxMinX, GeoBoxMinY, GeoBoxMinZ, GeoBoxMaxX, GeoBoxMaxY, GeoBoxMaxZ, "
               "FileDataID, ObjectEffectPackageID, OverrideLootEffectScale, OverrideNameScale, VerifiedBuild) VALUES "
               "(%d, '', %f, %f, %f, %f, %f, %f, %d, 0, 0, 0, 0);" % ((gid,) + tuple(caja) + (fdid[r],)))
    push += 1
    sql.append("INSERT INTO hotfix_data (Id, UniqueId, TableHash, RecordId, Status, VerifiedBuild) VALUES (%d, %d, %d, %d, 1, 0);" % (push, push, HASH, gid))
    ok += 1
open(SALIDA, "w", encoding="utf8").write("\n".join(sql) + "\n")
print("modelos de GameObject: %d con fichero de Blizzard, %d sin fila en el DBC, %d con modelo propio (no están en el cliente)" % (ok, sin_ruta, len(sin_fichero)))
for g, rt in sin_fichero[:10]: print("   propio:", g, rt)
with open(SALIDA, "rb") as f:
    res = subprocess.run(wg_rutas.mysql_args(wg_rutas.HOTFIX_DB), stdin=f, capture_output=True)
print("aplicado" if res.returncode == 0 else ("ERROR: " + res.stderr.decode("utf8", "replace")[:500]))
