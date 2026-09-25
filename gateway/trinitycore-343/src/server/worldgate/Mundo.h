/*
 * Mundo.h — H3: objetos del mundo para worldgate.
 *
 *   SMSG_UPDATE_OBJECT 3.3.5 (0x0A9 / 0x1F6 comprimido, de AzerothCore)
 *     -> se guardan los valores 3.3.5 de cada objeto (índices de AC UpdateFields.h)
 *     -> se vuelcan en un objeto de TrinityCore (Creature / Player / GameObject) que nunca entra en un mapa
 *     -> su propio código escribe el SMSG_UPDATE_OBJECT 3.4.3 (UF::).
 *
 * Los filtros "según quién mira" de TrinityCore (ViewerDependentValues.h) se saltan para objetos fuera de mapa:
 * AzerothCore ya mandó los valores filtrados para este jugador.
 * Se incluye desde Main.cpp después de Rd, Log y Apariencia.
 */
#pragma once

#include <fstream>
#include <chrono>
#include <algorithm>
#include <unordered_set>
#include "TransportMgr.h"
#include "RutasTransporte.h"
#include "Creature.h"
#include "CreatureData.h"
#include "GameObject.h"
#include "GameObjectData.h"
#include "Bag.h"
#include "Item.h"
#include "MovementPackets.h"
#include "InspectPackets.h"
#include "GameTime.h"

// paradas de ascensores que la pasarela registra por objeto (GameObject::GetPauseTimes las consulta; ver GameObject.cpp)
TC_GAME_API std::unordered_map<GameObject const*, std::vector<uint32>>& WorldgatePausas();

// objeto de TrinityCore manejado por la pasarela: expone lo protegido que hace falta para rellenarlo a mano
template <class B>
class Gate : public B
{
public:
    using B::B;
    void Init(ObjectGuid const& g) { this->Object::_Create(g); }
    template <typename T>
    void Set(UF::UpdateFieldSetter<T> s, typename UF::UpdateFieldSetter<T>::value_type v) { this->SetUpdateFieldValue(s, v); }
    void Plantilla(CreatureTemplate const* t) { this->m_creatureInfo = t; }
    void PlantillaGo(GameObjectTemplate const* t) { this->m_goInfo = t; }
    void Rotacion(int64 packed) { this->m_packedRotation = packed; }
    void Velocidad(UnitMoveType t, float rate) { this->m_speed_rate[t] = rate; }
    template <typename T>
    void Bandera(UF::UpdateFieldSetter<T> s, typename UF::UpdateFieldSetter<T>::value_type f) { this->SetUpdateFieldFlagValue(s, f); }
    void Transporte() { this->m_updateFlag.ServerTime = true; }
    // vehículo: el kit de TrinityCore no se desmonta al destruir (Uninstall avisa a scripts y al mapa, que aquí no hay)
    void Vehiculo(uint32 id, uint32 entry) { if constexpr (std::is_base_of_v<Unit, B>) this->CreateVehicleKit(id, entry, true); }
    ~Gate()
    {
        if constexpr (std::is_base_of_v<Unit, B>) (void)this->m_vehicleKit.release();
        // el registro de paradas de ascensores guarda punteros a GameObject
        if constexpr (std::is_base_of_v<GameObject, B>) WorldgatePausas().erase(static_cast<GameObject const*>(this));
    }
};

namespace M335
{
    // tipos de objeto 3.3.5
    enum : uint8 { T_OBJECT = 0, T_ITEM = 1, T_CONTAINER = 2, T_UNIT = 3, T_PLAYER = 4, T_GAMEOBJECT = 5, T_DYNOBJECT = 6, T_CORPSE = 7 };
    // índices de campo 3.3.5 (AzerothCore UpdateFields.h)
    enum : uint16
    {
        OBJ_ENTRY = 3, OBJ_SCALE = 4,
        U_CHARM = 6, U_SUMMON = 8, U_CRITTER = 10, U_CHARMEDBY = 12, U_SUMMONEDBY = 14, U_CREATEDBY = 16, U_TARGET = 18,
        U_CHANNEL_OBJECT = 20, U_CHANNEL_SPELL = 22, U_BYTES_0 = 23, U_HEALTH = 24, U_POWER1 = 25, U_MAXHEALTH = 32, U_MAXPOWER1 = 33,
        U_LEVEL = 54, U_FACTION = 55, U_VIRTUAL_ITEM = 56, U_FLAGS = 59, U_FLAGS_2 = 60,
        U_BOUNDING = 65, U_COMBATREACH = 66, U_DISPLAYID = 67, U_NATIVEDISPLAYID = 68, U_MOUNTDISPLAYID = 69,
        U_BYTES_1 = 74, U_DYNFLAGS = 79, U_MOD_CAST_SPEED = 80, U_CREATED_BY_SPELL = 81, U_NPC_FLAGS = 82, U_EMOTE = 83,
        U_BYTES_2 = 122, U_HOVER = 146,
        DO_CASTER = 6, DO_BYTES = 8, DO_SPELLID = 9, DO_RADIUS = 10, DO_CASTTIME = 11,   // DYNAMICOBJECT_* (AC UpdateFields.h)
        P_QUEST_LOG = 158,                           // 25 x (id, estado, 2 x contadores u16, hora)
        P_FLAGS = 150, P_GUILDID = 151, P_GUILDRANK = 152, P_BYTES = 153, P_BYTES_2 = 154, P_BYTES_3 = 155,
        P_VISIBLE_ITEM = 283, P_XP = 634, P_NEXT_XP = 635, P_COINAGE = 1170,
        // unidad (privados: solo llegan del propio jugador y de sus mascotas)
        U_REGEN_FLAT = 40, U_REGEN_INT = 47, U_ATTACKTIME = 62, U_RANGEDTIME = 64, U_MINDMG = 70, U_STAT = 84, U_POSSTAT = 89, U_NEGSTAT = 94,
        U_RESIST = 99, U_RESIST_POS = 106, U_RESIST_NEG = 113, U_BASE_MANA = 120, U_BASE_HEALTH = 121, U_AP = 123, U_AP_MODS = 124, U_AP_MULT = 125,
        U_RAP = 126, U_RAP_MODS = 127, U_RAP_MULT = 128, U_MINRDMG = 129, U_MAXRDMG = 130,
        // jugador propio
        P_SKILL = 636, P_POINTS1 = 1020, P_TRACK_CREATURES = 1022, P_TRACK_RESOURCES = 1023, P_BLOCK = 1024, P_DODGE = 1025, P_PARRY = 1026,
        P_EXPERTISE = 1027, P_OFF_EXPERTISE = 1028, P_CRIT = 1029, P_RCRIT = 1030, P_OCRIT = 1031, P_SPELLCRIT = 1032, P_SHIELD_BLOCK = 1039,
        P_SHIELD_BLOCK_CRIT = 1040, P_EXPLORED = 1041, P_REST_XP = 1169, P_DMG_POS = 1171, P_DMG_NEG = 1178, P_DMG_PCT = 1185, P_HEAL_POS = 1192,
        P_HEAL_PCT = 1193, P_HEAL_DONE_PCT = 1194, P_TARGET_RESIST = 1195, P_FIELD_BYTES = 1197, P_AMMO = 1198, P_BUYBACK_PRICE = 1201,
        P_BUYBACK_TIME = 1213, P_KILLS = 1225, P_LIFETIME_HK = 1228, P_WATCHED = 1230, P_RATING = 1231, P_MAX_LEVEL = 1279,
        P_GLYPH_SLOTS = 1312, P_GLYPHS = 1318, P_GLYPHS_ENABLED = 1324,
        IT_OWNER = 6, IT_CONTAINED = 8, IT_CREATOR = 10, IT_GIFTCREATOR = 12, IT_STACK = 14, IT_DURATION = 15, IT_CHARGES = 16,
        IT_FLAGS = 21, IT_ENCHANT = 22, IT_SEED = 58, IT_RANDPROP = 59, IT_DURABILITY = 60, IT_MAXDURABILITY = 61, IT_PLAYED = 62,
        CT_NUM_SLOTS = 64, CT_SLOT_1 = 66,
        CO_OWNER = 6, CO_PARTY = 8, CO_DISPLAY = 10, CO_ITEM = 11, CO_BYTES_1 = 30, CO_BYTES_2 = 31, CO_GUILD = 32, CO_FLAGS = 33, CO_DYNFLAGS = 34,
        P_INV_SLOT = 324,                            // 150 huecos x u64 (equipo, bolsas, mochila, banco, bolsas de banco, recompra, llavero, fichas)
        GO_DISPLAYID = 8, GO_FLAGS = 9, GO_PARENTROT = 10, GO_DYNAMIC = 14, GO_FACTION = 15, GO_LEVEL = 16, GO_BYTES_1 = 17,
    };
    // UPDATEFLAG_* 3.3.5
    enum : uint16 { UF_TRANSPORT = 0x2, UF_HAS_TARGET = 0x4, UF_UNKNOWN = 0x8, UF_LOWGUID = 0x10, UF_LIVING = 0x20,
                    UF_STATIONARY = 0x40, UF_VEHICLE = 0x80, UF_POSITION = 0x100, UF_ROTATION = 0x200 };

    // hueco de inventario 3.3.5 -> 3.4.3 (255 = no existe). 3.4.3 mete huecos de profesión (19-29) y bolsa de componentes (34)
    inline uint8 HuecoA343(uint32 s)
    {
        if (s < 19) return uint8(s);                 // equipo
        if (s < 23) return uint8(s - 19 + 30);       // bolsas
        if (s < 39) return uint8(s - 23 + 35);       // mochila (16)
        if (s < 67) return uint8(s - 39 + 59);       // banco
        if (s < 74) return uint8(s - 67 + 87);       // bolsas de banco
        if (s < 86) return uint8(s - 74 + 94);       // recompra
        if (s < 118) return uint8(s - 86 + 106);     // llavero
        return 255;                                  // fichas de moneda: no hay hueco en 3.4.3
    }
    // monedas: en 3.3.5 son objetos en las casillas de fichas (118-149) y la cantidad es su pila; en 3.4.3 son monedas
    // (SetupCurrency/SetCurrency). Mismo ID de moneda en CurrencyTypes.dbc 3.3.5 y CurrencyTypes.db2 del cliente
    // (docs/analysis/mon_monedas.csv). Honor y arena (43307/43308) no: ya van por los campos 1277/1278
    inline uint32 MonedaDeObjeto(uint32 objeto)
    {
        switch (objeto)
        {
            case 29434: return 42;  case 41596: return 61;  case 43016: return 81;  case 44990: return 241;   // justicia, joyero, cocina, sello
            case 40752: return 101; case 40753: return 102; case 45624: return 221; case 47241: return 301; case 49426: return 341;   // emblemas
            case 20560: return 121; case 20559: return 122; case 29024: return 123; case 42425: return 124;   // marcas de honor
            case 20558: return 125; case 43589: return 126; case 47395: return 321;
            case 43228: return 161; case 37836: return 201;                        // fragmento de guardián, moneda de ventura
            default: return 0;
        }
    }
    inline uint8 HuecoA335(uint32 s)
    {
        if (s == 255) return 255;
        if (s < 19) return uint8(s);
        if (s >= 30 && s < 34) return uint8(s - 30 + 19);
        if (s >= 35 && s < 51) return uint8(s - 35 + 23);
        if (s >= 59 && s < 87) return uint8(s - 59 + 39);
        if (s >= 87 && s < 94) return uint8(s - 87 + 67);
        if (s >= 94 && s < 106) return uint8(s - 94 + 74);
        if (s >= 106 && s < 138) return uint8(s - 106 + 86);
        return 254;                                  // hueco sin equivalente
    }

    inline uint8 Octeto(uint32 v, int i) { return uint8(v >> (i * 8)); }
    inline float Float(uint32 v) { float f; std::memcpy(&f, &v, 4); return f; }

    // MOVEMENTFLAG 3.3.5 -> 3.4.3: los bits 0-8 coinciden; en 3.4.3 no existen ONTRANSPORT (0x200) ni SPLINE_ENABLED (0x8000000),
    // así que lo que va por encima se desplaza uno (y dos pasado SPLINE_ENABLED)
    inline uint32 MovFlags(uint32 f)
    {
        uint32 o = f & 0x1FF;
        o |= (f & 0x07FFFC00) >> 1;                 // 0x400..0x4000000 -> 0x200..0x2000000
        o |= (f & 0x70000000) >> 2;                 // WATERWALKING, FALLING_SLOW, HOVER
        return o;
    }
    // y al revés (3.4.3 -> 3.3.5); ONTRANSPORT (0x200) lo pone quien llama si hay transporte
    inline uint32 MovFlagsA335(uint32 f)
    {
        uint32 o = f & 0x1FF;
        o |= (f & 0x03FFFE00) << 1;
        o |= (f & 0x1C000000) << 2;
        return o;
    }
    // MoveSplineFlag 3.3.5 -> 3.4.3 (los 8 bits bajos de 3.3.5 son el id de animación y los Final_* van aparte)
    inline uint32 SplineFlags(uint32 f)
    {
        static uint32 const tabla[][2] = {
            { 0x00000100, 0x00000020 }, { 0x00000200, 0x00000040 }, { 0x00000400, 0x00000080 }, { 0x00000800, 0x04000000 },
            { 0x00002000, 0x00000200 }, { 0x00004000, 0x00000400 }, { 0x00040000, 0x00000800 },
            { 0x00080000, 0x00001000 }, { 0x00100000, 0x00002000 }, { 0x00200000, 0x02000000 }, { 0x00400000, 0x00004000 },
            { 0x00800000, 0x00008000 }, { 0x01000000, 0x00010000 }, { 0x08000000, 0x00080000 } };
        uint32 o = 0;
        for (auto const& e : tabla) if (f & e[0]) o |= e[1];
        // en 3.3.5 Flying ya implica curva (Mask_CatmullRom = Flying | Catmullrom); en 3.4.3 solo Catmullrom (isSmooth):
        // sin ella el cliente va en línea recta de punto a punto y los vuelos dan saltos en cada giro
        if (f & 0x2000) o |= 0x00000800;
        return o;
    }
}

struct Mov335
{
    uint16 upd = 0; uint32 flags = 0; uint16 flags2 = 0;
    float x = 0, y = 0, z = 0, o = 0;
    float speed[9] = { };                            // walk, run, runback, swim, swimback, flight, flightback, turn, pitch
    int64 rot = 0;
    uint32 tiempoTransporte = 0;                     // UPDATEFLAG_TRANSPORT: progreso de la ruta (ms) en los transportes
    uint64 trans = 0; float tpos[4] = { }; uint32 ttime = 0; int8 tseat = -1;   // unidad a bordo de un transporte
    uint32 vehiculo = 0;                             // UPDATEFLAG_VEHICLE: Vehicle.dbc
    // SPLINE_ENABLED: ruta que la unidad lleva a medias al entrar en vista (AC PacketBuilder::WriteCreate)
    bool ruta = false; uint32 sf = 0; uint8 modo = 0; int32 pasado = 0, duracion = 0; uint32 rutaId = 0;
    float cara[3] = { }; uint64 caraGuid = 0; float caraAng = 0.f;
    std::vector<float> puntos;                       // x,y,z seguidos: la tabla interna del spline (con sus puntos virtuales)
};

// TransportMgr::GeneratePath de TrinityCore es privado; la instanciación explícita no comprueba el acceso (idioma
// estándar para llegar a un miembro privado sin tocar TrinityCore). Da la duración de la ruta como la calcula el cliente 3.4.3.
using GenerarRutaFn = void (TransportMgr::*)(GameObjectTemplate const*, TransportTemplate*);
template <GenerarRutaFn F> struct RobaGenerarRuta { friend GenerarRutaFn GenerarRutaTc() { return F; } };
template struct RobaGenerarRuta<&TransportMgr::GeneratePath>;
GenerarRutaFn GenerarRutaTc();

class Mundo
{
public:
    std::function<void(WorldPacket const*)> Enviar;
    std::string acct;
    Gate<Player>* yo = nullptr;                      // el jugador de la sesión (lo crea CrearPersonaje)
    uint64 yo335 = 0;
    uint32 mapa = 0;
    WorldSession* sesionOtros = nullptr;             // sesión muda compartida por los Player de otros jugadores
    std::function<void(uint32)> AlVerMision;         // una misión aparece en el registro (para pedir su plantilla)

    std::recursive_mutex mtx;                        // el hilo del cliente y el del worldserver entran aquí los dos

    // SMSG_UPDATE_COMBO_POINTS (3.3.5 los manda aparte; en 3.4.3 son el poder COMBO_POINTS del jugador)
    void PuntosCombo(uint8 n)
    {
        std::lock_guard<std::recursive_mutex> cerrojo(mtx);
        if (!yo) return;
        uint32 idx = sDB2Manager.GetPowerIndexByClass(POWER_COMBO_POINTS, yo->GetClass());
        if (idx >= MAX_POWERS_PER_CLASS) return;
        yo->Set(yo->m_values.ModifyValue(&Unit::m_unitData).ModifyValue(&UF::UnitData::MaxPower, idx), 5);
        yo->Set(yo->m_values.ModifyValue(&Unit::m_unitData).ModifyValue(&UF::UnitData::Power, idx), int32(n));
        UpdateData ud(mapa);
        yo->BuildValuesUpdateBlockForPlayer(&ud, yo);
        static_cast<Object*>(yo)->ClearUpdateMask(false);
        WorldPacket pkt; ud.BuildPacket(&pkt); Enviar(&pkt);
    }

    // CMSG_MOVE_INIT_ACTIVE_MOVER_COMPLETE: como TrinityCore, fija la hora de transporte del jugador activo
    void InitMover(uint32 ticks)
    {
        std::lock_guard<std::recursive_mutex> cerrojo(mtx);
        if (!yo) return;
        yo->SetPlayerLocalFlag(PLAYER_LOCAL_FLAG_OVERRIDE_TRANSPORT_SERVER_TIME);
        yo->SetTransportServerTime(int32(getMSTime() - ticks));
        UpdateData ud(mapa);
        yo->BuildValuesUpdateBlockForPlayer(&ud, yo);
        static_cast<Object*>(yo)->ClearUpdateMask(false);
        WorldPacket pkt;
        ud.BuildPacket(&pkt);
        Enviar(&pkt);
        Log("[%s] jugador activo listo (hora de transporte fijada)", acct.c_str());
        _esperaMover = false;
        SoltarTransportes();
    }

    // Como TrinityCore (Player::CanNeverSee: "some gameobjects dont function correctly if they are sent before
    // TransportServerTime is correctly set"): tras entrar o cambiar de mapa, los transportes no se mandan hasta el
    // CMSG_MOVE_INIT_ACTIVE_MOVER_COMPLETE. Mandados antes, el cliente los ponía en otro punto (la torre de destino, parados en
    // el aire) y ~10 s después, al fijarse la hora, saltaban a su sitio. Se sueltan aquí con el instante recalculado para el
    // tiempo que pasó esperando, y detrás sus pasajeros.
    bool _esperaMover = false;
    uint32 _contTeleport = 0;                        // último contador de teletransporte de AC (para los que manda la pasarela)
    std::vector<uint64> _transportesEsperando;
    // crea en el cliente un transporte retenido (o vuelve a mandar su creación) con el instante recalculado al momento, y
    // detrás sus pasajeros retenidos; en *pos deja dónde lo pone el cliente (ruta del generador de TC) si se pide
    bool SoltarUno(uint64 g, UpdateData& ud, uint32& np, Position* pos = nullptr)
    {
        auto it = _objs.find(g);
        if (it == _objs.end() || !it->second.o) return false;
        auto* go = static_cast<Gate<GameObject>*>(it->second.o.get());
        auto ahora = std::chrono::steady_clock::now();
        auto rj = _relojes.find(g);
        if (rj != _relojes.end() && rj->second.periodoAc)
        {
            uint32 pasado = uint32(std::chrono::duration_cast<std::chrono::milliseconds>(ahora - rj->second.t0).count());
            uint32 ac = (rj->second.acBase + pasado) % rj->second.periodoAc;
            auto c = _corrTc.find(rj->second.entry);
            uint32 periodoCli = c != _corrTc.end() ? c->second.periodoCli : std::max<uint32>(uint32(int32((*go->m_gameObjectData).Level)), 1);
            uint32 cli = (c != _corrTc.end() ? c->second.Convertir(ac) : ac) % periodoCli;
            rj->second.acBase = ac; rj->second.cliBase = cli; rj->second.t0 = rj->second.ultimo = ahora;
            uint32 dyn = go->GetDynamicFlags() & 0xFFFF;
            go->Set(go->m_values.ModifyValue(&Object::m_objectData).ModifyValue(&UF::ObjectData::DynamicFlags), dyn | (uint32(float(cli) / float(periodoCli) * 65535.0f) << 16));
            Log("[%s] transporte %u mandado: %u ms retenido, instante AC %u -> cliente %u", acct.c_str(), rj->second.entry, pasado, ac, cli);
            if (pos)
            {
                auto rt = _rutaTc.find(rj->second.entry);
                if (rt != _rutaTc.end())
                    if (Optional<Position> q = rt->second->ComputePosition(cli, nullptr, nullptr)) *pos = *q;
            }
        }
        GameTime::UpdateGameTimers();
        go->BuildCreateUpdateBlockForPlayer(&ud, yo);
        static_cast<Object*>(go)->ClearUpdateMask(false);
        auto pend = _pasajerosPend.find(g);
        if (pend != _pasajerosPend.end())
        {
            for (uint64 pg : pend->second)
            {
                auto ip = _objs.find(pg);
                if (ip == _objs.end() || !ip->second.o || !_pasajerosRetenidos.erase(pg)) continue;
                ip->second.o->BuildCreateUpdateBlockForPlayer(&ud, yo);
                ip->second.o->ClearUpdateMask(false);
                ++np;
            }
            _pasajerosPend.erase(pend);
        }
        return true;
    }

    std::vector<uint64> _reenviarAlFijar;             // transportes creados antes de la hora de transporte (el del jugador)
    void SoltarTransportes()
    {
        if ((_transportesEsperando.empty() && _reenviarAlFijar.empty()) || !yo) return;
        UpdateData ud(mapa);
        uint32 n = 0, np = 0;
        for (uint64 g : _transportesEsperando) if (SoltarUno(g, ud, np)) ++n;
        // el transporte en el que llegó el jugador se creó con él, antes de fijarse la hora de transporte: se vuelve a mandar
        // su creación (misma GUID) para que el cliente recalcule su desfase con la hora buena (re_transportes.md)
        for (uint64 g : _reenviarAlFijar) if (SoltarUno(g, ud, np)) ++n;
        _transportesEsperando.clear(); _reenviarAlFijar.clear();
        if (!n) return;
        WorldPacket pkt; ud.BuildPacket(&pkt); Enviar(&pkt);
        Log("[%s] %u transportes y %u pasajeros mandados tras fijar la hora de transporte", acct.c_str(), n, np);
        ColocarABordo();
    }

    // el jugador llega a bordo (cambio de mapa en barco o zepelín): el cliente no lo engancha solo con su MovementInfo y
    // caía del transporte. Como Unit::SendTeleportPacket de TC, se le teletransporta con la GUID del transporte y la posición
    // relativa en cuanto el cliente tiene ese transporte. El ACK que devuelve lo ignora AC (HandleMoveTeleportAck sin
    // teletransporte cercano pendiente)
    bool _colocarABordo = false;
    void ColocarABordo()
    {
        if (!_colocarABordo || !yo || yo->m_movementInfo.transport.guid.IsEmpty()) return;
        {
            auto it = _inv.find(yo->m_movementInfo.transport.guid);
            if (it != _inv.end() && TransporteEnCliente(it->second))
            {
                _colocarABordo = false;
                Position const& off = yo->m_movementInfo.transport.pos;
                WorldPackets::Movement::MoveTeleport tp;
                tp.MoverGUID = yo->GetGUID(); tp.SequenceIndex = _contTeleport;
                tp.Pos = Position(off.GetPositionX(), off.GetPositionY(), off.GetPositionZ());
                tp.Facing = off.GetOrientation();
                tp.TransportGUID = yo->m_movementInfo.transport.guid;
                Enviar(tp.Write());
                Log("[%s] jugador colocado a bordo de %s (%.1f, %.1f, %.1f)", acct.c_str(), tp.TransportGUID->ToString().c_str(),
                    off.GetPositionX(), off.GetPositionY(), off.GetPositionZ());
            }
        }
    }

    // 0x0A9 SMSG_UPDATE_OBJECT / 0x1F6 SMSG_COMPRESSED_UPDATE_OBJECT
    void Paquete(uint16 op, std::vector<uint8> const& body)
    {
        std::lock_guard<std::recursive_mutex> cerrojo(mtx);
        std::vector<uint8> plano;
        std::vector<uint8> const* b = &body;
        if (op == 0x1F6)
        {
            if (body.size() < 4) return;
            uLongf n = 0; std::memcpy(&n, body.data(), 4);
            plano.resize(n);
            if (uncompress(plano.data(), &n, body.data() + 4, uLong(body.size() - 4)) != Z_OK) { Log("[%s] UPDATE_OBJECT comprimido: zlib falla", acct.c_str()); return; }
            plano.resize(n); b = &plano;
        }
        Rd r(*b);
        uint32 bloques = r.get<uint32>();
        UpdateData ud(mapa);
        bool hay = false;
        std::vector<std::pair<uint64, Mov335>> rutas;    // unidades creadas a mitad de una ruta
        for (uint32 i = 0; i < bloques && r.p < b->size(); ++i)
        {
            uint8 tipo = r.get<uint8>();
            switch (tipo)
            {
                case 0:                                  // VALUES
                {
                    uint64 g = Pack(r);
                    std::vector<uint16> cam;
                    auto it = _objs.find(g);
                    if (it == _objs.end()) { std::vector<uint32> tmp; LeerValores(r, tmp, cam); break; }
                    LeerValores(r, it->second.v, cam);
                    hay |= Valores(g, it->second, cam, ud);
                    break;
                }
                case 1:                                  // MOVEMENT
                {
                    uint64 g = Pack(r);
                    Mov335 m; LeerMovimiento(r, m);
                    auto it = _objs.find(g);
                    if (it != _objs.end() && it->second.w) it->second.w->Relocate(m.x, m.y, m.z, m.o);
                    break;
                }
                case 2: case 3:                          // CREATE_OBJECT / CREATE_OBJECT2
                {
                    uint64 g = Pack(r);
                    uint8 t = r.get<uint8>();
                    Mov335 m; LeerMovimiento(r, m);
                    bool ruta = g != yo335 && RutaUsable(m);
                    Obj& o = _objs[g];
                    o.tipo = t;
                    if (g == yo335) yoRecibido = true;       // ya se puede crear al jugador con sus valores
                    std::vector<uint16> cam;
                    LeerValores(r, o.v, cam);
                    hay |= Crear(g, o, m, cam, ud);
                    if (ruta && o.w && !_pasajerosRetenidos.count(g)) rutas.emplace_back(g, std::move(m));
                    break;
                }
                case 4:                                  // OUT_OF_RANGE_OBJECTS
                {
                    uint32 n = r.get<uint32>();
                    for (uint32 k = 0; k < n; ++k) hay |= Quitar(Pack(r), ud, false);
                    break;
                }
                case 5:                                  // NEAR_OBJECTS (no se usa)
                {
                    uint32 n = r.get<uint32>();
                    for (uint32 k = 0; k < n; ++k) Pack(r);
                    break;
                }
                default:
                    Log("[%s] UPDATE_OBJECT: bloque %u desconocido, se descarta el resto", acct.c_str(), tipo);
                    i = bloques;
                    break;
            }
        }
        // pasajeros que llegaron antes que su transporte: se retuvieron sin crear y se crean ahora, detrás del transporte y en
        // el mismo paquete. El cliente 3.4.3 solo engancha una unidad a un transporte que ya conoce al crearla; un MoveUpdate
        // posterior no le vale para un PNJ (se quedaban en el suelo, debajo del zepelín)
        for (uint64 t : _transportesNuevos)
        {
            auto pend = _pasajerosPend.find(t);
            if (pend == _pasajerosPend.end()) continue;
            for (uint64 pg : pend->second)
            {
                auto it = _objs.find(pg);
                if (it == _objs.end() || !it->second.o || !_pasajerosRetenidos.erase(pg)) continue;
                it->second.o->BuildCreateUpdateBlockForPlayer(&ud, yo);
                it->second.o->ClearUpdateMask(false);
                hay = true;
            }
            Log("[%s] transporte %s: %u pasajeros creados tras él", acct.c_str(), ToTc(t).ToString().c_str(), (uint32)pend->second.size());
            _pasajerosPend.erase(pend);
        }
        _transportesNuevos.clear();
        if (hay)
        {
            WorldPacket pkt;
            ud.BuildPacket(&pkt);
            if (retener) retenidos.push_back(pkt);       // aún no existe el jugador: se manda después de crearlo
            else Enviar(&pkt);
        }
        if (!retener) ColocarABordo();                   // por si su transporte llegó en este paquete
        Monedas();
        for (auto const& [g, m] : rutas)                 // detrás de la creación: el cliente ya conoce a la unidad
        {
            WorldPacket pkt;
            if (!RutaEnCurso(g, m, pkt)) continue;
            if (retener) retenidos.push_back(pkt);
            else Enviar(&pkt);
        }
        ResincronizarTransportes();
    }
    bool retener = false;
    bool recreadoYo = false;                         // se acaba de recrear al jugador tras un cambio de mapa
    bool yoRecibido = false;                         // llegó el bloque de creación 3.3.5 del propio jugador
    std::vector<WorldPacket> retenidos;

    // 0x0AA SMSG_DESTROY_OBJECT: u64 guid, u8 al morir
    void Destruir(std::vector<uint8> const& body)
    {
        std::lock_guard<std::recursive_mutex> cerrojo(mtx);
        Rd r(body);
        UpdateData ud(mapa);
        if (Quitar(r.get<uint64>(), ud, true))
        {
            WorldPacket pkt;
            ud.BuildPacket(&pkt);
            Enviar(&pkt);
        }
    }

    // movimiento del propio jugador (cliente 3.4.3) -> cuerpo de MSG_MOVE_* 3.3.5 (AC Unit::BuildMovementPacket)
    std::vector<uint8> MovInfo335(MovementInfo const& mi)
    {
        std::lock_guard<std::recursive_mutex> cerrojo(mtx);
        std::vector<uint8> b = MovimientoA335(mi);
        size_t n = 1; for (int i = 0; i < 8; ++i) if (b[0] & (1 << i)) ++n;   // quita la guid empaquetada
        return std::vector<uint8>(b.begin() + n, b.end());
    }
    MovementInfo LeerMovInfo335(Rd& r, uint32* flags335 = nullptr)
    {
        MovementInfo mi;
        uint32 f = r.get<uint32>(); uint16 f2 = r.get<uint16>();
        mi.time = r.get<uint32>();
        float x = r.get<float>(), y = r.get<float>(), z = r.get<float>(), o = r.get<float>();
        mi.pos.Relocate(x, y, z, o);
        if (f & 0x200)
        {
            uint8 m = r.get<uint8>(); uint64 g = 0;
            for (int i = 0; i < 8; ++i) if (m & (1 << i)) g |= uint64(r.get<uint8>()) << (i * 8);
            mi.transport.guid = ToTc(g);                 // sin ella los teletransportes y cambios de velocidad a bordo soltaban al jugador
            float tx = r.get<float>(), ty = r.get<float>(), tz = r.get<float>(), to = r.get<float>();
            mi.transport.pos.Relocate(tx, ty, tz, to);
            mi.transport.time = r.get<uint32>(); mi.transport.seat = r.get<int8>();
            if (f2 & 0x400) r.get<uint32>();
        }
        if ((f & (0x200000 | 0x2000000)) || (f2 & 0x20)) mi.pitch = r.get<float>();
        mi.jump.fallTime = r.get<uint32>();
        if (f & 0x1000) { mi.jump.zspeed = r.get<float>(); mi.jump.sinAngle = r.get<float>(); mi.jump.cosAngle = r.get<float>(); mi.jump.xyspeed = r.get<float>(); }
        if (f & 0x4000000) mi.stepUpStartElevation = r.get<float>();
        mi.flags = M335::MovFlags(f);
        if (flags335) *flags335 = f;
        return mi;
    }

    // ---- teletransportes
    // MSG_MOVE_TELEPORT_ACK (propio): packguid, u32 contador, movimiento -> SMSG_MOVE_TELEPORT
    void Teleport335(std::vector<uint8> const& body)
    {
        std::lock_guard<std::recursive_mutex> cerrojo(mtx);
        Rd r(body);
        uint64 g = Pack(r); uint32 cont = r.get<uint32>();
        MovementInfo mi = LeerMovInfo335(r);
        if (g != yo335 || !yo) return;
        _contTeleport = cont;
        yo->Relocate(mi.pos);
        WorldPackets::Movement::MoveTeleport p;
        p.MoverGUID = yo->GetGUID(); p.SequenceIndex = cont;
        p.Pos = Position(mi.pos.GetPositionX(), mi.pos.GetPositionY(), mi.pos.GetPositionZ()); p.Facing = mi.pos.GetOrientation();
        if (!mi.transport.guid.IsEmpty())                // a bordo: como Unit::SendTeleportPacket de TC, posición local + transporte
        {
            p.Pos = Position(mi.transport.pos.GetPositionX(), mi.transport.pos.GetPositionY(), mi.transport.pos.GetPositionZ());
            p.Facing = mi.transport.pos.GetOrientation();
            p.TransportGUID = mi.transport.guid;
        }
        Enviar(p.Write());
    }
    // MSG_MOVE_TELEPORT (otros): packguid, movimiento -> SMSG_MOVE_UPDATE_TELEPORT
    void TeleportOtro335(std::vector<uint8> const& body)
    {
        std::lock_guard<std::recursive_mutex> cerrojo(mtx);
        Rd r(body);
        uint64 g = Pack(r);
        auto it = _objs.find(g);
        if (it == _objs.end() || !it->second.w || g == yo335) return;
        MovementInfo mi = LeerMovInfo335(r);
        mi.guid = it->second.tc;
        it->second.w->Relocate(mi.pos);
        WorldPackets::Movement::MoveUpdateTeleport p; p.Status = &mi;
        Enviar(p.Write());
    }
    // SMSG_TRANSFER_PENDING: u32 mapa, [u32 transporte, u32 mapa del transporte]
    void Pendiente335(std::vector<uint8> const& body)
    {
        std::lock_guard<std::recursive_mutex> cerrojo(mtx);
        Rd r(body);
        WorldPackets::Movement::TransferPending p;
        p.MapID = int32(r.get<uint32>());
        if (yo) p.OldMapPosition = Position(yo->GetPositionX(), yo->GetPositionY(), yo->GetPositionZ());
        if (r.p + 8 <= body.size())                      // a bordo: u32 transporte, u32 mapa de origen
        {
            p.Ship.emplace();
            p.Ship->ID = r.get<uint32>(); p.Ship->OriginMapID = int32(r.get<uint32>());
            _barcoPend = p.Ship->ID;
        }
        else
            _barcoPend = 0;
        if (yo)                                          // Player::TeleportTo de TC: sin hora de transporte hasta el INIT_ACTIVE_MOVER
        {
            yo->RemovePlayerLocalFlag(PLAYER_LOCAL_FLAG_OVERRIDE_TRANSPORT_SERVER_TIME);
            yo->SetTransportServerTime(0);
        }
        _esperaMover = true;
        Enviar(p.Write());
    }
    uint32 _barcoPend = 0;                           // transporte (entrada) con el que se cambia de mapa, del TRANSFER_PENDING
    // SMSG_NEW_WORLD: u32 mapa, x, y, z, o. El cliente vacía su mundo; el propio jugador se vuelve a crear entero
    void NuevoMundo335(std::vector<uint8> const& body)
    {
        std::lock_guard<std::recursive_mutex> cerrojo(mtx);
        Rd r(body);
        WorldPackets::Movement::NewWorld p;
        p.MapID = int32(r.get<uint32>());
        float x = r.get<float>(), y = r.get<float>(), z = r.get<float>(), o = r.get<float>();
        p.Reason = 16;                                   // NEW_WORLD_NORMAL, como MovementHandler.cpp de TC
        // a bordo, AC manda la posición DENTRO del barco (Player::TeleportTo con transporte); TC y el cliente esperan la del
        // mundo (con la local cargaba el terreno junto a 0,0). Se calcula con el primer punto de la ruta del cliente en el mapa
        // nuevo, que es donde AC deja el transporte al teletransportarlo (DelayedTeleportTransport)
        if (_barcoPend && TransporteValido(_barcoPend))
        {
            auto rt = _rutaTc.find(_barcoPend);
            if (rt != _rutaTc.end())
                for (TransportPathLeg const& leg : rt->second->PathLegs)
                    if (leg.MapId == uint32(p.MapID))
                    {
                        if (Optional<Position> t = rt->second->ComputePosition(leg.StartTimestamp, nullptr, nullptr))
                        {
                            float c = std::cos(t->GetOrientation()), sn = std::sin(t->GetOrientation());
                            float lx = x, ly = y;
                            x = t->GetPositionX() + lx * c - ly * sn;
                            y = t->GetPositionY() + ly * c + lx * sn;
                            z = t->GetPositionZ() + z;
                            o = Position::NormalizeOrientation(t->GetOrientation() + o);
                            Log("[%s] cambio de mapa a bordo del transporte %u: posición de mundo (%.1f, %.1f, %.1f)", acct.c_str(), _barcoPend, x, y, z);
                        }
                        break;
                    }
        }
        _barcoPend = 0;
        p.Loc.Pos = Position(x, y, z, o);
        mapa = uint32(p.MapID);
        for (auto it = _objs.begin(); it != _objs.end(); )
            if (it->first == yo335) ++it; else it = _objs.erase(it);
        _inv.clear();
        // lo que colgaba de objetos del mapa anterior: si no, un tripulante retenido que vuelve con la misma guid no se crea nunca
        _pasajerosPend.clear(); _pasajerosRetenidos.clear(); _transportesNuevos.clear(); _relojes.clear(); _movPend.clear();
        _transportesEsperando.clear(); _reenviarAlFijar.clear(); _esperaMover = true;
        if (yo) { yo->m_mapId = mapa; yo->Relocate(x, y, z, o); }
        _recrearYo = true;
        Enviar(p.Write());
        Log("[%s] cambio de mapa -> %u (%.1f, %.1f, %.1f)", acct.c_str(), mapa, x, y, z);
    }

    // ---- velocidades: orden 3.3.5 andar, correr, atrás, nadar, nadar atrás, girar, volar, volar atrás, cabeceo
    void Velocidad335(uint16 op, std::vector<uint8> const& body)
    {
        std::lock_guard<std::recursive_mutex> cerrojo(mtx);
        static uint16 const fuerza[9] = { 0x2DA, 0x0E2, 0x0E4, 0x0E6, 0x2DC, 0x2DE, 0x381, 0x383, 0x45C };
        static uint16 const curva[9] = { 0x301, 0x2FE, 0x2FF, 0x300, 0x302, 0x303, 0x385, 0x386, 0x45E };
        static uint16 const aviso[9] = { 0x0D1, 0x0CD, 0x0CF, 0x0D3, 0x0D5, 0x0D8, 0x37E, 0x380, 0x45B };
        static OpcodeServer const setOp[9] = { SMSG_MOVE_SET_WALK_SPEED, SMSG_MOVE_SET_RUN_SPEED, SMSG_MOVE_SET_RUN_BACK_SPEED, SMSG_MOVE_SET_SWIM_SPEED,
            SMSG_MOVE_SET_SWIM_BACK_SPEED, SMSG_MOVE_SET_TURN_RATE, SMSG_MOVE_SET_FLIGHT_SPEED, SMSG_MOVE_SET_FLIGHT_BACK_SPEED, SMSG_MOVE_SET_PITCH_RATE };
        static OpcodeServer const splOp[9] = { SMSG_MOVE_SPLINE_SET_WALK_SPEED, SMSG_MOVE_SPLINE_SET_RUN_SPEED, SMSG_MOVE_SPLINE_SET_RUN_BACK_SPEED, SMSG_MOVE_SPLINE_SET_SWIM_SPEED,
            SMSG_MOVE_SPLINE_SET_SWIM_BACK_SPEED, SMSG_MOVE_SPLINE_SET_TURN_RATE, SMSG_MOVE_SPLINE_SET_FLIGHT_SPEED, SMSG_MOVE_SPLINE_SET_FLIGHT_BACK_SPEED, SMSG_MOVE_SPLINE_SET_PITCH_RATE };
        static OpcodeServer const updOp[9] = { SMSG_MOVE_UPDATE_WALK_SPEED, SMSG_MOVE_UPDATE_RUN_SPEED, SMSG_MOVE_UPDATE_RUN_BACK_SPEED, SMSG_MOVE_UPDATE_SWIM_SPEED,
            SMSG_MOVE_UPDATE_SWIM_BACK_SPEED, SMSG_MOVE_UPDATE_TURN_RATE, SMSG_MOVE_UPDATE_FLIGHT_SPEED, SMSG_MOVE_UPDATE_FLIGHT_BACK_SPEED, SMSG_MOVE_UPDATE_PITCH_RATE };
        Rd r(body);
        for (int k = 0; k < 9; ++k)
        {
            if (op == fuerza[k])                         // propio: packguid, u32 contador, [u8 si correr], float
            {
                uint64 g = Pack(r); uint32 cont = r.get<uint32>(); if (k == 1) r.get<uint8>(); float v = r.get<float>();
                WorldPackets::Movement::MoveSetSpeed p(setOp[k]);
                p.MoverGUID = ToTc(g); p.SequenceIndex = cont; p.Speed = v;
                Enviar(p.Write());
                return;
            }
            if (op == curva[k])                          // PNJ: packguid, float
            {
                uint64 g = Pack(r); float v = r.get<float>();
                WorldPackets::Movement::MoveSplineSetSpeed p(splOp[k]);
                p.MoverGUID = ToTc(g); p.Speed = v;
                Enviar(p.Write());
                return;
            }
            if (op == aviso[k])                          // otros jugadores: packguid, movimiento, float
            {
                uint64 g = Pack(r);
                if (g == yo335) return;
                MovementInfo mi = LeerMovInfo335(r); mi.guid = ToTc(g);
                float v = r.get<float>();
                WorldPackets::Movement::MoveUpdateSpeed p(updOp[k]);
                p.Status = &mi; p.Speed = v;
                Enviar(p.Write());
                return;
            }
        }
    }
    static bool EsVelocidad(uint16 op)
    {
        switch (op)
        {
            case 0x2DA: case 0x0E2: case 0x0E4: case 0x0E6: case 0x2DC: case 0x2DE: case 0x381: case 0x383: case 0x45C:
            case 0x301: case 0x2FE: case 0x2FF: case 0x300: case 0x302: case 0x303: case 0x385: case 0x386: case 0x45E:
            case 0x0D1: case 0x0CD: case 0x0CF: case 0x0D3: case 0x0D5: case 0x0D8: case 0x37E: case 0x380: case 0x45B:
                return true;
            default: return false;
        }
    }

    // ---- estados de movimiento propios (packguid, u32 contador) y de PNJ (packguid)
    bool Estado335(uint16 op, std::vector<uint8> const& body)
    {
        std::lock_guard<std::recursive_mutex> cerrojo(mtx);
        OpcodeServer propio = OpcodeServer(NULL_OPCODE), curva = OpcodeServer(NULL_OPCODE);
        switch (op)
        {
            case 0x0E8: propio = SMSG_MOVE_ROOT; break;
            case 0x0EA: propio = SMSG_MOVE_UNROOT; break;
            case 0x0DE: propio = SMSG_MOVE_SET_WATER_WALK; break;
            case 0x0DF: propio = SMSG_MOVE_SET_LAND_WALK; break;
            case 0x0F4: propio = SMSG_MOVE_SET_HOVERING; break;
            case 0x0F5: propio = SMSG_MOVE_UNSET_HOVERING; break;
            case 0x0F2: propio = SMSG_MOVE_SET_FEATHER_FALL; break;
            case 0x0F3: propio = SMSG_MOVE_SET_NORMAL_FALL; break;
            case 0x343: propio = SMSG_MOVE_SET_CAN_FLY; break;
            case 0x344: propio = SMSG_MOVE_UNSET_CAN_FLY; break;
            case 0x4CE: propio = SMSG_MOVE_DISABLE_GRAVITY; break;   // SMSG_MOVE_GRAVITY_DISABLE
            case 0x4D0: propio = SMSG_MOVE_ENABLE_GRAVITY; break;    // SMSG_MOVE_GRAVITY_ENABLE
            case 0x31A: curva = SMSG_MOVE_SPLINE_ROOT; break;
            case 0x304: curva = SMSG_MOVE_SPLINE_UNROOT; break;
            case 0x305: curva = SMSG_MOVE_SPLINE_SET_FEATHER_FALL; break;
            case 0x306: curva = SMSG_MOVE_SPLINE_SET_NORMAL_FALL; break;
            case 0x307: curva = SMSG_MOVE_SPLINE_SET_HOVER; break;
            case 0x308: curva = SMSG_MOVE_SPLINE_UNSET_HOVER; break;
            case 0x309: curva = SMSG_MOVE_SPLINE_SET_WATER_WALK; break;
            case 0x30A: curva = SMSG_MOVE_SPLINE_SET_LAND_WALK; break;
            case 0x30B: curva = SMSG_MOVE_SPLINE_START_SWIM; break;
            case 0x30C: curva = SMSG_MOVE_SPLINE_STOP_SWIM; break;
            case 0x30D: curva = SMSG_MOVE_SPLINE_SET_RUN_MODE; break;
            case 0x30E: curva = SMSG_MOVE_SPLINE_SET_WALK_MODE; break;
            case 0x422: curva = SMSG_MOVE_SPLINE_SET_FLYING; break;
            case 0x423: curva = SMSG_MOVE_SPLINE_UNSET_FLYING; break;
            case 0x4D3: curva = SMSG_MOVE_SPLINE_DISABLE_GRAVITY; break;
            case 0x4D4: curva = SMSG_MOVE_SPLINE_ENABLE_GRAVITY; break;
            default: return false;
        }
        Rd r(body);
        uint64 g = Pack(r);
        if (propio != OpcodeServer(NULL_OPCODE))
        {
            WorldPackets::Movement::MoveSetFlag p(propio);
            p.MoverGUID = ToTc(g); p.SequenceIndex = r.get<uint32>();
            Enviar(p.Write());
        }
        else
        {
            WorldPackets::Movement::MoveSplineSetFlag p(curva);
            p.MoverGUID = ToTc(g);
            Enviar(p.Write());
            // como Unit::SetDisableGravity/SetHover de TC 3.4.3: el PNJ cambia de AnimTier y de PlayHoverAnim con la bandera
            auto it = _objs.find(g);
            if (it != _objs.end() && it->second.tipo == M335::T_UNIT && it->second.o)
            {
                auto* u = static_cast<Gate<Creature>*>(it->second.o.get());
                uint32 bandera = 0; bool poner = false, recalcular = true;
                switch (op)
                {
                    case 0x4D3: bandera = MOVEMENTFLAG_DISABLE_GRAVITY; poner = true; break;
                    case 0x4D4: bandera = MOVEMENTFLAG_DISABLE_GRAVITY; break;
                    case 0x307: bandera = MOVEMENTFLAG_HOVER; poner = true; break;
                    case 0x308: bandera = MOVEMENTFLAG_HOVER; break;
                    case 0x422: bandera = MOVEMENTFLAG_CAN_FLY; poner = true; recalcular = false; break;   // TC SetCanFly no toca el tier
                    case 0x423: bandera = MOVEMENTFLAG_CAN_FLY | MOVEMENTFLAG_FLYING; break;
                    case 0x31A: bandera = MOVEMENTFLAG_ROOT; poner = true; recalcular = false; break;
                    case 0x304: bandera = MOVEMENTFLAG_ROOT; recalcular = false; break;
                    default: break;
                }
                if (bandera)
                {
                    if (poner) u->AddUnitMovementFlag(bandera); else u->RemoveUnitMovementFlag(bandera);
                    bool retenido = _pasajerosRetenidos.count(g) || EsperandoMover(g);
                    uint8 antes = uint8(u->GetAnimTier()); bool hoverAntes = u->IsPlayingHoverAnim();
                    if (recalcular) AjustarTier(u, it->second.v);
                    if (!retenido && uint8(u->GetAnimTier()) != antes)
                    {
                        WorldPackets::Misc::SetAnimTier t;      // SMSG_SET_ANIM_TIER, como Unit::SetAnimTier (Unit.cpp:9986)
                        t.Unit = it->second.tc; t.Tier = int32(u->GetAnimTier());
                        Enviar(t.Write());
                    }
                    if (!retenido && u->IsPlayingHoverAnim() != hoverAntes)
                    {
                        WorldPackets::Misc::SetPlayHoverAnim h;  // SMSG_SET_PLAY_HOVER_ANIM (Unit.cpp:13184)
                        h.UnitGUID = it->second.tc; h.PlayHoverAnim = u->IsPlayingHoverAnim();
                        Enviar(h.Write());
                    }
                }
            }
        }
        return true;
    }

    // SMSG_MOVE_KNOCK_BACK: packguid, u32 contador, float cos, float sin, float horizontal, float vertical (con signo cambiado)
    void Empujon335(std::vector<uint8> const& body)
    {
        std::lock_guard<std::recursive_mutex> cerrojo(mtx);
        Rd r(body);
        WorldPackets::Movement::MoveKnockBack p;
        p.MoverGUID = ToTc(Pack(r)); p.SequenceIndex = r.get<uint32>();
        float c = r.get<float>(), s = r.get<float>();
        p.Direction = Position(c, s);
        p.Speeds.HorzSpeed = r.get<float>(); p.Speeds.VertSpeed = -r.get<float>();
        Enviar(p.Write());
    }

    std::vector<uint8> MovimientoA335(MovementInfo const& mi)
    {
        std::lock_guard<std::recursive_mutex> cerrojo(mtx);
        Wr w;
        uint64 mover = yo335;                            // un vehículo que el jugador conduce se mueve con su propia guid
        if (!mi.guid.IsEmpty() && yo && mi.guid != yo->GetGUID()) { auto it = _inv.find(mi.guid); if (it != _inv.end()) mover = it->second; }
        PutPack(w, mover);
        uint32 f = M335::MovFlagsA335(mi.flags);
        bool trans = !mi.transport.guid.IsEmpty();
        if (trans) f |= 0x200;
        // cabeceo permitido (cañones, vehículos que apuntan): 3.4.3 FULL_SPEED 0x08 / ALWAYS_ALLOW 0x10 -> 3.3.5 0x10 / 0x20
        uint16 f2 = uint16(((mi.flags2 & 0x08) ? 0x10 : 0) | ((mi.flags2 & 0x10) ? 0x20 : 0));
        w.put<uint32>(f).put<uint16>(f2).put<uint32>(mi.time);
        w.put<float>(mi.pos.GetPositionX()).put<float>(mi.pos.GetPositionY()).put<float>(mi.pos.GetPositionZ()).put<float>(mi.pos.GetOrientation());
        if (trans)
        {
            auto it = _inv.find(mi.transport.guid);
            PutPack(w, it != _inv.end() ? it->second : 0);
            w.put<float>(mi.transport.pos.GetPositionX()).put<float>(mi.transport.pos.GetPositionY())
             .put<float>(mi.transport.pos.GetPositionZ()).put<float>(mi.transport.pos.GetOrientation());
            w.put<uint32>(mi.transport.time).put<int8>(mi.transport.seat);
        }
        if ((f & (0x200000 | 0x2000000)) || (f2 & 0x20)) w.put<float>(mi.pitch);
        w.put<uint32>(mi.jump.fallTime);
        if (f & 0x1000) w.put<float>(mi.jump.zspeed).put<float>(mi.jump.sinAngle).put<float>(mi.jump.cosAngle).put<float>(mi.jump.xyspeed);
        if (f & 0x4000000) w.put<float>(mi.stepUpStartElevation);
        return w.b;
    }

    // MSG_MOVE_* 3.3.5 de otra unidad (jugadores, bots) -> SMSG_MOVE_UPDATE
    void Movimiento335(std::vector<uint8> const& body)
    {
        std::lock_guard<std::recursive_mutex> cerrojo(mtx);
        Rd r(body);
        uint64 g = Pack(r);
        auto it = _objs.find(g);
        if (it == _objs.end() || !it->second.w || g == yo335) return;
        MovementInfo mi;
        mi.guid = it->second.tc;
        uint32 f = r.get<uint32>(); uint16 f2 = r.get<uint16>();
        mi.time = r.get<uint32>();
        float x = r.get<float>(), y = r.get<float>(), z = r.get<float>(), o = r.get<float>();
        mi.pos.Relocate(x, y, z, o);
        if (f & 0x200)
        {
            mi.transport.guid = ToTc(Pack(r));
            float tx = r.get<float>(), ty = r.get<float>(), tz = r.get<float>(), to = r.get<float>();
            mi.transport.pos.Relocate(tx, ty, tz, to);
            mi.transport.time = r.get<uint32>(); mi.transport.seat = r.get<int8>();
            if (f2 & 0x400) r.get<uint32>();
        }
        if ((f & (0x200000 | 0x2000000)) || (f2 & 0x20)) mi.pitch = r.get<float>();
        mi.jump.fallTime = r.get<uint32>();
        if (f & 0x1000) { mi.jump.zspeed = r.get<float>(); mi.jump.sinAngle = r.get<float>(); mi.jump.cosAngle = r.get<float>(); mi.jump.xyspeed = r.get<float>(); }
        if (f & 0x4000000) mi.stepUpStartElevation = r.get<float>();
        mi.flags = M335::MovFlags(f);
        it->second.w->Relocate(x, y, z, o);
        WorldPackets::Movement::MoveUpdate mu;
        mu.Status = &mi;
        Enviar(mu.Write());
    }

    // SMSG_MONSTER_MOVE 3.3.5 (AC PacketBuilder::WriteMonsterMove) -> SMSG_ON_MONSTER_MOVE
    // SMSG_MONSTER_MOVE_TRANSPORT: igual, con packguid del transporte y u8 asiento detrás del que se mueve (puntos relativos)
    void MonsterMove335(std::vector<uint8> const& body, bool enTransporte = false)
    {
        std::lock_guard<std::recursive_mutex> cerrojo(mtx);
        Rd r(body);
        uint64 g = Pack(r);
        uint64 trans = 0; int8 asiento = 0;
        if (enTransporte) { trans = Pack(r); asiento = r.get<int8>(); }
        // el propio jugador también (vuelos): no está en _objs con objeto propio, se usa yo
        WorldObject* mov = nullptr; ObjectGuid movGuid;
        if (g == yo335 && yo) { mov = yo; movGuid = yo->GetGUID(); }
        else { auto it = _objs.find(g); if (it == _objs.end() || !it->second.w) return; mov = it->second.w; movGuid = it->second.tc; }
        r.get<uint8>();
        float sx = r.get<float>(), sy = r.get<float>(), sz = r.get<float>();
        WorldPackets::Movement::MonsterMove mm;
        mm.MoverGUID = movGuid;
        mm.Pos = Position(sx, sy, sz);
        mm.SplineData.ID = r.get<uint32>();
        auto& s = mm.SplineData.Move;
        if (trans) { s.TransportGUID = ToTc(trans); s.VehicleSeat = asiento; }
        uint8 tipo = r.get<uint8>();
        if (tipo == 1)                                   // parar
        {
            s.Flags = 0x20;                              // Done
            s.Points.emplace_back(sx, sy, sz);
            if (!trans) mov->Relocate(sx, sy, sz);
            Enviar(mm.Write());
            return;
        }
        switch (tipo)
        {
            case 2: { s.Face = 1; float fx = r.get<float>(), fy = r.get<float>(), fz = r.get<float>(); s.FaceSpot = Position(fx, fy, fz); break; }
            case 3: s.Face = 2; s.FaceGUID = ToTc(r.get<uint64>()); break;
            case 4: s.Face = 3; s.FaceDirection = r.get<float>(); break;
            default: s.Face = 0; break;
        }
        uint32 f = r.get<uint32>();
        s.Flags = M335::SplineFlags(f) | 0x00400000;     // UncompressedPath: los puntos van enteros
        if (f & 0x200000)                                // Animation
        {
            uint8 anim = r.get<uint8>(); uint32 inicio = r.get<uint32>();
            s.AnimTierTransition.emplace();
            s.AnimTierTransition->TierTransitionID = 0;
            s.AnimTierTransition->StartTime = inicio;
            s.AnimTierTransition->AnimTier = anim;
        }
        s.MoveTime = r.get<uint32>();
        if (f & 0x800)                                   // Parabolic
        {
            s.JumpExtraData.emplace();
            s.JumpExtraData->JumpGravity = r.get<float>();
            s.JumpExtraData->StartTime = r.get<uint32>();
        }
        uint32 n = r.get<uint32>();
        float dx = sx, dy = sy, dz = sz;
        if (f & (0x2000 | 0x40000))                      // Flying / Catmullrom: puntos sin comprimir
        {
            for (uint32 i = 0; i < n; ++i)
            {
                float px = r.get<float>(), py = r.get<float>(), pz = r.get<float>();
                if (px == 0.f && py == 0.f && pz == 0.f) continue;   // punto falso de los ciclos de AC
                // AC repite el punto de unión al empalmar rutas de vuelo: un tramo de longitud 0 hace saltar al cliente 3.4.3
                if (!s.Points.empty())
                {
                    Position const& u = s.Points.back().Pos;
                    if (std::fabs(u.GetPositionX() - px) < 0.05f && std::fabs(u.GetPositionY() - py) < 0.05f && std::fabs(u.GetPositionZ() - pz) < 0.05f) continue;
                }
                s.Points.emplace_back(px, py, pz);
                dx = px; dy = py; dz = pz;
            }
        }
        else                                             // lineal: destino + desplazamientos empaquetados desde el punto medio
        {
            dx = r.get<float>(); dy = r.get<float>(); dz = r.get<float>();
            float mx = (sx + dx) / 2.f, my = (sy + dy) / 2.f, mz = (sz + dz) / 2.f;
            for (uint32 i = 1; i < n; ++i)
            {
                uint32 pk = r.get<uint32>();
                float ox = float(int32(pk << 21) >> 21) * 0.25f, oy = float(int32(pk << 10) >> 21) * 0.25f, oz = float(int32(pk) >> 22) * 0.25f;
                s.Points.emplace_back(mx - ox, my - oy, mz - oz);
            }
            s.Points.emplace_back(dx, dy, dz);
        }
        if (!trans) mov->Relocate(dx, dy, dz);
        Enviar(mm.Write());
    }

    // Unidad que entra en vista a mitad de una ruta: en 3.3.5 la ruta va dentro del bloque de creación y la pasarela no la
    // traslada, así que el cliente 3.4.3 se quedaba con MOVEMENTFLAG_FORWARD y sin ruta = corriendo en el sitio hasta el
    // siguiente MONSTER_MOVE, y entonces saltaba. Se manda lo que le queda como SMSG_ON_MONSTER_MOVE.
    // Si no se puede (caída, salto parabólico, a bordo, terminada), se quitan las banderas de dirección.
    static bool RutaUsable(Mov335& m)
    {
        if (!m.ruta) return false;
        bool ciclica = (m.sf & 0x80000) != 0;
        bool ok = !m.trans && !(m.sf & (0x100 | 0x200 | 0x800 | 0x400000))   // Done, Falling, Parabolic, Frozen
                  && (ciclica || m.duracion - m.pasado > 50) && m.puntos.size() >= 9;
        if (!ok) m.flags &= ~0xFu;                       // FORWARD, BACKWARD, STRAFE_LEFT, STRAFE_RIGHT
        return ok;
    }
    // Tabla del spline de AC (Spline.cpp InitLinear/InitCatmullRom), n nodos:
    //   lineal: reales [0 .. n-2] (+1 virtual; cíclica: el último es el de enganche)
    //   catmullrom: reales [1 .. n-2] (virtuales 0 y n-1); cíclica: reales [1 .. n-3]
    bool RutaEnCurso(uint64 g, Mov335 const& m, WorldPacket& out)
    {
        auto it = _objs.find(g);
        if (it == _objs.end() || !it->second.w) return false;
        size_t n = m.puntos.size() / 3;
        bool ciclica = (m.sf & 0x80000) != 0;
        size_t lo = m.modo == 1 ? 1 : 0;
        size_t hi = (ciclica && m.modo == 1) ? n - 3 : n - 2;
        if (n < 3 || hi <= lo || hi >= n) return false;
        struct V3 { float x, y, z; V3 operator-(V3 o) const { return { x - o.x, y - o.y, z - o.z }; } V3 operator+(V3 o) const { return { x + o.x, y + o.y, z + o.z }; }
                    V3 operator*(float t) const { return { x * t, y * t, z * t }; } float dot(V3 o) const { return x * o.x + y * o.y + z * o.z; } };
        auto P = [&](size_t i) { return V3{ m.puntos[i * 3], m.puntos[i * 3 + 1], m.puntos[i * 3 + 2] }; };
        size_t desde = lo;                               // primer punto que queda por recorrer
        uint32 tiempo = uint32(m.duracion);
        if (!ciclica)
        {
            // tramo más cercano a la posición actual (AC la manda ya interpolada)
            V3 c{ m.x, m.y, m.z };
            float mejor = 1e30f;
            for (size_t k = lo; k < hi; ++k)
            {
                V3 a = P(k), b = P(k + 1), ab = b - a;
                float l2 = ab.dot(ab);
                float t = l2 > 0.f ? std::clamp((c - a).dot(ab) / l2, 0.f, 1.f) : 0.f;
                V3 e = a + ab * t - c;
                float d = e.dot(e);
                if (d < mejor) { mejor = d; desde = k + 1; }
            }
            tiempo = uint32(m.duracion - m.pasado);
        }
        WorldPackets::Movement::MonsterMove mm;
        mm.MoverGUID = it->second.tc;
        mm.Pos = Position(m.x, m.y, m.z);
        mm.SplineData.ID = m.rutaId;
        auto& s = mm.SplineData.Move;
        if (m.sf & 0x20000) { s.Face = 3; s.FaceDirection = m.caraAng; }
        else if (m.sf & 0x10000) { s.Face = 2; s.FaceGUID = ToTc(m.caraGuid); }
        else if (m.sf & 0x8000) { s.Face = 1; s.FaceSpot = Position(m.cara[0], m.cara[1], m.cara[2]); }
        s.Flags = M335::SplineFlags(m.sf & ~0x200000u) | 0x00400000;   // sin Animation (sus datos no vienen en la creación); puntos enteros
        s.MoveTime = std::max<uint32>(tiempo, 1);
        for (size_t k = desde; k <= hi; ++k)
        {
            V3 q = P(k);
            if (!s.Points.empty())                       // tramos de longitud 0 hacen saltar al cliente 3.4.3
            {
                Position const& u = s.Points.back().Pos;
                if (std::fabs(u.GetPositionX() - q.x) < 0.05f && std::fabs(u.GetPositionY() - q.y) < 0.05f && std::fabs(u.GetPositionZ() - q.z) < 0.05f) continue;
            }
            s.Points.emplace_back(q.x, q.y, q.z);
        }
        if (s.Points.empty()) return false;
        if (!ciclica) { Position const& d = s.Points.back().Pos; it->second.w->Relocate(d.GetPositionX(), d.GetPositionY(), d.GetPositionZ()); }
        out = *mm.Write();
        return true;
    }

    // misiones entregadas -> bits de ActivePlayerData::QuestCompleted (Player::SetQuestCompletedBit de TC: el bit sale de
    // QuestV2.UniqueBitFlag). AC 3.3.5 no tiene ese campo; el cliente 3.4.3 lo usa para IsQuestFlaggedCompleted y la interfaz.
    void MisionesHechas(std::vector<uint32> const& ids)
    {
        std::lock_guard<std::recursive_mutex> cerrojo(mtx);
        if (!yo) return;
        uint32 n = 0;
        for (uint32 q : ids)
        {
            uint32 bit = sDB2Manager.GetQuestUniqueBitFlag(q);
            if (!bit || (bit - 1) / 64 >= 875) continue;
            yo->Bandera(yo->m_values.ModifyValue(&Player::m_activePlayerData).ModifyValue(&UF::ActivePlayerData::QuestCompleted, (bit - 1) / 64),
                        UI64LIT(1) << ((bit - 1) % 64));
            ++n;
        }
        if (!n) return;
        UpdateData ud(mapa);
        yo->BuildValuesUpdateBlockForPlayer(&ud, yo);
        static_cast<Object*>(yo)->ClearUpdateMask(false);
        WorldPacket pkt; ud.BuildPacket(&pkt);
        Enviar(&pkt);
        if (ids.size() > 1) Log("[%s] %u misiones completadas marcadas en el cliente", acct.c_str(), n);
    }

    // GameObjectDisplayInfo cuyo modelo es un WMO (worldgate_go_wmo.txt, generado de las DB2 + listfile)
    static std::unordered_set<uint32>& DisplaysWmo()
    {
        static std::unordered_set<uint32> s = [] { std::unordered_set<uint32> r; std::ifstream f("worldgate_go_wmo.txt"); uint32 d; while (f >> d) r.insert(d); return r; }();
        return s;
    }
    static bool EsWmo(uint32 display) { return display && DisplaysWmo().count(display); }

    // vuelta a la pantalla de personajes: se olvida todo lo visto
    void Reiniciar()
    {
        std::lock_guard<std::recursive_mutex> cerrojo(mtx);
        _objs.clear();
        _pasajerosPend.clear(); _pasajerosRetenidos.clear(); _transportesNuevos.clear(); _relojes.clear(); _monedas.clear();
        _inv.clear();
        yo = nullptr;
        yoRecibido = false;
        _hayMovYo = false;
        retenidos.clear();
    }

    // modo prueba: guid 3.3.5 del objeto visible más cercano con esa entrada (0 si no hay)
    uint64 Guid335DeEntrada(uint32 entry)
    {
        std::lock_guard<std::recursive_mutex> cerrojo(mtx);
        uint64 mejor = 0; float dmin = 1e30f;
        for (auto const& [g, o] : _objs)
            if (o.w && At(o.v, M335::OBJ_ENTRY) == entry)
            {
                float dd = yo ? o.w->GetExactDist(yo) : 0.f;
                if (dd < dmin) { dmin = dd; mejor = g; }
            }
        return mejor;
    }
    bool PosicionDe(uint32 entry, Position& pos)
    {
        std::lock_guard<std::recursive_mutex> cerrojo(mtx);
        uint64 g = Guid335DeEntrada(entry);
        auto it = _objs.find(g);
        if (it == _objs.end() || !it->second.w) return false;
        pos = it->second.w->GetPosition();
        return true;
    }
    // aspecto de otro jugador que ya vemos (para la ventana de inspección)
    bool FichaJugador(uint64 g, WorldPackets::Inspect::PlayerModelDisplayInfo& d)
    {
        std::lock_guard<std::recursive_mutex> cerrojo(mtx);
        auto it = _objs.find(g);
        if (it == _objs.end() || it->second.tipo != M335::T_PLAYER || !it->second.o) return false;
        Player const* p = static_cast<Gate<Player>*>(it->second.o.get());
        d.GUID = it->second.tc;
        d.GenderID = p->GetNativeGender(); d.Race = p->GetRace(); d.ClassID = p->GetClass();
        for (UF::ChrCustomizationChoice const& c : p->m_playerData->Customizations) d.Customizations.push_back(c);
        return true;
    }

    bool PosicionGuid(uint64 g, Position& pos)
    {
        std::lock_guard<std::recursive_mutex> cerrojo(mtx);
        auto it = _objs.find(g);
        if (it == _objs.end() || !it->second.w) return false;
        pos = it->second.w->GetPosition();
        return true;
    }
    // un transporte solo se puede mandar si el cliente conoce su ruta (TaxiPath, Data0) y su mapa propio (Data6)
    std::set<uint32> _transportesAvisados;
    // ascensores (tipo 11): pausa = gameobject_template.Data0 (0 = se mueven sin parar)
    std::unordered_map<uint32, uint32> _pausaAscensor;
    std::unordered_map<ObjectGuid, uint32> _ascensores;   // ascensor -> parada a la que va (progreso en ms) según el último estado
    // duración de la animación de un ascensor en el cliente (TransportAnimation: el TimeIndex mayor de su TransportID)
    std::unordered_map<uint32, uint32> _periodoAscensor;
    uint32 PeriodoAscensor(uint32 entry)
    {
        auto it = _periodoAscensor.find(entry);
        if (it != _periodoAscensor.end()) return it->second;
        uint32 t = 0;
        for (TransportAnimationEntry const* e : sTransportAnimationStore)
            if (e->TransportID == entry) t = std::max(t, e->TimeIndex);
        return _periodoAscensor[entry] = t;
    }
    // tipo 33 (edificio destructible): el 3.4.3 lee ParentRotation.x como ID de DestructibleModelData (TC GameObject.cpp:1063,
    // Data18 de la plantilla). AC mete ahí su parche para el 3.3.5 (plataforma del Rey Exánime) y el WMO no se pintaba
    std::unordered_map<uint32, uint32> _modeloDestructible;
    uint32 ModeloDestructible(uint32 entry)
    {
        auto it = _modeloDestructible.find(entry);
        if (it != _modeloDestructible.end()) return it->second;
        uint32 d = 0;
        Db db;
        if (db.Open())
        {
            std::string q = "SELECT Data18 FROM " + Cfg::WorldDbName + ".gameobject_template WHERE entry = " + std::to_string(entry);
            if (!mysql_query(db.h, q.c_str()))
                if (MYSQL_RES* res = mysql_store_result(db.h))
                {
                    if (MYSQL_ROW row = mysql_fetch_row(res)) d = row[0] ? uint32(std::stoul(row[0])) : 0;
                    mysql_free_result(res);
                }
        }
        return _modeloDestructible[entry] = d;
    }
    // guiñada de la rotación empaquetada de AC (GameObject::UpdatePackedRotation: z 21 bits, y 21 bits << 21, x 22 bits << 42,
    // w >= 0 implícita). En un pasajero es la rotación local de la BD: da transport.pos.o como TC CreateGOPassenger
    static float GiroEmpaquetado(int64 p)
    {
        auto ext = [](int64 v, int bits) { int64 s = int64(1) << (bits - 1); return float((v ^ s) - s); };
        float x = ext((p >> 42) & 0x3FFFFF, 22) / float(1 << 21);
        float y = ext((p >> 21) & 0x1FFFFF, 21) / float(1 << 20);
        float z = ext(p & 0x1FFFFF, 21) / float(1 << 20);
        float w2 = 1.f - (x * x + y * y + z * z), w = w2 > 0.f ? std::sqrt(w2) : 0.f;
        return Position::NormalizeOrientation(std::atan2(2.f * (w * z + x * y), 1.f - 2.f * (y * y + z * z)));
    }
    uint32 PausaAscensor(uint32 entry)
    {
        auto it = _pausaAscensor.find(entry);
        if (it != _pausaAscensor.end()) return it->second;
        uint32 pausa = 0;
        Db db;
        if (db.Open())
        {
            std::string q = "SELECT Data0 FROM " + Cfg::WorldDbName + ".gameobject_template WHERE entry = " + std::to_string(entry);
            if (!mysql_query(db.h, q.c_str()))
                if (MYSQL_RES* res = mysql_store_result(db.h))
                {
                    if (MYSQL_ROW row = mysql_fetch_row(res)) pausa = row[0] ? uint32(std::stoul(row[0])) : 0;
                    mysql_free_result(res);
                }
        }
        return _pausaAscensor[entry] = pausa;
    }

    std::unordered_map<uint32, bool> _transportesOk;
    std::unordered_map<uint32, bool> _parables;          // entrada -> AC puede parar este transporte (canBeStopped)
    bool Parable(uint32 entry) { TransporteValido(entry); auto it = _parables.find(entry); return it != _parables.end() && it->second; }
    std::unordered_map<uint32, std::unique_ptr<TransportTemplate>> _rutaTc;   // ruta del transporte como la recorre el cliente
    std::unordered_map<uint32, uint32> _periodoTc;       // entrada -> duración de la vuelta según TrinityCore 3.4.3 (ms)
    std::unordered_map<uint32, CorrespondenciaTransporte> _corrTc;   // entrada -> instante AC -> instante cliente
    struct RelojTransporte { uint32 entry = 0, periodoAc = 0, acBase = 0, cliBase = 0; std::chrono::steady_clock::time_point t0, ultimo; };
    std::unordered_map<uint64, RelojTransporte> _relojes; // transportes que tiene el cliente: su reloj de AC desde la creación

    // El cliente deja correr su reloj de ruta desde la creación; AC reparte el tiempo entre nodos a su manera, así que cada
    // 10 s se comprueba y, si se separan mas de 2 s, se vuelve a mandar el progreso que toca (DynamicFlags).
    void ResincronizarTransportes()
    {
        static bool const apagado = std::ifstream("worldgate_sin_resincro.txt").good();
        if (apagado || _relojes.empty() || !yo || retener) return;
        auto ahora = std::chrono::steady_clock::now();
        UpdateData ud(mapa); bool hay = false;
        for (auto& [g, rj] : _relojes)
        {
            if (ahora - rj.ultimo < std::chrono::seconds(10)) continue;
            rj.ultimo = ahora;
            auto c = _corrTc.find(rj.entry); auto it = _objs.find(g);
            if (c == _corrTc.end() || it == _objs.end() || !it->second.o || !rj.periodoAc) continue;
            uint32 pasado = uint32(std::chrono::duration_cast<std::chrono::milliseconds>(ahora - rj.t0).count());
            uint32 periodoCli = c->second.periodoCli;
            uint32 debe = c->second.Convertir((rj.acBase + pasado) % rj.periodoAc);
            uint32 va = (rj.cliBase + pasado) % periodoCli;
            int32 dif = int32(debe) - int32(va);
            if (dif > int32(periodoCli / 2)) dif -= int32(periodoCli); else if (dif < -int32(periodoCli / 2)) dif += int32(periodoCli);
            if (std::abs(dif) < 2000) continue;          // entre nodos los relojes se separan hasta ~3 s: solo se corrigen los saltos grandes
            // se reancla: a partir de aquí el cliente corre desde "debe"
            rj.acBase = (rj.acBase + pasado) % rj.periodoAc; rj.cliBase = debe; rj.t0 = ahora;
            auto* go = static_cast<Gate<GameObject>*>(it->second.o.get());
            uint32 dyn = go->GetDynamicFlags() & 0xFFFF;
            go->Set(go->m_values.ModifyValue(&Object::m_objectData).ModifyValue(&UF::ObjectData::DynamicFlags), dyn | (uint32(float(debe) / float(periodoCli) * 65535.0f) << 16));
            go->BuildValuesUpdateBlockForPlayer(&ud, yo);
            static_cast<Object*>(go)->ClearUpdateMask(false);
            hay = true;
            Log("[%s] transporte %u: resincronizado %+d ms (cliente %u ms)", acct.c_str(), rj.entry, dif, debe);
        }
        if (hay) { WorldPacket pkt; ud.BuildPacket(&pkt); Enviar(&pkt); }
    }
    bool TransporteValido(uint32 entry)
    {
        auto it = _transportesOk.find(entry);
        if (it != _transportesOk.end()) return it->second;
        static bool const apagados = std::ifstream("worldgate_sin_transportes.txt").good();   // interruptor de emergencia
        if (apagados) return _transportesOk[entry] = false;
        bool ok = false;
        Db db;
        if (db.Open())
        {
            std::string q = "SELECT Data0, Data6, Data1, Data2, Data8 FROM " + Cfg::WorldDbName + ".gameobject_template WHERE entry = " + std::to_string(entry);
            if (!mysql_query(db.h, q.c_str()))
                if (MYSQL_RES* res = mysql_store_result(db.h))
                {
                    if (MYSQL_ROW row = mysql_fetch_row(res))
                    {
                        uint32 ruta = row[0] ? uint32(std::stoul(row[0])) : 0, mapaT = row[1] ? uint32(std::stoul(row[1])) : 0;
                        _parables[entry] = row[4] && std::stoul(row[4]) != 0;   // canBeStopped de AC (cañoneras de ICC)
                        ok = ruta && sTaxiPathStore.HasRecord(ruta) && (!mapaT || sMapStore.HasRecord(mapaT));
                        if (ok)                          // duración de la vuelta con el generador de TrinityCore 3.4.3
                        {
                            GameObjectTemplate gt; std::memset(&gt.raw, 0, sizeof(gt.raw));
                            gt.entry = entry; gt.type = GAMEOBJECT_TYPE_MAP_OBJ_TRANSPORT;
                            gt.moTransport.taxiPathID = ruta;
                            gt.moTransport.moveSpeed = row[2] ? uint32(std::stoul(row[2])) : 1;
                            gt.moTransport.accelRate = row[3] ? uint32(std::stoul(row[3])) : 1;
                            auto tt = std::make_unique<TransportTemplate>();
                            (sTransportMgr->*GenerarRutaTc())(&gt, tt.get());
                            _periodoTc[entry] = tt->TotalPathTime;
                            // instantes de AzerothCore por nodo -> instantes del cliente (ver RutasTransporte.h)
                            RutaAc::Ruta ac;
                            CorrespondenciaTransporte corr;
                            bool anclado = RutaAc::Generar(ruta, float(gt.moTransport.moveSpeed), float(gt.moTransport.accelRate), ac) && corr.Construir(ac, *tt);
                            double peor = 0;             // cuánto se separan los dos relojes a lo largo de la vuelta si se deja correr al cliente
                            if (anclado)
                                for (size_t i = 1; i < corr.anclas.size(); ++i)
                                    peor = std::max(peor, std::fabs((corr.anclas[i].second - corr.anclas[0].second) - (corr.anclas[i].first - corr.anclas[0].first)));
                            Log("transporte %u (ruta %u): vuelta AC calculada %u ms, cliente %u ms, %u nodos, %u anclas%s, desfase máximo sin corregir %.0f ms",
                                entry, ruta, ac.periodo, tt->TotalPathTime, (uint32)ac.nodos.size(), (uint32)corr.anclas.size(), anclado ? "" : " (SIN anclar)", peor);
                            if (anclado) _corrTc[entry] = std::move(corr);
                            _rutaTc[entry] = std::move(tt);
                        }
                    }
                    mysql_free_result(res);
                }
        }
        return _transportesOk[entry] = ok;
    }
    static bool EsBanda(uint32 mapa)
    {
        MapEntry const* e = sMapStore.LookupEntry(mapa);
        return e && e->IsRaid();
    }

    // raza, clase y sexo de un jugador visible (o del propio) para el marcador de los campos de batalla
    bool RazaClaseSexo(uint64 g, uint8& raza, uint8& clase, uint8& sexo)
    {
        std::lock_guard<std::recursive_mutex> cerrojo(mtx);
        Player const* p = nullptr;
        if (g == yo335) p = yo;
        else { auto it = _objs.find(g); if (it != _objs.end() && it->second.tipo == M335::T_PLAYER && it->second.o) p = static_cast<Gate<Player>*>(it->second.o.get()); }
        if (!p) return false;
        raza = p->GetRace(); clase = p->GetClass(); sexo = p->GetNativeGender();
        return true;
    }

    void Listar()
    {
        std::lock_guard<std::recursive_mutex> cerrojo(mtx);
        for (auto const& [g, o] : _objs)
            if (o.o)
                Log("[%s]   tipo %u entrada %u guid335 0x%llX a %.1f m", acct.c_str(), o.tipo, At(o.v, M335::OBJ_ENTRY), (unsigned long long)g,
                    yo && o.w ? o.w->GetExactDist(yo) : 0.f);
    }

    // valores 3.3.5 del propio jugador que llegaron antes de crearlo (se aplican en CrearPersonaje)
    void AplicarPendienteYo()
    {
        std::lock_guard<std::recursive_mutex> cerrojo(mtx);
        auto it = _objs.find(yo335);
        if (!yo || it == _objs.end()) return;
        it->second.tc = yo->GetGUID();
        yo->Set(yo->m_values.ModifyValue(&Unit::m_unitData).ModifyValue(&UF::UnitData::StateAnimID), sDB2Manager.GetEmptyAnimStateID());   // como Player de TC
        std::vector<uint16> todos;
        // los huecos del inventario apuntan a objetos que AC manda en otro paquete: van después (InventarioYo),
        // como TrinityCore, que crea al jugador y luego sus objetos antes de que el cliente los resuelva
        for (size_t i = 0; i < it->second.v.size(); ++i)
            if (it->second.v[i] && !(i >= M335::P_INV_SLOT && i < M335::P_INV_SLOT + 300)) todos.push_back(uint16(i));
        AplicarUnidad(yo, it->second.v, todos);
        AplicarJugador(yo, it->second.v, todos, true);
        if (_hayMovYo)                                   // velocidades y banderas del bloque de creación de AC
        {
            Velocidades(yo, _movYo); _hayMovYo = false;
            if (_movYo.trans) _colocarABordo = true;     // entra al juego a bordo: se le coloca en cuanto llegue su transporte
        }
        Log("[%s] valores del propio jugador aplicados antes de crearlo: %u campos (XP %u / %u)", acct.c_str(), (uint32)todos.size(),
            At(it->second.v, M335::P_XP), At(it->second.v, M335::P_NEXT_XP));
    }

    // objetos del inventario que AzerothCore mandó antes que al propio jugador: Crear() no los representa sin él (if (!yo)),
    // así que se crean aquí, justo después del jugador y antes de rellenar sus huecos (como TrinityCore)
    void CrearPendientes()
    {
        std::lock_guard<std::recursive_mutex> cerrojo(mtx);
        if (!yo) return;
        UpdateData ud(mapa);
        uint32 n = 0, nMundo = 0;
        // AC manda al entrar los transportes del mapa (Map::SendInitTransports) y lo que va a bordo ANTES que al jugador; se
        // descartaban y los barcos/zepelines no salían hasta que cambiaban de mapa. Se crean aquí: transportes primero, para
        // que sus pasajeros se enganchen al crearse
        for (int pasada = 0; pasada < 3; ++pasada)
            for (auto& [g, o] : _objs)
            {
                if (g == yo335 || o.o) continue;
                bool item = o.tipo == M335::T_ITEM || o.tipo == M335::T_CONTAINER;
                bool transp = o.tipo == M335::T_GAMEOBJECT && M335::Octeto(At(o.v, M335::GO_BYTES_1), 1) == 15;
                if ((pasada == 0) != transp || (pasada == 2) != item) continue;
                auto mp = _movPend.find(g);
                if (!item && mp == _movPend.end()) continue;
                std::vector<uint16> cam;
                for (size_t i = 0; i < o.v.size(); ++i) if (o.v[i]) cam.push_back(uint16(i));
                Mov335 m = mp != _movPend.end() ? mp->second : Mov335();
                if (Crear(g, o, m, cam, ud)) { ++n; if (!item) ++nMundo; }
            }
        _movPend.clear();
        _transportesNuevos.clear();
        if (!n) return;
        WorldPacket pkt; ud.BuildPacket(&pkt);
        Enviar(&pkt);
        Log("[%s] %u objetos creados tras el jugador (%u del mundo, el resto de inventario)", acct.c_str(), n, nMundo);
    }

    // huecos del inventario del propio jugador, una vez que el cliente ya tiene sus objetos
    // cantidad de cada moneda según las fichas del propio jugador (casillas 118-149); manda SetCurrency de las que cambian.
    // Si una casilla apunta a un objeto que aún no llegó, no manda nada (evita un 0 falso al entrar o cambiar de mapa)
    std::map<uint32, int32> _monedas;                // moneda -> última cantidad mandada al cliente
    void Monedas()
    {
        std::lock_guard<std::recursive_mutex> cerrojo(mtx);
        auto yoIt = _objs.find(yo335);
        if (!yo || retener || yoIt == _objs.end()) return;
        std::map<uint32, int32> ahora;
        for (uint32 s = 118; s < 150; ++s)
        {
            uint16 i = uint16(M335::P_INV_SLOT + s * 2);
            uint64 g = uint64(At(yoIt->second.v, i)) | (uint64(At(yoIt->second.v, uint16(i + 1))) << 32);
            if (!g) continue;
            auto it = _objs.find(g);
            if (it == _objs.end()) return;
            if (uint32 mon = M335::MonedaDeObjeto(At(it->second.v, M335::OBJ_ENTRY)))
                ahora[mon] += int32(At(it->second.v, M335::IT_STACK));
        }
        for (auto const& [mon, n] : _monedas) if (!ahora.count(mon)) ahora[mon] = 0;   // gastada del todo
        for (auto const& [mon, n] : ahora)
        {
            auto prev = _monedas.find(mon);
            if (prev != _monedas.end() && prev->second == n) continue;
            if (prev == _monedas.end() && n == 0) continue;
            WorldPackets::Misc::SetCurrency c;
            c.Type = int32(mon); c.Quantity = n; c.SuppressChatLog = true;
            Enviar(c.Write());
            _monedas[mon] = n;
        }
    }
    // el aviso de "has recibido X" lo manda Juego con el total: se anota para no repetirlo
    void AnotaMoneda(uint32 mon, int32 n) { std::lock_guard<std::recursive_mutex> cerrojo(mtx); _monedas[mon] = n; }

    void InventarioYo()
    {
        std::lock_guard<std::recursive_mutex> cerrojo(mtx);
        auto it = _objs.find(yo335);
        if (!yo || it == _objs.end()) return;
        std::vector<uint16> inv;
        for (uint16 i = M335::P_INV_SLOT; i < M335::P_INV_SLOT + 300 && i < it->second.v.size(); ++i) if (it->second.v[i]) inv.push_back(i);
        if (inv.empty()) return;
        AplicarJugador(yo, it->second.v, inv, true);
        UpdateData ud(mapa);
        yo->BuildValuesUpdateBlockForPlayer(&ud, yo);
        static_cast<Object*>(yo)->ClearUpdateMask(false);
        WorldPacket pkt; ud.BuildPacket(&pkt);
        Enviar(&pkt);
        Monedas();
    }

    // guid 3.4.3 -> 3.3.5 (objetos vistos; el propio jugador y los jugadores se reconstruyen)
    uint64 A335(ObjectGuid const& g)
    {
        std::lock_guard<std::recursive_mutex> cerrojo(mtx);
        if (g.IsEmpty()) return 0;
        auto it = _inv.find(g);
        if (it != _inv.end()) return it->second;
        if (g.IsPlayer()) return g.GetCounter();
        return 0;
    }

    uint32 V335(uint64 g, uint16 idx)                // valor 3.3.5 de cualquier objeto visto (incluido el propio)
    {
        std::lock_guard<std::recursive_mutex> cerrojo(mtx);
        auto o = _objs.find(g);
        return o == _objs.end() ? 0 : At(o->second.v, idx);
    }
    uint32 Valor335(ObjectGuid const& g, uint16 idx)
    {
        std::lock_guard<std::recursive_mutex> cerrojo(mtx);
        auto i = _inv.find(g);
        if (i == _inv.end()) return 0;
        auto o = _objs.find(i->second);
        return o == _objs.end() ? 0 : At(o->second.v, idx);
    }

    ObjectGuid ToTc(uint64 g) const
    {
        if (!g) return ObjectGuid::Empty;
        uint16 high = uint16(g >> 48);
        uint32 entry = uint32(g >> 24) & 0xFFFFFF, counter = uint32(g) & 0xFFFFFF;
        uint16 m = uint16(mapa);
        switch (high)
        {
            case 0x0000: return ObjectGuid::Create<HighGuid::Player>(uint32(g));
            case 0xF130: return ObjectGuid::Create<HighGuid::Creature>(m, entry, counter);
            case 0xF140: return ObjectGuid::Create<HighGuid::Pet>(m, entry, counter);
            case 0xF150: return ObjectGuid::Create<HighGuid::Vehicle>(m, entry, counter);
            case 0xF110: case 0xF120: return ObjectGuid::Create<HighGuid::GameObject>(m, entry, counter);
            case 0x4000: return ObjectGuid::Create<HighGuid::Item>(uint32(g));
            case 0x1FC0: return ObjectGuid::Create<HighGuid::Transport>(uint32(g));
            case 0xF100: return ObjectGuid::Create<HighGuid::DynamicObject>(m, entry, counter);
            case 0xF101: return ObjectGuid::Create<HighGuid::Corpse>(m, entry, counter);
            default:     return ObjectGuid::Create<HighGuid::Creature>(m, entry, counter);
        }
    }

private:
    struct Obj
    {
        uint8 tipo = 0;
        std::vector<uint32> v;                       // valores 3.3.5 por índice
        ObjectGuid tc;
        std::unique_ptr<Object> o;                   // nulo = aún sin representar en 3.4.3
        WorldObject* w = nullptr;                    // o, si es un objeto con posición (no los de inventario)
    };
    std::unordered_map<uint64, Obj> _objs;
    std::unordered_map<uint32, std::unique_ptr<CreatureTemplate>> _plantillas;
    std::unordered_map<uint32, std::unique_ptr<GameObjectTemplate>> _plantillasGo;
    std::set<uint8> _tiposAvisados;

    std::unordered_map<ObjectGuid, uint64> _inv;      // guid 3.4.3 -> 3.3.5
    bool _recrearYo = false;
    std::unordered_map<uint64, std::vector<uint64>> _pasajerosPend;   // transporte 3.3.5 -> pasajeros creados antes que él
    std::unordered_set<uint64> _pasajerosRetenidos;                 // pasajeros aún sin crear en el cliente (esperan al transporte)
    std::unordered_map<uint64, Mov335> _movPend;                    // movimiento de lo que llegó antes que el jugador
    bool TransporteEnCliente(uint64 t) const { auto it = _objs.find(t); return it != _objs.end() && it->second.o && !EsperandoMover(t); }
    bool EsperandoMover(uint64 g) const { return std::find(_transportesEsperando.begin(), _transportesEsperando.end(), g) != _transportesEsperando.end(); }
    std::vector<uint64> _transportesNuevos;                         // transportes creados en el UPDATE_OBJECT en curso
    Mov335 _movYo; bool _hayMovYo = false;         // movimiento del bloque de creación propio que llegó antes de crear al jugador
    static void PutPack(Wr& w, uint64 g)
    {
        uint8 m = 0; std::vector<uint8> b;
        for (int i = 0; i < 8; ++i) if (uint8 x = uint8(g >> (i * 8))) { m |= uint8(1 << i); b.push_back(x); }
        w.put<uint8>(m); for (uint8 x : b) w.put<uint8>(x);
    }
    static uint32 At(std::vector<uint32> const& v, size_t i) { return i < v.size() ? v[i] : 0; }
    static uint64 Pack(Rd& r)
    {
        uint8 m = r.get<uint8>(); uint64 g = 0;
        for (int i = 0; i < 8; ++i) if (m & (1 << i)) g |= uint64(r.get<uint8>()) << (i * 8);
        return g;
    }
    static void LeerValores(Rd& r, std::vector<uint32>& v, std::vector<uint16>& cam)
    {
        uint8 n = r.get<uint8>();
        std::vector<uint32> mask(n);
        for (uint32& m : mask) m = r.get<uint32>();
        for (uint32 i = 0; i < n * 32u; ++i)
            if (mask[i / 32] & (1u << (i % 32)))
            {
                uint32 val = r.get<uint32>();
                if (i >= v.size()) v.resize(i + 1, 0);
                v[i] = val;
                cam.push_back(uint16(i));
            }
    }
    // bloque de movimiento 3.3.5 (AC Object::BuildMovementUpdate + Unit::BuildMovementPacket + MoveSpline WriteCreate)
    static void LeerMovimiento(Rd& r, Mov335& m)
    {
        using namespace M335;
        m.upd = r.get<uint16>();
        if (m.upd & UF_LIVING)
        {
            m.flags = r.get<uint32>(); m.flags2 = r.get<uint16>(); r.get<uint32>();
            m.x = r.get<float>(); m.y = r.get<float>(); m.z = r.get<float>(); m.o = r.get<float>();
            if (m.flags & 0x200)                        // ONTRANSPORT
            {
                m.trans = Pack(r); for (float& f : m.tpos) f = r.get<float>(); m.ttime = r.get<uint32>(); m.tseat = r.get<int8>();
                if (m.flags2 & 0x400) r.get<uint32>();  // INTERPOLATED_MOVEMENT
            }
            if ((m.flags & (0x200000 | 0x2000000)) || (m.flags2 & 0x20)) r.get<float>();   // pitch
            r.get<uint32>();                            // fallTime
            if (m.flags & 0x1000) for (int k = 0; k < 4; ++k) r.get<float>();              // salto
            if (m.flags & 0x4000000) r.get<float>();    // SPLINE_ELEVATION
            for (float& s : m.speed) s = r.get<float>();
            if (m.flags & 0x8000000)                    // SPLINE_ENABLED
            {
                uint32 sf = m.sf = r.get<uint32>();
                if (sf & 0x20000) m.caraAng = r.get<float>();
                else if (sf & 0x10000) m.caraGuid = r.get<uint64>();
                else if (sf & 0x8000) for (float& f : m.cara) f = r.get<float>();
                m.pasado = r.get<int32>(); m.duracion = r.get<int32>(); m.rutaId = r.get<uint32>();
                r.get<float>(); r.get<float>(); r.get<float>(); r.get<uint32>();   // duration_mod, _next, vertical_acc, effect_start
                uint32 nodos = r.get<uint32>();
                if (nodos <= 4096)
                {
                    m.puntos.resize(size_t(nodos) * 3);
                    for (float& f : m.puntos) f = r.get<float>();
                    m.ruta = true;
                }
                else r.p += size_t(nodos) * 12;
                m.modo = r.get<uint8>();
                r.get<float>(); r.get<float>(); r.get<float>();
            }
        }
        else if (m.upd & UF_POSITION)
        {
            // GameObjects, cadáveres y objetos dinámicos (AC Object::BuildMovementUpdate): GUID del transporte, posición de
            // mundo, desplazamiento relativo (si va a bordo; si no, repite la de mundo) y orientación de MUNDO. La relativa no
            // viaja: para los GameObjects se saca de la rotación empaquetada (es la local de la BD)
            uint64 t = Pack(r);
            m.x = r.get<float>(); m.y = r.get<float>(); m.z = r.get<float>();
            float tx = r.get<float>(), ty = r.get<float>(), tz = r.get<float>();
            m.o = r.get<float>(); r.get<float>();
            if (t) { m.trans = t; m.tpos[0] = tx; m.tpos[1] = ty; m.tpos[2] = tz; m.tpos[3] = 0.f; }
        }
        else if (m.upd & UF_STATIONARY)
        {
            m.x = r.get<float>(); m.y = r.get<float>(); m.z = r.get<float>(); m.o = r.get<float>();
        }
        if (m.upd & UF_UNKNOWN) r.get<uint32>();
        if (m.upd & UF_LOWGUID) r.get<uint32>();
        if (m.upd & UF_HAS_TARGET) Pack(r);
        if (m.upd & UF_TRANSPORT) m.tiempoTransporte = r.get<uint32>();
        if (m.upd & UF_VEHICLE) { m.vehiculo = r.get<uint32>(); r.get<float>(); }
        if (m.upd & UF_ROTATION) m.rot = r.get<int64>();
    }

    CreatureTemplate const* PlantillaDe(uint32 entry)
    {
        auto& p = _plantillas[entry];
        if (!p) { p = std::make_unique<CreatureTemplate>(); p->Entry = entry; }
        return p.get();
    }
    GameObjectTemplate const* PlantillaGoDe(uint32 entry, uint8 tipo)
    {
        auto& p = _plantillasGo[entry];
        if (!p) { p = std::make_unique<GameObjectTemplate>(); p->entry = entry; }
        p->type = tipo;
        return p.get();
    }

    template <class G>
    void Colocar(G* u, ObjectGuid tc, Mov335 const& m)
    {
        u->Init(tc);
        u->m_mapId = mapa;
        u->Relocate(m.x, m.y, m.z, m.o);
        u->m_movementInfo.guid = tc;
    }
    // Multiplicadores de ritmo a 1 como Creature::UpdateEntry / Player::InitStatsForLevel de TrinityCore. El 3.3.5 no los tiene
    // (salvo ModCastingSpeed) y a 0 el cliente divide la velocidad del misil por ModTimeRate (0x185e960): velocidad infinita,
    // el misil impacta en el primer fotograma y no se ve viajar (Bola de fuego, flechas, balas de todos)
    static void RitmosTc(Unit* u)
    {
        u->SetModCastingSpeed(1.0f); u->SetModSpellHaste(1.0f); u->SetModHaste(1.0f);
        u->SetModRangedHaste(1.0f); u->SetModHasteRegen(1.0f); u->SetModTimeRate(1.0f);
    }

    template <class G>
    void Velocidades(G* u, Mov335 const& m)
    {
        static UnitMoveType const orden[9] = { MOVE_WALK, MOVE_RUN, MOVE_RUN_BACK, MOVE_SWIM, MOVE_SWIM_BACK, MOVE_FLIGHT, MOVE_FLIGHT_BACK, MOVE_TURN_RATE, MOVE_PITCH_RATE };
        u->SetUnitMovementFlags(M335::MovFlags(m.flags));
        for (int k = 0; k < 9; ++k)
        {
            float rate = u->GetSpeedRate(orden[k]), actual = u->GetSpeed(orden[k]);
            if (m.speed[k] > 0.f && actual > 0.f && rate > 0.f) u->Velocidad(orden[k], m.speed[k] / (actual / rate));
        }
        if (m.trans)                                     // a bordo de un transporte (si aún no lo conoce, se reenvía al crearlo)
        {
            u->m_movementInfo.transport.guid = ToTc(m.trans);
            u->m_movementInfo.transport.pos.Relocate(m.tpos[0], m.tpos[1], m.tpos[2], m.tpos[3]);
            u->m_movementInfo.transport.time = m.ttime;
            u->m_movementInfo.transport.seat = m.tseat;
        }
    }

#define UFO(u, campo) (u)->m_values.ModifyValue(&Object::m_objectData).ModifyValue(&UF::ObjectData::campo)
#define UFU(u, campo) (u)->m_values.ModifyValue(&Unit::m_unitData).ModifyValue(&UF::UnitData::campo)
    // AnimTier de una unidad como lo deja TrinityCore 3.4.3 (Unit::SetDisableGravity/SetHover, Unit.cpp:12617/12843).
    // AC no toca UNIT_FIELD_BYTES_1 byte 3 al quitar la gravedad (el cliente 3.3.5 sacaba el vuelo de las banderas de
    // movimiento); el 3.4.3 elige la animación por AnimTier y con Ground (0) anda/corre en el aire.
    // Lo que AC pone a mano (creature_addon.bytes1, fin de un spline con Animation) manda sobre lo deducido.
    template <class G>
    uint8 TierEfectivo(G* u, std::vector<uint32> const& v)
    {
        using namespace M335;
        uint8 t = Octeto(At(v, U_BYTES_1), 3);
        if (t == 1) t = 0;                               // UNIT_BYTE1_FLAG_ALWAYS_STAND (fantasma en AC); el 1 de 3.4.3 es Swim
        if (t || u->GetTypeId() != TYPEID_UNIT || At(v, U_HEALTH) == 0) return t;
        if (u->HasUnitMovementFlag(MOVEMENTFLAG_ROOT)) return uint8(u->GetAnimTier());   // TC no lo cambia enraizado
        if (u->HasUnitMovementFlag(MOVEMENTFLAG_DISABLE_GRAVITY | MOVEMENTFLAG_FLYING)) return 3;   // AnimTier::Fly
        if (u->HasUnitMovementFlag(MOVEMENTFLAG_HOVER)) return 2;                                   // AnimTier::Hover
        return 0;
    }
    template <class G>
    void AjustarTier(G* u, std::vector<uint32> const& v)
    {
        u->Set(UFU(u, AnimTier), TierEfectivo(u, v));
        if (u->GetTypeId() == TYPEID_UNIT)               // bit PlayHoverAnim de la creación (Object.cpp:160), sin mandar nada
            u->SetPlayHoverAnim(At(v, M335::U_HEALTH) != 0 && u->HasUnitMovementFlag(MOVEMENTFLAG_DISABLE_GRAVITY | MOVEMENTFLAG_HOVER), false);
    }

    // campos de unidad (PNJ y jugadores)
    template <class G>
    void AplicarUnidad(G* u, std::vector<uint32> const& v, std::vector<uint16> const& cam)
    {
        using namespace M335;
        bool poderes = false;
        for (uint16 i : cam)
        {
            uint32 x = At(v, i);
            switch (i)
            {
                case OBJ_ENTRY: u->Set(UFO(u, EntryID), int32(x)); break;
                case OBJ_SCALE: u->Set(UFO(u, Scale), Float(x)); break;
                case U_DYNFLAGS:                             // bits distintos en 3.4.3 (y 0x02 allí es HIDE_MODEL)
                {
                    uint32 n = 0;
                    if (x & 0x01) n |= 0x04;                 // LOOTABLE
                    if (x & 0x02) n |= 0x08;                 // TRACK_UNIT
                    if ((x & 0x04) && !(x & 0x08)) n |= 0x10;   // TAPPED (por otro: nombre gris); TAPPED_BY_PLAYER no existe
                    if (x & 0x10) n |= 0x20;                 // SPECIALINFO
                    if (x & 0x40) n |= 0x80;                 // REFER_A_FRIEND
                    u->Set(UFO(u, DynamicFlags), n);         // DEAD (0x20) y TAPPED_BY_ALL_THREAT_LIST (0x80) no existen en 3.4.3
                    break;
                }
                case U_CHARM: case U_CHARM + 1: u->Set(UFU(u, Charm), ToTc(Guid(v, U_CHARM))); break;
                case 75: u->Set(UFU(u, PetNumber), x); break;            // UNIT_FIELD_PETNUMBER: sin él la mascota sale con el nombre de la criatura
                case 76: u->Set(UFU(u, PetNameTimestamp), x); break;
                case 77: u->Set(UFU(u, PetExperience), x); break;
                case 78: u->Set(UFU(u, PetNextLevelExperience), x); break;
                case U_SUMMON: case U_SUMMON + 1: u->Set(UFU(u, Summon), ToTc(Guid(v, U_SUMMON))); break;
                case U_CHARMEDBY: case U_CHARMEDBY + 1: u->Set(UFU(u, CharmedBy), ToTc(Guid(v, U_CHARMEDBY))); break;
                case U_SUMMONEDBY: case U_SUMMONEDBY + 1: u->Set(UFU(u, SummonedBy), ToTc(Guid(v, U_SUMMONEDBY))); break;
                case U_CREATEDBY: case U_CREATEDBY + 1: u->Set(UFU(u, CreatedBy), ToTc(Guid(v, U_CREATEDBY))); break;
                case U_TARGET: case U_TARGET + 1: u->Set(UFU(u, Target), ToTc(Guid(v, U_TARGET))); break;
                case U_CHANNEL_OBJECT: case U_CHANNEL_OBJECT + 1:  // UNIT_FIELD_CHANNEL_OBJECT -> ChannelObjects (lo que mantiene la animación del canal)
                    u->ClearChannelObjects();
                    if (uint64 co = Guid(v, U_CHANNEL_OBJECT)) u->AddChannelObject(ToTc(co));
                    break;
                case U_CHANNEL_SPELL:                            // UNIT_CHANNEL_SPELL -> ChannelData (hechizo y su visual)
                    u->SetChannelSpellId(x);
                    u->SetChannelSpellXSpellVisualId(x ? uint32(VisualHechizo(x)) : 0);
                    break;
                case U_BYTES_0:
                    u->Set(UFU(u, Race), Octeto(x, 0)); u->Set(UFU(u, ClassId), Octeto(x, 1));
                    u->Set(UFU(u, Sex), Octeto(x, 2)); u->Set(UFU(u, DisplayPower), Octeto(x, 3));
                    poderes = true;
                    break;
                case U_HEALTH: u->Set(UFU(u, Health), int64(x)); break;
                case U_MAXHEALTH: u->Set(UFU(u, MaxHealth), int64(x)); break;
                case U_LEVEL: u->Set(UFU(u, Level), int32(x)); break;
                case U_FACTION: u->Set(UFU(u, FactionTemplate), int32(x)); break;
                case U_VIRTUAL_ITEM: case U_VIRTUAL_ITEM + 1: case U_VIRTUAL_ITEM + 2:
                    u->Set(u->m_values.ModifyValue(&Unit::m_unitData).ModifyValue(&UF::UnitData::VirtualItems, i - U_VIRTUAL_ITEM).ModifyValue(&UF::VisibleItem::ItemID), int32(x));
                    break;
                case U_FLAGS: u->Set(UFU(u, Flags), x); break;
                case U_FLAGS_2: u->Set(UFU(u, Flags2), x); break;
                case U_BOUNDING: u->Set(UFU(u, BoundingRadius), Float(x)); break;
                case U_COMBATREACH: u->Set(UFU(u, CombatReach), Float(x)); break;
                case U_DISPLAYID: u->Set(UFU(u, DisplayID), int32(x)); u->Set(UFU(u, DisplayScale), 1.0f); break;
                case U_NATIVEDISPLAYID: u->Set(UFU(u, NativeDisplayID), int32(x)); u->Set(UFU(u, NativeXDisplayScale), 1.0f); break;
                case U_MOUNTDISPLAYID: u->Set(UFU(u, MountDisplayID), int32(x)); break;
                case U_BYTES_1:
                    u->Set(UFU(u, StandState), Octeto(x, 0)); u->Set(UFU(u, PetTalentPoints), Octeto(x, 1));
                    u->Set(UFU(u, VisFlags), Octeto(x, 2)); AjustarTier(u, v);
                    break;
                case U_MOD_CAST_SPEED: u->Set(UFU(u, ModCastingSpeed), Float(x)); u->Set(UFU(u, ModSpellHaste), Float(x)); break;
                case 61: u->Set(UFU(u, AuraState), x); break;   // UNIT_FIELD_AURASTATE: mismas máscaras en AC y TC (Ejecutar, Revancha, Ataque de la Victoria...)
                case U_CREATED_BY_SPELL: u->Set(UFU(u, CreatedBySpell), int32(x)); break;
                case U_NPC_FLAGS: u->Set(u->m_values.ModifyValue(&Unit::m_unitData).ModifyValue(&UF::UnitData::NpcFlags, 0), x); break;
                case U_EMOTE: u->Set(UFU(u, EmoteState), int32(x)); break;
                case U_BYTES_2:
                    u->Set(UFU(u, SheatheState), Octeto(x, 0)); u->Set(UFU(u, PvpFlags), Octeto(x, 1));
                    u->Set(UFU(u, PetFlags), Octeto(x, 2)); u->Set(UFU(u, ShapeshiftForm), Octeto(x, 3));
                    break;
                case U_HOVER: u->Set(UFU(u, HoverHeight), Float(x)); break;
                case U_ATTACKTIME: case U_ATTACKTIME + 1: u->Set(u->m_values.ModifyValue(&Unit::m_unitData).ModifyValue(&UF::UnitData::AttackRoundBaseTime, i - U_ATTACKTIME), x); break;
                case U_RANGEDTIME: u->Set(UFU(u, RangedAttackRoundBaseTime), x); break;
                case U_MINDMG: u->Set(UFU(u, MinDamage), Float(x)); break;
                case U_MINDMG + 1: u->Set(UFU(u, MaxDamage), Float(x)); break;
                case U_MINDMG + 2: u->Set(UFU(u, MinOffHandDamage), Float(x)); break;
                case U_MINDMG + 3: u->Set(UFU(u, MaxOffHandDamage), Float(x)); break;
                case U_BASE_MANA: u->Set(UFU(u, BaseMana), int32(x)); break;
                case U_BASE_HEALTH: u->Set(UFU(u, BaseHealth), int32(x)); break;
                case U_AP: u->Set(UFU(u, AttackPower), int32(x)); break;
                case U_AP_MODS: u->Set(UFU(u, AttackPowerModPos), int32(x & 0xFFFF)); u->Set(UFU(u, AttackPowerModNeg), int32(x >> 16)); break;
                case U_AP_MULT: u->Set(UFU(u, AttackPowerMultiplier), Float(x)); break;
                case U_RAP: u->Set(UFU(u, RangedAttackPower), int32(x)); break;
                case U_RAP_MODS: u->Set(UFU(u, RangedAttackPowerModPos), int32(x & 0xFFFF)); u->Set(UFU(u, RangedAttackPowerModNeg), int32(x >> 16)); break;
                case U_RAP_MULT: u->Set(UFU(u, RangedAttackPowerMultiplier), Float(x)); break;
                case U_MINRDMG: u->Set(UFU(u, MinRangedDamage), Float(x)); break;
                case U_MAXRDMG: u->Set(UFU(u, MaxRangedDamage), Float(x)); break;
                default:
                    if ((i >= U_POWER1 && i < U_POWER1 + 7) || (i >= U_MAXPOWER1 && i < U_MAXPOWER1 + 7) || (i >= U_REGEN_FLAT && i < U_REGEN_FLAT + 14)) poderes = true;
                    else if (i >= U_STAT && i < U_STAT + 5) u->Set(u->m_values.ModifyValue(&Unit::m_unitData).ModifyValue(&UF::UnitData::Stats, i - U_STAT), int32(x));
                    else if (i >= U_POSSTAT && i < U_POSSTAT + 5) u->Set(u->m_values.ModifyValue(&Unit::m_unitData).ModifyValue(&UF::UnitData::StatPosBuff, i - U_POSSTAT), int32(x));
                    else if (i >= U_NEGSTAT && i < U_NEGSTAT + 5) u->Set(u->m_values.ModifyValue(&Unit::m_unitData).ModifyValue(&UF::UnitData::StatNegBuff, i - U_NEGSTAT), int32(x));
                    else if (i >= U_RESIST && i < U_RESIST + 7) u->Set(u->m_values.ModifyValue(&Unit::m_unitData).ModifyValue(&UF::UnitData::Resistances, i - U_RESIST), int32(x));
                    else if (i >= U_RESIST_POS && i < U_RESIST_POS + 7) u->Set(u->m_values.ModifyValue(&Unit::m_unitData).ModifyValue(&UF::UnitData::ResistanceBuffModsPositive, i - U_RESIST_POS), int32(x));
                    else if (i >= U_RESIST_NEG && i < U_RESIST_NEG + 7) u->Set(u->m_values.ModifyValue(&Unit::m_unitData).ModifyValue(&UF::UnitData::ResistanceBuffModsNegative, i - U_RESIST_NEG), int32(x));
                    break;
            }
        }
        // 3.3.5 guarda cada poder en su tipo (POWER1 = maná...); 3.4.3 los guarda por índice de la clase
        if (poderes)
        {
            uint8 cls = Octeto(At(v, U_BYTES_0), 1);
            for (uint8 p = 0; p < 7; ++p)
            {
                uint32 idx = sDB2Manager.GetPowerIndexByClass(Powers(p), cls);
                if (idx >= MAX_POWERS_PER_CLASS) continue;
                u->Set(u->m_values.ModifyValue(&Unit::m_unitData).ModifyValue(&UF::UnitData::Power, idx), int32(At(v, U_POWER1 + p)));
                u->Set(u->m_values.ModifyValue(&Unit::m_unitData).ModifyValue(&UF::UnitData::MaxPower, idx), int32(At(v, U_MAXPOWER1 + p)));
                u->Set(u->m_values.ModifyValue(&Unit::m_unitData).ModifyValue(&UF::UnitData::PowerRegenFlatModifier, idx), Float(At(v, U_REGEN_FLAT + p)));
                u->Set(u->m_values.ModifyValue(&Unit::m_unitData).ModifyValue(&UF::UnitData::PowerRegenInterruptedFlatModifier, idx), Float(At(v, U_REGEN_INT + p)));
            }
        }
    }

    // campos de jugador: apariencia, banderas, objetos visibles; y los privados si es el propio
    void AplicarJugador(Gate<Player>* p, std::vector<uint32> const& v, std::vector<uint16> const& cam, bool propio)
    {
        using namespace M335;
        bool apariencia = false;
        for (uint16 i : cam)
        {
            uint32 x = At(v, i);
            if (i == P_FLAGS)
            {
                // AC: 0x400/0x800 = ocultar casco/capa; en 3.4.3 son modo guerra y el casco/capa van en PlayerFlagsEx 0x80/0x100
                // (Wrathion Player.h, ShowingHelm 0x1b87650). 0x10000, 0x800000 y 0x1000000 significan otra cosa en 3.4.3
                p->Set(p->m_values.ModifyValue(&Player::m_playerData).ModifyValue(&UF::PlayerData::PlayerFlags), x & ~(0x400u | 0x800u | 0x10000u | 0x800000u | 0x1000000u));
                p->Set(p->m_values.ModifyValue(&Player::m_playerData).ModifyValue(&UF::PlayerData::PlayerFlagsEx), ((x & 0x400) ? 0x80u : 0u) | ((x & 0x800) ? 0x100u : 0u));
            }
            else if (i == 148 || i == 149)                  // PLAYER_DUEL_ARBITER: sin él el cliente no deja atacar al rival (0x179f536)
                p->Set(p->m_values.ModifyValue(&Player::m_playerData).ModifyValue(&UF::PlayerData::DuelArbiter), ToTc(Guid(v, 148)));
            else if (i == 156) p->Set(p->m_values.ModifyValue(&Player::m_playerData).ModifyValue(&UF::PlayerData::DuelTeam), x);   // PLAYER_DUEL_TEAM
            else if (i == 321) p->Set(p->m_values.ModifyValue(&Player::m_playerData).ModifyValue(&UF::PlayerData::PlayerTitle), int32(x));   // PLAYER_CHOSEN_TITLE
            else if (propio && i >= 626 && i < 632)          // PLAYER__FIELD_KNOWN_TITLES: 3 x u64
            {
                uint32 k = (i - 626) / 2;
                p->SetKnownTitles(k, uint64(At(v, uint16(626 + k * 2))) | (uint64(At(v, uint16(627 + k * 2))) << 32));
            }
            else if (propio && i == 1199)                    // PLAYER_SELF_RES_SPELL: opción de piedra de alma / reencarnación al morir
            {
                p->ClearSelfResSpell();
                if (x) p->AddSelfResSpell(int32(x));
            }
            else if (i == P_GUILDID) p->Set(p->m_values.ModifyValue(&Unit::m_unitData).ModifyValue(&UF::UnitData::GuildGUID), x ? ObjectGuid::Create<HighGuid::Guild>(x) : ObjectGuid::Empty);
            else if (i == P_GUILDRANK) p->Set(p->m_values.ModifyValue(&Player::m_playerData).ModifyValue(&UF::PlayerData::GuildRankID), x);
            else if (i == U_BYTES_0 || i == P_BYTES || i == P_BYTES_2 || i == P_BYTES_3) apariencia = true;
            else if (i >= P_VISIBLE_ITEM && i < P_VISIBLE_ITEM + 38 && (i - P_VISIBLE_ITEM) % 2 == 0)
                p->Set(p->m_values.ModifyValue(&Player::m_playerData).ModifyValue(&UF::PlayerData::VisibleItems, (i - P_VISIBLE_ITEM) / 2).ModifyValue(&UF::VisibleItem::ItemID), int32(x));
            else if (i >= P_VISIBLE_ITEM && i < P_VISIBLE_ITEM + 38)   // PLAYER_VISIBLE_ITEM_n_ENCHANTMENT: u16 permanente | u16 temporal
            {
                // brillo del arma: en 3.4.3 VisibleItem::ItemVisual = SpellItemEnchantment.ItemVisual (Item::GetVisibleItemVisual de TC,
                // que usa el permanente); si el permanente no tiene visual, el temporal (venenos, aceites), como pintaba el 3.3.5
                uint16 visual = 0;
                for (uint32 e : { x & 0xFFFF, x >> 16 })
                    if (!visual && e) if (SpellItemEnchantmentEntry const* en = sSpellItemEnchantmentStore.LookupEntry(e)) visual = en->ItemVisual;
                p->Set(p->m_values.ModifyValue(&Player::m_playerData).ModifyValue(&UF::PlayerData::VisibleItems, (i - P_VISIBLE_ITEM) / 2).ModifyValue(&UF::VisibleItem::ItemVisual), visual);
            }
            else if (propio && (i == 624 || i == 625))   // PLAYER_FARSIGHT (UNIT_END + 0x1DC): la cámara (Ojo de Acherus, visión lejana)
                p->Set(p->m_values.ModifyValue(&Player::m_activePlayerData).ModifyValue(&UF::ActivePlayerData::FarsightObject), ToTc(Guid(v, 624)));
            else if (propio && i >= P_INV_SLOT && i < P_INV_SLOT + 300)
            {
                uint32 s = (i - P_INV_SLOT) / 2;
                uint8 h = HuecoA343(s);
                if (h < 141)
                    p->Set(p->m_values.ModifyValue(&Player::m_activePlayerData).ModifyValue(&UF::ActivePlayerData::InvSlots, h),
                           ToTc(uint64(At(v, P_INV_SLOT + s * 2)) | (uint64(At(v, P_INV_SLOT + s * 2 + 1)) << 32)));
            }
            else if (i >= P_QUEST_LOG && i < P_QUEST_LOG + 125)
            {
                uint32 q = (i - P_QUEST_LOG) / 5, c = (i - P_QUEST_LOG) % 5;
                auto ql = p->m_values.ModifyValue(&Player::m_playerData).ModifyValue(&UF::PlayerData::QuestLog, q);
                if (c == 0) { p->Set(ql.ModifyValue(&UF::QuestLog::QuestID), int32(x)); if (AlVerMision && x) AlVerMision(x); }
                else if (c == 1) p->Set(ql.ModifyValue(&UF::QuestLog::StateFlags), x);
                else if (c == 2 || c == 3)
                {
                    uint32 base = (c - 2) * 2;
                    p->Set(ql.ModifyValue(&UF::QuestLog::ObjectiveProgress, base), uint16(x & 0xFFFF));
                    p->Set(ql.ModifyValue(&UF::QuestLog::ObjectiveProgress, base + 1), uint16(x >> 16));
                }
                else p->Set(ql.ModifyValue(&UF::QuestLog::EndTime), int64(x));
            }
            else if (propio && i >= P_SKILL && i < P_SKILL + 384)
            {
                uint32 s = (i - P_SKILL) / 3, c = (i - P_SKILL) % 3;
                auto sk = p->m_values.ModifyValue(&Player::m_activePlayerData).ModifyValue(&UF::ActivePlayerData::Skill);
                if (c == 0) { p->Set(sk.ModifyValue(&UF::SkillInfo::SkillLineID, s), uint16(x & 0xFFFF)); p->Set(sk.ModifyValue(&UF::SkillInfo::SkillStep, s), uint16(x >> 16)); }
                else if (c == 1) { p->Set(sk.ModifyValue(&UF::SkillInfo::SkillRank, s), uint16(x & 0xFFFF)); p->Set(sk.ModifyValue(&UF::SkillInfo::SkillStartingRank, s), uint16(1)); p->Set(sk.ModifyValue(&UF::SkillInfo::SkillMaxRank, s), uint16(x >> 16)); }
                else { p->Set(sk.ModifyValue(&UF::SkillInfo::SkillTempBonus, s), int16(x & 0xFFFF)); p->Set(sk.ModifyValue(&UF::SkillInfo::SkillPermBonus, s), uint16(x >> 16)); }
            }
            else if (propio && i >= P_EXPLORED && i < P_EXPLORED + 128)
            {
                uint32 j = (i - P_EXPLORED) / 2;
                p->Set(p->m_values.ModifyValue(&Player::m_activePlayerData).ModifyValue(&UF::ActivePlayerData::ExploredZones, j),
                       uint64(At(v, P_EXPLORED + j * 2)) | (uint64(At(v, P_EXPLORED + j * 2 + 1)) << 32));
            }
            else if (propio && i >= P_SPELLCRIT && i < P_SPELLCRIT + 7) p->Set(p->m_values.ModifyValue(&Player::m_activePlayerData).ModifyValue(&UF::ActivePlayerData::SpellCritPercentage, i - P_SPELLCRIT), Float(x));
            else if (propio && i >= P_DMG_POS && i < P_DMG_POS + 7) p->Set(p->m_values.ModifyValue(&Player::m_activePlayerData).ModifyValue(&UF::ActivePlayerData::ModDamageDonePos, i - P_DMG_POS), int32(x));
            else if (propio && i >= P_DMG_NEG && i < P_DMG_NEG + 7) p->Set(p->m_values.ModifyValue(&Player::m_activePlayerData).ModifyValue(&UF::ActivePlayerData::ModDamageDoneNeg, i - P_DMG_NEG), int32(x));
            else if (propio && i >= P_DMG_PCT && i < P_DMG_PCT + 7) p->Set(p->m_values.ModifyValue(&Player::m_activePlayerData).ModifyValue(&UF::ActivePlayerData::ModDamageDonePercent, i - P_DMG_PCT), Float(x));
            else if (propio && i >= P_BUYBACK_PRICE && i < P_BUYBACK_PRICE + 12) p->Set(p->m_values.ModifyValue(&Player::m_activePlayerData).ModifyValue(&UF::ActivePlayerData::BuybackPrice, i - P_BUYBACK_PRICE), x);
            else if (propio && i >= P_BUYBACK_TIME && i < P_BUYBACK_TIME + 12) p->Set(p->m_values.ModifyValue(&Player::m_activePlayerData).ModifyValue(&UF::ActivePlayerData::BuybackTimestamp, i - P_BUYBACK_TIME), int64(x));
            else if (propio && i >= P_RATING && i < P_RATING + 25) p->Set(p->m_values.ModifyValue(&Player::m_activePlayerData).ModifyValue(&UF::ActivePlayerData::CombatRatings, i - P_RATING), int32(x));
            else if (propio && i >= P_GLYPH_SLOTS && i < P_GLYPH_SLOTS + 6) p->Set(p->m_values.ModifyValue(&Player::m_activePlayerData).ModifyValue(&UF::ActivePlayerData::GlyphSlots, i - P_GLYPH_SLOTS), x);
            else if (propio && i >= P_GLYPHS && i < P_GLYPHS + 6) p->Set(p->m_values.ModifyValue(&Player::m_activePlayerData).ModifyValue(&UF::ActivePlayerData::Glyphs, i - P_GLYPHS), x);
            else if (propio && i >= P_POINTS1 && i <= P_GLYPHS_ENABLED) AplicarPropioSuelto(p, i, x, v);
            else if (propio && i == P_XP) p->Set(p->m_values.ModifyValue(&Player::m_activePlayerData).ModifyValue(&UF::ActivePlayerData::XP), int32(x));
            else if (propio && i == P_NEXT_XP) p->Set(p->m_values.ModifyValue(&Player::m_activePlayerData).ModifyValue(&UF::ActivePlayerData::NextLevelXP), int32(x));
            else if (propio && i == P_COINAGE) p->Set(p->m_values.ModifyValue(&Player::m_activePlayerData).ModifyValue(&UF::ActivePlayerData::Coinage), uint64(x));
        }
        if (apariencia)
        {
            uint8 race = Octeto(At(v, U_BYTES_0), 0), sex = Octeto(At(v, P_BYTES_3), 0);
            uint32 b = At(v, P_BYTES);
            uint8 legacy[5] = { Octeto(b, 0), Octeto(b, 1), Octeto(b, 2), Octeto(b, 3), Octeto(At(v, P_BYTES_2), 0) };
            std::vector<UF::ChrCustomizationChoice> cust = Apariencia::ToCustom(race, sex, legacy);
            p->SetCustomizations(Trinity::Containers::MakeIteratorPair(cust.begin(), cust.end()), false);
            p->Set(p->m_values.ModifyValue(&Player::m_playerData).ModifyValue(&UF::PlayerData::NativeSex), sex);
            // PLAYER_BYTES_3: byte 1 borrachera (pantalla borrosa y andar torcido), byte 3 facción de arena
            p->Set(p->m_values.ModifyValue(&Player::m_playerData).ModifyValue(&UF::PlayerData::Inebriation), Octeto(At(v, P_BYTES_3), 1));
            p->Set(p->m_values.ModifyValue(&Player::m_playerData).ModifyValue(&UF::PlayerData::ArenaFaction), Octeto(At(v, P_BYTES_3), 3));
            if (propio && Octeto(At(v, P_BYTES_2), 3))  // PLAYER_BYTES_2 byte 3: estado de descanso (1 descansado, 2 normal)
                p->Set(p->m_values.ModifyValue(&Player::m_activePlayerData).ModifyValue(&UF::ActivePlayerData::RestInfo, 0).ModifyValue(&UF::RestInfo::StateID), Octeto(At(v, P_BYTES_2), 3));
            if (propio)                                  // PLAYER_BYTES_2 byte 2: bolsas de banco compradas
                p->Set(p->m_values.ModifyValue(&Player::m_playerData).ModifyValue(&UF::PlayerData::NumBankSlots), Octeto(At(v, P_BYTES_2), 2));
        }
    }

#define UFI(it, campo) (it)->m_values.ModifyValue(&Item::m_itemData).ModifyValue(&UF::ItemData::campo)
    template <class G>
    void AplicarItem(G* it, std::vector<uint32> const& v, std::vector<uint16> const& cam, bool bolsa)
    {
        using namespace M335;
        for (uint16 i : cam)
        {
            uint32 x = At(v, i);
            switch (i)
            {
                case OBJ_ENTRY: it->Set(it->m_values.ModifyValue(&Object::m_objectData).ModifyValue(&UF::ObjectData::EntryID), int32(x)); break;
                case OBJ_SCALE: it->Set(it->m_values.ModifyValue(&Object::m_objectData).ModifyValue(&UF::ObjectData::Scale), Float(x)); break;
                case IT_OWNER: case IT_OWNER + 1: it->Set(UFI(it, Owner), ToTc(Guid(v, IT_OWNER))); break;
                case IT_CONTAINED: case IT_CONTAINED + 1: it->Set(UFI(it, ContainedIn), ToTc(Guid(v, IT_CONTAINED))); break;
                case IT_CREATOR: case IT_CREATOR + 1: it->Set(UFI(it, Creator), ToTc(Guid(v, IT_CREATOR))); break;
                case IT_GIFTCREATOR: case IT_GIFTCREATOR + 1: it->Set(UFI(it, GiftCreator), ToTc(Guid(v, IT_GIFTCREATOR))); break;
                case IT_STACK: it->Set(UFI(it, StackCount), x); break;
                case IT_DURATION: it->Set(UFI(it, Expiration), x); break;
                case IT_FLAGS: it->Set(UFI(it, DynamicFlags), x); break;
                case IT_SEED: it->Set(UFI(it, PropertySeed), int32(x)); break;
                case IT_RANDPROP: it->Set(UFI(it, RandomPropertiesID), int32(x)); break;
                case IT_DURABILITY: it->Set(UFI(it, Durability), x); break;
                case IT_MAXDURABILITY: it->Set(UFI(it, MaxDurability), x); break;
                case IT_PLAYED: it->Set(UFI(it, CreatePlayedTime), x); break;
                default:
                    if (i >= IT_CHARGES && i < IT_CHARGES + 5)
                        it->Set(it->m_values.ModifyValue(&Item::m_itemData).ModifyValue(&UF::ItemData::SpellCharges, i - IT_CHARGES), int32(x));
                    else if (i >= IT_ENCHANT && i < IT_ENCHANT + 36)
                    {
                        uint32 k = (i - IT_ENCHANT) / 3, c = (i - IT_ENCHANT) % 3;
                        if (k >= 7) ++k;                         // AC PROP 7-11 -> 3.4.3 PROP 8-12 (el 7 es USE_ENCHANTMENT_SLOT)
                        if (k >= MAX_ENCHANTMENT_SLOT) continue;
                        auto e = it->m_values.ModifyValue(&Item::m_itemData).ModifyValue(&UF::ItemData::Enchantment, k);
                        if (c == 0) it->Set(e.ModifyValue(&UF::ItemEnchantment::ID), int32(x));
                        else if (c == 1) it->Set(e.ModifyValue(&UF::ItemEnchantment::Duration), x);
                        else it->Set(e.ModifyValue(&UF::ItemEnchantment::Charges), int16(x & 0xFFFF));
                    }
                    else if (bolsa && i == CT_NUM_SLOTS)
                        it->Set(it->m_values.ModifyValue(&Bag::m_containerData).ModifyValue(&UF::ContainerData::NumSlots), x);
                    else if (bolsa && i >= CT_SLOT_1 && i < CT_SLOT_1 + 72)
                    {
                        uint32 s = (i - CT_SLOT_1) / 2;
                        it->Set(it->m_values.ModifyValue(&Bag::m_containerData).ModifyValue(&UF::ContainerData::Slots, s), ToTc(Guid(v, uint16(CT_SLOT_1 + s * 2))));
                    }
                    break;
            }
        }
    }
#undef UFI

    // SpellXSpellVisual por defecto de un hechizo (igual que Juego::VisualDe; aquí hace falta para los objetos dinámicos)
    static int32 VisualHechizo(uint32 spell)
    {
        static std::unordered_map<uint32, std::pair<uint32, int32>> const mapa = []
        {
            std::unordered_map<uint32, std::pair<uint32, int32>> m;
            for (SpellXSpellVisualEntry const* e : sSpellXSpellVisualStore)
            {
                if (e->DifficultyID != 0 || e->CasterPlayerConditionID || e->CasterUnitConditionID) continue;   // como TrinityCore
                m[e->SpellID] = { e->ID, e->Priority };
            }
            return m;
        }();
        auto it = mapa.find(spell);
        return it != mapa.end() ? int32(it->second.first) : 0;
    }

    // objeto dinámico 3.3.5: CASTER (guid), BYTES (byte 0 = tipo: 0 portal, 1 hechizo de área, 2 visión lejana), SPELLID, RADIUS, CASTTIME
#define UFD(d, campo) (d)->m_values.ModifyValue(&DynamicObject::m_dynamicObjectData).ModifyValue(&UF::DynamicObjectData::campo)
    void AplicarDinamico(Gate<DynamicObject>* d, std::vector<uint32> const& v, std::vector<uint16> const& cam)
    {
        using namespace M335;
        for (uint16 i : cam)
        {
            uint32 x = At(v, i);
            switch (i)
            {
                case OBJ_ENTRY: d->Set(d->m_values.ModifyValue(&Object::m_objectData).ModifyValue(&UF::ObjectData::EntryID), int32(x)); break;
                case OBJ_SCALE: d->Set(d->m_values.ModifyValue(&Object::m_objectData).ModifyValue(&UF::ObjectData::Scale), Float(x)); break;
                case DO_CASTER: case DO_CASTER + 1: d->Set(UFD(d, Caster), ToTc(Guid(v, DO_CASTER))); break;
                case DO_BYTES: d->Set(UFD(d, Type), Octeto(x, 0)); break;
                case DO_SPELLID:
                    d->Set(UFD(d, SpellID), int32(x));
                    d->Set(UFD(d, SpellXSpellVisualID), VisualHechizo(x));
                    break;
                case DO_RADIUS: d->Set(UFD(d, Radius), Float(x)); break;
                case DO_CASTTIME: d->Set(UFD(d, CastTime), x); break;
                default: break;
            }
        }
    }
#undef UFD

#define UFC(c, campo) (c)->m_values.ModifyValue(&Corpse::m_corpseData).ModifyValue(&UF::CorpseData::campo)
    void AplicarCadaver(Gate<Corpse>* c, std::vector<uint32> const& v, std::vector<uint16> const& cam)
    {
        using namespace M335;
        bool apariencia = false;
        for (uint16 i : cam)
        {
            uint32 x = At(v, i);
            switch (i)
            {
                case OBJ_ENTRY: c->Set(c->m_values.ModifyValue(&Object::m_objectData).ModifyValue(&UF::ObjectData::EntryID), int32(x)); break;
                case OBJ_SCALE: c->Set(c->m_values.ModifyValue(&Object::m_objectData).ModifyValue(&UF::ObjectData::Scale), Float(x)); break;
                case CO_OWNER: case CO_OWNER + 1: c->Set(UFC(c, Owner), ToTc(Guid(v, CO_OWNER))); break;
                case CO_PARTY: case CO_PARTY + 1: c->Set(UFC(c, PartyGUID), ToTc(Guid(v, CO_PARTY))); break;
                case CO_DISPLAY: c->Set(UFC(c, DisplayID), x); break;
                case CO_FLAGS: c->Set(UFC(c, Flags), x); break;
                case CO_DYNFLAGS: c->Set(UFC(c, DynamicFlags), x); break;
                case CO_BYTES_1: case CO_BYTES_2: apariencia = true; break;
                default:
                    if (i >= CO_ITEM && i < CO_ITEM + 19)
                        c->Set(c->m_values.ModifyValue(&Corpse::m_corpseData).ModifyValue(&UF::CorpseData::Items, i - CO_ITEM), x);
                    break;
            }
        }
        if (apariencia)                                  // bytes 1: -, raza, sexo, piel; bytes 2: cara, peinado, color, vello
        {
            uint32 b1 = At(v, CO_BYTES_1), b2 = At(v, CO_BYTES_2);
            uint8 race = Octeto(b1, 1), sex = Octeto(b1, 2);
            c->Set(UFC(c, RaceID), race); c->Set(UFC(c, Sex), sex);
            uint8 legacy[5] = { Octeto(b1, 3), Octeto(b2, 0), Octeto(b2, 1), Octeto(b2, 2), Octeto(b2, 3) };
            std::vector<UF::ChrCustomizationChoice> cust = Apariencia::ToCustom(race, sex, legacy);
            c->SetCustomizations(Trinity::Containers::MakeIteratorPair(cust.begin(), cust.end()));
        }
    }
#undef UFC

#define UFA(p, campo) (p)->m_values.ModifyValue(&Player::m_activePlayerData).ModifyValue(&UF::ActivePlayerData::campo)
    void AplicarPropioSuelto(Gate<Player>* p, uint16 i, uint32 x, std::vector<uint32> const& v)
    {
        using namespace M335;
        switch (i)
        {
            case P_POINTS1: p->Set(UFA(p, CharacterPoints), int32(x)); break;
            case P_COINAGE: p->Set(UFA(p, Coinage), uint64(x)); break;
            case P_TRACK_CREATURES: p->Set(UFA(p, TrackCreatureMask), x); break;
            case P_TRACK_RESOURCES: p->Set(p->m_values.ModifyValue(&Player::m_activePlayerData).ModifyValue(&UF::ActivePlayerData::TrackResourceMask, 0), x); break;
            case P_BLOCK: p->Set(UFA(p, BlockPercentage), Float(x)); break;
            case P_DODGE: p->Set(UFA(p, DodgePercentage), Float(x)); break;
            case P_PARRY: p->Set(UFA(p, ParryPercentage), Float(x)); break;
            case P_EXPERTISE: p->Set(UFA(p, MainhandExpertise), float(int32(x))); break;
            case P_OFF_EXPERTISE: p->Set(UFA(p, OffhandExpertise), float(int32(x))); break;
            case P_CRIT: p->Set(UFA(p, CritPercentage), Float(x)); break;
            case P_RCRIT: p->Set(UFA(p, RangedCritPercentage), Float(x)); break;
            case P_OCRIT: p->Set(UFA(p, OffhandCritPercentage), Float(x)); break;
            case P_SHIELD_BLOCK: p->Set(UFA(p, ShieldBlock), int32(x)); break;
            case P_SHIELD_BLOCK_CRIT: p->Set(UFA(p, ShieldBlockCritPercentage), Float(x)); break;
            case P_REST_XP:
            {
                auto ri = p->m_values.ModifyValue(&Player::m_activePlayerData).ModifyValue(&UF::ActivePlayerData::RestInfo, 0);
                p->Set(ri.ModifyValue(&UF::RestInfo::Threshold), x);
                uint8 st = Octeto(At(v, P_BYTES_2), 3);
                p->Set(ri.ModifyValue(&UF::RestInfo::StateID), uint8(st ? st : 2));
                break;
            }
            case P_HEAL_POS: p->Set(UFA(p, ModHealingDonePos), int32(x)); break;
            case P_HEAL_PCT: p->Set(UFA(p, ModHealingPercent), Float(x)); break;
            case P_HEAL_DONE_PCT: p->Set(UFA(p, ModHealingDonePercent), Float(x)); break;
            case P_TARGET_RESIST: p->Set(UFA(p, ModTargetResistance), int32(x)); break;
            case P_FIELD_BYTES: p->Set(UFA(p, MultiActionBars), Octeto(x, 2)); break;
            case P_AMMO: p->Set(UFA(p, AmmoID), int32(x)); break;
            case P_KILLS: p->Set(UFA(p, TodayHonorableKills), uint16(x & 0xFFFF)); p->Set(UFA(p, YesterdayHonorableKills), uint16(x >> 16)); break;
            case P_LIFETIME_HK: p->Set(UFA(p, LifetimeHonorableKills), x); break;
            case P_WATCHED: p->Set(UFA(p, WatchedFactionIndex), int32(x)); break;
            case P_MAX_LEVEL: p->Set(UFA(p, MaxLevel), int32(x)); break;
            case 1277: case 1278:                        // PLAYER_FIELD_HONOR/ARENA_CURRENCY -> monedas 1901 (honor) y 1900 (arena)
            {
                WorldPackets::Misc::SetCurrency c;
                c.Type = i == 1277 ? 1901 : 1900; c.Quantity = int32(x); c.SuppressChatLog = true;
                Enviar(c.Write());
                break;
            }
            case P_GLYPHS_ENABLED: p->Set(UFA(p, GlyphsEnabled), uint8(x)); break;
            default: break;
        }
    }
#undef UFA

    void AplicarGo(Gate<GameObject>* go, std::vector<uint32> const& v, std::vector<uint16> const& cam)
    {
        using namespace M335;
        for (uint16 i : cam)
        {
            uint32 x = At(v, i);
            switch (i)
            {
                case OBJ_ENTRY: go->Set(UFO(go, EntryID), int32(x)); break;
                case OBJ_SCALE: go->Set(UFO(go, Scale), Float(x)); break;
                case GO_DISPLAYID:
                    go->Set(go->m_values.ModifyValue(&GameObject::m_gameObjectData).ModifyValue(&UF::GameObjectData::DisplayID), int32(x));
                    [[fallthrough]];                             // las banderas dependen del modelo (GO_FLAG_MAP_OBJECT)
                case GO_FLAGS:
                {
                    // desde la 7.0 el cliente decide WMO o M2 con GO_FLAG_MAP_OBJECT (0x100000), no por la extensión; AC 3.3.5 no la
                    // pone y el cliente carga el WMO de barcos y zepelines como M2 y se cierra (RVA 0x11A72A1, lee [0+0x10])
                    uint32 fl = At(v, GO_FLAGS);
                    if (EsWmo(At(v, GO_DISPLAYID))) fl |= 0x00100000;
                    go->Set(go->m_values.ModifyValue(&GameObject::m_gameObjectData).ModifyValue(&UF::GameObjectData::Flags), fl);
                    break;
                }
                case GO_PARENTROT: case GO_PARENTROT + 1: case GO_PARENTROT + 2: case GO_PARENTROT + 3:
                {
                    QuaternionData q(Float(At(v, GO_PARENTROT)), Float(At(v, GO_PARENTROT + 1)), Float(At(v, GO_PARENTROT + 2)), Float(At(v, GO_PARENTROT + 3)));
                    if (q.x == 0.f && q.y == 0.f && q.z == 0.f && q.w == 0.f) q.w = 1.f;
                    if (Octeto(At(v, GO_BYTES_1), 1) == 33)
                    {
                        uint32 dmd = ModeloDestructible(uint32(At(v, OBJ_ENTRY)));
                        float fx; std::memcpy(&fx, &dmd, sizeof(fx));
                        q = QuaternionData(fx, 0.f, 0.f, 1.f);
                    }
                    go->Set(go->m_values.ModifyValue(&GameObject::m_gameObjectData).ModifyValue(&UF::GameObjectData::ParentRotation), q);
                    break;
                }
                case GO_DYNAMIC:                             // GAMEOBJECT_DYNAMIC: u16 banderas dinámicas (bits distintos en 3.4.3) | u16 progreso de ruta
                {
                    uint32 lo = x & 0xFFFF, n = 0;
                    if (lo & 0x01) n |= 0x0004;              // ACTIVATE
                    if (lo & 0x02) n |= 0x0008;              // ANIMATE
                    if (lo & 0x04) n |= 0x0080;              // NO_INTERACT
                    if (lo & 0x08) n |= 0x0020;              // SPARKLE
                    if (lo & 0x10) n |= 0x0040;              // STOPPED
                    if (n & (0x0004 | 0x0020)) n |= 0x0200;  // HIGHLIGHT: como TrinityCore con los objetos de misión
                    // barcos y zepelines: la mitad alta es el progreso de ruta. AC la calcula con el tiempo ACUMULADO desde que
                    // arrancó el mapa (int16(PathProgress / Period * 65535): basura en cuanto pasa una vuelta) y la manda en cada
                    // actualización de valores; el cliente la relee y recolocaba el transporte en otro punto de su ruta (se
                    // quedaba pillado o aparecía al otro lado). Se conserva la que puso la pasarela al crearlo
                    bool transporte = Octeto(At(v, GO_BYTES_1), 1) == 15;
                    bool ascensor = Octeto(At(v, GO_BYTES_1), 1) == 11 && PausaAscensor(uint32(At(v, OBJ_ENTRY)));   // su progreso lo pone GO_BYTES_1
                    uint32 alta = (transporte || ascensor) ? (go->GetDynamicFlags() & 0xFFFF0000) : (x & 0xFFFF0000);
                    // sin STOPPED (0x40) ni otras: con allowstopping el cliente las lee. Salvo los que AC sí para (canBeStopped:
                    // cañoneras de ICC), que llevan STOPPED mientras AC los tiene en LISTO, como Transport::Update de TC
                    if (transporte)
                        n = (Parable(uint32(At(v, OBJ_ENTRY))) && Octeto(At(v, GO_BYTES_1), 0) == 1) ? 0x40u : 0u;
                    go->Set(go->m_values.ModifyValue(&Object::m_objectData).ModifyValue(&UF::ObjectData::DynamicFlags), alta | n);
                    break;
                }
                case GO_FACTION: go->Set(go->m_values.ModifyValue(&GameObject::m_gameObjectData).ModifyValue(&UF::GameObjectData::FactionTemplate), int32(x)); break;
                case GO_LEVEL:                               // en ascensores es otra cosa en 3.4.3 (hora de llegada): va con el estado
                    if (Octeto(At(v, GO_BYTES_1), 1) != 11)
                        go->Set(go->m_values.ModifyValue(&GameObject::m_gameObjectData).ModifyValue(&UF::GameObjectData::Level), int32(x));
                    break;
                case GO_BYTES_1:
                {
                    int8 estado = int8(Octeto(x, 0));
                    // ascensores (tipo 11): en 3.4.3 24 = en ciclo, 25 + N = ir a la parada N y quedarse (TC SetTransportState).
                    // AC (StaticTransport): con pausa, LISTO(1) = en la parada inicial y ACTIVO(0) = en la de la pausa; sin pausa, en ciclo.
                    if (Octeto(x, 1) == 11)
                    {
                        // TC GameObjectType::Transport::OnStateChanged: 24 = en ciclo, 25 + N = ir a la parada N (PauseTimes[N]) y
                        // quedarse; Level = hora del reloj de transportes a la que llega. AC (StaticTransport con pausa): ACTIVO (0)
                        // = quieto en la pausa (Susurramuerte antes de morir), LISTO (1) = en marcha. Antes iba 1->25 y 0->26 (parada
                        // que no existe) y sin PauseTimes, así que el cliente lo ciclaba siempre
                        uint32 ent = uint32(At(v, OBJ_ENTRY)), pausa = PausaAscensor(ent);
                        estado = (!pausa || estado == 1) ? int8(24) : int8(25);
                        if (!pausa)
                            go->Set(go->m_values.ModifyValue(&GameObject::m_gameObjectData).ModifyValue(&UF::GameObjectData::Level), int32(getMSTime()));
                        else
                        {
                            // como GameObjectType::Transport::OnStateChanged de TC: el progreso es el punto DESDE el que sale y
                            // Level la hora a la que llega (ahora + |desde - destino|). Con Level = ahora el cliente daba el viaje
                            // por hecho y no se movía (el de Susurramuerte al morir ella). Al crearlo ya está en su parada
                            uint32 periodo = PeriodoAscensor(ent), destino = estado == 25 ? pausa : 0u, ahora = getMSTime();
                            auto [itA, nuevo] = _ascensores.try_emplace(go->GetGUID(), destino);
                            uint32 desde = nuevo ? destino : itA->second;
                            if (!nuevo && desde == destino) break;   // mismo estado (actualización de otro byte): no reiniciar el viaje
                            itA->second = destino;
                            uint32 viaje = uint32(std::abs(int32(desde) - int32(destino)));
                            go->Set(go->m_values.ModifyValue(&GameObject::m_gameObjectData).ModifyValue(&UF::GameObjectData::Level), int32(ahora + viaje));
                            uint32 prog = periodo ? uint32(float(desde % periodo) / float(periodo) * 65535.0f) : 0u;
                            uint32 dyn = go->GetDynamicFlags() & 0xFFFF & ~0x100u;   // sin INVERTED_MOVEMENT (con una parada TC no la pone)
                            go->Set(go->m_values.ModifyValue(&Object::m_objectData).ModifyValue(&UF::ObjectData::DynamicFlags), dyn | (prog << 16));
                        }
                    }
                    // MO_TRANSPORT con allowstopping (ver TranslateGameObjectQuery): ACTIVO = en marcha; LISTO lo pararía en
                    // la siguiente pausa
                    if (Octeto(x, 1) == 15 && !Parable(uint32(At(v, OBJ_ENTRY)))) estado = 0;   // los parables llevan el de AC
                    go->Set(go->m_values.ModifyValue(&GameObject::m_gameObjectData).ModifyValue(&UF::GameObjectData::State), estado);
                }
                    go->Set(go->m_values.ModifyValue(&GameObject::m_gameObjectData).ModifyValue(&UF::GameObjectData::TypeID), int8(Octeto(x, 1)));
                    go->Set(go->m_values.ModifyValue(&GameObject::m_gameObjectData).ModifyValue(&UF::GameObjectData::ArtKit), uint32(Octeto(x, 2)));
                    go->Set(go->m_values.ModifyValue(&GameObject::m_gameObjectData).ModifyValue(&UF::GameObjectData::PercentHealth), uint8(255));
                    break;
                default: break;
            }
        }
    }
#undef UFO
#undef UFU

    static uint64 Guid(std::vector<uint32> const& v, uint16 i) { return uint64(At(v, i)) | (uint64(At(v, i + 1)) << 32); }

    bool Crear(uint64 g, Obj& o, Mov335 const& m, std::vector<uint16> const& cam, UpdateData& ud)
    {
        using namespace M335;
        if (g == yo335)                                  // el propio jugador ya existe en el cliente: solo valores
        {
            if (!yo) { _movYo = m; _hayMovYo = true; return false; }   // aún no creado: AplicarPendienteYo lo recoge
            if (_recrearYo)                              // tras SMSG_NEW_WORLD el cliente espera la creación completa
            {
                _recrearYo = false;
                o.tc = yo->GetGUID();
                AplicarUnidad(yo, o.v, cam);
                AplicarJugador(yo, o.v, cam, true);
                yo->Relocate(m.x, m.y, m.z, m.o);
                Velocidades(yo, m);                      // .mod speed, .gm fly, monturas: AC no los vuelve a mandar si no cambian
                if (m.trans)                             // llega a bordo (barco o zepelín que cambia de mapa)
                {
                    yo->m_movementInfo.transport.guid = ToTc(m.trans);
                    yo->m_movementInfo.transport.pos.Relocate(m.tpos[0], m.tpos[1], m.tpos[2], m.tpos[3]);
                    yo->m_movementInfo.transport.time = m.ttime;
                    yo->m_movementInfo.transport.seat = m.tseat;
                    // como TC: el transporte va ANTES que el jugador y en el mismo paquete, para que el cliente lo enganche al
                    // crearlo (0x120ca30). Si el jugador llegaba suelto caía y, al llegar luego el zepelín (más adelante),
                    // el teletransporte a cubierta daba un salto hacia atrás. Se coloca además en el punto del zepelín del
                    // cliente más su desplazamiento, no en el que calculó AC
                    if (EsperandoMover(m.trans))
                    {
                        _transportesEsperando.erase(std::find(_transportesEsperando.begin(), _transportesEsperando.end(), m.trans));
                        uint32 np = 0; Position tp; bool hayPos = false;
                        tp.Relocate(0.f, 0.f, 0.f, 0.f);
                        if (SoltarUno(m.trans, ud, np, &tp))
                        {
                            hayPos = tp.GetPositionX() != 0.f || tp.GetPositionY() != 0.f;
                            _reenviarAlFijar.push_back(m.trans);
                        }
                        if (hayPos)
                        {
                            float c = std::cos(tp.GetOrientation()), sn = std::sin(tp.GetOrientation());
                            float lx = m.tpos[0], ly = m.tpos[1];
                            yo->Relocate(tp.GetPositionX() + lx * c - ly * sn, tp.GetPositionY() + ly * c + lx * sn,
                                         tp.GetPositionZ() + m.tpos[2], Position::NormalizeOrientation(tp.GetOrientation() + m.tpos[3]));
                        }
                        Log("[%s] recreado a bordo: transporte y %u pasajeros antes que el jugador", acct.c_str(), np);
                    }
                    else if (!TransporteEnCliente(m.trans))
                        _colocarABordo = true;               // aún no llegó su transporte: ColocarABordo cuando llegue
                }
                else
                    yo->m_movementInfo.transport.Reset();
                WorldPackets::Movement::MoveSetActiveMover am; am.MoverGUID = yo->GetGUID();
                Enviar(am.Write());
                yo->BuildCreateUpdateBlockForPlayer(&ud, yo);
                static_cast<Object*>(yo)->ClearUpdateMask(false);
                Log("[%s] jugador recreado en el mapa %u", acct.c_str(), mapa);
                recreadoYo = true;                       // Main reenvía la barra de acción tras mandar este paquete
                return true;
            }
            o.tc = yo->GetGUID();
            Log("[%s] propio 3.3.5: flags 0x%X flags2 0x%X bytes1 0x%X bytes2 0x%X dyn 0x%X pflags 0x%X", acct.c_str(),
                At(o.v, U_FLAGS), At(o.v, U_FLAGS_2), At(o.v, U_BYTES_1), At(o.v, U_BYTES_2), At(o.v, U_DYNFLAGS), At(o.v, P_FLAGS));
            AplicarUnidad(yo, o.v, cam);
            AplicarJugador(yo, o.v, cam, true);
            yo->BuildValuesUpdateBlockForPlayer(&ud, yo);
            static_cast<Object*>(yo)->ClearUpdateMask(false);   // en Player es protegido; en Object, público
            return true;
        }
        if (o.o)                                         // ya lo tenemos: tratarlo como valores
            return Valores(g, o, cam, ud);
        if (!yo) { _movPend[g] = m; return false; }      // CrearPendientes lo crea tras el jugador (AC manda antes los transportes)

        o.tc = ToTc(g);
        _inv[o.tc] = g;
        uint32 entry = At(o.v, OBJ_ENTRY);
        switch (o.tipo)
        {
            case T_UNIT:
            {
                auto c = std::make_unique<Gate<Creature>>(false);
                Colocar(c.get(), o.tc, m);
                c->Plantilla(PlantillaDe(entry));
                c->Set(c->m_values.ModifyValue(&Unit::m_unitData).ModifyValue(&UF::UnitData::StateAnimID), sDB2Manager.GetEmptyAnimStateID());   // Creature::Create de TC
                RitmosTc(c.get());
                Velocidades(c.get(), m);
                if (m.trans && !TransporteEnCliente(m.trans)) { _pasajerosPend[m.trans].push_back(g); _pasajerosRetenidos.insert(g); }
                if (m.trans)
                    Log("[%s] pasajero %u en %s (%.1f, %.1f, %.1f)%s", acct.c_str(), entry, ToTc(m.trans).ToString().c_str(), m.tpos[0], m.tpos[1], m.tpos[2],
                        _pasajerosRetenidos.count(g) ? " retenido hasta que llegue el transporte" : "");
                if (m.vehiculo && sVehicleStore.HasRecord(m.vehiculo)) c->Vehiculo(m.vehiculo, entry);
                AplicarUnidad(c.get(), o.v, cam);
                AjustarTier(c.get(), o.v);               // con bytes1 = 0 el campo no viene en la máscara de creación
                o.w = c.get();
                o.o = std::move(c);
                break;
            }
            case T_PLAYER:
            {
                auto p = std::make_unique<Gate<Player>>(sesionOtros);
                Colocar(p.get(), o.tc, m);
                p->Set(p->m_values.ModifyValue(&Unit::m_unitData).ModifyValue(&UF::UnitData::StateAnimID), sDB2Manager.GetEmptyAnimStateID());   // como Player de TC
                RitmosTc(p.get());
                p->SetVirtualPlayerRealm((Cfg::Region << 24) | (Cfg::Battlegroup << 16) | Cfg::RealmId);   // como CharacterHandler de TC
                Velocidades(p.get(), m);
                if (m.trans && !TransporteEnCliente(m.trans)) { _pasajerosPend[m.trans].push_back(g); _pasajerosRetenidos.insert(g); }
                AplicarUnidad(p.get(), o.v, cam);
                AplicarJugador(p.get(), o.v, cam, false);
                o.w = p.get();
                o.o = std::move(p);
                break;
            }
            case T_GAMEOBJECT:
            {
                // barcos y zepelines (MO_TRANSPORT): el cliente 3.4.3 casca (lectura en 0x10) al montarlos, igual que con
                // el TrinityCore 3.4.3 de referencia; se dejan fuera hasta traducir los transportes
                bool transporte = Octeto(At(o.v, GO_BYTES_1), 1) == 15;
                if (transporte && !TransporteValido(entry))
                {
                    if (_transportesAvisados.insert(entry).second)
                        Log("[%s] transporte %u sin ruta o mapa en el cliente 3.4.3: no se envía", acct.c_str(), entry);
                    return false;
                }
                auto go = std::make_unique<Gate<GameObject>>();
                go->Init(o.tc);
                go->m_mapId = mapa;
                go->Relocate(m.x, m.y, m.z, m.o);
                go->RelocateStationaryPosition(m.x, m.y, m.z, m.o);
                go->Rotacion(m.rot);
                // pasajero (cofre de las cañoneras de ICC, AC MotionTransport::CreateGOPassenger): como Transport::CreateGOPassenger de
                // TC, transporte + posición relativa en m_movementInfo; BuildCreateUpdateBlockForPlayer manda entonces MovementTransport
                // además de Stationary y el cliente lo arrastra con la nave. Sin esto se creaba fijo en su sitio de mundo.
                if (m.trans && !transporte)
                {
                    auto tr = _objs.find(m.trans);
                    uint32 te = tr != _objs.end() ? At(tr->second.v, OBJ_ENTRY) : 0;
                    if (!te || TransporteValido(te))             // nave descartada (sin ruta en 3.4.3): se queda suelto, como hasta ahora
                    {
                        go->m_movementInfo.transport.guid = ToTc(m.trans);
                        go->m_movementInfo.transport.pos.Relocate(m.tpos[0], m.tpos[1], m.tpos[2], m.rot ? GiroEmpaquetado(m.rot) : 0.f);
                        go->m_movementInfo.transport.seat = -1;
                        if (!TransporteEnCliente(m.trans)) { _pasajerosPend[m.trans].push_back(g); _pasajerosRetenidos.insert(g); }
                        Log("[%s] objeto pasajero %u en %s (%.1f, %.1f, %.1f)%s", acct.c_str(), entry, ToTc(m.trans).ToString().c_str(),
                            m.tpos[0], m.tpos[1], m.tpos[2], _pasajerosRetenidos.count(g) ? " retenido hasta que llegue el transporte" : "");
                    }
                }
                go->PlantillaGo(PlantillaGoDe(entry, Octeto(At(o.v, GO_BYTES_1), 1)));
                if (Octeto(At(o.v, GO_BYTES_1), 1) == 11)        // ascensor: sus paradas van en el bloque de creación (PauseTimes)
                    if (uint32 pausa = PausaAscensor(entry)) WorldgatePausas()[go.get()] = { pausa };
                // como GameObject::Create de TrinityCore: sin el estado de animación vacío el cliente no anima el objeto
                // (ni las puertas al abrirse, que es lo que mueve su colisión)
                go->Set(go->m_values.ModifyValue(&GameObject::m_gameObjectData).ModifyValue(&UF::GameObjectData::SpawnTrackingStateAnimID), sDB2Manager.GetEmptyAnimStateID());
                AplicarGo(go.get(), o.v, cam);
                if (transporte)                          // como Transport::Create de TrinityCore
                {
                    if (_esperaMover) _transportesEsperando.push_back(g);   // se manda en SoltarTransportes
                    else _transportesNuevos.push_back(g);
                    go->Transporte();
                    uint32 periodo = std::max<uint32>(At(o.v, GO_LEVEL), 1);
                    uint32 instante = m.tiempoTransporte % periodo;
                    uint32 periodoCli = periodo, instanteCli = instante;
                    // El reloj de AC recorre la vuelta entera (todos los mapas) con su reparto de tiempos; el cliente la recorre con el
                    // generador de TrinityCore (tramos por mapa, paradas), que reparte distinto alrededor de los saltos de mapa y de
                    // las paradas largas. Pasar la fracción tal cual dejaba el transporte parado al final del tramo anterior y luego
                    // saltaba. Se busca el instante de la ruta del cliente en este mapa y en esta posición (en paradas, el más cercano
                    // al punto de la vuelta de AC).
                    auto rt = _rutaTc.find(entry);
                    double mejorDist = 1e30; bool cerca = false;
                    auto corr = _corrTc.find(entry);
                    if (corr != _corrTc.end())       // por nodos (RutasTransporte.h)
                    {
                        periodoCli = corr->second.periodoCli;
                        instanteCli = corr->second.Convertir(instante);
                        cerca = true;
                    }
                    else if (rt != _rutaTc.end() && rt->second->TotalPathTime)
                    {
                        TransportTemplate const& tt = *rt->second;
                        periodoCli = tt.TotalPathTime;
                        double ref = double(instante) * periodoCli / periodo;
                        double mejorDif = 1e30; instanteCli = uint32(ref) % periodoCli;
                        for (uint32 s = 0; s < periodoCli; s += 100)
                        {
                            size_t leg = 0; TransportMovementState st = TransportMovementState::Moving;
                            Optional<Position> pos = tt.ComputePosition(s, &st, &leg);
                            if (!pos || leg >= tt.PathLegs.size() || tt.PathLegs[leg].MapId != mapa) continue;
                            double d = pos->GetExactDist(m.x, m.y, m.z);
                            double dif = std::fabs(double(s) - ref); dif = std::min(dif, double(periodoCli) - dif);
                            if (d < 3.0) { if (!cerca || dif < mejorDif) { cerca = true; mejorDif = dif; instanteCli = s; } }
                            else if (!cerca && d < mejorDist) { mejorDist = d; instanteCli = s; }
                        }
                    }
                    Log("[%s] transporte %u: vuelta AC %u ms (instante %u), cliente %u ms (instante %u)%s", acct.c_str(), entry, periodo, instante,
                        periodoCli, instanteCli, cerca ? "" : " SIN punto cercano en su ruta");
                    {
                        RelojTransporte& rj = _relojes[g];
                        rj.entry = entry; rj.periodoAc = periodo; rj.acBase = instante; rj.cliBase = instanteCli;
                        rj.t0 = rj.ultimo = std::chrono::steady_clock::now();
                    }
                    float progreso = float(instanteCli) / float(periodoCli);
                    go->Set(go->m_values.ModifyValue(&Object::m_objectData).ModifyValue(&UF::ObjectData::DynamicFlags), uint32(progreso * 65535.0f) << 16);
                    go->Set(go->m_values.ModifyValue(&GameObject::m_gameObjectData).ModifyValue(&UF::GameObjectData::Level), int32(periodoCli));
                    GameTime::UpdateGameTimers();
                }
                o.w = go.get();
                o.o = std::move(go);
                break;
            }
            case T_CORPSE:
            {
                auto c = std::make_unique<Gate<Corpse>>(CORPSE_RESURRECTABLE_PVE);
                c->Init(o.tc);
                c->m_mapId = mapa;
                c->Relocate(m.x, m.y, m.z, m.o);
                AplicarCadaver(c.get(), o.v, cam);
                o.w = c.get();
                o.o = std::move(c);
                break;
            }
            case T_DYNOBJECT:
            {
                auto d = std::make_unique<Gate<DynamicObject>>(false);
                d->Init(o.tc);
                d->m_mapId = mapa;
                d->Relocate(m.x, m.y, m.z, m.o);
                AplicarDinamico(d.get(), o.v, cam);
                o.w = d.get();
                o.o = std::move(d);
                break;
            }
            case T_ITEM:
            {
                auto it = std::make_unique<Gate<Item>>();
                it->Init(o.tc);
                AplicarItem(it.get(), o.v, cam, false);
                o.o = std::move(it);
                break;
            }
            case T_CONTAINER:
            {
                auto bg = std::make_unique<Gate<Bag>>();
                bg->Init(o.tc);
                AplicarItem(bg.get(), o.v, cam, true);
                o.o = std::move(bg);
                break;
            }
            default:
                if (_tiposAvisados.insert(o.tipo).second)
                    Log("[%s] UPDATE_OBJECT: objetos de tipo %u todavía sin traducir", acct.c_str(), o.tipo);
                return false;
        }
        if (_pasajerosRetenidos.count(g)) return false;  // se crea cuando llegue su transporte
        if (EsperandoMover(g)) return false;             // transporte retenido hasta el INIT_ACTIVE_MOVER
        o.o->BuildCreateUpdateBlockForPlayer(&ud, yo);
        o.o->ClearUpdateMask(false);
        return true;
    }

    bool Valores(uint64 g, Obj& o, std::vector<uint16> const& cam, UpdateData& ud)
    {
        using namespace M335;
        if (g == yo335)
        {
            if (!yo) return false;
            AplicarUnidad(yo, o.v, cam);
            AplicarJugador(yo, o.v, cam, true);
            if (_recrearYo) return false;                // entre SMSG_NEW_WORLD y la recreación el cliente no tiene al jugador: va en la creación
            yo->BuildValuesUpdateBlockForPlayer(&ud, yo);
            static_cast<Object*>(yo)->ClearUpdateMask(false);   // en Player es protegido; en Object, público
            return true;
        }
        if (!o.o) return false;
        bool retenido = _pasajerosRetenidos.count(g) != 0 || EsperandoMover(g);   // el cliente aún no lo tiene: se guardan los valores, no se mandan
        switch (o.tipo)
        {
            case T_UNIT: AplicarUnidad(static_cast<Gate<Creature>*>(o.o.get()), o.v, cam); break;
            case T_PLAYER:
            {
                auto* p = static_cast<Gate<Player>*>(o.o.get());
                AplicarUnidad(p, o.v, cam);
                AplicarJugador(p, o.v, cam, false);
                break;
            }
            case T_GAMEOBJECT: AplicarGo(static_cast<Gate<GameObject>*>(o.o.get()), o.v, cam); break;
            case T_ITEM: AplicarItem(static_cast<Gate<Item>*>(o.o.get()), o.v, cam, false); break;
            case T_CORPSE: AplicarCadaver(static_cast<Gate<Corpse>*>(o.o.get()), o.v, cam); break;
            case T_DYNOBJECT: AplicarDinamico(static_cast<Gate<DynamicObject>*>(o.o.get()), o.v, cam); break;
            case T_CONTAINER: AplicarItem(static_cast<Gate<Bag>*>(o.o.get()), o.v, cam, true); break;
            default: return false;
        }
        if (retenido) return false;
        o.o->BuildValuesUpdateBlockForPlayer(&ud, yo);
        o.o->ClearUpdateMask(false);
        return true;
    }

    bool Quitar(uint64 g, UpdateData& ud, bool destruir)
    {
        if (g == yo335) return false;
        auto it = _objs.find(g);
        if (it == _objs.end()) return false;
        bool visible = it->second.o != nullptr && !_pasajerosRetenidos.erase(g) && !EsperandoMover(g);
        if (EsperandoMover(g)) _transportesEsperando.erase(std::find(_transportesEsperando.begin(), _transportesEsperando.end(), g));
        _inv.erase(it->second.tc);
        if (visible)
        {
            if (destruir) ud.AddDestroyObject(it->second.tc);
            else ud.AddOutOfRangeGUID(it->second.tc);
        }
        _objs.erase(it);
        _relojes.erase(g);
        return visible;
    }
};
