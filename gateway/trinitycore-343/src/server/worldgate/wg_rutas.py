# -*- coding: utf-8 -*-
# Rutas y conexión que usan los generadores gen_*.py. Se cambian con variables de entorno:
#   WG_AC_DATA   carpeta de datos del worldserver de AzerothCore (la que tiene dbc\)      [<repo>\env\dist\bin\data]
#   WG_MYSQL     cliente mysql                                                           [mysql]
#   WG_DB_HOST / WG_DB_PORT / WG_DB_USER / WG_DB_PASS                                     [127.0.0.1 / 3306 / root / vacía]
#   WG_HOTFIX_DB base de hotfixes de la pasarela                                          [tc343_hotfixes]
#   WG_SALIDA    carpeta donde escribir los .sql generados                                [esta carpeta]
import os
AQUI = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(AQUI, "..", "..", "..", "..", ".."))
AC_SRC = os.path.join(REPO, "src", "server", "game")
TC_SRC = os.path.normpath(os.path.join(AQUI, "..", "game"))
AC_DATA = os.environ.get("WG_AC_DATA", os.path.join(REPO, "env", "dist", "bin", "data"))
DBC = os.path.join(AC_DATA, "dbc")
MYSQL = os.environ.get("WG_MYSQL", "mysql")
HOTFIX_DB = os.environ.get("WG_HOTFIX_DB", "tc343_hotfixes")
WORLD_DB = os.environ.get("WG_WORLD_DB", "acore_world")          # WG_WORLD_DB: base de mundo de AzerothCore
SALIDA = os.environ.get("WG_SALIDA", AQUI)

def mysql_args(*extra):
    a = [MYSQL, "-h" + os.environ.get("WG_DB_HOST", "127.0.0.1"), "-P" + os.environ.get("WG_DB_PORT", "3306"),
         "-u" + os.environ.get("WG_DB_USER", "root")]
    if os.environ.get("WG_DB_PASS"):
        a.append("-p" + os.environ["WG_DB_PASS"])
    return a + list(extra)
# DB2 del cliente 3.4.3 extraídos (los mismos que usa worldgate, DataDir de worldgate.conf) y columnas de Spell.dbc 3.3.5
DB2_DIR = os.environ.get("WG_DB2_DIR", os.path.join(REPO, "env", "dist", "bin", "data343", "dbc", "enUS"))
COLUMNAS_SPELL = os.path.join(AQUI, "db_spell_dbc_columnas.txt")
