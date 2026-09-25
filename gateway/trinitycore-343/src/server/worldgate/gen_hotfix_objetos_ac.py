# -*- coding: utf-8 -*-
"""Genera (NO aplica) hotfix_objetos_ac.sql: los objetos que el cliente 3.4.3 ya tiene en ItemSparse/Item/ItemEffect,
reescritos con los valores de AzerothCore original (item_template de AzerothCore).

Por qué: el cliente pinta el tooltip con su DB2 (valores de Classic) y AC aplica los suyos. Un objeto que el cliente
ya tiene no se pide por CMSG_DB_QUERY_BULK: hay que EMPUJARLO con hotfix_data (SMSG_AVAILABLE_HOTFIXES ->
CMSG_HOTFIX_REQUEST -> SMSG_HOTFIX_CONNECT, Main.cpp:488-494 y 749-784 de la pasarela).

Método: se parte de la fila del DB2 del cliente (todas sus columnas) y se pisan SOLO las columnas que tienen
equivalente en item_template (funciones sparse_ac / item_ac / efectos_ac). Mapa AC 3.3.5 -> 3.4.3:
  ItemSparse: name->Display, description->Description, Quality->OverallQualityID, Flags->Flags1,
    FlagsExtra->Flags2 (solo bits 0x3FF; 0x4 solo si el objeto se vende con ExtendedCost), BuyCount->VendorStackCount,
    BuyPrice, SellPrice, InventoryType, AllowableClass/AllowableRace (normalizadas: "todas" = igual), ItemLevel,
    RequiredLevel, RequiredSkill(+Rank), requiredspell->RequiredAbility, requiredhonorrank->RequiredPVPRank,
    RequiredReputationFaction->MinFactionID, RequiredReputationRank->MinReputation, maxcount->MaxCount,
    stackable->Stackable, ContainerSlots, stat_type/value1..10->StatModifierBonusStat/Amount1..10 (-1 = vacío),
    ScalingStatDistribution->ScalingStatDistributionID, dmg_min/max1..2->MinDamage/MaxDamage1..2 (redondeo; 3..5 = 0),
    dmg_type1->DamageDamageType, armor->Resistances1, holy..arcane_res->Resistances2..7, delay->ItemDelay,
    ammo_type->AmmunitionType, RangedModRange->ItemRange, bonding->Bonding, PageText->PageID, LanguageID,
    PageMaterial->PageMaterialID, startquest->StartQuestID, lockid->LockID, Material, sheath->SheatheType,
    RandomProperty->RandomSelect, RandomSuffix->ItemRandomSuffixGroupID, itemset->ItemSet, MaxDurability,
    area->ZoneBound1, Map->InstanceBound, BagFamily, TotemCategory->TotemCategoryID,
    socketColor_1..3->SocketType1..3 (máscara 1/2/4/8/14 -> índice 1/2/3/4/7), socketBonus->SocketMatchEnchantmentId,
    GemProperties, duration->DurationInInventory, ItemLimitCategory->LimitCategory, HolidayId->RequiredHoliday.
  Item: class->ClassID, subclass->SubclassID, Material, InventoryType, RequiredLevel, sheath->SheatheType,
    RandomProperty->RandomSelect, RandomSuffix->ItemRandomSuffixGroupID, SoundOverrideSubclass->SoundOverrideSubclassID,
    ScalingStatDistribution(ID), MaxDurability, ammo_type->AmmunitionType, ScalingStatValue, dmg_type1..2->DamageType1..2,
    armor/resistencias->Resistances1..7, daño->MinDamage/MaxDamage1..2 (TC 3.4.3 lee el daño de Item.db2, no de ItemSparse).
  ItemEffect (una fila por hechizo, ParentItemID = entry): spellid_k->SpellID, spelltrigger_k->TriggerType,
    spellcharges_k->Charges, spellcooldown_k->CoolDownMSec, spellcategory_k->SpellCategoryID,
    spellcategorycooldown_k->CategoryCoolDownMSec, k-1->LegacySlotIndex, ChrSpecializationID = 0.
    Se reutilizan los ID de ItemEffect que el cliente ya tiene para ese objeto; si sobran -> RecordRemoved; si faltan,
    ID nuevos tras el máximo (DB2 y BD). spellppmRate no tiene columna (lo aplica AC).
  Sin equivalente (se quedan del cliente): displayid (Classic renumeró ItemDisplayInfo y TC no carga esa tabla),
    icono, block (valor de bloqueo de escudo: no hay columna en 3.4.3), ArmorDamageModifier, RequiredCityRank,
    flagsCustom, FoodType, RequiredDisenchantSkill/DisenchantID, ScriptName. Las columnas propias de Classic (ExpansionID,
ContentTuningID, StatPercentEditor, DmgVariance, ItemNameDescriptionID, icono...) se conservan. Si la fila resultante
es igual a la del cliente, no se escribe nada.

Salida (todo en la BD de hotfixes que sirve la pasarela, WG_HOTFIX_DB):
  item_sparse / item / item_effect   VerifiedBuild = -343 (marca propia; TC la carga en la pasada "custom", que va
                                     DESPUÉS de las filas de Blizzard VerifiedBuild > 0, así que gana; DB2DatabaseLoader)
  hotfix_data                        Id = 905000000 + entry (un envío por objeto, estable entre ejecuciones);
                                     UniqueId = CRC32 del contenido del envío (cambia solo si cambia el objeto:
                                     así el cliente invalida su caché DBCache.bin y no vuelve a bajar lo que no cambió)
  ItemEffect sobrante               hotfix_data Status = 2 (RecordRemoved), sin fila

Uso:
  python gen_hotfix_objetos_ac.py [--sin-nombres] [--sin-efectos] [--locale-esES] [--salida fichero.sql]
    --sin-nombres   conserva nombre y descripción del cliente (Display/Description)
    --sin-efectos   no toca ItemEffect (hechizos de uso/equipar/golpe)
    --solo-tooltip  deja del cliente Material, SheatheType, SoundOverrideSubclass, LanguageID y PageMaterial
    --precio-tolerancia N  no empuja un objeto si lo único distinto es el precio y difiere en N cobres o menos
    --locale-esES   añade item_sparse_locale esES desde item_template_locale (hoy la pasarela manda solo enUS)
Escribe además hotfix_objetos_ac_informe.json y hotfix_objetos_ac_cambios.csv (objeto;tabla;campo;cliente;AC).
Solo lectura sobre MySQL (SELECT).
"""
import collections, csv, json, os, struct, sys, zlib
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import db_lib as L

import wg_rutas
BD_AC = wg_rutas.WORLD_DB
BD_HF = wg_rutas.HOTFIX_DB
VB = -343                               # VerifiedBuild de nuestras filas
PUSH0 = 905000000                       # hotfix_data.Id = PUSH0 + entry (bloque 905000000-905999999)
H_SPARSE, H_ITEM, H_EFFECT = 0x919BE54E, 0x50238EC2, 0x4002A5B1

ARGS = set(sys.argv[1:])
SOLO_TOOLTIP = "--solo-tooltip" in ARGS   # no toca lo que no sale en el tooltip: Material (sonido), SheatheType (dónde se
                                          # cuelga el arma), SoundOverrideSubclass, LanguageID y PageMaterial (libros)
TOL_PRECIO = int(sys.argv[sys.argv.index("--precio-tolerancia") + 1]) if "--precio-tolerancia" in sys.argv else 0
SALIDA = os.path.join(L.SALIDA, "hotfix_objetos_ac.sql")
if "--salida" in sys.argv:
    SALIDA = sys.argv[sys.argv.index("--salida") + 1]

# ------------------------------------------------------------------------------------------------ item_template
S = ["stat_type%d" % k for k in range(1, 11)], ["stat_value%d" % k for k in range(1, 11)]
SP = [["spellid_%d" % k, "spelltrigger_%d" % k, "spellcharges_%d" % k, "spellcooldown_%d" % k, "spellcategory_%d" % k,
       "spellcategorycooldown_%d" % k] for k in range(1, 6)]
COLS_AC = ("entry class subclass SoundOverrideSubclass name displayid Quality Flags FlagsExtra BuyCount BuyPrice SellPrice "
           "InventoryType AllowableClass AllowableRace ItemLevel RequiredLevel RequiredSkill RequiredSkillRank requiredspell "
           "requiredhonorrank RequiredReputationFaction RequiredReputationRank maxcount stackable ContainerSlots "
           "ScalingStatDistribution ScalingStatValue dmg_min1 dmg_max1 dmg_type1 dmg_min2 dmg_max2 dmg_type2 armor holy_res "
           "fire_res nature_res frost_res shadow_res arcane_res delay ammo_type RangedModRange bonding description PageText "
           "LanguageID PageMaterial startquest lockid Material sheath RandomProperty RandomSuffix itemset MaxDurability area "
           "Map BagFamily TotemCategory socketColor_1 socketColor_2 socketColor_3 socketBonus GemProperties duration "
           "ItemLimitCategory HolidayId").split() + S[0] + S[1] + sum(SP, [])
TEXTO = {"name", "description"}
REAL = {"dmg_min1", "dmg_max1", "dmg_min2", "dmg_max2", "RangedModRange"}

RAZAS_335 = 0x6FF        # razas jugables 3.3.5 (1-8, 10, 11)
CLASES_335 = 0x5FF       # clases jugables 3.3.5 (1-9, 11)
SOCKET = {0: 0, 1: 1, 2: 2, 4: 3, 8: 4, 14: 7}   # 3.3.5 SocketColor (máscara) -> 3.4.3 SocketType (índice); 14 = prismático
FLAGS2_AC = 0x3FF        # bits de FlagsExtra que AC define y usa (0x1 Horda, 0x2 Alianza, 0x4 oro+coste, 0x100 solo codicia...)
                         # los bits >= 0x400 (0x2000/0x4000 van en casi todos los objetos de Classic) se conservan


def material(ac, cli):
    """Material -1 de AC (MATERIAL_CONSUMABLES) no cabe en el BYTE sin signo de 3.4.3: el cliente lo guarda como 0."""
    return cli if ac == -1 and cli == 0 else max(ac, 0)


def rnd(x):
    """float de AC -> entero como lo guarda MySQL al meterlo en SMALLINT (redondeo, mitades hacia fuera)."""
    return int(x + 0.5) if x >= 0 else -int(-x + 0.5)


def todas(m, mascara):
    return m in (-1, 0) or (m & mascara) == mascara


def norm_mascara(ac, cli, mascara):
    """Raza/clase: si las dos cubren todas las jugables de 3.3.5, o coinciden en esos bits, se deja la del cliente."""
    if todas(ac, mascara) and todas(cli, mascara):
        return cli
    if (ac & mascara) == (cli & mascara):
        return cli
    return ac & mascara if ac not in (-1, 0) else -1


def carga_ac():
    filas = L.sql("SELECT %s FROM item_template" % ",".join("`%s`" % c for c in COLS_AC), BD_AC)
    ac = {}
    for f in filas:
        d = {}
        for c, x in zip(COLS_AC, f):
            if c in TEXTO:
                d[c] = x or ""
            elif c in REAL:
                d[c] = float(x or 0)
            else:
                d[c] = int(float(x or 0))
        for c in ("Flags", "FlagsExtra"):          # int unsigned en AC; FT_INT con signo en el DB2 y `int` en item_sparse
            d[c] = d[c] - (1 << 32) if d[c] >= 1 << 31 else d[c]
        ac[d["entry"]] = d
    return ac


def sparse_ac(a, c, ext_cost, nombres):
    """Fila ItemSparse 3.4.3 = fila del cliente `c` con los campos de item_template `a` encima."""
    r = dict(c)
    if nombres:
        r["Display"] = a["name"]
        r["Description"] = a["description"]
    r["AllowableRace"] = norm_mascara(a["AllowableRace"], c["AllowableRace"], RAZAS_335)
    r["AllowableClass"] = norm_mascara(a["AllowableClass"], c["AllowableClass"], CLASES_335)
    r["DurationInInventory"] = a["duration"] & 0xFFFFFFFF
    r["BagFamily"] = a["BagFamily"]
    r["StartQuestID"] = a["startquest"]
    r["ItemRange"] = a["RangedModRange"]
    r["Stackable"] = a["stackable"]
    r["MaxCount"] = a["maxcount"]
    r["MinReputation"] = a["RequiredReputationRank"]
    r["RequiredAbility"] = a["requiredspell"]
    r["SellPrice"] = a["SellPrice"]
    r["BuyPrice"] = a["BuyPrice"]
    r["VendorStackCount"] = a["BuyCount"]
    r["Flags1"] = a["Flags"]
    f2 = (c["Flags2"] & ~FLAGS2_AC) | (a["FlagsExtra"] & FLAGS2_AC)
    if a["entry"] not in ext_cost:          # 0x4 (oro además del coste extendido) solo se ve en vendedores con ExtendedCost
        f2 = (f2 & ~0x4) | (c["Flags2"] & 0x4)
    # ITEM_FLAG2_OVERRIDE_GOLD_COST (0x4000): sin ella el cliente 3.4.3 no enseña SellPrice/BuyPrice sino un precio
    # calculado por tablas (ItemPriceBase/ImportPrice*, como Item::GetBuyPrice de TC); con ella usa el de AC
    f2 |= 0x4000
    r["Flags2"] = f2
    r["MaxDurability"] = a["MaxDurability"]
    r["RequiredHoliday"] = a["HolidayId"]
    r["LimitCategory"] = a["ItemLimitCategory"]
    r["GemProperties"] = a["GemProperties"]
    r["SocketMatchEnchantmentId"] = a["socketBonus"]
    r["TotemCategoryID"] = a["TotemCategory"]
    r["InstanceBound"] = a["Map"]
    r["ZoneBound1"] = a["area"]
    r["ItemSet"] = a["itemset"]
    r["LockID"] = a["lockid"]
    r["PageID"] = a["PageText"]
    r["ItemDelay"] = a["delay"]
    r["MinFactionID"] = a["RequiredReputationFaction"]
    r["RequiredSkillRank"] = a["RequiredSkillRank"]
    r["RequiredSkill"] = a["RequiredSkill"]
    r["ItemLevel"] = a["ItemLevel"]
    # el cliente 3.4.3 SUMA QualityModifier al valor guardado: en armas es DPS (daño enseñado = daño guardado +
    # QualityModifier * velocidad) y en armaduras es armadura. Comprobado: AC = cliente + QualityModifier en 557 armas y
    # 510 armaduras. Aquí se escribe el valor final de AC, así que el modificador va a 0 (con -20 Thunderfury salía 6-77)
    r["QualityModifier"] = 0.0
    r["ItemRandomSuffixGroupID"] = a["RandomSuffix"]
    r["RandomSelect"] = a["RandomProperty"]
    dmg = [(a["dmg_min1"], a["dmg_max1"]), (a["dmg_min2"], a["dmg_max2"])]
    for k in range(5):
        mn, mx = dmg[k] if k < 2 else (0, 0)
        r["MinDamage%d" % (k + 1)] = rnd(mn)
        r["MaxDamage%d" % (k + 1)] = rnd(mx)
    for k, col in enumerate(("armor", "holy_res", "fire_res", "nature_res", "frost_res", "shadow_res", "arcane_res")):
        r["Resistances%d" % (k + 1)] = a[col]
    r["ScalingStatDistributionID"] = a["ScalingStatDistribution"]
    # atributos: si el conjunto (tipo, valor) es el mismo, se deja el orden/relleno del cliente
    st_a = sorted((a[S[0][k]], a[S[1][k]]) for k in range(10) if a[S[1][k]])
    st_c = sorted((c["StatModifierBonusStat%d" % k], c["StatModifierBonusAmount%d" % k]) for k in range(1, 11)
                  if c["StatModifierBonusAmount%d" % k] and c["StatModifierBonusStat%d" % k] != -1)
    if st_a != st_c:
        for k in range(10):
            t, v = a[S[0][k]], a[S[1][k]]
            r["StatModifierBonusStat%d" % (k + 1)] = -1 if (t == 0 and v == 0) else t
            r["StatModifierBonusAmount%d" % (k + 1)] = v
    for k in range(3):
        r["SocketType%d" % (k + 1)] = SOCKET.get(a["socketColor_%d" % (k + 1)], a["socketColor_%d" % (k + 1)])
    if not SOLO_TOOLTIP:
        r["SheatheType"] = a["sheath"]
        r["Material"] = material(a["Material"], c["Material"])
        r["PageMaterialID"] = a["PageMaterial"]
        r["LanguageID"] = a["LanguageID"]
    r["Bonding"] = a["bonding"]
    r["DamageDamageType"] = a["dmg_type1"]
    r["ContainerSlots"] = a["ContainerSlots"]
    r["RequiredPVPRank"] = a["requiredhonorrank"]
    r["InventoryType"] = a["InventoryType"]
    r["OverallQualityID"] = a["Quality"]
    r["AmmunitionType"] = a["ammo_type"]
    r["RequiredLevel"] = a["RequiredLevel"]
    return r


def item_ac(a, c):
    """Fila Item 3.4.3 (Item.db2) = cliente + item_template. Icono, ItemGroupSounds y ContentTuning: del cliente."""
    r = dict(c)
    r["ClassID"] = a["class"]
    r["SubclassID"] = a["subclass"]
    r["InventoryType"] = a["InventoryType"]
    r["RequiredLevel"] = a["RequiredLevel"]
    if not SOLO_TOOLTIP:
        r["Material"] = material(a["Material"], c["Material"])
        r["SheatheType"] = a["sheath"]
        r["SoundOverrideSubclassID"] = a["SoundOverrideSubclass"]
    r["RandomSelect"] = a["RandomProperty"]
    r["ItemRandomSuffixGroupID"] = a["RandomSuffix"]
    r["ScalingStatDistributionID"] = a["ScalingStatDistribution"]
    r["MaxDurability"] = a["MaxDurability"]
    r["AmmunitionType"] = a["ammo_type"]
    r["ScalingStatValue"] = a["ScalingStatValue"]
    r["DamageType1"], r["DamageType2"] = a["dmg_type1"], a["dmg_type2"]
    for k in (3, 4, 5):
        r["DamageType%d" % k] = 0
    for k, col in enumerate(("armor", "holy_res", "fire_res", "nature_res", "frost_res", "shadow_res", "arcane_res")):
        r["Resistances%d" % (k + 1)] = a[col]
    dmg = [(a["dmg_min1"], a["dmg_max1"]), (a["dmg_min2"], a["dmg_max2"])]
    for k in range(5):
        mn, mx = dmg[k] if k < 2 else (0, 0)
        r["MinDamage%d" % (k + 1)] = rnd(mn)
        r["MaxDamage%d" % (k + 1)] = rnd(mx)
    return r


def efectos_ac(a):
    """ItemEffect de AC: (LegacySlotIndex, TriggerType, Charges, CoolDownMSec, CategoryCoolDownMSec, SpellCategoryID, SpellID).
    spellppmRate no tiene columna en 3.4.3 (lo aplica AC en el servidor)."""
    out = []
    for k, (sid, trg, car, cd, cat, catcd) in enumerate(SP):
        if a[sid] > 0:
            out.append((k, a[trg], a[car], a[cd], a[catcd], a[cat], a[sid]))
    return out


def clave_ef(e):
    return tuple(e[1:])          # sin el hueco: el orden no se ve en el tooltip


def distinto(x, y):
    if isinstance(x, float) or isinstance(y, float):
        return abs(float(x) - float(y)) > 1e-4
    return x != y


def main():
    nombres = "--sin-nombres" not in ARGS
    con_efectos = "--sin-efectos" not in ARGS
    ac = carga_ac()
    sp, it = L.db2("ItemSparse"), L.db2("Item")
    ef_cli = collections.defaultdict(list)
    for r in L.db2("ItemEffect").values():
        ef_cli[r["ParentItemID"]].append(r)
    ext_cost = {int(r[0]) for r in L.sql("SELECT DISTINCT item FROM npc_vendor WHERE ExtendedCost <> 0 UNION "
                                         "SELECT DISTINCT item FROM game_event_npc_vendor WHERE ExtendedCost <> 0", BD_AC)}
    # filas de hotfix de Blizzard (VerifiedBuild > 0) que ya pisan el DB2 en el servidor: se comparan contra ellas
    # porque son las que tiene el almacén de TC; nuestras filas (VB -343, pasada custom) les ganan
    def hf(tabla):
        cols = [x[0] for x in L.sql("SELECT COLUMN_NAME FROM information_schema.COLUMNS WHERE TABLE_SCHEMA='%s' AND "
                                    "TABLE_NAME='%s' ORDER BY ORDINAL_POSITION" % (BD_HF, tabla), "information_schema")]
        return cols
    cols_sp, cols_it, cols_ef = hf("item_sparse"), hf("item"), hf("item_effect")
    max_ef = max(max(ef_cli_id for ef_cli_id in L.db2("ItemEffect")),
                 int(L.sql("SELECT IFNULL(MAX(ID),0) FROM item_effect", BD_HF)[0][0]))
    blizz = {t: {int(r[0]) for r in L.sql("SELECT ID FROM %s WHERE VerifiedBuild > 0" % t, BD_HF)}
             for t in ("item_sparse", "item", "item_effect")}
    ya_custom = {t: {int(r[0]) for r in L.sql("SELECT ID FROM %s WHERE VerifiedBuild = 0" % t, BD_HF)}
                 for t in ("item_sparse", "item", "item_effect")}
    loc = {}
    if "--locale-esES" in ARGS:
        loc = {int(r[0]): (r[1] or "", r[2] or "") for r in
               L.sql("SELECT ID, Name, Description FROM item_template_locale WHERE locale = 'esES'", BD_AC)}

    sig_ef = max_ef + 1
    cambios = []                               # (entry, tabla, campo, cliente, ac)
    envios = []                                # (entry, [(tabla, fila|None, status)])
    por_campo = collections.Counter()
    objetos_por_tabla = collections.Counter()
    tipos_ef = collections.Counter()
    for e in sorted(ac):
        if e not in sp:
            continue                           # los que faltan ya van por item_sparse VB=0 (DB_QUERY_BULK)
        a, c = ac[e], sp[e]
        regs = []
        ns = sparse_ac(a, c, ext_cost, nombres)
        dif = [k for k in ns if k != "ID" and distinto(ns[k], c[k])]
        if dif and TOL_PRECIO and all(k in ("SellPrice", "BuyPrice") and abs(ns[k] - c[k]) <= TOL_PRECIO for k in dif):
            dif = []                           # solo redondeo del precio (p. ej. 51858 frente a 51857 cobres)
        if dif:
            regs.append(("item_sparse", H_SPARSE, e, ns, 1))
            objetos_por_tabla["ItemSparse"] += 1
            for k in dif:
                cambios.append((e, "ItemSparse", k, c[k], ns[k]))
                por_campo["ItemSparse." + k] += 1
        if e in it:
            ni = item_ac(a, it[e])
            dif = [k for k in ni if k != "ID" and distinto(ni[k], it[e][k])]
            if dif:
                regs.append(("item", H_ITEM, e, ni, 1))
                objetos_por_tabla["Item"] += 1
                for k in dif:
                    cambios.append((e, "Item", k, it[e][k], ni[k]))
                    por_campo["Item." + k] += 1
        if con_efectos:
            ea = efectos_ac(a)
            ec = sorted(ef_cli.get(e, []), key=lambda r: (r["LegacySlotIndex"], r["ID"]))
            kc = sorted((r["TriggerType"], r["Charges"], r["CoolDownMSec"], r["CategoryCoolDownMSec"], r["SpellCategoryID"],
                         r["SpellID"]) for r in ec)
            if sorted(clave_ef(x) for x in ea) != kc:
                objetos_por_tabla["ItemEffect"] += 1
                sa, sc = {x[6] for x in ea}, {r["SpellID"] for r in ec}
                tipos_ef["otros hechizos" if sa != sc else "mismos hechizos, otro disparo/cargas/recarga"] += 1
                cambios.append((e, "ItemEffect", "hechizos", [(r["SpellID"], r["TriggerType"], r["Charges"], r["CoolDownMSec"],
                                                               r["CategoryCoolDownMSec"], r["SpellCategoryID"]) for r in ec],
                                [(x[6], x[1], x[2], x[3], x[4], x[5]) for x in ea]))
                ids = [r["ID"] for r in ec]
                for i, x in enumerate(ea):
                    if i < len(ids):
                        rid = ids[i]
                    else:
                        rid = sig_ef
                        sig_ef += 1
                    fila = {"ID": rid, "LegacySlotIndex": x[0], "TriggerType": x[1], "Charges": x[2], "CoolDownMSec": x[3],
                            "CategoryCoolDownMSec": x[4], "SpellCategoryID": x[5], "SpellID": x[6], "ChrSpecializationID": 0,
                            "ParentItemID": e}
                    regs.append(("item_effect", H_EFFECT, rid, fila, 1))
                for rid in ids[len(ea):]:
                    regs.append(("item_effect", H_EFFECT, rid, None, 2))      # RecordRemoved
        if regs:
            envios.append((e, regs))

    # ------------------------------------------------------------------------------------------------ SQL
    RANGO = {"tinyint": 8, "smallint": 16, "int": 32, "bigint": 64}
    fuera = collections.Counter()

    def sqlv(v, ty, col=""):
        base = ty.split("(")[0].split()[0]
        if base in RANGO and not isinstance(v, str):
            b = RANGO[base]
            lo, hi = (0, (1 << b) - 1) if "unsigned" in ty else (-(1 << (b - 1)), (1 << (b - 1)) - 1)
            if not lo <= int(v) <= hi:                  # MySQL lo recortaría en silencio con sql_mode=''
                fuera[col] += 1
                v = min(max(int(v), lo), hi)
        if isinstance(v, str):
            return "'" + v.replace("\\", "\\\\").replace("'", "\\'") + "'"
        if "float" in ty:
            return repr(float(struct.unpack("<f", struct.pack("<f", float(v)))[0]))
        return str(int(v))
    tipos = {}
    for t in ("item_sparse", "item", "item_effect"):
        tipos[t] = {x[0]: x[1] for x in L.sql("SELECT COLUMN_NAME, COLUMN_TYPE FROM information_schema.COLUMNS WHERE "
                                              "TABLE_SCHEMA='%s' AND TABLE_NAME='%s'" % (BD_HF, t), "information_schema")}
    COLS = {"item_sparse": [x for x in cols_sp if x != "VerifiedBuild"], "item": [x for x in cols_it if x != "VerifiedBuild"],
            "item_effect": [x for x in cols_ef if x != "VerifiedBuild"]}

    # tamaño del registro tal como lo serializa DB2StorageBase::WriteRecord (enUS), para estimar SMSG_HOTFIX_CONNECT
    TAM = {"FT_BYTE": 1, "FT_SHORT": 2, "FT_INT": 4, "FT_FLOAT": 4, "FT_LONG": 8}
    def tam(tabla_db2, fila):
        meta, grupos = L.esquema(tabla_db2)
        n = 0
        for base, noms, t, s, donde in grupos:
            if donde == "id":
                continue
            for nm in noms:
                n += (len(fila[nm].encode("utf-8")) + 1) if t.startswith("FT_STRING") else TAM[t]
        return n
    DB2 = {"item_sparse": "ItemSparse", "item": "Item", "item_effect": "ItemEffect"}

    filas = collections.defaultdict(list)
    hd = []
    bytes_contenido = 0
    for e, regs in envios:
        push = PUSH0 + e
        firma = repr([(t, rid, st, sorted((fila or {}).items())) for t, h, rid, fila, st in regs]).encode("utf-8")
        for i, (t, h, rid, fila, st) in enumerate(regs):
            uid = zlib.crc32(firma + b"|%d|%d|%d" % (h, rid, i)) & 0xFFFFFFFF
            hd.append((push, uid, h, rid, st))
            if fila is not None:
                filas[t].append(fila)
                bytes_contenido += tam(DB2[t], fila)
    ids = {t: [f["ID"] for f in filas[t]] for t in filas}
    out = []
    w = out.append
    w("-- Generado por docs/analysis/gen_hotfix_objetos_ac.py. NO APLICADO.")
    w("-- Objetos que el cliente 3.4.3 ya tiene (ItemSparse/Item/ItemEffect del DB2) con los valores de AzerothCore original")
    w("-- (item_template de AzerothCore). BD: la de hotfixes de la pasarela. Filas VerifiedBuild = %d;" % VB)
    w("-- hotfix_data Id = %d + entry (un envío por objeto), UniqueId = CRC32 del contenido del envío." % PUSH0)
    w("-- %d objetos: %s. hotfix_data: %d filas (%d RecordRemoved de ItemEffect)." % (
        len(envios), ", ".join("%s %d" % kv for kv in sorted(objetos_por_tabla.items())), len(hd), sum(1 for x in hd if x[4] == 2)))
    w("-- Contenido de SMSG_HOTFIX_CONNECT si el cliente lo pide todo: ~%d bytes (+ ~%d de cabeceras de registro)." % (
        bytes_contenido, 21 * len(hd)))
    w("-- Aplicar con la pasarela PARADA y reiniciarla después (LoadHotfixData). Deshacer: las DELETE de abajo (hotfix_data 905xxxxxx + filas VerifiedBuild = -343).")
    w("SET SESSION sql_mode='';")
    w("SET NAMES utf8mb4;")
    w("START TRANSACTION;")
    w("DELETE FROM hotfix_data WHERE Id >= %d AND Id < %d;" % (PUSH0, PUSH0 + 1000000))
    for t in ("item_sparse", "item", "item_effect"):
        w("DELETE FROM %s WHERE VerifiedBuild = %d;" % (t, VB))
    if loc:
        w("DELETE FROM item_sparse_locale WHERE VerifiedBuild = %d;" % VB)
    for t in ("item_sparse", "item", "item_effect"):
        cols = COLS[t]
        lote = []
        for f in sorted(filas[t], key=lambda x: x["ID"]):
            lote.append("(" + ",".join(sqlv(f[cn], tipos[t][cn], t + "." + cn) for cn in cols) + ",%d)" % VB)
            if len(lote) == 500:
                w("INSERT INTO %s (%s,VerifiedBuild) VALUES\n%s;" % (t, ",".join(cols), ",\n".join(lote)))
                lote = []
        if lote:
            w("INSERT INTO %s (%s,VerifiedBuild) VALUES\n%s;" % (t, ",".join(cols), ",\n".join(lote)))
    if loc:
        lote = []
        for f in sorted(filas["item_sparse"], key=lambda x: x["ID"]):
            if f["ID"] in loc and loc[f["ID"]][0]:
                n, d = loc[f["ID"]]
                lote.append("(%d,'esES',%s,'','','',%s,%d)" % (f["ID"], sqlv(d, ""), sqlv(n, ""), VB))
        for i in range(0, len(lote), 500):
            w("INSERT INTO item_sparse_locale (ID,locale,Description_lang,Display3_lang,Display2_lang,Display1_lang,Display_lang,"
              "VerifiedBuild) VALUES\n%s;" % ",\n".join(lote[i:i + 500]))
    for i in range(0, len(hd), 1000):
        w("INSERT INTO hotfix_data (Id,UniqueId,TableHash,RecordId,Status,VerifiedBuild) VALUES\n%s;" %
          ",\n".join("(%d,%d,%d,%d,%d,0)" % x for x in hd[i:i + 1000]))
    w("COMMIT;")
    with open(SALIDA, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(out) + "\n")

    with open(os.path.join(L.SALIDA, "hotfix_objetos_ac_cambios.csv"), "w", newline="", encoding="utf-8") as f:
        wr = csv.writer(f, delimiter=";")
        wr.writerow(["entry", "nombre_ac", "tabla", "campo", "cliente", "ac"])
        for e, t, k, x, y in cambios:
            wr.writerow([e, ac[e]["name"], t, k, x, y])

    choques = {t: sorted(set(ids.get(t, [])) & blizz[t]) for t in blizz}
    inf = collections.OrderedDict()
    inf["item_template_ac"] = len(ac)
    inf["en_itemsparse_cliente"] = sum(1 for e in ac if e in sp)
    inf["objetos_que_cambian"] = len(envios)
    inf["objetos_por_tabla"] = dict(objetos_por_tabla)
    inf["filas"] = {t: len(v) for t, v in filas.items()}
    inf["hotfix_data_filas"] = len(hd)
    inf["itemeffect_recordremoved"] = sum(1 for x in hd if x[4] == 2)
    inf["itemeffect_ids_nuevos"] = [max_ef + 1, sig_ef - 1] if sig_ef > max_ef + 1 else []
    inf["itemeffect_tipos"] = dict(tipos_ef)
    inf["bytes_contenido_hotfix_connect"] = bytes_contenido
    inf["bytes_cabeceras_hotfix_connect"] = 21 * len(hd)
    inf["pisan_filas_blizzard_vb52237"] = choques
    inf["ids_con_fila_vb0"] = {t: sorted(set(ids.get(t, [])) & ya_custom[t]) for t in ya_custom}
    inf["valores_fuera_de_rango_recortados"] = dict(fuera)
    inf["cambios_por_campo"] = dict(por_campo.most_common())
    inf["nombres"] = nombres
    inf["opciones"] = sorted(a for a in sys.argv[1:])
    json.dump(inf, open(os.path.join(L.SALIDA, "hotfix_objetos_ac_informe.json"), "w", encoding="utf-8"),
              ensure_ascii=False, indent=1, default=str)
    print(json.dumps(inf, ensure_ascii=False, indent=1, default=str))


if __name__ == "__main__":
    main()
