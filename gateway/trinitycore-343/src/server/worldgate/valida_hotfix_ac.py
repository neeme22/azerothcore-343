# -*- coding: utf-8 -*-
"""Valida SIN aplicar un .sql de hotfix: cada INSERT/REPLACE entero se pasa como EXPLAIN (MySQL 8 no modifica nada)
contra la BD de hotfixes (WG_HOTFIX_DB); comprueba además que no se repite ninguna clave primaria dentro del fichero.
Uso: python valida_hotfix_ac.py fichero.sql"""
import os, re, subprocess, sys, tempfile, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import db_lib as L
txt = open(sys.argv[1], encoding="utf-8").read()
sts = re.findall(r"^((?:INSERT|REPLACE) INTO .*?;)\s*$", txt, re.S | re.M)
ok = mal = 0
claves = collections.Counter()
for st in sts:
    with tempfile.NamedTemporaryFile("w", suffix=".sql", delete=False, encoding="utf-8") as f:
        f.write("SET NAMES utf8mb4;\nEXPLAIN " + st + "\n")
    import wg_rutas
    with open(f.name, "rb") as entrada:
        r = subprocess.run(wg_rutas.mysql_args(wg_rutas.HOTFIX_DB), stdin=entrada, capture_output=True)
    os.unlink(f.name)
    if r.returncode:
        mal += 1
        print("ERROR", st[:80], r.stderr.decode("utf-8", "replace")[:300])
    else:
        ok += 1
    tabla = st.split()[2]
    for fila in re.findall(r"^\((-?\d+),(-?\d+)?", st.split(" VALUES\n", 1)[1], re.M):
        pass
    cuerpo = st.split(" VALUES\n", 1)[1].rstrip(";")
    for linea in cuerpo.split("\n"):
        v = linea.strip("(),").split(",")
        clave = (tabla, v[0], v[2], v[3]) if tabla == "hotfix_data" else (tabla, v[0], v[-1])
        claves[clave] += 1
rep = [k for k, n in claves.items() if n > 1]
print("sentencias", len(sts), "válidas", ok, "con error", mal, "| claves repetidas", len(rep), rep[:5])
