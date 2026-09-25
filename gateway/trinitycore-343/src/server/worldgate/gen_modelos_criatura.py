# -*- coding: utf-8 -*-
# Modelos de criatura custom para el cliente 3.4.3 (filas de CreatureDisplayInfo por hotfix + anuncio en hotfix_data).
# Solo los que usan modelo y texturas de Blizzard (el cliente 3.4.3 tiene el fichero): el modelo se reutiliza buscando el
# CreatureModelData del cliente con ese FileDataID; las texturas del 3.3.5 son nombres junto al modelo -> FileDataID por el listfile.
# Los humanoides con CreatureDisplayInfoExtra (textura cocida propia), los modelos propios y las texturas propias se quedan fuera.
# Uso: python gen_modelos_criatura.py   (lee criatura_modelos_faltan.txt y criatura_modelos_cliente.txt de "worldgate --custom")
import os, struct, subprocess
import os, wg_rutas
DBC = wg_rutas.DBC
EXE = os.environ.get("WG_EXE_DIR", ".")   # carpeta con worldgate.exe
LISTFILE = os.path.expandvars(r"%LOCALAPPDATA%\wow.export\User Data\Default\casc\listfile\listfile.txt")
MYSQL = wg_rutas.MYSQL
SALIDA = EXE + r"\hotfix_modelos_criatura.sql"
HASH = 0xBFDAF9F1                 # CreatureDisplayInfo
PRIMER_PUSH = 900000000 + 2000000 # bloque de hotfix_data.Id para CreatureDisplayInfo

def dbc(p):
    d = open(p, 'rb').read(); n, c, t, s = struct.unpack_from('<4I', d, 4); st = 20 + n * t
    def cad(o):
        e = d.index(b'\0', st + o); return d[st + o:e].decode('latin-1')
    return [(struct.unpack_from('<%dI' % c, d, 20 + i * t), struct.unpack_from('<%df' % c, d, 20 + i * t), cad) for i in range(n)]

cdi = {}
for v, f, cad in dbc(DBC + r"\CreatureDisplayInfo.dbc"):
    cdi[v[0]] = dict(modelo=v[1], sonido=v[2], extra=v[3], escala=f[4], alfa=v[5], tex=[cad(v[6]), cad(v[7]), cad(v[8])], npcsonido=v[12])
cmd = {}
for v, f, cad in dbc(DBC + r"\CreatureModelData.dbc"): cmd[v[0]] = cad(v[2])
lf = {}
for l in open(LISTFILE, encoding='utf8', errors='replace'):
    i = l.find(';')
    if i > 0: lf[l[i + 1:].strip().lower()] = int(l[:i])
cli = {}
for l in open(EXE + r"\criatura_modelos_cliente.txt"):
    a, b = l.strip().split(';'); cli[int(b)] = int(a)
faltan = [int(x) for x in open(EXE + r"\criatura_modelos_faltan.txt") if x.strip()]
# si ya se generaron antes, criatura_modelos_faltan.txt no los trae: se guarda la lista original la primera vez
base = EXE + r"\criatura_modelos_faltan_base.txt"
if os.path.exists(base): faltan = [int(x) for x in open(base) if x.strip()]
else: open(base, "w").write("\n".join(map(str, faltan)) + "\n")

sql = ["SET SESSION sql_mode='';",
       "DELETE FROM creature_display_info WHERE VerifiedBuild = 0;",
       "DELETE FROM hotfix_data WHERE Id >= %d AND TableHash = %d;" % (PRIMER_PUSH, HASH)]
ok = 0; fuera = {}
push = PRIMER_PUSH
for d in faltan:
    c = cdi.get(d)
    if not c: fuera['sin fila'] = fuera.get('sin fila', 0) + 1; continue
    if c['extra']: fuera['humanoide con textura cocida'] = fuera.get('humanoide con textura cocida', 0) + 1; continue
    ruta = cmd.get(c['modelo'], '').replace('\\', '/').lower()
    for e in ('.mdx', '.mdl'):
        if ruta.endswith(e): ruta = ruta[:-4] + '.m2'
    if ruta not in lf: fuera['modelo propio'] = fuera.get('modelo propio', 0) + 1; continue
    if lf[ruta] not in cli: fuera['modelo sin CreatureModelData en el cliente'] = fuera.get('modelo sin CreatureModelData en el cliente', 0) + 1; continue
    carpeta = ruta.rsplit('/', 1)[0]
    texs = []
    for t in c['tex']:
        if not t: texs.append(0); continue
        p = carpeta + '/' + t.lower() + '.blp'
        texs.append(lf.get(p, -1))
    if -1 in texs: fuera['textura propia'] = fuera.get('textura propia', 0) + 1; continue
    sql.append("INSERT INTO creature_display_info (ID, ModelID, SoundID, SizeClass, CreatureModelScale, CreatureModelAlpha, BloodID, "
               "ExtendedDisplayInfoID, NPCSoundID, ParticleColorID, PortraitCreatureDisplayInfoID, PortraitTextureFileDataID, ObjectEffectPackageID, "
               "AnimReplacementSetID, Flags, StateSpellVisualKitID, PlayerOverrideScale, PetInstanceScale, UnarmedWeaponType, MountPoofSpellVisualKitID, "
               "DissolveEffectID, Gender, DissolveOutEffectID, CreatureModelMinLod, TextureVariationFileDataID1, TextureVariationFileDataID2, "
               "TextureVariationFileDataID3, TextureVariationFileDataID4, VerifiedBuild) VALUES "
               "(%d, %d, %d, 1, %f, %d, 0, 0, %d, 0, 0, 0, 0, 0, 0, 0, 0, 1, -1, 0, 0, 2, 0, 0, %d, %d, %d, 0, 0);"
               % (d, cli[lf[ruta]], c['sonido'], c['escala'] if c['escala'] > 0 else 1.0, min(c['alfa'], 255) or 255, c['npcsonido'], texs[0], texs[1], texs[2]))
    push += 1
    sql.append("INSERT INTO hotfix_data (Id, UniqueId, TableHash, RecordId, Status, VerifiedBuild) VALUES (%d, %d, %d, %d, 1, 0);" % (push, push, HASH, d))
    ok += 1
open(SALIDA, "w", encoding="utf8").write("\n".join(sql) + "\n")
print("modelos de criatura: %d resueltos con ficheros de Blizzard; fuera: %s" % (ok, fuera))
with open(SALIDA, "rb") as f:
    res = subprocess.run(wg_rutas.mysql_args(wg_rutas.HOTFIX_DB), stdin=f, capture_output=True)
print("aplicado" if res.returncode == 0 else ("ERROR: " + res.stderr.decode("utf8", "replace")[:500]))
