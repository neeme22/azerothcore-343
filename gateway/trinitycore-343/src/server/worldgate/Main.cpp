/*
 * worldgate — pasarela cliente 3.4.3.54261 <-> worldserver de AzerothCore (protocolo 3.3.5a, sin tocar).
 *
 *   cliente 3.4.3 --(AES-GCM, paquetes con bits, GUID 128)--> worldgate --(ARC4 de cabeceras, 3.3.5)--> worldserver AC
 *
 * Reutiliza de TrinityCore 3.4.3 (biblioteca `game`): WorldPacket/ByteBuffer con bits, WorldPacketCrypt, Ed25519
 * (EnterEncryptedMode), y las clases WorldPackets::* para escribir lo que va al cliente.
 * H1.2: handshake 3.4.3 + pantalla de personajes (lista traducida desde SMSG_CHAR_ENUM 3.3.5).
 * Direcciones, puertos, bases de datos y carpeta de DB2 en worldgate.conf (ver worldgate.conf.dist).
 */

#define _ALLOW_KEYWORD_MACROS
#define final   // solo para heredar de Player en la pasarela (no cambia la forma de la clase)
#include "Player.h"
#include "Corpse.h"
#include "DynamicObject.h"
#include "Spell.h"
#undef final
#include "AuthenticationPackets.h"
#include "BattlenetPackets.h"
#include "CharacterPackets.h"
#include "QueryPackets.h"
#include "WorldStatePackets.h"
#include "WowTime.h"
#include "ClientConfigPackets.h"
#include "HotfixPackets.h"
#include "MiscPackets.h"
#include "SystemPackets.h"
#include "ObjectMgr.h"
#include "ARC4.h"
#include "CryptoHash.h"
#include "CryptoRandom.h"
#include "HMAC.h"
#include "SessionKeyGenerator.h"
#include "WorldPacketCrypt.h"
#include "WorldPacket.h"
#include "Opcodes.h"
#include "WorldSession.h"
#include "DB2Stores.h"
#include "DatabaseEnv.h"
#include "MySQLThreading.h"
#include "AccountMgr.h"
#include "UpdateData.h"
#include "MovementPackets.h"
#include "Random.h"
#include "UpdateFields.h"
#include "OpenSSLCrypto.h"
#include <boost/dll/runtime_symbol_info.hpp>
#include <boost/asio.hpp>
#include <mysql.h>
#include <zlib.h>
#include <atomic>
#include <fstream>
#include <map>
#include <sstream>
#include <functional>
#include <set>
#include <unordered_map>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using boost::asio::ip::tcp;

// ------------------------------------------------------------------------------------------------ configuración
namespace Cfg
{
    std::string ListenIP = "127.0.0.1";   uint16 ListenPort = 8086;   // donde conecta el cliente 3.4.3 (realmlist id 2)
    std::string WorldIP = "127.0.0.1";    uint16 WorldPort = 8085;    // worldserver de AzerothCore (3.3.5)
    uint16 InstancePort = 8087;                                        // segunda conexión del cliente (SMSG_CONNECT_TO)
    std::string DbHost = "127.0.0.1";     uint16 DbPort = 3306;       // MySQL de AzerothCore
    std::string DbUser = "root", DbPass = "", DbName = "acore_auth";
    std::string CharDbName = "acore_characters";
    std::string WorldDbName = "acore_world";          // se pueden cambiar con worldgate_bd.txt (auth=, personajes=, mundo=, hotfixes=)
    std::string HotfixDbName = "tc343_hotfixes";
    std::string TcAuthDbName = "tc343_auth";          // permisos (RBAC) de TrinityCore que consultan su Player/WorldSession
    std::string DataDir = "data343/";   // DB2 del cliente 3.4.3 (mapextractor)
    std::string Locale;                                // idioma de los DB2 (enUS, esES...); vacío = enUS si está, si no el primero de DataDir/dbc
    LocaleConstant Db2Locale = LOCALE_enUS;            // el que se usa de verdad (se fija al cargar los DB2)
    uint32 RealmId = 2, Region = 1, Battlegroup = 1;
    std::string RealmName = "AzerothCore 3.4.3";
    std::string WorldserverConf = "worldserver.conf";   // worldserver.conf de AzerothCore (ritmos, etc.)
    int32 const PrimerHotfixPropio = 900000000;             // hotfix_data.Id desde aquí: filas propias que se anuncian al cliente
    // semilla de autenticación del build 54261 (tabla build_info.win64AuthSeed)
    uint8 const Win64AuthSeed[16] = { 0x25,0xFD,0x81,0x24,0x75,0xDC,0xF2,0x6F,0x9F,0x13,0x83,0xAE,0xD3,0x7F,0xC9,0x9E };
}

// semillas fijas del protocolo 3.4.3 (TrinityCore WorldSocket.cpp)
static uint8 const AuthCheckSeed[16]     = { 0xC5,0xC6,0x98,0x95,0x76,0x3F,0x1D,0xCD,0xB6,0xA1,0x37,0x28,0xB3,0x12,0xFF,0x8A };
static uint8 const SessionKeySeed[16]    = { 0x58,0xCB,0xCF,0x40,0xFE,0x2E,0xCE,0xA6,0x5A,0x90,0xB8,0x01,0x68,0x6C,0x28,0x0B };
static uint8 const ContinuedSessionSeed[16] = { 0x16,0xAD,0x0C,0xD4,0x46,0xF9,0x4F,0xB2,0xEF,0x7D,0xEA,0x2A,0x17,0x66,0x4D,0x2F };
static uint8 const EncryptionKeySeed[16] = { 0xE9,0x75,0x3C,0x50,0x90,0x93,0x61,0xDA,0x3B,0x07,0xEE,0xFA,0xFF,0x9D,0x41,0xB8 };
static std::string const ServerInit = "WORLD OF WARCRAFT CONNECTION - SERVER TO CLIENT - V2";
static std::string const ClientInit = "WORLD OF WARCRAFT CONNECTION - CLIENT TO SERVER - V2";

// opcodes 3.3.5a (AzerothCore)
enum Op335 : uint16
{
    SMSG335_AUTH_CHALLENGE = 0x1EC, CMSG335_AUTH_SESSION = 0x1ED, SMSG335_AUTH_RESPONSE = 0x1EE,
    CMSG335_CHAR_ENUM = 0x037, SMSG335_CHAR_ENUM = 0x03B, CMSG335_PLAYER_LOGIN = 0x03D, CMSG335_CHAR_CREATE = 0x036, SMSG335_CHAR_CREATE = 0x03A, CMSG335_CHAR_DELETE = 0x038, SMSG335_CHAR_DELETE = 0x03C, CMSG335_PING = 0x1DC, SMSG335_PONG = 0x1DD,
};

// opcodes 3.3.5 del worldserver que no se traducen (depuración): worldgate_off.txt junto al exe, uno por línea en hex
static std::set<uint16> g_apagados;
static bool g_traza = false;                          // worldgate_traza.txt: anota cada paquete (depurar cierres del cliente)
static std::mutex g_logMutex;
template <typename... Args>
static void Log(char const* fmt, Args... args)
{
    std::lock_guard<std::mutex> g(g_logMutex);
    std::time_t t = std::time(nullptr); char ts[16]; std::strftime(ts, sizeof(ts), "%H:%M:%S", std::localtime(&t));
    std::printf("%s ", ts); std::printf(fmt, args...); std::printf("\n"); std::fflush(stdout);
    if (FILE* lf = std::fopen("worldgate.log", "a")) { std::fprintf(lf, "%s ", ts); std::fprintf(lf, fmt, args...); std::fprintf(lf, "\n"); std::fclose(lf); }
}

// ------------------------------------------------------------------------------------------------ base de datos (copia)
struct Db
{
    MYSQL* h = nullptr;
    bool Open()
    {
        h = mysql_init(nullptr);
        return mysql_real_connect(h, Cfg::DbHost.c_str(), Cfg::DbUser.c_str(), Cfg::DbPass.c_str(), Cfg::DbName.c_str(), Cfg::DbPort, nullptr, 0) != nullptr;
    }
    ~Db() { if (h) mysql_close(h); }
    std::string Esc(std::string const& s)
    {
        std::string o(s.size() * 2 + 1, '\0');
        o.resize(mysql_real_escape_string(h, o.data(), s.data(), (unsigned long)s.size()));
        return o;
    }
    // id, username, session_key_bnet
    bool GetAccount(std::string const& user, uint32& id, std::string& name, std::vector<uint8>& keyBnet)
    {
        std::string q = "SELECT id, username, session_key_bnet FROM account WHERE username = '" + Esc(user) + "'";
        if (mysql_query(h, q.c_str())) return false;
        MYSQL_RES* r = mysql_store_result(h); if (!r) return false;
        MYSQL_ROW row = mysql_fetch_row(r); unsigned long* len = mysql_fetch_lengths(r);
        bool ok = row && row[2];
        if (ok) { id = std::stoul(row[0]); name = row[1]; keyBnet.assign((uint8*)row[2], (uint8*)row[2] + len[2]); }
        mysql_free_result(r);
        return ok;
    }
    // datos del personaje en la copia de acore_characters
    struct Personaje { uint8 race = 0, cls = 0, gender = 0, level = 1, skin = 0, face = 0, hair = 0, hairColor = 0, facial = 0; uint32 health = 1, power = 0; std::string name; uint8 modos = 0; };
    bool GetCharacter(uint64 guid, Personaje& c)
    {
        std::string q = "SELECT race, class, gender, level, skin, face, hairStyle, hairColor, facialStyle, health, power1, name, instance_mode_mask FROM "
                        + Cfg::CharDbName + ".characters WHERE guid = " + std::to_string(guid);
        if (mysql_query(h, q.c_str())) return false;
        MYSQL_RES* r = mysql_store_result(h); if (!r) return false;
        MYSQL_ROW row = mysql_fetch_row(r);
        bool ok = row != nullptr;
        if (ok)
        {
            auto u = [&](int i) { return row[i] ? uint32(std::stoul(row[i])) : 0u; };
            c.race = uint8(u(0)); c.cls = uint8(u(1)); c.gender = uint8(u(2)); c.level = uint8(u(3));
            c.skin = uint8(u(4)); c.face = uint8(u(5)); c.hair = uint8(u(6)); c.hairColor = uint8(u(7)); c.facial = uint8(u(8));
            c.health = u(9); c.power = u(10); c.name = row[11] ? row[11] : ""; c.modos = uint8(u(12));
        }
        mysql_free_result(r);
        return ok;
    }
    bool GetAccountId(std::string const& user, uint32& id, std::string& name)
    {
        std::string q = "SELECT id, username FROM account WHERE username = '" + Esc(user) + "'";
        if (mysql_query(h, q.c_str())) return false;
        MYSQL_RES* r = mysql_store_result(h); if (!r) return false;
        MYSQL_ROW row = mysql_fetch_row(r);
        bool ok = row != nullptr;
        if (ok) { id = std::stoul(row[0]); name = row[1]; }
        mysql_free_result(r);
        return ok;
    }
    bool SetSessionKey335(uint32 id, uint8 const* k, size_t n)
    {
        std::string hex; static char const* H = "0123456789ABCDEF";
        for (size_t i = 0; i < n; ++i) { hex += H[k[i] >> 4]; hex += H[k[i] & 15]; }
        std::string q = "UPDATE account SET session_key = 0x" + hex + " WHERE id = " + std::to_string(id);
        return mysql_query(h, q.c_str()) == 0;
    }
};

// ------------------------------------------------------------------------------------------------ utilidades 3.3.5
struct Rd   // lector little-endian simple
{
    std::vector<uint8> const& b; size_t p = 0;
    explicit Rd(std::vector<uint8> const& v) : b(v) { }
    template <typename T> T get() { T v{}; if (p + sizeof(T) <= b.size()) std::memcpy(&v, &b[p], sizeof(T)); p += sizeof(T); return v; }
    std::string cstr() { std::string s; while (p < b.size() && b[p]) s += char(b[p++]); ++p; return s; }
};
struct Wr
{
    std::vector<uint8> b;
    template <typename T> Wr& put(T v) { uint8 t[sizeof(T)]; std::memcpy(t, &v, sizeof(T)); b.insert(b.end(), t, t + sizeof(T)); return *this; }
    Wr& raw(void const* d, size_t n) { b.insert(b.end(), (uint8 const*)d, (uint8 const*)d + n); return *this; }
    Wr& cstr(std::string const& s) { raw(s.data(), s.size()); b.push_back(0); return *this; }
};


// ------------------------------------------------------------------------------------------------ apariencia clásica <-> ChrCustomization
// ChrCustomizationConversion (build 54261): raza, sexo, casilla clásica (1 piel, 2 cara, 3 peinado, 4 color de pelo,
// 5 vello facial), valor clásico -> elección; opcionalmente depende de otra casilla. La elección dice su opción.
namespace Apariencia
{
    struct Conv { uint8 race, sex, slot; uint32 data, choice; uint8 depSlot; uint32 depData; };
    static std::vector<Conv> g_conv;
    static std::unordered_map<uint32, uint32> g_choiceOption;           // elección -> opción

    static std::vector<std::string> SplitCsv(std::string const& l)
    {
        std::vector<std::string> o; std::string cur; bool q = false;
        for (char c : l) { if (c == '"') q = !q; else if (c == ',' && !q) { o.push_back(cur); cur.clear(); } else if (c != '\r') cur += c; }
        o.push_back(cur); return o;
    }
    static bool Load()
    {
        std::ifstream f("ChrCustomizationConversion.csv"); std::string l; std::getline(f, l);
        while (std::getline(f, l))
        {
            auto c = SplitCsv(l); if (c.size() < 8) continue;
            g_conv.push_back({ uint8(std::stoul(c[1])), uint8(std::stoul(c[2])), uint8(std::stoul(c[3])), uint32(std::stoul(c[4])),
                               uint32(std::stoul(c[5])), uint8(std::stoul(c[6])), uint32(std::stoul(c[7])) });
        }
        std::ifstream g("ChrCustomizationChoice.csv"); std::getline(g, l);
        while (std::getline(g, l)) { auto c = SplitCsv(l); if (c.size() > 2) g_choiceOption[uint32(std::stoul(c[1]))] = uint32(std::stoul(c[2])); }
        return !g_conv.empty() && !g_choiceOption.empty();
    }
    // clásico -> 3.4.3 (valores: piel, cara, peinado, color, vello)
    static std::vector<UF::ChrCustomizationChoice> ToCustom(uint8 race, uint8 sex, uint8 const v[5])
    {
        std::vector<UF::ChrCustomizationChoice> out;
        for (uint8 slot = 1; slot <= 5; ++slot)
        {
            Conv const* best = nullptr;
            for (Conv const& c : g_conv)
                if (c.race == race && c.sex == sex && c.slot == slot && c.data == v[slot - 1])
                    if (!best || (c.depSlot && c.depSlot <= 5 && v[c.depSlot - 1] == c.depData)) best = &c;
            if (!best) continue;
            UF::ChrCustomizationChoice ch; ch.ChrCustomizationChoiceID = best->choice; ch.ChrCustomizationOptionID = g_choiceOption[best->choice];
            out.push_back(ch);
        }
        return out;
    }
    // 3.4.3 -> clásico
    static void ToClassic(uint8 race, uint8 sex, std::vector<uint32> const& choices, uint8 v[5])
    {
        std::memset(v, 0, 5);
        for (uint32 ch : choices)
            for (Conv const& c : g_conv)
                if (c.race == race && c.sex == sex && c.choice == ch && c.slot >= 1 && c.slot <= 5) { v[c.slot - 1] = uint8(c.data); break; }
    }
}

// códigos de respuesta 3.3.5 -> 3.4.3 (crear / borrar / nombre)
static uint8 CodigoCrear(uint8 c)
{
    if (c >= 0x2E && c <= 0x3A) return uint8(c - 0x2E + 23);
    switch (c) { case 0x3B: return 56; case 0x3C: return 55; case 0x3D: return 36; case 0x3E: return 37; case 0x3F: return 38; case 0x40: return 39; default: break; }
    if (c >= 0x41 && c <= 0x45) return uint8(c - 0x41 + 41);
    if (c >= 0x46 && c <= 0x4B) return uint8(c - 0x46 + 62);
    if (c >= 0x57 && c <= 0x67) return uint8(c - 0x57 + 90);
    return 25;                                                             // CHAR_CREATE_ERROR
}

// ------------------------------------------------------------------------------------------------ sesión
// objetos de TrinityCore manejados por la pasarela (Gate<>) y traducción de SMSG_UPDATE_OBJECT (H3)
#include "Mundo.h"
#include "Juego.h"
using GatePlayer = Gate<Player>;

// valor entero del worldserver.conf del AzerothCore del proyecto (server\configs, junto a la carpeta ref)
static int32 ConfigAC(std::string const& clave, int32 defecto)
{
    static std::map<std::string, std::string> const conf = []
    {
        std::map<std::string, std::string> m;
        for (std::string const& ruta : { Cfg::WorldserverConf, std::string("worldserver.conf") })
        {
            std::ifstream f(ruta); std::string l;
            while (std::getline(f, l))
            {
                size_t i = l.find('=');
                if (l.empty() || l[0] == '#' || i == std::string::npos) continue;
                auto recorta = [](std::string x) { size_t a = x.find_first_not_of(" \t\r\""), b = x.find_last_not_of(" \t\r\""); return a == std::string::npos ? std::string() : x.substr(a, b - a + 1); };
                m[recorta(l.substr(0, i))] = recorta(l.substr(i + 1));
            }
            if (!m.empty()) break;
        }
        return m;
    }();
    auto it = conf.find(clave);
    if (it == conf.end()) return defecto;
    try { return int32(std::stol(it->second)); } catch (...) { return defecto; }
}

class Session;
static std::mutex g_regMutex;
static std::unordered_map<uint64, std::weak_ptr<Session>> g_pendientes;   // clave ConnectTo -> sesión

class Session : public std::enable_shared_from_this<Session>
{
public:
    Session(tcp::socket&& c, boost::asio::io_context& io) : _cli(std::move(c)), _srv(io) { }

    void Run()
    {
        try
        {
            ClientHandshake();
            ClientLoop();
        }
        catch (std::exception const& e) { Log("[%s] fin de sesión: %s", _acct.c_str(), e.what()); }
        boost::system::error_code ec; _cli.close(ec); _srv.close(ec);
        _stop = true;
        if (_srvThread.joinable()) _srvThread.join();
    }

private:
    // ---------------------------------------------------------------- cliente 3.4.3: E/S (dos conexiones)
    static void RecvOn(tcp::socket& s, WorldPacketCrypt& crypt, uint16& opcode, std::vector<uint8>& body)
    {
        uint8 hdr[18];                                   // u32 size, u8 tag[12], u16 opcode (cifrado)
        boost::asio::read(s, boost::asio::buffer(hdr, sizeof(hdr)));
        uint32 size; std::memcpy(&size, hdr, 4);
        if (size < 2 || size > 0x100000) throw std::runtime_error("cabecera de cliente inválida");
        std::vector<uint8> data(size);
        std::memcpy(data.data(), hdr + 16, 2);
        if (size > 2) boost::asio::read(s, boost::asio::buffer(data.data() + 2, size - 2));
        Trinity::Crypto::AES::Tag tag; std::memcpy(tag, hdr + 4, 12);
        if (!crypt.DecryptRecv(data.data(), data.size(), tag)) throw std::runtime_error("fallo al descifrar paquete del cliente");
        std::memcpy(&opcode, data.data(), 2);
        body.assign(data.begin() + 2, data.end());
    }

    static void SendOn(tcp::socket& s, WorldPacketCrypt& crypt, std::mutex& m, WorldPacket const* pkt)
    {
        std::lock_guard<std::mutex> g(m);
        uint16 opcode = uint16(pkt->GetOpcode());
        std::vector<uint8> out(16 + 2 + pkt->size());
        std::memcpy(out.data() + 16, &opcode, 2);
        if (pkt->size()) std::memcpy(out.data() + 18, pkt->contents(), pkt->size());
        uint32 size = uint32(2 + pkt->size());
        Trinity::Crypto::AES::Tag tag;
        if (!crypt.EncryptSend(out.data() + 16, size, tag)) throw std::runtime_error("fallo al cifrar");
        std::memcpy(out.data(), &size, 4);
        std::memcpy(out.data() + 4, tag, 12);
        boost::asio::write(s, boost::asio::buffer(out));
    }

    void CliReadExact(void* d, size_t n) { boost::asio::read(_cli, boost::asio::buffer(d, n)); }
    bool CliRecv(uint16& opcode, std::vector<uint8>& body) { RecvOn(_cli, _crypt, opcode, body); return true; }

    // reparte según la tabla de opcodes de TrinityCore: los "de instancia" van por la segunda conexión si ya existe
    static std::string Hex(WorldPacket const& p)       // traza: contenido de los paquetes pequeños
    {
        if (p.size() > 512) return "";
        static char const* d = "0123456789ABCDEF";
        std::string s; s.reserve(p.size() * 2);
        for (size_t i = 0; i < p.size(); ++i) { uint8 c = p.contents()[i]; s += d[c >> 4]; s += d[c & 15]; }
        return s;
    }
    void CliSend(WorldPacket const* pkt)
    {
        if (_prueba)
        {
            uint32 op = pkt->GetOpcode();
            std::lock_guard<std::mutex> g(_cliSend);
            if (_enviados[op]++ == 0 || g_traza)
                Log("[prueba] -> cliente %s (%u bytes)", GetOpcodeNameForLogging(static_cast<OpcodeServer>(op)).c_str(), (uint32)pkt->size());
            return;
        }
        if (g_traza) Log("[traza] -> cliente %s (%u) %s", GetOpcodeNameForLogging(static_cast<OpcodeServer>(pkt->GetOpcode())).c_str(), (uint32)pkt->size(), Hex(*pkt).c_str());
        ConnectionType con = pkt->GetConnection();
        if (con == CONNECTION_TYPE_DEFAULT)
            if (ServerOpcodeHandler const* h = opcodeTable[OpcodeServer(pkt->GetOpcode())])
                con = h->ConnectionIndex;
        if (con == CONNECTION_TYPE_INSTANCE && _c1Ready)
            SendOn(*_c1, _c1Crypt, _c1Send, pkt);
        else
            SendOn(_cli, _crypt, _cliSend, pkt);
    }

    // ---------------------------------------------------------------- cliente 3.4.3: handshake
    void ClientHandshake()
    {
        std::string s = ServerInit + "\n";
        boost::asio::write(_cli, boost::asio::buffer(s));
        std::vector<char> init(ClientInit.size() + 1);
        CliReadExact(init.data(), init.size());
        if (std::string(init.data(), ClientInit.size()) != ClientInit || init.back() != '\n')
            throw std::runtime_error("saludo de cliente incorrecto");

        _serverChallenge = Trinity::Crypto::GetRandomBytes<16>();
        WorldPackets::Auth::AuthChallenge ch;
        ch.Challenge = _serverChallenge;
        auto dos = Trinity::Crypto::GetRandomBytes<32>();
        std::memcpy(ch.DosChallenge.data(), dos.data(), 32);
        ch.DosZeroBits = 1;
        CliSend(ch.Write());
    }

    void HandleAuthSession(std::vector<uint8> const& body)
    {
        WorldPacket p(CMSG_AUTH_SESSION);
        p.append(body.data(), body.size());
        uint64 dosResponse; uint32 regionId, battlegroupId, realmId, ticketSize;
        std::array<uint8, 16> localChallenge; std::array<uint8, 24> digest;
        p >> dosResponse >> regionId >> battlegroupId >> realmId;
        p.read(localChallenge.data(), 16);
        p.read(digest.data(), 24);
        p.ReadBit();                                     // UseIPv6
        p >> ticketSize;
        std::string ticket(std::min<size_t>(ticketSize, p.size() - p.rpos()), '\0');
        if (!ticket.empty()) p.read(reinterpret_cast<uint8*>(ticket.data()), ticket.size());
        Log("AUTH_SESSION ticket='%s' realm=%u", ticket.c_str(), realmId);

        Db db;
        if (!db.Open()) throw std::runtime_error("no conecto a la BD del proyecto");
        std::vector<uint8> keyData;
        if (!db.GetAccount(ticket, _accountId, _acct, keyData) || keyData.empty())
            throw std::runtime_error("cuenta desconocida o sin session_key_bnet: " + ticket);

        Trinity::Crypto::SHA256 dk; dk.UpdateData(keyData.data(), keyData.size()); dk.UpdateData(Cfg::Win64AuthSeed, 16); dk.Finalize();
        Trinity::Crypto::HMAC_SHA256 hm(dk.GetDigest());
        hm.UpdateData(localChallenge); hm.UpdateData(_serverChallenge); hm.UpdateData(AuthCheckSeed, 16); hm.Finalize();
        if (std::memcmp(hm.GetDigest().data(), digest.data(), digest.size()) != 0)
            throw std::runtime_error("digest de AUTH_SESSION incorrecto (clave o semilla del build)");

        Trinity::Crypto::SHA256 kd; kd.UpdateData(keyData.data(), keyData.size()); kd.Finalize();
        Trinity::Crypto::HMAC_SHA256 sk(kd.GetDigest());
        sk.UpdateData(_serverChallenge); sk.UpdateData(localChallenge); sk.UpdateData(SessionKeySeed, 16); sk.Finalize();
        SessionKeyGenerator<Trinity::Crypto::SHA256> gen(sk.GetDigest());
        gen.Generate(_sessionKey.data(), 40);

        Trinity::Crypto::HMAC_SHA256 ek(_sessionKey);
        ek.UpdateData(localChallenge); ek.UpdateData(_serverChallenge); ek.UpdateData(EncryptionKeySeed, 16); ek.Finalize();
        std::memcpy(_encryptKey.data(), ek.GetDigest().data(), 16);

        // lado 3.3.5: clave de sesión propia para entrar en el worldserver de AC como un cliente normal
        _key335 = Trinity::Crypto::GetRandomBytes<40>();
        if (!db.SetSessionKey335(_accountId, _key335.data(), 40)) throw std::runtime_error("no pude escribir session_key 3.3.5");

        Log("[%s] digest OK; activando cifrado", _acct.c_str());
        WorldPackets::Auth::EnterEncryptedMode eem(_encryptKey, true);
        CliSend(eem.Write());
    }

    void AfterEncryption()
    {
        _crypt.Init(_encryptKey);
        ConnectWorld();                                  // entra en el worldserver 3.3.5 antes de responder al cliente

        uint32 realmAddress = (Cfg::Region << 24) | (Cfg::Battlegroup << 16) | Cfg::RealmId;
        static std::vector<RaceClassAvailability> const clases = BuildClasses();
        WorldPackets::Auth::AuthResponse ar;
        ar.Result = 0;                                   // ERROR_OK
        ar.SuccessInfo.emplace();
        ar.SuccessInfo->VirtualRealmAddress = realmAddress;
        std::string normalized = Cfg::RealmName; normalized.erase(std::remove(normalized.begin(), normalized.end(), ' '), normalized.end());
        ar.SuccessInfo->VirtualRealms.emplace_back(realmAddress, true, false, Cfg::RealmName, normalized);
        ar.SuccessInfo->ActiveExpansionLevel = 2;
        ar.SuccessInfo->AccountExpansionLevel = 2;
        ar.SuccessInfo->AvailableClasses = &clases;
        CliSend(ar.Write());

        WorldPackets::System::SetTimeZoneInformation tz;
        tz.ServerTimeTZ = "Europe/Madrid"; tz.GameTimeTZ = "Europe/Madrid"; tz.ServerRegionalTZ = "Europe/Madrid";
        CliSend(tz.Write());
        // como WorldSession::SendFeatureSystemStatusGlueScreen de TrinityCore; el límite de personajes, el de AzerothCore
        WorldPackets::System::FeatureSystemStatusGlueScreen fs;
        fs.MaxCharactersPerRealm = ConfigAC("CharactersPerRealm", 10);
        fs.MinimumExpansionLevel = 0;                    // EXPANSION_CLASSIC
        fs.MaximumExpansionLevel = 2;                    // WotLK
        fs.EuropaTicketSystemStatus.emplace();
        fs.EuropaTicketSystemStatus->ThrottleState.MaxTries = 10;
        fs.EuropaTicketSystemStatus->ThrottleState.PerMilliseconds = 60000;
        fs.EuropaTicketSystemStatus->ThrottleState.TryCount = 1;
        fs.EuropaTicketSystemStatus->ThrottleState.LastResetTimeBeforeNow = 111111;
        fs.EuropaTicketSystemStatus->BugsEnabled = true;
        fs.EuropaTicketSystemStatus->ComplaintsEnabled = true;
        fs.EuropaTicketSystemStatus->TicketsEnabled = true;
        fs.EuropaTicketSystemStatus->SuggestionsEnabled = true;
        CliSend(fs.Write());
        WorldPackets::ClientConfig::ClientCacheVersion cv;
        CliSend(cv.Write());
        WorldPackets::Hotfix::AvailableHotfixes ah;
        ah.VirtualRealmAddress = int32(realmAddress);
        static bool const sinPropios = std::ifstream("worldgate_sin_hotfix_propios.txt").good();   // interruptor de emergencia
        for (auto const& [id, push] : sDB2Manager.GetHotfixData())   // solo los propios: los de Blizzard ya los trae el cliente
            if (!sinPropios && id >= Cfg::PrimerHotfixPropio && !push.Records.empty() && (push.AvailableLocalesMask & (1 << Cfg::Db2Locale)))
                ah.Hotfixes.insert(push.Records.front().ID);
        CliSend(ah.Write());
        WorldPackets::ClientConfig::AccountDataTimes adt;
        adt.ServerTime = std::time(nullptr);
        CliSend(adt.Write());
        WorldPackets::Misc::TutorialFlags tf;
        std::memset(tf.TutorialData, 0xFF, sizeof(tf.TutorialData));
        CliSend(tf.Write());
        WorldPackets::Battlenet::ConnectionStatus bc;
        bc.State = 1; bc.SuppressNotification = true;
        CliSend(bc.Write());
        Log("[%s] en la pantalla de personajes", _acct.c_str());
    }

    static std::vector<RaceClassAvailability> BuildClasses()
    {
        // combinaciones raza/clase de WotLK (1 guerrero 2 paladín 3 cazador 4 pícaro 5 sacerdote 6 DK 7 chamán 8 mago 9 brujo 11 druida)
        struct RC { uint8 race; std::vector<uint8> classes; };
        std::vector<RC> t = {
            { 1, {1,2,4,5,6,8,9} }, { 2, {1,3,4,6,7,9} }, { 3, {1,2,3,4,5,6} }, { 4, {1,3,4,5,6,11} },
            { 5, {1,4,5,6,8,9} }, { 6, {1,3,6,7,11} }, { 7, {1,4,6,8,9} }, { 8, {1,3,4,5,6,7,8} },
            { 10, {2,3,4,5,6,8,9} }, { 11, {1,2,3,5,6,7,8} } };
        std::vector<RaceClassAvailability> out;
        for (RC const& rc : t)
        {
            RaceClassAvailability r; r.RaceID = rc.race;
            for (uint8 c : rc.classes)
            {
                ClassAvailability a; a.ClassID = c; a.ActiveExpansionLevel = c == 6 ? 2 : 0; a.AccountExpansionLevel = a.ActiveExpansionLevel;
                r.Classes.push_back(a);
            }
            out.push_back(r);
        }
        return out;
    }

    // ---------------------------------------------------------------- cliente 3.4.3: bucle
    void ClientLoop()
    {
        uint16 op; std::vector<uint8> body;
        while (!_stop && CliRecv(op, body))
            if (!Dispatch(op, body))
                return;
    }

    // paquete del cliente (llegue por la conexión de reino o por la de instancia); false = cerrar sesión.
    // Un paquete que no se deja leer (formato distinto al de TrinityCore) se descarta y la sesión sigue.
    bool Dispatch(uint16 op, std::vector<uint8> const& body)
    {
        try { return DispatchPaquete(op, body); }
        catch (ByteBufferException const& e)
        {
            Log("[%s] ERROR leyendo %s (%u bytes), se descarta: %s", _acct.c_str(),
                GetOpcodeNameForLogging(static_cast<OpcodeClient>(op)).c_str(), (uint32)body.size(), e.what());
            return true;
        }
    }
    bool DispatchPaquete(uint16 op, std::vector<uint8> const& body)
    {
        if (g_traza) Log("[traza] cliente -> %s (%u)", GetOpcodeNameForLogging(static_cast<OpcodeClient>(op)).c_str(), (uint32)body.size());
        {
            switch (op)
            {
                case CMSG_AUTH_SESSION: HandleAuthSession(body); break;
                case CMSG_ENTER_ENCRYPTED_MODE_ACK: AfterEncryption(); break;
                case CMSG_PING:
                {
                    Rd r(body); uint32 serial = r.get<uint32>(); uint32 latencia = r.get<uint32>();
                    WorldPackets::Auth::Pong pong(serial);
                    CliSend(pong.Write());
                    if (_tcPlayer) { Wr w; w.put<uint32>(serial).put<uint32>(latencia); SrvSend(CMSG335_PING, w.b); }
                    break;
                }
                case CMSG_ENUM_CHARACTERS:
                    SrvSend(CMSG335_CHAR_ENUM, {});
                    break;
                case CMSG_CREATE_CHARACTER:
                {
                    WorldPacket wp(CMSG_CREATE_CHARACTER); wp.append(body.data(), body.size());
                    WorldPackets::Character::CreateCharacter cc(std::move(wp)); cc.Read();
                    auto const& ci = *cc.CreateInfo;
                    std::vector<uint32> choices; for (auto const& c : ci.Customizations) choices.push_back(c.ChrCustomizationChoiceID);
                    uint8 v[5]; Apariencia::ToClassic(ci.Race, ci.Sex, choices, v);
                    Wr w; w.cstr(ci.Name).put<uint8>(ci.Race).put<uint8>(ci.Class).put<uint8>(ci.Sex)
                          .put<uint8>(v[0]).put<uint8>(v[1]).put<uint8>(v[2]).put<uint8>(v[3]).put<uint8>(v[4]).put<uint8>(0);
                    Log("[%s] crear '%s' raza %u clase %u sexo %u apariencia %u/%u/%u/%u/%u", _acct.c_str(), ci.Name.c_str(), ci.Race, ci.Class, ci.Sex, v[0], v[1], v[2], v[3], v[4]);
                    SrvSend(CMSG335_CHAR_CREATE, w.b);
                    break;
                }
                // movimiento del propio jugador -> MSG_MOVE_* 3.3.5 (mismo cuerpo para todos)
                case CMSG_MOVE_START_FORWARD: case CMSG_MOVE_START_BACKWARD: case CMSG_MOVE_STOP:
                case CMSG_MOVE_START_STRAFE_LEFT: case CMSG_MOVE_START_STRAFE_RIGHT: case CMSG_MOVE_STOP_STRAFE:
                case CMSG_MOVE_JUMP: case CMSG_MOVE_DOUBLE_JUMP: case CMSG_MOVE_START_TURN_LEFT: case CMSG_MOVE_START_TURN_RIGHT: case CMSG_MOVE_STOP_TURN:
                case CMSG_MOVE_START_PITCH_UP: case CMSG_MOVE_START_PITCH_DOWN: case CMSG_MOVE_STOP_PITCH:
                case CMSG_MOVE_SET_RUN_MODE: case CMSG_MOVE_SET_WALK_MODE: case CMSG_MOVE_FALL_LAND:
                case CMSG_MOVE_START_SWIM: case CMSG_MOVE_STOP_SWIM: case CMSG_MOVE_SET_FACING: case CMSG_MOVE_SET_FACING_HEARTBEAT:
                case CMSG_MOVE_SET_PITCH: case CMSG_MOVE_HEARTBEAT: case CMSG_MOVE_START_ASCEND: case CMSG_MOVE_STOP_ASCEND:
                case CMSG_MOVE_START_DESCEND: case CMSG_MOVE_FALL_RESET: case CMSG_MOVE_CHANGE_TRANSPORT: case CMSG_MOVE_SET_FLY:
                {
                    static std::unordered_map<uint16, uint16> const a335 = {
                        { CMSG_MOVE_START_FORWARD, 0x0B5 }, { CMSG_MOVE_START_BACKWARD, 0x0B6 }, { CMSG_MOVE_STOP, 0x0B7 },
                        { CMSG_MOVE_START_STRAFE_LEFT, 0x0B8 }, { CMSG_MOVE_START_STRAFE_RIGHT, 0x0B9 }, { CMSG_MOVE_STOP_STRAFE, 0x0BA },
                        { CMSG_MOVE_JUMP, 0x0BB }, { CMSG_MOVE_DOUBLE_JUMP, 0x0BB }, { CMSG_MOVE_START_TURN_LEFT, 0x0BC },
                        { CMSG_MOVE_START_TURN_RIGHT, 0x0BD }, { CMSG_MOVE_STOP_TURN, 0x0BE }, { CMSG_MOVE_START_PITCH_UP, 0x0BF },
                        { CMSG_MOVE_START_PITCH_DOWN, 0x0C0 }, { CMSG_MOVE_STOP_PITCH, 0x0C1 }, { CMSG_MOVE_SET_RUN_MODE, 0x0C2 },
                        { CMSG_MOVE_SET_WALK_MODE, 0x0C3 }, { CMSG_MOVE_FALL_LAND, 0x0C9 }, { CMSG_MOVE_START_SWIM, 0x0CA },
                        { CMSG_MOVE_STOP_SWIM, 0x0CB }, { CMSG_MOVE_SET_FACING, 0x0DA }, { CMSG_MOVE_SET_FACING_HEARTBEAT, 0x0DA },
                        { CMSG_MOVE_SET_PITCH, 0x0DB }, { CMSG_MOVE_HEARTBEAT, 0x0EE }, { CMSG_MOVE_START_ASCEND, 0x359 },
                        { CMSG_MOVE_STOP_ASCEND, 0x35A }, { CMSG_MOVE_START_DESCEND, 0x3A7 }, { CMSG_MOVE_FALL_RESET, 0x2CA },
                        { CMSG_MOVE_CHANGE_TRANSPORT, 0x38D }, { CMSG_MOVE_SET_FLY, 0x346 } };
                    if (!_tcPlayer) break;
                    WorldPacket wp{ static_cast<OpcodeClient>(op) }; wp.append(body.data(), body.size());
                    WorldPackets::Movement::ClientPlayerMovement mv(std::move(wp)); mv.Read();
                    SrvSend(a335.at(op), _mundo.MovimientoA335(mv.Status));
                    if (mv.Status.transport.guid.IsEmpty())
                        _juego.ComprobarDisparadores(_mundo.mapa, mv.Status.pos.GetPositionX(), mv.Status.pos.GetPositionY(), mv.Status.pos.GetPositionZ());
                    if (!_movAvisado) { _movAvisado = true; Log("[%s] primer movimiento del cliente -> 3.3.5 0x%03X (%.1f, %.1f, %.1f)", _acct.c_str(), a335.at(op), mv.Status.pos.GetPositionX(), mv.Status.pos.GetPositionY(), mv.Status.pos.GetPositionZ()); }
                    break;
                }
                case CMSG_MOVE_SPLINE_DONE:                  // -> 0x2C9: movimiento + u32 curva
                {
                    if (!_tcPlayer) break;
                    WorldPacket wp{ CMSG_MOVE_SPLINE_DONE }; wp.append(body.data(), body.size());
                    WorldPackets::Movement::MoveSplineDone sd(std::move(wp)); sd.Read();
                    std::vector<uint8> cuerpo = _mundo.MovimientoA335(sd.Status);
                    Wr w; w.raw(cuerpo.data(), cuerpo.size()).put<uint32>(uint32(sd.SplineID));
                    SrvSend(0x2C9, w.b);
                    break;
                }
                case CMSG_MOVE_INIT_ACTIVE_MOVER_COMPLETE:   // u32 ticks
                {
                    Rd r(body);
                    _mundo.InitMover(r.get<uint32>());
                    break;
                }
                case CMSG_REQUEST_ACCOUNT_DATA:      // -> 0x20A (u32 tipo); los tipos 8+ no existen en 3.3.5: se contestan vacíos
                {
                    WorldPacket wp(CMSG_REQUEST_ACCOUNT_DATA); wp.append(body.data(), body.size());
                    WorldPackets::ClientConfig::RequestAccountData q(std::move(wp)); q.Read();
                    if (q.DataType < 8) { Wr w; w.put<uint32>(q.DataType); SrvSend(0x20A, w.b); }
                    else
                    {
                        WorldPackets::ClientConfig::UpdateAccountData ua;
                        ua.Player = q.PlayerGuid; ua.DataType = q.DataType;
                        CliSend(ua.Write());
                    }
                    break;
                }
                case CMSG_UPDATE_ACCOUNT_DATA:       // -> 0x20B (u32 tipo, u32 hora, u32 tamaño, zlib)
                {
                    WorldPacket wp(CMSG_UPDATE_ACCOUNT_DATA); wp.append(body.data(), body.size());
                    WorldPackets::ClientConfig::UserClientUpdateAccountData q(std::move(wp)); q.Read();
                    if (q.DataType >= 8) break;
                    Wr w; w.put<uint32>(q.DataType).put<uint32>(uint32(q.Time.AsUnderlyingType())).put<uint32>(q.Size);
                    if (q.CompressedData.size()) w.raw(q.CompressedData.contents(), q.CompressedData.size());
                    SrvSend(0x20B, w.b);
                    break;
                }
                case CMSG_TIME_SYNC_RESPONSE:        // -> CMSG_TIME_SYNC_RESP 3.3.5 (u32 contador, u32 ticks del cliente)
                {
                    WorldPacket wp(CMSG_TIME_SYNC_RESPONSE); wp.append(body.data(), body.size());
                    WorldPackets::Misc::TimeSyncResponse ts(std::move(wp)); ts.Read();
                    Wr w; w.put<uint32>(ts.SequenceIndex).put<uint32>(ts.ClientTime);
                    SrvSend(0x391, w.b);
                    break;
                }
                case CMSG_QUERY_CREATURE:            // -> CMSG_CREATURE_QUERY 3.3.5 (u32 entry, u64 guid)
                {
                    WorldPacket wp(CMSG_QUERY_CREATURE); wp.append(body.data(), body.size());
                    WorldPackets::Query::QueryCreature q(std::move(wp)); q.Read();
                    Wr w; w.put<uint32>(q.CreatureID).put<uint64>(0);
                    SrvSend(0x060, w.b);
                    break;
                }
                case CMSG_QUERY_GAME_OBJECT:         // -> CMSG_GAMEOBJECT_QUERY 3.3.5 (u32 entry, u64 guid)
                {
                    WorldPacket wp(CMSG_QUERY_GAME_OBJECT); wp.append(body.data(), body.size());
                    WorldPackets::Query::QueryGameObject q(std::move(wp)); q.Read();
                    { std::lock_guard<std::mutex> g(_goMutex); _goGuids[q.GameObjectID] = q.Guid; }
                    Wr w; w.put<uint32>(q.GameObjectID).put<uint64>(0);
                    Log("[%s] consulta objeto %u", _acct.c_str(), q.GameObjectID);
                    SrvSend(0x05E, w.b);
                    break;
                }
                case CMSG_QUERY_PLAYER_NAMES:        // -> un CMSG_NAME_QUERY 3.3.5 (u64 guid) por jugador
                {
                    WorldPacket wp(CMSG_QUERY_PLAYER_NAMES); wp.append(body.data(), body.size());
                    WorldPackets::Query::QueryPlayerNames q(std::move(wp)); q.Read();
                    for (ObjectGuid const& g : q.Players)
                    {
                        Wr w; w.put<uint64>(g.GetCounter());
                        SrvSend(0x050, w.b);
                    }
                    break;
                }
                case CMSG_CHAR_DELETE:
                {
                    WorldPacket wp(CMSG_CHAR_DELETE); wp.append(body.data(), body.size());
                    WorldPackets::Character::CharDelete cd(std::move(wp)); cd.Read();
                    Wr w; w.put<uint64>(cd.Guid.GetCounter());
                    SrvSend(CMSG335_CHAR_DELETE, w.b);
                    break;
                }
                // ---- servicios de personaje (pantalla de personajes)
                case CMSG_CHARACTER_RENAME_REQUEST:          // -> CMSG_CHAR_RENAME 0x2C7: u64, cstr
                {
                    WorldPacket wp(CMSG_CHARACTER_RENAME_REQUEST); wp.append(body.data(), body.size());
                    WorldPackets::Character::CharacterRenameRequest q(std::move(wp)); q.Read();
                    _pjServicio = q.RenameInfo->Guid.GetCounter();
                    Wr w; w.put<uint64>(_pjServicio).cstr(q.RenameInfo->NewName); SrvSend(0x2C7, w.b);
                    break;
                }
                case CMSG_CHAR_CUSTOMIZE:                    // -> 0x473: u64, cstr, u8 sexo, piel, color, peinado, vello, cara
                {
                    WorldPacket wp(CMSG_CHAR_CUSTOMIZE); wp.append(body.data(), body.size());
                    WorldPackets::Character::CharCustomize q(std::move(wp)); q.Read();
                    auto const& ci = *q.CustomizeInfo;
                    _pjServicio = ci.CharGUID.GetCounter(); _razaServicio = _pjs[_pjServicio].second;
                    std::vector<uint32> ch; for (auto const& c : ci.Customizations) ch.push_back(c.ChrCustomizationChoiceID);
                    uint8 v[5]; Apariencia::ToClassic(_razaServicio, ci.SexID, ch, v);   // piel, cara, peinado, color, vello
                    Wr w; w.put<uint64>(_pjServicio).cstr(ci.CharName).put<uint8>(ci.SexID).put<uint8>(v[0]).put<uint8>(v[3]).put<uint8>(v[2]).put<uint8>(v[4]).put<uint8>(v[1]);
                    SrvSend(0x473, w.b);
                    break;
                }
                case CMSG_CHAR_RACE_OR_FACTION_CHANGE:       // -> 0x4D9 (facción) / 0x4F8 (raza): u64, cstr, u8 sexo, piel, color, peinado, vello, cara, raza
                {
                    WorldPacket wp(CMSG_CHAR_RACE_OR_FACTION_CHANGE); wp.append(body.data(), body.size());
                    WorldPackets::Character::CharRaceOrFactionChange q(std::move(wp)); q.Read();
                    auto const& ci = *q.RaceOrFactionChangeInfo;
                    _pjServicio = ci.Guid.GetCounter(); _razaServicio = ci.RaceID;
                    std::vector<uint32> ch; for (auto const& c : ci.Customizations) ch.push_back(c.ChrCustomizationChoiceID);
                    uint8 v[5]; Apariencia::ToClassic(ci.RaceID, ci.SexID, ch, v);
                    Wr w; w.put<uint64>(_pjServicio).cstr(ci.Name).put<uint8>(ci.SexID).put<uint8>(v[0]).put<uint8>(v[3]).put<uint8>(v[2]).put<uint8>(v[4]).put<uint8>(v[1]).put<uint8>(ci.RaceID);
                    SrvSend(ci.FactionChange ? 0x4D9 : 0x4F8, w.b);
                    break;
                }
                case CMSG_SET_PLAYER_DECLINED_NAMES:         // -> 0x419: u64, cstr nombre actual, 5 x cstr
                {
                    WorldPacket wp(CMSG_SET_PLAYER_DECLINED_NAMES); wp.append(body.data(), body.size());
                    WorldPackets::Character::SetPlayerDeclinedNames q(std::move(wp)); q.Read();
                    uint64 g = q.Player.GetCounter();
                    Wr w; w.put<uint64>(g).cstr(_pjs[g].first);
                    for (std::string const& n : q.DeclinedNames.name) w.cstr(n);
                    SrvSend(0x419, w.b);
                    break;
                }
                case CMSG_GENERATE_RANDOM_CHARACTER_NAME:    // el cliente 3.3.5 lo generaba él; TrinityCore lo saca de NameGen.db2
                {
                    WorldPacket wp(CMSG_GENERATE_RANDOM_CHARACTER_NAME); wp.append(body.data(), body.size());
                    WorldPackets::Character::GenerateRandomCharacterName q(std::move(wp)); q.Read();
                    WorldPackets::Character::GenerateRandomCharacterNameResult res;
                    res.Name = sDB2Manager.GetNameGenEntry(q.Race, q.Sex);
                    res.Success = !res.Name.empty();
                    CliSend(res.Write());
                    break;
                }
                case CMSG_HOTFIX_REQUEST:                    // copia de WorldSession::HandleHotfixRequest (idioma enUS)
                {
                    WorldPacket wp(CMSG_HOTFIX_REQUEST); wp.append(body.data(), body.size());
                    WorldPackets::Hotfix::HotfixRequest q(std::move(wp)); q.Read();
                    DB2Manager::HotfixContainer const& hotfixes = sDB2Manager.GetHotfixData();
                    // la respuesta se parte en paquetes de ~1 MB (entre envíos, nunca a mitad): con los hotfixes de objetos de
                    // AzerothCore la primera vez son ~7 MB y el mayor que se ha visto aceptar al cliente es de 855 KB
                    auto respPtr = std::make_unique<WorldPackets::Hotfix::HotfixConnect>();
                    uint32 enviados = 0, paquetes = 0;
                    auto vacia = [&]()
                    {
                        if (respPtr->Hotfixes.empty()) return;
                        enviados += uint32(respPtr->Hotfixes.size()); ++paquetes;
                        CliSend(respPtr->Write());
                        respPtr = std::make_unique<WorldPackets::Hotfix::HotfixConnect>();
                    };
                    for (int32 hotfixId : q.Hotfixes)
                    {
                        auto itPush = hotfixes.find(hotfixId); DB2Manager::HotfixPush const* push = itPush != hotfixes.end() ? &itPush->second : nullptr;
                        if (!push) continue;
                        if (respPtr->HotfixContent.size() >= 1024 * 1024) vacia();
                        WorldPackets::Hotfix::HotfixConnect& resp = *respPtr;
                        for (DB2Manager::HotfixRecord const& rec : push->Records)
                        {
                            auto& hd = resp.Hotfixes.emplace_back();
                            hd.Record = rec;
                            if (rec.HotfixStatus != DB2Manager::HotfixRecord::Status::Valid) continue;
                            DB2StorageBase const* store = sDB2Manager.GetStorage(rec.TableHash);
                            if (store && store->HasRecord(uint32(rec.RecordID)))
                            {
                                std::size_t pos = resp.HotfixContent.size();
                                store->WriteRecord(uint32(rec.RecordID), Cfg::Db2Locale, resp.HotfixContent);
                                hd.Size = uint32(resp.HotfixContent.size() - pos);
                            }
                            // tablas que TC no carga (ItemDisplayInfo, ModelFileData, TextureFileData...): la fila va en binario en
                            // hotfix_blob, como en HotfixHandler.cpp de TC. Es la vía para el contenido custom de esas tablas
                            else if (std::vector<uint8> const* blob = sDB2Manager.GetHotfixBlobData(rec.TableHash, rec.RecordID, Cfg::Db2Locale))
                            {
                                hd.Size = uint32(blob->size());
                                resp.HotfixContent.append(blob->data(), blob->size());
                            }
                            else
                                hd.Record.HotfixStatus = store ? DB2Manager::HotfixRecord::Status::RecordRemoved : DB2Manager::HotfixRecord::Status::Invalid;
                        }
                    }
                    if (!paquetes || !respPtr->Hotfixes.empty()) { enviados += uint32(respPtr->Hotfixes.size()); ++paquetes; CliSend(respPtr->Write()); }
                    Log("[%s] hotfixes pedidos por el cliente: %u, registros enviados: %u en %u paquetes", _acct.c_str(), (uint32)q.Hotfixes.size(), enviados, paquetes);
                    break;
                }
                case CMSG_DB_QUERY_BULK:
                {
                    // el servidor no tiene filas DB2 propias todavía: se contesta "sin datos" y el cliente usa las suyas
                    WorldPacket wp(CMSG_DB_QUERY_BULK); wp.append(body.data(), body.size());
                    WorldPackets::Hotfix::DBQueryBulk q(std::move(wp));
                    q.Read();
                    DB2StorageBase const* store = sDB2Manager.GetStorage(q.TableHash);
                    if (_tablasPedidas.insert(q.TableHash).second)   // qué tablas DB2 pide el cliente por su cuenta (una vez por tabla)
                        Log("[%s] el cliente pide filas de la tabla %s (0x%08X), p. ej. %u", _acct.c_str(), store ? store->GetFileName().c_str() : "desconocida",
                            q.TableHash, q.Queries.empty() ? 0u : q.Queries[0].RecordID);
                    for (auto const& rec : q.Queries)
                    {
                        WorldPackets::Hotfix::DBReply rep;
                        rep.TableHash = q.TableHash; rep.RecordID = rec.RecordID; rep.Timestamp = uint32(std::time(nullptr));
                        if (store && store->HasRecord(rec.RecordID))   // DB2 del cliente + hotfix de tc343_hotfixes (objetos custom)
                        {
                            rep.Status = DB2Manager::HotfixRecord::Status::Valid;
                            store->WriteRecord(rec.RecordID, Cfg::Db2Locale, rep.Data);
                        }
                        CliSend(rep.Write());
                    }
                    break;
                }
                case CMSG_SERVER_TIME_OFFSET_REQUEST:
                {
                    WorldPackets::Misc::ServerTimeOffset sto; sto.Time = std::time(nullptr);
                    CliSend(sto.Write());
                    break;
                }
                case CMSG_GET_UNDELETE_CHARACTER_COOLDOWN_STATUS:
                {
                    WorldPackets::Character::UndeleteCooldownStatusResponse u;
                    CliSend(u.Write());
                    break;
                }
                case CMSG_BATTLE_PAY_GET_PRODUCT_LIST: case CMSG_BATTLE_PAY_GET_PURCHASE_LIST:
                case CMSG_UPDATE_VAS_PURCHASE_STATES: case CMSG_SOCIAL_CONTRACT_REQUEST: case CMSG_BATTLENET_REQUEST:
                    break;                                   // tienda/servicios de Blizzard: se ignoran
                case CMSG_LOG_DISCONNECT:
                    Log("[%s] el cliente se desconecta", _acct.c_str());
                    return false;
                case CMSG_PLAYER_LOGIN:
                {
                    WorldPacket wp(CMSG_PLAYER_LOGIN); wp.append(body.data(), body.size());
                    WorldPackets::Character::PlayerLogin pl(std::move(wp)); pl.Read();
                    _loginGuid = pl.Guid.GetCounter();
                    Log("[%s] entrar al mundo con el personaje %llu: pido la conexión de instancia", _acct.c_str(), (unsigned long long)_loginGuid);
                    InitJuego();
                    SendConnectTo();
                    break;
                }
                case CMSG_QUEUED_MESSAGES_END:
                    break;
                default:
                    if (_juego.mundo && _juego.Cliente(op, body)) break;
                    if (_vistosCli.insert(op).second)
                        Log("[%s] cliente -> %s (%u bytes) sin traducir todavía", _acct.c_str(), GetOpcodeNameForLogging(static_cast<OpcodeClient>(op)).c_str(), (uint32)body.size());
                    break;
            }
        }
        return true;
    }

    // ---------------------------------------------------------------- H3: respuestas de consulta
    // SMSG_CREATURE_QUERY_RESPONSE 3.3.5: entry (bit 31 = no existe), nombre + 3 vacíos, subnombre, icono, type_flags, type,
    // family, rank, 2 killcredit, 4 modelos, hp, maná, líder, 6 objetos de misión, movimiento
    void TranslateCreatureQuery(std::vector<uint8> const& body)
    {
        Rd r(body);
        WorldPackets::Query::QueryCreatureResponse res;
        uint32 entry = r.get<uint32>();
        res.CreatureID = entry & 0x7FFFFFFF;
        res.Allow = !(entry & 0x80000000) && body.size() > 4;
        if (res.Allow)
        {
            auto& s = res.Stats;
            s.Name[0] = r.cstr(); r.cstr(); r.cstr(); r.cstr();
            s.Title = r.cstr();
            s.CursorName = r.cstr();
            s.Flags[0] = r.get<uint32>();
            s.CreatureType = int32(r.get<uint32>());
            s.CreatureFamily = int32(r.get<uint32>());
            s.Classification = int32(r.get<uint32>());
            s.ProxyCreatureID[0] = r.get<uint32>(); s.ProxyCreatureID[1] = r.get<uint32>();
            for (int i = 0; i < 4; ++i)
                if (uint32 m = r.get<uint32>())
                {
                    WorldPackets::Query::CreatureXDisplay d; d.CreatureDisplayID = m; d.Scale = 1.0f; d.Probability = 1.0f;
                    s.Display.CreatureDisplay.push_back(d);
                    s.Display.TotalProbability += 1.0f;
                }
            s.HpMulti = r.get<float>(); s.EnergyMulti = r.get<float>();
            s.Leader = r.get<uint8>() != 0;
            for (int i = 0; i < 6; ++i) if (uint32 q = r.get<uint32>()) s.QuestItems.push_back(int32(q));
            s.CreatureMovementInfoID = r.get<uint32>();
            s.Class = 1;
        }
        CliSend(res.Write());
    }
    // SMSG_GAMEOBJECT_QUERY_RESPONSE 3.3.5: entry (bit 31 = no existe), type, displayId, nombre + 3 vacíos, icono, barra de
    // lanzamiento, unk, 24 datos, tamaño, 6 objetos de misión
    void TranslateGameObjectQuery(std::vector<uint8> const& body)
    {
        Rd r(body);
        WorldPackets::Query::QueryGameObjectResponse res;
        uint32 entry = r.get<uint32>();
        res.GameObjectID = entry & 0x7FFFFFFF;
        { std::lock_guard<std::mutex> g(_goMutex); res.Guid = _goGuids[res.GameObjectID]; }
        res.Allow = !(entry & 0x80000000) && body.size() > 4;
        if (res.Allow)
        {
            auto& s = res.Stats;
            s.Type = r.get<uint32>(); s.DisplayID = r.get<uint32>();
            s.Name[0] = r.cstr(); r.cstr(); r.cstr(); r.cstr();
            s.IconName = r.cstr(); s.CastBarCaption = r.cstr(); s.UnkString = r.cstr();
            for (int i = 0; i < 24; ++i) s.Data[i] = r.get<uint32>();
            s.Size = r.get<float>();
            // MO_TRANSPORT: el cliente 3.4.3 mueve todos los transportes con un reloj común (tt = G + hora local) y SOLO les
            // aplica el desfase propio sacado del progreso de DynamicFlags si la plantilla tiene allowstopping (Data[8]);
            // AC pone ahí su canBeStopped (0) y cada transporte iba con un error de fase fijo (quietos, saltos, tripulación
            // separada). Ver agentes/re_transportes.md
            if (s.Type == 15) { s.Data[8] = 1; s.Data[9] = 0; }   // allowstopping, InitStopped
            for (int i = 0; i < 6; ++i) if (uint32 q = r.get<uint32>()) s.QuestItems.push_back(int32(q));
        }
        Log("[%s] respuesta objeto %u: %s tipo %u modelo %u '%s'", _acct.c_str(), res.GameObjectID, res.Allow ? "ok" : "NO EXISTE", res.Stats.Type, res.Stats.DisplayID, res.Stats.Name[0].c_str());
        CliSend(res.Write());
    }
    // SMSG_NAME_QUERY_RESPONSE 3.3.5: packguid, u8 (0 = encontrado), nombre, reino, raza, sexo, clase, u8 declinado
    void TranslateNameQuery(std::vector<uint8> const& body)
    {
        Rd r(body);
        uint8 m = r.get<uint8>(); uint64 g = 0;
        for (int i = 0; i < 8; ++i) if (m & (1 << i)) g |= uint64(r.get<uint8>()) << (i * 8);
        WorldPackets::Query::QueryPlayerNamesResponse res;
        WorldPackets::Query::NameCacheLookupResult nc;
        nc.Player = ObjectGuid::Create<HighGuid::Player>(uint32(g));
        nc.Result = r.get<uint8>();
        if (nc.Result == 0)
        {
            nc.Data.emplace();
            auto& d = *nc.Data;
            d.Name = r.cstr(); r.cstr();
            _juego.Nombre(g, d.Name);
            d.Race = r.get<uint8>(); d.Sex = r.get<uint8>(); d.ClassID = r.get<uint8>();
            d.GuidActual = nc.Player;
            d.VirtualRealmAddress = (Cfg::Region << 24) | (Cfg::Battlegroup << 16) | Cfg::RealmId;
            d.AccountID = ObjectGuid::Create<HighGuid::WowAccount>(0);
            d.BnetAccountID = ObjectGuid::Create<HighGuid::BNetAccount>(0);
            d.Level = 1;
        }
        res.Players.push_back(std::move(nc));
        CliSend(res.Write());
    }

    void InitJuego()
    {
        _mundo.Enviar = [this](WorldPacket const* p) { CliSend(p); };
        _mundo.acct = _acct;
        _mundo.yo335 = _loginGuid;
        _juego.Enviar = _mundo.Enviar;
        _juego.Srv = [this](uint16 o, std::vector<uint8> const& b) { SrvSend(o, b); };
        _juego.acct = _acct;
        _juego.yo335 = _loginGuid;
        _juego.realmAddress = (Cfg::Region << 24) | (Cfg::Battlegroup << 16) | Cfg::RealmId;
        _juego.mundo = &_mundo;
        _mundo.AlVerMision = [this](uint32 q) { _juego.MisionVista(q); };
        _juego.AlSalir = [this]()
        {
            _mundo.Reiniciar();
            // como WorldSession::LogoutPlayer de TrinityCore: la conexión de instancia se cierra; al volver a entrar se pide otra
            _c1Ready = false;
            if (_c1) { boost::system::error_code ec; _c1->close(ec); }
            Log("[%s] de vuelta a la pantalla de personajes", _acct.c_str());
        };
    }

    // ---------------------------------------------------------------- H2: el personaje en el cliente
    uint8 _modosDificultad = 0;                          // characters.instance_mode_mask: bits 0-3 mazmorra, 4-7 banda
    void CrearPersonaje(int32 mapId, float x, float y, float z, float o)
    {
        Db db;
        if (!db.Open()) { Log("[%s] sin BD para crear el personaje", _acct.c_str()); return; }
        Db::Personaje c;
        if (!db.GetCharacter(_loginGuid, c)) { Log("[%s] no encuentro el personaje %llu", _acct.c_str(), (unsigned long long)_loginGuid); return; }
        _modosDificultad = c.modos;

        std::string nombreCuenta = _acct;
        _tcSession = std::make_unique<WorldSession>(_accountId, std::move(nombreCuenta), 1, nullptr, SEC_PLAYER, uint8(2), time_t(0),
                                                    std::string("Wn64"), Minutes(0), Cfg::Db2Locale, 0u, false);
        _tcPlayer = std::make_unique<GatePlayer>(_tcSession.get());
        GatePlayer* pl = _tcPlayer.get();
        std::string mudo;
        _sesionOtros = std::make_unique<WorldSession>(0u, std::move(mudo), 0, nullptr, SEC_PLAYER, uint8(2), time_t(0),
                                                      std::string("Wn64"), Minutes(0), Cfg::Db2Locale, 0u, false);
        ObjectGuid guid = ObjectGuid::Create<HighGuid::Player>(_loginGuid);
        pl->Init(guid);
        pl->m_mapId = uint32(mapId);
        pl->Relocate(x, y, z, o);
        pl->SetName(c.name);
        pl->SetRace(c.race); pl->SetClass(c.cls);
        pl->SetGender(Gender(c.gender)); pl->SetNativeGender(Gender(c.gender));
        pl->SetLevel(c.level, false);
        if (ChrModelEntry const* model = sDB2Manager.GetChrModel(c.race, c.gender))
            pl->SetDisplayId(model->DisplayID, true);
        if (ChrRacesEntry const* rEntry = sChrRacesStore.LookupEntry(c.race))
            pl->SetFaction(rEntry->FactionID);
        uint8 legacy[5] = { c.skin, c.face, c.hair, c.hairColor, c.facial };
        std::vector<UF::ChrCustomizationChoice> cust = Apariencia::ToCustom(c.race, c.gender, legacy);
        pl->SetCustomizations(Trinity::Containers::MakeIteratorPair(cust.begin(), cust.end()), false);
        pl->SetMaxHealth(std::max<uint32>(c.health, 1)); pl->SetHealth(std::max<uint32>(c.health, 1));
        Powers pw = c.cls == CLASS_WARRIOR ? POWER_RAGE : c.cls == CLASS_ROGUE ? POWER_ENERGY : c.cls == CLASS_DEATH_KNIGHT ? POWER_RUNIC_POWER : POWER_MANA;
        pl->SetPowerType(pw, false);
        int32 maxPw = pw == POWER_MANA ? int32(std::max<uint32>(c.power, 100)) : pw == POWER_ENERGY ? 100 : 1000;
        pl->SetMaxPower(pw, maxPw); pl->SetPower(pw, pw == POWER_MANA ? maxPw : (pw == POWER_ENERGY ? 100 : 0), false);

        // ritmos a 1 como Player::InitStatsForLevel de TC (con ModTimeRate a 0 los misiles no viajan; ver Mundo::RitmosTc)
        pl->SetModCastingSpeed(1.0f); pl->SetModSpellHaste(1.0f); pl->SetModHaste(1.0f);
        pl->SetModRangedHaste(1.0f); pl->SetModHasteRegen(1.0f); pl->SetModTimeRate(1.0f);
        // multiplicadores del jugador activo que el 3.3.5 no tiene (Player::InitStatsForLevel de TC); a 0 los tooltips calculan 0
        pl->Set(pl->m_values.ModifyValue(&Player::m_activePlayerData).ModifyValue(&UF::ActivePlayerData::ModSpellPowerPercent), 1.0f);
        pl->Set(pl->m_values.ModifyValue(&Player::m_activePlayerData).ModifyValue(&UF::ActivePlayerData::ModPeriodicHealingDonePercent), 1.0f);
        pl->Set(pl->m_values.ModifyValue(&Player::m_activePlayerData).ModifyValue(&UF::ActivePlayerData::ModHealingPercent), 1.0f);
        pl->Set(pl->m_values.ModifyValue(&Player::m_activePlayerData).ModifyValue(&UF::ActivePlayerData::ModHealingDonePercent), 1.0f);
        for (uint8 i = 0; i < MAX_SPELL_SCHOOL; ++i)
            pl->Set(pl->m_values.ModifyValue(&Player::m_activePlayerData).ModifyValue(&UF::ActivePlayerData::ModDamageDonePercent, i), 1.0f);
        for (uint16 i = 0; i < 3; ++i)
        {
            pl->Set(pl->m_values.ModifyValue(&Player::m_activePlayerData).ModifyValue(&UF::ActivePlayerData::WeaponDmgMultipliers, i), 1.0f);
            pl->Set(pl->m_values.ModifyValue(&Player::m_activePlayerData).ModifyValue(&UF::ActivePlayerData::WeaponAtkSpeedMultipliers, i), 1.0f);
        }
        pl->SetVirtualPlayerRealm((Cfg::Region << 24) | (Cfg::Battlegroup << 16) | Cfg::RealmId);   // a 0 el Lua falla con el nombre del reino
        pl->SetInventorySlotCount(INVENTORY_DEFAULT_SIZE);   // TrinityCore solo lo pone en Player::Create: sin esto la mochila no tiene huecos
        pl->Set(pl->m_values.ModifyValue(&Player::m_activePlayerData).ModifyValue(&UF::ActivePlayerData::MaxLevel), 80);

        // a partir de aquí el mundo (PNJ, objetos, otros jugadores) llega traducido de los UPDATE_OBJECT 3.3.5
        _mundo.Enviar = [this](WorldPacket const* p) { CliSend(p); };
        _mundo.acct = _acct;
        _mundo.mapa = uint32(mapId);
        _mundo.yo335 = _loginGuid;
        _mundo.sesionOtros = _sesionOtros.get();
        _mundo.yo = pl;
        _mundo._esperaMover = true;                      // transportes retenidos hasta el INIT_ACTIVE_MOVER (como TC)
        for (int t = 0; t < 2; ++t)                      // como RestMgr de TrinityCore: estado de descanso siempre puesto (2 normal)
            pl->Set(pl->m_values.ModifyValue(&Player::m_activePlayerData).ModifyValue(&UF::ActivePlayerData::RestInfo, t).ModifyValue(&UF::RestInfo::StateID), uint8(2));
        _mundo.AplicarPendienteYo();
        _juego.Nombre(_loginGuid, c.name);

        // paquetes de arranque en el orden de TrinityCore (Player::SendInitialPacketsBeforeAddToMap / AfterAddToMap)
        {
            WowTime ahora; ahora.SetUtcTimeFromUnixTime(std::time(nullptr));
            WorldPackets::Misc::LoginSetTimeSpeed ts;
            ts.NewSpeed = 0.01666667f;                   // velocidad del reloj del juego; sin esto el cliente no deja moverse
            ts.GameTime = ahora; ts.ServerTime = ahora;
            CliSend(ts.Write());
            WorldPackets::Misc::WorldServerInfo wsi;
            CliSend(wsi.Write());
            WorldPackets::Character::InitialSetup is;
            is.ServerExpansionLevel = 2;
            CliSend(is.Write());
        }
        WorldPackets::Movement::MoveSetActiveMover am;
        am.MoverGUID = guid;
        CliSend(am.Write());

        UpdateData ud{ static_cast<uint32>(mapId) };
        pl->BuildCreateUpdateBlockForPlayer(&ud, pl);
        WorldPacket pkt;
        ud.BuildPacket(&pkt);
        CliSend(&pkt);
        Log("[%s] SMSG_UPDATE_OBJECT de %s enviado (%u bytes; raza %u clase %u nivel %u)", _acct.c_str(), c.name.c_str(), (uint32)pkt.size(), c.race, c.cls, c.level);

        {
            WorldPackets::Misc::PhaseShiftChange psc;
            psc.Client = guid;
            psc.Phaseshift.PhaseShiftFlags = 0x08;       // PhaseShiftFlags::Unphased
            CliSend(psc.Write());
            WorldPackets::WorldState::InitWorldStates iws;
            iws.MapID = mapId;
            CliSend(iws.Write());
        }
        if (!_tutorial335.empty()) { _juego.Servidor(0x0FD, _tutorial335); _tutorial335.clear(); }
    }

    // ---------------------------------------------------------------- conexión de instancia (H2)
    void SendConnectTo()
    {
        _connectKey = (uint64(_accountId) & 0xFFFFFFFF) | (uint64(1) << 32) | (uint64(urand(0, 0x7FFFFFFF)) << 33);
        {
            std::lock_guard<std::mutex> g(g_regMutex);
            g_pendientes[_connectKey] = weak_from_this();
        }
        WorldPackets::Auth::ConnectTo ct;
        ct.Key = _connectKey;
        ct.Serial = WorldPackets::Auth::ConnectToSerial::WorldAttempt1;
        ct.Payload.Port = Cfg::InstancePort;
        auto v4 = boost::asio::ip::make_address_v4(Cfg::ListenIP).to_bytes();
        std::memcpy(ct.Payload.Where.Address.V4.data(), v4.data(), 4);
        ct.Payload.Where.Type = WorldPackets::Auth::ConnectTo::IPv4;
        ct.Con = CONNECTION_TYPE_INSTANCE;
        SendOn(_cli, _crypt, _cliSend, ct.Write());
    }

public:
    // ---------------------------------------------------------------- modo prueba (sin cliente 3.4.3)
    void PruebaEntrar(std::string const& cuenta, uint64 guid)
    {
        _prueba = true;
        Db db;
        if (!db.Open() || !db.GetAccountId(cuenta, _accountId, _acct)) throw std::runtime_error("cuenta de prueba desconocida: " + cuenta);
        _key335 = Trinity::Crypto::GetRandomBytes<40>();
        if (!db.SetSessionKey335(_accountId, _key335.data(), 40)) throw std::runtime_error("no pude escribir session_key 3.3.5");
        ConnectWorld();
        SrvSend(CMSG335_CHAR_ENUM, {});                  // AC solo acepta personajes listados antes en esta sesión
        std::this_thread::sleep_for(std::chrono::milliseconds(1500));
        _loginGuid = guid;
        InitJuego();
        Wr w; w.put<uint64>(guid);
        SrvSend(CMSG335_PLAYER_LOGIN, w.b);
        Log("[prueba] CMSG_PLAYER_LOGIN %llu enviado", (unsigned long long)guid);
    }
    void PruebaSrv(uint16 op, std::vector<uint8> const& b) { SrvSend(op, b); }
    void PruebaCli(uint16 op, std::vector<uint8> const& b) { Dispatch(op, b); }
    uint64 PruebaGuid(uint32 entry) { return entry ? _mundo.Guid335DeEntrada(entry) : _loginGuid; }
    void PruebaLista() { _mundo.Listar(); }
    void PruebaIr(uint32 entry)
    {
        Position pos;
        if (!_mundo.PosicionDe(entry, pos)) { Log("[prueba] no veo la entrada %u", entry); return; }
        MovementInfo mi;
        mi.pos.Relocate(pos.GetPositionX() + 1.5f, pos.GetPositionY(), pos.GetPositionZ(), 0.f);
        mi.time = getMSTime();
        SrvSend(0x0EE, _mundo.MovimientoA335(mi));
        if (_tcPlayer) _tcPlayer->Relocate(mi.pos);
        Log("[prueba] voy junto a %u (%.1f, %.1f, %.1f)", entry, pos.GetPositionX(), pos.GetPositionY(), pos.GetPositionZ());
    }
    void PruebaFin()
    {
        _stop = true;
        boost::system::error_code ec; _srv.close(ec);
        if (_srvThread.joinable()) _srvThread.join();
        Log("[prueba] resumen: %u opcodes 3.4.3 distintos enviados al cliente; %u opcodes 3.3.5 sin traducir", (uint32)_enviados.size(), (uint32)_vistos335.size());
        for (auto const& [op, n] : _enviados) Log("[prueba]   %6u x %s", n, GetOpcodeNameForLogging(static_cast<OpcodeServer>(op)).c_str());
    }

    // llamada desde el hilo que aceptó la segunda conexión, ya leído CMSG_AUTH_CONTINUED_SESSION
    void AttachInstance(std::shared_ptr<tcp::socket> sock, std::array<uint8, 16> const& serverChallenge,
                        uint64 key, std::array<uint8, 16> const& localChallenge, std::array<uint8, 24> const& digest)
    {
        Trinity::Crypto::HMAC_SHA256 hm(_sessionKey);
        hm.UpdateData(reinterpret_cast<uint8 const*>(&key), sizeof(key));
        hm.UpdateData(localChallenge); hm.UpdateData(serverChallenge); hm.UpdateData(ContinuedSessionSeed, 16); hm.Finalize();
        if (std::memcmp(hm.GetDigest().data(), digest.data(), digest.size()) != 0)
            throw std::runtime_error("digest de AUTH_CONTINUED_SESSION incorrecto");

        Trinity::Crypto::HMAC_SHA256 ek(_sessionKey);
        ek.UpdateData(localChallenge); ek.UpdateData(serverChallenge); ek.UpdateData(EncryptionKeySeed, 16); ek.Finalize();
        std::array<uint8, 16> encKey; std::memcpy(encKey.data(), ek.GetDigest().data(), 16);

        // cada conexión de instancia empieza con el cifrado de cero (al volver a entrar tras salir al menú, como TrinityCore)
        _c1Ready = false;
        _c1Crypt.~WorldPacketCrypt(); new (&_c1Crypt) WorldPacketCrypt();
        _c1 = sock;
        // el reto y CMSG_AUTH_CONTINUED_SESSION se enviaron/leyeron sin cifrar, pero cuentan en los contadores de AES-GCM
        { uint8 d[2] = { 0, 0 }; Trinity::Crypto::AES::Tag tg{}; _c1Crypt.EncryptSend(d, 2, tg); _c1Crypt.DecryptRecv(d, 2, tg); }
        WorldPackets::Auth::EnterEncryptedMode eem(encKey, true);
        SendOn(*_c1, _c1Crypt, _c1Send, eem.Write());
        uint16 op; std::vector<uint8> body;
        RecvOn(*_c1, _c1Crypt, op, body);
        if (op != CMSG_ENTER_ENCRYPTED_MODE_ACK)
            throw std::runtime_error("la conexión de instancia no confirmó el cifrado");
        _c1Crypt.Init(encKey);
        _c1Ready = true;
        Log("[%s] conexión de instancia lista", _acct.c_str());

        WorldPackets::Auth::ResumeComms rc(CONNECTION_TYPE_INSTANCE);
        SendOn(*_c1, _c1Crypt, _c1Send, rc.Write());

        // ahora sí: el personaje entra en el worldserver de AzerothCore
        Wr w; w.put<uint64>(_loginGuid);
        SrvSend(CMSG335_PLAYER_LOGIN, w.b);
        Log("[%s] CMSG_PLAYER_LOGIN enviado al worldserver 3.3.5", _acct.c_str());

        while (!_stop)
        {
            RecvOn(*sock, _c1Crypt, op, body);           // su propio socket: tras salir al menú se cierra y este hilo termina
            if (!Dispatch(op, body))
                break;
        }
    }

private:

    // ---------------------------------------------------------------- worldserver 3.3.5
    void SrvReadExact(void* d, size_t n) { boost::asio::read(_srv, boost::asio::buffer(d, n)); }

    void SrvRecv(uint16& opcode, std::vector<uint8>& body)
    {
        uint8 h[5];
        SrvReadExact(h, 1);
        if (_arcOn) _srvDecrypt.UpdateData(h, 1);
        size_t hlen = (h[0] & 0x80) ? 5 : 4;
        SrvReadExact(h + 1, hlen - 1);
        if (_arcOn) _srvDecrypt.UpdateData(h + 1, hlen - 1);
        uint32 size = hlen == 5 ? (((h[0] & 0x7F) << 16) | (h[1] << 8) | h[2]) : ((h[0] << 8) | h[1]);
        std::memcpy(&opcode, h + hlen - 2, 2);
        body.resize(size - 2);
        if (!body.empty()) SrvReadExact(body.data(), body.size());
    }

    void SrvSend(uint16 opcode, std::vector<uint8> const& body)
    {
        std::lock_guard<std::mutex> g(_srvSend);
        uint8 h[6];
        uint16 size = uint16(body.size() + 4);
        h[0] = uint8(size >> 8); h[1] = uint8(size & 0xFF);
        uint32 op = opcode; std::memcpy(h + 2, &op, 4);
        if (_arcOn) _srvEncrypt.UpdateData(h, 6);
        std::vector<uint8> out(h, h + 6); out.insert(out.end(), body.begin(), body.end());
        boost::asio::write(_srv, boost::asio::buffer(out));
    }

    void ConnectWorld()
    {
        _srv.connect(tcp::endpoint(boost::asio::ip::make_address(Cfg::WorldIP), Cfg::WorldPort));
        uint16 op; std::vector<uint8> body;
        SrvRecv(op, body);
        if (op != SMSG335_AUTH_CHALLENGE || body.size() < 8) throw std::runtime_error("el worldserver no mandó AUTH_CHALLENGE");
        uint8 seed[4]; std::memcpy(seed, body.data() + 4, 4);

        uint8 local[4]; auto rnd = Trinity::Crypto::GetRandomBytes<4>(); std::memcpy(local, rnd.data(), 4);
        uint8 zero[4] = { 0, 0, 0, 0 };
        Trinity::Crypto::SHA1 sha;
        sha.UpdateData(_acct); sha.UpdateData(zero, 4); sha.UpdateData(local, 4); sha.UpdateData(seed, 4);
        sha.UpdateData(_key335.data(), 40); sha.Finalize();

        // bloque de addons vacío: u32 tamaño sin comprimir + zlib( u32 numAddons=0, u32 hora=0 )
        uint8 plain[8] = { 0 }; uLongf clen = compressBound(8); std::vector<uint8> z(clen);
        compress(z.data(), &clen, plain, 8); z.resize(clen);

        Wr w;
        w.put<uint32>(12340).put<uint32>(0).cstr(_acct).put<uint32>(0).raw(local, 4)
         // el reino del worldserver (RealmID de su worldserver.conf, 1 por defecto), no el del cliente 3.4.3 (Cfg::RealmId):
         // AzerothCore rechaza la sesión con REALM_LIST_REALM_NOT_FOUND (39) si no coincide con el suyo
         .put<uint32>(0).put<uint32>(0).put<uint32>(uint32(ConfigAC("RealmID", 1))).put<uint64>(0).raw(sha.GetDigest().data(), 20)
         .put<uint32>(8).raw(z.data(), z.size());
        SrvSend(CMSG335_AUTH_SESSION, w.b);

        // a partir de aquí el worldserver cifra cabeceras (ARC4-drop1024); papeles de cliente
        uint8 const ServerEncryptionKey[16] = { 0xCC,0x98,0xAE,0x04,0xE8,0x97,0xEA,0xCA,0x12,0xDD,0xC0,0x93,0x42,0x91,0x53,0x57 };
        uint8 const ServerDecryptionKey[16] = { 0xC2,0xB3,0x72,0x3C,0xC6,0xAE,0xD9,0xB5,0x34,0x3C,0x53,0xEE,0x2F,0x43,0x67,0xCE };
        _srvDecrypt.Init(Trinity::Crypto::HMAC_SHA1::GetDigestOf(ServerEncryptionKey, _key335));
        _srvEncrypt.Init(Trinity::Crypto::HMAC_SHA1::GetDigestOf(ServerDecryptionKey, _key335));
        std::array<uint8, 1024> drop{};
        _srvDecrypt.UpdateData(drop); drop.fill(0); _srvEncrypt.UpdateData(drop);
        _arcOn = true;

        for (;;)
        {
            SrvRecv(op, body);
            if (op == SMSG335_AUTH_RESPONSE)
            {
                uint8 res = body.empty() ? 0xFF : body[0];
                if (res != 0x0C) throw std::runtime_error("el worldserver rechazó la sesión, código " + std::to_string(res));
                break;
            }
        }
        Log("[%s] dentro del worldserver de AzerothCore (3.3.5)", _acct.c_str());
        auto self = shared_from_this();
        _srvThread = std::thread([self]() { self->ServerLoop(); });
    }

    void ServerLoop()
    {
        try
        {
            uint16 op; std::vector<uint8> body;
            std::deque<std::pair<uint16, std::vector<uint8>>> dentro;   // subpaquetes de SMSG_MULTIPLE_MOVES
            while (!_stop)
            {
                if (!dentro.empty()) { op = dentro.front().first; body = std::move(dentro.front().second); dentro.pop_front(); }
                else SrvRecv(op, body);
                if (g_traza) Log("[traza] 3.3.5 -> 0x%03X (%u)", op, (uint32)body.size());
                if (g_apagados.count(op)) continue;
                if (op == 0x51E)                         // SMSG_MULTIPLE_MOVES: u32 tamaño, (u8 tamaño, u16 opcode, cuerpo)*
                {
                    size_t p = 4;
                    while (p + 3 <= body.size())
                    {
                        uint8 n = body[p]; uint16 o = uint16(body[p + 1] | (body[p + 2] << 8));
                        if (n < 2 || p + 1 + n > body.size()) break;
                        dentro.emplace_back(o, std::vector<uint8>(body.begin() + p + 3, body.begin() + p + 1 + n));
                        p += 1 + n;
                    }
                    continue;
                }
                try {
                switch (op)
                {
                    case SMSG335_CHAR_ENUM: TranslateCharEnum(body); break;
                    case 0x236:                  // SMSG_LOGIN_VERIFY_WORLD 3.3.5: u32 mapa, x, y, z, o
                    {
                        Rd r(body);
                        WorldPackets::Character::LoginVerifyWorld lvw;
                        lvw.MapID = int32(r.get<uint32>());
                        float x = r.get<float>(), y = r.get<float>(), z = r.get<float>(), o = r.get<float>();
                        lvw.Pos.Pos.Relocate(x, y, z, o);
                        Log("[%s] LOGIN_VERIFY_WORLD mapa %d (%.1f, %.1f, %.1f)", _acct.c_str(), lvw.MapID, x, y, z);
                        CliSend(lvw.Write());
                        // como TrinityCore: hechizos, barra de acción, facciones... tienen que llegar ANTES de crear al jugador
                        // (si llegan después el cliente no los asocia: sin hechizos ni idioma). Se crea con el primer UPDATE_OBJECT.
                        _crearPendiente = true; _crearMapa = lvw.MapID; _crearPos[0] = x; _crearPos[1] = y; _crearPos[2] = z; _crearPos[3] = o;
                        break;
                    }
                    case 0x0A9: case 0x1F6:      // SMSG_(COMPRESSED_)UPDATE_OBJECT
                        if (_crearPendiente)             // como TrinityCore: el jugador se crea ya con sus valores (XP, descanso, habilidades...)
                        {
                            // AC manda primero los objetos del inventario y después el bloque del jugador: se retiene todo
                            // hasta que llega ese bloque (o, por si acaso, hasta 8 paquetes) y el jugador sale el primero
                            _mundo.retener = true;
                            _mundo.Paquete(op, body);
                            _mundo.retener = false;
                            if (_mundo.yoRecibido || ++_paquetesAntesDeCrear >= 8)
                            {
                                _crearPendiente = false; _paquetesAntesDeCrear = 0;
                                CrearPersonaje(_crearMapa, _crearPos[0], _crearPos[1], _crearPos[2], _crearPos[3]);
                                _mundo.CrearPendientes();   // objetos que llegaron antes que el jugador
                                for (WorldPacket const& p : _mundo.retenidos) CliSend(&p);
                                _mundo.retenidos.clear();
                                _mundo.InventarioYo();   // ahora que el cliente tiene los objetos
                                _juego.ReenviarBotones();   // la barra de acción, que la creación dejó vacía
                                _juego.PedirMisionesHechas();   // bits de misiones completadas (QuestCompleted)
                                {   // AC no manda la dificultad al entrar: sin esto el menú del retrato no marca la guardada
                                    WorldPackets::Misc::DungeonDifficultySet dd; dd.DifficultyID = (_modosDificultad & 0x0F) ? 2 : 1;
                                    CliSend(dd.Write());
                                    WorldPackets::Misc::RaidDifficultySet rd; rd.DifficultyID = int32((_modosDificultad >> 4) & 0x03) + 3; rd.Legacy = 0;
                                    CliSend(rd.Write());
                                }
                            }
                            break;
                        }
                        _mundo.Paquete(op, body);
                        if (_mundo.recreadoYo) { _mundo.recreadoYo = false; _juego.ReenviarBotones(); }   // cambio de mapa: la barra otra vez
                        break;
                    case 0x061: TranslateCreatureQuery(body); break;
                    case 0x209:                  // SMSG_ACCOUNT_DATA_TIMES: u32 hora, u8 1, u32 máscara, u32 hora por bit
                    {
                        Rd r(body);
                        WorldPackets::ClientConfig::AccountDataTimes adt;
                        adt.ServerTime = time_t(r.get<uint32>());
                        r.get<uint8>();
                        uint32 mask = r.get<uint32>();
                        for (uint32 i = 0; i < 8; ++i) if (mask & (1u << i)) adt.AccountTimes[i] = time_t(r.get<uint32>());
                        if (_tcPlayer || _loginGuid) adt.PlayerGuid = ObjectGuid::Create<HighGuid::Player>(_loginGuid);
                        CliSend(adt.Write());
                        Log("[%s] ACCOUNT_DATA_TIMES %s (máscara 0x%X)", _acct.c_str(), adt.PlayerGuid.IsEmpty() ? "de cuenta" : "del personaje", mask);
                        break;
                    }
                    case 0x20C:                  // SMSG_UPDATE_ACCOUNT_DATA: u64 guid, u32 tipo, u32 hora, u32 tamaño, zlib
                    {
                        Rd r(body);
                        WorldPackets::ClientConfig::UpdateAccountData ua;
                        uint64 g = r.get<uint64>();
                        if (g) ua.Player = ObjectGuid::Create<HighGuid::Player>(uint32(g));
                        ua.DataType = uint8(r.get<uint32>());
                        ua.Time = time_t(r.get<uint32>());
                        ua.Size = r.get<uint32>();
                        if (r.p < body.size()) ua.CompressedData.append(body.data() + r.p, body.size() - r.p);
                        CliSend(ua.Write());
                        break;
                    }
                    case 0x463: break;           // SMSG_UPDATE_ACCOUNT_DATA_COMPLETE
                    case 0x0DD: _mundo.MonsterMove335(body); break;                 // SMSG_MONSTER_MOVE
                    case 0x2AE: _mundo.MonsterMove335(body, true); break;           // SMSG_MONSTER_MOVE_TRANSPORT
                    case 0x0C7: _mundo.Teleport335(body); break;                    // MSG_MOVE_TELEPORT_ACK (propio)
                    case 0x0C5: _mundo.TeleportOtro335(body); break;                // MSG_MOVE_TELEPORT (otros)
                    case 0x03F: _mundo.Pendiente335(body); break;                   // SMSG_TRANSFER_PENDING
                    case 0x03E: _mundo.NuevoMundo335(body); break;                  // SMSG_NEW_WORLD
                    case 0x0EF: _mundo.Empujon335(body); break;                     // SMSG_MOVE_KNOCK_BACK
                    case 0x0B5: case 0x0B6: case 0x0B7: case 0x0B8: case 0x0B9: case 0x0BA: case 0x0BB: case 0x0BC:
                    case 0x0BD: case 0x0BE: case 0x0BF: case 0x0C0: case 0x0C1: case 0x0C2: case 0x0C3: case 0x0C9:
                    case 0x0CA: case 0x0CB: case 0x0DA: case 0x0DB: case 0x0EE: case 0x359: case 0x35A: case 0x3A7:
                    case 0x0EC: case 0x0ED: case 0x0F1: case 0x0F7: case 0x2B0: case 0x2B1: case 0x3AD: case 0x4D2: case 0x518:
                        _mundo.Movimiento335(body);                                   // MSG_MOVE_* de otros (también raíz, levitar, agua, vuelo...)
                        break;
                    case 0x390:                  // SMSG_TIME_SYNC_REQ: u32 contador
                    {
                        Rd r(body);
                        WorldPackets::Misc::TimeSyncRequest ts; ts.SequenceIndex = r.get<uint32>();
                        CliSend(ts.Write());
                        break;
                    }
                    case 0x1DD: break;           // SMSG_PONG 3.3.5 (el cliente ya tiene el suyo)
                    case 0x05F: TranslateGameObjectQuery(body); break;
                    case 0x051: TranslateNameQuery(body); break;
                    case 0x0AA:                  // SMSG_DESTROY_OBJECT
                        _mundo.Destruir(body);
                        break;
                    case SMSG335_CHAR_CREATE:
                    {
                        WorldPackets::Character::CreateChar cc; cc.Code = CodigoCrear(body.empty() ? 0x31 : body[0]);
                        Log("[%s] crear personaje: código 3.3.5 0x%02X -> %u", _acct.c_str(), body.empty() ? 0 : body[0], cc.Code);
                        CliSend(cc.Write());
                        break;
                    }
                    case 0x2C8:                  // SMSG_CHAR_RENAME: u8 código, [u64, cstr]
                    {
                        Rd r(body); uint8 c = r.get<uint8>();
                        WorldPackets::Character::CharacterRenameResult p; p.Result = CodigoCrear(c);
                        if (c == 0) { uint64 g = r.get<uint64>(); p.Guid = ObjectGuid::Create<HighGuid::Player>(g); p.Name = r.cstr(); _pjs[g].first = p.Name; }
                        else p.Guid = ObjectGuid::Create<HighGuid::Player>(_pjServicio);
                        CliSend(p.Write());
                        break;
                    }
                    case 0x474:                  // SMSG_CHAR_CUSTOMIZE: u8 código, [u64, cstr, u8 sexo, piel, cara, peinado, color, vello]
                    case 0x4DA:                  // SMSG_CHAR_FACTION_CHANGE: igual + u8 raza
                    {
                        Rd r(body); uint8 c = r.get<uint8>();
                        if (c != 0)
                        {
                            if (op == 0x474) { WorldPackets::Character::CharCustomizeFailure p; p.Result = CodigoCrear(c); p.CharGUID = ObjectGuid::Create<HighGuid::Player>(_pjServicio); CliSend(p.Write()); }
                            else { WorldPackets::Character::CharFactionChangeResult p; p.Result = CodigoCrear(c); p.Guid = ObjectGuid::Create<HighGuid::Player>(_pjServicio); CliSend(p.Write()); }
                            break;
                        }
                        uint64 g = r.get<uint64>(); std::string nombre = r.cstr(); uint8 sexo = r.get<uint8>();
                        uint8 v[5]; v[0] = r.get<uint8>(); v[1] = r.get<uint8>(); v[2] = r.get<uint8>(); v[3] = r.get<uint8>(); v[4] = r.get<uint8>();
                        uint8 raza = op == 0x4DA ? r.get<uint8>() : _pjs[g].second;
                        _pjs[g] = { nombre, raza };
                        WorldPackets::Character::CharCustomizeInfo ci;
                        ci.CharGUID = ObjectGuid::Create<HighGuid::Player>(g); ci.SexID = sexo; ci.CharName = nombre;
                        for (UF::ChrCustomizationChoice const& ch : Apariencia::ToCustom(raza, sexo, v)) ci.Customizations.push_back(ch);
                        if (op == 0x474) { WorldPackets::Character::CharCustomizeSuccess p(&ci); CliSend(p.Write()); }
                        else
                        {
                            WorldPackets::Character::CharFactionChangeResult p; p.Result = 0; p.Guid = ci.CharGUID;
                            p.Display.emplace(); p.Display->Name = nombre; p.Display->SexID = sexo; p.Display->RaceID = raza; p.Display->Customizations = &ci.Customizations;
                            CliSend(p.Write());
                        }
                        break;
                    }
                    case 0x41A:                  // SMSG_SET_PLAYER_DECLINED_NAMES_RESULT: u32 código, u64
                    {
                        Rd r(body);
                        WorldPackets::Character::SetPlayerDeclinedNamesResult p; p.ResultCode = int32(r.get<uint32>());
                        p.Player = ObjectGuid::Create<HighGuid::Player>(r.get<uint64>());
                        CliSend(p.Write());
                        break;
                    }
                    case 0x041:                  // SMSG_CHARACTER_LOGIN_FAILED: u8 código
                    {
                        WorldPackets::Character::CharacterLoginFailed p(WorldPackets::Character::LoginFailureReason::Failed);
                        CliSend(p.Write());
                        Log("[%s] AzerothCore rechaza la entrada al mundo (código 0x%02X)", _acct.c_str(), body.empty() ? 0 : body[0]);
                        break;
                    }
                    case SMSG335_CHAR_DELETE:
                    {
                        WorldPackets::Character::DeleteChar dc; dc.Code = CodigoCrear(body.empty() ? 0x48 : body[0]);
                        CliSend(dc.Write());
                        break;
                    }
                    default:                     // todavía sin traducir: se anota una vez por opcode
                        if (Mundo::EsVelocidad(op)) { _mundo.Velocidad335(op, body); break; }
                        if (_mundo.Estado335(op, body)) break;
                        if (op == 0x0FD && !_juego.mundo) { _tutorial335 = body; break; }   // llega al abrir la sesión 3.3.5: se reenvía al entrar
                        if (_juego.mundo && _juego.Servidor(op, body)) break;
                        if (_vistos335.insert(op).second)
                            Log("[%s] worldserver -> opcode 3.3.5 0x%03X (%u bytes) sin traducir", _acct.c_str(), op, (uint32)body.size());
                        break;
                }
                }
                catch (std::exception const& e)
                {
                    Log("[%s] ERROR traduciendo 3.3.5 0x%03X (%u bytes): %s", _acct.c_str(), op, (uint32)body.size(), e.what());
                }
            }
        }
        catch (std::exception const& e) { if (!_stop) Log("[%s] worldserver: %s", _acct.c_str(), e.what()); }
    }

    // SMSG_CHAR_ENUM 3.3.5 -> SMSG_ENUM_CHARACTERS_RESULT 3.4.3
    void TranslateCharEnum(std::vector<uint8> const& body)
    {
        Rd r(body);
        uint8 count = r.get<uint8>();
        WorldPackets::Character::EnumCharactersResult res;
        res.Success = true;
        res.MaxCharacterLevel = 80;
        for (uint8 race : { 1, 2, 3, 4, 5, 6, 7, 8, 10, 11 })   // como race_unlock_requirement de TrinityCore: todas desbloqueadas
        {
            WorldPackets::Character::EnumCharactersResult::RaceUnlock ru;
            ru.RaceID = race; ru.HasExpansion = true;
            res.RaceUnlockData.push_back(ru);
        }
        for (uint8 i = 0; i < count; ++i)
        {
            WorldPackets::Character::EnumCharactersResult::CharacterInfo ci;
            uint64 guid = r.get<uint64>();
            ci.Guid = ObjectGuid::Create<HighGuid::Player>(guid & 0xFFFFFFFF);
            ci.Name = r.cstr();
            ci.ListPosition = i;
            ci.RaceID = r.get<uint8>(); ci.ClassID = r.get<uint8>(); ci.SexID = r.get<uint8>();
            _pjs[guid & 0xFFFFFFFF] = { ci.Name, ci.RaceID };
            uint8 legacy[5]; for (uint8& b : legacy) b = r.get<uint8>();                            // piel, cara, peinado, color, vello
            ci.Customizations = Apariencia::ToCustom(ci.RaceID, ci.SexID, legacy);
            ci.ExperienceLevel = r.get<uint8>();
            ci.ZoneID = int32(r.get<uint32>()); ci.MapID = int32(r.get<uint32>());
            // el cliente 3.4.3 se cierra en la lista ("Bad zone ID") si la zona o el mapa no están en sus DB2 (contenido propio):
            // solo para esta vista previa se cambia por uno que sí tenga
            if (!sMapStore.HasRecord(uint32(ci.MapID))) { Log("[%s] %s está en el mapa %d, que el cliente 3.4.3 no tiene: la lista enseña el 0", _acct.c_str(), ci.Name.c_str(), ci.MapID); ci.MapID = 0; ci.ZoneID = 12; }
            else if (ci.ZoneID && !sAreaTableStore.HasRecord(uint32(ci.ZoneID))) { Log("[%s] %s está en la zona %d, que el cliente 3.4.3 no tiene", _acct.c_str(), ci.Name.c_str(), ci.ZoneID); ci.ZoneID = 0; }
            float x = r.get<float>(), y = r.get<float>(), z = r.get<float>();
            ci.PreloadPos.Pos.Relocate(x, y, z);
            r.get<uint32>();                                                                     // guild id
            ci.Flags = r.get<uint32>();
            ci.Flags2 = r.get<uint32>();
            ci.FirstLogin = r.get<uint8>() != 0;
            ci.PetCreatureDisplayID = r.get<uint32>(); ci.PetExperienceLevel = r.get<uint32>(); ci.PetCreatureFamilyID = r.get<uint32>();
            for (uint32 s = 0; s < 23; ++s)
            {
                ci.VisualItems[s].DisplayID = r.get<uint32>();
                ci.VisualItems[s].InvType = r.get<uint8>();
                ci.VisualItems[s].DisplayEnchantID = r.get<uint32>();
            }
            res.Characters.push_back(std::move(ci));
        }
        CliSend(res.Write());
        Log("[%s] lista de personajes: %u", _acct.c_str(), (uint32)count);
    }

    tcp::socket _cli, _srv;
    std::mutex _cliSend, _srvSend;
    std::thread _srvThread;
    std::atomic<bool> _stop{ false };
    WorldPacketCrypt _crypt;
    std::array<uint8, 16> _serverChallenge{};
    std::array<uint8, 40> _sessionKey{};
    std::array<uint8, 16> _encryptKey{};
    std::array<uint8, 40> _key335{};
    Trinity::Crypto::ARC4 _srvEncrypt, _srvDecrypt;
    bool _arcOn = false;
    uint32 _accountId = 0;
    std::shared_ptr<tcp::socket> _c1;
    WorldPacketCrypt _c1Crypt;
    std::mutex _c1Send;
    std::atomic<bool> _c1Ready{ false };
    uint64 _connectKey = 0;
    uint64 _loginGuid = 0;
    std::unique_ptr<WorldSession> _tcSession;
    std::unique_ptr<GatePlayer> _tcPlayer;
    std::unique_ptr<WorldSession> _sesionOtros;   // antes que _mundo: sus Player la usan hasta destruirse
    Mundo _mundo;
    Juego _juego;                                   // después de _mundo (lo usa)
    std::set<uint16> _vistosCli;
    std::unordered_map<uint64, std::pair<std::string, uint8>> _pjs;   // personajes de la lista: nombre y raza
    uint64 _pjServicio = 0; uint8 _razaServicio = 0;                  // último cambio de nombre/apariencia/facción pedido
    bool _movAvisado = false;
    std::mutex _goMutex;
    std::unordered_map<uint32, ObjectGuid> _goGuids;   // última guid consultada por entrada de objeto
    std::set<uint16> _vistos335;
    bool _crearPendiente = false; int32 _crearMapa = 0; float _crearPos[4] = { }; uint32 _paquetesAntesDeCrear = 0;   // creación del jugador aplazada al primer UPDATE_OBJECT
    std::set<uint32> _tablasPedidas;                 // tablas DB2 que el cliente ha pedido con CMSG_DB_QUERY_BULK
    std::vector<uint8> _tutorial335;                 // SMSG_TUTORIAL_FLAGS recibido antes de entrar al mundo
    std::string _acct = "?";
    bool _prueba = false;
    std::map<uint32, uint32> _enviados;            // modo prueba: opcode 3.4.3 -> veces
};

static std::vector<uint8> HexBytes(std::string const& s, Session& ses)
{
    std::vector<uint8> out; std::istringstream in(s); std::string tok;
    while (in >> tok)
    {
        if (tok[0] == '%')                               // guid empaquetada
        {
            uint64 g = ses.PruebaGuid(tok == "%yo" ? 0 : uint32(std::stoul(tok.substr(1))));
            uint8 m = 0; std::vector<uint8> bb;
            for (int i = 0; i < 8; ++i) if (uint8 x = uint8(g >> (i * 8))) { m |= uint8(1 << i); bb.push_back(x); }
            out.push_back(m); out.insert(out.end(), bb.begin(), bb.end());
            continue;
        }
        if (tok[0] == '@')
        {
            uint64 g = ses.PruebaGuid(tok == "@yo" ? 0 : uint32(std::stoul(tok.substr(1))));
            for (int i = 0; i < 8; ++i) out.push_back(uint8(g >> (i * 8)));
            continue;
        }
        for (size_t i = 0; i + 1 < tok.size(); i += 2) out.push_back(uint8(std::stoul(tok.substr(i, 2), nullptr, 16)));
    }
    return out;
}

// ------------------------------------------------------------------------------------------------ objetos custom -> hotfix
static int GenerarObjetos(char const* salida)
{
    Db db;
    if (!db.Open()) { std::printf("sin BD\n"); return 1; }
    // apariencia ya existente en el cliente para cada modelo (ItemDisplayInfoID) y su icono
    std::unordered_map<uint32, std::pair<uint32, int32>> apariencia;
    for (ItemAppearanceEntry const* a : sItemAppearanceStore)
        if (!apariencia.count(a->ItemDisplayInfoID)) apariencia[a->ItemDisplayInfoID] = { a->ID, a->DefaultIconFileDataID };
    std::unordered_map<uint32, std::string> esES;
    if (!mysql_query(db.h, (std::string("SELECT ID, Name FROM ") + Cfg::WorldDbName + ".item_template_locale WHERE locale = 'esES'").c_str()))
        if (MYSQL_RES* res = mysql_store_result(db.h))
        {
            while (MYSQL_ROW row = mysql_fetch_row(res)) if (row[1] && *row[1]) esES[uint32(std::stoul(row[0]))] = row[1];
            mysql_free_result(res);
        }

    char const* cols = "entry,class,subclass,SoundOverrideSubclass,name,displayid,Quality,Flags,FlagsExtra,BuyCount,BuyPrice,SellPrice,InventoryType,"
        "AllowableClass,AllowableRace,ItemLevel,RequiredLevel,RequiredSkill,RequiredSkillRank,requiredspell,requiredhonorrank,"
        "RequiredReputationFaction,RequiredReputationRank,maxcount,stackable,ContainerSlots,"
        "stat_type1,stat_value1,stat_type2,stat_value2,stat_type3,stat_value3,stat_type4,stat_value4,stat_type5,stat_value5,"
        "stat_type6,stat_value6,stat_type7,stat_value7,stat_type8,stat_value8,stat_type9,stat_value9,stat_type10,stat_value10,"
        "ScalingStatDistribution,ScalingStatValue,dmg_min1,dmg_max1,dmg_type1,dmg_min2,dmg_max2,dmg_type2,"
        "armor,holy_res,fire_res,nature_res,frost_res,shadow_res,arcane_res,delay,ammo_type,RangedModRange,"
        "spellid_1,spelltrigger_1,spellcharges_1,spellcooldown_1,spellcategory_1,spellcategorycooldown_1,"
        "spellid_2,spelltrigger_2,spellcharges_2,spellcooldown_2,spellcategory_2,spellcategorycooldown_2,"
        "spellid_3,spelltrigger_3,spellcharges_3,spellcooldown_3,spellcategory_3,spellcategorycooldown_3,"
        "spellid_4,spelltrigger_4,spellcharges_4,spellcooldown_4,spellcategory_4,spellcategorycooldown_4,"
        "spellid_5,spelltrigger_5,spellcharges_5,spellcooldown_5,spellcategory_5,spellcategorycooldown_5,"
        "bonding,description,PageText,LanguageID,PageMaterial,startquest,lockid,Material,sheath,RandomProperty,RandomSuffix,"
        "itemset,MaxDurability,area,Map,BagFamily,TotemCategory,socketColor_1,socketColor_2,socketColor_3,socketBonus,GemProperties,"
        "duration,ItemLimitCategory,HolidayId";
    std::string q = std::string("SELECT ") + cols + " FROM " + Cfg::WorldDbName + ".item_template";
    if (mysql_query(db.h, q.c_str())) { std::printf("error SQL: %s\n", mysql_error(db.h)); return 1; }
    MYSQL_RES* res = mysql_store_result(db.h);
    if (!res) return 1;

    std::ofstream f(salida, std::ios::binary);
    f << "-- Generado por worldgate --generar-objetos: objetos de acore_world.item_template que el cliente 3.4.3 no tiene.\n"
         "-- VerifiedBuild = 0 (custom). Se aplica sobre la base de hotfixes de la pasarela.\n"
         "DELETE FROM item WHERE VerifiedBuild = 0; DELETE FROM item_sparse WHERE VerifiedBuild = 0; DELETE FROM item_sparse_locale WHERE VerifiedBuild = 0;\n"
         "DELETE FROM item_modified_appearance WHERE VerifiedBuild = 0; DELETE FROM item_appearance WHERE VerifiedBuild = 0; DELETE FROM item_effect WHERE VerifiedBuild = 0;\n";
    uint32 hechos = 0, nuevasApariencias = 0;
    // identificadores nuevos consecutivos tras el máximo del cliente: TrinityCore dimensiona el índice de cada almacén hasta el ID mayor
    uint32 sigApariencia = sItemAppearanceStore.GetNumRows(), sigModificada = sItemModifiedAppearanceStore.GetNumRows(), sigEfecto = sItemEffectStore.GetNumRows();
    std::unordered_map<uint32, uint32> aparienciaNueva;   // modelo -> apariencia creada
    auto esc = [&](char const* s) { return s ? db.Esc(s) : std::string(); };
    while (MYSQL_ROW row = mysql_fetch_row(res))
    {
        auto I = [&](int i) -> int64 { return row[i] ? std::stoll(row[i]) : 0; };
        auto F = [&](int i) -> double { return row[i] ? std::stod(row[i]) : 0.0; };
        uint32 entry = uint32(I(0));
        if (sItemSparseStore.HasRecord(entry)) continue;
        uint32 display = uint32(I(5));
        int k = 0;
        // índices de las columnas de arriba
        int const C_CLASS = 1, C_SUB = 2, C_SOUND = 3, C_NAME = 4, C_QUALITY = 6, C_FLAGS = 7, C_FLAGSX = 8, C_BUYCOUNT = 9, C_BUY = 10, C_SELL = 11,
                  C_INV = 12, C_ACLASS = 13, C_ARACE = 14, C_ILVL = 15, C_REQLVL = 16, C_SKILL = 17, C_SKILLRANK = 18, C_REQSPELL = 19, C_HONOR = 20,
                  C_REPFAC = 21, C_REPRANK = 22, C_MAXCOUNT = 23, C_STACK = 24, C_SLOTS = 25, C_STAT = 26, C_SSD = 46, C_SSV = 47, C_DMG = 48,
                  C_ARMOR = 54, C_DELAY = 61, C_AMMO = 62, C_RANGE = 63, C_SPELL = 64, C_BONDING = 94, C_DESC = 95, C_PAGE = 96, C_LANG = 97,
                  C_PAGEMAT = 98, C_STARTQ = 99, C_LOCK = 100, C_MATERIAL = 101, C_SHEATH = 102, C_RANDPROP = 103, C_RANDSUF = 104, C_SET = 105,
                  C_MAXDUR = 106, C_AREA = 107, C_MAP = 108, C_BAGFAM = 109, C_TOTEM = 110, C_SOCKET = 111, C_SOCKBONUS = 114, C_GEMPROP = 115,
                  C_DURATION = 116, C_LIMITCAT = 117, C_HOLIDAY = 118;
        (void)k;
        // apariencia: la del cliente si el modelo existe; si no, una nueva (sin modelo en el cliente, icono de interrogación)
        uint32 idApariencia = 0; int32 icono = 134400;
        auto ap = apariencia.find(display);
        if (ap != apariencia.end()) { idApariencia = ap->second.first; icono = ap->second.second ? ap->second.second : 134400; }
        else if (display)
        {
            auto an = aparienciaNueva.find(display);
            if (an != aparienciaNueva.end()) idApariencia = an->second;
            else
            {
                idApariencia = sigApariencia++;
                aparienciaNueva[display] = idApariencia;
                f << "INSERT INTO item_appearance (ID, DisplayType, ItemDisplayInfoID, DefaultIconFileDataID, UiOrder, VerifiedBuild) VALUES ("
                  << idApariencia << ",0," << display << ",134400,0,0);\n";
                ++nuevasApariencias;
            }
        }
        // Item.db2
        f << "INSERT INTO item (ID, ClassID, SubclassID, Material, InventoryType, RequiredLevel, SheatheType, RandomSelect, ItemRandomSuffixGroupID, "
             "SoundOverrideSubclassID, ScalingStatDistributionID, IconFileDataID, ItemGroupSoundsID, ContentTuningID, MaxDurability, AmmunitionType, "
             "ScalingStatValue, DamageType1, DamageType2, DamageType3, DamageType4, DamageType5, Resistances1, Resistances2, Resistances3, Resistances4, "
             "Resistances5, Resistances6, Resistances7, MinDamage1, MinDamage2, MinDamage3, MinDamage4, MinDamage5, MaxDamage1, MaxDamage2, MaxDamage3, "
             "MaxDamage4, MaxDamage5, VerifiedBuild) VALUES ("
          << entry << "," << I(C_CLASS) << "," << I(C_SUB) << "," << I(C_MATERIAL) << "," << I(C_INV) << "," << I(C_REQLVL) << "," << I(C_SHEATH) << ","
          << I(C_RANDPROP) << "," << I(C_RANDSUF) << "," << I(C_SOUND) << "," << I(C_SSD) << "," << icono << ",0,0," << I(C_MAXDUR) << "," << I(C_AMMO) << ","
          << I(C_SSV) << "," << I(C_DMG + 2) << "," << I(C_DMG + 5) << ",0,0,0,"
          << I(C_ARMOR) << "," << I(C_ARMOR + 1) << "," << I(C_ARMOR + 2) << "," << I(C_ARMOR + 3) << "," << I(C_ARMOR + 4) << "," << I(C_ARMOR + 5) << "," << I(C_ARMOR + 6) << ","
          << F(C_DMG) << "," << F(C_DMG + 3) << ",0,0,0," << F(C_DMG + 1) << "," << F(C_DMG + 4) << ",0,0,0,0);\n";
        // ItemSparse.db2
        f << "INSERT INTO item_sparse (ID, AllowableRace, Description, Display3, Display2, Display1, Display, DmgVariance, DurationInInventory, QualityModifier, "
             "BagFamily, StartQuestID, ItemRange, Stackable, MaxCount, MinReputation, RequiredAbility, SellPrice, BuyPrice, VendorStackCount, PriceVariance, "
             "PriceRandomValue, Flags1, Flags2, Flags3, Flags4, FactionRelated, ModifiedCraftingReagentItemID, ContentTuningID, PlayerLevelToItemLevelCurveID, "
             "MaxDurability, ItemNameDescriptionID, RequiredTransmogHoliday, RequiredHoliday, LimitCategory, GemProperties, SocketMatchEnchantmentId, "
             "TotemCategoryID, InstanceBound, ZoneBound1, ZoneBound2, ItemSet, LockID, PageID, ItemDelay, MinFactionID, RequiredSkillRank, RequiredSkill, "
             "ItemLevel, AllowableClass, ItemRandomSuffixGroupID, RandomSelect, MinDamage1, MinDamage2, MinDamage3, MinDamage4, MinDamage5, MaxDamage1, "
             "MaxDamage2, MaxDamage3, MaxDamage4, MaxDamage5, Resistances1, Resistances2, Resistances3, Resistances4, Resistances5, Resistances6, Resistances7, "
             "ScalingStatDistributionID, StatModifierBonusAmount1, StatModifierBonusAmount2, StatModifierBonusAmount3, StatModifierBonusAmount4, "
             "StatModifierBonusAmount5, StatModifierBonusAmount6, StatModifierBonusAmount7, StatModifierBonusAmount8, StatModifierBonusAmount9, "
             "StatModifierBonusAmount10, ExpansionID, ArtifactID, SpellWeight, SpellWeightCategory, SocketType1, SocketType2, SocketType3, SheatheType, "
             "Material, PageMaterialID, LanguageID, Bonding, DamageDamageType, StatModifierBonusStat1, StatModifierBonusStat2, StatModifierBonusStat3, "
             "StatModifierBonusStat4, StatModifierBonusStat5, StatModifierBonusStat6, StatModifierBonusStat7, StatModifierBonusStat8, StatModifierBonusStat9, "
             "StatModifierBonusStat10, ContainerSlots, RequiredPVPMedal, RequiredPVPRank, InventoryType, OverallQualityID, AmmunitionType, RequiredLevel, "
             "VerifiedBuild) VALUES ("
          << entry << "," << I(C_ARACE) << ",'" << esc(row[C_DESC]) << "','','',''," << "'" << esc(row[C_NAME]) << "',0," << I(C_DURATION) << ",0,"
          << I(C_BAGFAM) << "," << I(C_STARTQ) << "," << F(C_RANGE) << "," << I(C_STACK) << "," << I(C_MAXCOUNT) << "," << I(C_REPRANK) << "," << I(C_REQSPELL) << ","
          << I(C_SELL) << "," << I(C_BUY) << "," << I(C_BUYCOUNT) << ",1,1," << I(C_FLAGS) << "," << I(C_FLAGSX) << ",0,0,0,0,0,0,"
          << I(C_MAXDUR) << ",0,0," << I(C_HOLIDAY) << "," << I(C_LIMITCAT) << "," << I(C_GEMPROP) << "," << I(C_SOCKBONUS) << ","
          << I(C_TOTEM) << "," << I(C_MAP) << "," << I(C_AREA) << ",0," << I(C_SET) << "," << I(C_LOCK) << "," << I(C_PAGE) << "," << I(C_DELAY) << ","
          << I(C_REPFAC) << "," << I(C_SKILLRANK) << "," << I(C_SKILL) << "," << I(C_ILVL) << "," << I(C_ACLASS) << "," << I(C_RANDSUF) << "," << I(C_RANDPROP) << ","
          << F(C_DMG) << "," << F(C_DMG + 3) << ",0,0,0," << F(C_DMG + 1) << "," << F(C_DMG + 4) << ",0,0,0,"
          << I(C_ARMOR) << "," << I(C_ARMOR + 1) << "," << I(C_ARMOR + 2) << "," << I(C_ARMOR + 3) << "," << I(C_ARMOR + 4) << "," << I(C_ARMOR + 5) << "," << I(C_ARMOR + 6) << ","
          << I(C_SSD);
        for (int s = 0; s < 10; ++s) f << "," << I(C_STAT + s * 2 + 1);
        f << ",0,0,0,0," << I(C_SOCKET) << "," << I(C_SOCKET + 1) << "," << I(C_SOCKET + 2) << "," << I(C_SHEATH) << "," << I(C_MATERIAL) << ","
          << I(C_PAGEMAT) << "," << I(C_LANG) << "," << I(C_BONDING) << "," << I(C_DMG + 2);
        for (int s = 0; s < 10; ++s) { int64 ty = I(C_STAT + s * 2), va = I(C_STAT + s * 2 + 1); f << "," << ((ty == 0 && va == 0) ? -1 : ty); }
        f << "," << I(C_SLOTS) << ",0," << I(C_HONOR) << "," << I(C_INV) << "," << I(C_QUALITY) << "," << I(C_AMMO) << "," << I(C_REQLVL) << ",0);\n";
        auto loc = esES.find(entry);
        if (loc != esES.end())
            f << "INSERT INTO item_sparse_locale (ID, locale, Description_lang, Display3_lang, Display2_lang, Display1_lang, Display_lang, VerifiedBuild) VALUES ("
              << entry << ",'esES','','','','','" << db.Esc(loc->second) << "',0);\n";
        if (idApariencia)
            f << "INSERT INTO item_modified_appearance (ID, ItemID, ItemAppearanceModifierID, ItemAppearanceID, OrderIndex, TransmogSourceTypeEnum, VerifiedBuild) VALUES ("
              << sigModificada++ << "," << entry << ",0," << idApariencia << ",0,0,0);\n";
        for (int s = 0; s < 5; ++s)
        {
            int c = C_SPELL + s * 6;
            if (!I(c)) continue;
            f << "INSERT INTO item_effect (ID, LegacySlotIndex, TriggerType, Charges, CoolDownMSec, CategoryCoolDownMSec, SpellCategoryID, SpellID, "
                 "ChrSpecializationID, ParentItemID, VerifiedBuild) VALUES ("
              << sigEfecto++ << "," << s << "," << I(c + 1) << "," << I(c + 2) << "," << I(c + 3) << "," << I(c + 5) << ","
              << I(c + 4) << "," << I(c) << ",0," << entry << ",0);\n";
        }
        ++hechos;
    }
    mysql_free_result(res);
    std::printf("%u objetos custom escritos en %s (%u apariencias nuevas)\n", hechos, salida, nuevasApariencias);
    std::fflush(stdout);
    return 0;
}

static int DiagnosticoCustom()
{
    Db db;
    if (!db.Open()) { std::printf("sin BD\n"); return 1; }
    std::printf("diagnóstico de contenido custom\n"); std::fflush(stdout);
    auto contar = [&](char const* nombre, std::string const& sql, auto existe)
    {
        if (mysql_query(db.h, sql.c_str())) { std::printf("%s: error SQL %s\n", nombre, mysql_error(db.h)); return; }
        MYSQL_RES* res = mysql_store_result(db.h); if (!res) return;
        uint32 total = 0, faltan = 0; std::string ejemplos;
        while (MYSQL_ROW row = mysql_fetch_row(res))
        {
            ++total;
            uint32 id = uint32(std::stoul(row[0]));
            if (!existe(id)) { if (faltan < 12) ejemplos += std::string(" ") + row[0] + (row[1] ? std::string("(") + row[1] + ")" : ""); ++faltan; }
        }
        mysql_free_result(res);
        std::printf("%-26s %7u en AC, %6u no están en el cliente 3.4.3. Ej.:%s\n", nombre, total, faltan, ejemplos.c_str());
        std::fflush(stdout);
    };
    contar("objetos (item_template)", (std::string("SELECT entry, name FROM ") + Cfg::WorldDbName + ".item_template").c_str(), [](uint32 id) { return sItemSparseStore.HasRecord(id); });
    contar("modelos de criatura", (std::string("SELECT DISTINCT CreatureDisplayID, NULL FROM ") + Cfg::WorldDbName + ".creature_template_model").c_str(), [](uint32 id) { return sCreatureDisplayInfoStore.HasRecord(id); });
    contar("modelos de GameObject", (std::string("SELECT DISTINCT displayId, NULL FROM ") + Cfg::WorldDbName + ".gameobject_template WHERE displayId <> 0").c_str(), [](uint32 id) { return sGameObjectDisplayInfoStore.HasRecord(id); });
    contar("hechizos (spell_dbc)", (std::string("SELECT ID, NULL FROM ") + Cfg::WorldDbName + ".spell_dbc").c_str(), [](uint32 id) { return sSpellNameStore.HasRecord(id); });
    contar("textos (npc_text sin BT)", (std::string("SELECT ID, NULL FROM ") + Cfg::WorldDbName + ".npc_text WHERE BroadcastTextID0 = 0").c_str(), [](uint32) { return false; });
    contar("broadcast_text", (std::string("SELECT ID, NULL FROM ") + Cfg::WorldDbName + ".broadcast_text").c_str(), [](uint32 id) { return sBroadcastTextStore.HasRecord(id); });
    contar("disparadores de área", (std::string("SELECT entry, map FROM ") + Cfg::WorldDbName + ".areatrigger").c_str(), [](uint32 id) { return sAreaTriggerStore.HasRecord(id); });
    contar("mazmorras del buscador", (std::string("SELECT dungeonId, name FROM ") + Cfg::WorldDbName + ".lfg_dungeon_template").c_str(), [](uint32 id) { return sLFGDungeonsStore.HasRecord(id); });
    {
        std::ofstream fc("criatura_modelos_faltan.txt");  // modelos de criatura que el cliente no tiene
        if (!mysql_query(db.h, (std::string("SELECT DISTINCT CreatureDisplayID FROM ") + Cfg::WorldDbName + ".creature_template_model").c_str()))
            if (MYSQL_RES* res = mysql_store_result(db.h))
            {
                while (MYSQL_ROW row = mysql_fetch_row(res)) { uint32 id = uint32(std::stoul(row[0])); if (!sCreatureDisplayInfoStore.HasRecord(id)) fc << id << "\n"; }
                mysql_free_result(res);
            }
        std::ofstream fm("criatura_modelos_cliente.txt"); // CreatureModelData del cliente 3.4.3: id;FileDataID (para reutilizarlos)
        for (CreatureModelDataEntry const* e : sCreatureModelDataStore) fm << e->ID << ";" << e->FileDataID << "\n";
        std::ofstream f("go_modelos_faltan.txt");         // modelos de GameObject que el cliente no tiene, para estudiarlos
        if (!mysql_query(db.h, (std::string("SELECT DISTINCT displayId FROM ") + Cfg::WorldDbName + ".gameobject_template WHERE displayId <> 0").c_str()))
            if (MYSQL_RES* res = mysql_store_result(db.h))
            {
                while (MYSQL_ROW row = mysql_fetch_row(res)) { uint32 id = uint32(std::stoul(row[0])); if (!sGameObjectDisplayInfoStore.HasRecord(id)) f << id << "\n"; }
                mysql_free_result(res);
            }
    }
    contar("mapas", (std::string("SELECT DISTINCT map, NULL FROM ") + Cfg::WorldDbName + ".creature").c_str(), [](uint32 id) { return sMapStore.HasRecord(id); });
    std::printf("hash de tabla GameObjectDisplayInfo 0x%08X, CreatureDisplayInfo 0x%08X, AreaTrigger 0x%08X\n",
        sGameObjectDisplayInfoStore.GetTableHash(), sCreatureDisplayInfoStore.GetTableHash(), sAreaTriggerStore.GetTableHash());
    for (CurrencyTypesEntry const* c : sCurrencyTypesStore)
        if (strstr(c->Name[Cfg::Db2Locale], "Honor") || strstr(c->Name[Cfg::Db2Locale], "Arena"))
            std::printf("moneda %u: %s (categoría %d)\n", c->ID, c->Name[Cfg::Db2Locale], c->CategoryID);
    {
        std::map<uint32, uint32> rutas;                  // transporte -> fotogramas de TransportAnimation en el cliente
        for (TransportAnimationEntry const* a : sTransportAnimationStore) ++rutas[a->TransportID];
        std::printf("TransportAnimation: %u filas, %u transportes. Ej.:", sTransportAnimationStore.GetNumRows(), uint32(rutas.size()));
        { int k = 0; for (auto const& [t, n] : rutas) { if (k++ < 30) std::printf(" %u(%u)", t, n); } std::printf("\n"); }
        contar("transportes (ruta)", (std::string("SELECT entry, name FROM ") + Cfg::WorldDbName + ".transports").c_str(), [&](uint32 id) { return rutas.count(id) != 0; });
        contar("transportes (TaxiPath)", (std::string("SELECT g.Data0, t.entry FROM ") + Cfg::WorldDbName + ".transports t JOIN " + Cfg::WorldDbName + ".gameobject_template g ON g.entry = t.entry").c_str(),
            [&](uint32 id) { return sTaxiPathStore.HasRecord(id); });
        contar("transportes (mapa propio)", (std::string("SELECT g.Data6, t.entry FROM ") + Cfg::WorldDbName + ".transports t JOIN " + Cfg::WorldDbName + ".gameobject_template g ON g.entry = t.entry WHERE g.Data6 <> 0").c_str(),
            [&](uint32 id) { return sMapStore.HasRecord(id); });
        contar("transportes (modelo)", (std::string("SELECT g.displayId, t.entry FROM ") + Cfg::WorldDbName + ".transports t JOIN " + Cfg::WorldDbName + ".gameobject_template g ON g.entry = t.entry").c_str(),
            [&](uint32 id) { return sGameObjectDisplayInfoStore.HasRecord(id); });
    }
    return 0;
}

static int ModoPrueba(boost::asio::io_context& io, std::string const& cuenta, uint64 guid, std::string const& guion)
{
    auto ses = std::make_shared<Session>(tcp::socket(io), io);
    try { ses->PruebaEntrar(cuenta, guid); }
    catch (std::exception const& e) { Log("[prueba] no entra: %s", e.what()); return 1; }
    std::ifstream f(guion); std::string l;
    while (std::getline(f, l))
    {
        if (l.empty() || l[0] == '#') continue;
        std::istringstream in(l); std::string cmd; in >> cmd;
        std::string resto; std::getline(in, resto);
        try
        {
            if (cmd == "espera") std::this_thread::sleep_for(std::chrono::milliseconds(std::stoul(resto)));
            else if (cmd == "lista") ses->PruebaLista();
            else if (cmd == "ir") ses->PruebaIr(uint32(std::stoul(resto)));
            else if (cmd == "335" || cmd == "cli")
            {
                std::istringstream r2(resto); std::string ops; r2 >> ops; std::string bytes; std::getline(r2, bytes);
                uint16 op = 0;
                if (cmd == "cli" && ops.rfind("CMSG_", 0) == 0)
                {
                    for (uint32 o = 0; o < 0x4000 && !op; ++o)
                        if (GetOpcodeNameForLogging(static_cast<OpcodeClient>(o)).rfind("[" + ops + " ", 0) == 0) op = uint16(o);
                    if (!op) { Log("[prueba] opcode de cliente desconocido: %s", ops.c_str()); continue; }
                }
                else op = uint16(std::stoul(ops, nullptr, 16));
                std::vector<uint8> b = HexBytes(bytes, *ses);
                Log("[prueba] %s 0x%04X (%u bytes)", cmd.c_str(), op, (uint32)b.size());
                if (cmd == "335") ses->PruebaSrv(op, b); else ses->PruebaCli(op, b);
            }
        }
        catch (std::exception const& e) { Log("[prueba] línea '%s': %s", l.c_str(), e.what()); }
    }
    ses->PruebaFin();
    return 0;
}

int main(int argc, char** argv)
{
    // worldgate.conf junto al exe (clave = valor, # comentario): direcciones, puertos, BD y carpeta de DB2.
    // Sin él quedan los valores de Cfg; worldgate_bd.txt (abajo) sigue valiendo para cambiar solo los nombres de las BD
    if (std::ifstream f("worldgate.conf"); f)
    {
        auto recorta = [](std::string x) { size_t a = x.find_first_not_of(" \t\r\""), b = x.find_last_not_of(" \t\r\""); return a == std::string::npos ? std::string() : x.substr(a, b - a + 1); };
        std::string l;
        while (std::getline(f, l))
        {
            auto eq = l.find('=');
            if (l.empty() || l[0] == '#' || eq == std::string::npos) continue;
            std::string k = recorta(l.substr(0, eq)), v = recorta(l.substr(eq + 1));
            auto num = [&](auto& d) { try { d = decltype(d + 0)(std::stoul(v)); } catch (...) { std::printf("worldgate.conf: %s no es un número\n", k.c_str()); } };
            if (k == "ListenIP") Cfg::ListenIP = v;
            else if (k == "ListenPort") num(Cfg::ListenPort);
            else if (k == "InstancePort") num(Cfg::InstancePort);
            else if (k == "WorldServerIP") Cfg::WorldIP = v;
            else if (k == "WorldServerPort") num(Cfg::WorldPort);
            else if (k == "DatabaseHost") Cfg::DbHost = v;
            else if (k == "DatabasePort") num(Cfg::DbPort);
            else if (k == "DatabaseUser") Cfg::DbUser = v;
            else if (k == "DatabasePassword") Cfg::DbPass = v;
            else if (k == "AuthDatabase") Cfg::DbName = v;
            else if (k == "CharacterDatabase") Cfg::CharDbName = v;
            else if (k == "WorldDatabase") Cfg::WorldDbName = v;
            else if (k == "HotfixDatabase") Cfg::HotfixDbName = v;
            else if (k == "TcAuthDatabase") Cfg::TcAuthDbName = v;
            else if (k == "Locale") Cfg::Locale = v;
            else if (k == "DataDir") { Cfg::DataDir = v; if (!v.empty() && v.back() != '/' && v.back() != '\\') Cfg::DataDir += '/'; }
            else if (k == "RealmId") num(Cfg::RealmId);
            else if (k == "RealmName") Cfg::RealmName = v;
            else if (k == "WorldServerConf") Cfg::WorldserverConf = v;
            else std::printf("worldgate.conf: clave desconocida %s\n", k.c_str());
        }
    }
    // nombres de las bases de AzerothCore (base limpia acore343_* o la copia custom acore_*): una línea clave=valor por base
    if (std::ifstream f("worldgate_bd.txt"); f)
    {
        std::string l;
        while (std::getline(f, l))
        {
            if (!l.empty() && l.back() == '\r') l.pop_back();
            auto eq = l.find('=');
            if (l.empty() || l[0] == '#' || eq == std::string::npos) continue;
            std::string k = l.substr(0, eq), v = l.substr(eq + 1);
            if (k == "auth") Cfg::DbName = v;
            else if (k == "personajes") Cfg::CharDbName = v;
            else if (k == "mundo") Cfg::WorldDbName = v;
            else if (k == "hotfixes") Cfg::HotfixDbName = v;
        }
    }
    std::printf("worldgate 3.4.3 -> AzerothCore 3.3.5  |  cliente %s:%u  |  worldserver %s:%u  |  BD %s:%u\n",
        Cfg::ListenIP.c_str(), Cfg::ListenPort, Cfg::WorldIP.c_str(), Cfg::WorldPort, Cfg::DbHost.c_str(), Cfg::DbPort);
    OpenSSLCrypto::threadsSetup(boost::dll::program_location().remove_filename());   // carga el proveedor legacy (ARC4)
    if (!WorldPackets::Auth::EnterEncryptedMode::InitializeEncryption())
    {
        std::printf("No pude cargar la clave Ed25519\n");
        return 1;
    }
    {
        MySQL::Library_Init();
        HotfixDatabase.SetConnectionInfo(Cfg::DbHost + ";" + std::to_string(Cfg::DbPort) + ";" + Cfg::DbUser + ";" + Cfg::DbPass + ";" + Cfg::HotfixDbName, 1, 1);
        if (HotfixDatabase.Open() || !HotfixDatabase.PrepareStatements()) { std::printf("ERROR: no abre %s\n", Cfg::HotfixDbName.c_str()); return 1; }
        // el Player/WorldSession de TrinityCore consultan permisos (RBAC) en la BD de login: la de referencia tc343_auth, no acore_auth
        LoginDatabase.SetConnectionInfo(Cfg::DbHost + ";" + std::to_string(Cfg::DbPort) + ";" + Cfg::DbUser + ";" + Cfg::DbPass + ";" + Cfg::TcAuthDbName, 1, 1);
        // con alguna BD ya abierta, salir de main deja colgados sus hilos y el mensaje no llega a verse: _Exit tras volcarlo
        if (LoginDatabase.Open() || !LoginDatabase.PrepareStatements()) { std::printf("ERROR: no abre %s (se crea con gateway/sql/tc343_auth.sql)\n", Cfg::TcAuthDbName.c_str()); std::fflush(stdout); std::_Exit(1); }
        sAccountMgr->LoadRBAC();
        std::fflush(stdout);
        // idioma de los DB2: el de worldgate.conf (Locale), o enUS si mapextractor lo sacó, o el primero que haya en
        // DataDir/dbc (un cliente en español solo trae dbc/esES; con enUS fijo no se cargaba ningún DB2)
        if (!Cfg::Locale.empty())
        {
            Cfg::Db2Locale = GetLocaleByName(Cfg::Locale);
            if (!IsValidLocale(Cfg::Db2Locale)) { std::printf("ERROR: Locale = %s no es un idioma válido (enUS, esES, deDE...)\n", Cfg::Locale.c_str()); std::fflush(stdout); std::_Exit(1); }
        }
        else if (!std::filesystem::is_directory(Cfg::DataDir + "dbc/enUS"))
        {
            std::error_code ec;
            for (auto const& d : std::filesystem::directory_iterator(Cfg::DataDir + "dbc", ec))
            {
                LocaleConstant l = GetLocaleByName(d.path().filename().string());
                if (d.is_directory() && IsValidLocale(l)) { Cfg::Db2Locale = l; break; }
            }
        }
        uint32 locales = sDB2Manager.LoadStores(Cfg::DataDir, Cfg::Db2Locale);
        if (!locales)
        {
            std::printf("ERROR: no hay DB2 del cliente en %sdbc/%s/ (ejecuta mapextractor -e 2 en la carpeta del cliente 3.4.3 y revisa DataDir y Locale en worldgate.conf)\n",
                        Cfg::DataDir.c_str(), localeNames[Cfg::Db2Locale]);
            std::fflush(stdout);
            std::_Exit(1);
        }
        std::printf("DB2 del cliente cargados desde %s (idioma %s, máscara de idiomas 0x%X)\n", Cfg::DataDir.c_str(), localeNames[Cfg::Db2Locale], locales);
        sDB2Manager.LoadHotfixBlob(locales);
        sDB2Manager.LoadHotfixData(locales);
        sDB2Manager.LoadHotfixOptionalData(locales);
        uint32 propios = 0;
        for (auto const& [id, push] : sDB2Manager.GetHotfixData()) if (id >= Cfg::PrimerHotfixPropio) ++propios;
        std::printf("hotfix_data: %u envíos, %u propios (se anuncian al cliente)\n", (uint32)sDB2Manager.GetHotfixData().size(), propios);
    }
    if (!Apariencia::Load())
        std::printf("AVISO: no encuentro ChrCustomizationConversion.csv / ChrCustomizationChoice.csv junto al exe: sin apariencia\n");
    else
        std::printf("apariencia: %u conversiones, %u elecciones\n", (uint32)Apariencia::g_conv.size(), (uint32)Apariencia::g_choiceOption.size());
    mysql_library_init(0, nullptr, nullptr);
    {
        g_traza = std::ifstream("worldgate_traza.txt").good();
        g_misilMunicion = std::ifstream("worldgate_misil_municion.txt").good();
        if (g_misilMunicion) std::printf("EXPERIMENTO: AmmoDisplayID en los hechizos con misil (worldgate_misil_municion.txt)\n");
        if (g_traza) std::printf("traza de paquetes activada (worldgate_traza.txt)\n");
        std::ifstream f("worldgate_off.txt"); std::string l;
        while (std::getline(f, l)) if (!l.empty() && l[0] != '#') g_apagados.insert(uint16(std::stoul(l, nullptr, 16)));
        for (uint16 o : g_apagados) std::printf("opcode 3.3.5 0x%03X desactivado (worldgate_off.txt)\n", o);
    }
    opcodeTable.Initialize();
    if (!WorldPackets::Auth::ConnectTo::InitializeEncryption())
    {
        std::printf("No pude cargar la clave RSA de ConnectTo\n");
        return 1;
    }
    boost::asio::io_context io;
    if (argc >= 2 && std::string(argv[1]) == "--custom")
        std::_Exit(DiagnosticoCustom());
    if (argc >= 3 && std::string(argv[1]) == "--cond")        // qué exige una PlayerCondition (y su árbol de modificadores)
    {
        for (int a = 2; a < argc; ++a)
        {
            PlayerConditionEntry const* c = sPlayerConditionStore.LookupEntry(uint32(std::stoul(argv[a])));
            if (!c) { std::printf("condición %s: no existe\n", argv[a]); continue; }
            std::printf("condición %u: niveles %u-%u, expansión %d-%d, tier %d-%d, worldstate expr %u, árbol %u, fase %u, flags 0x%X, clases 0x%X\n",
                c->ID, c->MinLevel, c->MaxLevel, int32(c->MinExpansionLevel), int32(c->MaxExpansionLevel), int32(c->MinExpansionTier), int32(c->MaxExpansionTier),
                uint32(c->WorldStateExpressionID), c->ModifierTreeID, uint32(c->PhaseID), uint32(c->Flags), uint32(c->ClassMask));
            if (WorldStateExpressionEntry const* w = sWorldStateExpressionStore.LookupEntry(c->WorldStateExpressionID))
                std::printf("  expresión: %s\n", w->Expression);
            std::function<void(uint32, int)> arbol = [&](uint32 id, int nivel)
            {
                for (ModifierTreeEntry const* m : sModifierTreeStore)
                    if (m->ID == id || (nivel > 0 && false)) {}
                if (ModifierTreeEntry const* m = sModifierTreeStore.LookupEntry(id))
                    std::printf("  %*snodo %u: tipo %d, asset %d, secundario %d, operador %d, cantidad %d\n", nivel * 2, "", m->ID, m->Type, m->Asset, m->SecondaryAsset, int32(m->Operator), int32(m->Amount));
                for (ModifierTreeEntry const* m : sModifierTreeStore)
                    if (m->Parent == id) arbol(m->ID, nivel + 1);
            };
            if (c->ModifierTreeID) arbol(c->ModifierTreeID, 0);
        }
        std::_Exit(0);
    }
    if (argc >= 2 && std::string(argv[1]) == "--bm")          // filas de BattlemasterList del cliente
    {
        for (BattlemasterListEntry const* e : sBattlemasterListStore)
        {
            std::printf("lista %u '%s': tipo %d, niveles %d-%d, condición %d, flags 0x%X, mapas", e->ID, e->Name[Cfg::Db2Locale], int32(e->InstanceType),
                int32(e->MinLevel), int32(e->MaxLevel), int32(e->RequiredPlayerConditionID), uint32(uint8(e->Flags)));
            for (int16 m : e->MapID) if (m >= 0) std::printf(" %d", m);
            std::printf("\n");
        }
        std::_Exit(0);
    }
    if (argc >= 3 && std::string(argv[1]) == "--godisplay")  // FileDataID del modelo de cada GameObjectDisplayInfo
    {
        if (std::string(argv[2]) == "todos")
        {
            for (GameObjectDisplayInfoEntry const* e : sGameObjectDisplayInfoStore) std::printf("display %u: FileDataID %d\n", e->ID, e->FileDataID);
            std::_Exit(0);
        }
        for (int k = 2; k < argc; ++k)
        {
            uint32 d = uint32(std::stoul(argv[k]));
            GameObjectDisplayInfoEntry const* e = sGameObjectDisplayInfoStore.LookupEntry(d);
            std::printf("display %u: %s %d\n", d, e ? "FileDataID" : "NO EXISTE", e ? e->FileDataID : 0);
        }
        std::_Exit(0);
    }
    if (argc >= 5 && std::string(argv[1]) == "--transporte") // ruta, velocidad, aceleración: nodos de AC contra la ruta del cliente
    {
        uint32 ruta = uint32(std::stoul(argv[2]));
        GameObjectTemplate gt; std::memset(&gt.raw, 0, sizeof(gt.raw));
        gt.entry = 1; gt.type = GAMEOBJECT_TYPE_MAP_OBJ_TRANSPORT;
        gt.moTransport.taxiPathID = ruta; gt.moTransport.moveSpeed = uint32(std::stoul(argv[3])); gt.moTransport.accelRate = uint32(std::stoul(argv[4]));
        TransportTemplate tt;
        (sTransportMgr->*GenerarRutaTc())(&gt, &tt);
        RutaAc::Ruta ac;
        bool okAc = RutaAc::Generar(ruta, float(gt.moTransport.moveSpeed), float(gt.moTransport.accelRate), ac);
        CorrespondenciaTransporte corr;
        bool okC = okAc && corr.Construir(ac, tt);
        std::printf("ruta %u: AC %s vuelta %u ms, %u nodos | cliente vuelta %u ms, %u tramos | anclas %u%s\n", ruta, okAc ? "ok" : "FALLA", ac.periodo,
            (uint32)ac.nodos.size(), tt.TotalPathTime, (uint32)tt.PathLegs.size(), (uint32)corr.anclas.size(), okC ? "" : " (SIN anclar)");
        for (size_t i = 0; i < tt.PathLegs.size(); ++i)
            std::printf("  tramo %u: mapa %u, empieza %u ms, dura %u ms\n", (uint32)i, tt.PathLegs[i].MapId, tt.PathLegs[i].StartTimestamp, tt.PathLegs[i].Duration);
        auto donde = [&](uint32 t, uint32& mapa) { size_t leg = 0; TransportMovementState st = TransportMovementState::Moving;
            Optional<Position> p = tt.ComputePosition(t % tt.TotalPathTime, &st, &leg); mapa = leg < tt.PathLegs.size() ? tt.PathLegs[leg].MapId : 9999;
            return p ? *p : Position(); };
        for (size_t i = 0; i < ac.nodos.size(); ++i)
        {
            RutaAc::Nodo const& n = ac.nodos[i];
            uint32 c = corr.Convertir(n.llega), m = 0; Position p = donde(c, m);
            std::printf("  nodo %2u mapa %u (%.0f, %.0f, %.0f)%s%s llega %6u sale %6u -> cliente %6u: mapa %u (%.0f, %.0f, %.0f) dist %.1f\n", (uint32)i, n.mapa,
                n.pos.x, n.pos.y, n.pos.z, n.parada ? " PARADA" : "", n.salto ? " SALTO" : "", n.llega, n.sale, c, m, p.GetPositionX(), p.GetPositionY(), p.GetPositionZ(),
                m == n.mapa ? p.GetExactDist(n.pos.x, n.pos.y, n.pos.z) : -1.f);
        }
        for (auto const& [a, c] : corr.anclas) std::printf("  ancla AC %8.0f -> cliente %8.0f\n", a, c);
        std::_Exit(0);
    }
    if (argc >= 3 && std::string(argv[1]) == "--efectos")    // efectos de un hechizo en las DB2 del cliente (SpellEffect)
    {
        for (int k = 2; k < argc; ++k)
        {
            uint32 spell = uint32(std::stoul(argv[k]));
            for (SpellEffectEntry const* e : sSpellEffectStore)
                if (e->SpellID == spell)
                    std::printf("hechizo %u efecto %d: tipo %u aura %d misc %d/%d base %d trigger %d dificultad %d\n", spell, e->EffectIndex, e->Effect,
                        int32(e->EffectAura), e->EffectMiscValue[0], e->EffectMiscValue[1], e->EffectBasePoints, e->EffectTriggerSpell, e->DifficultyID);
        }
        std::_Exit(0);
    }
    if (argc >= 3 && std::string(argv[1]) == "--visual")     // filas de SpellXSpellVisual de un hechizo y si llevan misil
    {
        uint32 spell = uint32(std::stoul(argv[2]));
        for (SpellXSpellVisualEntry const* e : sSpellXSpellVisualStore)
        {
            if (e->SpellID != spell) continue;
            SpellVisualEntry const* sv = sSpellVisualStore.LookupEntry(e->SpellVisualID);
            uint32 misiles = 0;
            if (sv) if (auto const* m = sDB2Manager.GetSpellVisualMissiles(sv->SpellVisualMissileSetID)) misiles = uint32(m->size());
            std::printf("SpellXSpellVisual %u: dificultad %u, visual %u, prioridad %d, cond. lanzador %u/%u, cond. espectador %u/%u, set de misiles %u (%u misiles)\n",
                e->ID, uint32(e->DifficultyID), e->SpellVisualID, int32(e->Priority), e->CasterPlayerConditionID, e->CasterUnitConditionID,
                e->ViewerPlayerConditionID, e->ViewerUnitConditionID, sv ? uint32(sv->SpellVisualMissileSetID) : 0, misiles);
            if (sv) if (auto const* m = sDB2Manager.GetSpellVisualMissiles(sv->SpellVisualMissileSetID))
                for (SpellVisualMissileEntry const* x : *m)
                {
                    SpellVisualEffectNameEntry const* en = sSpellVisualEffectNameStore.LookupEntry(x->SpellVisualEffectNameID);
                    std::printf("  misil %u: efecto %u (modelo FileDataID %d, textura %d, tipo %u, escala %.2f, alfa %.2f), anclaje %d -> %d, movimiento %u, banderas 0x%X\n",
                        x->ID, x->SpellVisualEffectNameID, en ? en->ModelFileDataID : -1, en ? en->TextureFileDataID : -1, en ? en->Type : 0,
                        en ? en->Scale : 0.f, en ? en->Alpha : 0.f, int32(x->Attachment), int32(x->DestinationAttachment), uint32(x->SpellMissileMotionID), x->Flags);
                }
        }
        std::_Exit(0);
    }
    if (argc >= 3 && std::string(argv[1]) == "--generar-objetos")
        std::_Exit(GenerarObjetos(argv[2]));
    if (argc >= 5 && std::string(argv[1]) == "--prueba")
        return ModoPrueba(io, argv[2], std::stoull(argv[3]), argv[4]);
    tcp::acceptor acc(io, tcp::endpoint(boost::asio::ip::make_address(Cfg::ListenIP), Cfg::ListenPort));

    // segunda conexión del cliente: saludo, reto, CMSG_AUTH_CONTINUED_SESSION y se entrega a su sesión
    std::thread([&io]() {
        tcp::acceptor inst(io, tcp::endpoint(boost::asio::ip::make_address(Cfg::ListenIP), Cfg::InstancePort));
        for (;;)
        {
            auto s = std::make_shared<tcp::socket>(io);
            inst.accept(*s);
            s->set_option(tcp::no_delay(true));
            std::thread([s]() {
                try
                {
                    std::string ini = ServerInit + "\n";
                    boost::asio::write(*s, boost::asio::buffer(ini));
                    std::vector<char> init(ClientInit.size() + 1);
                    boost::asio::read(*s, boost::asio::buffer(init));
                    if (std::string(init.data(), ClientInit.size()) != ClientInit) throw std::runtime_error("saludo de instancia incorrecto");
                    WorldPacketCrypt plano; std::mutex m;
                    std::array<uint8, 16> challenge = Trinity::Crypto::GetRandomBytes<16>();
                    WorldPackets::Auth::AuthChallenge ch; ch.Challenge = challenge;
                    auto dos = Trinity::Crypto::GetRandomBytes<32>(); std::memcpy(ch.DosChallenge.data(), dos.data(), 32); ch.DosZeroBits = 1;
                    // envío/lectura sin cifrar con el mismo formato de cabecera
                    {
                        WorldPacket const* pk = ch.Write();
                        uint16 opc = uint16(pk->GetOpcode()); uint32 size = uint32(2 + pk->size());
                        std::vector<uint8> out(18 + pk->size(), 0); std::memcpy(out.data(), &size, 4); std::memcpy(out.data() + 16, &opc, 2);
                        std::memcpy(out.data() + 18, pk->contents(), pk->size());
                        boost::asio::write(*s, boost::asio::buffer(out));
                    }
                    uint8 hdr[18]; boost::asio::read(*s, boost::asio::buffer(hdr));
                    uint32 size; std::memcpy(&size, hdr, 4); uint16 opc; std::memcpy(&opc, hdr + 16, 2);
                    std::vector<uint8> body(size - 2); if (!body.empty()) boost::asio::read(*s, boost::asio::buffer(body));
                    if (opc != CMSG_AUTH_CONTINUED_SESSION) throw std::runtime_error("se esperaba CMSG_AUTH_CONTINUED_SESSION");
                    Rd r(body);
                    r.get<uint64>();                                        // DosResponse
                    uint64 key = r.get<uint64>();
                    std::array<uint8, 16> local; std::array<uint8, 24> digest;
                    for (uint8& b : local) b = r.get<uint8>();
                    for (uint8& b : digest) b = r.get<uint8>();
                    std::shared_ptr<Session> ses;
                    {
                        std::lock_guard<std::mutex> g(g_regMutex);
                        auto it = g_pendientes.find(key);
                        if (it != g_pendientes.end()) { ses = it->second.lock(); g_pendientes.erase(it); }
                    }
                    if (!ses) throw std::runtime_error("clave de ConnectTo desconocida");
                    ses->AttachInstance(s, challenge, key, local, digest);
                }
                catch (boost::system::system_error const& e)
                {
                    // al volver al menú AlSalir cierra este socket a propósito: la lectura pendiente acaba con uno de estos
                    auto c = e.code();
                    if (c != boost::asio::error::operation_aborted && c != boost::asio::error::bad_descriptor && c.value() != 10038 && c.value() != 10053)
                        Log("conexión de instancia: %s", e.what());
                }
                catch (std::exception const& e) { Log("conexión de instancia: %s", e.what()); }
            }).detach();
        }
    }).detach();
    for (;;)
    {
        tcp::socket s(io);
        acc.accept(s);
        s.set_option(tcp::no_delay(true));
        Log("conexión de %s", s.remote_endpoint().address().to_string().c_str());
        auto ses = std::make_shared<Session>(std::move(s), io);
        std::thread([ses]() { ses->Run(); }).detach();
    }
}
