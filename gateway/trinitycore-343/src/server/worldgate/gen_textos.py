# -*- coding: utf-8 -*-
# Textos custom para el cliente 3.4.3 por hotfix (tc343_hotfixes.broadcast_text / broadcast_text_locale, VerifiedBuild = 0):
#  1) broadcast_text de acore_world que no están en tc343_hotfixes (los propios: 990000+, etc.)
#  2) npc_text sin BroadcastTextID: cada texto recibe un BroadcastText nuevo; la correspondencia queda en worldgate.npc_text_bt
# Uso: python gen_textos.py   (escribe hotfix_textos.sql junto al exe y lo aplica en la MySQL del proyecto)
import subprocess, os
import os, wg_rutas
MYSQL = wg_rutas.MYSQL
ARGS = wg_rutas.mysql_args("-N", "--default-character-set=utf8mb4", "-B", "-e")
SALIDA = os.path.join(wg_rutas.SALIDA, "hotfix_textos.sql")

def q(sql):
    out = subprocess.run(ARGS + [sql], capture_output=True).stdout.decode("utf8", "replace")
    filas = []
    for l in out.split("\n"):
        if not l: continue
        filas.append([None if c == "NULL" else c.replace("\\n", "\n").replace("\\t", "\t").replace("\\\\", "\\") for c in l.split("\t")])
    return filas

def esc(s):
    if s is None: return "''"
    return "'" + s.replace("\\", "\\\\").replace("'", "\\'").replace("\n", "\\n") + "'"

def n(s):
    try: return str(int(float(s)))
    except (TypeError, ValueError): return "0"

existentes = set(int(r[0]) for r in q("SELECT ID FROM tc343_hotfixes.broadcast_text WHERE VerifiedBuild <> 0"))
# IDs nuevos consecutivos tras el mayor existente (Blizzard o propio): TrinityCore dimensiona el índice hasta el ID mayor
sig = max(max(existentes), max(int(r[0]) for r in q("SELECT ID FROM acore_world.broadcast_text"))) + 1
sql = ["SET SESSION sql_mode='';",
       "DELETE FROM broadcast_text WHERE VerifiedBuild = 0;",
       "DELETE FROM broadcast_text_locale WHERE VerifiedBuild = 0;"]
cols = "ID, LanguageID, ConditionID, EmotesID, Flags, ChatBubbleDurationMs, VoiceOverPriorityID, SoundKitID1, SoundKitID2, EmoteID1, EmoteID2, EmoteID3, EmoteDelay1, EmoteDelay2, EmoteDelay3, Text, Text1, VerifiedBuild"
propios = 0
for r in q("SELECT ID, LanguageID, MaleText, FemaleText, EmoteID1, EmoteID2, EmoteID3, EmoteDelay1, EmoteDelay2, EmoteDelay3, SoundEntriesId, EmotesID, Flags FROM acore_world.broadcast_text"):
    i = int(r[0])
    if i in existentes: continue
    sql.append("INSERT INTO broadcast_text (%s) VALUES (%d,%s,0,%s,%s,0,0,%s,0,%s,%s,%s,%s,%s,%s,%s,%s,0);" % (
        cols, i, n(r[1]), n(r[11]), n(r[12]), n(r[10]), n(r[4]), n(r[5]), n(r[6]), n(r[7]), n(r[8]), n(r[9]), esc(r[2]), esc(r[3])))
    propios += 1
ids_propios = [str(int(r[0])) for r in q("SELECT ID FROM acore_world.broadcast_text") if int(r[0]) not in existentes]
if ids_propios:
    for r in q("SELECT ID, locale, MaleText, FemaleText FROM acore_world.broadcast_text_locale WHERE ID IN (%s)" % ",".join(ids_propios)):
        sql.append("INSERT INTO broadcast_text_locale (ID, locale, Text_lang, Text1_lang, VerifiedBuild) VALUES (%s,%s,%s,%s,0);" % (r[0], esc(r[1]), esc(r[2]), esc(r[3])))

# npc_text sin BroadcastText
cam = ", ".join("text%d_0, text%d_1, BroadcastTextID%d, lang%d, em%d_0, em%d_1, em%d_2, em%d_3, em%d_4, em%d_5" % ((k,) * 10) for k in range(8))
npc = 0
tabla = {}
for r in q("SELECT ID, %s FROM acore_world.npc_text" % cam):
    idt = int(r[0])
    for k in range(8):
        b = 1 + k * 10
        t0, t1, bt, lang = r[b], r[b + 1], r[b + 2], r[b + 3]
        if (bt not in (None, "0")) or not ((t0 or "") or (t1 or "")): continue
        nid = sig; sig += 1
        tabla[(idt, k)] = nid
        sql.append("INSERT INTO broadcast_text (%s) VALUES (%d,%s,0,0,0,0,0,0,0,%s,%s,%s,%s,%s,%s,%s,%s,0);" % (
            cols, nid, n(lang), n(r[b + 5]), n(r[b + 7]), n(r[b + 9]), n(r[b + 4]), n(r[b + 6]), n(r[b + 8]), esc(t0 or t1), esc(t1 or t0)))
        npc += 1
loccam = ", ".join("Text%d_0, Text%d_1" % (k, k) for k in range(8))
for r in q("SELECT ID, locale, %s FROM acore_world.npc_text_locale" % loccam):
    idt = int(r[0])
    for k in range(8):
        if (idt, k) not in tabla: continue
        t0, t1 = r[2 + k * 2], r[3 + k * 2]
        if not (t0 or t1): continue
        sql.append("INSERT INTO broadcast_text_locale (ID, locale, Text_lang, Text1_lang, VerifiedBuild) VALUES (%d,%s,%s,%s,0);" % (tabla[(idt, k)], esc(r[1]), esc(t0 or t1), esc(t1 or t0)))

sql.append("CREATE DATABASE IF NOT EXISTS worldgate;")
sql.append("CREATE TABLE IF NOT EXISTS worldgate.npc_text_bt (ID INT UNSIGNED NOT NULL, idx TINYINT UNSIGNED NOT NULL, BroadcastTextID INT UNSIGNED NOT NULL, PRIMARY KEY (ID, idx));")
sql.append("DELETE FROM worldgate.npc_text_bt;")
for (idt, k), nid in tabla.items(): sql.append("INSERT INTO worldgate.npc_text_bt VALUES (%d,%d,%d);" % (idt, k, nid))
open(SALIDA, "w", encoding="utf8").write("\n".join(sql) + "\n")
print("broadcast_text propios: %d, textos de npc_text: %d -> %s" % (propios, npc, SALIDA))
with open(SALIDA, "rb") as f:
    r = subprocess.run(wg_rutas.mysql_args("--default-character-set=utf8mb4", wg_rutas.HOTFIX_DB), stdin=f, capture_output=True)
print("aplicado" if r.returncode == 0 else ("ERROR: " + r.stderr.decode("utf8", "replace")[:500]))
