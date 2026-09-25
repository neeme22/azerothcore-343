/*
 * Juego.h — H4: lógica de juego para worldgate (todo lo que no es crear objetos ni moverlos).
 *
 *   Servidor(op, cuerpo): paquete 3.3.5 de AzerothCore -> paquete(s) 3.4.3 de TrinityCore al cliente
 *   Cliente(op, cuerpo):  paquete 3.4.3 del cliente     -> paquete(s) 3.3.5 al worldserver
 * Devuelven false si el opcode no se traduce (quien llama lo anota).
 * Formatos 3.3.5: fuentes de AzerothCore (source\src\server\game). Formatos 3.4.3: clases WorldPackets::* de TrinityCore.
 * Se incluye desde Main.cpp después de Mundo.h.
 */
#pragma once

#include "ChannelPackets.h"
#include "ChatPackets.h"
#include "CombatLogPackets.h"
#include "CombatPackets.h"
#include "Tablas.h"
#include "MonturasVelocidad.h"
#include "AurasPuntos.h"
#include <queue>
#include "QuestPackets.h"
#include "ItemPackets.h"
#include "LootPackets.h"
#include "GameObjectPackets.h"
#include "NPCPackets.h"
#include "PetPackets.h"
#include "PartyPackets.h"
#include "ReputationPackets.h"
#include "SpellPackets.h"
#include "SystemPackets.h"
#include "TalentPackets.h"
#include "TicketPackets.h"
inline bool g_misilMunicion = false;   // worldgate_misil_municion.txt: experimento del proyectil (ver SpellGo)
#include "TaxiPackets.h"
#include "MailPackets.h"
#include "GuildPackets.h"
#include "SocialPackets.h"
#include "SocialMgr.h"
#include "DuelPackets.h"
#include "AreaTriggerPackets.h"
#include "TotemPackets.h"
#include "InstancePackets.h"
#include "MiscPackets.h"
#include "CollectionMgr.h"
#include "CharacterPackets.h"
#include "MovementPackets.h"
#include "AchievementPackets.h"
#include "EquipmentSetPackets.h"
#include "BankPackets.h"
#include "TradePackets.h"
#include "InspectPackets.h"
#include "AddonPackets.h"
#include "CalendarPackets.h"
#include "Util.h"
#include "VehiclePackets.h"
#include "LFGPacketsCommon.h"
#include "LFGPackets.h"
#include "PetitionPackets.h"
#include "LFG.h"
#include "Battleground.h"
#include "QueryPackets.h"
#include "AuthenticationPackets.h"
#include "WhoPackets.h"
#include "BattlegroundPackets.h"
#include "ObjectMgr.h"
#include "DB2Stores.h"

class Juego
{
public:
    std::function<void(WorldPacket const*)> Enviar;
    std::function<void(uint16, std::vector<uint8> const&)> Srv;
    Mundo* mundo = nullptr;
    std::string acct;
    uint64 yo335 = 0;
    uint32 realmAddress = 0;
    std::function<void()> AlSalir;                   // lo pone la sesión: limpiar al volver a la pantalla de personajes

    // caché de nombres de jugadores (3.3.5): la rellenan las respuestas de CMSG_NAME_QUERY
    void Nombre(uint64 g, std::string const& n)
    {
        std::vector<WorldPackets::Chat::Chat> pend;
        {
            std::lock_guard<std::mutex> l(_mNombres);
            _nombres[g] = n;
            auto it = _pendientes.find(g);
            if (it != _pendientes.end()) { pend = std::move(it->second); _pendientes.erase(it); }
        }
        for (auto& c : pend) { c.SenderName = n; Enviar(c.Write()); }
    }

    ObjectGuid Yo() const { return ObjectGuid::Create<HighGuid::Player>(uint32(yo335)); }

    // AzerothCore no deja hablar a un jugador en idioma 0 (universal): se usa el de su facción (7 común, 1 orco)
    uint32 IdiomaChat(int32 idioma)
    {
        if (idioma > 0) return uint32(idioma);
        uint8 raza = 0, clase = 0, sexo = 0;
        if (mundo && mundo->RazaClaseSexo(yo335, raza, clase, sexo))
            if (ChrRacesEntry const* cr = sChrRacesStore.LookupEntry(raza))
                return cr->Alliance == 1 ? 1u : 7u;
        return 7;
    }

    // ------------------------------------------------------------------------------------------ 3.3.5 -> 3.4.3
    bool Servidor(uint16 op, std::vector<uint8> const& b)
    {
        Rd r(b);
        switch (op)
        {
            case 0x12A: InitialSpells(r); return true;
            case 0x096: Chat335(r, false); return true;
            case 0x3B3: Chat335(r, true); return true;        // SMSG_GM_MESSAGECHAT
            case 0x33D:                                      // SMSG_MOTD: u32 n, cstr*
            {
                uint32 n = r.get<uint32>();
                for (uint32 i = 0; i < n && r.p < b.size(); ++i) Sistema(r.cstr());
                return true;
            }
            case 0x2A9:                                      // SMSG_CHAT_PLAYER_NOT_FOUND: cstr
            {
                WorldPackets::Chat::ChatPlayerNotfound p(r.cstr());
                Enviar(p.Write());
                return true;
            }
            case 0x105:                                      // SMSG_TEXT_EMOTE: u64 guid, u32 emote, u32 num, u32 largo, cstr nombre
            {
                WorldPackets::Chat::STextEmote p;
                p.SourceGUID = mundo->ToTc(r.get<uint64>());
                p.SourceAccountGUID = ObjectGuid::Create<HighGuid::WowAccount>(0);
                p.EmoteID = int32(r.get<uint32>());
                p.SoundIndex = int32(r.get<uint32>());
                Enviar(p.Write());
                return true;
            }
            case 0x099: Canal335(r); return true;             // SMSG_CHANNEL_NOTIFY
            // ---- H7a
            case 0x26F: Establo335(r); return true;           // MSG_LIST_STABLED_PETS
            case 0x273:                                      // SMSG_STABLE_RESULT: u8 (mismos códigos que StableResult de 3.4.3)
            {
                WorldPackets::Pet::PetStableResult p; p.Result = r.get<uint8>(); Enviar(p.Write());
                return true;
            }
            case 0x1F1:                                      // MSG_SAVE_GUILD_EMBLEM: u32 error
            {
                WorldPackets::Guild::PlayerSaveGuildEmblem p; p.Error = int32(r.get<uint32>()); Enviar(p.Write());
                return true;
            }
            case 0x065:                                      // SMSG_WHOIS: cstr
            {
                WorldPackets::Who::WhoIsResponse p; p.AccountName = r.cstr(); Enviar(p.Write());
                return true;
            }
            case 0x3C8:                                      // SMSG_COMPLAIN_RESULT: u8
            {
                WorldPackets::Ticket::ComplaintResult p; p.Result = r.get<uint8>(); Enviar(p.Write());
                return true;
            }
            case 0x4B2:                                      // SMSG_ITEM_REFUND_INFO_RESPONSE: u64, u32 oro, honor, arena, 5 x (u32, u32), u32, u32 tiempo
            {
                WorldPackets::Item::SetItemPurchaseData p;
                p.ItemGUID = mundo->ToTc(r.get<uint64>());
                ContenidoCompra(r, p.Contents);
                r.get<uint32>(); p.PurchaseTime = r.get<uint32>();
                Enviar(p.Write());
                return true;
            }
            case 0x4B5:                                      // SMSG_ITEM_REFUND_RESULT: u64, u32 error, [contenido]
            {
                WorldPackets::Item::ItemPurchaseRefundResult p;
                p.ItemGUID = mundo->ToTc(r.get<uint64>());
                p.Result = uint8(r.get<uint32>());
                if (!p.Result && r.p + 12 <= r.b.size()) { p.Contents.emplace(); ContenidoCompra(r, *p.Contents); }
                Enviar(p.Write());
                return true;
            }
            case 0x402: ImagenReflejada335(r, b.size()); return true;   // SMSG_MIRRORIMAGE_DATA
            // ---- H7e: calendario (formatos 3.3.5 de AzerothCore CalendarHandler.cpp / CalendarMgr.cpp)
            case 0x436: Calendario335(r); return true;        // SMSG_CALENDAR_SEND_CALENDAR
            case 0x437: EventoCalendario335(r); return true;  // SMSG_CALENDAR_SEND_EVENT
            case 0x438: case 0x439:                          // SMSG_CALENDAR_FILTER_GUILD (Guild::MassInviteToEvent) / SMSG_CALENDAR_ARENA_TEAM: u32 n, (packguid, u8 0)*
            {
                // 3.4.3 solo tiene la invitación masiva de "comunidad" (la hermandad); la de equipo de arena va por el mismo paquete.
                // AC manda 0 en lugar del nivel: se usa el visto (si el jugador está cerca) o el mínimo que pidió el cliente
                WorldPackets::Calendar::CalendarCommunityInvite p;
                uint32 n = r.get<uint32>();
                for (uint32 i = 0; i < n && r.p < b.size(); ++i)
                {
                    uint64 g = Pack(r); uint8 nivel = r.get<uint8>();
                    if (uint32 v = mundo->V335(g, M335::U_LEVEL)) nivel = uint8(v);
                    if (!nivel) nivel = _calNivelMin;
                    p.Invites.emplace_back(mundo->ToTc(g), nivel);
                }
                Enviar(p.Write());
                return true;
            }
            case 0x43A:                                      // SMSG_CALENDAR_EVENT_INVITE: packguid, u64 evento, u64 invitación, u8 nivel, u8 estado, u8 hay hora, [hora], u8 no es apuntarse
            {
                WorldPackets::Calendar::CalendarInviteAdded p;
                p.InviteGuid = mundo->ToTc(Pack(r));
                p.EventID = r.get<uint64>(); p.InviteID = r.get<uint64>();
                p.Level = r.get<uint8>(); p.Status = r.get<uint8>();
                bool hayHora = r.get<uint8>() != 0;
                p.ResponseTime = HoraCal(hayHora ? r.get<uint32>() : HoraLocal335(CAL_SIN_RESPUESTA));
                r.get<uint8>();
                p.Type = 0;                                  // Wrathion: 1 si es evento de hermandad (3.3.5 no lo dice)
                p.ClearPending = true;                       // Wrathion: cierto salvo en eventos de hermandad
                Enviar(p.Write());
                return true;
            }
            case 0x43B:                                      // SMSG_CALENDAR_EVENT_INVITE_REMOVED: packguid, u64 evento, u32 flags, u8
            {
                WorldPackets::Calendar::CalendarInviteRemoved p;
                p.InviteGuid = mundo->ToTc(Pack(r)); p.EventID = r.get<uint64>(); p.Flags = r.get<uint32>();
                p.ClearPending = r.get<uint8>() != 0;
                Enviar(p.Write());
                return true;
            }
            case 0x43C:                                      // SMSG_CALENDAR_EVENT_STATUS: packguid, u64 evento, hora evento, u32 flags, u8 estado, u8 rango, hora respuesta
            {
                WorldPackets::Calendar::CalendarInviteStatus p;
                uint64 g = Pack(r);
                p.InviteGuid = mundo->ToTc(g); p.EventID = r.get<uint64>(); p.Date = HoraCal(r.get<uint32>());
                p.Flags = r.get<uint32>(); p.Status = r.get<uint8>(); r.get<uint8>();    // el rango no va en 3.4.3
                p.ResponseTime = HoraCal(r.get<uint32>());
                p.ClearPending = g == yo335;                 // Wrathion: solo para el propio invitado
                Enviar(p.Write());
                return true;
            }
            case 0x43D:                                      // SMSG_CALENDAR_COMMAND_RESULT: u32 0, u8 0, cstr nombre, u32 error
            {
                // CalendarError tiene los mismos valores en AC y en TC 3.4.3 (0..40): no hace falta tabla
                WorldPackets::Calendar::CalendarCommandResult p;
                r.get<uint32>(); r.get<uint8>();
                p.Name = r.cstr(); p.Result = uint8(r.get<uint32>());
                p.Command = 1;                               // Wrathion (FIXME también allí)
                Enviar(p.Write());
                return true;
            }
            case 0x43E:                                      // SMSG_CALENDAR_RAID_LOCKOUT_ADDED: hora, u32 mapa, u32 dificultad, u32 segundos, u64 guid de instancia
            {
                WorldPackets::Calendar::CalendarRaidLockoutAdded p;
                p.ServerTime = HoraCal(r.get<uint32>());
                p.MapID = int32(r.get<uint32>());
                p.DifficultyID = DificultadCal(uint32(p.MapID), r.get<uint32>());
                p.TimeRemaining = int32(r.get<uint32>());
                p.InstanceID = r.get<uint64>() & 0xFFFFFFFF;
                Enviar(p.Write());
                return true;
            }
            case 0x43F:                                      // SMSG_CALENDAR_RAID_LOCKOUT_REMOVED: u32 mapa, u32 dificultad, u32 segundos, u64 guid de instancia
            {
                WorldPackets::Calendar::CalendarRaidLockoutRemoved p;
                p.MapID = int32(r.get<uint32>());
                p.DifficultyID = DificultadCal(uint32(p.MapID), r.get<uint32>());
                r.get<uint32>();
                p.InstanceID = r.get<uint64>() & 0xFFFFFFFF;
                Enviar(p.Write());
                return true;
            }
            case 0x471:                                      // SMSG_CALENDAR_RAID_LOCKOUT_UPDATED: hora, u32 mapa, u32 dificultad, u32 segundos antes, u32 segundos ahora
            {
                WorldPackets::Calendar::CalendarRaidLockoutUpdated p;
                p.ServerTime = HoraCal(r.get<uint32>());
                p.MapID = int32(r.get<uint32>());
                p.DifficultyID = DificultadCal(uint32(p.MapID), r.get<uint32>());
                uint32 antes = r.get<uint32>();
                if (antes > 400000000) antes = 0;            // AC manda la marca absoluta si el reinicio viejo ya pasó (HandleSetSavedInstanceExtend)
                p.OldTimeRemaining = int32(antes);
                p.NewTimeRemaining = int32(r.get<uint32>());
                Enviar(p.Write());
                return true;
            }
            case 0x440:                                      // SMSG_CALENDAR_EVENT_INVITE_ALERT: u64 evento, cstr título, hora, u32 flags, u32 tipo, i32 mazmorra,
            {                                                //   u64 invitación, u8 estado, u8 rango, packguid creador, packguid quien invita
                WorldPackets::Calendar::CalendarInviteAlert p;
                p.EventID = r.get<uint64>(); p.EventName = r.cstr(); p.Date = HoraCal(r.get<uint32>());
                p.Flags = r.get<uint32>(); p.EventType = uint8(r.get<uint32>()); p.TextureID = int32(r.get<uint32>());
                p.InviteID = r.get<uint64>(); p.Status = r.get<uint8>(); p.ModeratorStatus = r.get<uint8>();
                p.OwnerGuid = mundo->ToTc(Pack(r)); p.InvitedByGuid = mundo->ToTc(Pack(r));
                p.EventClubID = ClubCal(p.Flags);
                Enviar(p.Write());
                return true;
            }
            case 0x441:                                      // SMSG_CALENDAR_EVENT_INVITE_REMOVED_ALERT: u64 evento, hora, u32 flags, u8 estado
            {
                WorldPackets::Calendar::CalendarInviteRemovedAlert p;
                p.EventID = r.get<uint64>(); p.Date = HoraCal(r.get<uint32>()); p.Flags = r.get<uint32>(); p.Status = r.get<uint8>();
                Enviar(p.Write());
                return true;
            }
            case 0x442:                                      // SMSG_CALENDAR_EVENT_INVITE_STATUS_ALERT: u64 evento, hora, u32 flags, u8 estado (AC no lo manda; formato de su comentario)
            {
                WorldPackets::Calendar::CalendarInviteStatusAlert p;
                p.EventID = r.get<uint64>(); p.Date = HoraCal(r.get<uint32>()); p.Flags = r.get<uint32>(); p.Status = r.get<uint8>();
                Enviar(p.Write());
                return true;
            }
            case 0x443:                                      // SMSG_CALENDAR_EVENT_REMOVED_ALERT: u8 (1 = no avisar de acción pendiente), u64 evento, hora
            {
                WorldPackets::Calendar::CalendarEventRemovedAlert p;
                p.ClearPending = r.get<uint8>() != 0; p.EventID = r.get<uint64>(); p.Date = HoraCal(r.get<uint32>());
                Enviar(p.Write());
                return true;
            }
            case 0x444:                                      // SMSG_CALENDAR_EVENT_UPDATED_ALERT: u8, u64 evento, hora antigua, u32 flags, hora, u8 tipo, i32 mazmorra,
            {                                                //   cstr título, cstr descripción, u8 repetición, u32 máx. invitaciones, u32
                WorldPackets::Calendar::CalendarEventUpdatedAlert p;
                p.ClearPending = r.get<uint8>() != 0; p.EventID = r.get<uint64>(); p.OriginalDate = HoraCal(r.get<uint32>());
                p.Flags = r.get<uint32>(); p.Date = HoraCal(r.get<uint32>()); p.EventType = r.get<uint8>();
                p.TextureID = int32(r.get<uint32>()); p.EventName = r.cstr(); p.Description = r.cstr();
                p.LockDate = p.Date;                         // 3.3.5 no la manda aquí; el gateway la crea igual a la fecha del evento (CMSG_CALENDAR_ADD_EVENT)
                p.EventClubID = ClubCal(p.Flags);
                Enviar(p.Write());
                return true;
            }
            case 0x445:                                      // SMSG_CALENDAR_EVENT_MODERATOR_STATUS_ALERT: packguid, u64 evento, u8 rango, u8
            {
                WorldPackets::Calendar::CalendarModeratorStatus p;
                p.InviteGuid = mundo->ToTc(Pack(r)); p.EventID = r.get<uint64>(); p.Status = r.get<uint8>();
                p.ClearPending = r.get<uint8>() != 0;
                Enviar(p.Write());
                return true;
            }
            case 0x448:                                      // SMSG_CALENDAR_SEND_NUM_PENDING: u32
            {
                WorldPackets::Calendar::CalendarSendNumPending p(r.get<uint32>()); Enviar(p.Write());
                return true;
            }
            case 0x460:                                      // SMSG_CALENDAR_EVENT_INVITE_NOTES: packguid, u64, cstr nota, u8 (AC no lo manda; formato de su comentario)
            {
                WorldPackets::Calendar::CalendarInviteNotes p;
                p.InviteGuid = mundo->ToTc(Pack(r)); p.EventID = r.get<uint64>(); p.Notes = r.cstr(); p.ClearPending = r.get<uint8>() != 0;
                Enviar(p.Write());
                return true;
            }
            case 0x461:                                      // SMSG_CALENDAR_EVENT_INVITE_NOTES_ALERT: u64, cstr nota (AC no lo manda)
            {
                uint64 id = r.get<uint64>();
                WorldPackets::Calendar::CalendarInviteNotesAlert p(id, r.cstr()); Enviar(p.Write());
                return true;
            }
            case 0x4BB: { WorldPackets::Calendar::CalendarClearPendingAction p; Enviar(p.Write()); return true; }   // SMSG_CALENDAR_CLEAR_PENDING_ACTION: vacío
            // ---- H7f: equipos de arena, inspección JcJ y honor
            case 0x34E: EquipoArenaLista335(r); return true;  // SMSG_ARENA_TEAM_ROSTER
            case 0x34C:                                      // SMSG_ARENA_TEAM_QUERY_RESPONSE: u32 id, cstr nombre, u32 tipo, u32 fondo, u32 emblema, u32 color emblema, u32 borde, u32 color borde
            {
                uint32 id = r.get<uint32>(); std::string nombre = r.cstr(); uint32 tipo = r.get<uint32>();
                uint32 est[5]; for (uint32& x : est) x = r.get<uint32>();
                { std::lock_guard<std::mutex> l(_mArena); _arenaNombres[nombre] = id; _arenaTipo[id] = tipo; }
                // 3.4.3 (derivado de WPP 4.4.0): u32 id, bit hay datos, [u32 id, u32 tamaño, 5 x u32 estandarte (mismo orden
                // que en 3.3.5), bits7 largo, nombre]
                std::string nom = nombre.substr(0, 127);
                WorldPacket w(SMSG_QUERY_ARENA_TEAM_RESPONSE, 40 + nom.size());
                w << uint32(id);
                w.WriteBit(true); w.FlushBits();
                w << uint32(id) << uint32(tipo);
                for (uint32 x : est) w << uint32(x);
                w.WriteBits(nom.size(), 7); w.FlushBits();
                w.WriteString(nom);
                Enviar(&w);
                return true;
            }
            case 0x35B:                                      // SMSG_ARENA_TEAM_STATS: u32 id, u32 índice, semana jugadas/ganadas, temporada jugadas/ganadas, u32 puesto
            {
                // Su forma en 3.4.3 no se conoce: no se reenvía. Se guarda y va dentro de SMSG_ARENA_TEAM_ROSTER, que en
                // 3.4.3 lleva esos mismos datos en la cabecera. Si cambian (fin de partida puntuada) se pide la lista otra vez.
                uint32 id = r.get<uint32>();
                std::array<uint32, 6> st; for (uint32& x : st) x = r.get<uint32>();
                bool refrescar;
                {
                    std::lock_guard<std::mutex> l(_mArena);
                    auto it = _arenaStats.find(id);
                    refrescar = it != _arenaStats.end() && it->second != st && !_arenaPend.count(id);
                    _arenaStats[id] = st;
                }
                if (refrescar) PedirEquipoArena(id, false);
                return true;
            }
            case 0x357: EventoArena335(r); return true;       // SMSG_ARENA_TEAM_EVENT
            case 0x350:                                      // SMSG_ARENA_TEAM_INVITE: cstr quien invita, cstr equipo
            {
                // Sin formato 3.4.3 conocido (el ACCEPT del cliente lleva guid del que invita y del equipo, así que la
                // invitación tampoco es la de 3.3.5): se avisa por el chat. AzerothCore acepta sin datos, así que vale
                // cualquier CMSG_ARENA_TEAM_ACCEPT; que AcceptArenaTeam() lo mande sin ventana abierta está SIN PROBAR.
                std::string quien = r.cstr(), equipo = r.cstr();
                Sistema(quien + " te invita a unirte al equipo de arena " + equipo +
                    ". Para aceptar: /run AcceptArenaTeam()  Para rechazar: /run DeclineArenaTeam()");
                return true;
            }
            case 0x349:                                      // SMSG_ARENA_TEAM_COMMAND_RESULT: u32 acción, cstr equipo, cstr jugador, u32 error
            {
                // 3.4.3 (derivado de WPP 4.4.0): u8 acción, u8 error, bits7 largo equipo, bits8 largo jugador, cadenas.
                // Los valores de acción/error se pasan tal cual (ArenaTeamCommandTypes / ArenaTeamCommandErrors de 3.3.5).
                uint32 accion = r.get<uint32>(); std::string equipo = r.cstr().substr(0, 127), jugador = r.cstr().substr(0, 255);
                uint32 err = r.get<uint32>();
                WorldPacket w(SMSG_ARENA_TEAM_COMMAND_RESULT, 8 + equipo.size() + jugador.size());
                w << uint8(accion) << uint8(err);
                w.WriteBits(equipo.size(), 7); w.WriteBits(jugador.size(), 8); w.FlushBits();
                w.WriteString(equipo); w.WriteString(jugador);
                Enviar(&w);
                return true;
            }
            case 0x376:                                      // SMSG_ARENA_ERROR: u32 (0), u8 tipo (2, 3, 5)
            {
                // 3.4.3 no tiene este paquete ni un GameError equivalente (ERR_ARENA_NO_TEAM_II): texto de sistema
                uint32 x = r.get<uint32>(); uint32 t = x ? 0 : r.get<uint8>();
                Sistema(t ? "No perteneces a ningún equipo de arena de " + std::to_string(t) + "v" + std::to_string(t) + "."
                          : std::string("No perteneces a ningún equipo de arena de ese tamaño."));
                return true;
            }
            case 0x4C7:                                      // SMSG_ARENA_UNIT_DESTROYED: u64 -> SMSG_DESTROY_ARENA_UNIT
            {
                WorldPackets::Battleground::DestroyArenaUnit p; p.Guid = mundo->ToTc(r.get<uint64>());
                Enviar(p.Write());
                return true;
            }
            case 0x377:                                      // MSG_INSPECT_ARENA_TEAMS: u64 jugador, u8 hueco, u32 id, u32 índice, u32 jugadas y ganadas temporada, u32 jugadas del miembro, u32 índice personal
            {
                // AzerothCore manda uno por equipo; 3.4.3 quiere los tres juntos: se acumulan y se reenvía el conjunto
                uint64 g = r.get<uint64>(); uint8 hueco = r.get<uint8>();
                EquipoInsp x; x.id = r.get<uint32>(); x.rating = r.get<uint32>(); x.jugadas = r.get<uint32>(); x.ganadas = r.get<uint32>();
                x.misJugadas = r.get<uint32>(); x.miRating = r.get<uint32>();
                if (hueco >= 3) return true;
                std::array<EquipoInsp, 3> e;
                {
                    std::lock_guard<std::mutex> l(_mArena);
                    if (_inspArenaDe != g) { _inspArenaDe = g; _inspArena = { }; }
                    _inspArena[hueco] = x; e = _inspArena;
                }
                EnviarInspeccionJcJ(g, e);
                return true;
            }
            case 0x2D6:                                      // MSG_INSPECT_HONOR_STATS: u64, u8 (honor truncado), u32 muertes (hoy | ayer << 16), u32 honor hoy, u32 honor ayer, u32 muertes totales
            {
                // 3.3.5 no lleva muertes deshonrosas, semanales ni rango: van a 0. El honor de hoy no tiene campo en 3.4.3.
                WorldPackets::Inspect::InspectHonorStatsResult p;
                p.Target = mundo->ToTc(r.get<uint64>()); r.get<uint8>();
                uint32 muertes = r.get<uint32>(); r.get<uint32>();
                p.YesterdayHonor = r.get<uint32>(); p.LifeTimeHK = r.get<uint32>();
                p.TodayHK = uint16(muertes & 0xFFFF); p.YesterdayHK = uint16(muertes >> 16);
                Enviar(p.Write());
                return true;
            }
            // ---- H7b: registros de combate
            case 0x24C: Ejecucion335(r); return true;         // SMSG_SPELLLOGEXECUTE
            case 0x260:                                      // SMSG_PROCRESIST: u64 lanzador, u64 objetivo, u32 hechizo, u8
            {
                WorldPackets::CombatLog::ProcResist p;
                p.Caster = mundo->ToTc(r.get<uint64>()); p.Target = mundo->ToTc(r.get<uint64>()); p.SpellID = int32(r.get<uint32>());
                Enviar(p.Write());
                return true;
            }
            case 0x262:                                      // SMSG_DISPEL_FAILED: u64 lanzador, u64 víctima, u32 hechizo, u32*
            {
                WorldPackets::Spells::DispelFailed p;
                p.CasterGUID = mundo->ToTc(r.get<uint64>()); p.VictimGUID = mundo->ToTc(r.get<uint64>()); p.SpellID = r.get<uint32>();
                while (r.p + 4 <= r.b.size()) p.FailedSpells.push_back(int32(r.get<uint32>()));
                Enviar(p.Write());
                return true;
            }
            case 0x333:                                      // SMSG_SPELLSTEALLOG: packguid víctima, packguid lanzador, u32 hechizo, u8, u32 n, (u32 hechizo, u8 transferir)*
            {
                WorldPackets::CombatLog::SpellDispellLog p;
                p.TargetGUID = mundo->ToTc(Pack(r)); p.CasterGUID = mundo->ToTc(Pack(r));
                p.DispelledBySpellID = int32(r.get<uint32>()); r.get<uint8>();
                p.IsSteal = true;
                uint32 n = r.get<uint32>();
                for (uint32 i = 0; i < n && r.p + 5 <= r.b.size(); ++i)
                {
                    WorldPackets::CombatLog::SpellDispellData d; d.SpellID = int32(r.get<uint32>()); r.get<uint8>(); d.Harmful = false;
                    p.DispellData.push_back(d);
                }
                Enviar(p.Write());
                return true;
            }
            // ---- H7h: Conquista del Invierno
            case 0x4E1:                                      // SMSG_BATTLEFIELD_MGR_QUEUE_INVITE: u32 batalla, u8 -> se acepta sola
            {
                uint32 id = r.get<uint32>();
                Wr w; w.put<uint32>(id).put<uint8>(1); Srv(0x4E2, w.b);
                return true;
            }
            case 0x4E4:                                      // SMSG_BATTLEFIELD_MGR_QUEUE_REQUEST_RESPONSE: u32 batalla, u32 zona, u8 aceptado, u8 hay sitio, u8
            {
                uint32 id = r.get<uint32>(); r.get<uint32>(); uint8 aceptado = r.get<uint8>(), sitio = r.get<uint8>();
                if (!aceptado) { Sistema("No puedes ponerte en cola para Conquista del Invierno ahora."); return true; }
                { std::lock_guard<std::mutex> l(_mBg); _batallaCola = id; }
                WorldPackets::Battleground::BattlefieldStatusQueued p;
                p.Hdr = CabeceraBatalla(id); p.EligibleForMatchmaking = true;
                Enviar(p.Write());
                if (!sitio) Sistema("Conquista del Invierno está llena: quedas en cola.");
                return true;
            }
            case 0x4DE:                                      // SMSG_BATTLEFIELD_MGR_ENTRY_INVITE: u32 batalla, u32 zona, u32 caduca (hora unix)
            {
                uint32 id = r.get<uint32>(); r.get<uint32>(); uint32 caduca = r.get<uint32>();
                { std::lock_guard<std::mutex> l(_mBg); _batallaCola = id; }
                WorldPackets::Battleground::BattlefieldStatusNeedConfirmation p;
                p.Hdr = CabeceraBatalla(id); p.Mapid = 571;
                uint32 ahora = uint32(time(nullptr));
                p.Timeout = caduca > ahora ? (caduca - ahora) * IN_MILLISECONDS : 20 * IN_MILLISECONDS;
                Enviar(p.Write());
                return true;
            }
            case 0x4E6:                                      // SMSG_BATTLEFIELD_MGR_EJECTED: u32 batalla, u8 motivo, u8, u8
            {
                uint32 id = r.get<uint32>();
                WorldPackets::Battleground::BattlefieldStatusNone p; p.Ticket = CabeceraBatalla(id).Ticket;
                Enviar(p.Write());
                { std::lock_guard<std::mutex> l(_mBg); _batallaCola = 0; }
                return true;
            }
            case 0x4E0: case 0x4E5: case 0x4E8: return true;  // ENTERED / EJECT_PENDING / STATE_CHANGE: Wrathion no manda nada
            // ---- H7g: sueltos
            case 0x032:                                      // SMSG_DESTRUCTIBLE_BUILDING_DAMAGE: packguid edificio, packguid atacante, packguid jugador, u32 daño, u32 hechizo
            {
                WorldPackets::GameObject::DestructibleBuildingDamage p;
                p.Target = mundo->ToTc(Pack(r)); p.Caster = mundo->ToTc(Pack(r)); p.Owner = mundo->ToTc(Pack(r));
                p.Damage = int32(r.get<uint32>()); p.SpellID = int32(r.get<uint32>());
                Enviar(p.Write());
                return true;
            }
            case 0x412:                                      // SMSG_OVERRIDE_LIGHT: u32 luz del mapa, u32 luz nueva, u32 ms
            {
                WorldPackets::Misc::OverrideLight p;
                p.AreaLightID = int32(r.get<uint32>()); p.OverrideLightID = int32(r.get<uint32>()); p.TransitionMilliseconds = int32(r.get<uint32>());
                Enviar(p.Write());
                return true;
            }
            case 0x3C1:                                      // SMSG_CROSSED_INEBRIATION_THRESHOLD: u64, u32 nivel, u32 objeto
            {
                WorldPackets::Misc::CrossedInebriationThreshold p;
                p.Guid = mundo->ToTc(r.get<uint64>()); p.Threshold = int32(r.get<uint32>()); p.ItemID = int32(r.get<uint32>());
                Enviar(p.Write());
                return true;
            }
            case 0x178:                                      // SMSG_PET_NAME_INVALID: u32 error, cstr nombre, u8 declinados, [5 cstr]
            {
                WorldPackets::Pet::PetNameInvalid p;
                p.Result = uint8(r.get<uint32>()); p.RenameData.NewName = r.cstr();
                if (r.get<uint8>()) { p.RenameData.DeclinedNames.emplace(); for (std::string& n : p.RenameData.DeclinedNames->name) n = r.cstr(); }
                Enviar(p.Write());
                return true;
            }
            case 0x46F:                                      // SMSG_QUESTUPDATE_ADD_PVP_KILL: u32 misión, u32 actual, u32 necesario
            {
                WorldPackets::Quest::QuestUpdateAddPvPCredit p;
                p.QuestID = int32(r.get<uint32>()); p.Count = uint16(r.get<uint32>());
                Enviar(p.Write());
                return true;
            }
            case 0x396: { WorldPackets::Instance::ResetFailedNotify p; Enviar(p.Write()); return true; }   // SMSG_RESET_FAILED_NOTIFY: u32 mapa
            case 0x3F0: case 0x3F2:                          // SMSG_USERLIST_ADD / UPDATE: u64, u8 flags jugador, u8 flags canal, u32 n, cstr canal
            {
                ObjectGuid g = mundo->ToTc(r.get<uint64>()); uint8 fj = r.get<uint8>(), fc = r.get<uint8>(); r.get<uint32>(); std::string canal = r.cstr();
                if (op == 0x3F0) { WorldPackets::Channel::UserlistAdd p; p.AddedUserGUID = g; p.UserFlags = fj; p._ChannelFlags = fc; p.ChannelName = canal; Enviar(p.Write()); }
                else { WorldPackets::Channel::UserlistUpdate p; p.UpdatedUserGUID = g; p.UserFlags = fj; p._ChannelFlags = fc; p.ChannelName = canal; Enviar(p.Write()); }
                return true;
            }
            case 0x3F1:                                      // SMSG_USERLIST_REMOVE: u64, u8 flags canal, u32 n, cstr canal
            {
                WorldPackets::Channel::UserlistRemove p;
                p.RemovedUserGUID = mundo->ToTc(r.get<uint64>()); p._ChannelFlags = r.get<uint8>(); r.get<uint32>(); p.ChannelName = r.cstr();
                Enviar(p.Write());
                return true;
            }
            case 0x206:                                      // SMSG_GMTICKET_CREATE: 2 creado; 3 error = casi siempre "ya tienes uno" (AC no manda el 1)
            {
                uint32 res = r.get<uint32>();
                Sistema(res == 2 ? "Ticket enviado a los GM. Consulta su estado con .ayuda ver"
                      : "No se ha creado: ya tienes un ticket abierto. Usa .ayuda <texto> para cambiarlo o .ayuda borrar.");
                Srv(0x211, {});                              // refresca el estado (-> 0x212 -> CASE_STATUS)
                return true;
            }
            case 0x208:                                      // SMSG_GMTICKET_UPDATETEXT: u32 4 ok / 5 error
                Sistema(r.get<uint32>() == 4 ? "Texto del ticket actualizado." : "No tienes ningún ticket que actualizar.");
                Srv(0x211, {});
                return true;
            case 0x212:                                      // SMSG_GMTICKET_GETTICKET
            {
                uint32 est = r.get<uint32>();
                {
                    std::lock_guard<std::mutex> l(_mTicket);
                    if (est == 6)
                    {
                        _gmTicket.hay = true; _gmTicket.completado = false;
                        _gmTicket.id = r.get<uint32>(); _gmTicket.texto = r.cstr(); r.get<uint8>();
                        float dias = r.get<float>();
                        _gmTicket.abierto = time(nullptr) - time_t(dias * 86400.f);
                    }
                    else _gmTicket = {};
                }
                CasoTicket();
                if (_verTicket.exchange(false))              // lo pidió ".ayuda ver"
                    Sistema(est == 6 ? "Ticket #" + std::to_string(_gmTicket.id) + " abierto: " + _gmTicket.texto.substr(0, 200)
                                     : "No tienes ningún ticket abierto.");
                return true;
            }
            case 0x4EF:                                      // SMSG_GMRESPONSE_RECEIVED: u32, u32 id, cstr texto, 4 x cstr respuesta
            {
                r.get<uint32>(); uint32 id = r.get<uint32>(); std::string texto = r.cstr(), resp;
                for (int i = 0; i < 4 && r.p < b.size(); ++i) resp += r.cstr();
                {
                    std::lock_guard<std::mutex> l(_mTicket);
                    _gmTicket.hay = true; _gmTicket.completado = true; _gmTicket.id = id; _gmTicket.texto = texto; _gmTicket.respuesta = resp;
                    if (!_gmTicket.abierto) _gmTicket.abierto = time(nullptr);
                }
                CasoTicket();
                Sistema("|cff00ccff[GM]|r Respuesta a tu ticket #" + std::to_string(id) + ":");
                for (size_t i = 0; i < resp.size(); i += 240) Sistema(resp.substr(i, 240));
                Sistema("Escribe .ayuda borrar cuando hayas terminado.");
                _verTicket = false;
                return true;
            }
            case 0x218:                                      // SMSG_GMTICKET_DELETETICKET: u32 9
            {
                { std::lock_guard<std::mutex> l(_mTicket); _gmTicket = {}; }
                CasoTicket();
                Sistema("Tu ticket está cerrado.");
                return true;
            }
            case 0x4F1: return true;                         // SMSG_GMRESPONSE_STATUS_UPDATE (encuesta): el 3.4.3 no tiene esa interfaz
            case 0x21B:                                      // SMSG_GMTICKET_SYSTEMSTATUS: u32 estado (AC 0 desactivado; 3.4.3 -1)
            {
                WorldPackets::Ticket::GMTicketSystemStatus p; p.Status = r.get<uint32>() ? 1 : -1; Enviar(p.Write());
                return true;
            }
            case 0x498:                                      // SMSG_SERVER_FIRST_ACHIEVEMENT: cstr nombre, u64, u32 logro, u32 enlace
            {
                std::string nombre = r.cstr(); r.get<uint64>(); uint32 logro = r.get<uint32>();
                Sistema(nombre + " ha conseguido el logro [" + std::to_string(logro) + "] el primero del reino.");   // sin clase en TrinityCore 3.4.3
                return true;
            }
            // de AzerothCore sin equivalente en el cliente 3.4.3: se consumen
            case 0x19A:                                      // SMSG_QUESTUPDATE_ADD_ITEM: el 3.4.3 cuenta los objetos del inventario
            case 0x20D:                                      // SMSG_CLEAR_FAR_SIGHT_IMMEDIATE
            case 0x219:                                      // SMSG_CHAT_WRONG_FACTION
            case 0x297:                                      // SMSG_SHOW_MAILBOX
            case 0x2E6:                                      // SMSG_WARDEN_DATA (Warden del 3.3.5)
            case 0x2F5:                                      // SMSG_PLAY_TIME_WARNING
            case 0x360:                                      // SMSG_UPDATE_LFG_LIST (buscador antiguo)
            case 0x38B:                                      // SMSG_REALM_SPLIT
            case 0x41F: case 0x421:                          // recluta a un amigo
            case 0x492:                                      // SMSG_PET_UPDATE_COMBO_POINTS
            case 0x4F7:                                      // SMSG_WORLD_STATE_UI_TIMER_UPDATE
            case 0x506:                                      // SMSG_CORPSE_NOT_IN_INSTANCE
            case 0x088:                                      // SMSG_GUILD_INFO (el 3.4.3 no lo pide)
            case 0x3D5:                                      // SMSG_CHANNEL_MEMBER_COUNT (el 3.4.3 no lo pide)
                return true;
            case 0x516:                                      // SMSG_MOVE_SET_COLLISION_HGT: packguid, u32 contador, float altura
            {
                WorldPackets::Movement::MoveSetCollisionHeight p;
                p.MoverGUID = mundo->ToTc(Pack(r)); p.SequenceIndex = r.get<uint32>(); p.Height = r.get<float>();
                p.Scale = 1.0f; p.Reason = WorldPackets::Movement::UpdateCollisionHeightReason::Mount;
                Enviar(p.Write());
                return true;
            }
            case 0x319:                                      // MSG_MOVE_TIME_SKIPPED (de otro): packguid, u32 ms
            {
                WorldPackets::Movement::MoveSkipTime p;
                p.MoverGUID = mundo->ToTc(Pack(r)); p.TimeSkipped = r.get<uint32>();
                Enviar(p.Write());
                return true;
            }
            case 0x1F7:                                      // SMSG_PLAY_SPELL_IMPACT: u64 objetivo, u32 SpellVisualKit
            {
                WorldPackets::Spells::PlaySpellVisualKit p;
                p.Unit = mundo->ToTc(r.get<uint64>()); p.KitRecID = int32(r.get<uint32>());
                Enviar(p.Write());
                return true;
            }

            // ---- combate cuerpo a cuerpo
            case 0x143:                                      // SMSG_ATTACKSTART: u64 atacante, u64 víctima
            {
                WorldPackets::Combat::AttackStart p;
                p.Attacker = mundo->ToTc(r.get<uint64>()); p.Victim = mundo->ToTc(r.get<uint64>());
                Enviar(p.Write());
                return true;
            }
            case 0x144:                                      // SMSG_ATTACKSTOP: packguid, packguid, u32
            {
                WorldPackets::Combat::SAttackStop p;
                p.Attacker = mundo->ToTc(Pack(r)); p.Victim = mundo->ToTc(Pack(r));
                p.NowDead = r.get<uint32>() != 0;
                Enviar(p.Write());
                return true;
            }
            case 0x14A: AttackerState335(r); return true;
            case 0x145: case 0x146: case 0x148: case 0x149:
            {
                AttackSwingErr e = op == 0x145 ? AttackSwingErr::NotInRange : op == 0x146 ? AttackSwingErr::BadFacing
                                 : op == 0x148 ? AttackSwingErr::DeadTarget : AttackSwingErr::CantAttack;
                WorldPackets::Combat::AttackSwingError p(e);
                Enviar(p.Write());
                return true;
            }
            case 0x14E: { WorldPackets::Combat::CancelCombat p; Enviar(p.Write()); return true; }
            case 0x29C:                                      // SMSG_CANCEL_AUTO_REPEAT: packguid
            {
                WorldPackets::Combat::CancelAutoRepeat p; p.Guid = mundo->ToTc(Pack(r));
                Enviar(p.Write());
                return true;
            }

            // ---- hechizos
            case 0x131: SpellStart335(r); return true;
            case 0x132: SpellGo335(r); return true;
            case 0x130:                                      // SMSG_CAST_FAILED: u8 cuenta, u32 hechizo, u8 resultado, ...
            {
                WorldPackets::Spells::CastFailed p;
                uint8 cc = r.get<uint8>(); p.SpellID = int32(r.get<uint32>());
                p.CastID = CastId(yo335, cc, uint32(p.SpellID));
                p.Visual.SpellXSpellVisualID = int32(VisualDe(uint32(p.SpellID)));
                p.Reason = kSpellCastResult[r.get<uint8>()];
                Enviar(p.Write());
                return true;
            }
            case 0x133: case 0x2A6:                          // SMSG_SPELL_FAILURE / SMSG_SPELL_FAILED_OTHER: packguid, u8 cuenta, u32, u8
            {
                uint64 c = Pack(r); uint8 cc = r.get<uint8>(); uint32 spell = r.get<uint32>(); uint8 res = r.get<uint8>();
                if (op == 0x133)
                {
                    WorldPackets::Spells::SpellFailure p;
                    // el del SPELL_START; sin borrarlo: AC manda detrás SPELL_FAILED_OTHER, que lo necesita igual
                    p.CasterUnit = mundo->ToTc(c); p.CastID = CastId(c, cc, spell, false, true, false); p.SpellID = spell;
                    p.Visual.SpellXSpellVisualID = int32(VisualDe(spell)); p.Reason = uint16(kSpellCastResult[res]);
                    Enviar(p.Write());
                }
                else
                {
                    WorldPackets::Spells::SpellFailedOther p;
                    p.CasterUnit = mundo->ToTc(c); p.CastID = CastId(c, cc, spell, false, true); p.SpellID = spell;
                    p.Visual.SpellXSpellVisualID = int32(VisualDe(spell)); p.Reason = uint8(kSpellCastResult[res]);
                    Enviar(p.Write());
                }
                return true;
            }
            case 0x134:                                      // SMSG_SPELL_COOLDOWN: u64 lanzador, u8 banderas, (u32 hechizo, u32 ms)*
            {
                WorldPackets::Spells::SpellCooldown p;
                p.Caster = mundo->ToTc(r.get<uint64>()); p.Flags = r.get<uint8>();
                while (r.p + 8 <= b.size()) { uint32 s = r.get<uint32>(), ms = r.get<uint32>(); p.SpellCooldowns.emplace_back(s, ms); }
                Enviar(p.Write());
                return true;
            }
            case 0x135:                                      // SMSG_COOLDOWN_EVENT: u32 hechizo, u64 guid
            {
                uint32 s = r.get<uint32>();
                uint64 g = r.p + 8 <= r.b.size() ? r.get<uint64>() : 0;   // AC: el de la mascota si es suyo
                WorldPackets::Spells::CooldownEvent p(g && g != yo335, int32(s));
                Enviar(p.Write());
                return true;
            }
            case 0x1DE:                                      // SMSG_CLEAR_COOLDOWN: u32 hechizo, u64 guid
            {
                WorldPackets::Spells::ClearCooldown p; p.SpellID = int32(r.get<uint32>());
                uint64 g = r.p + 8 <= r.b.size() ? r.get<uint64>() : 0;
                p.IsPet = g && g != yo335;
                Enviar(p.Write());
                return true;
            }
            case 0x139:                                      // MSG_CHANNEL_START: packguid, u32 hechizo, u32 duración
            {
                WorldPackets::Spells::SpellChannelStart p;
                p.CasterGUID = mundo->ToTc(Pack(r)); p.SpellID = int32(r.get<uint32>());
                p.Visual.SpellXSpellVisualID = int32(VisualDe(uint32(p.SpellID))); p.ChannelDuration = r.get<uint32>();
                Enviar(p.Write());
                return true;
            }
            case 0x13A:                                      // MSG_CHANNEL_UPDATE: packguid, u32 restante
            {
                WorldPackets::Spells::SpellChannelUpdate p;
                p.CasterGUID = mundo->ToTc(Pack(r)); p.TimeRemaining = int32(r.get<uint32>());
                Enviar(p.Write());
                return true;
            }
            case 0x12B:                                      // SMSG_LEARNED_SPELL: u32 hechizo, u16
            {
                WorldPackets::Spells::LearnedSpells p;
                WorldPackets::Spells::LearnedSpellInfo i; i.SpellID = int32(r.get<uint32>());
                p.ClientLearnedSpellData.push_back(i);
                Enviar(p.Write());
                if (sDB2Manager.GetMount(uint32(i.SpellID)))    // montura nueva: a la colección (CollectionMgr::SendSingleMountUpdate)
                {
                    MountContainer una; una.emplace(uint32(i.SpellID), MOUNT_NEEDS_FANFARE);
                    WorldPackets::Misc::AccountMountUpdate m; m.IsFullUpdate = false; m.Mounts = &una;
                    Enviar(m.Write());
                }
                return true;
            }
            case 0x203:                                      // SMSG_REMOVED_SPELL: u32 hechizo
            {
                WorldPackets::Spells::UnlearnedSpells p; p.SpellID.push_back(r.get<uint32>());
                Enviar(p.Write());
                return true;
            }
            case 0x12C:                                      // SMSG_SUPERCEDED_SPELL: u32 viejo, u32 nuevo
            {
                WorldPackets::Spells::SupercededSpells p;
                WorldPackets::Spells::LearnedSpellInfo i; int32 viejo = int32(r.get<uint32>()); i.SpellID = int32(r.get<uint32>()); i.Superceded = viejo;
                p.ClientLearnedSpellData.push_back(i);
                Enviar(p.Write());
                return true;
            }

            // ---- registros de combate
            case 0x250:                                      // SMSG_SPELLNONMELEEDAMAGELOG
            {
                WorldPackets::CombatLog::SpellNonMeleeDamageLog p;
                p.Me = mundo->ToTc(Pack(r)); p.CasterGUID = mundo->ToTc(Pack(r));
                p.SpellID = int32(r.get<uint32>()); p.Visual.SpellXSpellVisualID = int32(VisualDe(uint32(p.SpellID)));
                p.Damage = int32(r.get<uint32>()); p.OriginalDamage = p.Damage;
                int32 ok = int32(r.get<uint32>()); p.Overkill = ok > 0 ? ok : -1;
                p.SchoolMask = r.get<uint8>(); p.Absorbed = int32(r.get<uint32>()); p.Resisted = int32(r.get<uint32>());
                r.get<uint8>(); r.get<uint8>();                // physicalLog (AC: daño físico) y sin uso; los periódicos van en 0x24E: Periodic = false
                p.ShieldBlock = int32(r.get<uint32>()); p.Flags = int32(r.get<uint32>());
                Enviar(p.Write());
                return true;
            }
            case 0x150:                                      // SMSG_SPELLHEALLOG: packguid obj, packguid lanzador, u32, u32 cura, u32 sobra, u32 absorbe, u8 crítico
            {
                WorldPackets::CombatLog::SpellHealLog p;
                p.TargetGUID = mundo->ToTc(Pack(r)); p.CasterGUID = mundo->ToTc(Pack(r));
                p.SpellID = int32(r.get<uint32>()); p.Health = int32(r.get<uint32>()); p.OriginalHeal = p.Health;
                p.OverHeal = int32(r.get<uint32>()); p.Absorbed = int32(r.get<uint32>()); p.Crit = r.get<uint8>() != 0;
                Enviar(p.Write());
                return true;
            }
            case 0x151:                                      // SMSG_SPELLENERGIZELOG: packguid obj, packguid lanzador, u32, u32 tipo, u32 cantidad
            {
                WorldPackets::CombatLog::SpellEnergizeLog p;
                p.TargetGUID = mundo->ToTc(Pack(r)); p.CasterGUID = mundo->ToTc(Pack(r));
                p.SpellID = int32(r.get<uint32>()); p.Type = int32(r.get<uint32>()); p.Amount = int32(r.get<uint32>());
                Enviar(p.Write());
                return true;
            }
            case 0x24E: PeriodicAura335(r); return true;
            case 0x24B:                                      // SMSG_SPELLLOGMISS: u32, u64 lanzador, u8, u32 n, (u64, u8)*
            {
                WorldPackets::CombatLog::SpellMissLog p;
                p.SpellID = int32(r.get<uint32>()); p.Caster = mundo->ToTc(r.get<uint64>()); r.get<uint8>();
                uint32 n = r.get<uint32>();
                for (uint32 i = 0; i < n && r.p + 9 <= b.size(); ++i) { ObjectGuid v = mundo->ToTc(r.get<uint64>()); p.Entries.emplace_back(v, r.get<uint8>()); }
                Enviar(p.Write());
                return true;
            }
            case 0x1FC:                                      // SMSG_ENVIRONMENTALDAMAGELOG: u64, u8 tipo, u32 daño, u32 absorbe, u32 resiste
            {
                WorldPackets::CombatLog::EnvironmentalDamageLog p;
                p.Victim = mundo->ToTc(r.get<uint64>()); p.Type = r.get<uint8>();
                p.Amount = int32(r.get<uint32>()); p.Absorbed = int32(r.get<uint32>()); p.Resisted = int32(r.get<uint32>());
                Enviar(p.Write());
                return true;
            }
            case 0x32F:                                      // SMSG_SPELLINSTAKILLLOG: u64 lanzador, u64 objetivo, u32
            {
                WorldPackets::CombatLog::SpellInstakillLog p;
                p.Caster = mundo->ToTc(r.get<uint64>()); p.Target = mundo->ToTc(r.get<uint64>()); p.SpellID = int32(r.get<uint32>());
                Enviar(p.Write());
                return true;
            }

            // ---- diálogos
            case 0x17D: Gossip335(r); return true;           // SMSG_GOSSIP_MESSAGE
            case 0x17E: { WorldPackets::NPC::GossipComplete p; Enviar(p.Write()); return true; }
            case 0x224:                                      // SMSG_GOSSIP_POI: u32 banderas, float x, float y, u32 icono, u32 importancia, cstr nombre
            {
                WorldPackets::NPC::GossipPOI p;
                p.Flags = r.get<uint32>(); float x = r.get<float>(), y = r.get<float>(); p.Pos = Position(x, y, 0.f);
                p.Icon = int32(r.get<uint32>()); p.Importance = int32(r.get<uint32>()); p.Name = r.cstr();
                Enviar(p.Write());
                return true;
            }

            // ---- misiones
            case 0x185: QuestList335(r); return true;        // SMSG_QUESTGIVER_QUEST_LIST
            case 0x188: QuestDetails335(r); return true;     // SMSG_QUESTGIVER_QUEST_DETAILS
            case 0x05D: QuestQuery335(r); return true;       // SMSG_QUEST_QUERY_RESPONSE
            case 0x18B: RequestItems335(r); return true;     // SMSG_QUESTGIVER_REQUEST_ITEMS
            case 0x18D: OfferReward335(r); return true;      // SMSG_QUESTGIVER_OFFER_REWARD
            case 0x501:                                      // SMSG_QUERY_QUESTS_COMPLETED_RESPONSE: u32 n, u32 misión*
            {
                uint32 n = r.get<uint32>();
                std::vector<uint32> ids;
                for (uint32 i = 0; i < n && r.p + 4 <= b.size(); ++i) ids.push_back(r.get<uint32>());
                mundo->MisionesHechas(ids);
                return true;
            }
            case 0x191:                                      // SMSG_QUESTGIVER_QUEST_COMPLETE: u32 misión, u32 xp, u32 dinero, u32 honor, u32 talentos, u32 arena
            {
                WorldPackets::Quest::QuestGiverQuestComplete p;
                p.QuestID = int32(r.get<uint32>()); p.XPReward = int32(r.get<uint32>()); p.MoneyReward = r.get<uint32>();
                Enviar(p.Write());
                mundo->MisionesHechas({ uint32(p.QuestID) });
                return true;
            }
            case 0x199:                                      // SMSG_QUESTUPDATE_ADD_KILL: u32 misión, u32 entrada(|bit31 objeto), u32 cuenta, u32 total, u64 guid
            {
                WorldPackets::Quest::QuestUpdateAddCredit p;
                p.QuestID = int32(r.get<uint32>()); uint32 e = r.get<uint32>();
                p.ObjectID = int32(e & 0x7FFFFFFF); p.ObjectiveType = (e & 0x80000000) ? 2 : 0;
                p.Count = uint16(r.get<uint32>()); p.Required = uint16(r.get<uint32>()); p.VictimGUID = mundo->ToTc(r.get<uint64>());
                Enviar(p.Write());
                return true;
            }
            case 0x198:                                      // SMSG_QUESTUPDATE_COMPLETE: u32
            {
                WorldPackets::Quest::QuestUpdateComplete p; p.QuestID = int32(r.get<uint32>());
                Enviar(p.Write());
                return true;
            }
            case 0x196: case 0x197:                          // SMSG_QUESTUPDATE_FAILED(TIMER): u32
            {
                WorldPackets::Quest::QuestUpdateFailedTimer p; p.QuestID = r.get<uint32>();
                Enviar(p.Write());
                return true;
            }
            case 0x192:                                      // SMSG_QUESTGIVER_QUEST_FAILED: u32 misión, u32 motivo
            {
                WorldPackets::Quest::QuestGiverQuestFailed p; p.QuestID = r.get<uint32>(); p.Reason = r.get<uint32>();
                Enviar(p.Write());
                return true;
            }
            case 0x18F:                                      // SMSG_QUESTGIVER_QUEST_INVALID: u32 motivo
            {
                WorldPackets::Quest::QuestGiverInvalidQuest p; p.Reason = r.get<uint32>(); p.SendErrorMessage = true;
                Enviar(p.Write());
                return true;
            }
            case 0x195: { WorldPackets::Quest::QuestLogFull p; Enviar(p.Write()); return true; }
            case 0x19C:                                      // SMSG_QUEST_CONFIRM_ACCEPT: u32, cstr, u64
            {
                WorldPackets::Quest::QuestConfirmAcceptResponse p;
                p.QuestID = int32(r.get<uint32>()); p.QuestTitle = r.cstr(); p.InitiatedBy = mundo->ToTc(r.get<uint64>());
                Enviar(p.Write());
                return true;
            }

            // ---- botín
            case 0x160: Loot335(r); return true;             // SMSG_LOOT_RESPONSE
            case 0x162:                                      // SMSG_LOOT_REMOVED: u8 hueco
            {
                WorldPackets::Loot::LootRemoved p;
                { std::lock_guard<std::mutex> l(_mLoot); p.LootObj = _lootObj; p.Owner = mundo->ToTc(_lootDueno); }
                p.LootListID = r.get<uint8>();
                Enviar(p.Write());
                return true;
            }
            case 0x163:                                      // SMSG_LOOT_MONEY_NOTIFY: u32 dinero, u8 solo
            {
                WorldPackets::Loot::LootMoneyNotify p;
                p.Money = r.get<uint32>(); p.SoleLooter = r.get<uint8>() != 0;
                Enviar(p.Write());
                return true;
            }
            case 0x165:                                      // SMSG_LOOT_CLEAR_MONEY
            {
                WorldPackets::Loot::CoinRemoved p;
                { std::lock_guard<std::mutex> l(_mLoot); p.LootObj = _lootObj; }
                Enviar(p.Write());
                return true;
            }
            case 0x161:                                      // SMSG_LOOT_RELEASE_RESPONSE: u64, u8
            {
                WorldPackets::Loot::LootReleaseResponse p;
                uint64 g = r.get<uint64>();
                { std::lock_guard<std::mutex> l(_mLoot); p.LootObj = _lootObj; }
                p.Owner = mundo->ToTc(g);
                Enviar(p.Write());
                return true;
            }

            // ---- entrenadores y ventanas de PNJ
            case 0x1B1: Trainer335(r); return true;          // SMSG_TRAINER_LIST
            case 0x1B4:                                      // SMSG_TRAINER_BUY_FAILED: u64, u32 hechizo, u32 motivo
            {
                WorldPackets::NPC::TrainerBuyFailed p;
                p.TrainerGUID = mundo->ToTc(r.get<uint64>()); p.SpellID = int32(r.get<uint32>()); p.TrainerFailedReason = int32(r.get<uint32>());
                Enviar(p.Write());
                return true;
            }
            case 0x1B3: return true;                         // SMSG_TRAINER_BUY_SUCCEEDED: en 3.4.3 basta con el hechizo aprendido
            case 0x1B8: Abrir(r.get<uint64>(), PlayerInteractionType::Banker); return true;         // SMSG_SHOW_BANK
            case 0x2EB: Abrir(r.get<uint64>(), PlayerInteractionType::Binder); return true;         // SMSG_BINDER_CONFIRM
            case 0x222: Abrir(r.get<uint64>(), PlayerInteractionType::SpiritHealer); return true;   // SMSG_SPIRIT_HEALER_CONFIRM
            case 0x255:                                      // MSG_AUCTION_HELLO: u64 subastador, u32 casa, u8 abierta
            {
                uint64 g = r.get<uint64>();
                { std::lock_guard<std::mutex> l(_mSub); _subastador = g; _casaSubastas = r.get<uint32>(); }
                Abrir(g, PlayerInteractionType::Auctioneer);
                return true;
            }

            // ---- varios de juego
            case 0x1D9:                                      // SMSG_START_MIRROR_TIMER: u32 tipo, u32 valor, u32 máx, i32 escala, u8 pausa, u32 hechizo
            {
                int32 tipo = int32(r.get<uint32>()), val = int32(r.get<uint32>()), mx = int32(r.get<uint32>()), esc = int32(r.get<uint32>());
                bool pausa = r.get<uint8>() != 0; int32 sp = int32(r.get<uint32>());
                WorldPackets::Misc::StartMirrorTimer p(tipo, val, mx, esc, sp, pausa);
                Enviar(p.Write());
                return true;
            }
            case 0x1DA: { int32 tipo = int32(r.get<uint32>()); bool pausa = r.get<uint8>() != 0; WorldPackets::Misc::PauseMirrorTimer p(tipo, pausa); Enviar(p.Write()); return true; }
            case 0x1DB: { WorldPackets::Misc::StopMirrorTimer p(int32(r.get<uint32>())); Enviar(p.Write()); return true; }
            case 0x159:                                      // SMSG_CLIENT_CONTROL_UPDATE: packguid, u8
            {
                WorldPackets::Movement::ControlUpdate p;
                p.Guid = mundo->ToTc(Pack(r)); p.On = r.get<uint8>() != 0;
                Enviar(p.Write());
                // en 3.3.5 el cliente cambiaba solo de móvil al recibir el control; en 3.4.3 lo manda el servidor:
                // Player::SetClientControl -> SetMovedUnit -> SMSG_MOVE_SET_ACTIVE_MOVER (sin él no se mueve el Ojo de Acherus)
                if (p.On) { WorldPackets::Movement::MoveSetActiveMover am; am.MoverGUID = p.Guid; Enviar(am.Write()); }
                return true;
            }
            case 0x13C:                                      // SMSG_AI_REACTION: u64, u32
            {
                WorldPackets::Combat::AIReaction p; p.UnitGUID = mundo->ToTc(r.get<uint64>()); p.Reaction = r.get<uint32>();
                Enviar(p.Write());
                return true;
            }
            case 0x152: { WorldPackets::Combat::BreakTarget p; p.UnitGUID = mundo->ToTc(Pack(r)); Enviar(p.Write()); return true; }
            case 0x0B3:                                      // SMSG_GAMEOBJECT_CUSTOM_ANIM: u64, u32
            {
                WorldPackets::GameObject::GameObjectCustomAnim p; p.ObjectGUID = mundo->ToTc(r.get<uint64>()); p.CustomAnim = r.get<uint32>();
                Enviar(p.Write());
                return true;
            }
            case 0x215: { WorldPackets::GameObject::GameObjectDespawn p; p.ObjectGUID = mundo->ToTc(r.get<uint64>()); Enviar(p.Write()); return true; }
            case 0x1DF: { WorldPackets::GameObject::PageText p; p.GameObjectGUID = mundo->ToTc(r.get<uint64>()); Enviar(p.Write()); return true; }
            case 0x05B:                                      // SMSG_PAGE_TEXT_QUERY_RESPONSE: u32 id, cstr texto, u32 siguiente
            {
                WorldPackets::Query::QueryPageTextResponse p;
                WorldPackets::Query::QueryPageTextResponse::PageTextInfo pg;
                p.PageTextID = r.get<uint32>(); pg.ID = p.PageTextID; pg.Text = r.cstr(); pg.NextPageID = r.get<uint32>();
                p.Allow = true; p.Pages.push_back(pg);
                Enviar(p.Write());
                return true;
            }
            case 0x0AE: { WorldPackets::Item::ReadItemResultOK p; p.Item = mundo->ToTc(r.get<uint64>()); Enviar(p.Write()); return true; }
            case 0x0AF: { WorldPackets::Item::ReadItemResultFailed p; p.Item = mundo->ToTc(r.get<uint64>()); Enviar(p.Write()); return true; }
            case 0x0B0:                                      // SMSG_ITEM_COOLDOWN: u64, u32 hechizo
            {
                WorldPackets::Item::ItemCooldown p; p.ItemGuid = mundo->ToTc(r.get<uint64>()); p.SpellID = r.get<uint32>();
                Enviar(p.Write());
                return true;
            }
            case 0x1D7:                                      // SMSG_ENCHANTMENTLOG: packguid objetivo, packguid lanzador, u32 objeto, u32 encantamiento
            {
                WorldPackets::Item::EnchantmentLog p;
                p.Owner = mundo->ToTc(Pack(r)); p.Caster = mundo->ToTc(Pack(r)); p.ItemID = int32(r.get<uint32>()); p.Enchantment = int32(r.get<uint32>());
                Enviar(p.Write());
                return true;
            }
            case 0x1EB:                                      // SMSG_ITEM_ENCHANT_TIME_UPDATE: u64 objeto, u32 hueco, u32 duración, u64 jugador
            {
                WorldPackets::Item::ItemEnchantTimeUpdate p;
                p.ItemGuid = mundo->ToTc(r.get<uint64>()); p.Slot = r.get<uint32>(); p.DurationLeft = r.get<uint32>(); p.OwnerGuid = mundo->ToTc(r.get<uint64>());
                Enviar(p.Write());
                return true;
            }
            case 0x1EA: { WorldPackets::Item::ItemTimeUpdate p; p.ItemGuid = mundo->ToTc(r.get<uint64>()); p.DurationLeft = r.get<uint32>(); Enviar(p.Write()); return true; }
            case 0x2BD: { WorldPackets::Misc::DurabilityDamageDeath p; p.Percent = 10; Enviar(p.Write()); return true; }
            case 0x39D: { Pack(r); mundo->PuntosCombo(r.get<uint8>()); return true; }    // SMSG_UPDATE_COMBO_POINTS
            case 0x483: case 0x482:                          // SMSG_(HIGHEST_)THREAT_UPDATE: packguid, [packguid mayor], u32 n, (packguid, u32)*
            {
                ObjectGuid unidad = mundo->ToTc(Pack(r));
                ObjectGuid mayor; if (op == 0x482) mayor = mundo->ToTc(Pack(r));
                std::vector<WorldPackets::Combat::ThreatInfo> lista;
                uint32 n = r.get<uint32>();
                for (uint32 i = 0; i < n && r.p < b.size(); ++i) { WorldPackets::Combat::ThreatInfo ti; ti.UnitGUID = mundo->ToTc(Pack(r)); ti.Threat = r.get<uint32>(); lista.push_back(ti); }
                if (op == 0x482) { WorldPackets::Combat::HighestThreatUpdate p; p.UnitGUID = unidad; p.HighestThreatGUID = mayor; p.ThreatList = lista; Enviar(p.Write()); }
                else { WorldPackets::Combat::ThreatUpdate p; p.UnitGUID = unidad; p.ThreatList = lista; Enviar(p.Write()); }
                return true;
            }
            case 0x484: { WorldPackets::Combat::ThreatRemove p; p.UnitGUID = mundo->ToTc(Pack(r)); p.AboutGUID = mundo->ToTc(Pack(r)); Enviar(p.Write()); return true; }
            case 0x485: { WorldPackets::Combat::ThreatClear p; p.UnitGUID = mundo->ToTc(Pack(r)); Enviar(p.Write()); return true; }

            // ---- hermandades
            case 0x055: GuildQuery335(r); return true;       // SMSG_GUILD_QUERY_RESPONSE
            case 0x08A: GuildRoster335(r); return true;      // SMSG_GUILD_ROSTER
            case 0x092: GuildEvent335(r, b.size()); return true;   // SMSG_GUILD_EVENT
            case 0x083:                                      // SMSG_GUILD_INVITE: cstr quien, cstr hermandad
            {
                WorldPackets::Guild::GuildInvite p;
                p.InviterName = r.cstr(); p.GuildName = r.cstr();
                p.InviterVirtualRealmAddress = realmAddress; p.GuildVirtualRealmAddress = realmAddress;
                Enviar(p.Write());
                return true;
            }
            case 0x086: Sistema(r.cstr() + " rechaza tu invitación a la hermandad."); return true;   // SMSG_GUILD_DECLINE (TC no tiene el paquete 3.4.3)
            case 0x093:                                      // SMSG_GUILD_COMMAND_RESULT: u32 orden, cstr, u32 resultado
            {
                WorldPackets::Guild::GuildCommandResult p;
                p.Command = int32(r.get<uint32>()); p.Name = r.cstr(); p.Result = int32(r.get<uint32>());
                Enviar(p.Write());
                return true;
            }

            // ---- correo
            case 0x23B: MailList335(r); return true;         // SMSG_MAIL_LIST_RESULT
            case 0x239:                                      // SMSG_SEND_MAIL_RESULT: u32 carta, u32 acción, u32 error, [u32 error de equipo | u32 objeto, u32 cantidad]
            {
                WorldPackets::Mail::MailCommandResult p;
                p.MailID = r.get<uint32>(); p.Command = int32(r.get<uint32>()); p.ErrorCode = int32(r.get<uint32>());
                if (p.ErrorCode == 1 && r.p + 4 <= b.size()) p.BagResult = kInventoryResult[r.get<uint32>() & 0xFF];   // MAIL_ERR_EQUIP_ERROR
                else if (p.Command == 2 && r.p + 8 <= b.size()) { p.AttachID = r.get<uint32>(); p.QtyInInventory = int32(r.get<uint32>()); }   // MAIL_ITEM_TAKEN
                Enviar(p.Write());
                return true;
            }
            case 0x285:                                      // SMSG_RECEIVED_MAIL: u32
            {
                WorldPacket pkt(SMSG_NOTIFY_RECEIVED_MAIL, 4);
                pkt << float(0.0f);
                Enviar(&pkt);
                return true;
            }
            case 0x284:                                      // MSG_QUERY_NEXT_MAIL_TIME: float, u32 n, (u64, u32, u32 tipo, u32 papel, float)*
            {
                float prox = r.get<float>();
                uint32 n = r.get<uint32>();
                ByteBuffer lista; uint32 k = 0;
                for (uint32 i = 0; i < n && r.p + 24 <= b.size(); ++i, ++k)
                {
                    ObjectGuid g = mundo->ToTc(r.get<uint64>()); int32 alt = int32(r.get<uint32>()); int8 tipo = int8(r.get<uint32>());
                    int32 papel = int32(r.get<uint32>()); float queda = r.get<float>();
                    lista << g << float(queda) << int32(alt) << int8(tipo) << int32(papel);
                }
                WorldPacket pkt(SMSG_MAIL_QUERY_NEXT_TIME_RESULT, 8 + lista.size());
                pkt << float(prox) << int32(k);
                pkt.append(lista);
                Enviar(&pkt);
                return true;
            }

            // ---- vuelos
            case 0x1A9:                                      // SMSG_SHOWTAXINODES: u32 1, u64, u32 nodo actual, u32 máscara*
            {
                WorldPackets::Taxi::ShowTaxiNodes p;
                r.get<uint32>();
                p.WindowInfo.emplace();
                p.WindowInfo->UnitGUID = mundo->ToTc(r.get<uint64>());
                p.WindowInfo->CurrentNode = int32(r.get<uint32>());
                { std::lock_guard<std::mutex> l(_mTaxi); _nodoActual = uint32(p.WindowInfo->CurrentNode); _taxiConocidos.clear(); }
                for (size_t k = 0; r.p + 4 <= b.size(); k += 4)
                {
                    uint32 m = r.get<uint32>();
                    for (size_t j = 0; j < 4; ++j)
                        if (k + j < p.CanLandNodes.size()) { p.CanLandNodes[k + j] = uint8(m >> (j * 8)); p.CanUseNodes[k + j] = uint8(m >> (j * 8)); }
                    std::lock_guard<std::mutex> l(_mTaxi);
                    _taxiConocidos.push_back(m);
                }
                Enviar(p.Write());
                return true;
            }
            case 0x1AB:                                      // SMSG_TAXINODE_STATUS: u64, u8
            {
                WorldPackets::Taxi::TaxiNodeStatus p;
                p.Unit = mundo->ToTc(r.get<uint64>()); p.Status = r.get<uint8>();
                Enviar(p.Write());
                return true;
            }
            case 0x1AE: { WorldPackets::Taxi::ActivateTaxiReply p; p.Reply = uint8(r.get<uint32>()); Enviar(p.Write()); return true; }
            case 0x1AF: { WorldPackets::Taxi::NewTaxiPath p; Enviar(p.Write()); return true; }

            // ---- amigos e ignorados
            case 0x067: Contactos335(r); return true;       // SMSG_CONTACT_LIST
            case 0x068:                                      // SMSG_FRIEND_STATUS: u8 resultado, u64, [nota, estado...]
            {
                WorldPackets::Social::FriendStatus p;
                p.FriendResult = r.get<uint8>();
                uint64 g = r.get<uint64>();
                p.Guid = mundo->ToTc(g); p.WowAccountGuid = ObjectGuid::Create<HighGuid::WowAccount>(0); p.VirtualRealmAddress = realmAddress;
                if (p.FriendResult == 0x06 || p.FriendResult == 0x02)   // FRIEND_ADDED_ONLINE / FRIEND_ONLINE: u8 estado, u32 zona, u32 nivel, u32 clase
                {
                    if (p.FriendResult == 0x06 && r.p < b.size()) p.Notes = r.cstr();
                    p.Status = r.get<uint8>(); p.AreaID = r.get<uint32>(); p.Level = r.get<uint32>(); p.ClassID = r.get<uint32>();
                }
                else if (p.FriendResult == 0x07 && r.p < b.size()) p.Notes = r.cstr();          // FRIEND_ADDED_OFFLINE
                Enviar(p.Write());
                return true;
            }

            // ---- duelos
            case 0x167:                                      // SMSG_DUEL_REQUESTED: u64 bandera, u64 retador
            {
                WorldPackets::Duel::DuelRequested p;
                p.ArbiterGUID = mundo->ToTc(r.get<uint64>()); p.RequestedByGUID = mundo->ToTc(r.get<uint64>());
                p.RequestedByWowAccount = ObjectGuid::Create<HighGuid::WowAccount>(0);
                { std::lock_guard<std::mutex> l(_mTaxi); _arbitroDuelo = p.ArbiterGUID; }
                Enviar(p.Write());
                return true;
            }
            case 0x168: { WorldPackets::Duel::DuelOutOfBounds p; Enviar(p.Write()); return true; }
            case 0x169: { WorldPackets::Duel::DuelInBounds p; Enviar(p.Write()); return true; }
            case 0x16A: { WorldPackets::Duel::DuelComplete p; p.Started = r.get<uint8>() != 0; Enviar(p.Write()); return true; }
            case 0x16B:                                      // SMSG_DUEL_WINNER: u8 huyó, cstr perdedor, cstr ganador
            {
                WorldPackets::Duel::DuelWinner p;
                p.Fled = r.get<uint8>() != 0; p.BeatenName = r.cstr(); p.WinnerName = r.cstr();
                p.BeatenVirtualRealmAddress = realmAddress; p.WinnerVirtualRealmAddress = realmAddress;
                Enviar(p.Write());
                return true;
            }
            case 0x2B7: { WorldPackets::Duel::DuelCountdown p(r.get<uint32>()); Enviar(p.Write()); return true; }

            // ---- tiempo y avisos
            case 0x1CD:                                      // SMSG_PLAYED_TIME: u32 total, u32 nivel, u8
            {
                WorldPackets::Character::PlayedTime p;
                p.TotalTime = int32(r.get<uint32>()); p.LevelTime = int32(r.get<uint32>()); p.TriggerEvent = r.get<uint8>() != 0;
                Enviar(p.Write());
                return true;
            }
            case 0x1CF: { WorldPackets::Query::QueryTimeResponse p; p.CurrentTime = time_t(r.get<uint32>()); Enviar(p.Write()); return true; }
            case 0x1CB: { WorldPackets::Chat::PrintNotification p(r.cstr()); Enviar(p.Write()); return true; }   // SMSG_NOTIFICATION
            case 0x291:                                      // SMSG_SERVER_MESSAGE: u32 tipo, cstr
            {
                WorldPackets::Chat::ChatServerMessage p;
                p.MessageID = int32(r.get<uint32>());
                std::string s = r.cstr(); p.StringParam = s;
                Enviar(p.Write());
                return true;
            }
            case 0x2B8: { r.get<uint32>(); Sistema(r.cstr()); return true; }                                   // SMSG_AREA_TRIGGER_MESSAGE

            // ---- grupos
            case 0x06F:                                      // SMSG_GROUP_INVITE: u8 puede aceptar, cstr quien, u32, u8 n, u32
            {
                WorldPackets::Party::PartyInvite p;
                p.CanAccept = r.get<uint8>() != 0;
                p.InviterName = r.cstr();
                p.InviterGUID = GuidPorNombre(p.InviterName);
                std::string norm = Cfg::RealmName; norm.erase(std::remove(norm.begin(), norm.end(), ' '), norm.end());
                p.InviterRealm = WorldPackets::Auth::VirtualRealmInfo(realmAddress, true, false, Cfg::RealmName, norm);
                Enviar(p.Write());
                return true;
            }
            case 0x07D: GroupList335(r); return true;        // SMSG_GROUP_LIST
            case 0x07E: case 0x2F2: MemberStats335(r, op == 0x2F2); return true;   // SMSG_PARTY_MEMBER_STATS(_FULL)
            case 0x07F:                                      // SMSG_PARTY_COMMAND_RESULT: u32 operación, cstr nombre, u32 resultado, u32 dato
            {
                WorldPackets::Party::PartyCommandResult p;
                p.Command = uint8(r.get<uint32>()); p.Name = r.cstr(); p.Result = uint8(r.get<uint32>());
                if (r.p + 4 <= b.size()) p.ResultData = r.get<uint32>();
                Enviar(p.Write());
                return true;
            }
            case 0x074: { WorldPackets::Party::GroupDecline p(r.cstr()); Enviar(p.Write()); return true; }
            case 0x077: { WorldPackets::Party::GroupUninvite p; Enviar(p.Write()); return true; }
            case 0x079: { WorldPackets::Party::GroupNewLeader p; p.Name = r.cstr(); Enviar(p.Write()); return true; }
            case 0x07C: { WorldPackets::Party::GroupDestroyed p; Enviar(p.Write()); return true; }
            case 0x1D5:                                      // MSG_MINIMAP_PING: u64, float x, float y
            {
                WorldPackets::Party::MinimapPing p;
                p.Sender = mundo->ToTc(r.get<uint64>()); p.PositionX = r.get<float>(); p.PositionY = r.get<float>();
                Enviar(p.Write());
                return true;
            }
            case 0x1F5: return true;                         // SMSG_PARTYKILLLOG

            // ---- talentos y modificadores
            case 0x4C0:                                      // SMSG_TALENTS_INFO
            {
                WorldPackets::Talent::UpdateTalentData p;
                p.IsPetTalents = r.get<uint8>() != 0;
                p.UnspentTalentPoints = r.get<uint32>();
                if (p.IsPetTalents)
                {
                    WorldPackets::Talent::TalentGroupInfo g;
                    uint8 n = r.get<uint8>();
                    for (uint8 i = 0; i < n && r.p + 5 <= b.size(); ++i) { WorldPackets::Talent::TalentInfo ti; ti.TalentID = r.get<uint32>(); ti.Rank = r.get<uint8>(); g.Talents.push_back(ti); }
                    p.TalentGroupInfos.push_back(g);
                }
                else
                {
                    uint8 grupos = r.get<uint8>(); p.ActiveGroup = r.get<uint8>();
                    for (uint8 k = 0; k < grupos && r.p < b.size(); ++k)
                    {
                        WorldPackets::Talent::TalentGroupInfo g;
                        uint8 n = r.get<uint8>();
                        for (uint8 i = 0; i < n && r.p + 5 <= b.size(); ++i) { WorldPackets::Talent::TalentInfo ti; ti.TalentID = r.get<uint32>(); ti.Rank = r.get<uint8>(); g.Talents.push_back(ti); }
                        uint8 ng = r.get<uint8>();
                        for (uint8 i = 0; i < ng && r.p + 2 <= b.size(); ++i) g.GlyphIDs.push_back(r.get<uint16>());
                        p.TalentGroupInfos.push_back(g);
                    }
                }
                Enviar(p.Write());
                return true;
            }
            case 0x266: case 0x267:                          // SMSG_SET_FLAT/PCT_SPELL_MODIFIER: u8 bit, u8 operación, i32 valor
            {
                WorldPackets::Spells::SetSpellModifier p(op == 0x266 ? SMSG_SET_FLAT_SPELL_MODIFIER : SMSG_SET_PCT_SPELL_MODIFIER);
                uint8 bit = r.get<uint8>(), modop = r.get<uint8>(); int32 v = int32(r.get<uint32>());
                WorldPackets::Spells::SpellModifier m; m.ModIndex = modop;
                WorldPackets::Spells::SpellModifierData md; md.ClassIndex = bit;
                md.ModifierValue = op == 0x266 ? float(v) : 1.0f + float(v) * 0.01f;
                m.ModifierData.push_back(md);
                p.Modifiers.push_back(m);
                Enviar(p.Write());
                return true;
            }

            // ---- muerte
            case 0x216:                                      // MSG_CORPSE_QUERY: u8 hay, [i32 mapa, x, y, z, i32 mapa real, u32]
            {
                WorldPackets::Query::CorpseLocation p;
                p.Player = Yo();
                p.Valid = r.get<uint8>() != 0;
                if (p.Valid)
                {
                    p.MapID = int32(r.get<uint32>());
                    float x = r.get<float>(), y = r.get<float>(), z = r.get<float>();
                    p.Position = Position(x, y, z);
                    p.ActualMapID = int32(r.get<uint32>());
                }
                Enviar(p.Write());
                return true;
            }
            case 0x269:                                      // SMSG_CORPSE_RECLAIM_DELAY: u32 ms
            {
                WorldPackets::Misc::CorpseReclaimDelay p; p.Remaining = r.get<uint32>();
                Enviar(p.Write());
                return true;
            }
            case 0x378:                                      // SMSG_DEATH_RELEASE_LOC: u32 mapa, x, y, z
            {
                WorldPackets::Misc::DeathReleaseLoc p;
                p.MapID = int32(r.get<uint32>());
                float x = r.get<float>(), y = r.get<float>(), z = r.get<float>();
                p.Loc = Position(x, y, z);
                Enviar(p.Write());
                return true;
            }
            case 0x15B:                                      // SMSG_RESURRECT_REQUEST: u64, u32 largo, cstr nombre, u8, u8 mareo
            {
                WorldPackets::Spells::ResurrectRequest p;
                p.ResurrectOffererGUID = mundo->ToTc(r.get<uint64>());
                p.ResurrectOffererVirtualRealmAddress = realmAddress;
                r.get<uint32>(); p.Name = r.cstr(); r.get<uint8>();
                p.Sickness = r.p < r.b.size() && r.get<uint8>() != 0;
                p.UseTimer = true;
                Enviar(p.Write());
                return true;
            }

            // ---- mascotas
            case 0x179: PetSpells335(r, b.size()); return true;   // SMSG_PET_SPELLS
            case 0x053:                                      // SMSG_PET_NAME_QUERY_RESPONSE: u32 número, cstr nombre, u32 marca, u8 declinado
            {
                WorldPackets::Query::QueryPetNameResponse p;
                uint32 num = r.get<uint32>();
                {
                    std::lock_guard<std::mutex> l(_mPet);
                    auto it = _mascotasPorNumero.find(num);
                    if (it != _mascotasPorNumero.end()) p.UnitGUID = it->second;
                }
                p.Name = r.cstr();
                p.Allow = !p.Name.empty();
                if (r.p + 4 <= b.size()) p.Timestamp = time_t(r.get<uint32>());
                Enviar(p.Write());
                return true;
            }
            case 0x17A:                                      // SMSG_PET_MODE: u64, u8 reacción, u8 orden, u16 banderas
            {
                WorldPackets::Pet::PetMode p;
                p.PetGUID = mundo->ToTc(r.get<uint64>());
                p.ReactState = ReactStates(r.get<uint8>()); p.CommandState = CommandStates(r.get<uint8>());
                p.Flag = uint8(r.get<uint16>());
                Enviar(p.Write());
                return true;
            }
            case 0x138:                                      // SMSG_PET_CAST_FAILED: u8 cuenta, u32 hechizo, u8 resultado
            {
                WorldPackets::Spells::PetCastFailed p;
                uint8 cc = r.get<uint8>(); p.SpellID = int32(r.get<uint32>());
                p.CastID = CastId(0, cc, uint32(p.SpellID));
                p.Reason = kSpellCastResult[r.get<uint8>()];
                Enviar(p.Write());
                return true;
            }
            case 0x2C6:                                      // SMSG_PET_ACTION_FEEDBACK: u8
            {
                WorldPackets::Pet::PetActionFeedback p;
                p.Response = ::PetActionFeedback(r.get<uint8>());
                Enviar(p.Write());
                return true;
            }
            case 0x324:                                      // SMSG_PET_ACTION_SOUND: u64, u32
            {
                WorldPackets::Pet::PetActionSound p;
                p.UnitGUID = mundo->ToTc(r.get<uint64>()); p.Action = int32(r.get<uint32>());
                Enviar(p.Write());
                return true;
            }
            case 0x499: case 0x49A:                          // SMSG_PET_LEARNED_SPELL / UNLEARNED_SPELL: u32
            {
                uint32 s = r.get<uint32>();
                if (op == 0x499) { WorldPackets::Pet::PetLearnedSpells p; p.Spells.push_back(s); Enviar(p.Write()); }
                else { WorldPackets::Pet::PetUnlearnedSpells p; p.Spells.push_back(s); Enviar(p.Write()); }
                return true;
            }
            case 0x4AA: case 0x325: case 0x475: return true; // SMSG_PET_GUIDS / DISMISS_SOUND / RENAMEABLE

            // ---- inventario
            case 0x112:                                      // SMSG_INVENTORY_CHANGE_FAILURE: u8 res, [u64, u64, u8, extra]
            {
                WorldPackets::Item::InventoryChangeFailure p;
                uint8 res = r.get<uint8>();
                p.BagResult = kInventoryResult[res];
                if (res)
                {
                    p.Item[0] = mundo->ToTc(r.get<uint64>()); p.Item[1] = mundo->ToTc(r.get<uint64>());
                    p.ContainerBSlot = r.get<uint8>();
                    if ((res == 1 || res == 53) && r.p + 4 <= b.size()) p.Level = int32(r.get<uint32>());   // CANT_EQUIP_LEVEL_I / PURCHASE_LEVEL_TOO_LOW
                }
                Enviar(p.Write());
                return true;
            }
            case 0x166:                                      // SMSG_ITEM_PUSH_RESULT
            {
                WorldPackets::Item::ItemPushResult p;
                p.PlayerGUID = mundo->ToTc(r.get<uint64>());
                bool recibido = r.get<uint32>() != 0, creado = r.get<uint32>() != 0, chat = r.get<uint32>() != 0;
                uint8 bolsa = r.get<uint8>(); int32 hueco = int32(r.get<uint32>());
                p.Item.ItemID = r.get<uint32>(); r.get<uint32>(); p.Item.RandomPropertiesID = int32(r.get<uint32>());
                p.Quantity = int32(r.get<uint32>()); p.QuantityInInventory = int32(r.get<uint32>());
                p.Slot = bolsa == 255 ? 255 : M335::HuecoA343(bolsa);
                p.SlotInBag = (bolsa == 255 && hueco >= 0) ? int32(M335::HuecoA343(uint32(hueco))) : hueco;
                p.Pushed = recibido; p.Created = creado;
                p.DisplayText = chat ? WorldPackets::Item::ItemPushResult::DISPLAY_TYPE_NORMAL : WorldPackets::Item::ItemPushResult::DISPLAY_TYPE_HIDDEN;
                // moneda del propio jugador (emblemas, sellos, marcas): en 3.4.3 no es un objeto, es un SetCurrency con el total
                if (uint32 mon = M335::MonedaDeObjeto(uint32(p.Item.ItemID)); mon && p.PlayerGUID == mundo->ToTc(yo335))
                {
                    WorldPackets::Misc::SetCurrency c;
                    c.Type = int32(mon); c.Quantity = p.QuantityInInventory; c.QuantityChange = p.Quantity; c.SuppressChatLog = !chat;
                    Enviar(c.Write());
                    mundo->AnotaMoneda(mon, p.QuantityInInventory);
                    return true;
                }
                Enviar(p.Write());
                return true;
            }
            case 0x19F:                                      // SMSG_LIST_INVENTORY: u64 vendedor, u8 n, (u32 hueco+1, u32 objeto, u32 modelo, i32 existencias, u32 precio, u32 durab., u32 lote, u32 coste ext.)*
            {
                WorldPackets::NPC::VendorInventory p;
                p.Vendor = mundo->ToTc(r.get<uint64>());
                uint8 n = r.get<uint8>();
                for (uint8 i = 0; i < n && r.p + 32 <= b.size(); ++i)
                {
                    WorldPackets::NPC::VendorItem it;
                    it.MuID = int32(r.get<uint32>()); it.Item.ItemID = r.get<uint32>(); r.get<uint32>();
                    it.Quantity = int32(r.get<uint32>()); it.Price = r.get<uint32>(); it.Durability = int32(r.get<uint32>());
                    it.StackCount = int32(r.get<uint32>()); it.ExtendedCostID = int32(r.get<uint32>());
                    it.Type = 1;                             // ITEM_VENDOR_TYPE_ITEM
                    p.Items.push_back(it);
                }
                if (n == 0) p.Reason = 0;
                Enviar(p.Write());
                return true;
            }
            case 0x1A1:                                      // SMSG_SELL_ITEM: u64 vendedor, u64 objeto, u8 error
            {
                WorldPackets::Item::SellResponse p;
                p.VendorGUID = mundo->ToTc(r.get<uint64>()); p.ItemGUIDs.push_back(mundo->ToTc(r.get<uint64>()));
                uint8 e = r.get<uint8>(); p.Reason = SellResult(e < std::size(kSellResult) ? kSellResult[e] : 0);
                Enviar(p.Write());
                return true;
            }
            case 0x1A4:                                      // SMSG_BUY_ITEM: u64 vendedor, u32 hueco, i32 quedan, u32 cantidad
            {
                WorldPackets::Item::BuySucceeded p;
                p.VendorGUID = mundo->ToTc(r.get<uint64>()); p.Muid = r.get<uint32>();
                p.NewQuantity = int32(r.get<uint32>()); p.QuantityBought = r.get<uint32>();
                Enviar(p.Write());
                return true;
            }
            case 0x1A5:                                      // SMSG_BUY_FAILED: u64 vendedor, u32 objeto, u8 error
            {
                WorldPackets::Item::BuyFailed p;
                p.VendorGUID = mundo->ToTc(r.get<uint64>()); p.Muid = r.get<uint32>();
                uint8 e = r.get<uint8>(); p.Reason = BuyResult(e < std::size(kBuyResult) ? kBuyResult[e] : 0);
                Enviar(p.Write());
                return true;
            }

            // ---- varios
            case 0x1D4:                                      // SMSG_LEVELUP_INFO: u32 nivel, u32 vida, 7 x u32 poder, 5 x u32 atributos
            {
                WorldPackets::Misc::LevelUpInfo p;
                p.Level = int32(r.get<uint32>()); p.HealthDelta = int32(r.get<uint32>());
                int32 pw[7]; for (int32& x : pw) x = int32(r.get<uint32>());
                for (size_t i = 0; i < p.PowerDelta.size() && i < 7; ++i) p.PowerDelta[i] = pw[i];
                for (size_t i = 0; i < p.StatDelta.size(); ++i) p.StatDelta[i] = int32(r.get<uint32>());
                Enviar(p.Write());
                return true;
            }
            case 0x04C:                                      // SMSG_LOGOUT_RESPONSE: u32 resultado, u8 instantáneo
            {
                WorldPackets::Character::LogoutResponse p;
                p.LogoutResult = int32(r.get<uint32>()); p.Instant = r.get<uint8>() != 0;
                Enviar(p.Write());
                return true;
            }
            case 0x04D: { WorldPackets::Character::LogoutComplete p; Enviar(p.Write()); if (AlSalir) AlSalir(); return true; }
            case 0x04F: { WorldPackets::Character::LogoutCancelAck p; Enviar(p.Write()); return true; }
            case 0x129: ActionButtons(r); return true;
            case 0x122: Factions(r); return true;
            case 0x127:                                      // SMSG_SET_PROFICIENCY: u8 clase, u32 máscara
            {
                WorldPackets::Item::SetProficiency p;
                p.ProficiencyClass = r.get<uint8>(); p.ProficiencyMask = r.get<uint32>();
                Enviar(p.Write());
                return true;
            }
            case 0x155:                                      // SMSG_BINDPOINTUPDATE: x, y, z, u32 mapa, u32 área
            {
                WorldPackets::Misc::BindPointUpdate p;
                float x = r.get<float>(), y = r.get<float>(), z = r.get<float>();
                p.BindPosition = Position(x, y, z);
                p.BindMapID = r.get<uint32>(); p.BindAreaID = r.get<uint32>();
                Enviar(p.Write());
                return true;
            }
            case 0x495: case 0x496: Auras(r, op == 0x495); return true;
            case 0x480:                                      // SMSG_POWER_UPDATE: packguid, u8 tipo, u32 valor
            {
                WorldPackets::Combat::PowerUpdate p;
                p.Guid = mundo->ToTc(Pack(r));
                uint8 tipo = r.get<uint8>(); int32 val = int32(r.get<uint32>());
                p.Powers.emplace_back(val, tipo);
                Enviar(p.Write());
                return true;
            }
            case 0x29D:                                      // SMSG_STANDSTATE_UPDATE: u8
            {
                WorldPackets::Misc::StandStateUpdate p;
                p.State = UnitStandStateType(r.get<uint8>());
                Enviar(p.Write());
                return true;
            }
            case 0x2C2:                                      // SMSG_INIT_WORLD_STATES: u32 mapa, zona, área, u16 n, (u32, u32)*
            {
                WorldPackets::WorldState::InitWorldStates p;
                p.MapID = int32(r.get<uint32>()); p.AreaID = int32(r.get<uint32>()); p.SubareaID = int32(r.get<uint32>());
                uint16 n = r.get<uint16>();
                for (uint16 i = 0; i < n && r.p + 8 <= b.size(); ++i)
                {
                    int32 id = int32(r.get<uint32>()), v = int32(r.get<uint32>());
                    p.Worldstates.emplace_back(id, v);
                }
                // fases de contenido de Classic: BattlemasterList del 3.4.3 exige estos world states (PlayerCondition con
                // WorldStateExpression) para enseñar cada campo de batalla. AzerothCore los tiene todos abiertos.
                for (int32 ws : { 17224, 17225, 17227, 21975 })       // Alterac, Garganta Grito de Guerra, Arathi, Isla de la Conquista
                    p.Worldstates.emplace_back(ws, 1);
                Enviar(p.Write());
                return true;
            }
            case 0x2C3:                                      // SMSG_UPDATE_WORLD_STATE: u32 id, u32 valor
            {
                WorldPackets::WorldState::UpdateWorldState p;
                p.VariableID = r.get<uint32>(); p.Value = int32(r.get<uint32>());
                Enviar(p.Write());
                return true;
            }
            case 0x2F4:                                      // SMSG_WEATHER: u32 estado, float intensidad, u8 brusco
            {
                uint32 st = r.get<uint32>(); float g = r.get<float>(); bool ab = r.get<uint8>() != 0;
                WorldPackets::Misc::Weather p(WeatherState(st), g, ab);
                Enviar(p.Write());
                return true;
            }
            case 0x2D2:                                      // SMSG_PLAY_SOUND: u32 sonido
            {
                WorldPackets::Misc::PlaySound p(ObjectGuid::Empty, int32(r.get<uint32>()), 0);
                Enviar(p.Write());
                return true;
            }
            case 0x103:                                      // SMSG_EMOTE: u32 emote, u64 guid
            {
                WorldPackets::Chat::Emote p;
                p.EmoteID = r.get<uint32>(); p.Guid = mundo->ToTc(r.get<uint64>());
                Enviar(p.Write());
                return true;
            }
            case 0x1D0:                                      // SMSG_LOG_XPGAIN: u64 víctima, u32 total, u8 tipo, [u32 sin bono, float]
            {
                WorldPackets::Character::LogXPGain p;
                p.Victim = mundo->ToTc(r.get<uint64>());
                p.Amount = int32(r.get<uint32>());
                p.Reason = r.get<uint8>();
                p.Original = p.Amount;
                if (p.Reason == 0) { p.Original = int32(r.get<uint32>()); p.GroupBonus = r.get<float>(); }
                Enviar(p.Write());
                return true;
            }
            case 0x1F8:                                      // SMSG_EXPLORATION_EXPERIENCE: u32 área, u32 xp
            {
                WorldPackets::Misc::ExplorationExperience p;
                p.AreaID = int32(r.get<uint32>()); p.Experience = int32(r.get<uint32>());
                Enviar(p.Write());
                return true;
            }
            case 0x183:                                      // SMSG_QUESTGIVER_STATUS: u64 guid, u8 estado
            {
                WorldPackets::Quest::QuestGiverStatus p;
                p.QuestGiver.Guid = mundo->ToTc(r.get<uint64>());
                p.QuestGiver.Status = EstadoMision(r.get<uint8>());
                Enviar(p.Write());
                return true;
            }
            case 0x418:                                      // SMSG_QUESTGIVER_STATUS_MULTIPLE: u32 n, (u64 guid, u8 estado)*
            {
                WorldPackets::Quest::QuestGiverStatusMultiple p;
                uint32 n = r.get<uint32>();
                for (uint32 i = 0; i < n && r.p + 9 <= b.size(); ++i)
                {
                    ObjectGuid g = mundo->ToTc(r.get<uint64>());
                    p.QuestGiver.emplace_back(g, EstadoMision(r.get<uint8>()));
                }
                Enviar(p.Write());
                return true;
            }
            case 0x3C9:                                      // SMSG_FEATURE_SYSTEM_STATUS (en el mundo): el de TrinityCore por defecto
            {
                WorldPackets::System::FeatureSystemStatus p;
                p.ComplaintStatus = 2;
                p.CfgRealmID = Cfg::RealmId;
                p.EuropaTicketSystemStatus.emplace();
                p.EuropaTicketSystemStatus->ThrottleState.MaxTries = 10;
                p.EuropaTicketSystemStatus->ThrottleState.PerMilliseconds = 60000;
                p.EuropaTicketSystemStatus->ThrottleState.TryCount = 1;
                p.EuropaTicketSystemStatus->ThrottleState.LastResetTimeBeforeNow = 111111;
                p.EuropaTicketSystemStatus->TicketsEnabled = true;
                p.EuropaTicketSystemStatus->BugsEnabled = true;
                p.EuropaTicketSystemStatus->ComplaintsEnabled = true;
                p.EuropaTicketSystemStatus->SuggestionsEnabled = true;
                // BrowserEnabled se queda en false: con true el cliente casca al abrir la ayuda (nota de TC)
                Enviar(p.Write());
                return true;
            }
            case 0x0FD:                                      // SMSG_TUTORIAL_FLAGS: 8 x u32
            {
                WorldPackets::Misc::TutorialFlags p;
                for (int i = 0; i < 8; ++i) p.TutorialData[i] = r.get<uint32>();
                Enviar(p.Write());
                return true;
            }
            case 0x2A5:                                      // SMSG_SET_FORCED_REACTIONS: u32 n, (u32 facción, u32 reacción)*
            {
                WorldPackets::Reputation::SetForcedReactions p;
                uint32 n = r.get<uint32>();
                for (uint32 i = 0; i < n && r.p + 8 <= b.size(); ++i)
                {
                    WorldPackets::Reputation::ForcedReaction fr;
                    fr.Faction = int32(r.get<uint32>()); fr.Reaction = int32(r.get<uint32>());
                    p.Reactions.push_back(fr);
                }
                Enviar(p.Write());
                return true;
            }
            // ---- tiradas de botín en grupo (la guid del botín 3.3.5 es la del cadáver/objeto)
            case 0x2A1:                                      // SMSG_LOOT_START_ROLL: u64, u32 mapa, u32 hueco, u32 objeto, u32 sufijo, u32 prop, u32 cant, u32 ms, u8 máscara
            {
                WorldPackets::Loot::StartLootRoll p;
                uint64 src = r.get<uint64>();
                p.LootObj = TiradaObj(src);
                p.MapID = int32(r.get<uint32>());
                p.Item.LootListID = uint8(r.get<uint32>());
                p.Item.Loot.ItemID = r.get<uint32>(); r.get<uint32>();
                p.Item.Loot.RandomPropertiesID = int32(r.get<uint32>());
                p.Item.Quantity = r.get<uint32>();
                p.RollTime = Milliseconds(r.get<uint32>());
                p.ValidRolls = r.get<uint8>();
                p.Method = 3;                                // GROUP_LOOT
                Enviar(p.Write());
                return true;
            }
            case 0x2A2:                                      // SMSG_LOOT_ROLL: u64, u32 hueco, u64 jugador, u32 objeto, u32, u32, u8 número, u8 tipo, u8 autopase
            {
                WorldPackets::Loot::LootRollBroadcast p;
                p.LootObj = TiradaObj(r.get<uint64>());
                p.Item.LootListID = uint8(r.get<uint32>());
                p.Player = mundo->ToTc(r.get<uint64>());
                p.Item.Loot.ItemID = r.get<uint32>(); r.get<uint32>();
                p.Item.Loot.RandomPropertiesID = int32(r.get<uint32>());
                uint8 num = r.get<uint8>(); p.RollType = r.get<uint8>(); p.Autopassed = r.get<uint8>() != 0;
                p.Roll = num >= 128 ? -1 : int32(num);
                Enviar(p.Write());
                return true;
            }
            case 0x29F:                                      // SMSG_LOOT_ROLL_WON: u64, u32 hueco, u32 objeto, u32, u32, u64 ganador, u8 número, u8 tipo
            {
                WorldPackets::Loot::LootRollWon p;
                p.LootObj = TiradaObj(r.get<uint64>());
                p.Item.LootListID = uint8(r.get<uint32>());
                p.Item.Loot.ItemID = r.get<uint32>(); r.get<uint32>();
                p.Item.Loot.RandomPropertiesID = int32(r.get<uint32>());
                p.Winner = mundo->ToTc(r.get<uint64>());
                p.Roll = int32(r.get<uint8>()); p.RollType = r.get<uint8>();
                p.MainSpec = true;
                Enviar(p.Write());
                return true;
            }
            case 0x29E:                                      // SMSG_LOOT_ALL_PASSED: u64, u32 hueco, u32 objeto, u32 prop, u32 sufijo
            {
                WorldPackets::Loot::LootAllPassed p;
                p.LootObj = TiradaObj(r.get<uint64>());
                p.Item.LootListID = uint8(r.get<uint32>());
                p.Item.Loot.ItemID = r.get<uint32>();
                p.Item.Loot.RandomPropertiesID = int32(r.get<uint32>());
                Enviar(p.Write());
                return true;
            }
            case 0x2A4:                                      // SMSG_LOOT_MASTER_LIST: u8 n, u64*
            {
                WorldPackets::Loot::MasterLootCandidateList p;
                uint8 n = r.get<uint8>();
                for (uint8 i = 0; i < n && r.p + 8 <= b.size(); ++i) p.Players.insert(mundo->ToTc(r.get<uint64>()));
                { std::lock_guard<std::mutex> l(_mLoot); p.LootObj = _lootObj; }
                Enviar(p.Write());
                return true;
            }
            case 0x2AB:                                      // SMSG_SUMMON_REQUEST: u64 invocador, u32 zona, u32 ms
            {
                WorldPackets::Movement::SummonRequest p;
                p.SummonerGUID = mundo->ToTc(r.get<uint64>());
                p.SummonerVirtualRealmAddress = realmAddress;
                p.AreaID = int32(r.get<uint32>());
                Enviar(p.Write());
                return true;
            }
            case 0x2CC:                                      // SMSG_RAID_INSTANCE_INFO: u32 n, (u32 mapa, u32 dif, u64 id, u8 activo, u8 extendido, u32 s)*
            {
                WorldPackets::Instance::InstanceInfo p;
                uint32 n = r.get<uint32>();
                for (uint32 i = 0; i < n && r.p + 22 <= b.size(); ++i)
                {
                    WorldPackets::Instance::InstanceLock il;
                    il.MapID = r.get<uint32>(); il.DifficultyID = r.get<uint32>();
                    il.InstanceID = r.get<uint64>() & 0xFFFFFFFF;
                    il.Locked = r.get<uint8>() != 0; il.Extended = r.get<uint8>() != 0;
                    il.TimeRemaining = int32(r.get<uint32>());
                    il.DifficultyID = DificultadCal(il.MapID, il.DifficultyID);   // por el tipo de mapa, no por MapID >= 600 (Naxx 25, MC, Azjol-Nerub salían mal)
                    p.LockList.push_back(il);
                }
                Enviar(p.Write());
                return true;
            }
            case 0x1FB:                                      // MSG_RANDOM_ROLL: u32 mín, u32 máx, u32 resultado, u64 quien
            {
                WorldPackets::Misc::RandomRoll p;
                p.Min = int32(r.get<uint32>()); p.Max = int32(r.get<uint32>()); p.Result = int32(r.get<uint32>());
                p.Roller = mundo->ToTc(r.get<uint64>());
                p.RollerWowAccount = ObjectGuid::Create<HighGuid::WowAccount>(0);
                Enviar(p.Write());
                return true;
            }
            // sin equivalente útil o ya enviados por la pasarela a su manera
            // ---- logros (los IDs de logro y criterio coinciden con los del 3.4.3; los custom el cliente no los conoce y los ignora)
            case 0x47D:                                      // SMSG_ALL_ACHIEVEMENT_DATA
            {
                WorldPackets::Achievement::AllAchievementData p;
                LeerLogros(r, p.Data, Yo());
                Enviar(p.Write());
                return true;
            }
            case 0x46C:                                      // SMSG_RESPOND_INSPECT_ACHIEVEMENTS: packguid, bloque de logros
            {
                WorldPackets::Achievement::RespondInspectAchievements p;
                p.Player = mundo->ToTc(Pack(r));
                LeerLogros(r, p.Data, p.Player);
                Enviar(p.Write());
                return true;
            }
            case 0x46A:                                      // SMSG_CRITERIA_UPDATE: u32, packguid cant, packguid, u32, u32 fecha, u32, u32
            {
                WorldPackets::Achievement::CriteriaUpdate p;
                p.CriteriaID = r.get<uint32>(); p.Quantity = Pack(r); p.PlayerGUID = mundo->ToTc(Pack(r));
                p.Flags = r.get<uint32>(); p.CurrentTime.SetPackedTime(r.get<uint32>());
                p.ElapsedTime = Seconds(r.get<uint32>());
                p.CreationTime = time(nullptr) - time_t(r.get<uint32>());
                Enviar(p.Write());
                return true;
            }
            case 0x468:                                      // SMSG_ACHIEVEMENT_EARNED: packguid, u32 logro, u32 fecha, u32
            {
                WorldPackets::Achievement::AchievementEarned p;
                p.Earner = p.Sender = mundo->ToTc(Pack(r));
                p.EarnerNativeRealm = p.EarnerVirtualRealm = realmAddress;
                p.AchievementID = r.get<uint32>(); p.Time.SetPackedTime(r.get<uint32>());
                Enviar(p.Write());
                return true;
            }

            // ---- conjuntos de equipo
            case 0x4BC:                                      // SMSG_EQUIPMENT_SET_LIST: u32 n, (packguid, u32 índice, cstr, cstr, 19 x packguid)*
            {
                uint32 n = r.get<uint32>();
                std::vector<EquipmentSetInfo::EquipmentSetData> sets;
                for (uint32 i = 0; i < n && r.p < b.size(); ++i)
                {
                    EquipmentSetInfo::EquipmentSetData d;
                    d.Guid = Pack(r); d.SetID = r.get<uint32>(); d.SetName = r.cstr(); d.SetIcon = r.cstr();
                    for (uint32 s = 0; s < EQUIPMENT_SET_SLOTS; ++s)
                    {
                        uint64 g = Pack(r);
                        if (g == 1) d.IgnoreMask |= 1u << s;
                        else if (g) d.Pieces[s] = mundo->ToTc(g);
                    }
                    { std::lock_guard<std::mutex> l(_mConj); _conjIgnorar[d.Guid] = d.IgnoreMask; }
                    sets.push_back(d);
                }
                WorldPackets::EquipmentSet::LoadEquipmentSet p;
                for (auto const& d : sets) p.SetData.push_back(&d);
                Enviar(p.Write());
                return true;
            }
            case 0x137:                                      // SMSG_EQUIPMENT_SET_SAVED: u32 índice, packguid
            {
                WorldPackets::EquipmentSet::EquipmentSetID p;
                p.SetID = r.get<uint32>(); p.GUID = Pack(r);
                Enviar(p.Write());
                return true;
            }
            case 0x4D6:                                      // SMSG_EQUIPMENT_SET_USE_RESULT: u8
            {
                WorldPackets::EquipmentSet::UseEquipmentSetResult p;
                { std::lock_guard<std::mutex> l(_mConj); p.GUID = _conjUsado; }
                p.Reason = r.get<uint8>();
                Enviar(p.Write());
                return true;
            }
            // ---- banco
            case 0x1BA:                                      // SMSG_BUY_BANK_SLOT_RESULT: u32 (3 = bien)
            {
                uint32 res = r.get<uint32>();
                if (res == 0) Sistema("No puedes comprar más huecos de banco.");
                else if (res == 1) Sistema("No tienes dinero suficiente.");
                return true;
            }
            // ---- marcas de misión en el mapa, texto de objetos, /who
            case 0x1E4: QuestPOI335(r); return true;
            case 0x244:                                      // SMSG_ITEM_TEXT_QUERY_RESPONSE: u8 0, u64, cstr | u8 1
            {
                WorldPackets::Query::QueryItemTextResponse p;
                p.Valid = r.get<uint8>() == 0;
                if (p.Valid) { p.Id = mundo->ToTc(r.get<uint64>()); p.Item.Text = r.cstr(); }
                Enviar(p.Write());
                return true;
            }
            case 0x063: Who335(r); return true;             // SMSG_WHO
            // ---- comprobación de listos
            case 0x322:                                      // MSG_RAID_READY_CHECK: u64 quien la inicia
            {
                WorldPackets::Party::ReadyCheckStarted p;
                p.PartyGUID = _grupoTc; p.InitiatorGUID = mundo->ToTc(r.get<uint64>());
                p.Duration = Milliseconds(35000);
                Enviar(p.Write());
                return true;
            }
            case 0x3AE:                                      // MSG_RAID_READY_CHECK_CONFIRM: u64, u8
            {
                WorldPackets::Party::ReadyCheckResponse p;
                p.PartyGUID = _grupoTc; p.Player = mundo->ToTc(r.get<uint64>()); p.IsReady = r.get<uint8>() != 0;
                Enviar(p.Write());
                return true;
            }
            case 0x3C6:                                      // MSG_RAID_READY_CHECK_FINISHED
            {
                WorldPackets::Party::ReadyCheckCompleted p; p.PartyGUID = _grupoTc;
                Enviar(p.Write());
                return true;
            }
            case 0x2E4:                                      // SMSG_AREA_SPIRIT_HEALER_TIME: u64, u32 ms
            {
                WorldPackets::Battleground::AreaSpiritHealerTime p;
                p.HealerGuid = mundo->ToTc(r.get<uint64>()); p.TimeLeft = int32(r.get<uint32>());
                Enviar(p.Write());
                return true;
            }
            case 0x2AA:                                      // MSG_TALENT_WIPE_CONFIRM: u64 PNJ, u32 coste
            {
                WorldPackets::Talent::RespecWipeConfirm p;
                p.RespecMaster = mundo->ToTc(r.get<uint64>()); p.Cost = r.get<uint32>(); p.RespecType = 0;
                Enviar(p.Write());
                return true;
            }
            // ---- comercio
            case 0x120:                                      // SMSG_TRADE_STATUS: u32 estado, extra según estado
            {
                WorldPackets::Trade::TradeStatus p;
                uint32 st = r.get<uint32>();
                p.Status = ::TradeStatus(st);
                switch (st)
                {
                    case 1:                                  // propuesto: u64 quien
                        p.Partner = mundo->ToTc(r.get<uint64>());
                        p.PartnerAccount = ObjectGuid::Create<HighGuid::WowAccount>(0);
                        break;
                    case 2: p.ID = r.get<uint32>(); _comercioEstado = 0; break;   // ventana abierta
                    case 12:                                 // fallo: u32 InventoryResult, u8 es del otro, u32 objeto
                        p.BagResult = int32(r.get<uint32>()); p.FailureForYou = r.get<uint8>() == 0; p.ItemID = r.get<uint32>();
                        break;
                    case 22: case 23: p.TradeSlot = r.get<uint8>(); break;
                    default: break;
                }
                Enviar(p.Write());
                return true;
            }
            case 0x121:                                      // SMSG_TRADE_STATUS_EXTENDED
            {
                WorldPackets::Trade::TradeUpdated p;
                p.WhichPlayer = r.get<uint8>(); p.ID = r.get<uint32>(); r.get<uint32>();
                uint32 huecos = r.get<uint32>();
                p.Gold = r.get<uint32>(); p.ProposedEnchantment = int32(r.get<uint32>());
                for (uint32 i = 0; i < huecos && r.p + 73 <= b.size(); ++i)
                {
                    uint8 slot = r.get<uint8>();
                    uint32 entry = r.get<uint32>(); r.get<uint32>();
                    uint32 cant = r.get<uint32>(); uint32 envuelto = r.get<uint32>();
                    uint64 regalo = r.get<uint64>();
                    uint32 perm = r.get<uint32>(); r.get<uint32>(); r.get<uint32>(); r.get<uint32>();
                    uint64 creador = r.get<uint64>();
                    uint32 cargas = r.get<uint32>(); r.get<uint32>(); int32 prop = int32(r.get<uint32>());
                    uint32 cerrojo = r.get<uint32>(), maxDur = r.get<uint32>(), dur = r.get<uint32>();
                    if (!entry) continue;
                    WorldPackets::Trade::TradeItem it;
                    it.Slot = slot; it.Item.ItemID = entry; it.Item.RandomPropertiesID = prop; it.StackCount = int32(cant);
                    it.GiftCreator = regalo ? mundo->ToTc(regalo) : ObjectGuid::Empty;
                    if (!envuelto)
                    {
                        WorldPackets::Trade::UnwrappedTradeItem u;
                        u.Item = it.Item; u.EnchantID = int32(perm); u.Creator = creador ? mundo->ToTc(creador) : ObjectGuid::Empty;
                        u.Charges = int32(cargas); u.Lock = cerrojo != 0; u.MaxDurability = maxDur; u.Durability = dur;
                        it.Unwrapped = u;
                    }
                    p.Items.push_back(it);
                }
                p.CurrentStateIndex = p.ClientStateIndex = ++_comercioEstado;
                Enviar(p.Write());
                return true;
            }
            case 0x3F4: Inspeccion335(r); return true;      // SMSG_INSPECT_TALENT
            // ---- subastas
            case 0x25B:                                      // SMSG_AUCTION_COMMAND_RESULT: u32 subasta, u32 acción, u32 error, [u32]
            {
                WorldPacket w(SMSG_AUCTION_COMMAND_RESULT, 48);
                uint32 id = r.get<uint32>(), accion = r.get<uint32>(), err = r.get<uint32>();
                uint32 extra = r.p + 4 <= b.size() ? r.get<uint32>() : 0;
                w << int32(id) << int32(accion) << int32(err) << int32(err == 1 ? extra : 0);
                w << ObjectGuid::Empty << uint64(0) << uint64(0) << uint32(0);
                Enviar(&w);
                return true;
            }
            case 0x25C: case 0x25D: case 0x265:              // listas: u32 n, subastas, u32 total, u32 espera
            {
                std::vector<SubastaTc> v;
                uint32 n = r.get<uint32>();
                for (uint32 i = 0; i < n && r.p + 148 <= b.size(); ++i) v.push_back(LeerSubasta(r));
                uint32 total = r.get<uint32>(); uint32 espera = r.get<uint32>();
                if (op == 0x25C)
                {
                    WorldPacket w(SMSG_AUCTION_LIST_ITEMS_RESULT, 16 + v.size() * 120);
                    w << uint32(v.size()) << uint32(total) << uint32(espera) << uint8(0);
                    for (auto const& a : v) EscribirSubasta(w, a);
                    Enviar(&w);
                }
                else
                {
                    WorldPacket w(op == 0x25D ? SMSG_AUCTION_LIST_OWNER_ITEMS_RESULT : SMSG_AUCTION_LIST_BIDDER_ITEMS_RESULT, 16 + v.size() * 120);
                    w << uint32(v.size()) << uint32(v.size()) << uint32(espera);
                    for (auto const& a : v) EscribirSubasta(w, a);
                    Enviar(&w);
                }
                return true;
            }
            case 0x25E:                                      // SMSG_AUCTION_BIDDER_NOTIFICATION: u32 tipo, u32 subasta, u64 pujador, u32 puja, u32 dif, u32 objeto, u32
            {
                uint32 tipo = r.get<uint32>(), id = r.get<uint32>(); uint64 puj = r.get<uint64>();
                uint32 puja = r.get<uint32>(), dif = r.get<uint32>(), obj = r.get<uint32>();
                WorldPacket w(tipo == 1 ? SMSG_AUCTION_WON_NOTIFICATION : SMSG_AUCTION_OUTBID_NOTIFICATION, 64);
                WorldPackets::Item::ItemInstance it; it.ItemID = obj;
                w << int32(tipo) << int32(id) << (puj ? mundo->ToTc(puj) : ObjectGuid::Empty) << it;
                if (tipo != 1) w << uint64(puja) << uint64(dif);
                Enviar(&w);
                return true;
            }
            case 0x25F:                                      // SMSG_AUCTION_OWNER_NOTIFICATION: u32 subasta, u32 puja, u32, u64, u32 objeto, u32, float
            {
                uint32 id = r.get<uint32>(), puja = r.get<uint32>(); r.get<uint32>(); r.get<uint64>();
                WorldPackets::Item::ItemInstance it; it.ItemID = r.get<uint32>();
                WorldPacket w(SMSG_AUCTION_CLOSED_NOTIFICATION, 48);
                w << int32(id) << uint64(puja) << it << float(3600.0f);
                w.WriteBit(true); w.FlushBits();
                Enviar(&w);
                return true;
            }
            case 0x28D: case 0x490: return true;             // SMSG_AUCTION_REMOVED_NOTIFICATION / pendientes (se contestan en local)
            // ---- instancias
            case 0x329: case 0x4EB:                          // MSG_SET_DUNGEON/RAID_DIFFICULTY: u32 dificultad, u32, u32 en grupo
            {
                uint32 d = r.get<uint32>();
                if (op == 0x329) { WorldPackets::Misc::DungeonDifficultySet p; p.DifficultyID = int32(d + 1); Enviar(p.Write()); }
                else
                {
                    // Legacy = 0: la casilla 1 es la de GetLegacyRaidDifficultyID, que el menú del retrato no lee
                    WorldPackets::Misc::RaidDifficultySet p; p.DifficultyID = int32(d + 3); p.Legacy = 0;
                    Enviar(p.Write());
                }
                return true;
            }
            case 0x31E:                                      // SMSG_INSTANCE_RESET: u32 mapa
            {
                WorldPackets::Instance::InstanceReset p; p.MapID = r.get<uint32>();
                Enviar(p.Write());
                return true;
            }
            case 0x31F:                                      // SMSG_INSTANCE_RESET_FAILED: u32 motivo, u32 mapa
            {
                WorldPackets::Instance::InstanceResetFailed p; p.ResetFailedReason = uint8(r.get<uint32>()); p.MapID = r.get<uint32>();
                Enviar(p.Write());
                return true;
            }
            case 0x320:                                      // SMSG_UPDATE_LAST_INSTANCE: u32 mapa
            {
                WorldPackets::Instance::UpdateLastInstance p; p.MapID = r.get<uint32>();
                Enviar(p.Write());
                return true;
            }
            case 0x32B:                                      // SMSG_UPDATE_INSTANCE_OWNERSHIP: u32
            {
                WorldPackets::Instance::UpdateInstanceOwnership p; p.IOwnInstance = int32(r.get<uint32>());
                Enviar(p.Write());
                return true;
            }
            case 0x2CB: { WorldPackets::Instance::InstanceSaveCreated p; Enviar(p.Write()); return true; }
            case 0x2FA:                                      // SMSG_RAID_INSTANCE_MESSAGE: u32 tipo, u32 mapa, u32 dificultad, u32 tiempo, [u8 bloqueada, u8 extendida]
            {
                WorldPackets::Instance::RaidInstanceMessage p;
                p.Type = uint8(r.get<uint32>()); p.MapID = r.get<uint32>();
                uint32 d = r.get<uint32>(); r.get<uint32>();
                p.DifficultyID = d + (p.MapID >= 0 && d < 4 && Mundo::EsBanda(p.MapID) ? 3 : 1);
                if (r.p + 2 <= b.size()) { p.Locked = r.get<uint8>() != 0; p.Extended = r.get<uint8>() != 0; }
                Enviar(p.Write());
                return true;
            }
            case 0x147:                                      // SMSG_INSTANCE_LOCK_WARNING_QUERY: u32 ms, u32 encuentros, u8 extendiendo
            {
                WorldPackets::Instance::PendingRaidLock p;
                p.TimeUntilLock = int32(r.get<uint32>()); p.CompletedMask = r.get<uint32>(); p.Extending = r.get<uint8>() != 0;
                Enviar(p.Write());
                return true;
            }
            // ---- marcas de banda
            case 0x321:                                      // MSG_RAID_TARGET_UPDATE: u8 0, u64 quien, u8 marca, u64 objetivo | u8 1, (u8 marca, u64)*
            {
                uint8 tipo = r.get<uint8>();
                if (tipo == 0)
                {
                    WorldPackets::Party::SendRaidTargetUpdateSingle p;
                    p.ChangedBy = mundo->ToTc(r.get<uint64>()); p.Symbol = int8(r.get<uint8>());
                    uint64 t = r.get<uint64>(); p.Target = t ? mundo->ToTc(t) : ObjectGuid::Empty;
                    Enviar(p.Write());
                }
                else
                {
                    WorldPackets::Party::SendRaidTargetUpdateAll p;
                    while (r.p + 9 <= b.size()) { uint8 m = r.get<uint8>(); uint64 t = r.get<uint64>(); if (t) p.TargetIcons[m] = mundo->ToTc(t); }
                    Enviar(p.Write());
                }
                return true;
            }
            // ---- cinemáticas, películas y sonidos de objeto
            case 0x0FA: { WorldPackets::Misc::TriggerCinematic p; p.CinematicID = r.get<uint32>(); Enviar(p.Write()); return true; }
            case 0x464: { WorldPackets::Misc::TriggerMovie p; p.MovieID = r.get<uint32>(); Enviar(p.Write()); return true; }
            case 0x278:                                      // SMSG_PLAY_OBJECT_SOUND: u32 sonido, u64 objeto
            {
                WorldPackets::Misc::PlayObjectSound p;
                p.SoundKitID = int32(r.get<uint32>());
                p.SourceObjectGUID = p.TargetObjectGUID = mundo->ToTc(r.get<uint64>());
                Position pos; if (mundo->PosicionGuid(mundo->A335(p.SourceObjectGUID), pos)) p.Position = pos;
                Enviar(p.Write());
                return true;
            }
            // ---- hermandad
            case 0x3FD:                                      // MSG_GUILD_PERMISSIONS: u32 rango, u32 derechos, u32 oro/día, u32 pestañas, 6 x (u32, u32)
            {
                WorldPackets::Guild::GuildPermissionsQueryResults p;
                p.RankID = r.get<uint32>(); p.Flags = int32(r.get<uint32>()); p.WithdrawGoldLimit = int32(r.get<uint32>());
                p.NumTabs = int32(r.get<uint32>());
                for (int i = 0; i < 6 && r.p + 8 <= b.size(); ++i)
                {
                    WorldPackets::Guild::GuildPermissionsQueryResults::GuildRankTabPermissions t;
                    t.Flags = int32(r.get<uint32>()); t.WithdrawItemLimit = int32(r.get<uint32>());
                    p.Tab.push_back(t);
                }
                Enviar(p.Write());
                return true;
            }
            case 0x3FF:                                      // MSG_GUILD_EVENT_LOG_QUERY: u8 n, (u8 tipo, u64, [u64], [u8 rango], u32 hace)*
            {
                WorldPackets::Guild::GuildEventLogQueryResults p;
                uint8 n = r.get<uint8>();
                for (uint8 i = 0; i < n && r.p < b.size(); ++i)
                {
                    WorldPackets::Guild::GuildEventEntry e;
                    e.TransactionType = r.get<uint8>();
                    e.PlayerGUID = mundo->ToTc(r.get<uint64>());
                    if (e.TransactionType != 2 && e.TransactionType != 6) e.OtherGUID = mundo->ToTc(r.get<uint64>());   // salvo unirse/salir
                    if (e.TransactionType == 3 || e.TransactionType == 4) e.RankID = r.get<uint8>();                    // ascenso/descenso
                    e.TransactionDate = r.get<uint32>();
                    p.Entry.push_back(e);
                }
                Enviar(p.Write());
                return true;
            }
            case 0x4A7:                                      // SMSG_PLAYER_VEHICLE_DATA: packguid, u32 vehículo
            {
                WorldPackets::Vehicle::SetVehicleRecID p;
                p.VehicleGUID = mundo->ToTc(Pack(r)); p.VehicleRecID = int32(r.get<uint32>());
                Enviar(p.Write());
                return true;
            }
            // ---- campos de batalla
            case 0x2D4: EstadoBg335(r); return true;        // SMSG_BATTLEFIELD_STATUS
            case 0x2E0: Marcador335(r); return true;        // MSG_PVP_LOG_DATA
            case 0x23D:                                      // SMSG_BATTLEFIELD_LIST: u64, u8 desde, u32 tipo, u8, u8, ...
            {
                WorldPackets::Battleground::BattlefieldList p;
                uint64 g = r.get<uint64>(); r.get<uint8>();
                p.BattlemasterGuid = g ? mundo->ToTc(g) : ObjectGuid::Empty;
                p.BattlemasterListID = int32(r.get<uint32>());
                p.MinLevel = 10; p.MaxLevel = 80; p.PvpAnywhere = g == 0;
                r.get<uint8>(); r.get<uint8>(); p.HasRandomWinToday = r.get<uint8>() != 0;
                // recompensas 3.3.3 (BattlegroundMgr::BuildBattlegroundListPacket): honor al ganar, arena al ganar, honor al perder;
                // en 3.4.3 van en SMSG_REQUEST_PVP_REWARDS_RESPONSE: se guardan y se reenvían si cambian
                if (r.p + 12 <= b.size())
                {
                    uint32 gh = r.get<uint32>(), ga = r.get<uint32>(), ph = r.get<uint32>();
                    bool cambia = gh != _bgGanaHonor || ga != _bgGanaArena || ph != _bgPierdeHonor;
                    _bgGanaHonor = gh; _bgGanaArena = ga; _bgPierdeHonor = ph;
                    if (cambia) RecompensasJcJ();
                }
                p.Battlefields = { 1, 1, 1, 1, 2, 2, 2, 3, 3, 3, 4, 4, 5 };   // Wrathion (capturas del 3.4.3): sin esto la ventana JcJ sale vacía
                Enviar(p.Write());
                return true;
            }
            case 0x2EC: case 0x2ED:                          // SMSG_BATTLEGROUND_PLAYER_JOINED / LEFT: u64
            {
                ObjectGuid g = mundo->ToTc(r.get<uint64>());
                if (op == 0x2EC) { WorldPackets::Battleground::BattlegroundPlayerJoined p; p.Guid = g; Enviar(p.Write()); }
                else { WorldPackets::Battleground::BattlegroundPlayerLeft p; p.Guid = g; Enviar(p.Write()); }
                return true;
            }
            case 0x2E9:                                      // MSG_BATTLEGROUND_PLAYER_POSITIONS: u32 n, (u64, x, y)*, u32 n, (u64, x, y)*
            {
                WorldPackets::Battleground::BattlegroundPlayerPositions p;
                for (int lista = 0; lista < 2 && r.p + 4 <= b.size(); ++lista)
                {
                    uint32 n = r.get<uint32>();
                    for (uint32 i = 0; i < n && r.p + 16 <= b.size(); ++i)
                    {
                        WorldPackets::Battleground::BattlegroundPlayerPosition pp;
                        pp.Guid = mundo->ToTc(r.get<uint64>());
                        float x = r.get<float>(), y = r.get<float>(); pp.Pos.Pos.Relocate(x, y);
                        pp.IconID = lista == 1 ? int8(i == 0 ? 1 : 2) : 0;   // portadores: bandera de la Alianza / de la Horda
                        p.FlagCarriers.push_back(pp);
                    }
                }
                Enviar(p.Write());
                return true;
            }
            case 0x2E8:                                      // SMSG_GROUP_JOINED_BATTLEGROUND: i32 resultado
            {
                int32 res = int32(r.get<uint32>());
                if (res >= 0) return true;                   // en 3.3.5 los >= 0 son el tipo de campo al que se unió
                ColaBg c; uint32 hueco = 0;
                { std::lock_guard<std::mutex> l(_mBg); if (!_colasBg.empty()) { hueco = _colasBg.begin()->first; c = _colasBg.begin()->second; } }
                WorldPackets::Battleground::BattlefieldStatusFailed p;
                p.Ticket = TicketBg(hueco, c.desde); p.QueueID = ColaId(c); p.Reason = res;
                Enviar(p.Write());
                return true;
            }
            case 0x28C:                                      // SMSG_PVP_CREDIT: u32 honor, u64 víctima, u32 rango
            {
                WorldPackets::Combat::PvPCredit p;
                p.Honor = p.OriginalHonor = int32(r.get<uint32>()); p.Target = mundo->ToTc(r.get<uint64>()); p.Rank = int32(r.get<uint32>());
                Enviar(p.Write());
                return true;
            }
            case 0x4A6: Sistema("No puedes hacer eso todavía."); return true;   // SMSG_BATTLEGROUND_INFO_THROTTLED
            // ---- buscador de mazmorras
            case 0x367: ActualizarLfg335(r, false); return true;   // SMSG_LFG_UPDATE_PLAYER
            case 0x368: ActualizarLfg335(r, true); return true;    // SMSG_LFG_UPDATE_PARTY
            case 0x36F:                                      // SMSG_LFG_PLAYER_INFO: u8 n, (u32, u8 hecha, u32 dinero, u32 xp, u32, u32, u8 n, (u32 obj, u32, u32 cant)*)*, bloqueos
            {
                WorldPackets::LFG::LfgPlayerInfo p;
                uint8 n = r.get<uint8>();
                for (uint8 i = 0; i < n && r.p + 22 <= b.size(); ++i)
                {
                    WorldPackets::LFG::LfgPlayerDungeonInfo d;
                    d.Slot = r.get<uint32>(); d.FirstReward = r.get<uint8>() != 0;
                    d.Rewards.RewardMoney = int32(r.get<uint32>()); d.Rewards.RewardXP = int32(r.get<uint32>()); r.get<uint32>(); r.get<uint32>();
                    uint8 ni = r.get<uint8>();
                    for (uint8 k = 0; k < ni && r.p + 12 <= b.size(); ++k) { int32 it = int32(r.get<uint32>()); r.get<uint32>(); d.Rewards.Item.emplace_back(it, int32(r.get<uint32>())); }
                    d.CompletionQuantity = 1; d.CompletionLimit = 1; d.SpecificLimit = 1; d.OverallLimit = 1; d.Quantity = 1;
                    if (!sLFGDungeonsStore.HasRecord(d.Slot & 0xFFFFFF)) { Log("[%s] mazmorra aleatoria %u no existe en el cliente 3.4.3: se quita", acct.c_str(), d.Slot & 0xFFFFFF); continue; }
                    p.Dungeon.push_back(d);
                }
                LeerBloqueos(r, p.BlackList.Slot);
                Enviar(p.Write());
                return true;
            }
            case 0x372:                                      // SMSG_LFG_PARTY_INFO: u8 n, (u64, bloqueos)*
            {
                WorldPackets::LFG::LfgPartyInfo p;
                uint8 n = r.get<uint8>();
                for (uint8 i = 0; i < n && r.p + 12 <= b.size(); ++i)
                {
                    WorldPackets::LFG::LFGBlackList bl; bl.PlayerGuid = mundo->ToTc(r.get<uint64>());
                    LeerBloqueos(r, bl.Slot);
                    p.Player.push_back(bl);
                }
                Enviar(p.Write());
                return true;
            }
            case 0x2BB:                                      // SMSG_LFG_ROLE_CHOSEN: u64, u8 listo, u32 roles
            {
                WorldPackets::LFG::RoleChosen p;
                p.Player = mundo->ToTc(r.get<uint64>()); p.Accepted = r.get<uint8>() != 0; p.RoleMask = uint8(r.get<uint32>());
                Enviar(p.Write());
                return true;
            }
            case 0x363:                                      // SMSG_LFG_ROLE_CHECK_UPDATE: u32 estado, u8 empieza, u8 n, u32*, u8 n, (u64, u8, u32 roles, u8 nivel)*
            {
                WorldPackets::LFG::LFGRoleCheckUpdate p;
                p.PartyIndex = 127;
                p.RoleCheckStatus = uint8(r.get<uint32>()); p.IsBeginning = r.get<uint8>() != 0;
                uint8 n = r.get<uint8>();
                for (uint8 i = 0; i < n && r.p + 4 <= b.size(); ++i) p.JoinSlots.push_back(r.get<uint32>());
                uint8 nm = r.get<uint8>();
                for (uint8 i = 0; i < nm && r.p + 14 <= b.size(); ++i)
                {
                    ObjectGuid g = mundo->ToTc(r.get<uint64>()); bool listo = r.get<uint8>() != 0; uint8 roles = uint8(r.get<uint32>()); uint8 nivel = r.get<uint8>();
                    p.Members.emplace_back(g, roles, nivel, listo);
                }
                Enviar(p.Write());
                return true;
            }
            case 0x364:                                      // SMSG_LFG_JOIN_RESULT: u32 resultado, u32 estado, [bloqueos del grupo]
            {
                WorldPackets::LFG::LFGJoinResult p;
                p.Ticket = TicketLfg();
                p.Result = ResultadoUnirseLfg(r.get<uint32>()); p.ResultDetail = uint8(r.get<uint32>());
                if (r.p < b.size())
                {
                    uint8 n = r.get<uint8>();
                    for (uint8 i = 0; i < n && r.p + 12 <= b.size(); ++i)
                    {
                        WorldPackets::LFG::LFGBlackList bl; bl.PlayerGuid = mundo->ToTc(r.get<uint64>());
                        LeerBloqueos(r, bl.Slot);
                        p.BlackList.push_back(bl);
                    }
                }
                Enviar(p.Write());
                return true;
            }
            case 0x365:                                      // SMSG_LFG_QUEUE_STATUS: u32 mazmorra, i32 media, i32 mía, 3 x i32 por rol, 3 x u8 faltan, u32 en cola
            {
                WorldPackets::LFG::LFGQueueStatus p;
                p.Ticket = TicketLfg();
                p.Slot = r.get<uint32>(); p.AvgWaitTime = r.get<uint32>(); p.AvgWaitTimeMe = r.get<uint32>();
                for (auto& t : p.AvgWaitTimeByRole) t = r.get<uint32>();
                for (auto& f : p.LastNeeded) f = r.get<uint8>();
                p.QueuedTime = r.get<uint32>();
                Enviar(p.Write());
                return true;
            }
            case 0x1FF:                                      // SMSG_LFG_PLAYER_REWARD: u32 aleatoria, u32 hecha, u8, u32 1, u32 dinero, u32 xp, u32, u32, u8 n, (u32 obj, u32, u32 cant)*
            {
                WorldPackets::LFG::LFGPlayerReward p;
                p.QueuedSlot = r.get<uint32>(); p.ActualSlot = r.get<uint32>(); r.get<uint8>(); r.get<uint32>();
                p.RewardMoney = int32(r.get<uint32>()); p.AddedXP = int32(r.get<uint32>()); r.get<uint32>(); r.get<uint32>();
                uint8 n = r.get<uint8>();
                for (uint8 i = 0; i < n && r.p + 12 <= b.size(); ++i) { int32 it = int32(r.get<uint32>()); r.get<uint32>(); p.Rewards.emplace_back(it, r.get<uint32>(), 0, false); }
                Enviar(p.Write());
                return true;
            }
            case 0x36D:                                      // SMSG_LFG_BOOT_PROPOSAL_UPDATE: u8, u8, u8, u64, u32 x4, cstr
            {
                WorldPackets::LFG::LfgBootPlayer p;
                auto& i = p.Info;
                i.VoteInProgress = r.get<uint8>() != 0; i.MyVoteCompleted = r.get<uint8>() != 0; i.MyVote = r.get<uint8>() != 0;
                i.Target = mundo->ToTc(r.get<uint64>());
                i.TotalVotes = r.get<uint32>(); i.BootVotes = r.get<uint32>(); i.TimeLeft = int32(r.get<uint32>()); i.VotesNeeded = r.get<uint32>();
                i.Reason = r.cstr();
                i.VotePassed = !i.VoteInProgress && i.BootVotes >= i.VotesNeeded;
                Enviar(p.Write());
                return true;
            }
            case 0x361:                                      // SMSG_LFG_PROPOSAL_UPDATE: u32 mazmorra, u8 estado, u32 id, u32 jefes, u8 silenciosa, u8 n, (u32 rol, u8 yo, u8, u8, u8 respondió, u8 aceptó)*
            {
                WorldPackets::LFG::LFGProposalUpdate p;
                p.Ticket = TicketLfg();
                p.Slot = r.get<uint32>(); p.State = int8(r.get<uint8>()); p.ProposalID = r.get<uint32>();
                p.CompletedMask = r.get<uint32>(); p.ValidCompletedMask = true; p.ProposalSilent = r.get<uint8>() != 0;
                uint8 n = r.get<uint8>();
                for (uint8 i = 0; i < n && r.p + 9 <= b.size(); ++i)
                {
                    WorldPackets::LFG::LFGProposalUpdatePlayer j;
                    j.Roles = uint8(r.get<uint32>()); j.Me = r.get<uint8>() != 0; j.MyParty = r.get<uint8>() != 0; j.SameParty = r.get<uint8>() != 0;
                    j.Responded = r.get<uint8>() != 0; j.Accepted = r.get<uint8>() != 0;
                    p.Players.push_back(j);
                }
                Enviar(p.Write());
                return true;
            }
            case 0x398: { WorldPackets::LFG::LFGDisabled p; Enviar(p.Write()); return true; }
            case 0x293: { WorldPackets::LFG::LFGOfferContinue p(r.get<uint32>()); Enviar(p.Write()); return true; }
            case 0x200: { WorldPackets::LFG::LFGTeleportDenied p(lfg::LfgTeleportResult(r.get<uint32>())); Enviar(p.Write()); return true; }
            case 0x369: return true;                         // SMSG_LFG_UPDATE_SEARCH (buscador de bandas)
            case 0x09B:                                      // SMSG_CHANNEL_LIST: u8 tipo, cstr canal, u8 banderas, u32 n, (u64, u8)*
            {
                WorldPackets::Channel::ChannelListResponse p;
                p._Display = r.get<uint8>() != 0; p._Channel = r.cstr(); p._ChannelFlags = r.get<uint8>();
                uint32 n = r.get<uint32>();
                for (uint32 i = 0; i < n && r.p + 9 <= b.size(); ++i) { ObjectGuid g = mundo->ToTc(r.get<uint64>()); p._Members.emplace_back(g, realmAddress, r.get<uint8>()); }
                Enviar(p.Write());
                return true;
            }
            // ---- cartas de hermandad
            case 0x1BC:                                      // SMSG_PETITION_SHOWLIST: u64, u8 n, (u32 índice, u32 objeto, u32 modelo, u32 precio, u32, u32 firmas)*
            {
                WorldPackets::Petition::ServerPetitionShowList p;
                p.Unit = mundo->ToTc(r.get<uint64>());
                if (r.get<uint8>()) { r.get<uint32>(); r.get<uint32>(); r.get<uint32>(); p.Price = r.get<uint32>(); }
                Enviar(p.Write());
                return true;
            }
            case 0x1BF:                                      // SMSG_PETITION_SHOW_SIGNATURES: u64 carta, u64 dueño, u32 id, u8 n, (u64, u32)*
            {
                WorldPackets::Petition::ServerPetitionShowSignatures p;
                p.Item = mundo->ToTc(r.get<uint64>()); p.Owner = mundo->ToTc(r.get<uint64>());
                p.OwnerAccountID = ObjectGuid::Create<HighGuid::WowAccount>(0);
                p.PetitionID = int32(r.get<uint32>());
                uint8 n = r.get<uint8>();
                for (uint8 i = 0; i < n && r.p + 12 <= b.size(); ++i)
                {
                    WorldPackets::Petition::ServerPetitionShowSignatures::PetitionSignature f;
                    f.Signer = mundo->ToTc(r.get<uint64>()); f.Choice = int32(r.get<uint32>());
                    p.Signatures.push_back(f);
                }
                Enviar(p.Write());
                return true;
            }
            case 0x1C7:                                      // SMSG_PETITION_QUERY_RESPONSE: u32 id, u64 dueño, cstr nombre, cstr, u32 firmas, u32 firmas, ...
            {
                WorldPackets::Petition::QueryPetitionResponse p;
                p.PetitionID = r.get<uint32>(); p.Allow = true;
                auto& i = p.Info;
                i.PetitionID = int32(p.PetitionID); i.Petitioner = mundo->ToTc(r.get<uint64>());
                i.Title = r.cstr(); i.BodyText = r.cstr();
                i.MinSignatures = int32(r.get<uint32>()); i.MaxSignatures = int32(r.get<uint32>());
                Enviar(p.Write());
                return true;
            }
            case 0x1C1:                                      // SMSG_PETITION_SIGN_RESULTS: u64 carta, u64 jugador, u32 error
            {
                WorldPackets::Petition::PetitionSignResults p;
                p.Item = mundo->ToTc(r.get<uint64>()); p.Player = mundo->ToTc(r.get<uint64>()); p.Error = int32(r.get<uint32>());
                Enviar(p.Write());
                return true;
            }
            case 0x1C5:                                      // SMSG_TURN_IN_PETITION_RESULTS: u32
            {
                WorldPackets::Petition::TurnInPetitionResult p; p.Result = int32(r.get<uint32>());
                Enviar(p.Write());
                return true;
            }
            case 0x38F:                                      // SMSG_OFFER_PETITION_ERROR: u64
            {
                WorldPackets::Petition::OfferPetitionError p; p.PlayerGUID = mundo->ToTc(r.get<uint64>());
                Enviar(p.Write());
                return true;
            }
            case 0x2C1:                                      // MSG_PETITION_RENAME: u64, cstr
            {
                WorldPackets::Petition::PetitionRenameGuildResponse p;
                p.PetitionGuid = mundo->ToTc(r.get<uint64>()); p.NewGuildName = r.cstr();
                Enviar(p.Write());
                return true;
            }
            case 0x1C2:                                      // MSG_PETITION_DECLINE: u64 quien rechaza
                Sistema(NombreDe(r.get<uint64>()) + " ha rechazado firmar tu carta.");
                return true;
            // ---- peluquería
            case 0x427: { WorldPackets::Misc::EnableBarberShop p; p.CustomizationScope = 0; Enviar(p.Write()); return true; }
            case 0x428:                                      // SMSG_BARBER_SHOP_RESULT: u32
            {
                WorldPackets::Character::BarberShopResult p(WorldPackets::Character::BarberShopResult::ResultEnum(r.get<uint32>()));
                Enviar(p.Write());
                return true;
            }
            // ---- banco de hermandad
            case 0x3E8: BancoHermandad335(r); return true;  // SMSG_GUILD_BANK_LIST
            case 0x3EE:                                      // MSG_GUILD_BANK_LOG_QUERY: u8 pestaña, u8 n, (i8 tipo, u64, ..., u32 hace)*
            {
                WorldPackets::Guild::GuildBankLogQueryResults p;
                p.Tab = r.get<uint8>();
                uint8 n = r.get<uint8>();
                for (uint8 i = 0; i < n && r.p + 9 <= b.size(); ++i)
                {
                    WorldPackets::Guild::GuildBankLogEntry e;
                    e.EntryType = r.get<int8>(); e.PlayerGUID = mundo->ToTc(r.get<uint64>());
                    if (e.EntryType == 1 || e.EntryType == 2) { e.ItemID = int32(r.get<uint32>()); e.Count = int32(r.get<uint32>()); }
                    else if (e.EntryType == 3 || e.EntryType == 7) { e.ItemID = int32(r.get<uint32>()); e.Count = int32(r.get<uint32>()); e.OtherTab = r.get<int8>(); }
                    else e.Money = r.get<uint32>();
                    e.TimeOffset = r.get<uint32>();
                    p.Entry.push_back(e);
                }
                Enviar(p.Write());
                return true;
            }
            case 0x40A:                                      // MSG_QUERY_GUILD_BANK_TEXT: u8, cstr
            {
                WorldPackets::Guild::GuildBankTextQueryResult p; p.Tab = r.get<uint8>(); p.Text = r.cstr();
                Enviar(p.Write());
                return true;
            }
            case 0x3FE:                                      // MSG_GUILD_BANK_MONEY_WITHDRAWN: i32
            {
                WorldPackets::Guild::GuildBankRemainingWithdrawMoney p; p.RemainingWithdrawMoney = int32(r.get<uint32>());
                Enviar(p.Write());
                return true;
            }
            // ---- reputación
            case 0x124:                                      // SMSG_SET_FACTION_STANDING: f32, u8 aviso, u32 n, (u32 índice, u32 reputación)*
            {
                WorldPackets::Reputation::SetFactionStanding p;
                p.BonusFromAchievementSystem = r.get<float>(); p.ShowVisual = r.get<uint8>() != 0;
                uint32 n = r.get<uint32>();
                for (uint32 i = 0; i < n && r.p + 8 <= b.size(); ++i) { int32 idx = int32(r.get<uint32>()); p.Faction.emplace_back(idx, int32(r.get<uint32>())); }
                Enviar(p.Write());
                return true;
            }
            case 0x123: { WorldPackets::Character::SetFactionVisible p(true); p.FactionIndex = r.get<uint32>(); Enviar(p.Write()); return true; }
            // ---- hechizos y combate
            case 0x1E2:                                      // SMSG_SPELL_DELAYED: packguid, u32 ms
            {
                WorldPackets::Spells::SpellDelayed p; p.Caster = mundo->ToTc(Pack(r)); p.ActualDelay = int32(r.get<uint32>());
                Enviar(p.Write());
                return true;
            }
            case 0x1F3:                                      // SMSG_PLAY_SPELL_VISUAL: u64, u32 kit
            {
                WorldPackets::Spells::PlaySpellVisualKit p; p.Unit = mundo->ToTc(r.get<uint64>()); p.KitRecID = int32(r.get<uint32>());
                Enviar(p.Write());
                return true;
            }
            case 0x263:                                      // SMSG_SPELLORDAMAGE_IMMUNE: u64 lanzador, u64 víctima, u32 hechizo, u8 periódico
            {
                WorldPackets::CombatLog::SpellOrDamageImmune p;
                p.CasterGUID = mundo->ToTc(r.get<uint64>()); p.VictimGUID = mundo->ToTc(r.get<uint64>()); p.SpellID = r.get<uint32>(); p.IsPeriodic = r.get<uint8>() != 0;
                Enviar(p.Write());
                return true;
            }
            case 0x413:                                      // SMSG_TOTEM_CREATED: u8 hueco, u64, u32 ms, u32 hechizo
            {
                WorldPackets::Totem::TotemCreated p;
                p.Slot = r.get<uint8>(); p.Totem = mundo->ToTc(r.get<uint64>()); p.Duration = Milliseconds(int32(r.get<uint32>())); p.SpellID = int32(r.get<uint32>());
                Enviar(p.Write());
                return true;
            }
            case 0x491:                                      // SMSG_MODIFY_COOLDOWN: u32 hechizo, u64, i32 ms
            {
                WorldPackets::Spells::ModifyCooldown p;
                p.SpellID = int32(r.get<uint32>()); uint64 g = r.get<uint64>(); p.IsPet = g != yo335; p.DeltaTime = int32(r.get<uint32>());
                Enviar(p.Write());
                return true;
            }
            case 0x3BF: { WorldPackets::Spells::ClearTarget p; p.Guid = mundo->ToTc(r.get<uint64>()); Enviar(p.Write()); return true; }
            case 0x3AC:                                      // SMSG_DISMOUNT: packguid
            {
                WorldPacket w(SMSG_DISMOUNT, 18); w << mundo->ToTc(Pack(r));
                Enviar(&w);
                return true;
            }
            case 0x172:                                      // SMSG_MOUNTSPECIAL_ANIM: u64
            {
                WorldPackets::Misc::SpecialMountAnim p; p.UnitGUID = mundo->ToTc(r.get<uint64>());
                Enviar(p.Write());
                return true;
            }
            case 0x2B4: { WorldPacket w(SMSG_FEIGN_DEATH_RESISTED, 0); Enviar(&w); return true; }
            case 0x173: { WorldPackets::Pet::PetTameFailure p; p.Result = r.get<uint8>(); Enviar(p.Write()); return true; }
            case 0x1C8: { WorldPackets::GameObject::FishNotHooked p; Enviar(p.Write()); return true; }
            case 0x1C9: { WorldPackets::GameObject::FishEscaped p; Enviar(p.Write()); return true; }
            case 0x494: { WorldPackets::Misc::PreRessurect p; p.PlayerGUID = mundo->ToTc(Pack(r)); Enviar(p.Write()); return true; }
            case 0x49D: { WorldPackets::Vehicle::OnCancelExpectedRideVehicleAura p; Enviar(p.Write()); return true; }
            case 0x50B: { WorldPackets::Item::SocketGemsSuccess p; p.Item = mundo->ToTc(r.get<uint64>()); Enviar(p.Write()); return true; }
            // ---- mundo
            case 0x254: { WorldPackets::Misc::ZoneUnderAttack p; p.AreaID = int32(r.get<uint32>()); Enviar(p.Write()); return true; }
            case 0x277: { WorldPackets::Misc::PlayMusic p(r.get<uint32>()); Enviar(p.Write()); return true; }
            case 0x373:                                      // SMSG_TITLE_EARNED: u32 título, u32 ganado
            {
                uint32 t = r.get<uint32>(); bool ganado = r.get<uint32>() != 0;
                WorldPackets::Character::TitleEarned p(ganado ? SMSG_TITLE_EARNED : SMSG_TITLE_LOST); p.Index = t;
                Enviar(p.Write());
                return true;
            }
            case 0x158:                                      // SMSG_PLAYERBOUND: u64 posadero, u32 zona
            {
                WorldPackets::Misc::PlayerBound p(mundo->ToTc(r.get<uint64>()), 0); p.AreaID = r.get<uint32>();
                Enviar(p.Write());
                return true;
            }
            case 0x040:                                      // SMSG_TRANSFER_ABORTED: u32 mapa, u8 motivo, [u8]
            {
                WorldPackets::Movement::TransferAborted p;
                p.MapID = r.get<uint32>(); p.TransfertAbort = r.get<uint8>(); if (r.p < b.size()) p.Arg = r.get<uint8>();
                Enviar(p.Write());
                return true;
            }
            case 0x286:                                      // SMSG_RAID_GROUP_ONLY: u32 tiempo, u32 motivo
            {
                WorldPackets::Instance::RaidGroupOnly p; p.Delay = int32(r.get<uint32>()); p.Reason = r.get<uint32>();
                Enviar(p.Write());
                return true;
            }
            case 0x214:                                      // SMSG_UPDATE_INSTANCE_ENCOUNTER_UNIT: u32 tipo, packguid, [u8 prioridad]
            {
                uint32 tipo = r.get<uint32>();
                if (tipo > 2) return true;
                ObjectGuid g = mundo->ToTc(Pack(r));
                uint8 pr = r.p < b.size() ? r.get<uint8>() : 0;
                if (tipo == 0) { WorldPackets::Instance::InstanceEncounterEngageUnit p; p.Unit = g; p.TargetFramePriority = pr; Enviar(p.Write()); }
                else if (tipo == 1) { WorldPackets::Instance::InstanceEncounterDisengageUnit p; p.Unit = g; Enviar(p.Write()); }
                else { WorldPackets::Instance::InstanceEncounterChangePriority p; p.Unit = g; p.TargetFramePriority = pr; Enviar(p.Write()); }
                return true;
            }
            case 0x49F: { WorldPackets::Achievement::AchievementDeleted p; p.AchievementID = r.get<uint32>(); Enviar(p.Write()); return true; }
            case 0x49E: { WorldPackets::Achievement::CriteriaDeleted p; p.CriteriaID = r.get<uint32>(); Enviar(p.Write()); return true; }
            case 0x2FD: { WorldPackets::Chat::ChatRestricted p; p.Reason = r.get<uint8>(); Enviar(p.Write()); return true; }
            case 0x32D: { WorldPackets::Chat::ChatPlayerAmbiguous p(r.cstr()); Enviar(p.Write()); return true; }
            // ---- runas del caballero de la muerte
            case 0x487:                                      // SMSG_RESYNC_RUNES: u32 n, (u8 tipo, u8 enfriamiento pasado 0-255)*
            {
                uint32 n = r.get<uint32>();
                WorldPackets::Spells::ResyncRunes p(n);
                p.Runes.Start = uint8((1u << std::min<uint32>(n, 8)) - 1);
                for (uint32 i = 0; i < n && r.p + 2 <= b.size(); ++i)
                {
                    r.get<uint8>(); uint8 pasado = r.get<uint8>();
                    if (pasado == 255) p.Runes.Count |= uint8(1u << i);
                    p.Runes.Cooldowns.push_back(pasado);
                }
                Enviar(p.Write());
                return true;
            }
            case 0x486:                                      // SMSG_CONVERT_RUNE: u8 índice, u8 tipo nuevo (formato de Wrathion)
            {
                uint8 idx = r.get<uint8>(), tipo = r.get<uint8>();
                WorldPacket w(SMSG_CONVERT_RUNE, 14);
                w << uint8(idx) << uint8(1) << uint32(0) << uint32(idx) << uint32(tipo);
                Enviar(&w);
                return true;
            }
            case 0x488: { WorldPackets::Spells::AddRunePower p; p.AddedRunesMask = r.get<uint32>(); Enviar(p.Write()); return true; }
            // ---- registro de combate: espinas y disipados
            case 0x24F:                                      // SMSG_SPELLDAMAGESHIELD: u64 dañado, u64 dueño del escudo, u32 hechizo, u32 daño, u32 exceso, u32 escuela
            {
                WorldPackets::CombatLog::SpellDamageShield p;
                p.Defender = mundo->ToTc(r.get<uint64>()); p.Attacker = mundo->ToTc(r.get<uint64>());
                p.SpellID = int32(r.get<uint32>()); p.TotalDamage = p.OriginalDamage = int32(r.get<uint32>());
                p.OverKill = int32(r.get<uint32>()); p.SchoolMask = int32(r.get<uint32>());
                Enviar(p.Write());
                return true;
            }
            case 0x27B:                                      // SMSG_SPELLDISPELLOG: packguid víctima, packguid lanzador, u32 hechizo, u8, u32 n, (u32, u8)*
            {
                WorldPackets::CombatLog::SpellDispellLog p;
                p.TargetGUID = mundo->ToTc(Pack(r)); p.CasterGUID = mundo->ToTc(Pack(r));
                p.DispelledBySpellID = int32(r.get<uint32>()); r.get<uint8>();
                uint32 n = r.get<uint32>();
                for (uint32 i = 0; i < n && r.p + 5 <= b.size(); ++i)
                {
                    WorldPackets::CombatLog::SpellDispellData d; d.SpellID = int32(r.get<uint32>()); d.Harmful = r.get<uint8>() == 0;
                    p.DispellData.push_back(d);
                }
                Enviar(p.Write());
                return true;
            }
            case 0x3F9:                                      // SMSG_LOOT_LIST (marca de botín en la placa)
            case 0x47C:                                      // SMSG_SET_PHASE_SHIFT: AzerothCore ya filtra por fase lo que manda
            case 0x042: case 0x2EF: case 0x4AB: case 0x41E: case 0x455: case 0x344:
                return true;
            case 0x33B:                                      // SMSG_INSTANCE_DIFFICULTY: u32 dificultad, u32 dinámica
            {
                // TC lo manda como SMSG_WORLD_SERVER_INFO en cada entrada a un mapa (Player::SendInitialPacketsBeforeAddToMap):
                // dificultad del mapa y tamaño del grupo. Sin él el indicador del minimapa sale a 0 y GetInstanceInfo() no la sabe
                uint32 d = r.get<uint32>();
                WorldPackets::Misc::WorldServerInfo wsi;
                MapEntry const* me = sMapStore.LookupEntry(mundo->mapa);
                if (me && me->IsDungeon())
                {
                    wsi.DifficultyID = DificultadCal(mundo->mapa, d);
                    if (MapDifficultyEntry const* md = sDB2Manager.GetMapDifficultyData(mundo->mapa, Difficulty(wsi.DifficultyID)))
                        wsi.InstanceGroupSize = md->MaxPlayers;
                }
                Enviar(wsi.Write());
                return true;
            }
            default:
                return false;
        }
    }

    // ------------------------------------------------------------------------------------------ 3.4.3 -> 3.3.5
    bool Cliente(uint16 op, std::vector<uint8> const& b)
    {
        switch (op)
        {
            case CMSG_QUEST_GIVER_STATUS_QUERY:              // -> 0x182 (u64 guid)
            {
                WorldPackets::Quest::QuestGiverStatusQuery q(Paquete(CMSG_QUEST_GIVER_STATUS_QUERY, b)); q.Read();
                Wr w; w.put<uint64>(mundo->A335(q.QuestGiverGUID));
                Srv(0x182, w.b);
                return true;
            }
            // chat -> CMSG_MESSAGECHAT 0x095: u32 tipo, u32 idioma, [cstr destino], cstr texto
            case CMSG_CHAT_MESSAGE_SAY: case CMSG_CHAT_MESSAGE_YELL: case CMSG_CHAT_MESSAGE_PARTY: case CMSG_CHAT_MESSAGE_RAID:
            case CMSG_CHAT_MESSAGE_GUILD: case CMSG_CHAT_MESSAGE_OFFICER: case CMSG_CHAT_MESSAGE_RAID_WARNING: case CMSG_CHAT_MESSAGE_INSTANCE_CHAT:
            {
                WorldPackets::Chat::ChatMessage q(Paquete(OpcodeClient(op), b)); q.Read();
                if (op == CMSG_CHAT_MESSAGE_SAY && q.Text.rfind(".ayuda", 0) == 0 && (q.Text.size() == 6 || q.Text[6] == ' '))
                {
                    std::string arg = q.Text.size() > 7 ? q.Text.substr(7) : "";
                    bool hay; { std::lock_guard<std::mutex> l(_mTicket); hay = _gmTicket.hay && !_gmTicket.completado; }
                    if (arg.empty()) Sistema("Uso: .ayuda <texto> (abrir o cambiar ticket) | .ayuda ver | .ayuda borrar");
                    else if (arg == "ver") { _verTicket = true; Srv(0x211, {}); }
                    else if (arg == "borrar")
                    {
                        bool comp; { std::lock_guard<std::mutex> l(_mTicket); comp = _gmTicket.completado; }
                        Srv(comp ? 0x4F0 : 0x217, {});
                    }
                    else if (hay) { Wr w; w.cstr(arg.substr(0, 1000)); Srv(0x207, w.b); }
                    else
                    {
                        float x = 0, y = 0, z = 0; uint32 mapa = mundo ? mundo->mapa : 0;
                        if (mundo && mundo->yo) { x = mundo->yo->GetPositionX(); y = mundo->yo->GetPositionY(); z = mundo->yo->GetPositionZ(); }
                        // posición: la de la última actualización propia que tenga Mundo (o 0,0,0; AC solo la usa para .ticket viewid/.go ticket)
                        Wr w; w.put<uint32>(mapa).put<float>(x).put<float>(y).put<float>(z).cstr(arg.substr(0, 1000))
                              .put<uint32>(1).put<uint8>(0).put<uint32>(0).put<uint32>(0);
                        Srv(0x205, w.b);
                    }
                    return true;
                }
                uint32 tipo = op == CMSG_CHAT_MESSAGE_SAY ? 1 : op == CMSG_CHAT_MESSAGE_YELL ? 6 : op == CMSG_CHAT_MESSAGE_PARTY ? 2
                            : op == CMSG_CHAT_MESSAGE_RAID ? 3 : op == CMSG_CHAT_MESSAGE_GUILD ? 4 : op == CMSG_CHAT_MESSAGE_OFFICER ? 5
                            : op == CMSG_CHAT_MESSAGE_RAID_WARNING ? 0x28 : 0x2C;
                Wr w; w.put<uint32>(tipo).put<uint32>(IdiomaChat(int32(q.Language))).cstr(q.Text);
                Srv(0x095, w.b);
                return true;
            }
            case CMSG_CHAT_MESSAGE_WHISPER:
            {
                WorldPackets::Chat::ChatMessageWhisper q(Paquete(CMSG_CHAT_MESSAGE_WHISPER, b)); q.Read();
                std::string dest = q.Target.substr(0, q.Target.find('-'));   // "Nombre-Reino" -> "Nombre"
                Wr w; w.put<uint32>(7).put<uint32>(IdiomaChat(int32(q.Language))).cstr(dest).cstr(q.Text);
                Srv(0x095, w.b);
                return true;
            }
            case CMSG_CHAT_MESSAGE_CHANNEL:
            {
                WorldPackets::Chat::ChatMessageChannel q(Paquete(CMSG_CHAT_MESSAGE_CHANNEL, b)); q.Read();
                Wr w; w.put<uint32>(0x11).put<uint32>(IdiomaChat(int32(q.Language))).cstr(q.Target).cstr(q.Text);
                Srv(0x095, w.b);
                return true;
            }
            case CMSG_CHAT_MESSAGE_EMOTE:
            {
                WorldPackets::Chat::ChatMessageEmote q(Paquete(CMSG_CHAT_MESSAGE_EMOTE, b)); q.Read();
                Wr w; w.put<uint32>(0x0A).put<uint32>(0).cstr(q.Text);
                Srv(0x095, w.b);
                return true;
            }
            case CMSG_CHAT_MESSAGE_AFK: case CMSG_CHAT_MESSAGE_DND:
            {
                WorldPackets::Chat::ChatMessageAFK q(Paquete(OpcodeClient(op), b)); q.Read();
                Wr w; w.put<uint32>(op == CMSG_CHAT_MESSAGE_AFK ? 0x17 : 0x18).put<uint32>(0).cstr(q.Text);
                Srv(0x095, w.b);
                return true;
            }
            case CMSG_SEND_TEXT_EMOTE:                       // -> 0x104: u32 emote, u32 num, u64 objetivo
            {
                WorldPackets::Chat::CTextEmote q(Paquete(CMSG_SEND_TEXT_EMOTE, b)); q.Read();
                Wr w; w.put<uint32>(uint32(q.EmoteID)).put<uint32>(uint32(q.SoundIndex)).put<uint64>(mundo->A335(q.Target));
                Srv(0x104, w.b);
                return true;
            }
            case CMSG_CHAT_JOIN_CHANNEL:                     // -> 0x097: u32 id, u8, u8, cstr nombre, cstr clave
            {
                WorldPackets::Channel::JoinChannel q(Paquete(CMSG_CHAT_JOIN_CHANNEL, b)); q.Read();
                Wr w; w.put<uint32>(uint32(q.ChatChannelId)).put<uint8>(0).put<uint8>(0).cstr(q.ChannelName).cstr(q.Password);
                Srv(0x097, w.b);
                return true;
            }
            case CMSG_CHAT_LEAVE_CHANNEL:                    // -> 0x098: u32, cstr nombre
            {
                WorldPackets::Channel::LeaveChannel q(Paquete(CMSG_CHAT_LEAVE_CHANNEL, b)); q.Read();
                Wr w; w.put<uint32>(uint32(q.ZoneChannelID)).cstr(q.ChannelName);
                Srv(0x098, w.b);
                return true;
            }
            // addon -> CMSG_MESSAGECHAT 0x095 (AddonA335)
            case CMSG_CHAT_ADDON_MESSAGE: case CMSG_CHAT_ADDON_MESSAGE_TARGETED:
            {
                WorldPackets::Chat::ChatAddonMessageParams pa; std::string destino;
                if (op == CMSG_CHAT_ADDON_MESSAGE)
                { WorldPackets::Chat::ChatAddonMessage q(Paquete(CMSG_CHAT_ADDON_MESSAGE, b)); q.Read(); pa = q.Params; }
                else
                { WorldPackets::Chat::ChatAddonMessageTargeted q(Paquete(CMSG_CHAT_ADDON_MESSAGE_TARGETED, b)); q.Read(); pa = q.Params; destino = q.Target; }
                AddonA335(pa, destino);
                return true;
            }
            // susurro de addon (AIO manda así su "Init"): en 3.4.3 es un opcode propio que TrinityCore no implementa.
            // Formato como ChatAddonMessageTargeted: u9 largo del destino, parámetros, [guid de canal], destino. Los bytes que
            // sobren entre los parámetros y el destino se saltan (así vale con o sin guid)
            case CMSG_CHAT_ADDON_MESSAGE_WHISPER:
            {
                WorldPacket p = Paquete(OpcodeClient(op), b);
                WorldPackets::Chat::ChatAddonMessageParams pa; std::string destino;
                try
                {
                    uint32 largoDestino = p.ReadBits(9);
                    p.ResetBitPos();
                    uint32 largoPrefijo = p.ReadBits(5), largoTexto = p.ReadBits(8);
                    pa.IsLogged = p.ReadBit();
                    pa.Type = ChatMsg(p.read<int32>());
                    pa.Prefix = p.ReadString(largoPrefijo);
                    pa.Text = p.ReadString(largoTexto, false);
                    if (p.size() - p.rpos() > largoDestino) p.read_skip(p.size() - p.rpos() - largoDestino);
                    destino = p.ReadString(largoDestino);
                }
                catch (ByteBufferException const&)
                {
                    Log("[%s] CMSG_CHAT_ADDON_MESSAGE_WHISPER ilegible (%u bytes)", acct.c_str(), uint32(b.size()));
                    return true;
                }
                if (!_susurroAddonVisto)
                {
                    _susurroAddonVisto = true;
                    Log("[%s] susurro de addon: prefijo '%s', tipo %d, %u bytes de texto, destino '%s'", acct.c_str(),
                        pa.Prefix.c_str(), int32(pa.Type), uint32(pa.Text.size()), destino.c_str());
                }
                pa.Type = CHAT_MSG_WHISPER;
                AddonA335(pa, destino);
                return true;
            }
            case CMSG_CHAT_REGISTER_ADDON_PREFIXES: case CMSG_CHAT_UNREGISTER_ALL_ADDON_PREFIXES:
                return true;                                        // AC 3.3.5 no filtra por prefijo: nada que hacer
            case CMSG_SET_SELECTION:                         // -> 0x13D u64
            {
                WorldPackets::Misc::SetSelection q(Paquete(CMSG_SET_SELECTION, b)); q.Read();
                Wr w; w.put<uint64>(mundo->A335(q.Selection)); Srv(0x13D, w.b);
                return true;
            }
            case CMSG_ATTACK_SWING:                          // -> 0x141 u64
            {
                WorldPackets::Combat::AttackSwing q(Paquete(CMSG_ATTACK_SWING, b)); q.Read();
                Wr w; w.put<uint64>(mundo->A335(q.Victim)); Srv(0x141, w.b);
                return true;
            }
            case CMSG_ATTACK_STOP: Srv(0x142, {}); return true;
            case CMSG_CANCEL_AUTO_REPEAT_SPELL: Srv(0x26D, {}); return true;
            case CMSG_CAST_SPELL:                            // -> 0x12E: u8 cuenta, u32 hechizo, u8 banderas, objetivos
            {
                WorldPackets::Spells::CastSpell q(Paquete(CMSG_CAST_SPELL, b)); q.Read();
                uint8 cc = NuevoCast(q.Cast.CastID, uint32(q.Cast.SpellID));
                Wr w; w.put<uint8>(cc).put<uint32>(uint32(q.Cast.SpellID)).put<uint8>(BanderasTrayecto(q.Cast));
                EscribirObjetivos(w, q.Cast.Target);
                EscribirTrayecto(w, q.Cast);
                Srv(0x12E, w.b);
                return true;
            }
            case CMSG_CANCEL_CAST:                           // -> 0x12F: u8 cuenta, u32 hechizo
            {
                WorldPackets::Spells::CancelCast q(Paquete(CMSG_CANCEL_CAST, b)); q.Read();
                Wr w; w.put<uint8>(CuentaDe(q.CastID)).put<uint32>(q.SpellID); Srv(0x12F, w.b);
                return true;
            }
            case CMSG_CANCEL_AURA:                           // -> 0x136 u32
            {
                WorldPackets::Spells::CancelAura q(Paquete(CMSG_CANCEL_AURA, b)); q.Read();
                Wr w; w.put<uint32>(uint32(q.SpellID)); Srv(0x136, w.b);
                for (uint32 v : VariantesDe(uint32(q.SpellID)))   // Invencible y demás monturas que escalan
                {
                    Wr wv; wv.put<uint32>(v); Srv(0x136, wv.b);
                }
                return true;
            }
            case CMSG_CANCEL_CHANNELLING:                    // -> 0x13B u32
            {
                WorldPackets::Spells::CancelChannelling q(Paquete(CMSG_CANCEL_CHANNELLING, b)); q.Read();
                Wr w; w.put<uint32>(uint32(q.ChannelSpell)); Srv(0x13B, w.b);
                return true;
            }
            case CMSG_SET_ACTION_BUTTON:                     // -> 0x128: u8 botón, u32 acción|tipo<<24
            {
                WorldPackets::Spells::SetActionButton q(Paquete(CMSG_SET_ACTION_BUTTON, b)); q.Read();
                if (q.Index < _botones.size()) _botones[q.Index] = uint64(uint32(q.Action));   // copia para reenviar tras un cambio de mapa
                Wr w; w.put<uint8>(q.Index).put<uint32>(uint32(q.Action)); Srv(0x128, w.b);
                return true;
            }
            case CMSG_STAND_STATE_CHANGE:                    // -> 0x101 u32
            {
                WorldPackets::Misc::StandStateChange q(Paquete(CMSG_STAND_STATE_CHANGE, b)); q.Read();
                Wr w; w.put<uint32>(uint32(q.StandState)); Srv(0x101, w.b);
                return true;
            }
            case CMSG_SET_SHEATHED:                          // -> 0x1E0 u32
            {
                WorldPackets::Combat::SetSheathed q(Paquete(CMSG_SET_SHEATHED, b)); q.Read();
                Wr w; w.put<uint32>(uint32(q.CurrentSheathState)); Srv(0x1E0, w.b);
                return true;
            }
            case CMSG_REPOP_REQUEST:                         // -> 0x15A u8
            {
                WorldPackets::Misc::RepopRequest q(Paquete(CMSG_REPOP_REQUEST, b)); q.Read();
                Wr w; w.put<uint8>(q.CheckInstance ? 1 : 0); Srv(0x15A, w.b);
                return true;
            }
            case CMSG_RECLAIM_CORPSE:                        // -> 0x1D2 u64
            {
                WorldPackets::Misc::ReclaimCorpse q(Paquete(CMSG_RECLAIM_CORPSE, b)); q.Read();
                Wr w; w.put<uint64>(mundo->A335(q.CorpseGUID)); Srv(0x1D2, w.b);
                return true;
            }
            case CMSG_QUERY_PAGE_TEXT:                       // -> 0x05A: u32 página, u64
            {
                WorldPackets::Query::QueryPageText q(Paquete(CMSG_QUERY_PAGE_TEXT, b)); q.Read();
                Wr w; w.put<uint32>(q.PageTextID).put<uint64>(mundo->A335(q.ItemGUID)); Srv(0x05A, w.b);
                return true;
            }

            // ---- hermandades
            case CMSG_QUERY_GUILD_INFO:                      // -> 0x054 u32 id de hermandad
            {
                WorldPackets::Guild::QueryGuildInfo q(Paquete(CMSG_QUERY_GUILD_INFO, b)); q.Read();
                Wr w; w.put<uint32>(uint32(q.GuildGuid.GetCounter())); Srv(0x054, w.b);
                return true;
            }
            case CMSG_GUILD_GET_ROSTER: Srv(0x089, {}); return true;
            case CMSG_GUILD_GET_RANKS: EnviarRangos(); return true;
            case CMSG_GUILD_INVITE_BY_NAME:                  // -> 0x082 cstr
            {
                WorldPackets::Guild::GuildInviteByName q(Paquete(CMSG_GUILD_INVITE_BY_NAME, b)); q.Read();
                std::string nombre = q.Name.substr(0, q.Name.find('-'));
                if (q.Unused910)                             // ArenaTeamInviteByName usa este mismo paquete con el equipo detrás (RE 0x7672a0)
                {
                    Wr w; w.put<uint32>(EquipoArenaDe(uint32(*q.Unused910))).cstr(nombre); Srv(0x34F, w.b);   // CMSG_ARENA_TEAM_INVITE
                    return true;
                }
                Wr w; w.cstr(nombre); Srv(0x082, w.b);
                return true;
            }
            case CMSG_ACCEPT_GUILD_INVITE: Srv(0x084, {}); return true;
            case CMSG_GUILD_DECLINE_INVITATION: Srv(0x085, {}); return true;
            case CMSG_GUILD_LEAVE: Srv(0x08D, {}); return true;
            case CMSG_GUILD_DELETE: Srv(0x08F, {}); return true;
            case CMSG_GUILD_PROMOTE_MEMBER: case CMSG_GUILD_DEMOTE_MEMBER: case CMSG_GUILD_OFFICER_REMOVE_MEMBER:
            {
                ObjectGuid g;
                if (op == CMSG_GUILD_PROMOTE_MEMBER) { WorldPackets::Guild::GuildPromoteMember q(Paquete(CMSG_GUILD_PROMOTE_MEMBER, b)); q.Read(); g = q.Promotee; }
                else if (op == CMSG_GUILD_DEMOTE_MEMBER) { WorldPackets::Guild::GuildDemoteMember q(Paquete(CMSG_GUILD_DEMOTE_MEMBER, b)); q.Read(); g = q.Demotee; }
                else { WorldPackets::Guild::GuildOfficerRemoveMember q(Paquete(CMSG_GUILD_OFFICER_REMOVE_MEMBER, b)); q.Read(); g = q.Removee; }
                Wr w; w.cstr(NombreDe(mundo->A335(g)));
                Srv(op == CMSG_GUILD_PROMOTE_MEMBER ? 0x08B : op == CMSG_GUILD_DEMOTE_MEMBER ? 0x08C : 0x08E, w.b);
                return true;
            }
            case CMSG_GUILD_UPDATE_MOTD_TEXT:                // -> 0x091 cstr
            {
                WorldPackets::Guild::GuildUpdateMotdText q(Paquete(CMSG_GUILD_UPDATE_MOTD_TEXT, b)); q.Read();
                Wr w; w.cstr(std::string(q.MotdText)); Srv(0x091, w.b);
                return true;
            }
            case CMSG_GUILD_UPDATE_INFO_TEXT:                // -> 0x2FC cstr
            {
                WorldPackets::Guild::GuildUpdateInfoText q(Paquete(CMSG_GUILD_UPDATE_INFO_TEXT, b)); q.Read();
                Wr w; w.cstr(std::string(q.InfoText)); Srv(0x2FC, w.b);
                return true;
            }
            case CMSG_GUILD_SET_MEMBER_NOTE:                 // -> 0x234 pública / 0x235 de oficial: cstr nombre, cstr nota
            {
                WorldPackets::Guild::GuildSetMemberNote q(Paquete(CMSG_GUILD_SET_MEMBER_NOTE, b)); q.Read();
                Wr w; w.cstr(NombreDe(mundo->A335(q.NoteeGUID))).cstr(std::string(q.Note)); Srv(q.IsPublic ? 0x234 : 0x235, w.b);
                return true;
            }
            case CMSG_GUILD_SET_GUILD_MASTER:                // -> 0x090 cstr
            {
                WorldPackets::Guild::GuildSetGuildMaster q(Paquete(CMSG_GUILD_SET_GUILD_MASTER, b)); q.Read();
                Wr w; w.cstr(q.NewMasterName.substr(0, q.NewMasterName.find('-'))); Srv(0x090, w.b);
                return true;
            }

            // ---- correo
            case CMSG_MAIL_GET_LIST:                         // -> 0x23A u64
            {
                WorldPackets::Mail::MailGetList q(Paquete(CMSG_MAIL_GET_LIST, b)); q.Read();
                { std::lock_guard<std::mutex> l(_mMail); _buzon = mundo->A335(q.Mailbox); }
                Wr w; w.put<uint64>(mundo->A335(q.Mailbox)); Srv(0x23A, w.b);
                return true;
            }
            case CMSG_SEND_MAIL:                             // -> 0x238
            {
                WorldPackets::Mail::SendMail q(Paquete(CMSG_SEND_MAIL, b)); q.Read();
                auto const& in = q.Info;
                Wr w; w.put<uint64>(mundo->A335(in.Mailbox)).cstr(in.Target.substr(0, in.Target.find('-'))).cstr(in.Subject).cstr(in.Body)
                         .put<uint32>(uint32(in.StationeryID)).put<uint32>(0).put<uint8>(uint8(in.Attachments.size()));
                for (auto const& a : in.Attachments) w.put<uint8>(a.AttachPosition).put<uint64>(mundo->A335(a.ItemGUID));
                w.put<uint32>(uint32(in.SendMoney)).put<uint32>(uint32(in.Cod)).put<uint64>(0).put<uint8>(0);
                Srv(0x238, w.b);
                return true;
            }
            case CMSG_MAIL_TAKE_MONEY:                       // -> 0x245: u64, u32
            {
                WorldPackets::Mail::MailTakeMoney q(Paquete(CMSG_MAIL_TAKE_MONEY, b)); q.Read();
                Wr w; w.put<uint64>(mundo->A335(q.Mailbox)).put<uint32>(uint32(q.MailID)); Srv(0x245, w.b);
                return true;
            }
            case CMSG_MAIL_TAKE_ITEM:                        // -> 0x246: u64, u32 carta, u32 objeto
            {
                WorldPackets::Mail::MailTakeItem q(Paquete(CMSG_MAIL_TAKE_ITEM, b)); q.Read();
                Wr w; w.put<uint64>(mundo->A335(q.Mailbox)).put<uint32>(uint32(q.MailID)).put<uint32>(uint32(q.AttachID)); Srv(0x246, w.b);
                return true;
            }
            case CMSG_MAIL_MARK_AS_READ:                     // -> 0x247: u64, u32
            {
                WorldPackets::Mail::MailMarkAsRead q(Paquete(CMSG_MAIL_MARK_AS_READ, b)); q.Read();
                Wr w; w.put<uint64>(mundo->A335(q.Mailbox)).put<uint32>(uint32(q.MailID)); Srv(0x247, w.b);
                return true;
            }
            case CMSG_MAIL_RETURN_TO_SENDER:                 // -> 0x248: u64 buzón, u32, u64
            {
                WorldPackets::Mail::MailReturnToSender q(Paquete(CMSG_MAIL_RETURN_TO_SENDER, b)); q.Read();
                uint64 buzon; { std::lock_guard<std::mutex> l(_mMail); buzon = _buzon; }
                Wr w; w.put<uint64>(buzon).put<uint32>(uint32(q.MailID)).put<uint64>(mundo->A335(q.SenderGUID)); Srv(0x248, w.b);
                return true;
            }
            case CMSG_MAIL_DELETE:                           // -> 0x249: u64 buzón, u32, u32
            {
                WorldPackets::Mail::MailDelete q(Paquete(CMSG_MAIL_DELETE, b)); q.Read();
                uint64 buzon; { std::lock_guard<std::mutex> l(_mMail); buzon = _buzon; }
                Wr w; w.put<uint64>(buzon).put<uint32>(uint32(q.MailID)).put<uint32>(0); Srv(0x249, w.b);
                return true;
            }
            case CMSG_MAIL_CREATE_TEXT_ITEM:                 // -> 0x24A: u64, u32
            {
                WorldPackets::Mail::MailCreateTextItem q(Paquete(CMSG_MAIL_CREATE_TEXT_ITEM, b)); q.Read();
                Wr w; w.put<uint64>(mundo->A335(q.Mailbox)).put<uint32>(uint32(q.MailID)); Srv(0x24A, w.b);
                return true;
            }
            case CMSG_QUERY_NEXT_MAIL_TIME: Srv(0x284, {}); return true;

            // ---- vuelos
            case CMSG_TAXI_NODE_STATUS_QUERY:                // -> 0x1AA u64
            {
                WorldPackets::Taxi::TaxiNodeStatusQuery q(Paquete(CMSG_TAXI_NODE_STATUS_QUERY, b)); q.Read();
                Wr w; w.put<uint64>(mundo->A335(q.UnitGUID)); Srv(0x1AA, w.b);
                return true;
            }
            case CMSG_TAXI_QUERY_AVAILABLE_NODES:            // -> 0x1AC u64
            {
                WorldPackets::Taxi::TaxiQueryAvailableNodes q(Paquete(CMSG_TAXI_QUERY_AVAILABLE_NODES, b)); q.Read();
                Wr w; w.put<uint64>(mundo->A335(q.Unit)); Srv(0x1AC, w.b);
                return true;
            }
            case CMSG_ENABLE_TAXI_NODE:                      // -> 0x493 u64
            {
                WorldPackets::Taxi::EnableTaxiNode q(Paquete(CMSG_ENABLE_TAXI_NODE, b)); q.Read();
                Wr w; w.put<uint64>(mundo->A335(q.Unit)); Srv(0x493, w.b);
                return true;
            }
            case CMSG_ACTIVATE_TAXI:                         // -> 0x1AD: u64, u32 origen, u32 destino
            {
                WorldPackets::Taxi::ActivateTaxi q(Paquete(CMSG_ACTIVATE_TAXI, b)); q.Read();
                uint32 origen; { std::lock_guard<std::mutex> l(_mTaxi); origen = _nodoActual; }
                std::vector<uint32> ruta = RutaTaxi(origen, uint32(q.Node));
                if (ruta.size() < 2)                     // sin ruta conocida: que AzerothCore conteste el error (0x1AD origen -> destino)
                {
                    Wr w; w.put<uint64>(mundo->A335(q.Vendor)).put<uint32>(origen).put<uint32>(q.Node); Srv(0x1AD, w.b);
                    return true;
                }
                Wr w; w.put<uint64>(mundo->A335(q.Vendor)).put<uint32>(uint32(ruta.size()));
                for (uint32 n : ruta) w.put<uint32>(n);
                Srv(0x312, w.b);                         // CMSG_ACTIVATETAXIEXPRESS
                return true;
            }
            // ---- amigos e ignorados
            case CMSG_SEND_CONTACT_LIST:                     // -> 0x066 u32
            {
                WorldPackets::Social::SendContactList q(Paquete(CMSG_SEND_CONTACT_LIST, b)); q.Read();
                Wr w; w.put<uint32>(q.Flags); Srv(0x066, w.b);
                return true;
            }
            case CMSG_ADD_FRIEND:                            // -> 0x069: cstr nombre, cstr nota
            {
                WorldPackets::Social::AddFriend q(Paquete(CMSG_ADD_FRIEND, b)); q.Read();
                Wr w; w.cstr(q.Name.substr(0, q.Name.find('-'))).cstr(q.Notes); Srv(0x069, w.b);
                return true;
            }
            case CMSG_DEL_FRIEND:                            // -> 0x06A u64
            {
                WorldPackets::Social::DelFriend q(Paquete(CMSG_DEL_FRIEND, b)); q.Read();
                Wr w; w.put<uint64>(mundo->A335(q.Player.Guid)); Srv(0x06A, w.b);
                return true;
            }
            case CMSG_ADD_IGNORE:                            // -> 0x06C cstr
            {
                WorldPackets::Social::AddIgnore q(Paquete(CMSG_ADD_IGNORE, b)); q.Read();
                Wr w; w.cstr(q.Name.substr(0, q.Name.find('-'))); Srv(0x06C, w.b);
                return true;
            }
            case CMSG_DEL_IGNORE:                            // -> 0x06D u64
            {
                WorldPackets::Social::DelIgnore q(Paquete(CMSG_DEL_IGNORE, b)); q.Read();
                Wr w; w.put<uint64>(mundo->A335(q.Player.Guid)); Srv(0x06D, w.b);
                return true;
            }
            // ---- duelos
            case CMSG_DUEL_RESPONSE:                         // -> 0x16C aceptar / 0x16D rechazar (u64 bandera)
            {
                WorldPackets::Duel::DuelResponse q(Paquete(CMSG_DUEL_RESPONSE, b)); q.Read();
                Wr w; w.put<uint64>(mundo->A335(q.ArbiterGUID)); Srv(q.Accepted ? 0x16C : 0x16D, w.b);
                return true;
            }
            // ---- tiempo
            case CMSG_REQUEST_PLAYED_TIME:                   // -> 0x1CC u8
            {
                WorldPackets::Character::RequestPlayedTime q(Paquete(CMSG_REQUEST_PLAYED_TIME, b)); q.Read();
                Wr w; w.put<uint8>(q.TriggerScriptEvent ? 1 : 0); Srv(0x1CC, w.b);
                return true;
            }
            case CMSG_QUERY_TIME: Srv(0x1CE, {}); return true;

            // ---- grupos
            case CMSG_PARTY_INVITE:                          // -> 0x06E: cstr nombre, u32
            {
                WorldPackets::Party::PartyInviteClient q(Paquete(CMSG_PARTY_INVITE, b)); q.Read();
                Wr w; w.cstr(q.TargetName.substr(0, q.TargetName.find('-'))).put<uint32>(0); Srv(0x06E, w.b);
                return true;
            }
            case CMSG_PARTY_INVITE_RESPONSE:                 // -> 0x072 aceptar (u32) / 0x073 rechazar
            {
                WorldPackets::Party::PartyInviteResponse q(Paquete(CMSG_PARTY_INVITE_RESPONSE, b)); q.Read();
                if (q.Accept) { Wr w; w.put<uint32>(0); Srv(0x072, w.b); } else Srv(0x073, {});
                return true;
            }
            case CMSG_LEAVE_GROUP: Srv(0x07B, {}); return true;
            case CMSG_PARTY_UNINVITE:                        // -> 0x076: u64, cstr motivo
            {
                WorldPackets::Party::PartyUninvite q(Paquete(CMSG_PARTY_UNINVITE, b)); q.Read();
                Wr w; w.put<uint64>(mundo->A335(q.TargetGUID)).cstr(q.Reason); Srv(0x076, w.b);
                return true;
            }
            case CMSG_SET_PARTY_LEADER:                      // -> 0x078 u64
            {
                WorldPackets::Party::SetPartyLeader q(Paquete(CMSG_SET_PARTY_LEADER, b)); q.Read();
                Wr w; w.put<uint64>(mundo->A335(q.TargetGUID)); Srv(0x078, w.b);
                return true;
            }
            case CMSG_SET_LOOT_METHOD:                       // -> 0x07A: u32 método, u64 maestro, u32 umbral
            {
                WorldPackets::Party::SetLootMethod q(Paquete(CMSG_SET_LOOT_METHOD, b)); q.Read();
                Wr w; w.put<uint32>(q.LootMethod).put<uint64>(mundo->A335(q.LootMasterGUID)).put<uint32>(q.LootThreshold); Srv(0x07A, w.b);
                return true;
            }
            case CMSG_CONVERT_RAID: Srv(0x28E, {}); return true;
            case CMSG_SET_ASSISTANT_LEADER:                  // -> 0x28F: u64, u8
            {
                WorldPackets::Party::SetAssistantLeader q(Paquete(CMSG_SET_ASSISTANT_LEADER, b)); q.Read();
                Wr w; w.put<uint64>(mundo->A335(q.Target)).put<uint8>(q.Apply ? 1 : 0); Srv(0x28F, w.b);
                return true;
            }
            case CMSG_REQUEST_PARTY_MEMBER_STATS:            // -> 0x27F u64
            {
                WorldPackets::Party::RequestPartyMemberStats q(Paquete(CMSG_REQUEST_PARTY_MEMBER_STATS, b)); q.Read();
                Wr w; w.put<uint64>(mundo->A335(q.TargetGUID)); Srv(0x27F, w.b);
                return true;
            }
            case CMSG_MINIMAP_PING:                          // -> 0x1D5: float x, float y
            {
                WorldPackets::Party::MinimapPingClient q(Paquete(CMSG_MINIMAP_PING, b)); q.Read();
                Wr w; w.put<float>(q.PositionX).put<float>(q.PositionY); Srv(0x1D5, w.b);
                return true;
            }

            // ---- talentos
            case CMSG_LEARN_TALENT:                          // -> 0x251: u32 talento, u32 rango
            {
                WorldPackets::Talent::LearnTalent q(Paquete(CMSG_LEARN_TALENT, b)); q.Read();
                Wr w; w.put<uint32>(uint32(q.TalentID)).put<uint32>(q.RequestedRank); Srv(0x251, w.b);
                return true;
            }
            case CMSG_LEARN_PREVIEW_TALENTS:                 // -> 0x4C1: i32 pestaña, u32 n, (u32 talento, u32 rango)*
            {
                WorldPackets::Talent::LearnPreviewTalents q(Paquete(CMSG_LEARN_PREVIEW_TALENTS, b)); q.Read();
                Wr w; w.put<int32>(-1).put<uint32>(uint32(q.Talents.size()));
                for (auto const& ti : q.Talents) w.put<uint32>(ti.TalentID).put<uint32>(ti.Rank);
                Srv(0x4C1, w.b);
                return true;
            }

            // ---- muerte
            case CMSG_QUERY_CORPSE_LOCATION_FROM_CLIENT: Srv(0x216, {}); return true;   // -> MSG_CORPSE_QUERY
            case CMSG_RESURRECT_RESPONSE:                    // -> 0x15C: u64, u8
            {
                WorldPackets::Misc::ResurrectResponse q(Paquete(CMSG_RESURRECT_RESPONSE, b)); q.Read();
                Wr w; w.put<uint64>(mundo->A335(q.Resurrecter)).put<uint8>(q.Response == 0 ? 1 : 0); Srv(0x15C, w.b);
                return true;
            }

            // ---- mascotas
            case CMSG_QUERY_PET_NAME:                        // -> 0x052: u32 número de mascota, u64 guid
            {
                WorldPackets::Query::QueryPetName q(Paquete(CMSG_QUERY_PET_NAME, b)); q.Read();
                uint32 num = mundo->Valor335(q.UnitGUID, 75);   // UNIT_FIELD_PETNUMBER
                { std::lock_guard<std::mutex> l(_mPet); _mascotasPorNumero[num] = q.UnitGUID; }
                Wr w; w.put<uint32>(num).put<uint64>(mundo->A335(q.UnitGUID)); Srv(0x052, w.b);
                return true;
            }
            case CMSG_PET_ACTION:                            // -> 0x175: u64 mascota, u32 acción, u64 objetivo
            {
                WorldPackets::Pet::PetAction q(Paquete(CMSG_PET_ACTION, b)); q.Read();
                Wr w; w.put<uint64>(mundo->A335(q.PetGUID)).put<uint32>(BotonA335(q.Action)).put<uint64>(mundo->A335(q.TargetGUID)); Srv(0x175, w.b);
                return true;
            }
            case CMSG_PET_SET_ACTION:                        // -> 0x174: u64 mascota, u32 posición, u32 acción
            {
                WorldPackets::Pet::PetSetAction q(Paquete(CMSG_PET_SET_ACTION, b)); q.Read();
                Wr w; w.put<uint64>(mundo->A335(q.PetGUID)).put<uint32>(q.Index).put<uint32>(BotonA335(q.Action)); Srv(0x174, w.b);
                return true;
            }
            case CMSG_PET_CAST_SPELL:                        // -> 0x1F0: u64 mascota, u8 cuenta, u32 hechizo, u8 banderas, objetivos
            {
                WorldPackets::Spells::PetCastSpell q(Paquete(CMSG_PET_CAST_SPELL, b)); q.Read();
                uint8 cc = NuevoCast(q.Cast.CastID, uint32(q.Cast.SpellID));
                Wr w; w.put<uint64>(mundo->A335(q.PetGUID)).put<uint8>(cc).put<uint32>(uint32(q.Cast.SpellID)).put<uint8>(BanderasTrayecto(q.Cast));
                EscribirObjetivos(w, q.Cast.Target);
                EscribirTrayecto(w, q.Cast);
                Srv(0x1F0, w.b);
                return true;
            }
            case CMSG_PET_ABANDON:                           // -> 0x176 u64
            {
                WorldPackets::Pet::PetAbandon q(Paquete(CMSG_PET_ABANDON, b)); q.Read();
                Wr w; w.put<uint64>(mundo->A335(q.Pet)); Srv(0x176, w.b);
                return true;
            }
            case CMSG_PET_STOP_ATTACK:                       // -> 0x2EA u64
            {
                WorldPackets::Pet::PetStopAttack q(Paquete(CMSG_PET_STOP_ATTACK, b)); q.Read();
                Wr w; w.put<uint64>(mundo->A335(q.PetGUID)); Srv(0x2EA, w.b);
                return true;
            }

            // ---- botín
            case CMSG_LOOT_UNIT:                             // -> 0x15D u64
            {
                WorldPackets::Loot::LootUnit q(Paquete(CMSG_LOOT_UNIT, b)); q.Read();
                Wr w; w.put<uint64>(mundo->A335(q.Unit)); Srv(0x15D, w.b);
                return true;
            }
            case CMSG_LOOT_ITEM:                             // -> 0x108 CMSG_AUTOSTORE_LOOT_ITEM (u8 hueco) por cada uno
            {
                WorldPackets::Loot::LootItem q(Paquete(CMSG_LOOT_ITEM, b)); q.Read();
                for (auto const& l : q.Loot) { Wr w; w.put<uint8>(l.LootListID); Srv(0x108, w.b); }
                return true;
            }
            case CMSG_LOOT_MONEY: Srv(0x15E, {}); return true;
            case CMSG_LOOT_RELEASE:                          // -> 0x15F u64 del dueño del botín
            {
                WorldPackets::Loot::LootRelease q(Paquete(CMSG_LOOT_RELEASE, b)); q.Read();
                uint64 g;
                { std::lock_guard<std::mutex> l(_mLoot); g = (q.Unit == _lootObj || q.Unit.IsEmpty()) ? _lootDueno : mundo->A335(q.Unit); }
                Wr w; w.put<uint64>(g); Srv(0x15F, w.b);
                return true;
            }
            // ---- objetos del escenario y PNJ con ventana
            case CMSG_GAME_OBJ_USE:                          // -> 0x0B1 u64
            {
                WorldPackets::GameObject::GameObjUse q(Paquete(CMSG_GAME_OBJ_USE, b)); q.Read();
                uint8 tipoGo = M335::Octeto(mundo->Valor335(q.Guid, M335::GO_BYTES_1), 1);
                if (tipoGo == 19) Abrir(mundo->A335(q.Guid), PlayerInteractionType::MailInfo);
                else if (tipoGo == 34) Abrir(mundo->A335(q.Guid), PlayerInteractionType::GuildBanker);
                Wr w; w.put<uint64>(mundo->A335(q.Guid)); Srv(0x0B1, w.b);
                return true;
            }
            case CMSG_GAME_OBJ_REPORT_USE:                   // -> 0x481 u64
            {
                WorldPackets::GameObject::GameObjReportUse q(Paquete(CMSG_GAME_OBJ_REPORT_USE, b)); q.Read();
                Wr w; w.put<uint64>(mundo->A335(q.Guid)); Srv(0x481, w.b);
                return true;
            }
            case CMSG_TRAINER_LIST: case CMSG_BINDER_ACTIVATE: case CMSG_BANKER_ACTIVATE: case CMSG_SPIRIT_HEALER_ACTIVATE:
            case CMSG_TABARD_VENDOR_ACTIVATE:
            {
                WorldPackets::NPC::Hello q(Paquete(OpcodeClient(op), b)); q.Read();
                uint16 o335 = op == CMSG_TRAINER_LIST ? 0x1B0 : op == CMSG_BINDER_ACTIVATE ? 0x1B5 : op == CMSG_BANKER_ACTIVATE ? 0x1B7
                            : op == CMSG_SPIRIT_HEALER_ACTIVATE ? 0x21C : 0x1F2;
                Wr w; w.put<uint64>(mundo->A335(q.Unit)); Srv(o335, w.b);
                return true;
            }
            case CMSG_TRAINER_BUY_SPELL:                     // -> 0x1B2: u64, u32 hechizo
            {
                WorldPackets::NPC::TrainerBuySpell q(Paquete(CMSG_TRAINER_BUY_SPELL, b)); q.Read();
                Wr w; w.put<uint64>(mundo->A335(q.TrainerGUID)).put<uint32>(uint32(q.SpellID)); Srv(0x1B2, w.b);
                return true;
            }

            // ---- diálogos y misiones
            case CMSG_TALK_TO_GOSSIP:                          // -> 0x17B u64
            {
                WorldPackets::NPC::Hello q(Paquete(CMSG_TALK_TO_GOSSIP, b)); q.Read();
                Wr w; w.put<uint64>(mundo->A335(q.Unit)); Srv(0x17B, w.b);
                return true;
            }
            case CMSG_GOSSIP_SELECT_OPTION:                  // -> 0x17C: u64, u32 menú, u32 opción, [cstr código]
            {
                WorldPackets::NPC::GossipSelectOption q(Paquete(CMSG_GOSSIP_SELECT_OPTION, b)); q.Read();
                Wr w; w.put<uint64>(mundo->A335(q.GossipUnit)).put<uint32>(uint32(q.GossipID)).put<uint32>(uint32(q.GossipOptionID));
                if (!q.PromotionCode.empty()) w.cstr(q.PromotionCode);
                Srv(0x17C, w.b);
                return true;
            }
            case CMSG_QUERY_NPC_TEXT:                        // respuesta local desde acore_world.npc_text
            {
                WorldPackets::Query::QueryNPCText q(Paquete(CMSG_QUERY_NPC_TEXT, b)); q.Read();
                TextoPNJ(q.TextID);
                return true;
            }
            case CMSG_QUEST_GIVER_HELLO:                     // -> 0x184 u64
            {
                WorldPackets::Quest::QuestGiverHello q(Paquete(CMSG_QUEST_GIVER_HELLO, b)); q.Read();
                Wr w; w.put<uint64>(mundo->A335(q.QuestGiverGUID)); Srv(0x184, w.b);
                return true;
            }
            case CMSG_QUEST_GIVER_QUERY_QUEST:               // -> 0x186: u64, u32, u8
            {
                WorldPackets::Quest::QuestGiverQueryQuest q(Paquete(CMSG_QUEST_GIVER_QUERY_QUEST, b)); q.Read();
                Wr w; w.put<uint64>(mundo->A335(q.QuestGiverGUID)).put<uint32>(uint32(q.QuestID)).put<uint8>(q.RespondToGiver ? 1 : 0); Srv(0x186, w.b);
                return true;
            }
            case CMSG_QUEST_GIVER_ACCEPT_QUEST:              // -> 0x189: u64, u32, u32
            {
                WorldPackets::Quest::QuestGiverAcceptQuest q(Paquete(CMSG_QUEST_GIVER_ACCEPT_QUEST, b)); q.Read();
                Wr w; w.put<uint64>(mundo->A335(q.QuestGiverGUID)).put<uint32>(uint32(q.QuestID)).put<uint32>(0); Srv(0x189, w.b);
                return true;
            }
            case CMSG_QUEST_GIVER_COMPLETE_QUEST:            // -> 0x18A: u64, u32
            {
                WorldPackets::Quest::QuestGiverCompleteQuest q(Paquete(CMSG_QUEST_GIVER_COMPLETE_QUEST, b)); q.Read();
                Wr w; w.put<uint64>(mundo->A335(q.QuestGiverGUID)).put<uint32>(uint32(q.QuestID)); Srv(0x18A, w.b);
                return true;
            }
            case CMSG_QUEST_GIVER_REQUEST_REWARD:            // -> 0x18C: u64, u32
            {
                WorldPackets::Quest::QuestGiverRequestReward q(Paquete(CMSG_QUEST_GIVER_REQUEST_REWARD, b)); q.Read();
                Wr w; w.put<uint64>(mundo->A335(q.QuestGiverGUID)).put<uint32>(uint32(q.QuestID)); Srv(0x18C, w.b);
                return true;
            }
            case CMSG_QUEST_GIVER_CHOOSE_REWARD:             // -> 0x18E: u64, u32, u32 índice de la recompensa elegida
            {
                WorldPackets::Quest::QuestGiverChooseReward q(Paquete(CMSG_QUEST_GIVER_CHOOSE_REWARD, b)); q.Read();
                uint32 idx = 0;
                {
                    std::lock_guard<std::mutex> l(_mMis);
                    auto const& v = _eleccion[uint32(q.QuestID)];
                    for (uint32 i = 0; i < v.size(); ++i) if (v[i] == q.Choice.Item.ItemID) { idx = i; break; }
                }
                Wr w; w.put<uint64>(mundo->A335(q.QuestGiverGUID)).put<uint32>(uint32(q.QuestID)).put<uint32>(idx); Srv(0x18E, w.b);
                return true;
            }
            case CMSG_QUEST_LOG_REMOVE_QUEST:                // -> 0x194 u8
            {
                WorldPackets::Quest::QuestLogRemoveQuest q(Paquete(CMSG_QUEST_LOG_REMOVE_QUEST, b)); q.Read();
                Wr w; w.put<uint8>(q.Entry); Srv(0x194, w.b);
                return true;
            }
            case CMSG_QUEST_CONFIRM_ACCEPT:                  // -> 0x19B u32
            {
                WorldPackets::Quest::QuestConfirmAccept q(Paquete(CMSG_QUEST_CONFIRM_ACCEPT, b)); q.Read();
                Wr w; w.put<uint32>(uint32(q.QuestID)); Srv(0x19B, w.b);
                return true;
            }
            case CMSG_PUSH_QUEST_TO_PARTY:                   // -> 0x19D u32
            {
                WorldPackets::Quest::PushQuestToParty q(Paquete(CMSG_PUSH_QUEST_TO_PARTY, b)); q.Read();
                Wr w; w.put<uint32>(uint32(q.QuestID)); Srv(0x19D, w.b);
                return true;
            }
            case CMSG_QUERY_QUEST_INFO:                      // -> 0x05C u32
            {
                WorldPackets::Quest::QueryQuestInfo q(Paquete(CMSG_QUERY_QUEST_INFO, b)); q.Read();
                Wr w; w.put<uint32>(uint32(q.QuestID)); Srv(0x05C, w.b);
                return true;
            }
            case CMSG_QUEST_GIVER_CLOSE_QUEST:
                return true;

            // ---- inventario (huecos 3.4.3 -> 3.3.5)
            case CMSG_SWAP_INV_ITEM:                         // -> 0x10D: u8 destino, u8 origen
            {
                WorldPackets::Item::SwapInvItem q(Paquete(CMSG_SWAP_INV_ITEM, b)); q.Read();
                Wr w; w.put<uint8>(M335::HuecoA335(q.Slot2)).put<uint8>(M335::HuecoA335(q.Slot1)); Srv(0x10D, w.b);
                return true;
            }
            case CMSG_SWAP_ITEM:                             // -> 0x10C: u8 bolsa dst, u8 hueco dst, u8 bolsa src, u8 hueco src
            {
                WorldPackets::Item::SwapItem q(Paquete(CMSG_SWAP_ITEM, b)); q.Read();
                Wr w;
                w.put<uint8>(Bolsa(q.ContainerSlotB)).put<uint8>(Hueco(q.ContainerSlotB, q.SlotB))
                 .put<uint8>(Bolsa(q.ContainerSlotA)).put<uint8>(Hueco(q.ContainerSlotA, q.SlotA));
                Srv(0x10C, w.b);
                return true;
            }
            case CMSG_AUTO_EQUIP_ITEM:                       // -> 0x10A: u8 bolsa, u8 hueco
            {
                WorldPackets::Item::AutoEquipItem q(Paquete(CMSG_AUTO_EQUIP_ITEM, b)); q.Read();
                Wr w; w.put<uint8>(Bolsa(q.PackSlot)).put<uint8>(Hueco(q.PackSlot, q.Slot)); Srv(0x10A, w.b);
                return true;
            }
            case CMSG_AUTO_STORE_BAG_ITEM:                   // -> 0x10B: u8 bolsa src, u8 hueco src, u8 bolsa dst
            {
                WorldPackets::Item::AutoStoreBagItem q(Paquete(CMSG_AUTO_STORE_BAG_ITEM, b)); q.Read();
                Wr w; w.put<uint8>(Bolsa(q.ContainerSlotA)).put<uint8>(Hueco(q.ContainerSlotA, q.SlotA)).put<uint8>(Bolsa(q.ContainerSlotB));
                Srv(0x10B, w.b);
                return true;
            }
            case CMSG_DESTROY_ITEM:                          // -> 0x111: u8 bolsa, u8 hueco, u8 cantidad, 3 x u8
            {
                WorldPackets::Item::DestroyItem q(Paquete(CMSG_DESTROY_ITEM, b)); q.Read();
                Wr w; w.put<uint8>(Bolsa(q.ContainerId)).put<uint8>(Hueco(q.ContainerId, q.SlotNum)).put<uint8>(uint8(std::min<uint32>(q.Count, 255)))
                         .put<uint8>(0).put<uint8>(0).put<uint8>(0);
                Srv(0x111, w.b);
                return true;
            }
            case CMSG_SPLIT_ITEM:                            // -> 0x10E: u8 bolsa src, hueco src, bolsa dst, hueco dst, u32 cantidad
            {
                WorldPackets::Item::SplitItem q(Paquete(CMSG_SPLIT_ITEM, b)); q.Read();
                Wr w; w.put<uint8>(Bolsa(q.FromPackSlot)).put<uint8>(Hueco(q.FromPackSlot, q.FromSlot))
                         .put<uint8>(Bolsa(q.ToPackSlot)).put<uint8>(Hueco(q.ToPackSlot, q.ToSlot)).put<uint32>(uint32(q.Quantity));
                Srv(0x10E, w.b);
                return true;
            }
            case CMSG_AUTO_EQUIP_ITEM_SLOT:                  // -> 0x10F: u64 objeto, u8 hueco
            {
                WorldPackets::Item::AutoEquipItemSlot q(Paquete(CMSG_AUTO_EQUIP_ITEM_SLOT, b)); q.Read();
                Wr w; w.put<uint64>(mundo->A335(q.Item)).put<uint8>(M335::HuecoA335(q.ItemDstSlot)); Srv(0x10F, w.b);
                return true;
            }
            case CMSG_READ_ITEM:                             // -> 0x0AD: u8 bolsa, u8 hueco
            {
                WorldPackets::Item::ReadItem q(Paquete(CMSG_READ_ITEM, b)); q.Read();
                Wr w; w.put<uint8>(Bolsa(q.PackSlot)).put<uint8>(Hueco(q.PackSlot, q.Slot)); Srv(0x0AD, w.b);
                return true;
            }
            case CMSG_OPEN_ITEM:                             // -> 0x0AC: u8 bolsa, u8 hueco
            {
                WorldPackets::Spells::OpenItem q(Paquete(CMSG_OPEN_ITEM, b)); q.Read();
                Wr w; w.put<uint8>(Bolsa(q.PackSlot)).put<uint8>(Hueco(q.PackSlot, q.Slot)); Srv(0x0AC, w.b);
                return true;
            }
            case CMSG_USE_ITEM:                              // -> 0x0AB: u8 bolsa, u8 hueco, u8 cuenta, u32 hechizo, u64 objeto, u32 glifo, u8 banderas, objetivos
            {
                WorldPackets::Spells::UseItem q(Paquete(CMSG_USE_ITEM, b)); q.Read();
                uint8 cc = NuevoCast(q.Cast.CastID, uint32(q.Cast.SpellID));
                Wr w; w.put<uint8>(Bolsa(q.PackSlot)).put<uint8>(Hueco(q.PackSlot, q.Slot)).put<uint8>(cc).put<uint32>(uint32(q.Cast.SpellID))
                         .put<uint64>(mundo->A335(q.CastItem)).put<uint32>(0).put<uint8>(BanderasTrayecto(q.Cast));
                EscribirObjetivos(w, q.Cast.Target);
                EscribirTrayecto(w, q.Cast);
                Srv(0x0AB, w.b);
                return true;
            }
            // ---- vendedores
            case CMSG_LIST_INVENTORY:                        // -> 0x19E u64
            {
                WorldPackets::NPC::Hello q(Paquete(CMSG_LIST_INVENTORY, b)); q.Read();
                Wr w; w.put<uint64>(mundo->A335(q.Unit)); Srv(0x19E, w.b);
                return true;
            }
            case CMSG_SELL_ITEM:                             // -> 0x1A0: u64 vendedor, u64 objeto, u32 cantidad
            {
                WorldPackets::Item::SellItem q(Paquete(CMSG_SELL_ITEM, b)); q.Read();
                Wr w; w.put<uint64>(mundo->A335(q.VendorGUID)).put<uint64>(mundo->A335(q.ItemGUID)).put<uint32>(q.Amount); Srv(0x1A0, w.b);
                return true;
            }
            case CMSG_BUY_ITEM:                              // -> 0x1A2: u64 vendedor, u32 objeto, u32 hueco (MuID), u32 cantidad, u8
            {
                WorldPackets::Item::BuyItem q(Paquete(CMSG_BUY_ITEM, b)); q.Read();
                Wr w; w.put<uint64>(mundo->A335(q.VendorGUID)).put<uint32>(q.Item.ItemID).put<uint32>(q.Muid).put<uint32>(uint32(q.Quantity)).put<uint8>(0);
                Srv(0x1A2, w.b);
                return true;
            }
            case CMSG_BUY_BACK_ITEM:                         // -> 0x290: u64 vendedor, u32 hueco de recompra
            {
                WorldPackets::Item::BuyBackItem q(Paquete(CMSG_BUY_BACK_ITEM, b)); q.Read();
                Wr w; w.put<uint64>(mundo->A335(q.VendorGUID)).put<uint32>(M335::HuecoA335(q.Slot)); Srv(0x290, w.b);
                return true;
            }
            case CMSG_REPAIR_ITEM:                           // -> 0x2A8: u64 PNJ, u64 objeto, u8 banco de hermandad
            {
                WorldPackets::Item::RepairItem q(Paquete(CMSG_REPAIR_ITEM, b)); q.Read();
                Wr w; w.put<uint64>(mundo->A335(q.NpcGUID)).put<uint64>(mundo->A335(q.ItemGUID)).put<uint8>(q.UseGuildBank ? 1 : 0); Srv(0x2A8, w.b);
                return true;
            }
            // ---- confirmaciones de movimiento: packguid, u32 contador, movimiento [, float | u32]
            case CMSG_MOVE_FORCE_WALK_SPEED_CHANGE_ACK: case CMSG_MOVE_FORCE_RUN_SPEED_CHANGE_ACK: case CMSG_MOVE_FORCE_RUN_BACK_SPEED_CHANGE_ACK:
            case CMSG_MOVE_FORCE_SWIM_SPEED_CHANGE_ACK: case CMSG_MOVE_FORCE_SWIM_BACK_SPEED_CHANGE_ACK: case CMSG_MOVE_FORCE_TURN_RATE_CHANGE_ACK:
            case CMSG_MOVE_FORCE_FLIGHT_SPEED_CHANGE_ACK: case CMSG_MOVE_FORCE_FLIGHT_BACK_SPEED_CHANGE_ACK: case CMSG_MOVE_FORCE_PITCH_RATE_CHANGE_ACK:
            {
                WorldPackets::Movement::MovementSpeedAck q(Paquete(OpcodeClient(op), b)); q.Read();
                uint16 o335 = op == CMSG_MOVE_FORCE_WALK_SPEED_CHANGE_ACK ? 0x2DB : op == CMSG_MOVE_FORCE_RUN_SPEED_CHANGE_ACK ? 0x0E3
                            : op == CMSG_MOVE_FORCE_RUN_BACK_SPEED_CHANGE_ACK ? 0x0E5 : op == CMSG_MOVE_FORCE_SWIM_SPEED_CHANGE_ACK ? 0x0E7
                            : op == CMSG_MOVE_FORCE_SWIM_BACK_SPEED_CHANGE_ACK ? 0x2DD : op == CMSG_MOVE_FORCE_TURN_RATE_CHANGE_ACK ? 0x2DF
                            : op == CMSG_MOVE_FORCE_FLIGHT_SPEED_CHANGE_ACK ? 0x382 : op == CMSG_MOVE_FORCE_FLIGHT_BACK_SPEED_CHANGE_ACK ? 0x384 : 0x45D;
                uint64 quien = q.Ack.Status.guid.IsEmpty() ? 0 : mundo->A335(q.Ack.Status.guid);   // el vehículo si lo conduce
                Wr w; PutPack(w, quien ? quien : yo335); w.put<uint32>(uint32(q.Ack.AckIndex));
                auto mv = mundo->MovInfo335(q.Ack.Status); w.raw(mv.data(), mv.size());
                w.put<float>(q.Speed);
                Srv(o335, w.b);
                return true;
            }
            case CMSG_MOVE_FORCE_ROOT_ACK: case CMSG_MOVE_FORCE_UNROOT_ACK: case CMSG_MOVE_KNOCK_BACK_ACK:
            case CMSG_MOVE_WATER_WALK_ACK: case CMSG_MOVE_HOVER_ACK: case CMSG_MOVE_FEATHER_FALL_ACK: case CMSG_MOVE_SET_CAN_FLY_ACK:
            case CMSG_MOVE_GRAVITY_DISABLE_ACK: case CMSG_MOVE_GRAVITY_ENABLE_ACK:
            {
                WorldPackets::Movement::MovementAckMessage q(Paquete(OpcodeClient(op), b));
                if (op == CMSG_MOVE_KNOCK_BACK_ACK) { WorldPackets::Movement::MoveKnockBackAck k(Paquete(OpcodeClient(op), b)); k.Read(); q.Ack = k.Ack; }
                else q.Read();
                uint16 o335 = op == CMSG_MOVE_FORCE_ROOT_ACK ? 0x0E9 : op == CMSG_MOVE_FORCE_UNROOT_ACK ? 0x0EB : op == CMSG_MOVE_KNOCK_BACK_ACK ? 0x0F0
                            : op == CMSG_MOVE_WATER_WALK_ACK ? 0x2D0 : op == CMSG_MOVE_HOVER_ACK ? 0x0F6 : op == CMSG_MOVE_FEATHER_FALL_ACK ? 0x2CF
                            : op == CMSG_MOVE_GRAVITY_DISABLE_ACK ? 0x4CF : op == CMSG_MOVE_GRAVITY_ENABLE_ACK ? 0x4D1 : 0x345;
                uint64 quien = q.Ack.Status.guid.IsEmpty() ? 0 : mundo->A335(q.Ack.Status.guid);   // el vehículo si lo conduce
                Wr w; PutPack(w, quien ? quien : yo335); w.put<uint32>(uint32(q.Ack.AckIndex));
                auto mv = mundo->MovInfo335(q.Ack.Status); w.raw(mv.data(), mv.size());
                if (o335 == 0x2D0 || o335 == 0x0F6 || o335 == 0x2CF || o335 == 0x345 || o335 == 0x4CF || o335 == 0x4D1) w.put<uint32>(1);   // isApplied
                Srv(o335, w.b);
                return true;
            }
            case CMSG_MOVE_SET_COLLISION_HEIGHT_ACK:         // -> CMSG_MOVE_SET_COLLISION_HGT_ACK 0x517: packguid, u32 contador, movimiento, float altura
            {
                WorldPackets::Movement::MoveSetCollisionHeightAck q(Paquete(CMSG_MOVE_SET_COLLISION_HEIGHT_ACK, b)); q.Read();
                Wr w; PutPack(w, yo335); w.put<uint32>(uint32(q.Data.AckIndex));
                auto mv = mundo->MovInfo335(q.Data.Status); w.raw(mv.data(), mv.size());
                w.put<float>(q.Height);
                Srv(0x517, w.b);
                return true;
            }
            case CMSG_MOVE_TELEPORT_ACK:                     // -> 0x0C7: packguid, u32 contador, u32 hora
            {
                WorldPackets::Movement::MoveTeleportAck q(Paquete(CMSG_MOVE_TELEPORT_ACK, b)); q.Read();
                Wr w; PutPack(w, yo335); w.put<uint32>(uint32(q.AckIndex)).put<uint32>(uint32(q.MoveTime));
                Srv(0x0C7, w.b);
                return true;
            }
            case CMSG_WORLD_PORT_RESPONSE: Srv(0x0DC, {}); return true;   // -> MSG_MOVE_WORLDPORT_ACK
            case CMSG_LOGOUT_REQUEST: Srv(0x04B, {}); return true;
            case CMSG_LOGOUT_CANCEL: Srv(0x04E, {}); return true;
            case CMSG_QUEST_GIVER_STATUS_MULTIPLE_QUERY:     // -> 0x417 (vacío)
                Srv(0x417, {});
                return true;

            // ---- mundo: disparadores (entradas de mazmorra, posadas, objetivos de explorar), montura, etc.
            case CMSG_AREA_TRIGGER:                          // -> 0x0B4 u32 (el 3.3.5 solo avisa al entrar)
            {
                WorldPackets::AreaTrigger::AreaTrigger q(Paquete(CMSG_AREA_TRIGGER, b)); q.Read();
                if (q.Entered) { Wr w; w.put<uint32>(uint32(q.AreaTriggerID)); Srv(0x0B4, w.b); }
                return true;
            }
            case CMSG_CANCEL_MOUNT_AURA: Srv(0x375, {}); return true;
            case CMSG_SET_ACTIVE_MOVER:                      // -> 0x26A u64
            {
                WorldPackets::Movement::SetActiveMover q(Paquete(CMSG_SET_ACTIVE_MOVER, b)); q.Read();
                Wr w; w.put<uint64>(mundo->A335(q.ActiveMover)); Srv(0x26A, w.b);
                return true;
            }
            case CMSG_MOVE_TIME_SKIPPED:                     // -> 0x2CE: packguid, u32
            {
                WorldPackets::Movement::MoveTimeSkipped q(Paquete(CMSG_MOVE_TIME_SKIPPED, b)); q.Read();
                Wr w; PutPack(w, mundo->A335(q.MoverGUID)); w.put<uint32>(q.TimeSkipped); Srv(0x2CE, w.b);
                return true;
            }
            case CMSG_SET_ACTION_BAR_TOGGLES:                // -> 0x2BF u8
            {
                WorldPackets::Character::SetActionBarToggles q(Paquete(CMSG_SET_ACTION_BAR_TOGGLES, b)); q.Read();
                Wr w; w.put<uint8>(q.Mask); Srv(0x2BF, w.b);
                return true;
            }
            case CMSG_SET_TITLE:                             // -> 0x374 u32 (índice de bit; -1 = ninguno)
            {
                WorldPackets::Character::SetTitle q(Paquete(CMSG_SET_TITLE, b)); q.Read();
                Wr w; w.put<uint32>(q.TitleID > 0 ? uint32(q.TitleID) : 0); Srv(0x374, w.b);
                return true;
            }
            case CMSG_SET_FACTION_AT_WAR: case CMSG_SET_FACTION_NOT_AT_WAR:   // -> 0x125: u32 índice, u8 en guerra
            {
                uint16 idx;
                if (op == CMSG_SET_FACTION_AT_WAR) { WorldPackets::Character::SetFactionAtWar q(Paquete(CMSG_SET_FACTION_AT_WAR, b)); q.Read(); idx = q.FactionIndex; }
                else { WorldPackets::Character::SetFactionNotAtWar q(Paquete(CMSG_SET_FACTION_NOT_AT_WAR, b)); q.Read(); idx = q.FactionIndex; }
                Wr w; w.put<uint32>(idx).put<uint8>(op == CMSG_SET_FACTION_AT_WAR ? 1 : 0); Srv(0x125, w.b);
                return true;
            }
            case CMSG_SET_FACTION_INACTIVE:                  // -> 0x317: u32, u8
            {
                WorldPackets::Character::SetFactionInactive q(Paquete(CMSG_SET_FACTION_INACTIVE, b)); q.Read();
                Wr w; w.put<uint32>(q.Index).put<uint8>(q.State ? 1 : 0); Srv(0x317, w.b);
                return true;
            }
            case CMSG_SET_WATCHED_FACTION:                   // -> 0x318 u32
            {
                WorldPackets::Character::SetWatchedFaction q(Paquete(CMSG_SET_WATCHED_FACTION, b)); q.Read();
                Wr w; w.put<uint32>(q.FactionIndex); Srv(0x318, w.b);
                return true;
            }
            case CMSG_RANDOM_ROLL:                           // -> MSG_RANDOM_ROLL 0x1FB: u32 mín, u32 máx
            {
                WorldPackets::Misc::RandomRollClient q(Paquete(CMSG_RANDOM_ROLL, b)); q.Read();
                Wr w; w.put<uint32>(uint32(q.Min)).put<uint32>(uint32(q.Max)); Srv(0x1FB, w.b);
                return true;
            }
            case CMSG_TOGGLE_PVP: Srv(0x253, {}); return true;
            case CMSG_SET_PVP:                               // -> 0x253 u8
            {
                WorldPackets::Misc::SetPvP q(Paquete(CMSG_SET_PVP, b)); q.Read();
                Wr w; w.put<uint8>(q.EnablePVP ? 1 : 0); Srv(0x253, w.b);
                return true;
            }
            case CMSG_SUMMON_RESPONSE:                       // -> 0x2AC: u64, u8
            {
                WorldPackets::Movement::SummonResponse q(Paquete(CMSG_SUMMON_RESPONSE, b)); q.Read();
                Wr w; w.put<uint64>(mundo->A335(q.SummonerGUID)).put<uint8>(q.Accept ? 1 : 0); Srv(0x2AC, w.b);
                return true;
            }
            case CMSG_TOTEM_DESTROYED:                       // -> 0x414 u8
            {
                WorldPackets::Totem::TotemDestroyed q(Paquete(CMSG_TOTEM_DESTROYED, b)); q.Read();
                Wr w; w.put<uint8>(q.Slot); Srv(0x414, w.b);
                return true;
            }
            case CMSG_UNLEARN_SKILL:                         // -> 0x202 u32
            {
                WorldPackets::Spells::UnlearnSkill q(Paquete(CMSG_UNLEARN_SKILL, b)); q.Read();
                Wr w; w.put<uint32>(q.SkillLine); Srv(0x202, w.b);
                return true;
            }
            case CMSG_CANCEL_TEMP_ENCHANTMENT:               // -> 0x379 u32
            {
                WorldPackets::Item::CancelTempEnchantment q(Paquete(CMSG_CANCEL_TEMP_ENCHANTMENT, b)); q.Read();
                Wr w; w.put<uint32>(uint32(q.Slot)); Srv(0x379, w.b);
                return true;
            }
            case CMSG_LOOT_ROLL:                             // -> 0x2A0: u64 botín, u32 hueco, u8 tipo
            {
                WorldPackets::Loot::LootRoll q(Paquete(CMSG_LOOT_ROLL, b)); q.Read();
                Wr w; w.put<uint64>(TiradaSrc(q.LootObj)).put<uint32>(q.LootListID).put<uint8>(q.RollType); Srv(0x2A0, w.b);
                return true;
            }
            case CMSG_OPT_OUT_OF_LOOT:                       // -> 0x409 u32
            {
                WorldPackets::Party::OptOutOfLoot q(Paquete(CMSG_OPT_OUT_OF_LOOT, b)); q.Read();
                Wr w; w.put<uint32>(q.PassOnLoot ? 1 : 0); Srv(0x409, w.b);
                return true;
            }
            case CMSG_REQUEST_RAID_INFO: Srv(0x2CD, {}); return true;
            case CMSG_QUERY_INSPECT_ACHIEVEMENTS:            // -> 0x46B packguid
            {
                WorldPackets::Inspect::QueryInspectAchievements q(Paquete(CMSG_QUERY_INSPECT_ACHIEVEMENTS, b)); q.Read();
                Wr w; PutPack(w, mundo->A335(q.Guid)); Srv(0x46B, w.b);
                return true;
            }
            case CMSG_REQUEST_PET_INFO: Srv(0x279, {}); return true;
            case CMSG_BATTLEMASTER_JOIN_ARENA:               // -> 0x358: u64 maestro, u8 hueco (0 2v2, 1 3v3, 2 5v5), u8 en grupo, u8 puntuada
            {
                WorldPackets::Battleground::BattlemasterJoinArena q(Paquete(CMSG_BATTLEMASTER_JOIN_ARENA, b)); q.Read();
                Wr w; w.put<uint64>(_maestroBatalla).put<uint8>(q.TeamSizeIndex).put<uint8>(1).put<uint8>(1); Srv(0x358, w.b);
                return true;
            }
            case CMSG_BATTLEMASTER_JOIN_SKIRMISH:            // escaramuza = la misma petición 0x358 sin puntuar (u8 hueco 0/1/2, u8 en grupo, u8 0)
            {
                WorldPackets::Battleground::BattlemasterJoinSkirmishArena q(Paquete(CMSG_BATTLEMASTER_JOIN_SKIRMISH, b)); q.Read();
                uint64 maestro = q.BattlemasterGuid.IsEmpty() ? _maestroBatalla : mundo->A335(q.BattlemasterGuid);
                Wr w; w.put<uint64>(maestro).put<uint8>(q.Bracket).put<uint8>(q.JoinAsGroup ? 1 : 0).put<uint8>(0); Srv(0x358, w.b);
                return true;
            }
            // ---- vehículos
            case CMSG_REQUEST_VEHICLE_PREV_SEAT: Srv(0x477, {}); return true;
            case CMSG_REQUEST_VEHICLE_NEXT_SEAT: Srv(0x478, {}); return true;
            case CMSG_REQUEST_VEHICLE_SWITCH_SEAT:           // -> 0x479: packguid, i8 asiento
            {
                WorldPackets::Vehicle::RequestVehicleSwitchSeat q(Paquete(CMSG_REQUEST_VEHICLE_SWITCH_SEAT, b)); q.Read();
                Wr w; PutPack(w, mundo->A335(q.Vehicle)); w.put<int8>(int8(q.SeatIndex)); Srv(0x479, w.b);
                return true;
            }
            case CMSG_MOVE_CHANGE_VEHICLE_SEATS:             // -> 0x49B: movimiento del vehículo, packguid destino, i8 asiento
            {
                WorldPackets::Vehicle::MoveChangeVehicleSeats q(Paquete(CMSG_MOVE_CHANGE_VEHICLE_SEATS, b)); q.Read();
                std::vector<uint8> mov = mundo->MovimientoA335(q.Status);
                Wr w; w.raw(mov.data(), mov.size()); PutPack(w, mundo->A335(q.DstVehicle)); w.put<int8>(int8(q.DstSeatIndex));
                Srv(0x49B, w.b);
                return true;
            }
            case CMSG_MOVE_DISMISS_VEHICLE:                  // -> 0x46D: movimiento del vehículo
            {
                WorldPackets::Vehicle::MoveDismissVehicle q(Paquete(CMSG_MOVE_DISMISS_VEHICLE, b)); q.Read();
                Srv(0x46D, mundo->MovimientoA335(q.Status));
                return true;
            }
            case CMSG_RIDE_VEHICLE_INTERACT:                 // -> 0x4A8 u64
            {
                WorldPackets::Vehicle::RideVehicleInteract q(Paquete(CMSG_RIDE_VEHICLE_INTERACT, b)); q.Read();
                Wr w; w.put<uint64>(mundo->A335(q.Vehicle)); Srv(0x4A8, w.b);
                return true;
            }
            case CMSG_EJECT_PASSENGER:                       // -> 0x4A9 u64
            {
                WorldPackets::Vehicle::EjectPassenger q(Paquete(CMSG_EJECT_PASSENGER, b)); q.Read();
                Wr w; w.put<uint64>(mundo->A335(q.Passenger)); Srv(0x4A9, w.b);
                return true;
            }
            // ---- banco de hermandad
            case CMSG_GUILD_BANK_ACTIVATE:                   // -> 0x3E6: u64, u8 completo
            {
                WorldPackets::Guild::GuildBankActivate q(Paquete(CMSG_GUILD_BANK_ACTIVATE, b)); q.Read();
                Wr w; w.put<uint64>(mundo->A335(q.Banker)).put<uint8>(q.FullUpdate ? 1 : 0); Srv(0x3E6, w.b);
                return true;
            }
            case CMSG_GUILD_BANK_QUERY_TAB:                  // -> 0x3E7: u64, u8 pestaña, u8 completo
            {
                WorldPackets::Guild::GuildBankQueryTab q(Paquete(CMSG_GUILD_BANK_QUERY_TAB, b)); q.Read();
                Wr w; w.put<uint64>(mundo->A335(q.Banker)).put<uint8>(q.Tab).put<uint8>(q.FullUpdate ? 1 : 0); Srv(0x3E7, w.b);
                return true;
            }
            case CMSG_GUILD_BANK_DEPOSIT_MONEY: case CMSG_GUILD_BANK_WITHDRAW_MONEY:   // -> 0x3EC / 0x3ED: u64, u32
            {
                ObjectGuid g; uint64 dinero;
                if (op == CMSG_GUILD_BANK_DEPOSIT_MONEY) { WorldPackets::Guild::GuildBankDepositMoney q(Paquete(CMSG_GUILD_BANK_DEPOSIT_MONEY, b)); q.Read(); g = q.Banker; dinero = q.Money; }
                else { WorldPackets::Guild::GuildBankWithdrawMoney q(Paquete(CMSG_GUILD_BANK_WITHDRAW_MONEY, b)); q.Read(); g = q.Banker; dinero = q.Money; }
                Wr w; w.put<uint64>(mundo->A335(g)).put<uint32>(uint32(std::min<uint64>(dinero, 0xFFFFFFFF)));
                Srv(op == CMSG_GUILD_BANK_DEPOSIT_MONEY ? 0x3EC : 0x3ED, w.b);
                return true;
            }
            case CMSG_GUILD_BANK_BUY_TAB:                    // -> 0x3EA: u64, u8
            {
                WorldPackets::Guild::GuildBankBuyTab q(Paquete(CMSG_GUILD_BANK_BUY_TAB, b)); q.Read();
                Wr w; w.put<uint64>(mundo->A335(q.Banker)).put<uint8>(q.BankTab); Srv(0x3EA, w.b);
                return true;
            }
            case CMSG_GUILD_BANK_UPDATE_TAB:                 // -> 0x3EB: u64, u8, cstr, cstr
            {
                WorldPackets::Guild::GuildBankUpdateTab q(Paquete(CMSG_GUILD_BANK_UPDATE_TAB, b)); q.Read();
                Wr w; w.put<uint64>(mundo->A335(q.Banker)).put<uint8>(q.BankTab).cstr(std::string(q.Name)).cstr(std::string(q.Icon)); Srv(0x3EB, w.b);
                return true;
            }
            case CMSG_GUILD_BANK_LOG_QUERY:                  // -> 0x3EE u8
            {
                WorldPackets::Guild::GuildBankLogQuery q(Paquete(CMSG_GUILD_BANK_LOG_QUERY, b)); q.Read();
                Wr w; w.put<uint8>(uint8(q.Tab)); Srv(0x3EE, w.b);
                return true;
            }
            case CMSG_GUILD_BANK_TEXT_QUERY:                 // -> 0x40A u8
            {
                WorldPackets::Guild::GuildBankTextQuery q(Paquete(CMSG_GUILD_BANK_TEXT_QUERY, b)); q.Read();
                Wr w; w.put<uint8>(uint8(q.Tab)); Srv(0x40A, w.b);
                return true;
            }
            case CMSG_GUILD_BANK_SET_TAB_TEXT:               // -> 0x40B: u8, cstr
            {
                WorldPackets::Guild::GuildBankSetTabText q(Paquete(CMSG_GUILD_BANK_SET_TAB_TEXT, b)); q.Read();
                Wr w; w.put<uint8>(uint8(q.Tab)).cstr(std::string(q.TabText)); Srv(0x40B, w.b);
                return true;
            }
            case CMSG_GUILD_BANK_REMAINING_WITHDRAW_MONEY_QUERY: Srv(0x3FE, {}); return true;
            // movimientos de objetos
            case CMSG_MOVE_GUILD_BANK_ITEM:
            { WorldPackets::Guild::MoveGuildBankItem q(Paquete(CMSG_MOVE_GUILD_BANK_ITEM, b)); q.Read(); BancoHermandadEntreHuecos(q.Banker, q.BankTab, q.BankSlot, q.BankTab1, q.BankSlot1, 0); return true; }
            case CMSG_MERGE_GUILD_BANK_ITEM_WITH_GUILD_BANK_ITEM:
            { WorldPackets::Guild::MergeGuildBankItemWithGuildBankItem q(Paquete(CMSG_MERGE_GUILD_BANK_ITEM_WITH_GUILD_BANK_ITEM, b)); q.Read(); BancoHermandadEntreHuecos(q.Banker, q.BankTab, q.BankSlot, q.BankTab1, q.BankSlot1, q.StackCount); return true; }
            case CMSG_SPLIT_GUILD_BANK_ITEM:
            { WorldPackets::Guild::SplitGuildBankItem q(Paquete(CMSG_SPLIT_GUILD_BANK_ITEM, b)); q.Read(); BancoHermandadEntreHuecos(q.Banker, q.BankTab, q.BankSlot, q.BankTab1, q.BankSlot1, q.StackCount); return true; }
            case CMSG_SWAP_GUILD_BANK_ITEM_WITH_GUILD_BANK_ITEM:
            { WorldPackets::Guild::SwapGuildBankItemWithGuildBankItem q(Paquete(CMSG_SWAP_GUILD_BANK_ITEM_WITH_GUILD_BANK_ITEM, b)); q.Read(); BancoHermandadEntreHuecos(q.Banker, q.BankTab[0], q.BankSlot[0], q.BankTab[1], q.BankSlot[1], 0); return true; }
            case CMSG_AUTO_GUILD_BANK_ITEM:
            { WorldPackets::Guild::AutoGuildBankItem q(Paquete(CMSG_AUTO_GUILD_BANK_ITEM, b)); q.Read(); BancoHermandadConInventario(q.Banker, false, q.BankTab, q.BankSlot, q.ContainerSlot, q.ContainerItemSlot, 0); return true; }
            case CMSG_STORE_GUILD_BANK_ITEM:
            { WorldPackets::Guild::StoreGuildBankItem q(Paquete(CMSG_STORE_GUILD_BANK_ITEM, b)); q.Read(); BancoHermandadConInventario(q.Banker, true, q.BankTab, q.BankSlot, q.ContainerSlot, q.ContainerItemSlot, 0); return true; }
            case CMSG_SWAP_ITEM_WITH_GUILD_BANK_ITEM:
            { WorldPackets::Guild::SwapItemWithGuildBankItem q(Paquete(CMSG_SWAP_ITEM_WITH_GUILD_BANK_ITEM, b)); q.Read(); BancoHermandadConInventario(q.Banker, false, q.BankTab, q.BankSlot, q.ContainerSlot, q.ContainerItemSlot, 0); return true; }
            case CMSG_MERGE_ITEM_WITH_GUILD_BANK_ITEM:
            { WorldPackets::Guild::MergeItemWithGuildBankItem q(Paquete(CMSG_MERGE_ITEM_WITH_GUILD_BANK_ITEM, b)); q.Read(); BancoHermandadConInventario(q.Banker, false, q.BankTab, q.BankSlot, q.ContainerSlot, q.ContainerItemSlot, q.StackCount); return true; }
            case CMSG_SPLIT_ITEM_TO_GUILD_BANK:
            { WorldPackets::Guild::SplitItemToGuildBank q(Paquete(CMSG_SPLIT_ITEM_TO_GUILD_BANK, b)); q.Read(); BancoHermandadConInventario(q.Banker, false, q.BankTab, q.BankSlot, q.ContainerSlot, q.ContainerItemSlot, q.StackCount); return true; }
            case CMSG_MERGE_GUILD_BANK_ITEM_WITH_ITEM:
            { WorldPackets::Guild::MergeGuildBankItemWithItem q(Paquete(CMSG_MERGE_GUILD_BANK_ITEM_WITH_ITEM, b)); q.Read(); BancoHermandadConInventario(q.Banker, true, q.BankTab, q.BankSlot, q.ContainerSlot, q.ContainerItemSlot, q.StackCount); return true; }
            case CMSG_SPLIT_GUILD_BANK_ITEM_TO_INVENTORY:
            { WorldPackets::Guild::SplitGuildBankItemToInventory q(Paquete(CMSG_SPLIT_GUILD_BANK_ITEM_TO_INVENTORY, b)); q.Read(); BancoHermandadConInventario(q.Banker, true, q.BankTab, q.BankSlot, q.ContainerSlot, q.ContainerItemSlot, q.StackCount); return true; }
            case CMSG_AUTO_STORE_GUILD_BANK_ITEM:
            { WorldPackets::Guild::AutoStoreGuildBankItem q(Paquete(CMSG_AUTO_STORE_GUILD_BANK_ITEM, b)); q.Read(); BancoHermandadConInventario(q.Banker, true, q.BankTab, q.BankSlot, {}, 0, 0, true); return true; }
            case CMSG_ALTER_APPEARANCE:                      // -> 0x426: u32 peinado, u32 color, u32 vello, u32 piel
            {
                WorldPackets::Character::AlterApperance q(Paquete(CMSG_ALTER_APPEARANCE, b)); q.Read();
                uint8 raza = 0, clase = 0, sexo = 0;
                if (!mundo->RazaClaseSexo(yo335, raza, clase, sexo)) return true;
                std::vector<uint32> choices; for (auto const& c : q.Customizations) choices.push_back(c.ChrCustomizationChoiceID);
                uint8 v[5]; Apariencia::ToClassic(raza, sexo, choices, v);   // piel, cara, peinado, color de pelo, vello
                Wr w; w.put<uint32>(v[2]).put<uint32>(v[3]).put<uint32>(v[4]).put<uint32>(v[0]); Srv(0x426, w.b);
                return true;
            }
            // ---- cartas de hermandad
            case CMSG_PETITION_SHOW_LIST:                    // -> 0x1BB u64
            {
                WorldPackets::Petition::PetitionShowList q(Paquete(CMSG_PETITION_SHOW_LIST, b)); q.Read();
                Wr w; w.put<uint64>(mundo->A335(q.PetitionUnit)); Srv(0x1BB, w.b);
                return true;
            }
            case CMSG_PETITION_BUY:                          // -> 0x1BD: u64, u32, u64, cstr nombre, cstr, 7 x u32, u16, 3 x u32, 10 x cstr, u32 índice, u32
            {
                WorldPackets::Petition::PetitionBuy q(Paquete(CMSG_PETITION_BUY, b)); q.Read();
                Wr w; w.put<uint64>(mundo->A335(q.Unit)).put<uint32>(0).put<uint64>(0).cstr(q.Title).cstr("");
                for (int i = 0; i < 7; ++i) w.put<uint32>(0);
                w.put<uint16>(0);
                for (int i = 0; i < 3; ++i) w.put<uint32>(0);
                for (int i = 0; i < 10; ++i) w.cstr("");
                w.put<uint32>(1).put<uint32>(0);             // índice 1: carta de hermandad
                Srv(0x1BD, w.b);
                return true;
            }
            case CMSG_PETITION_SHOW_SIGNATURES:              // -> 0x1BE u64
            {
                WorldPackets::Petition::PetitionShowSignatures q(Paquete(CMSG_PETITION_SHOW_SIGNATURES, b)); q.Read();
                Wr w; w.put<uint64>(mundo->A335(q.Item)); Srv(0x1BE, w.b);
                return true;
            }
            case CMSG_QUERY_PETITION:                        // -> 0x1C6: u32 id, u64 carta
            {
                WorldPackets::Petition::QueryPetition q(Paquete(CMSG_QUERY_PETITION, b)); q.Read();
                Wr w; w.put<uint32>(q.PetitionID).put<uint64>(mundo->A335(q.ItemGUID)); Srv(0x1C6, w.b);
                return true;
            }
            case CMSG_SIGN_PETITION:                         // -> 0x1C0: u64, u8
            {
                WorldPackets::Petition::SignPetition q(Paquete(CMSG_SIGN_PETITION, b)); q.Read();
                Wr w; w.put<uint64>(mundo->A335(q.PetitionGUID)).put<uint8>(q.Choice); Srv(0x1C0, w.b);
                return true;
            }
            case CMSG_DECLINE_PETITION:                      // -> 0x1C2 u64
            {
                WorldPackets::Petition::DeclinePetition q(Paquete(CMSG_DECLINE_PETITION, b)); q.Read();
                Wr w; w.put<uint64>(mundo->A335(q.PetitionGUID)); Srv(0x1C2, w.b);
                return true;
            }
            case CMSG_OFFER_PETITION:                        // -> 0x1C3: u32, u64 carta, u64 jugador
            {
                WorldPackets::Petition::OfferPetition q(Paquete(CMSG_OFFER_PETITION, b)); q.Read();
                Wr w; w.put<uint32>(0).put<uint64>(mundo->A335(q.ItemGUID)).put<uint64>(mundo->A335(q.TargetPlayer)); Srv(0x1C3, w.b);
                return true;
            }
            case CMSG_TURN_IN_PETITION:                      // -> 0x1C4 u64
            {
                WorldPackets::Petition::TurnInPetition q(Paquete(CMSG_TURN_IN_PETITION, b)); q.Read();
                Wr w; w.put<uint64>(mundo->A335(q.Item)); Srv(0x1C4, w.b);
                return true;
            }
            case CMSG_PETITION_RENAME_GUILD:                 // -> 0x2C1: u64, cstr
            {
                WorldPackets::Petition::PetitionRenameGuild q(Paquete(CMSG_PETITION_RENAME_GUILD, b)); q.Read();
                Wr w; w.put<uint64>(mundo->A335(q.PetitionGuid)).cstr(q.NewGuildName); Srv(0x2C1, w.b);
                return true;
            }
            // ---- comandos de canal: cstr canal [, cstr jugador]
            case CMSG_CHAT_CHANNEL_LIST: case CMSG_CHAT_CHANNEL_DISPLAY_LIST: case CMSG_CHAT_CHANNEL_OWNER: case CMSG_CHAT_CHANNEL_ANNOUNCEMENTS:
            {
                WorldPackets::Channel::ChannelCommand q(Paquete(OpcodeClient(op), b)); q.Read();
                uint16 o = op == CMSG_CHAT_CHANNEL_OWNER ? 0x09E : op == CMSG_CHAT_CHANNEL_ANNOUNCEMENTS ? 0x0A7 : 0x09A;
                Wr w; w.cstr(q.ChannelName); Srv(o, w.b);
                return true;
            }
            case CMSG_CHAT_CHANNEL_SET_OWNER: case CMSG_CHAT_CHANNEL_MODERATOR: case CMSG_CHAT_CHANNEL_UNMODERATOR:
            case CMSG_CHAT_CHANNEL_INVITE: case CMSG_CHAT_CHANNEL_KICK: case CMSG_CHAT_CHANNEL_BAN: case CMSG_CHAT_CHANNEL_UNBAN:
            case CMSG_CHAT_CHANNEL_SILENCE_ALL: case CMSG_CHAT_CHANNEL_UNSILENCE_ALL:
            {
                WorldPackets::Channel::ChannelPlayerCommand q(Paquete(OpcodeClient(op), b)); q.Read();
                uint16 o = op == CMSG_CHAT_CHANNEL_SET_OWNER ? 0x09D : op == CMSG_CHAT_CHANNEL_MODERATOR ? 0x09F : op == CMSG_CHAT_CHANNEL_UNMODERATOR ? 0x0A0
                         : op == CMSG_CHAT_CHANNEL_INVITE ? 0x0A3 : op == CMSG_CHAT_CHANNEL_KICK ? 0x0A4 : op == CMSG_CHAT_CHANNEL_BAN ? 0x0A5
                         : op == CMSG_CHAT_CHANNEL_UNBAN ? 0x0A6 : op == CMSG_CHAT_CHANNEL_SILENCE_ALL ? 0x3CD : 0x3CF;
                Wr w; w.cstr(q.ChannelName).cstr(q.Name.substr(0, q.Name.find('-'))); Srv(o, w.b);
                return true;
            }
            case CMSG_CHAT_CHANNEL_PASSWORD:                 // -> 0x09C: cstr, cstr
            {
                WorldPackets::Channel::ChannelPassword q(Paquete(CMSG_CHAT_CHANNEL_PASSWORD, b)); q.Read();
                Wr w; w.cstr(q.ChannelName).cstr(q.Password); Srv(0x09C, w.b);
                return true;
            }
            // ---- buscador de mazmorras
            case CMSG_DF_JOIN:                               // -> 0x35C: u32 roles, u8, u8, u8 n, u32*, u8 3, 3 x u8, cstr
            {
                WorldPackets::LFG::DFJoin q(Paquete(CMSG_DF_JOIN, b)); q.Read();
                Wr w; w.put<uint32>(q.Roles).put<uint8>(0).put<uint8>(0).put<uint8>(uint8(q.Slots.size()));
                for (uint32 sl : q.Slots) w.put<uint32>(sl);
                w.put<uint8>(3).put<uint8>(0).put<uint8>(0).put<uint8>(0).cstr("");
                Srv(0x35C, w.b);
                return true;
            }
            case CMSG_DF_LEAVE: Srv(0x35D, {}); return true;
            case CMSG_DF_PROPOSAL_RESPONSE:                  // -> 0x362: u32 propuesta, u8
            {
                WorldPackets::LFG::DFProposalResponse q(Paquete(CMSG_DF_PROPOSAL_RESPONSE, b)); q.Read();
                Wr w; w.put<uint32>(q.ProposalID).put<uint8>(q.Accepted ? 1 : 0); Srv(0x362, w.b);
                return true;
            }
            case CMSG_DF_SET_ROLES:                          // -> 0x36A u8
            {
                WorldPackets::LFG::DFSetRoles q(Paquete(CMSG_DF_SET_ROLES, b)); q.Read();
                Wr w; w.put<uint8>(q.RolesDesired); Srv(0x36A, w.b);
                return true;
            }
            case CMSG_DF_BOOT_PLAYER_VOTE:                   // -> 0x36C u8
            {
                WorldPackets::LFG::DFBootPlayerVote q(Paquete(CMSG_DF_BOOT_PLAYER_VOTE, b)); q.Read();
                Wr w; w.put<uint8>(q.Vote ? 1 : 0); Srv(0x36C, w.b);
                return true;
            }
            case CMSG_DF_TELEPORT:                           // -> 0x370 u8 salir
            {
                WorldPackets::LFG::DFTeleport q(Paquete(CMSG_DF_TELEPORT, b)); q.Read();
                Wr w; w.put<uint8>(q.TeleportOut ? 1 : 0); Srv(0x370, w.b);
                return true;
            }
            case CMSG_DF_GET_SYSTEM_INFO:                    // -> 0x36E (jugador) / 0x371 (grupo)
            {
                WorldPackets::LFG::DFGetSystemInfo q(Paquete(CMSG_DF_GET_SYSTEM_INFO, b)); q.Read();
                Srv(q.Player ? 0x36E : 0x371, {});
                return true;
            }
            case CMSG_DF_GET_JOIN_STATUS: Srv(0x296, {}); return true;
            // ---- campos de batalla
            case CMSG_BATTLEMASTER_HELLO:                    // -> 0x2D7 u64
            {
                WorldPackets::NPC::Hello q(Paquete(CMSG_BATTLEMASTER_HELLO, b)); q.Read();
                _maestroBatalla = mundo->A335(q.Unit);
                Wr w; w.put<uint64>(_maestroBatalla); Srv(0x2D7, w.b);
                return true;
            }
            case CMSG_BATTLEFIELD_LIST:                      // -> 0x23C: u32 tipo, u8 desde, u8
            {
                WorldPackets::Battleground::BattlefieldListRequest q(Paquete(CMSG_BATTLEFIELD_LIST, b)); q.Read();
                if (EsListaConquista(uint32(q.ListID)))          // AC no tiene la lista 1089 y no contesta: sin lista el cliente no deja unirse
                {
                    WorldPackets::Battleground::BattlefieldList p;
                    p.BattlemasterListID = q.ListID; p.PvpAnywhere = true;
                    BattlemasterListEntry const* e = sBattlemasterListStore.LookupEntry(uint32(q.ListID));
                    p.MinLevel = e ? e->MinLevel : 71; p.MaxLevel = e ? e->MaxLevel : 80;
                    Enviar(p.Write());
                    return true;
                }
                Wr w; w.put<uint32>(uint32(q.ListID)).put<uint8>(0).put<uint8>(0); Srv(0x23C, w.b);
                return true;
            }
            case CMSG_BATTLEMASTER_JOIN:                     // -> 0x2EE: u64 maestro, u32 tipo, u32 instancia, u8 en grupo
            {
                WorldPackets::Battleground::BattlemasterJoin q(Paquete(CMSG_BATTLEMASTER_JOIN, b)); q.Read();
                if (q.QueueIDs.empty()) return true;
                BattlegroundQueueTypeId c = BattlegroundQueueTypeId::FromPacked(q.QueueIDs[0]);
                if (c.Type == 2 || EsListaConquista(c.BattlemasterListId))
                {
                    // AC no atiende CMSG_BATTLEFIELD_MGR_QUEUE_REQUEST (0x4E3 = Handle_NULL/STATUS_NEVER): la cola de Conquista del
                    // Invierno la abre el propio AC (Battlefield::InvitePlayerToQueue) a quien está en la zona 15 min antes o
                    // durante la batalla, o el maestro de batalla (npc_wg_queue). Los GM (.gm on) no reciben invitación.
                    Sistema("Conquista del Invierno: la cola se abre 15 minutos antes de la batalla. Habla con el maestro de batalla "
                            "de Conquista del Invierno (Dalaran o tu capital) o espera en la zona; con .gm on no te invitan.");
                    return true;
                }
                Wr w; w.put<uint64>(_maestroBatalla).put<uint32>(c.BattlemasterListId).put<uint32>(0).put<uint8>(0);
                Srv(0x2EE, w.b);
                return true;
            }
            case CMSG_BATTLEFIELD_PORT:                      // -> 0x2D5: u8 arena, u8, u32 tipo, u16, u8 aceptar
            {
                WorldPackets::Battleground::BattlefieldPort q(Paquete(CMSG_BATTLEFIELD_PORT, b)); q.Read();
                if (q.Ticket.Id & kTicketBatalla)            // Conquista del Invierno -> ENTRY_INVITE_RESPONSE / EXIT_REQUEST
                {
                    uint32 id = q.Ticket.Id & ~kTicketBatalla;
                    Wr w; w.put<uint32>(id);
                    if (q.AcceptedInvite) { w.put<uint8>(1); Srv(0x4DF, w.b); }
                    else Srv(0x4E7, w.b);
                    return true;
                }
                ColaBg c;
                { std::lock_guard<std::mutex> l(_mBg); auto it = _colasBg.find(q.Ticket.Id); if (it != _colasBg.end()) c = it->second; }
                Wr w; w.put<uint8>(c.arena).put<uint8>(0).put<uint32>(c.tipo).put<uint16>(0x1F90).put<uint8>(q.AcceptedInvite ? 1 : 0);
                Srv(0x2D5, w.b);
                return true;
            }
            case CMSG_BATTLEFIELD_LEAVE:                     // -> 0x2E1: u8, u8, u32, u16
            {
                Wr w; w.put<uint8>(0).put<uint8>(0).put<uint32>(0).put<uint16>(0); Srv(0x2E1, w.b);
                return true;
            }
            case CMSG_PVP_LOG_DATA: Srv(0x2E0, {}); return true;
            case CMSG_REQUEST_BATTLEFIELD_STATUS: Srv(0x2D3, {}); return true;
            // ---- hermandad
            case CMSG_GUILD_PERMISSIONS_QUERY: Srv(0x3FD, {}); return true;
            case CMSG_GUILD_EVENT_LOG_QUERY: Srv(0x3FF, {}); return true;
            case CMSG_GUILD_SET_RANK_PERMISSIONS:            // -> 0x231: u32 rango, u32 derechos, cstr nombre, u32 oro/día, 6 x (u32, u32)
            {
                WorldPackets::Guild::GuildSetRankPermissions q(Paquete(CMSG_GUILD_SET_RANK_PERMISSIONS, b)); q.Read();
                Wr w; w.put<uint32>(q.RankID).put<uint32>(q.Flags).cstr(std::string(q.RankName)).put<uint32>(q.WithdrawGoldLimit);
                for (int i = 0; i < 6; ++i) w.put<uint32>(q.TabFlags[i]).put<uint32>(q.TabWithdrawItemLimit[i]);
                Srv(0x231, w.b);
                return true;
            }
            case CMSG_GUILD_ADD_RANK:                        // -> 0x232 cstr
            {
                WorldPackets::Guild::GuildAddRank q(Paquete(CMSG_GUILD_ADD_RANK, b)); q.Read();
                Wr w; w.cstr(std::string(q.Name)); Srv(0x232, w.b);
                return true;
            }
            case CMSG_GUILD_DELETE_RANK: Srv(0x233, {}); return true;   // el 3.3.5 borra siempre el último
            // ---- instancias
            case CMSG_SET_DUNGEON_DIFFICULTY:                // -> 0x329 u32 (3.4.3: 1 normal, 2 heroica)
            {
                WorldPackets::Misc::SetDungeonDifficulty q(Paquete(CMSG_SET_DUNGEON_DIFFICULTY, b)); q.Read();
                Wr w; w.put<uint32>(q.DifficultyID >= 2 ? 1 : 0); Srv(0x329, w.b);
                // sin grupo AC cambia la dificultad y no contesta (el 3.3.5 se lo apuntaba solo); el 3.4.3 solo actualiza el
                // menú con SMSG_SET_DUNGEON_DIFFICULTY. Si AC lo rechaza, su 0x329 llega después y deja el valor bueno
                if (!_enGrupo) { WorldPackets::Misc::DungeonDifficultySet p; p.DifficultyID = q.DifficultyID >= 2 ? 2 : 1; Enviar(p.Write()); }
                return true;
            }
            case CMSG_SET_RAID_DIFFICULTY:                   // -> 0x4EB u32 (3.4.3: 3 10N, 4 25N, 5 10H, 6 25H)
            {
                WorldPackets::Misc::SetRaidDifficulty q(Paquete(CMSG_SET_RAID_DIFFICULTY, b)); q.Read();
                uint32 d = q.DifficultyID >= 3 ? uint32(q.DifficultyID - 3) : 0;
                Wr w; w.put<uint32>(d); Srv(0x4EB, w.b);
                if (!_enGrupo) { WorldPackets::Misc::RaidDifficultySet p; p.DifficultyID = int32(d + 3); p.Legacy = 0; Enviar(p.Write()); }
                return true;
            }
            case CMSG_RESET_INSTANCES: Srv(0x31D, {}); return true;
            case CMSG_INSTANCE_LOCK_RESPONSE:                // -> 0x13F u8
            {
                WorldPackets::Instance::InstanceLockResponse q(Paquete(CMSG_INSTANCE_LOCK_RESPONSE, b)); q.Read();
                Wr w; w.put<uint8>(q.AcceptLock ? 1 : 0); Srv(0x13F, w.b);
                return true;
            }
            // ---- marcas de banda
            case CMSG_UPDATE_RAID_TARGET:                    // -> 0x321: u8 marca, u64 objetivo
            {
                WorldPackets::Party::UpdateRaidTarget q(Paquete(CMSG_UPDATE_RAID_TARGET, b)); q.Read();
                Wr w; w.put<uint8>(uint8(q.Symbol)).put<uint64>(mundo->A335(q.Target)); Srv(0x321, w.b);
                return true;
            }
            // ---- cinemáticas y tutoriales
            case CMSG_NEXT_CINEMATIC_CAMERA: Srv(0x0FB, {}); return true;
            case CMSG_COMPLETE_CINEMATIC: Srv(0x0FC, {}); return true;
            case CMSG_TUTORIAL:                              // -> 0x0FE u32 bit / 0x0FF borrar / 0x100 reiniciar
            {
                WorldPackets::Misc::TutorialSetFlag q(Paquete(CMSG_TUTORIAL, b)); q.Read();
                if (q.Action == 0) { Wr w; w.put<uint32>(q.TutorialBit); Srv(0x0FE, w.b); }
                else Srv(q.Action == 1 ? 0x0FF : 0x100, {});
                return true;
            }
            // ---- clic en vehículos y PNJ con hechizo
            case CMSG_SPELL_CLICK:                           // -> 0x3F8 u64
            {
                WorldPackets::Spells::SpellClick q(Paquete(CMSG_SPELL_CLICK, b)); q.Read();
                Wr w; w.put<uint64>(mundo->A335(q.SpellClickUnitGuid)); Srv(0x3F8, w.b);
                return true;
            }
            // ---- talentos, mascotas
            case CMSG_REMOVE_GLYPH:                          // -> 0x48A u32
            {
                WorldPackets::Talent::RemoveGlyph q(Paquete(CMSG_REMOVE_GLYPH, b)); q.Read();
                Wr w; w.put<uint32>(q.GlyphSlot); Srv(0x48A, w.b);
                return true;
            }
            case CMSG_PET_SPELL_AUTOCAST:                    // -> 0x2F3: u64, u32, u8
            {
                WorldPackets::Pet::PetSpellAutocast q(Paquete(CMSG_PET_SPELL_AUTOCAST, b)); q.Read();
                Wr w; w.put<uint64>(mundo->A335(q.PetGUID)).put<uint32>(q.SpellID).put<uint8>(q.AutocastEnabled ? 1 : 0); Srv(0x2F3, w.b);
                return true;
            }
            case CMSG_PET_CANCEL_AURA:                       // -> 0x26B: u64, u32
            {
                WorldPackets::Pet::PetCancelAura q(Paquete(CMSG_PET_CANCEL_AURA, b)); q.Read();
                Wr w; w.put<uint64>(mundo->A335(q.PetGUID)).put<uint32>(uint32(q.SpellID)); Srv(0x26B, w.b);
                return true;
            }
            // ---- social y grupo
            case CMSG_SET_CONTACT_NOTES:                     // -> 0x06B: u64, cstr
            {
                WorldPackets::Social::SetContactNotes q(Paquete(CMSG_SET_CONTACT_NOTES, b)); q.Read();
                Wr w; w.put<uint64>(mundo->A335(q.Player.Guid)).cstr(q.Notes); Srv(0x06B, w.b);
                return true;
            }
            case CMSG_CHANGE_SUB_GROUP:                      // -> 0x27E: cstr nombre, u8 grupo
            {
                WorldPackets::Party::ChangeSubGroup q(Paquete(CMSG_CHANGE_SUB_GROUP, b)); q.Read();
                Wr w; w.cstr(NombreDe(mundo->A335(q.TargetGUID))).put<uint8>(q.NewSubGroup); Srv(0x27E, w.b);
                return true;
            }
            case CMSG_SWAP_SUB_GROUPS:                       // -> 0x280: cstr, cstr
            {
                WorldPackets::Party::SwapSubGroups q(Paquete(CMSG_SWAP_SUB_GROUPS, b)); q.Read();
                Wr w; w.cstr(NombreDe(mundo->A335(q.FirstTarget))).cstr(NombreDe(mundo->A335(q.SecondTarget))); Srv(0x280, w.b);
                return true;
            }
            case CMSG_QUEST_PUSH_RESULT:                     // -> 0x276: u64, u32 misión, u8
            {
                WorldPackets::Quest::QuestPushResult q(Paquete(CMSG_QUEST_PUSH_RESULT, b)); q.Read();
                Wr w; w.put<uint64>(mundo->A335(q.SenderGUID)).put<uint32>(q.QuestID).put<uint8>(q.Result); Srv(0x276, w.b);
                return true;
            }
            case CMSG_FAR_SIGHT:                             // -> 0x27A u8
            {
                WorldPackets::Misc::FarSight q(Paquete(CMSG_FAR_SIGHT, b)); q.Read();
                Wr w; w.put<uint8>(q.Enable ? 1 : 0); Srv(0x27A, w.b);
                return true;
            }
            case CMSG_CAN_DUEL:                              // el 3.3.5 no pregunta: se contesta que sí y el cliente lanza Duelo (7266)
            {
                WorldPackets::Duel::CanDuel q(Paquete(CMSG_CAN_DUEL, b)); q.Read();
                WorldPackets::Duel::CanDuelResult p; p.TargetGUID = q.TargetGUID; p.Result = true;
                Enviar(p.Write());
                return true;
            }
            case CMSG_QUERY_REALM_NAME:                      // se contesta aquí: un solo reino
            {
                WorldPackets::Query::QueryRealmName q(Paquete(CMSG_QUERY_REALM_NAME, b)); q.Read();
                std::string nom = Cfg::RealmName, norm = nom; norm.erase(std::remove(norm.begin(), norm.end(), ' '), norm.end());
                WorldPackets::Query::RealmQueryResponse p;
                p.VirtualRealmAddress = q.VirtualRealmAddress;
                p.LookupState = q.VirtualRealmAddress == realmAddress ? 0 : 1;
                p.NameInfo = WorldPackets::Auth::VirtualRealmNameInfo(true, false, nom, norm);
                Enviar(p.Write());
                return true;
            }
            case CMSG_OBJECT_UPDATE_FAILED:                  // el cliente no pudo crear un objeto: se anota para depurar
            {
                WorldPackets::Misc::ObjectUpdateFailed q(Paquete(CMSG_OBJECT_UPDATE_FAILED, b)); q.Read();
                Log("[%s] el cliente no pudo crear %s (3.3.5 0x%llX)", acct.c_str(), q.ObjectGUID.ToString().c_str(), (unsigned long long)mundo->A335(q.ObjectGUID));
                return true;
            }
            case CMSG_CLOSE_INTERACTION: case CMSG_SAVE_CUF_PROFILES: case CMSG_QUEST_GIVER_STATUS_TRACKED_QUERY: case CMSG_REQUEST_PARTY_JOIN_UPDATES:
            case CMSG_UPDATE_AADC_STATUS:
            case CMSG_CANCEL_QUEUED_SPELL: case CMSG_SET_TAXI_BENCHMARK_MODE: case CMSG_QUERY_QUEST_COMPLETION_NPCS:
            case CMSG_KEYBOUND_OVERRIDE: case CMSG_SET_ADVANCED_COMBAT_LOGGING: case CMSG_REMOVE_NEW_ITEM:
                return true;
            // ---- subastas
            case CMSG_AUCTION_HELLO_REQUEST:                 // se contesta aquí con la casa que dio MSG_AUCTION_HELLO
            {
                WorldPacket rq = Paquete(CMSG_AUCTION_HELLO_REQUEST, b);
                ObjectGuid g; rq >> g;
                uint32 casa; { std::lock_guard<std::mutex> l(_mSub); casa = _casaSubastas; }
                WorldPacket w(SMSG_AUCTION_HELLO_RESPONSE, 32);
                w << g << uint32(casa) << uint32(3600) << uint32(0);
                w.WriteBit(true); w.FlushBits();
                Enviar(&w);
                return true;
            }
            case CMSG_AUCTION_LIST_ITEMS:                    // -> 0x258
            {
                WorldPacket rq = Paquete(CMSG_AUCTION_LIST_ITEMS, b);
                ObjectGuid g; uint32 desde; uint8 nmin, nmax; uint32 calidad;
                rq >> g >> desde >> nmin >> nmax >> calidad;
                uint32 nOrden = rq.ReadBits(2);
                uint32 nMascotas = rq.read<uint32>(); rq.read<int8>();
                rq.read_skip(nMascotas);
                bool manchado = rq.ReadBit();
                uint32 largo = rq.ReadBits(8);
                std::string nombre = rq.ReadString(largo);
                uint32 nClases = rq.ReadBits(3);
                bool usable = rq.ReadBit(); rq.ReadBit();
                rq.ResetBitPos();
                for (uint32 i = 0; i < nOrden; ++i) { rq.ResetBitPos(); rq.ReadBits(5); }
                if (manchado) { WorldPackets::Addon::AddOnInfo ai; rq >> ai; }       // llamada desde un accesorio
                uint32 clase = 0xFFFFFFFF, subclase = 0xFFFFFFFF, hueco = 0xFFFFFFFF;
                for (uint32 i = 0; i < nClases; ++i)
                {
                    int32 c = rq.read<int32>();
                    uint32 nSub = rq.ReadBits(5);
                    rq.ResetBitPos();
                    std::vector<std::pair<uint64, int32>> subs;
                    for (uint32 k = 0; k < nSub; ++k) { uint64 inv = rq.read<uint64>(); int32 sc = rq.read<int32>(); subs.emplace_back(inv, sc); }
                    if (nClases == 1)
                    {
                        clase = uint32(c);
                        if (subs.size() == 1)
                        {
                            subclase = uint32(subs[0].second);
                            uint64 inv = subs[0].first;
                            if (inv && !(inv & (inv - 1))) { hueco = 0; while (!(inv & 1)) { inv >>= 1; ++hueco; } }
                        }
                    }
                }
                Wr w; w.put<uint64>(mundo->A335(g)).put<uint32>(desde).cstr(nombre).put<uint8>(nmin).put<uint8>(nmax)
                         .put<uint32>(hueco).put<uint32>(clase).put<uint32>(subclase).put<uint32>(calidad)
                         .put<uint8>(usable ? 1 : 0).put<uint8>(0).put<uint8>(0);
                Srv(0x258, w.b);
                return true;
            }
            case CMSG_AUCTION_LIST_OWNER_ITEMS: case CMSG_AUCTION_LIST_BIDDER_ITEMS:   // -> 0x259 / 0x264: u64, u32 desde [, u32 0]
            {
                WorldPacket rq = Paquete(OpcodeClient(op), b);
                ObjectGuid g; uint32 desde; rq >> g >> desde;
                Wr w; w.put<uint64>(mundo->A335(g)).put<uint32>(desde);
                if (op == CMSG_AUCTION_LIST_BIDDER_ITEMS) w.put<uint32>(0);
                Srv(op == CMSG_AUCTION_LIST_OWNER_ITEMS ? 0x259 : 0x264, w.b);
                return true;
            }
            case CMSG_AUCTION_PLACE_BID:                     // -> 0x25A: u64, u32 subasta, u32 precio
            {
                WorldPacket rq = Paquete(CMSG_AUCTION_PLACE_BID, b);
                ObjectGuid g; int32 id; uint64 puja; rq >> g >> id >> puja;
                Wr w; w.put<uint64>(mundo->A335(g)).put<uint32>(uint32(id)).put<uint32>(uint32(std::min<uint64>(puja, 0xFFFFFFFF)));
                Srv(0x25A, w.b);
                return true;
            }
            case CMSG_AUCTION_REMOVE_ITEM:                   // -> 0x257: u64, u32 subasta
            {
                WorldPacket rq = Paquete(CMSG_AUCTION_REMOVE_ITEM, b);
                ObjectGuid g; int32 id; rq >> g >> id;
                Wr w; w.put<uint64>(mundo->A335(g)).put<uint32>(uint32(id));
                Srv(0x257, w.b);
                return true;
            }
            case CMSG_AUCTION_SELL_ITEM:                     // -> 0x256: u64, u32 1, (u64 objeto, u32 cant), u32 puja, u32 compra, u32 minutos
            {
                WorldPacket rq = Paquete(CMSG_AUCTION_SELL_ITEM, b);
                ObjectGuid g; uint64 puja, compra; uint32 minutos;
                rq >> g >> puja >> compra >> minutos;
                bool manchado = rq.ReadBit(); rq.ReadBits(6);
                rq.ResetBitPos();
                if (manchado) { WorldPackets::Addon::AddOnInfo ai; rq >> ai; }
                ObjectGuid obj; uint32 cant; rq >> obj >> cant;
                Wr w; w.put<uint64>(mundo->A335(g)).put<uint32>(1).put<uint64>(mundo->A335(obj)).put<uint32>(cant)
                         .put<uint32>(uint32(std::min<uint64>(puja, 0xFFFFFFFF))).put<uint32>(uint32(std::min<uint64>(compra, 0xFFFFFFFF))).put<uint32>(minutos);
                Srv(0x256, w.b);
                return true;
            }
            case CMSG_AUCTION_LIST_PENDING_SALES:            // el 3.3.5 de AzerothCore siempre la manda vacía
            {
                WorldPacket w(SMSG_AUCTION_LIST_PENDING_SALES_RESULT, 8);
                w << int32(0) << int32(0);
                Enviar(&w);
                return true;
            }
            case CMSG_AUCTION_REPLICATE_ITEMS:
                return true;
            case CMSG_INSPECT:                               // -> 0x114 u64
            {
                WorldPackets::Inspect::Inspect q(Paquete(CMSG_INSPECT, b)); q.Read();
                Wr w; w.put<uint64>(mundo->A335(q.Target)); Srv(0x114, w.b);
                return true;
            }
            // ---- comercio
            case CMSG_INITIATE_TRADE:                        // -> 0x116 u64
            {
                WorldPackets::Trade::InitiateTrade q(Paquete(CMSG_INITIATE_TRADE, b)); q.Read();
                Wr w; w.put<uint64>(mundo->A335(q.Guid)); Srv(0x116, w.b);
                return true;
            }
            case CMSG_BEGIN_TRADE: Srv(0x117, {}); return true;
            case CMSG_BUSY_TRADE: Srv(0x118, {}); return true;
            case CMSG_IGNORE_TRADE: Srv(0x119, {}); return true;
            case CMSG_ACCEPT_TRADE: { Wr w; w.put<uint32>(0); Srv(0x11A, w.b); return true; }
            case CMSG_UNACCEPT_TRADE: Srv(0x11B, {}); return true;
            case CMSG_CANCEL_TRADE: Srv(0x11C, {}); return true;
            case CMSG_SET_TRADE_ITEM:                        // -> 0x11D: u8 hueco de comercio, u8 bolsa, u8 hueco
            {
                WorldPackets::Trade::SetTradeItem q(Paquete(CMSG_SET_TRADE_ITEM, b)); q.Read();
                Wr w; w.put<uint8>(q.TradeSlot).put<uint8>(Bolsa(q.PackSlot)).put<uint8>(Hueco(q.PackSlot, q.ItemSlotInPack)); Srv(0x11D, w.b);
                return true;
            }
            case CMSG_CLEAR_TRADE_ITEM:                      // -> 0x11E u8
            {
                WorldPackets::Trade::ClearTradeItem q(Paquete(CMSG_CLEAR_TRADE_ITEM, b)); q.Read();
                Wr w; w.put<uint8>(q.TradeSlot); Srv(0x11E, w.b);
                return true;
            }
            case CMSG_SET_TRADE_GOLD:                        // -> 0x11F u32
            {
                WorldPackets::Trade::SetTradeGold q(Paquete(CMSG_SET_TRADE_GOLD, b)); q.Read();
                Wr w; w.put<uint32>(uint32(std::min<uint64>(q.Coinage, 0xFFFFFFFF))); Srv(0x11F, w.b);
                return true;
            }
            // ---- banco
            case CMSG_BUY_BANK_SLOT:                         // -> 0x1B9 u64
            {
                WorldPackets::Bank::BuyBankSlot q(Paquete(CMSG_BUY_BANK_SLOT, b)); q.Read();
                Wr w; w.put<uint64>(mundo->A335(q.Guid)); Srv(0x1B9, w.b);
                return true;
            }
            case CMSG_AUTOBANK_ITEM: case CMSG_AUTOSTORE_BANK_ITEM:   // -> 0x283 / 0x282: u8 bolsa, u8 hueco
            {
                uint8 bo, hu;
                if (op == CMSG_AUTOBANK_ITEM) { WorldPackets::Bank::AutoBankItem q(Paquete(CMSG_AUTOBANK_ITEM, b)); q.Read(); bo = q.Bag; hu = q.Slot; }
                else { WorldPackets::Bank::AutoStoreBankItem q(Paquete(CMSG_AUTOSTORE_BANK_ITEM, b)); q.Read(); bo = q.Bag; hu = q.Slot; }
                Wr w; w.put<uint8>(Bolsa(bo)).put<uint8>(Hueco(bo, hu)); Srv(op == CMSG_AUTOBANK_ITEM ? 0x283 : 0x282, w.b);
                return true;
            }
            // ---- consultas
            case CMSG_QUEST_POI_QUERY:                       // -> 0x1E3: u32 n, u32*
            {
                // el 3.4.3 manda i32 n + 25 huecos (104 bytes); QuestPOIQuery::Read de TrinityCore lee 175 (retail) y se sale
                Rd q(b);
                int32 n = std::max(0, std::min<int32>(q.get<int32>(), 25));
                n = std::min<int32>(n, int32((b.size() - 4) / 4));
                Wr w; w.put<uint32>(uint32(n));
                for (int32 i = 0; i < n; ++i) w.put<uint32>(q.get<uint32>());
                Srv(0x1E3, w.b);
                return true;
            }
            case CMSG_ITEM_TEXT_QUERY:                       // -> 0x243 u64
            {
                WorldPackets::Query::ItemTextQuery q(Paquete(CMSG_ITEM_TEXT_QUERY, b)); q.Read();
                Wr w; w.put<uint64>(mundo->A335(q.Id)); Srv(0x243, w.b);
                return true;
            }
            case CMSG_WHO:                                   // -> 0x062
            {
                WorldPackets::Who::WhoRequestPkt q(Paquete(CMSG_WHO, b)); q.Read();
                auto const& rq = q.Request;
                _whoPeticion = q.RequestID;
                uint32 razas = uint32(rq.RaceFilter.RawValue); if (!razas) razas = 0xFFFFFFFF;
                uint32 clases = rq.ClassFilter == -1 || rq.ClassFilter == 0 ? 0xFFFFFFFF : uint32(rq.ClassFilter);
                Wr w; w.put<uint32>(uint32(std::max(rq.MinLevel, 0))).put<uint32>(uint32(rq.MaxLevel > 0 ? rq.MaxLevel : 100))
                         .cstr(rq.Name).cstr(rq.Guild).put<uint32>(razas).put<uint32>(clases);
                std::vector<int32> zonas;
                for (int32 a : q.Areas) if (a) zonas.push_back(a);
                w.put<uint32>(uint32(zonas.size()));
                for (int32 a : zonas) w.put<uint32>(uint32(a));
                uint32 nw = uint32(std::min<size_t>(rq.Words.size(), 4));
                w.put<uint32>(nw);
                for (uint32 i = 0; i < nw; ++i) w.cstr(rq.Words[i].Word);
                Srv(0x062, w.b);
                return true;
            }
            // ---- grupo
            case CMSG_DO_READY_CHECK: Srv(0x322, {}); return true;
            case CMSG_READY_CHECK_RESPONSE:                  // -> 0x322 u8
            {
                WorldPackets::Party::ReadyCheckResponseClient q(Paquete(CMSG_READY_CHECK_RESPONSE, b)); q.Read();
                Wr w; w.put<uint8>(q.IsReady ? 1 : 0); Srv(0x322, w.b);
                return true;
            }
            case CMSG_MASTER_LOOT_ITEM:                      // -> 0x2A3 por objeto: u64 botín, u8 hueco, u64 destinatario
            {
                WorldPackets::Loot::MasterLootItem q(Paquete(CMSG_MASTER_LOOT_ITEM, b)); q.Read();
                uint64 dest = mundo->A335(q.Target);
                for (auto const& lr : q.Loot)
                {
                    Wr w; w.put<uint64>(TiradaSrc(lr.Object)).put<uint8>(lr.LootListID).put<uint64>(dest); Srv(0x2A3, w.b);
                }
                return true;
            }
            // ---- varios
            case CMSG_REQUEST_VEHICLE_EXIT: Srv(0x476, {}); return true;
            case CMSG_SELF_RES: Srv(0x2B3, {}); return true;
            case CMSG_AREA_SPIRIT_HEALER_QUERY: case CMSG_AREA_SPIRIT_HEALER_QUEUE:   // -> 0x2E2 / 0x2E3 u64
            {
                ObjectGuid g;
                if (op == CMSG_AREA_SPIRIT_HEALER_QUERY) { WorldPackets::Battleground::AreaSpiritHealerQuery q(Paquete(CMSG_AREA_SPIRIT_HEALER_QUERY, b)); q.Read(); g = q.HealerGuid; }
                else { WorldPackets::Battleground::AreaSpiritHealerQueue q(Paquete(CMSG_AREA_SPIRIT_HEALER_QUEUE, b)); q.Read(); g = q.HealerGuid; }
                Wr w; w.put<uint64>(mundo->A335(g)); Srv(op == CMSG_AREA_SPIRIT_HEALER_QUERY ? 0x2E2 : 0x2E3, w.b);
                return true;
            }
            case CMSG_CONFIRM_RESPEC_WIPE:                   // -> MSG_TALENT_WIPE_CONFIRM 0x2AA u64
            {
                WorldPackets::Talent::ConfirmRespecWipe q(Paquete(CMSG_CONFIRM_RESPEC_WIPE, b)); q.Read();
                Wr w; w.put<uint64>(mundo->A335(q.RespecMaster)); Srv(0x2AA, w.b);
                return true;
            }
            case CMSG_DISMISS_CRITTER:                       // -> 0x48D u64
            {
                WorldPackets::Pet::DismissCritter q(Paquete(CMSG_DISMISS_CRITTER, b)); q.Read();
                Wr w; w.put<uint64>(mundo->A335(q.CritterGUID)); Srv(0x48D, w.b);
                return true;
            }
            case CMSG_PET_RENAME:                            // -> 0x177: u64, cstr, u8 declinados
            {
                WorldPackets::Pet::PetRename q(Paquete(CMSG_PET_RENAME, b)); q.Read();
                Wr w; w.put<uint64>(mundo->A335(q.RenameData.PetGUID)).cstr(q.RenameData.NewName).put<uint8>(0); Srv(0x177, w.b);
                return true;
            }
            case CMSG_SOCKET_GEMS:                           // -> 0x347: u64 objeto, 3 x u64 gema
            {
                WorldPackets::Item::SocketGems q(Paquete(CMSG_SOCKET_GEMS, b)); q.Read();
                Wr w; w.put<uint64>(mundo->A335(q.ItemGuid));
                for (uint32 i = 0; i < 3; ++i) w.put<uint64>(i < q.GemItem.size() ? mundo->A335(q.GemItem[i]) : 0);
                Srv(0x347, w.b);
                return true;
            }
            case CMSG_WRAP_ITEM:                             // -> 0x1D3: u8 bolsa papel, u8 hueco papel, u8 bolsa objeto, u8 hueco objeto
            {
                WorldPackets::Item::WrapItem q(Paquete(CMSG_WRAP_ITEM, b)); q.Read();
                if (q.Inv.Items.size() != 2) return true;
                auto const& g = q.Inv.Items[0]; auto const& o = q.Inv.Items[1];
                Wr w; w.put<uint8>(Bolsa(g.ContainerSlot)).put<uint8>(Hueco(g.ContainerSlot, g.Slot))
                         .put<uint8>(Bolsa(o.ContainerSlot)).put<uint8>(Hueco(o.ContainerSlot, o.Slot));
                Srv(0x1D3, w.b);
                return true;
            }
            // ---- conjuntos de equipo
            case CMSG_SAVE_EQUIPMENT_SET:                    // -> 0x4BD: packguid, u32 índice, cstr, cstr, 19 x packguid (1 = ignorado)
            {
                WorldPackets::EquipmentSet::SaveEquipmentSet q(Paquete(CMSG_SAVE_EQUIPMENT_SET, b)); q.Read();
                auto const& d = q.Set;
                { std::lock_guard<std::mutex> l(_mConj); if (d.Guid) _conjIgnorar[d.Guid] = d.IgnoreMask; }
                Wr w; PutPack(w, d.Guid); w.put<uint32>(d.SetID).cstr(d.SetName).cstr(d.SetIcon);
                for (uint32 s = 0; s < EQUIPMENT_SET_SLOTS; ++s)
                    PutPack(w, (d.IgnoreMask & (1u << s)) ? 1 : mundo->A335(d.Pieces[s]));
                Srv(0x4BD, w.b);
                return true;
            }
            case CMSG_DELETE_EQUIPMENT_SET:                  // -> 0x13E packguid
            {
                WorldPackets::EquipmentSet::DeleteEquipmentSet q(Paquete(CMSG_DELETE_EQUIPMENT_SET, b)); q.Read();
                Wr w; PutPack(w, q.ID); Srv(0x13E, w.b);
                return true;
            }
            case CMSG_USE_EQUIPMENT_SET:                     // -> 0x4D5: 19 x (packguid, u8 bolsa, u8 hueco)
            {
                WorldPackets::EquipmentSet::UseEquipmentSet q(Paquete(CMSG_USE_EQUIPMENT_SET, b)); q.Read();
                uint32 ign = 0;
                { std::lock_guard<std::mutex> l(_mConj); _conjUsado = q.GUID; auto it = _conjIgnorar.find(q.GUID); if (it != _conjIgnorar.end()) ign = it->second; }
                Wr w;
                for (uint32 s = 0; s < EQUIPMENT_SET_SLOTS; ++s)
                {
                    auto const& it = q.Items[s];
                    PutPack(w, (ign & (1u << s)) ? 1 : mundo->A335(it.Item));
                    w.put<uint8>(Bolsa(it.ContainerSlot)).put<uint8>(Hueco(it.ContainerSlot, it.Slot));
                }
                Srv(0x4D5, w.b);
                return true;
            }
            case CMSG_REQUEST_CEMETERY_LIST:                 // el 3.3.5 no lo tiene: lista vacía
            {
                WorldPackets::Misc::RequestCemeteryListResponse p;
                Enviar(p.Write());
                return true;
            }
            // ---- H7e: calendario -> CMSG_CALENDAR_* 3.3.5 (lecturas de AzerothCore CalendarHandler.cpp)
            case CMSG_CALENDAR_GET: Srv(0x429, {}); return true;             // CMSG_CALENDAR_GET_CALENDAR: vacío
            case CMSG_CALENDAR_GET_NUM_PENDING: Srv(0x447, {}); return true;  // vacío
            case CMSG_CALENDAR_GET_EVENT:                    // -> 0x42A: u64 evento
            {
                WorldPackets::Calendar::CalendarGetEvent q(Paquete(CMSG_CALENDAR_GET_EVENT, b)); q.Read();
                Wr w; w.put<uint64>(q.EventID); Srv(0x42A, w.b);
                return true;
            }
            case CMSG_CALENDAR_COMMUNITY_INVITE:             // -> CMSG_CALENDAR_GUILD_FILTER 0x42B: u32 nivel mín, u32 nivel máx, u32 rango mín
            {
                // la "comunidad" de 3.4.3 es la hermandad (Wrathion llama a Guild::MassInviteToEvent, igual que AC). MaxRankOrder es el
                // orden del rango (0 = maestro), el mismo número que el rango de AC (IsRankNotLower: rango <= el pedido). ClubID sobra
                WorldPackets::Calendar::CalendarCommunityInviteRequest q(Paquete(CMSG_CALENDAR_COMMUNITY_INVITE, b)); q.Read();
                _calNivelMin = q.MinLevel ? q.MinLevel : 1;
                Wr w; w.put<uint32>(q.MinLevel).put<uint32>(q.MaxLevel).put<uint32>(q.MaxRankOrder); Srv(0x42B, w.b);
                return true;
            }
            case CMSG_CALENDAR_ADD_EVENT:                    // -> 0x42D: cstr título, cstr descripción, u8 tipo, u8 repetición, u32 máx. invitaciones,
            {                                                //   i32 mazmorra, hora, hora de zona, u32 flags, u32 n, (packguid, u8 estado, u8 rango)*
                WorldPackets::Calendar::CalendarAddEvent q(Paquete(CMSG_CALENDAR_ADD_EVENT, b)); q.Read();
                auto const& e = q.EventInfo;
                uint32 hora = HoraA335(e.Time);
                // AC descarta en silencio títulos de más de 31 bytes y descripciones de más de 255 (3.4.3 admite 255 / 2047)
                Wr w; w.cstr(RecorteUtf8(e.Title, 31)).cstr(RecorteUtf8(e.Description, 255));
                w.put<uint8>(e.EventType).put<uint8>(0).put<uint32>(q.MaxSize).put<int32>(e.TextureID);
                w.put<uint32>(hora).put<uint32>(hora).put<uint32>(e.Flags);   // la 2.ª hora (LockDate en SEND_EVENT) = la del evento
                w.put<uint32>(uint32(e.Invites.size()));     // AC no la lee si es anuncio de hermandad (flag 0x40): sobra, no estorba
                for (auto const& i : e.Invites) { PutPack(w, mundo->A335(i.Guid)); w.put<uint8>(i.Status).put<uint8>(i.Moderator); }
                Srv(0x42D, w.b);
                return true;
            }
            case CMSG_CALENDAR_UPDATE_EVENT:                 // -> 0x42E: u64 evento, u64 invitación, cstr, cstr, u8 tipo, u8 repetición, u32 máx.,
            {                                                //   i32 mazmorra, hora, hora de zona, u32 flags
                WorldPackets::Calendar::CalendarUpdateEvent q(Paquete(CMSG_CALENDAR_UPDATE_EVENT, b)); q.Read();
                auto const& e = q.EventInfo;
                uint32 hora = HoraA335(e.Time);
                Wr w; w.put<uint64>(e.EventID).put<uint64>(e.ModeratorID).cstr(RecorteUtf8(e.Title, 31)).cstr(RecorteUtf8(e.Description, 255));
                w.put<uint8>(e.EventType).put<uint8>(0).put<uint32>(q.MaxSize).put<int32>(int32(e.TextureID));
                w.put<uint32>(hora).put<uint32>(hora).put<uint32>(e.Flags);
                Srv(0x42E, w.b);
                return true;
            }
            case CMSG_CALENDAR_REMOVE_EVENT:                 // -> 0x42F: u64 evento, u64 invitación, u32 flags (AC solo lee el evento)
            {
                WorldPackets::Calendar::CalendarRemoveEvent q(Paquete(CMSG_CALENDAR_REMOVE_EVENT, b)); q.Read();
                Wr w; w.put<uint64>(q.EventID).put<uint64>(q.ModeratorID).put<uint32>(q.Flags); Srv(0x42F, w.b);
                return true;
            }
            case CMSG_CALENDAR_COPY_EVENT:                   // -> 0x430: u64 evento, u64 invitación, hora
            {
                WorldPackets::Calendar::CalendarCopyEvent q(Paquete(CMSG_CALENDAR_COPY_EVENT, b)); q.Read();
                Wr w; w.put<uint64>(q.EventID).put<uint64>(q.ModeratorID).put<uint32>(HoraA335(q.Date)); Srv(0x430, w.b);
                return true;
            }
            case CMSG_CALENDAR_INVITE:                       // -> CMSG_CALENDAR_EVENT_INVITE 0x431: u64 evento, u64 invitación, cstr nombre, u8 preinvitación, u8 de hermandad
            {
                // Wrathion: Creating = aún no hay evento (la preinvitación de AC); IsSignUp = el evento es de hermandad (isGuildEvent de AC)
                WorldPackets::Calendar::CalendarInvite q(Paquete(CMSG_CALENDAR_INVITE, b)); q.Read();
                Wr w; w.put<uint64>(q.EventID).put<uint64>(q.ModeratorID).cstr(q.Name.substr(0, q.Name.find('-')))   // "Nombre-Reino" -> "Nombre"
                      .put<uint8>(q.Creating ? 1 : 0).put<uint8>(q.IsSignUp ? 1 : 0);
                Srv(0x431, w.b);
                return true;
            }
            case CMSG_CALENDAR_RSVP:                         // -> CMSG_CALENDAR_EVENT_RSVP 0x432: u64 evento, u64 invitación, u32 estado
            {
                WorldPackets::Calendar::CalendarRSVP q(Paquete(CMSG_CALENDAR_RSVP, b)); q.Read();
                Wr w; w.put<uint64>(q.EventID).put<uint64>(q.InviteID).put<uint32>(q.Status); Srv(0x432, w.b);
                return true;
            }
            case CMSG_CALENDAR_REMOVE_INVITE:                // -> CMSG_CALENDAR_EVENT_REMOVE_INVITE 0x433: packguid, u64 invitación, u64 invitación del moderador, u64 evento
            {
                WorldPackets::Calendar::CalendarRemoveInvite q(Paquete(CMSG_CALENDAR_REMOVE_INVITE, b)); q.Read();
                Wr w; PutPack(w, mundo->A335(q.Guid)); w.put<uint64>(q.InviteID).put<uint64>(q.ModeratorID).put<uint64>(q.EventID); Srv(0x433, w.b);
                return true;
            }
            case CMSG_CALENDAR_STATUS:                       // -> CMSG_CALENDAR_EVENT_STATUS 0x434: packguid, u64 evento, u64 invitación, u64 invitación del moderador, u8 estado
            {
                WorldPackets::Calendar::CalendarStatus q(Paquete(CMSG_CALENDAR_STATUS, b)); q.Read();
                Wr w; PutPack(w, mundo->A335(q.Guid)); w.put<uint64>(q.EventID).put<uint64>(q.InviteID).put<uint64>(q.ModeratorID).put<uint8>(q.Status);
                Srv(0x434, w.b);
                return true;
            }
            case CMSG_CALENDAR_MODERATOR_STATUS:             // -> CMSG_CALENDAR_EVENT_MODERATOR_STATUS 0x435: packguid, u64 evento, u64 invitación, u64 invitación del moderador, u8 rango
            {
                WorldPackets::Calendar::CalendarModeratorStatusQuery q(Paquete(CMSG_CALENDAR_MODERATOR_STATUS, b)); q.Read();
                Wr w; PutPack(w, mundo->A335(q.Guid)); w.put<uint64>(q.EventID).put<uint64>(q.InviteID).put<uint64>(q.ModeratorID).put<uint8>(q.Status);
                Srv(0x435, w.b);
                return true;
            }
            case CMSG_CALENDAR_COMPLAIN:                     // -> 0x446: u64 evento, u64 guid (sin empaquetar) de quien invitó
            {
                WorldPackets::Calendar::CalendarComplain q(Paquete(CMSG_CALENDAR_COMPLAIN, b)); q.Read();
                Wr w; w.put<uint64>(q.EventID).put<uint64>(mundo->A335(q.InvitedByGUID)); Srv(0x446, w.b);
                return true;
            }
            case CMSG_CALENDAR_EVENT_SIGN_UP:                // -> CMSG_CALENDAR_EVENT_SIGNUP 0x4BA: u64 evento, u8 provisional
            {
                WorldPackets::Calendar::CalendarEventSignUp q(Paquete(CMSG_CALENDAR_EVENT_SIGN_UP, b)); q.Read();
                Wr w; w.put<uint64>(q.EventID).put<uint8>(q.Tentative ? 1 : 0); Srv(0x4BA, w.b);
                return true;
            }
            // ---- H7f: equipos de arena, inspección JcJ y honor
            case CMSG_ARENA_TEAM_ROSTER:                     // u32 -> 0x34B (consulta: trae las estadísticas) + 0x34D, u32 id
            {
                // Wrathion (3.4.3) lo lee como hueco 0-2; WPP 4.4.0 como id de equipo. EquipoArenaId acepta los dos.
                Rd q(b); uint32 v = q.get<uint32>();
                if (uint32 id = EquipoArenaId(v)) PedirEquipoArena(id, true);
                else ListaArenaVacia(v);
                return true;
            }
            case CMSG_QUERY_ARENA_TEAM:                      // u32 id (supuesto) -> 0x34B u32
            {
                Rd q(b); uint32 id = q.get<uint32>();
                if (id) { Wr w; w.put<uint32>(id); Srv(0x34B, w.b); }
                return true;
            }
            case CMSG_ARENA_TEAM_ACCEPT: case CMSG_ARENA_TEAM_DECLINE:   // guid de quien invita, guid del equipo -> 0x351 / 0x352 sin datos
                Srv(op == CMSG_ARENA_TEAM_ACCEPT ? 0x351 : 0x352, {});
                return true;
            case CMSG_ARENA_TEAM_LEAVE: case CMSG_ARENA_TEAM_DISBAND:    // i32 equipo -> 0x353 / 0x355 u32 id
            {
                Rd q(b); uint32 id = EquipoArenaId(q.get<uint32>());
                if (id) { Wr w; w.put<uint32>(id); Srv(op == CMSG_ARENA_TEAM_LEAVE ? 0x353 : 0x355, w.b); }
                return true;
            }
            case CMSG_ARENA_TEAM_LEADER: case CMSG_ARENA_TEAM_REMOVE:    // i32 equipo, guid jugador -> 0x356 / 0x354: u32 id, cstr nombre
            {
                WorldPacket rq = Paquete(OpcodeClient(op), b);
                uint32 v = 0; ObjectGuid g; rq >> v >> g;
                uint32 id = EquipoArenaId(v);
                std::string nombre = NombreDe(mundo->A335(g));
                if (id && !nombre.empty()) { Wr w; w.put<uint32>(id).cstr(nombre); Srv(op == CMSG_ARENA_TEAM_LEADER ? 0x356 : 0x354, w.b); }
                return true;
            }
            case CMSG_INSPECT_PVP:                           // -> MSG_INSPECT_ARENA_TEAMS 0x377 u64
            {
                WorldPackets::Inspect::InspectPvP q(Paquete(CMSG_INSPECT_PVP, b)); q.Read();
                uint64 g = mundo->A335(q.Target);
                { std::lock_guard<std::mutex> l(_mArena); _inspArenaDe = g; _inspArena = { }; }
                EnviarInspeccionJcJ(g, { });                 // respuesta vacía ya: sin equipos AzerothCore no contesta nada
                Wr w; w.put<uint64>(g); Srv(0x377, w.b);
                return true;
            }
            case CMSG_REQUEST_HONOR_STATS:                   // -> MSG_INSPECT_HONOR_STATS 0x2D6 u64
            {
                WorldPackets::Inspect::RequestHonorStats q(Paquete(CMSG_REQUEST_HONOR_STATS, b)); q.Read();
                Wr w; w.put<uint64>(mundo->A335(q.Target)); Srv(0x2D6, w.b);
                return true;
            }
            // ---- H7a: personaje, establo, mascota, grupo, hermandad, objetos, informes
            case CMSG_SHOWING_HELM: case CMSG_SHOWING_CLOAK:  // -> 0x2B9 / 0x2BA: u8
            {
                bool v;
                if (op == CMSG_SHOWING_HELM) { WorldPackets::Character::ShowingHelm q(Paquete(OpcodeClient(op), b)); q.Read(); v = q.ShowHelm; }
                else { WorldPackets::Character::ShowingCloak q(Paquete(OpcodeClient(op), b)); q.Read(); v = q.ShowCloak; }
                Wr w; w.put<uint8>(v ? 1 : 0); Srv(op == CMSG_SHOWING_HELM ? 0x2B9 : 0x2BA, w.b);
                return true;
            }
            case CMSG_SET_AMMO:                              // -> 0x268: u32
            {
                WorldPackets::Item::SetAmmo q(Paquete(OpcodeClient(op), b)); q.Read();
                Wr w; w.put<uint32>(q.ItemId); Srv(0x268, w.b);
                return true;
            }
            case CMSG_REQUEST_STABLED_PETS:                  // -> MSG_LIST_STABLED_PETS 0x26F: u64
            {
                WorldPackets::NPC::RequestStabledPets q(Paquete(OpcodeClient(op), b)); q.Read();
                Wr w; w.put<uint64>(mundo->A335(q.StableMaster)); Srv(0x26F, w.b);
                return true;
            }
            case CMSG_STABLE_PET:                            // -> 0x270: u64 (guarda la mascota actual)
            {
                WorldPackets::NPC::StablePet q(Paquete(OpcodeClient(op), b)); q.Read();
                Wr w; w.put<uint64>(mundo->A335(q.StableMaster)); Srv(0x270, w.b);
                return true;
            }
            case CMSG_BUY_STABLE_SLOT:                       // -> 0x272: u64
            {
                WorldPackets::NPC::BuyStableSlot q(Paquete(OpcodeClient(op), b)); q.Read();
                Wr w; w.put<uint64>(mundo->A335(q.StableMaster)); Srv(0x272, w.b);
                return true;
            }
            case CMSG_UNSTABLE_PET: case CMSG_STABLE_SWAP_PET:   // -> 0x271 / 0x275: u64, u32 mascota
            {
                ObjectGuid m; uint32 n;
                if (op == CMSG_UNSTABLE_PET) { WorldPackets::NPC::UnStablePet q(Paquete(OpcodeClient(op), b)); q.Read(); m = q.StableMaster; n = q.PetNumber; }
                else { WorldPackets::NPC::StableSwapPet q(Paquete(OpcodeClient(op), b)); q.Read(); m = q.StableMaster; n = q.PetNumber; }
                Wr w; w.put<uint64>(mundo->A335(m)).put<uint32>(n); Srv(op == CMSG_UNSTABLE_PET ? 0x271 : 0x275, w.b);
                return true;
            }
            case CMSG_PET_LEARN_TALENT:                      // -> 0x47A: u64, u32 talento, u32 rango
            {
                WorldPackets::Talent::LearnPetTalent q(Paquete(OpcodeClient(op), b)); q.Read();
                Wr w; w.put<uint64>(mundo->A335(q.PetGUID)).put<uint32>(q.TalentID).put<uint32>(q.Rank); Srv(0x47A, w.b);
                return true;
            }
            case CMSG_LEARN_PREVIEW_TALENTS_PET:             // -> 0x4C2: u64, u32 n, (u32 talento, u32 rango)*
            {
                WorldPackets::Talent::LearnPetPreviewTalents q(Paquete(OpcodeClient(op), b)); q.Read();
                Wr w; w.put<uint64>(mundo->A335(q.PetGUID)).put<uint32>(uint32(q.Talents.size()));
                for (auto const& t : q.Talents) w.put<uint32>(t.TalentID).put<uint32>(t.Rank);
                Srv(0x4C2, w.b);
                return true;
            }
            case CMSG_SET_PARTY_ASSIGNMENT:                  // -> MSG_PARTY_ASSIGNMENT 0x38E: u8 tipo, u8 poner, u64
            {
                WorldPackets::Party::SetPartyAssignment q(Paquete(OpcodeClient(op), b)); q.Read();
                Wr w; w.put<uint8>(q.Assignment).put<uint8>(q.Set ? 1 : 0).put<uint64>(mundo->A335(q.Target)); Srv(0x38E, w.b);
                return true;
            }
            case CMSG_SAVE_GUILD_EMBLEM:                     // -> MSG_SAVE_GUILD_EMBLEM 0x1F1: u64, u32 emblema, color, borde, color borde, fondo
            {
                WorldPackets::Guild::SaveGuildEmblem q(Paquete(OpcodeClient(op), b)); q.Read();
                Wr w; w.put<uint64>(mundo->A335(q.Vendor)).put<int32>(q.EStyle).put<int32>(q.EColor).put<int32>(q.BStyle).put<int32>(q.BColor).put<int32>(q.Bg);
                Srv(0x1F1, w.b);
                return true;
            }
            case CMSG_GUILD_ASSIGN_MEMBER_RANK:              // 3.3.5 solo sube o baja de uno en uno: se repite promote/demote por nombre
            {
                WorldPackets::Guild::GuildAssignMemberRank q(Paquete(OpcodeClient(op), b)); q.Read();
                uint64 g = mundo->A335(q.Member);
                int32 actual;
                { std::lock_guard<std::mutex> l(_mGuild); auto it = _rangoMiembro.find(g); if (it == _rangoMiembro.end()) return true; actual = it->second; }
                std::string nombre = NombreDe(g);
                for (int32 i = actual; i != q.RankOrder; i += q.RankOrder > actual ? 1 : -1)
                {
                    Wr w; w.cstr(nombre); Srv(q.RankOrder > actual ? 0x08C : 0x08B, w.b);   // rango mayor = más bajo: demote
                }
                return true;
            }
            case CMSG_REPORT_PVP_PLAYER_AFK:                 // -> 0x3E4: u64
            {
                WorldPackets::Battleground::ReportPvPPlayerAFK q(Paquete(OpcodeClient(op), b)); q.Read();
                Wr w; w.put<uint64>(mundo->A335(q.Offender)); Srv(0x3E4, w.b);
                return true;
            }
            case CMSG_CHAT_REPORT_IGNORED:                   // -> CMSG_CHAT_IGNORED 0x225: u64, u8
            {
                WorldPackets::Chat::ChatReportIgnored q(Paquete(OpcodeClient(op), b)); q.Read();
                Wr w; w.put<uint64>(mundo->A335(q.IgnoredGUID)).put<uint8>(q.Reason); Srv(0x225, w.b);
                return true;
            }
            case CMSG_CHAT_CHANNEL_DECLINE_INVITE:           // -> CMSG_DECLINE_CHANNEL_INVITE 0x410: cstr
            {
                WorldPackets::Channel::ChannelCommand q(Paquete(OpcodeClient(op), b)); q.Read();
                Wr w; w.cstr(q.ChannelName); Srv(0x410, w.b);
                return true;
            }
            case CMSG_GET_ITEM_PURCHASE_DATA:                // -> CMSG_ITEM_REFUND_INFO 0x4B3: u64
            {
                WorldPackets::Item::GetItemPurchaseData q(Paquete(OpcodeClient(op), b)); q.Read();
                Wr w; w.put<uint64>(mundo->A335(q.ItemGUID)); Srv(0x4B3, w.b);
                return true;
            }
            case CMSG_ITEM_PURCHASE_REFUND:                  // -> CMSG_ITEM_REFUND 0x4B4: u64
            {
                WorldPackets::Item::ItemPurchaseRefund q(Paquete(OpcodeClient(op), b)); q.Read();
                Wr w; w.put<uint64>(mundo->A335(q.ItemGUID)); Srv(0x4B4, w.b);
                return true;
            }
            case CMSG_GET_MIRROR_IMAGE_DATA:                 // -> CMSG_GET_MIRRORIMAGE_DATA 0x401: u64
            {
                WorldPackets::Spells::GetMirrorImageData q(Paquete(OpcodeClient(op), b)); q.Read();
                Wr w; w.put<uint64>(mundo->A335(q.UnitGUID)); Srv(0x401, w.b);
                return true;
            }
            case CMSG_WHO_IS:                                // -> CMSG_WHOIS 0x064: cstr
            {
                WorldPackets::Who::WhoIsRequest q(Paquete(OpcodeClient(op), b)); q.Read();
                Wr w; w.cstr(q.CharName); Srv(0x064, w.b);
                return true;
            }
            case CMSG_COMPLAINT:                             // -> CMSG_COMPLAIN 0x3C7: u8 tipo, u64, (correo: u32, u32 id, u32) | (chat: u32, u32 tipo, u32 canal, u32 s, cstr)
            {
                WorldPackets::Ticket::Complaint q(Paquete(OpcodeClient(op), b)); q.Read();
                Wr w; w.put<uint8>(q.ComplaintType == 0 ? 0 : 1).put<uint64>(mundo->A335(q.Offender.PlayerGuid));
                if (q.ComplaintType == 0) w.put<uint32>(0).put<uint32>(uint32(q.MailID)).put<uint32>(0);
                else w.put<uint32>(0).put<uint32>(q.Chat.Command).put<uint32>(q.Chat.ChannelID).put<uint32>(q.Offender.TimeSinceOffence).cstr(q.Chat.MessageLog);
                Srv(0x3C7, w.b);
                return true;
            }
            case CMSG_BUG_REPORT:                            // -> CMSG_BUG 0x1CA: u32 sugerencia, u32 largo, texto, u32 largo, tipo
            {
                WorldPackets::Ticket::BugReport q(Paquete(OpcodeClient(op), b)); q.Read();
                std::string tipo = q.DiagInfo;
                Wr w; w.put<uint32>(q.Type).put<uint32>(uint32(q.Text.size() + 1)).cstr(q.Text).put<uint32>(uint32(tipo.size() + 1)).cstr(tipo);
                Srv(0x1CA, w.b);
                return true;
            }
            case CMSG_UPDATE_MISSILE_TRAJECTORY:             // -> 0x462: u64, u32 hechizo, f elevación, f velocidad, xyz, xyz, u8 parar
            {
                WorldPackets::Spells::UpdateMissileTrajectory q(Paquete(OpcodeClient(op), b)); q.Read();
                Wr w; w.put<uint64>(mundo->A335(q.Guid)).put<uint32>(uint32(q.SpellID)).put<float>(q.Pitch).put<float>(q.Speed)
                      .put<float>(q.FirePos.Pos.GetPositionX()).put<float>(q.FirePos.Pos.GetPositionY()).put<float>(q.FirePos.Pos.GetPositionZ())
                      .put<float>(q.ImpactPos.Pos.GetPositionX()).put<float>(q.ImpactPos.Pos.GetPositionY()).put<float>(q.ImpactPos.Pos.GetPositionZ())
                      .put<uint8>(0);
                Srv(0x462, w.b);
                return true;
            }
            case CMSG_MOUNT_SPECIAL_ANIM: Srv(0x171, {}); return true;
            case CMSG_GM_TICKET_GET_SYSTEM_STATUS: Srv(0x21A, {}); return true;
            // el cliente 3.4.3 no tiene ventana de ticket de GM: "Informar de un error" y "Sugerencia" (SubmitUserFeedback) se
            // convierten en CMSG_GMTICKET_CREATE 0x205 (u32 mapa, 3 float, cstr texto, u32 respuesta, u8 más ayuda, u32 0 tiempos, u32 0 chat)
            case CMSG_GM_TICKET_GET_CASE_STATUS: Srv(0x211, {}); return true;   // -> 0x212 / 0x4EF (+ 0x1CF hora, ya traducido)
            case CMSG_GM_TICKET_ACKNOWLEDGE_SURVEY:                              // "he leído la respuesta" -> cerrar
            {
                bool comp; { std::lock_guard<std::mutex> l(_mTicket); comp = _gmTicket.completado; }
                Srv(comp ? 0x4F0 : 0x217, {});
                return true;
            }
            case CMSG_SUPPORT_TICKET_SUBMIT_COMPLAINT:
            {
                WorldPackets::Ticket::SupportTicketSubmitComplaint q(Paquete(CMSG_SUPPORT_TICKET_SUBMIT_COMPLAINT, b)); q.Read();
                bool hay; { std::lock_guard<std::mutex> l(_mTicket); hay = _gmTicket.hay; }
                std::string texto = "[Denuncia] guid " + std::to_string(mundo->A335(q.TargetCharacterGUID)) + " tipo " + std::to_string(q.ReportType)
                                  + " cat " + std::to_string(q.MajorCategory) + "/" + std::to_string(q.MinorCategoryFlags) + ": " + q.Note;
                if (!hay)
                {
                    Wr w; w.put<uint32>(uint32(q.Header.MapID)).put<float>(q.Header.Position.Pos.GetPositionX())
                          .put<float>(q.Header.Position.Pos.GetPositionY()).put<float>(q.Header.Position.Pos.GetPositionZ())
                          .cstr(texto.substr(0, 1000)).put<uint32>(1).put<uint8>(0).put<uint32>(0).put<uint32>(0);
                    Srv(0x205, w.b);
                }
                else
                {
                    Wr w; w.put<uint8>(1).put<uint64>(mundo->A335(q.TargetCharacterGUID)).put<uint32>(0).put<uint32>(0)
                          .put<uint32>(0).put<uint32>(0).cstr(texto.substr(0, 250));
                    Srv(0x3C7, w.b);
                }
                return true;
            }
            case CMSG_SUBMIT_USER_FEEDBACK:
            {
                WorldPackets::Ticket::SubmitUserFeedback q(Paquete(CMSG_SUBMIT_USER_FEEDBACK, b)); q.Read();
                if (q.Note.empty()) return true;
                std::string texto = q.Note.rfind("[AYUDA]", 0) == 0 ? q.Note.substr(7) : (q.IsSuggestion ? "[Sugerencia] " : "[Error] ") + q.Note;
                Wr w; w.put<uint32>(uint32(q.Header.MapID)).put<float>(q.Header.Position.Pos.GetPositionX())
                      .put<float>(q.Header.Position.Pos.GetPositionY()).put<float>(q.Header.Position.Pos.GetPositionZ())
                      .cstr(texto.substr(0, 1000)).put<uint32>(1).put<uint8>(0).put<uint32>(0).put<uint32>(0);
                Srv(0x205, w.b);
                return true;
            }
            case CMSG_REQUEST_RATED_PVP_INFO:                // como Wrathion: brackets vacíos (AC no tiene índice personal por bracket)
            {
                WorldPackets::Battleground::RatedPvpInfo p; Enviar(p.Write());
                return true;
            }
            case CMSG_REQUEST_PVP_REWARDS: RecompensasJcJ(); return true;
            case CMSG_SET_ROLE:                              // rol del marco de grupo (solo el propio) -> CMSG_LFG_SET_ROLES 0x36A u8 (mismos bits)
            {
                WorldPackets::Party::SetRole q(Paquete(CMSG_SET_ROLE, b)); q.Read();
                if (!q.TargetGUID.IsEmpty() && mundo->A335(q.TargetGUID) != yo335) return true;
                Wr w; w.put<uint8>(q.Role); Srv(0x36A, w.b);
                return true;
            }
            case CMSG_CANCEL_GROWTH_AURA: Srv(0x29B, {}); return true;
            case CMSG_HEARTH_AND_RESURRECT: Srv(0x49C, {}); return true;
            case CMSG_COMPLETE_MOVIE: Srv(0x465, {}); return true;
            case CMSG_KEEP_ALIVE: Srv(0x407, {}); return true;
            // informes del cliente moderno y utilidades de retail: no existen en 3.3.5
            case CMSG_DISCARDED_TIME_SYNC_ACKS: case CMSG_REPORT_CLIENT_VARIABLES: case CMSG_REPORT_ENABLED_ADDONS:
            case CMSG_REPORT_KEYBINDING_EXECUTION_COUNTS: case CMSG_ENABLE_NAGLE: case CMSG_OBJECT_UPDATE_RESCUED:
            case CMSG_DECLINE_GUILD_INVITES: case CMSG_OPENING_CINEMATIC: case CMSG_QUERY_CORPSE_TRANSPORT:
            case CMSG_REQUEST_WORLD_QUEST_UPDATE: case CMSG_SET_EVERYONE_IS_ASSISTANT:
            case CMSG_BATTLE_PET_REQUEST_JOURNAL_LOCK: case CMSG_TIME_SYNC_RESPONSE_DROPPED: case CMSG_GET_ACCOUNT_NOTIFICATIONS:
                return true;
            // peticiones del cliente moderno sin equivalente en 3.3.5: se callan
            case CMSG_LOADING_SCREEN_NOTIFY: case CMSG_REQUEST_FORCED_REACTIONS: case CMSG_VIOLENCE_LEVEL: case CMSG_OVERRIDE_SCREEN_FLASH:
            case CMSG_REQUEST_CONQUEST_FORMULA_CONSTANTS:
            case CMSG_REQUEST_LFG_LIST_BLACKLIST: case CMSG_LFG_LIST_GET_STATUS:
            case CMSG_BATTLE_PET_REQUEST_JOURNAL:
            case CMSG_GET_ACCOUNT_CHARACTER_LIST: case CMSG_QUERY_COUNTDOWN_TIMER: case CMSG_GUILD_SET_ACHIEVEMENT_TRACKING:
            case CMSG_EMOTE:
                return true;
            default:
                return false;
        }
    }

private:
    static WorldPacket Paquete(OpcodeClient op, std::vector<uint8> const& b)
    {
        WorldPacket wp(op);
        if (!b.empty()) wp.append(b.data(), b.size());
        return wp;
    }
    static uint64 Pack(Rd& r)
    {
        uint8 m = r.get<uint8>(); uint64 g = 0;
        for (int i = 0; i < 8; ++i) if (m & (1 << i)) g |= uint64(r.get<uint8>()) << (i * 8);
        return g;
    }


    // ---- identificadores de lanzamiento: el cliente 3.4.3 usa una guid (CastID), el 3.3.5 un contador de 8 bits
    std::mutex _mCast;
    std::array<ObjectGuid, 256> _castIds;
    std::array<uint32, 256> _castSpell = { };
    // como TrinityCore (Player::ExecutePendingSpellCastRequest): el servidor crea su propio CastID (fuente NORMAL) y se lo
    // enlaza al cliente con SMSG_SPELL_PREPARE {ClientCastID, ServerCastID}; SPELL_START/GO/fallos van con el del servidor.
    // Sin ese enlace el cliente trata el lanzamiento como ajeno y no dibuja el misil (ver 02_pendientes §2).
    std::array<ObjectGuid, 256> _castSrv;
    static constexpr uint8 kFuenteNormal = 3;       // SPELL_CAST_SOURCE_NORMAL
    uint8 _castCount = 0;
    uint32 _castSerie = 0;
    uint8 NuevoCast(ObjectGuid const& id, uint32 spell)
    {
        std::lock_guard<std::mutex> l(_mCast);
        if (++_castCount == 0) _castCount = 1;
        _castIds[_castCount] = id; _castSpell[_castCount] = spell; _castSrv[_castCount].Clear();
        return _castCount;
    }
    uint8 CuentaDe(ObjectGuid const& id)
    {
        std::lock_guard<std::mutex> l(_mCast);
        for (int i = 1; i < 256; ++i) if (_castIds[i] == id || (!id.IsEmpty() && _castSrv[i] == id)) return uint8(i);
        return 0;
    }
    // inicio: nuevo CastID (o el que mandó el propio cliente); lanzamiento: el mismo del inicio, para que el cliente los enlace
    std::map<std::pair<uint64, uint32>, ObjectGuid> _castEnCurso;   // (lanzador 3.3.5, hechizo) -> CastID del SPELL_START
    ObjectGuid CastId(uint64 lanzador, uint8 cc, uint32 spell, bool inicio = false, bool lanzamiento = false, bool borrar = true)
    {
        std::lock_guard<std::mutex> l(_mCast);
        if (lanzador == yo335 && cc && _castSpell[cc] == spell && !_castIds[cc].IsEmpty())
        {
            if (_castSrv[cc].IsEmpty())
            {
                _castSrv[cc] = ObjectGuid::Create<HighGuid::Cast>(kFuenteNormal, uint16(mundo->mapa), spell, ++_castSerie);
                WorldPackets::Spells::SpellPrepare sp;
                sp.ClientCastID = _castIds[cc]; sp.ServerCastID = _castSrv[cc];
                Enviar(sp.Write());
            }
            return _castSrv[cc];
        }
        auto clave = std::make_pair(lanzador, spell);
        if (lanzamiento)
        {
            auto it = _castEnCurso.find(clave);
            if (it != _castEnCurso.end()) { ObjectGuid g = it->second; if (borrar) _castEnCurso.erase(it); return g; }
        }
        ObjectGuid g = ObjectGuid::Create<HighGuid::Cast>(kFuenteNormal, uint16(mundo->mapa), spell, ++_castSerie);
        if (inicio) { if (_castEnCurso.size() > 4096) _castEnCurso.clear(); _castEnCurso[clave] = g; }
        return g;
    }

    // bolsa (255 = mochila; si no, hueco de la bolsa) y hueco dentro de ella: 3.4.3 -> 3.3.5
    static uint8 Bolsa(uint8 b) { return b == 255 ? 255 : M335::HuecoA335(b); }
    static uint8 Hueco(uint8 bolsa, uint8 s) { return bolsa == 255 ? M335::HuecoA335(s) : s; }

    static constexpr uint32 kObjetivoObjeto = 0x2 | 0x4 | 0x8 | 0x80 | 0x100 | 0x400 | 0x10000 | 0x100000 | 0x200 | 0x8000 | 0x800 | 0x4000;
    // SpellCastTargets 3.3.5 (AC SpellCastTargets::Write)
    void LeerObjetivos(Rd& r, WorldPackets::Spells::SpellTargetData& t)
    {
        uint32 m = r.get<uint32>();
        t.Flags = m;
        if (m & kObjetivoObjeto) t.Unit = mundo->ToTc(Pack(r));
        if (m & (0x10 | 0x1000)) t.Item = mundo->ToTc(Pack(r));
        if (m & 0x20) { t.SrcLocation.emplace(); t.SrcLocation->Transport = mundo->ToTc(Pack(r)); float x = r.get<float>(), y = r.get<float>(), z = r.get<float>(); t.SrcLocation->Location = Position(x, y, z); }
        if (m & 0x40) { t.DstLocation.emplace(); t.DstLocation->Transport = mundo->ToTc(Pack(r)); float x = r.get<float>(), y = r.get<float>(), z = r.get<float>(); t.DstLocation->Location = Position(x, y, z); }
        if (m & 0x2000) t.Name = r.cstr();
    }
    void EscribirObjetivos(Wr& w, WorldPackets::Spells::SpellTargetData const& t)
    {
        uint32 m = t.Flags & 0x1FFFFF;
        w.put<uint32>(m);
        if (m & kObjetivoObjeto) PutPack(w, mundo->A335(t.Unit));
        if (m & (0x10 | 0x1000)) PutPack(w, mundo->A335(t.Item));
        if (m & 0x20) { PutPack(w, t.SrcLocation ? mundo->A335(t.SrcLocation->Transport) : 0); Position const& l = t.SrcLocation ? Position(t.SrcLocation->Location) : Position(); w.put<float>(l.GetPositionX()).put<float>(l.GetPositionY()).put<float>(l.GetPositionZ()); }
        if (m & 0x40) { PutPack(w, t.DstLocation ? mundo->A335(t.DstLocation->Transport) : 0); Position const& l = t.DstLocation ? Position(t.DstLocation->Location) : Position(); w.put<float>(l.GetPositionX()).put<float>(l.GetPositionY()).put<float>(l.GetPositionZ()); }
        if (m & 0x2000) w.cstr(t.Name);
    }
    // trayectoria del misil que predice el cliente (MissileTrajectory {Pitch, Speed}): AC la lee con castFlags 0x02 tras los
    // objetivos (WorldSession::HandleClientCastFlags: float elevación, float velocidad, u8 hay movimiento). Sin ella AC no
    // devuelve CAST_FLAG_ADJUST_MISSILE/TravelTime y el cliente se queda sin misil en los instantáneos que lo predicen
    // (Disparo arcano, Lanza de hielo) y en las catapultas/cañones de vehículo
    static uint8 BanderasTrayecto(WorldPackets::Spells::SpellCastRequest const& c)
    {
        return (c.MissileTrajectory.Pitch != 0.0f || c.MissileTrajectory.Speed != 0.0f) ? 0x02 : 0x00;
    }
    static void EscribirTrayecto(Wr& w, WorldPackets::Spells::SpellCastRequest const& c)
    {
        if (!BanderasTrayecto(c)) return;
        w.put<float>(c.MissileTrajectory.Pitch).put<float>(c.MissileTrajectory.Speed).put<uint8>(0);
    }
    static void PutPack(Wr& w, uint64 g)
    {
        uint8 m = 0; std::vector<uint8> bb;
        for (int i = 0; i < 8; ++i) if (uint8 x = uint8(g >> (i * 8))) { m |= uint8(1 << i); bb.push_back(x); }
        w.put<uint8>(m); for (uint8 x : bb) w.put<uint8>(x);
    }

    // banderas de lanzamiento que se pasan tal cual (las que exigen datos extra en 3.4.3 se quitan)
    // 0x20 (proyectil/munición), 0x800 (poder), 0x20000 (trayectoria) y 0x200000 (runas) se vuelven a poner en PoderRestante si llegan sus datos
    static uint32 CastFlags(uint32 f) { return f & ~(0x20u | 0x800u | 0x20000u | 0x80000u | 0x200000u | 0x400000u); }

    // SMSG_SPELL_START: packguid objeto/lanzador, packguid lanzador, u8 cuenta, u32 hechizo, u32 banderas, u32 tiempo, objetivos, ...
    void SpellStart335(Rd& r)
    {
        WorldPackets::Spells::SpellStart p;
        uint64 obj = Pack(r), c = Pack(r);
        uint8 cc = r.get<uint8>();
        auto& d = p.Cast;
        d.SpellID = int32(r.get<uint32>());
        d.CasterGUID = mundo->ToTc(obj); d.CasterUnit = mundo->ToTc(c);
        d.CastID = CastId(c, cc, uint32(d.SpellID), true);
        d.Visual.SpellXSpellVisualID = int32(VisualDe(uint32(d.SpellID)));
        uint32 f335 = r.get<uint32>();
        d.CastFlags = CastFlags(f335);
        d.CastTime = r.get<uint32>();
        LeerObjetivos(r, d.Target);
        PoderRestante(r, f335, c, d);
        Enviar(p.Write());
    }
    // SMSG_SPELL_GO: igual que START con u32 hora en lugar de tiempo, y antes de los objetivos: u8 n aciertos (u64)*, u8 n fallos (u64, u8[, u8])*
    void SpellGo335(Rd& r)
    {
        WorldPackets::Spells::SpellGo p;
        uint64 obj = Pack(r), c = Pack(r);
        uint8 cc = r.get<uint8>();
        auto& d = p.Cast;
        d.SpellID = int32(r.get<uint32>());
        d.CasterGUID = mundo->ToTc(obj); d.CasterUnit = mundo->ToTc(c);
        d.CastID = CastId(c, cc, uint32(d.SpellID), false, true);
        d.Visual.SpellXSpellVisualID = int32(VisualDe(uint32(d.SpellID)));
        uint32 f335 = r.get<uint32>();
        d.CastFlags = CastFlags(f335);
        r.get<uint32>();
        d.CastTime = getMSTime();                    // como Spell::SendSpellGo de TrinityCore
        uint8 n = r.get<uint8>();
        for (uint8 i = 0; i < n; ++i) d.HitTargets.push_back(mundo->ToTc(r.get<uint64>()));
        n = r.get<uint8>();
        for (uint8 i = 0; i < n; ++i)
        {
            d.MissTargets.push_back(mundo->ToTc(r.get<uint64>()));
            uint8 motivo = r.get<uint8>(), refleja = 0;
            if (motivo == 11) refleja = r.get<uint8>();
            d.MissStatus.emplace_back(motivo, refleja);
        }
        LeerObjetivos(r, d.Target);
        PoderRestante(r, f335, c, d);
        // EXPERIMENTO (worldgate_misil_municion.txt): el cliente solo calcula el vuelo del misil si trae AmmoDisplayID o si un kit
        // de la visual del lanzador lo pide (0x16f4dc0, ver 02_pendientes §2). Con el fichero, los hechizos con misil lo llevan a 0.
        if (g_misilMunicion && !d.AmmoDisplayID && !d.HitTargets.empty() && TieneMisil(uint32(d.Visual.SpellXSpellVisualID)))
        { d.AmmoDisplayID = 0; d.AmmoInventoryType = 0; }
        Enviar(p.Write());
    }
    static bool TieneMisil(uint32 sxsv)
    {
        SpellXSpellVisualEntry const* e = sSpellXSpellVisualStore.LookupEntry(sxsv);
        SpellVisualEntry const* sv = e ? sSpellVisualStore.LookupEntry(e->SpellVisualID) : nullptr;
        return sv && sv->SpellVisualMissileSetID && sDB2Manager.GetSpellVisualMissiles(sv->SpellVisualMissileSetID);
    }

    // CAST_FLAG_POWER_LEFT_SELF (0x800): AzerothCore añade tras los objetivos u32 poder del lanzador; TrinityCore lo manda como
    // RemainingPower {tipo, valor} con la misma bandera (Spell::SendSpellStart/SendSpellGo)
    // Cola de SMSG_SPELL_START/GO de AzerothCore (Spell::SendSpellGo) tras los objetivos, en este orden:
    // 0x800 u32 poder | 0x200000 u8 runas antes, u8 después, u8 enfriamiento por runa gastada | 0x20000 float elevación, u32 demora
    // | 0x20 u32 modelo de munición, u32 tipo de inventario. En 3.4.3 van en RemainingPower, MissileTrajectory y AmmoDisplayID/InventoryType.
    // tipo de poder de un hechizo (SpellPower de orden 0 de las DB2 del cliente); -1 si no cuesta nada
    static int8 TipoPoderDe(uint32 spell)
    {
        static std::unordered_map<uint32, int8> const tabla = []
        {
            std::unordered_map<uint32, int8> t;
            for (SpellPowerEntry const* e : sSpellPowerStore)
                if (e->OrderIndex == 0 || !t.count(e->SpellID)) t[e->SpellID] = e->PowerType;
            return t;
        }();
        auto it = tabla.find(spell);
        return it != tabla.end() ? it->second : int8(-1);
    }
    void PoderRestante(Rd& r, uint32 f335, uint64 lanzador, WorldPackets::Spells::SpellCastData& d)
    {
        if ((f335 & 0x800) && r.p + 4 <= r.b.size())
        {
            // AC manda el poder del TIPO DEL HECHIZO (Spell::SendSpellGo: GetPower(m_spellInfo->PowerType)), no el que enseña la
            // unidad: con un DK, cada habilidad de runas ponía su poder rúnico a 0. Las de runas (5) no se mandan (TC tampoco)
            int8 tipo = TipoPoderDe(uint32(d.SpellID));
            if (tipo < 0) tipo = int8(M335::Octeto(mundo->Valor335(mundo->ToTc(lanzador), M335::U_BYTES_0), 3));
            int32 valor = int32(r.get<uint32>());
            if (tipo != POWER_RUNES)
            {
                WorldPackets::Spells::SpellPowerData pd;
                pd.Type = tipo; pd.Cost = valor;
                d.RemainingPower.push_back(pd);
                d.CastFlags |= 0x800;
            }
        }
        if ((f335 & 0x200000) && r.p + 2 <= r.b.size())
        {
            uint8 antes = r.get<uint8>(), despues = r.get<uint8>();
            for (int i = 0; i < 6; ++i) if ((antes & (1 << i)) && !(despues & (1 << i))) r.get<uint8>();
        }
        if ((f335 & 0x20000) && r.p + 8 <= r.b.size())
        {
            d.MissileTrajectory.Pitch = r.get<float>(); d.MissileTrajectory.TravelTime = r.get<uint32>();
            d.CastFlags |= 0x20000;
        }
        if ((f335 & 0x20) && r.p + 8 <= r.b.size())   // flechas y balas (TrinityCore: CAST_FLAG_PROJECTILE)
        {
            d.AmmoDisplayID = int32(r.get<uint32>()); d.AmmoInventoryType = int32(r.get<uint32>());
            d.CastFlags |= 0x20;
        }
    }

    // SMSG_ATTACKERSTATEUPDATE (AC Unit::SendAttackStateUpdate)
    void AttackerState335(Rd& r)
    {
        WorldPackets::CombatLog::AttackerStateUpdate p;
        p.HitInfo = r.get<uint32>();
        p.AttackerGUID = mundo->ToTc(Pack(r)); p.VictimGUID = mundo->ToTc(Pack(r));
        p.Damage = int32(r.get<uint32>()); p.OriginalDamage = p.Damage;
        int32 ok = int32(r.get<uint32>()); p.OverDamage = ok > 0 ? ok : -1;
        uint8 n = r.get<uint8>();
        std::vector<std::array<int32, 3>> sub(n);
        for (auto& s : sub) { s[0] = int32(r.get<uint32>()); r.get<float>(); s[1] = int32(r.get<uint32>()); s[2] = 0; }
        std::vector<int32> abs(n, 0), res(n, 0);
        if (p.HitInfo & (0x20 | 0x40)) for (auto& a : abs) a = int32(r.get<uint32>());
        if (p.HitInfo & (0x80 | 0x100)) for (auto& x : res) x = int32(r.get<uint32>());
        if (n)
        {
            p.SubDmg.emplace();
            p.SubDmg->SchoolMask = sub[0][0]; p.SubDmg->Damage = sub[0][1]; p.SubDmg->FDamage = float(sub[0][1]);
            p.SubDmg->Absorbed = abs[0]; p.SubDmg->Resisted = res[0];
        }
        p.VictimState = r.get<uint8>();
        p.AttackerState = r.get<uint32>();
        p.MeleeSpellID = r.get<uint32>();
        if (p.HitInfo & 0x2000) p.BlockAmount = int32(r.get<uint32>());
        if (p.HitInfo & 0x800000) p.RageGained = int32(r.get<uint32>());
        p.HitInfo &= ~0x1u;                              // UNK1 = datos de depuración que no se traducen
        Enviar(p.Write());
    }

    // SMSG_PERIODICAURALOG: packguid objetivo, packguid lanzador, u32 hechizo, u32 n, (u32 tipo de aura, datos)*
    void PeriodicAura335(Rd& r)
    {
        WorldPackets::CombatLog::SpellPeriodicAuraLog p;
        p.TargetGUID = mundo->ToTc(Pack(r)); p.CasterGUID = mundo->ToTc(Pack(r));
        p.SpellID = int32(r.get<uint32>());
        uint32 n = r.get<uint32>();
        for (uint32 i = 0; i < n && r.p < r.b.size(); ++i)
        {
            WorldPackets::CombatLog::SpellPeriodicAuraLog::SpellLogEffect e;
            e.Effect = int32(r.get<uint32>());
            switch (e.Effect)
            {
                case 3: case 89:                             // PERIODIC_DAMAGE(_PERCENT): daño, sobra, escuela, absorbe, resiste, crítico
                    e.Amount = int32(r.get<uint32>()); e.OriginalDamage = e.Amount; e.OverHealOrKill = int32(r.get<uint32>());
                    e.SchoolMaskOrPower = int32(r.get<uint32>()); e.AbsorbedOrAmplitude = int32(r.get<uint32>());
                    e.Resisted = int32(r.get<uint32>()); e.Crit = r.get<uint8>() != 0;
                    break;
                case 8: case 20:                             // PERIODIC_HEAL / OBS_MOD_HEALTH: cura, sobra, absorbe, crítico
                    e.Amount = int32(r.get<uint32>()); e.OriginalDamage = e.Amount; e.OverHealOrKill = int32(r.get<uint32>());
                    e.AbsorbedOrAmplitude = int32(r.get<uint32>()); e.Crit = r.get<uint8>() != 0;
                    break;
                case 21: case 24:                            // OBS_MOD_POWER / PERIODIC_ENERGIZE: tipo, cantidad
                    e.SchoolMaskOrPower = int32(r.get<uint32>()); e.Amount = int32(r.get<uint32>());
                    break;
                case 64:                                     // PERIODIC_MANA_LEECH: tipo, cantidad, multiplicador
                    e.SchoolMaskOrPower = int32(r.get<uint32>()); e.Amount = int32(r.get<uint32>()); r.get<float>();
                    break;
                default:
                    break;
            }
            p.Effects.push_back(e);
        }
        Enviar(p.Write());
    }

    // ---- misiones: objetos de recompensa a elegir (en el orden del paquete 3.3.5) y plantillas pedidas
    std::mutex _mMis;
    std::unordered_map<uint32, std::vector<uint32>> _eleccion;
    std::set<uint32> _misionesPedidas;
public:
    void MisionVista(uint32 q)                       // una misión en el registro: su plantilla, para que el cliente la entienda
    {
        {
            std::lock_guard<std::mutex> l(_mMis);
            if (!_misionesPedidas.insert(q).second) return;
        }
        Wr w; w.put<uint32>(q); Srv(0x05C, w.b);
    }
private:

    // icono de diálogo 3.3.5 -> acción de PNJ 3.4.3 (el icono lo deduce el cliente de la acción)
    static uint8 AccionGossip(uint8 icono)
    {
        switch (icono)
        {
            case 1: return 1;   // vendedor
            case 2: return 2;   // vuelo
            case 3: return 3;   // entrenador
            case 5: return 5;   // posada
            case 6: return 6;   // banquero
            case 8: return 8;   // tabardos
            case 9: return 9;   // campos de batalla
            default: return 0;
        }
    }

    // SMSG_GOSSIP_MESSAGE (AC PlayerMenu::SendGossipMenu)
    void Gossip335(Rd& r)
    {
        WorldPackets::NPC::GossipMessage p;
        p.GossipGUID = mundo->ToTc(r.get<uint64>());
        p.GossipID = int32(r.get<uint32>());
        p.TextID = int32(r.get<uint32>());
        uint32 n = r.get<uint32>();
        std::vector<std::string> textos; textos.reserve(n * 2);   // las opciones guardan string_view
        struct Op { uint32 id; uint8 icono, codigo; uint32 dinero; };
        std::vector<Op> ops;
        for (uint32 i = 0; i < n && r.p < r.b.size(); ++i)
        {
            Op o; o.id = r.get<uint32>(); o.icono = r.get<uint8>(); o.codigo = r.get<uint8>(); o.dinero = r.get<uint32>();
            textos.push_back(r.cstr()); textos.push_back(r.cstr());
            ops.push_back(o);
        }
        for (size_t i = 0; i < ops.size(); ++i)
        {
            WorldPackets::NPC::ClientGossipOptions g;
            g.GossipOptionID = int32(ops[i].id);
            g.OptionNPC = GossipOptionNpc(AccionGossip(ops[i].icono));
            g.OptionFlags = ops[i].codigo;
            g.OptionCost = int32(ops[i].dinero);
            g.OrderIndex = int32(i);
            g.Text = textos[i * 2];
            g.Confirm = textos[i * 2 + 1];
            p.GossipOptions.push_back(g);
        }
        uint32 nq = r.get<uint32>();
        for (uint32 i = 0; i < nq && r.p < r.b.size(); ++i)
            p.GossipText.push_back(TextoMision(r));
        Enviar(p.Write());
    }
    // misión en una lista (diálogo o lista del que da misiones): u32 id, u32 icono, i32 nivel, u32 banderas, u8 repetible, cstr título
    static WorldPackets::NPC::ClientGossipText TextoMision(Rd& r)
    {
        WorldPackets::NPC::ClientGossipText q;
        q.QuestID = int32(r.get<uint32>()); q.QuestType = int32(r.get<uint32>()); q.QuestLevel = int32(r.get<uint32>());
        q.QuestFlags[0] = int32(r.get<uint32>()); q.Repeatable = r.get<uint8>() != 0; q.QuestTitle = r.cstr();
        q.QuestMaxScalingLevel = 255;
        return q;
    }

    // CMSG_QUERY_NPC_TEXT: el 3.4.3 pide identificadores de BroadcastText; están en acore_world.npc_text
    void TextoPNJ(uint32 id)
    {
        WorldPackets::Query::QueryNPCTextResponse p;
        p.TextID = id;
        Db db;
        if (db.Open())
        {
            // por cada texto: probabilidad, BroadcastTextID y, si no tiene, el que le dio gen_textos.py (worldgate.npc_text_bt)
            std::string q = "SELECT ";
            for (int i = 0; i < 8; ++i)
                q += (i ? "," : "") + std::string("Probability") + std::to_string(i) + ",BroadcastTextID" + std::to_string(i)
                   + ",(SELECT b.BroadcastTextID FROM worldgate.npc_text_bt b WHERE b.ID = n.ID AND b.idx = " + std::to_string(i) + ")";
            q += " FROM " + Cfg::WorldDbName + ".npc_text n WHERE n.ID = " + std::to_string(id);
            if (!mysql_query(db.h, q.c_str()))
                if (MYSQL_RES* res = mysql_store_result(db.h))
                {
                    if (MYSQL_ROW row = mysql_fetch_row(res))
                        for (int i = 0; i < 8; ++i)
                        {
                            p.Probabilities[i] = row[i * 3] ? std::stof(row[i * 3]) : 0.f;
                            p.BroadcastTextID[i] = row[i * 3 + 1] ? uint32(std::stoul(row[i * 3 + 1])) : 0;
                            if (!p.BroadcastTextID[i] && row[i * 3 + 2])
                                p.BroadcastTextID[i] = uint32(std::stoul(row[i * 3 + 2]));
                            if (p.BroadcastTextID[i]) p.Allow = true;
                        }
                    mysql_free_result(res);
                }
        }
        if (!p.Allow) Log("[%s] npc_text %u sin BroadcastText: el cliente 3.4.3 no tendrá texto", acct.c_str(), id);
        Enviar(p.Write());
    }

    // SMSG_QUESTGIVER_QUEST_LIST: u64, cstr saludo, u32 retardo, u32 emote, u8 n, misiones
    void QuestList335(Rd& r)
    {
        WorldPackets::Quest::QuestGiverQuestListMessage p;
        p.QuestGiverGUID = mundo->ToTc(r.get<uint64>());
        p.Greeting = r.cstr(); p.GreetEmoteDelay = r.get<uint32>(); p.GreetEmoteType = r.get<uint32>();
        uint8 n = r.get<uint8>();
        for (uint8 i = 0; i < n && r.p < r.b.size(); ++i) p.QuestDataText.push_back(TextoMision(r));
        Enviar(p.Write());
    }

    // recompensas comunes: u32 n elegir (id, cant, modelo)*, u32 n fijos (id, cant, modelo)*, u32 dinero, u32 xp
    void Recompensas(Rd& r, WorldPackets::Quest::QuestRewards& rw, uint32 quest, bool guardarEleccion, bool conXp = true)
    {
        uint32 n = r.get<uint32>();
        std::vector<uint32> elegir;
        for (uint32 i = 0; i < n && r.p + 12 <= r.b.size(); ++i)
        {
            uint32 id = r.get<uint32>(), c = r.get<uint32>(); r.get<uint32>();
            if (i < rw.ChoiceItems.size()) { rw.ChoiceItems[i].Item.ItemID = id; rw.ChoiceItems[i].Quantity = int32(c); }
            elegir.push_back(id);
        }
        rw.ChoiceItemCount = int32(n);
        uint32 m = r.get<uint32>();
        for (uint32 i = 0; i < m && r.p + 12 <= r.b.size(); ++i)
        {
            uint32 id = r.get<uint32>(), c = r.get<uint32>(); r.get<uint32>();
            if (i < rw.ItemID.size()) { rw.ItemID[i] = int32(id); rw.ItemQty[i] = int32(c); }
        }
        rw.ItemCount = int32(m);
        rw.Money = int32(r.get<uint32>());
        if (conXp) rw.XP = int32(r.get<uint32>());
        if (guardarEleccion) { std::lock_guard<std::mutex> l(_mMis); _eleccion[quest] = elegir; }
    }
    // cola de recompensas: u32 honor, float, [u32 0x08 en la oferta], u32 hechizo, i32 hechizo lanzado, u32 título, u32 talentos, u32 arena, u32, 5 facciones, 5 valores, 5 ajustes
    static void RecompensasCola(Rd& r, WorldPackets::Quest::QuestRewards& rw, bool oferta)
    {
        rw.Honor = int32(r.get<uint32>()); r.get<float>();
        if (oferta) r.get<uint32>();
        uint32 sp = r.get<uint32>(); int32 cast = int32(r.get<uint32>());
        rw.SpellCompletionDisplayID[0] = int32(sp); rw.SpellCompletionID = cast;
        rw.Title = int32(r.get<uint32>()); r.get<uint32>(); r.get<uint32>(); r.get<uint32>();
        for (int i = 0; i < 5; ++i) rw.FactionID[i] = int32(r.get<uint32>());
        for (int i = 0; i < 5; ++i) rw.FactionValue[i] = int32(r.get<uint32>());
        for (int i = 0; i < 5; ++i) rw.FactionOverride[i] = int32(r.get<uint32>());
    }

    // SMSG_QUESTGIVER_QUEST_DETAILS (AC PlayerMenu::SendQuestGiverQuestDetails)
    void QuestDetails335(Rd& r)
    {
        WorldPackets::Quest::QuestGiverQuestDetails p;
        p.QuestGiverGUID = mundo->ToTc(r.get<uint64>());
        p.InformUnit = mundo->ToTc(r.get<uint64>());
        p.QuestID = int32(r.get<uint32>());
        p.QuestTitle = r.cstr(); p.DescriptionText = r.cstr(); p.LogDescription = r.cstr();
        p.AutoLaunched = r.get<uint8>() != 0;
        p.QuestFlags[0] = r.get<uint32>();
        p.SuggestedPartyMembers = int32(r.get<uint32>());
        p.StartCheat = r.get<uint8>() != 0;
        Recompensas(r, p.Rewards, uint32(p.QuestID), false);
        RecompensasCola(r, p.Rewards, false);
        uint32 ne = r.get<uint32>();
        for (uint32 i = 0; i < ne && r.p + 8 <= r.b.size(); ++i) { int32 e = int32(r.get<uint32>()); uint32 dl = r.get<uint32>(); p.DescEmotes.emplace_back(e, dl); }
        {
            std::lock_guard<std::mutex> l(_mMis);
            auto it = _objetivos.find(uint32(p.QuestID));
            if (it != _objetivos.end())
                for (QuestObjective const& o : it->second) { WorldPackets::Quest::QuestObjectiveSimple s; s.ID = int32(o.ID); s.ObjectID = o.ObjectID; s.Amount = o.Amount; s.Type = o.Type; p.Objectives.push_back(s); }
        }
        p.DisplayPopup = true;
        Enviar(p.Write());
    }

    std::unordered_map<uint32, std::vector<QuestObjective>> _objetivos;   // bajo _mMis

    // SMSG_QUEST_QUERY_RESPONSE (AC PlayerMenu::SendQuestQueryResponse) -> SMSG_QUERY_QUEST_INFO_RESPONSE
    void QuestQuery335(Rd& r)
    {
        WorldPackets::Quest::QueryQuestInfoResponse p;
        auto& q = p.Info;
        q.QuestID = int32(r.get<uint32>()); p.QuestID = uint32(q.QuestID); p.Allow = true;
        q.QuestType = int32(r.get<uint32>()); q.QuestLevel = int32(r.get<uint32>()); q.QuestMinLevel = int32(r.get<uint32>());
        q.QuestSortID = int32(r.get<uint32>()); q.QuestInfoID = int32(r.get<uint32>()); q.SuggestedGroupNum = int32(r.get<uint32>());
        int32 repFac = int32(r.get<uint32>()), repVal = int32(r.get<uint32>()); r.get<uint32>(); r.get<uint32>();
        q.RewardNextQuest = int32(r.get<uint32>()); q.RewardXPDifficulty = int32(r.get<uint32>());
        q.RewardMoney = int32(r.get<uint32>()); q.RewardBonusMoney = int32(r.get<uint32>());
        q.RewardDisplaySpell[0] = int32(r.get<uint32>()); q.RewardSpell = int32(r.get<uint32>());
        q.RewardHonor = int32(r.get<uint32>()); q.RewardKillHonor = r.get<float>();
        q.StartItem = int32(r.get<uint32>()); q.Flags = r.get<uint32>(); q.RewardTitle = int32(r.get<uint32>());
        uint32 jugadores = r.get<uint32>(); r.get<uint32>(); q.RewardArenaPoints = int32(r.get<uint32>()); r.get<uint32>();
        for (int i = 0; i < 4; ++i) { q.RewardItems[i] = int32(r.get<uint32>()); q.RewardAmount[i] = int32(r.get<uint32>()); }
        for (int i = 0; i < 6; ++i) { q.UnfilteredChoiceItems[i].ItemID = int32(r.get<uint32>()); q.UnfilteredChoiceItems[i].Quantity = int32(r.get<uint32>()); }
        for (int i = 0; i < 5; ++i) q.RewardFactionID[i] = int32(r.get<uint32>());
        for (int i = 0; i < 5; ++i) q.RewardFactionValue[i] = int32(r.get<uint32>());
        for (int i = 0; i < 5; ++i) q.RewardFactionOverride[i] = int32(r.get<uint32>());
        q.POIContinent = int32(r.get<uint32>()); q.POIx = r.get<float>(); q.POIy = r.get<float>(); q.POIPriority = int32(r.get<uint32>());
        q.LogTitle = r.cstr(); q.LogDescription = r.cstr(); q.QuestDescription = r.cstr(); q.AreaDescription = r.cstr(); q.QuestCompletionLog = r.cstr();
        int32 npcgo[4]; uint32 cuenta[4];
        for (int i = 0; i < 4; ++i)
        {
            uint32 e = r.get<uint32>(); npcgo[i] = (e & 0x80000000) ? -int32(e & 0x7FFFFFFF) : int32(e);
            cuenta[i] = r.get<uint32>(); q.ItemDrop[i] = int32(r.get<uint32>()); q.ItemDropQuantity[i] = int32(r.get<uint32>());
        }
        uint32 item[6], itemc[6];
        for (int i = 0; i < 6; ++i) { item[i] = r.get<uint32>(); itemc[i] = r.get<uint32>(); }
        std::string texto[4];
        for (int i = 0; i < 4; ++i) texto[i] = r.cstr();
        // objetivos 3.4.3: criaturas/objetos en los índices 0-3 (los contadores del registro 3.3.5), objetos de inventario 4-9
        std::vector<QuestObjective> objs;
        auto nuevo = [&](uint8 tipo, int8 idx, int32 obj, int32 cant, std::string const& desc)
        {
            QuestObjective o; o.ID = uint32(q.QuestID) * 16 + uint32(idx < 0 ? 15 : idx); o.QuestID = uint32(q.QuestID);
            o.Type = tipo; o.StorageIndex = idx; o.ObjectID = obj; o.Amount = cant; o.Description = desc;
            objs.push_back(o);
        };
        for (int i = 0; i < 4; ++i)
            if (npcgo[i]) nuevo(npcgo[i] > 0 ? QUEST_OBJECTIVE_MONSTER : QUEST_OBJECTIVE_GAMEOBJECT, int8(i), std::abs(npcgo[i]), int32(cuenta[i]), texto[i]);
        for (int i = 0; i < 6; ++i)
            if (item[i]) nuevo(QUEST_OBJECTIVE_ITEM, int8(4 + i), int32(item[i]), int32(itemc[i]), "");
        if (repFac) nuevo(QUEST_OBJECTIVE_MIN_REPUTATION, -1, repFac, repVal, "");
        if (jugadores) nuevo(QUEST_OBJECTIVE_PLAYERKILLS, 10, 0, int32(jugadores), "");
        q.Objectives = objs;
        {
            std::lock_guard<std::mutex> l(_mMis);
            _objetivos[uint32(q.QuestID)] = objs;
        }
        Enviar(p.Write());
    }

    // SMSG_QUESTGIVER_REQUEST_ITEMS (AC PlayerMenu::SendQuestGiverRequestItems)
    void RequestItems335(Rd& r)
    {
        WorldPackets::Quest::QuestGiverRequestItems p;
        p.QuestGiverGUID = mundo->ToTc(r.get<uint64>());
        p.QuestID = int32(r.get<uint32>());
        p.QuestTitle = r.cstr(); p.CompletionText = r.cstr();
        p.CompEmoteDelay = int32(r.get<uint32>()); p.CompEmoteType = int32(r.get<uint32>());
        p.AutoLaunched = r.get<uint32>() != 0;
        p.QuestFlags[0] = r.get<uint32>(); p.SuggestPartyMembers = int32(r.get<uint32>());
        p.MoneyToGet = int32(r.get<uint32>());
        uint32 n = r.get<uint32>();
        for (uint32 i = 0; i < n && r.p + 12 <= r.b.size(); ++i) { int32 id = int32(r.get<uint32>()), c = int32(r.get<uint32>()); r.get<uint32>(); p.Collect.emplace_back(id, c); }
        uint32 puede = r.get<uint32>();
        p.StatusFlags = puede ? 0xDF : 0xDB;         // bit 0x04: se puede entregar (TrinityCore usa 0xDB / 0xDF)
        Enviar(p.Write());
    }

    // SMSG_QUESTGIVER_OFFER_REWARD (AC PlayerMenu::SendQuestGiverOfferReward)
    void OfferReward335(Rd& r)
    {
        WorldPackets::Quest::QuestGiverOfferRewardMessage p;
        auto& d = p.QuestData;
        d.QuestGiverGUID = mundo->ToTc(r.get<uint64>());
        d.QuestID = int32(r.get<uint32>());
        p.QuestTitle = r.cstr(); p.RewardText = r.cstr();
        d.AutoLaunched = r.get<uint8>() != 0;
        d.QuestFlags[0] = int32(r.get<uint32>()); d.SuggestedPartyMembers = int32(r.get<uint32>());
        uint32 ne = r.get<uint32>();
        for (uint32 i = 0; i < ne && r.p + 8 <= r.b.size(); ++i) { uint32 dl = r.get<uint32>(); int32 e = int32(r.get<uint32>()); d.Emotes.emplace_back(e, dl); }
        Recompensas(r, d.Rewards, uint32(d.QuestID), true);
        RecompensasCola(r, d.Rewards, true);
        Enviar(p.Write());
    }

    // SMSG_QUEST_POI_QUERY_RESPONSE: u32 n, (u32 misión, u32 manchas, (u32 id, i32 objetivo, u32 mapa, u32 WorldMapArea, u32 planta,
    // u32 prioridad, u32 banderas, u32 puntos, (i32 x, i32 y)*)*)*. El 3.4.3 quiere UiMap: se saca del mapa y el primer punto.
    void QuestPOI335(Rd& r)
    {
        std::vector<QuestPOIData> datos;
        uint32 n = r.get<uint32>();
        for (uint32 i = 0; i < n && r.p + 8 <= r.b.size(); ++i)
        {
            QuestPOIData d; d.QuestID = int32(r.get<uint32>());
            uint32 nb = r.get<uint32>();
            for (uint32 k = 0; k < nb && r.p + 32 <= r.b.size(); ++k)
            {
                QuestPOIBlobData bl;
                bl.BlobIndex = int32(r.get<uint32>()); bl.ObjectiveIndex = int32(r.get<uint32>());
                bl.MapID = int32(r.get<uint32>()); r.get<uint32>(); r.get<uint32>();
                bl.Priority = int32(r.get<uint32>()); bl.Flags = int32(r.get<uint32>());
                uint32 np = r.get<uint32>();
                for (uint32 j = 0; j < np && r.p + 8 <= r.b.size(); ++j)
                {
                    int32 x = int32(r.get<uint32>()), y = int32(r.get<uint32>());
                    bl.Points.emplace_back(x, y, 0);
                }
                if (bl.ObjectiveIndex >= 0 && bl.ObjectiveIndex <= 9)
                    bl.QuestObjectiveID = d.QuestID * 16 + bl.ObjectiveIndex;
                if (!bl.Points.empty())
                {
                    int32 ui = 0;
                    if (DB2Manager::GetUiMapPosition(float(bl.Points[0].X), float(bl.Points[0].Y), 0.f, bl.MapID, 0, 0, 0, UI_MAP_SYSTEM_WORLD, false, &ui))
                        bl.UiMapID = ui;
                    else Log("[%s] marca de mision %d/%d: sin UiMap para mapa %d (%d, %d)", acct.c_str(), d.QuestID, bl.BlobIndex, bl.MapID, bl.Points[0].X, bl.Points[0].Y);
                }
                d.Blobs.push_back(std::move(bl));
            }
            datos.push_back(std::move(d));
        }
        WorldPackets::Query::QuestPOIQueryResponse p;
        for (auto const& d : datos) p.QuestPOIDataStats.push_back(&d);
        Enviar(p.Write());
    }

    // SMSG_INSPECT_TALENT: packguid, u32 puntos libres, u8 grupos, u8 activo, por grupo (u8 n, (u32 talento, u8 rango)*, u8 n, u16 glifo*),
    // u32 máscara de huecos, por hueco (u32 objeto, u16 máscara de encantamientos, u16 encantamiento*, i16 prop, packguid creador, u32 sufijo).
    // Los talentos del grupo activo van en InspectResult::TalentRanks (formato 3.4.3 de Wrathion, 71 huecos).
    void Inspeccion335(Rd& r)
    {
        uint64 g = Pack(r);
        WorldPackets::Inspect::InspectResult p;
        r.get<uint32>();
        uint8 grupos = r.get<uint8>(); uint8 activo = r.get<uint8>();
        for (uint8 k = 0; k < grupos && r.p < r.b.size(); ++k)
        {
            uint8 n = r.get<uint8>();
            for (uint8 i = 0; i < n; ++i)
            {
                uint32 t = r.get<uint32>(); uint8 rango = r.get<uint8>();
                if (k == activo && i < p.TalentRanks.size()) { p.TalentRanks[i].Id = t; p.TalentRanks[i].Rank = rango; }
            }
            uint8 ng = r.get<uint8>(); r.p += size_t(ng) * 2;
        }
        if (!mundo->FichaJugador(g, p.DisplayInfo)) p.DisplayInfo.GUID = mundo->ToTc(g);
        p.DisplayInfo.Name = NombreDe(g);
        uint32 mascara = r.get<uint32>();
        for (uint8 i = 0; i < EQUIPMENT_SLOT_END && r.p < r.b.size(); ++i)
        {
            if (!(mascara & (1u << i))) continue;
            Gate<Item> tmp; tmp.Init(ObjectGuid::Create<HighGuid::Item>(i + 1));
            tmp.Set(tmp.m_values.ModifyValue(&Object::m_objectData).ModifyValue(&UF::ObjectData::EntryID), int32(r.get<uint32>()));
            uint16 em = r.get<uint16>();
            for (uint8 j = 0; j < 12; ++j)
                if (em & (1u << j))
                {
                    uint16 e = r.get<uint16>();
                    uint8 j343 = j < 7 ? j : uint8(j + 1);   // AC PROP 7-11 -> 3.4.3 8-12 (7 = USE_ENCHANTMENT_SLOT)
                    if (j343 < MAX_ENCHANTMENT_SLOT)
                        tmp.Set(tmp.m_values.ModifyValue(&Item::m_itemData).ModifyValue(&UF::ItemData::Enchantment, j343).ModifyValue(&UF::ItemEnchantment::ID), int32(e));
                }
            tmp.Set(tmp.m_values.ModifyValue(&Item::m_itemData).ModifyValue(&UF::ItemData::RandomPropertiesID), int32(r.get<int16>()));
            uint64 creador = Pack(r);
            r.get<uint32>();
            if (creador) tmp.Set(tmp.m_values.ModifyValue(&Item::m_itemData).ModifyValue(&UF::ItemData::Creator), mundo->ToTc(creador));
            p.DisplayInfo.Items.emplace_back(&tmp, i);
        }
        Enviar(p.Write());
    }

    // ---- subastas
    std::mutex _mSub;
    uint64 _subastador = 0;
    uint32 _casaSubastas = 1;
    struct SubastaTc
    {
        uint32 id = 0, entry = 0, cant = 0, cargas = 0, pujaMin = 0, incremento = 0, compra = 0, ms = 0, puja = 0;
        int32 prop = 0;
        uint64 dueno = 0, pujador = 0;
        uint32 ench[7][3] = {};
    };
    // subasta 3.3.5 (AuctionEntry::BuildAuctionInfo): u32 id, u32 objeto, 7 x (u32 enc, u32 dur, u32 cargas), i32 prop, u32 sufijo,
    // u32 cant, u32 cargas, u32 0, u64 dueño, u32 puja inicial, u32 incremento, u32 compra, u32 ms, u64 pujador, u32 puja
    static SubastaTc LeerSubasta(Rd& r)
    {
        SubastaTc a;
        a.id = r.get<uint32>(); a.entry = r.get<uint32>();
        for (auto& e : a.ench) { e[0] = r.get<uint32>(); e[1] = r.get<uint32>(); e[2] = r.get<uint32>(); }
        a.prop = int32(r.get<uint32>()); r.get<uint32>();
        a.cant = r.get<uint32>(); a.cargas = r.get<uint32>(); r.get<uint32>();
        a.dueno = r.get<uint64>(); a.pujaMin = r.get<uint32>(); a.incremento = r.get<uint32>(); a.compra = r.get<uint32>();
        a.ms = r.get<uint32>(); a.pujador = r.get<uint64>(); a.puja = r.get<uint32>();
        return a;
    }
    // AuctionItem 3.4.3 (Wrathion / TrinityCore AuctionHousePackets.cpp): sin datos de servidor (CensorServerSideInfo)
    void EscribirSubasta(WorldPacket& w, SubastaTc const& a)
    {
        std::vector<WorldPackets::Item::ItemEnchantData> enc;
        for (uint8 i = 0; i < 7; ++i) if (a.ench[i][0]) enc.emplace_back(int32(a.ench[i][0]), a.ench[i][1], int32(a.ench[i][2]), i);
        bool pujado = a.pujador != 0;
        w.WriteBit(true);                                // Item
        w.WriteBits(enc.size(), 4);
        w.WriteBits(0, 2);                               // gemas
        w.WriteBit(a.pujaMin != 0); w.WriteBit(a.incremento != 0); w.WriteBit(a.compra != 0); w.WriteBit(false);   // MinBid, MinIncrement, Buyout, UnitPrice
        w.WriteBit(true);                                // CensorServerSideInfo
        w.WriteBit(false);                               // CensorBidInfo
        w.WriteBit(false); w.WriteBit(false);            // BucketKey, Creator
        w.WriteBit(pujado); w.WriteBit(pujado);          // Bidder, BidAmount
        w.FlushBits();
        WorldPackets::Item::ItemInstance it; it.ItemID = a.entry; it.RandomPropertiesID = a.prop;
        w << it;
        w << int32(a.cant) << int32(a.cargas) << int32(0) << int32(a.id);
        w << (a.dueno ? mundo->ToTc(a.dueno) : ObjectGuid::Empty);
        w << int32(a.ms) << uint8(0);
        for (auto const& e : enc) w << e;
        if (a.pujaMin) w << uint64(a.pujaMin);
        if (a.incremento) w << uint64(a.incremento);
        if (a.compra) w << uint64(a.compra);
        if (pujado) { w << mundo->ToTc(a.pujador); w << uint64(a.puja); }
    }

    // ---- campos de batalla
    struct ColaBg { uint32 tipo = 0; uint8 arena = 0; bool puntuada = false; time_t desde = 0; };
    std::mutex _mBg;
    std::map<uint32, ColaBg> _colasBg;                       // hueco de cola 3.3.5 -> campo de batalla
    uint64 _maestroBatalla = 0;

    WorldPackets::LFG::RideTicket TicketBg(uint32 hueco, time_t desde)
    {
        WorldPackets::LFG::RideTicket t;
        t.RequesterGuid = Yo(); t.Id = hueco; t.Type = WorldPackets::LFG::RideType::Battlegrounds; t.Time = desde;
        return t;
    }
    static constexpr uint32 kTicketBatalla = 0x80000000;
    uint32 _batallaCola = 0;                                 // Conquista del Invierno en cola / invitada
    WorldPackets::Battleground::BattlefieldStatusHeader CabeceraBatalla(uint32 id)
    {
        WorldPackets::Battleground::BattlefieldStatusHeader h;
        h.Ticket = TicketBg(kTicketBatalla | id, time(nullptr));
        h.QueueID.push_back(MAKE_PAIR64(id | 0x20000, 0x1F100000));   // Battlefield::GetQueueId de TrinityCore / Wrathion
        h.RangeMin = 1; h.RangeMax = 80; h.TeamSize = 0; h.InstanceID = 0;
        return h;
    }
    static bool EsListaConquista(uint32 lista)
    {
        BattlemasterListEntry const* e = sBattlemasterListStore.LookupEntry(lista);
        if (!e) return false;
        for (int16 m : e->MapID) if (m == 571 || m == 2118) return true;   // 2118 = mapa propio de Conquista del Invierno en 3.4.3 (lista 1089)
        return false;
    }
    static uint64 ColaId(ColaBg const& c)
    {
        BattlegroundQueueTypeId q{ uint16(c.tipo), uint8(c.arena ? 1 : 0), c.puntuada, uint8(c.arena) };
        return q.GetPacked();
    }

    // SMSG_BATTLEFIELD_STATUS: u32 hueco, [u64 0 si nada] | u8 arena, u8, u32 tipo, u16, u8 nivel mín, u8 máx, u32 instancia, u8 puntuada,
    // u32 estado (1 en cola: u32 media, u32 esperando | 2 invitación: u32 mapa, u64, u32 ms | 3 dentro: u32 mapa, u64, u32 cierre, u32 empezada, u8 facción)
    void EstadoBg335(Rd& r)
    {
        uint32 hueco = r.get<uint32>();
        if (r.b.size() <= 12)                            // STATUS_NONE: u32 hueco, u64 0
        {
            ColaBg c;
            { std::lock_guard<std::mutex> l(_mBg); auto it = _colasBg.find(hueco); if (it != _colasBg.end()) { c = it->second; _colasBg.erase(it); } }
            WorldPackets::Battleground::BattlefieldStatusNone p; p.Ticket = TicketBg(hueco, c.desde);
            Enviar(p.Write());
            return;
        }
        uint8 arena = r.get<uint8>(); r.get<uint8>();
        ColaBg c; c.arena = arena; c.tipo = r.get<uint32>(); r.get<uint16>();
        uint8 nmin = r.get<uint8>(), nmax = r.get<uint8>(); uint32 inst = r.get<uint32>(); c.puntuada = r.get<uint8>() != 0;
        uint32 estado = r.get<uint32>();
        {
            std::lock_guard<std::mutex> l(_mBg);
            auto it = _colasBg.find(hueco);
            c.desde = it != _colasBg.end() && it->second.tipo == c.tipo ? it->second.desde : time(nullptr);
            _colasBg[hueco] = c;
        }
        WorldPackets::Battleground::BattlefieldStatusHeader h;
        h.Ticket = TicketBg(hueco, c.desde);
        h.QueueID.push_back(ColaId(c));
        h.RangeMin = nmin; h.RangeMax = nmax; h.TeamSize = arena; h.InstanceID = inst; h.RegisteredMatch = c.puntuada;
        if (estado == 1)
        {
            WorldPackets::Battleground::BattlefieldStatusQueued p;
            p.Hdr = h; p.AverageWaitTime = r.get<uint32>(); p.WaitTime = r.get<uint32>(); p.EligibleForMatchmaking = true;
            Enviar(p.Write());
        }
        else if (estado == 2)
        {
            WorldPackets::Battleground::BattlefieldStatusNeedConfirmation p;
            p.Hdr = h; p.Mapid = r.get<uint32>(); r.get<uint64>(); p.Timeout = r.get<uint32>();
            Enviar(p.Write());
        }
        else if (estado == 3)
        {
            WorldPackets::Battleground::BattlefieldStatusActive p;
            p.Hdr = h; p.Mapid = r.get<uint32>(); r.get<uint64>(); p.ShutdownTimer = r.get<uint32>(); p.StartTimer = r.get<uint32>();
            p.ArenaFaction = r.get<uint8>();
            Enviar(p.Write());
        }
    }

    // MSG_PVP_LOG_DATA: u8 arena, [2 x (u32, u32, u32), 2 x cstr], u8 terminada, [u8 ganador], u32 n,
    // (u64, u32 golpes, arena: u8 equipo | u32 muertes honorables, u32 muertes, u32 honor extra; u32 daño, u32 sanación, u32 n, u32*)*
    void Marcador335(Rd& r)
    {
        WorldPackets::Battleground::PVPMatchStatisticsMessage p;
        uint8 arena = r.get<uint8>();
        if (arena) { for (int i = 0; i < 6; ++i) r.get<uint32>(); r.cstr(); r.cstr(); }
        if (r.get<uint8>()) p.Data.Winner = r.get<uint8>();   // 3.4.3 lo manda (Wrathion)
        uint32 n = r.get<uint32>();
        for (uint32 i = 0; i < n && r.p + 8 <= r.b.size(); ++i)
        {
            WorldPackets::Battleground::PVPMatchStatistics::PVPMatchPlayerStatistics e;
            uint64 g = r.get<uint64>();
            e.PlayerGUID = mundo->ToTc(g); e.Kills = r.get<uint32>();
            uint8 raza = 0, clase = 0, sexo = 0;
            bool visto = mundo->RazaClaseSexo(g, raza, clase, sexo);
            if (arena) e.Faction = r.get<uint8>();
            else
            {
                e.Honor.emplace();
                e.Honor->HonorKills = r.get<uint32>(); e.Honor->Deaths = r.get<uint32>(); e.Honor->ContributionPoints = r.get<uint32>();
                if (ChrRacesEntry const* cr = visto ? sChrRacesStore.LookupEntry(raza) : nullptr) e.Faction = cr->Alliance == 0 ? 1 : 0;
            }
            e.DamageDone = r.get<uint32>(); e.HealingDone = r.get<uint32>();
            uint32 ns = r.get<uint32>();
            for (uint32 k = 0; k < ns && r.p + 4 <= r.b.size(); ++k) e.Stats.emplace_back(int32(k), int32(r.get<uint32>()));
            e.IsInWorld = true; e.Race = raza; e.Class = clase; e.Sex = int8(sexo);
            if (e.Faction < 2) ++p.Data.PlayerCount[e.Faction];
            p.Data.Statistics.push_back(e);
        }
        Enviar(p.Write());
    }

    // ---- buscador de mazmorras
    // ticket de GM de AC (un solo ticket abierto por jugador)
    std::mutex _mTicket;
    struct { bool hay = false; bool completado = false; uint32 id = 0; time_t abierto = 0; std::string texto, respuesta; } _gmTicket;

    void CasoTicket()                               // SMSG_GM_TICKET_CASE_STATUS con 0 o 1 caso
    {
        WorldPackets::Ticket::GMTicketCaseStatus p;
        std::lock_guard<std::mutex> l(_mTicket);
        if (_gmTicket.hay)
        {
            WorldPackets::Ticket::GMTicketCaseStatus::GMTicketCase c;
            c.CaseID = int32(_gmTicket.id);
            c.CaseOpened = _gmTicket.abierto;
            c.CaseStatus = _gmTicket.completado ? 4 : 1;          // LE_TICKET_STATUS_RESPONSE / _OPEN (volcado 54261)
            c.CfgRealmID = Cfg::RealmId;                         // el mismo CfgRealmID que en FeatureSystemStatus
            c.CharacterID = yo335;
            c.WaitTimeOverrideMinutes = 0;                       // 0: el cliente pinta waitMessage tal cual (sin format)
            std::string m = _gmTicket.completado ? "Respuesta del GM: " + _gmTicket.respuesta
                                                 : "Ticket #" + std::to_string(_gmTicket.id) + " en espera. Escribe .ayuda ver";
            c.WaitTimeOverrideMessage = m.substr(0, 1000);       // 10 bits
            p.Cases.push_back(std::move(c));
        }
        Enviar(p.Write());
    }
    std::atomic<bool> _verTicket{false};             // lo pidió ".ayuda ver": enseñar el ticket en el chat

    WorldPackets::LFG::RideTicket _ticketLfg;
    WorldPackets::LFG::RideTicket TicketLfg()
    {
        if (_ticketLfg.RequesterGuid.IsEmpty()) { _ticketLfg.RequesterGuid = Yo(); _ticketLfg.Id = 1; _ticketLfg.Type = WorldPackets::LFG::RideType::Lfg; _ticketLfg.Time = time(nullptr); }
        return _ticketLfg;
    }
    static uint8 TipoActualizacionLfg(uint8 t)       // LfgUpdateType 3.3.5 -> 3.4.3
    {
        switch (t)
        {
            case 5: return 6; case 6: return 7; case 7: return 8; case 8: return 9; case 9: return 10; case 10: return 11;
            case 12: return 13; case 13: return 15; case 14: return 16; case 15: return 17; case 16: return 18;
            case 2: return 8; case 3: return 6;       // buscador de bandas: salir / entrar
            default: return t;
        }
    }
    static uint8 ResultadoUnirseLfg(uint32 r)        // LfgJoinResult 3.3.5 -> 3.4.3
    {
        static uint8 const t[18] = { 0x00, 0x2E, 0x1F, 0x00, 0x21, 0x22, 0x06, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D };
        return r < 18 ? t[r] : uint8(r);
    }
    // bloque de bloqueos 3.3.5: u32 n, (u32 mazmorra, u32 motivo)*
    static void LeerBloqueos(Rd& r, std::vector<WorldPackets::LFG::LFGBlackListSlot>& v)
    {
        uint32 n = r.get<uint32>();
        for (uint32 i = 0; i < n && r.p + 8 <= r.b.size(); ++i)
        {
            uint32 slot = r.get<uint32>(), mot = r.get<uint32>();
            if (!sLFGDungeonsStore.HasRecord(slot & 0xFFFFFF)) { Log("[%s] mazmorra %u del buscador no existe en el cliente 3.4.3: se quita", "LFG", slot & 0xFFFFFF); continue; }
            v.emplace_back(slot, mot, 0, 0, 0);
        }
    }
    // SMSG_LFG_UPDATE_PLAYER / PARTY: u8 tipo, u8 extra, [u8 unirse (solo grupo), u8 en cola, u8, u8, (grupo: 3 x u8), u8 n, u32*, cstr]
    void ActualizarLfg335(Rd& r, bool grupo)
    {
        WorldPackets::LFG::LFGUpdateStatus p;
        uint8 tipo335 = r.get<uint8>();
        uint8 tipo = TipoActualizacionLfg(tipo335);
        bool extra = r.get<uint8>() != 0;
        bool unirse = false, cola = false;
        if (extra)
        {
            if (grupo) unirse = r.get<uint8>() != 0;
            cola = r.get<uint8>() != 0; r.get<uint8>(); r.get<uint8>();
            if (grupo) { r.get<uint8>(); r.get<uint8>(); r.get<uint8>(); }
            uint8 n = r.get<uint8>();
            for (uint8 i = 0; i < n && r.p + 4 <= r.b.size(); ++i) { uint32 sl = r.get<uint32>(); if (sLFGDungeonsStore.HasRecord(sl & 0xFFFFFF)) p.Slots.push_back(sl); }
            if (!grupo) unirse = tipo335 == 5 || tipo335 == 12 || tipo335 == 13 || (tipo335 == 14 && !p.Slots.empty());
        }
        if (tipo335 == 5) _ticketLfg = {};              // nueva cola: nuevo tique
        p.Ticket = TicketLfg();
        p.SubType = 1;                                   // LFG_QUEUE_DUNGEON
        p.Reason = tipo; p.IsParty = grupo; p.NotifyUI = true;
        p.Joined = unirse; p.Queued = cola; p.LfgJoined = tipo335 != 7;
        Enviar(p.Write());
    }

    // ---- banco de hermandad: CMSG_GUILD_BANK_SWAP_ITEMS 3.3.5
    // entre huecos del banco: u64, u8 1, u8 pestaña dst, u8 hueco dst, u32 0, u8 pestaña src, u8 hueco src, u32 0, u8 0, u32 cantidad
    void BancoHermandadEntreHuecos(ObjectGuid const& banco, uint8 tabSrc, uint8 slotSrc, uint8 tabDst, uint8 slotDst, uint32 cant)
    {
        Wr w; w.put<uint64>(mundo->A335(banco)).put<uint8>(1).put<uint8>(tabDst).put<uint8>(slotDst).put<uint32>(0)
                 .put<uint8>(tabSrc).put<uint8>(slotSrc).put<uint32>(0).put<uint8>(0).put<uint32>(cant);
        Srv(0x3E9, w.b);
    }
    // con el inventario: u64, u8 0, u8 pestaña, u8 hueco, u32 0, u8 automático, [u32, u8 al jugador, u32] | [u8 bolsa, u8 hueco, u8 al jugador, u32 cantidad]
    void BancoHermandadConInventario(ObjectGuid const& banco, bool alJugador, uint8 tab, uint8 slot, Optional<uint8> bolsa, uint8 hueco, uint32 cant, bool automatico = false)
    {
        Wr w; w.put<uint64>(mundo->A335(banco)).put<uint8>(0).put<uint8>(tab).put<uint8>(slot).put<uint32>(0).put<uint8>(automatico ? 1 : 0);
        if (automatico) w.put<uint32>(0).put<uint8>(1).put<uint32>(0);
        else
        {
            uint8 bo = bolsa.value_or(255);
            w.put<uint8>(Bolsa(bo)).put<uint8>(Hueco(bo, hueco)).put<uint8>(alJugador ? 1 : 0).put<uint32>(cant);
        }
        Srv(0x3E9, w.b);
    }
    // SMSG_GUILD_BANK_LIST: u64 dinero, u8 pestaña, i32 retiradas, u8 completo, [pestaña 0 y completo: u8 n, (cstr, cstr)*],
    // u8 n, (u8 hueco, u32 objeto, [i32 banderas, i32 prop, [i32 semilla], i32 cant, i32 encant., u8 cargas, u8 n, (u8, i32)*])*
    void BancoHermandad335(Rd& r)
    {
        WorldPackets::Guild::GuildBankQueryResults p;
        p.Money = r.get<uint64>(); p.Tab = r.get<uint8>(); p.WithdrawalsRemaining = int32(r.get<uint32>());
        p.FullUpdate = r.get<uint8>() != 0;
        if (p.Tab == 0 && p.FullUpdate)
        {
            uint8 n = r.get<uint8>();
            for (uint8 i = 0; i < n && r.p < r.b.size(); ++i)
            {
                WorldPackets::Guild::GuildBankTabInfo t; t.TabIndex = i; t.Name = r.cstr(); t.Icon = r.cstr();
                p.TabInfo.push_back(t);
            }
        }
        uint8 n = r.get<uint8>();
        for (uint8 i = 0; i < n && r.p + 5 <= r.b.size(); ++i)
        {
            WorldPackets::Guild::GuildBankItemInfo it;
            it.Slot = r.get<uint8>(); it.Item.ItemID = r.get<uint32>();
            if (it.Item.ItemID)
            {
                it.Flags = int32(r.get<uint32>());
                it.Item.RandomPropertiesID = int32(r.get<uint32>());
                if (it.Item.RandomPropertiesID) it.Item.RandomPropertiesSeed = int32(r.get<uint32>());
                it.Count = int32(r.get<uint32>()); it.EnchantmentID = int32(r.get<uint32>()); it.Charges = r.get<uint8>();
                uint8 ns = r.get<uint8>();
                for (uint8 k = 0; k < ns && r.p + 5 <= r.b.size(); ++k) { r.get<uint8>(); r.get<uint32>(); }
            }
            p.ItemInfo.push_back(it);
        }
        Enviar(p.Write());
    }

    // bloque de logros 3.3.5 (AchievementMgr::BuildAllDataPacket): (i32 logro, u32 fecha)* -1,
    // (i32 criterio, packguid cantidad, packguid, u32 banderas, u32 fecha, u32, u32)* -1
    void LeerLogros(Rd& r, WorldPackets::Achievement::AllAchievements& d, ObjectGuid const& dueno)
    {
        while (r.p + 4 <= r.b.size())
        {
            int32 id = int32(r.get<uint32>());
            if (id < 0) break;
            WorldPackets::Achievement::EarnedAchievement e;
            e.Id = uint32(id); e.Date.SetPackedTime(r.get<uint32>());
            e.Owner = dueno; e.VirtualRealmAddress = e.NativeRealmAddress = realmAddress;
            d.Earned.push_back(e);
        }
        while (r.p + 4 <= r.b.size())
        {
            int32 id = int32(r.get<uint32>());
            if (id < 0) break;
            WorldPackets::Achievement::CriteriaProgress c;
            c.Id = uint32(id); c.Quantity = Pack(r); Pack(r);
            c.Player = dueno; c.Flags = r.get<uint32>();
            c.Date.SetPackedTime(r.get<uint32>());
            c.TimeFromStart = Seconds(r.get<uint32>()); c.TimeFromCreate = Seconds(r.get<uint32>());
            d.Progress.push_back(c);
        }
    }

public:
    // ---- disparadores que el cliente 3.4.3 no conoce: los detecta la pasarela
    struct Disparador { uint32 id, mapa; float x, y, z, radio, largo, ancho, alto, giro; };
    std::vector<Disparador> _disparadores;
    bool _disparadoresCargados = false;
    std::set<uint32> _dentroDe;

    void CargarDisparadores()
    {
        _disparadoresCargados = true;
        Db db;
        if (!db.Open()) return;
        if (mysql_query(db.h, ("SELECT entry, map, x, y, z, radius, length, width, height, orientation FROM " + Cfg::WorldDbName + ".areatrigger").c_str())) return;
        MYSQL_RES* res = mysql_store_result(db.h);
        if (!res) return;
        while (MYSQL_ROW row = mysql_fetch_row(res))
        {
            uint32 id = uint32(std::stoul(row[0]));
            if (sAreaTriggerStore.HasRecord(id)) continue;   // el cliente ya lo detecta y lo manda él
            auto f = [&](int i) { return row[i] ? std::stof(row[i]) : 0.f; };
            _disparadores.push_back({ id, uint32(std::stoul(row[1])), f(2), f(3), f(4), f(5), f(6), f(7), f(8), f(9) });
        }
        mysql_free_result(res);
        Log("[%s] %u disparadores de área los vigila la pasarela (el cliente 3.4.3 no los tiene)", acct.c_str(), (uint32)_disparadores.size());
    }

    void ComprobarDisparadores(uint32 mapa, float px, float py, float pz)
    {
        if (!_disparadoresCargados) CargarDisparadores();
        for (Disparador const& d : _disparadores)
        {
            bool dentro = false;
            if (d.mapa == mapa)
            {
                if (d.radio > 0.f)
                {
                    float dx = px - d.x, dy = py - d.y, dz = pz - d.z;
                    dentro = dx * dx + dy * dy + dz * dz <= d.radio * d.radio;
                }
                else                                     // caja girada (Position::IsWithinBox)
                {
                    double rot = 2 * M_PI - d.giro, sn = std::sin(rot), cs = std::cos(rot);
                    float bx = px - d.x, by = py - d.y;
                    float lx = float(bx * cs - by * sn), ly = float(by * cs + bx * sn);
                    dentro = std::fabs(lx) <= d.largo / 2 && std::fabs(ly) <= d.ancho / 2 && std::fabs(pz - d.z) <= d.alto / 2;
                }
            }
            if (dentro)
            {
                if (_dentroDe.insert(d.id).second) { Wr w; w.put<uint32>(d.id); Srv(0x0B4, w.b); }
            }
            else
                _dentroDe.erase(d.id);
        }
    }
private:

    // SMSG_WHO: u32 mostrados, u32 coincidencias, (cstr nombre, cstr hermandad, u32 nivel, u32 clase, u32 raza, u8 sexo, u32 zona)*
    // El 3.3.5 no manda guid: se busca por nombre en la base de personajes (el cliente la usa para susurrar, invitar...)
    void Who335(Rd& r)
    {
        WorldPackets::Who::WhoResponsePkt p;
        p.RequestID = _whoPeticion;
        uint32 n = r.get<uint32>(); r.get<uint32>();
        std::vector<std::string> nombres;
        for (uint32 i = 0; i < n && r.p < r.b.size(); ++i)
        {
            WorldPackets::Who::WhoEntry e;
            e.PlayerData.Name = r.cstr(); e.GuildName = r.cstr();
            e.PlayerData.Level = uint8(r.get<uint32>()); e.PlayerData.ClassID = uint8(r.get<uint32>());
            e.PlayerData.Race = uint8(r.get<uint32>()); e.PlayerData.Sex = r.get<uint8>();
            e.AreaID = int32(r.get<uint32>());
            e.PlayerData.VirtualRealmAddress = realmAddress;
            e.PlayerData.AccountID = e.PlayerData.BnetAccountID = ObjectGuid::Create<HighGuid::WowAccount>(0);
            if (!e.GuildName.empty()) { e.GuildGUID = ObjectGuid::Create<HighGuid::Guild>(1); e.GuildVirtualRealmAddress = realmAddress; }
            nombres.push_back(e.PlayerData.Name);
            p.Response.Entries.push_back(e);
        }
        if (!nombres.empty())
        {
            Db db;
            if (db.Open())
            {
                std::string q = "SELECT name, guid FROM " + Cfg::CharDbName + ".characters WHERE name IN (";
                for (size_t i = 0; i < nombres.size(); ++i) q += (i ? ",'" : "'") + db.Esc(nombres[i]) + "'";
                q += ")";
                std::unordered_map<std::string, uint64> guids;
                if (!mysql_query(db.h, q.c_str()))
                    if (MYSQL_RES* res = mysql_store_result(db.h))
                    {
                        while (MYSQL_ROW row = mysql_fetch_row(res)) if (row[0] && row[1]) guids[row[0]] = std::stoull(row[1]);
                        mysql_free_result(res);
                    }
                for (auto& e : p.Response.Entries)
                {
                    auto it = guids.find(e.PlayerData.Name);
                    if (it != guids.end()) { e.PlayerData.GuidActual = ObjectGuid::Create<HighGuid::Player>(it->second); Nombre(it->second, e.PlayerData.Name); }
                }
            }
        }
        Enviar(p.Write());
    }

    // ---- botín: el 3.4.3 identifica cada botín con una guid propia (LootObject); el 3.3.5, con la del dueño
    std::mutex _mLoot;
    ObjectGuid _lootObj;
    uint64 _lootDueno = 0;
    uint32 _lootSerie = 0;
    std::unordered_map<uint64, ObjectGuid> _tiradas;
    ObjectGuid _grupoTc;                                     // guid 3.4.3 del grupo actual (SMSG_PARTY_UPDATE)
    bool _enGrupo = false;                                   // AC no contesta los cambios de dificultad sin grupo
    uint32 _whoPeticion = 0;                                 // RequestID del último /who
    uint32 _comercioEstado = 0;                              // índice de estado del comercio (el cliente 3.4.3 lo devuelve al aceptar)
    // conjuntos de equipo: máscara de huecos ignorados por conjunto (el 3.3.5 los manda como guid 1) y el último usado
    std::mutex _mConj;
    std::unordered_map<uint64, uint32> _conjIgnorar;
    uint64 _conjUsado = 0;          // guid 3.3.5 del botín en tirada -> LootObject 3.4.3

    ObjectGuid TiradaObj(uint64 src)
    {
        std::lock_guard<std::mutex> l(_mLoot);
        auto it = _tiradas.find(src);
        if (it != _tiradas.end()) return it->second;
        if (_tiradas.size() > 256) _tiradas.clear();
        return _tiradas[src] = ObjectGuid::Create<HighGuid::LootObject>(uint16(mundo->mapa), 0, ++_lootSerie);
    }
    uint64 TiradaSrc(ObjectGuid const& g)
    {
        std::lock_guard<std::mutex> l(_mLoot);
        for (auto const& [s, o] : _tiradas) if (o == g) return s;
        return g == _lootObj ? _lootDueno : 0;
    }

    // SMSG_LOOT_RESPONSE: u64 dueño, u8 tipo, u32 dinero, u8 n, (u8 hueco, u32 objeto, u32 cant, u32 modelo, u32 sufijo, u32 prop, u8 tipo hueco)*
    void Loot335(Rd& r)
    {
        WorldPackets::Loot::LootResponse p;
        uint64 dueno = r.get<uint64>();
        uint8 tipo = r.get<uint8>();
        p.Owner = mundo->ToTc(dueno);
        {
            std::lock_guard<std::mutex> l(_mLoot);
            _lootObj = ObjectGuid::Create<HighGuid::LootObject>(uint16(mundo->mapa), 0, ++_lootSerie);
            _lootDueno = dueno;
            p.LootObj = _lootObj;
        }
        p.AcquireReason = tipo;
        if (tipo == 0)                                   // LOOT_NONE: error
        {
            p.FailureReason = r.p < r.b.size() ? r.get<uint8>() : 0;
            Enviar(p.Write());
            return;
        }
        p.Coins = r.get<uint32>();
        uint8 n = r.get<uint8>();
        for (uint8 i = 0; i < n && r.p + 22 <= r.b.size(); ++i)
        {
            WorldPackets::Loot::LootItemData it;
            it.LootListID = r.get<uint8>();
            it.Loot.ItemID = r.get<uint32>(); it.Quantity = r.get<uint32>(); r.get<uint32>(); r.get<uint32>();
            it.Loot.RandomPropertiesID = int32(r.get<uint32>());
            it.UIType = r.get<uint8>();
            p.Items.push_back(it);
        }
        p.Acquired = true;
        p._LootMethod = 0;
        Enviar(p.Write());
    }

    // SMSG_TRAINER_LIST: u64, i32 tipo, i32 n, (i32 hechizo, u8 estado, i32 coste, 2 x i32, u8 nivel, i32 hab., i32 rango, 3 x i32)*, cstr saludo
    void Trainer335(Rd& r)
    {
        WorldPackets::NPC::TrainerList p;
        p.TrainerGUID = mundo->ToTc(r.get<uint64>());
        p.TrainerType = int32(r.get<uint32>());
        p.TrainerID = 1;
        uint32 n = r.get<uint32>();
        for (uint32 i = 0; i < n && r.p + 34 <= r.b.size(); ++i)
        {
            WorldPackets::NPC::TrainerListSpell s;
            s.SpellID = int32(r.get<uint32>());
            // Trainer::SpellState: AC 0 disponible, 1 no disponible, 2 aprendido; TC 3.4.3 0 aprendido, 1 disponible, 2 no disponible
            static uint8 const estado[3] = { 1, 2, 0 };
            uint8 e = r.get<uint8>(); s.Usable = e < 3 ? estado[e] : 2;
            s.MoneyCost = r.get<uint32>();
            r.get<uint32>(); r.get<uint32>();
            s.ReqLevel = r.get<uint8>(); s.ReqSkillLine = r.get<uint32>(); s.ReqSkillRank = r.get<uint32>();
            for (int k = 0; k < 3; ++k) s.ReqAbility[k] = int32(r.get<uint32>());
            p.Spells.push_back(s);
        }
        p.Greeting = r.cstr();
        Enviar(p.Write());
    }

    void Abrir(uint64 g, PlayerInteractionType tipo)
    {
        WorldPackets::NPC::NPCInteractionOpenResult p;
        p.Npc = mundo->ToTc(g); p.InteractionType = tipo; p.Success = true;
        Enviar(p.Write());
    }

    // ---- mascotas
    std::mutex _mPet;
    std::unordered_map<uint32, ObjectGuid> _mascotasPorNumero;

    // SMSG_PET_SPELLS: u64 (0 = sin mascota) | u64, u16 familia, u32 duración, u8 reacción, u8 orden, u16 banderas,
    // 10 x u32 barra, u8 n, u32 hechizos*, u8 n, (u32 hechizo, u16 categoría, u32 recarga, u32 recarga de categoría)*
    // botones de mascota/posesión/vehículo: AC empaqueta acción | tipo << 24 (CharmInfo.h); el 3.4.3 lee acción & 0x7FFFFF y el
    // tipo desde el bit 23 (Wrathion UnitDefines.h: ENABLED = 0xC0800000, DISABLED = 0x80800000, PASSIVE = 0x00800000,
    // COMMAND = 0x03800000, REACTION = 0x03000000). Copiados tal cual el cliente no los reconoce como hechizo: barra vacía
    // (Ojo de Acherus, vehículos, atacar/seguir/quieto)
    static uint32 BotonA343(uint32 v) { uint32 t = v >> 24; return (v & 0x007FFFFF) | ((t & 0xC0u) << 24) | ((t & 0x3Fu) << 23); }
    static uint32 BotonA335(uint32 v) { uint32 t = ((v >> 24) & 0xC0u) | ((v >> 23) & 0x3Fu); return (v & 0x007FFFFF) | (t << 24); }

    void PetSpells335(Rd& r, size_t tam)
    {
        uint64 g = r.get<uint64>();
        if (!g || tam <= 8)
        {
            WorldPackets::Pet::PetSpells vacio;          // como Player::PetSpellInitialize/StopCastingCharm de TC: lista vacía
            Enviar(vacio.Write());
            return;
        }
        WorldPackets::Pet::PetSpells p;
        p.PetGUID = mundo->ToTc(g);
        p._CreatureFamily = r.get<uint16>(); p.TimeLimit = r.get<uint32>();
        p.ReactState = r.get<uint8>(); p.CommandState = r.get<uint8>(); p.Flag = uint8(r.get<uint16>());
        for (int i = 0; i < 10; ++i) p.ActionButtons[i] = int(BotonA343(r.get<uint32>()));
        uint8 n = r.get<uint8>();
        for (uint8 i = 0; i < n && r.p + 4 <= r.b.size(); ++i) p.Actions.push_back(BotonA343(r.get<uint32>()));
        uint8 nc = r.p < r.b.size() ? r.get<uint8>() : 0;
        for (uint8 i = 0; i < nc && r.p + 14 <= r.b.size(); ++i)
        {
            WorldPackets::Pet::PetSpellCooldown c;
            c.SpellID = int32(r.get<uint32>()); c.Category = r.get<uint16>(); c.Duration = int32(r.get<uint32>()); c.CategoryDuration = int32(r.get<uint32>());
            p.Cooldowns.push_back(c);
        }
        Enviar(p.Write());
    }

    // ---- hermandades: nombres de rango (de la consulta) y permisos (de la lista) para SMSG_GUILD_RANKS
    std::mutex _mGuild;
    std::vector<std::string> _rangosNombre;
    std::vector<std::pair<uint32, uint32>> _rangosPermisos;   // (derechos, oro por día)
    uint32 _guildId = 0;

    // SMSG_GUILD_QUERY_RESPONSE: u32 id, cstr nombre, 10 x cstr rango, u32 x5 tabardo, u32 n rangos
    void GuildQuery335(Rd& r)
    {
        WorldPackets::Guild::QueryGuildInfoResponse p;
        uint32 id = r.get<uint32>();
        p.GuildGuid = ObjectGuid::Create<HighGuid::Guild>(id);
        p.Info.emplace();
        auto& in = *p.Info;
        in.GuildGUID = p.GuildGuid; in.VirtualRealmAddress = realmAddress;
        in.GuildName = r.cstr();
        std::string rangos[10];
        for (auto& s : rangos) s = r.cstr();
        in.EmblemStyle = r.get<uint32>(); in.EmblemColor = r.get<uint32>(); in.BorderStyle = r.get<uint32>();
        in.BorderColor = r.get<uint32>(); in.BackgroundColor = r.get<uint32>();
        uint32 n = r.get<uint32>();
        std::vector<std::string> nombres;
        for (uint32 i = 0; i < n && i < 10; ++i) { in.Ranks.emplace_back(i, i, rangos[i]); nombres.push_back(rangos[i]); }
        { std::lock_guard<std::mutex> l(_mGuild); _rangosNombre = nombres; _guildId = id; }
        Enviar(p.Write());
    }

    void EnviarRangos()
    {
        WorldPackets::Guild::GuildRanks p;
        std::lock_guard<std::mutex> l(_mGuild);
        for (size_t i = 0; i < _rangosNombre.size(); ++i)
        {
            WorldPackets::Guild::GuildRankData d;
            d.RankID = uint8(i); d.RankOrder = int32(i); d.RankName = _rangosNombre[i];
            if (i < _rangosPermisos.size()) { d.Flags = _rangosPermisos[i].first; d.WithdrawGoldLimit = _rangosPermisos[i].second; }
            for (uint32 k = 0; k < GUILD_BANK_MAX_TABS; ++k) { d.TabFlags[k] = 0; d.TabWithdrawItemLimit[k] = 0; }
            p.Ranks.push_back(d);
        }
        Enviar(p.Write());
    }

    // SMSG_GUILD_ROSTER: u32 n, cstr MOTD, cstr info, u32 n rangos, (u32 derechos, u32 oro, 6 x (u32, u32))*,
    // miembros (u64, u8 conectado, cstr, u32 rango, u8 nivel, u8 clase, u8 sexo, u32 zona, [float si desconectado], cstr, cstr)
    void GuildRoster335(Rd& r)
    {
        WorldPackets::Guild::GuildRoster p;
        uint32 n = r.get<uint32>();
        p.WelcomeText = r.cstr(); p.InfoText = r.cstr();
        uint32 nr = r.get<uint32>();
        std::vector<std::pair<uint32, uint32>> permisos;
        for (uint32 i = 0; i < nr && r.p + 56 <= r.b.size(); ++i)
        {
            uint32 der = r.get<uint32>(), oro = r.get<uint32>();
            for (int k = 0; k < 12; ++k) r.get<uint32>();
            permisos.emplace_back(der, oro);
        }
        { std::lock_guard<std::mutex> l(_mGuild); _rangosPermisos = permisos; }
        for (uint32 i = 0; i < n && r.p < r.b.size(); ++i)
        {
            WorldPackets::Guild::GuildRosterMemberData m;
            uint64 g = r.get<uint64>();
            m.Guid = mundo->ToTc(g);
            m.Status = r.get<uint8>();
            m.Name = r.cstr();
            m.RankID = int32(r.get<uint32>()); m.Level = r.get<uint8>(); m.ClassID = r.get<uint8>(); m.Gender = r.get<uint8>();
            { std::lock_guard<std::mutex> l(_mGuild); _rangoMiembro[g] = m.RankID; }
            m.AreaID = int32(r.get<uint32>());
            if (!m.Status) m.LastSave = r.get<float>();
            m.Note = r.cstr(); m.OfficerNote = r.cstr();
            m.VirtualRealmAddress = realmAddress; m.Authenticated = true;
            if (!m.Name.empty()) Nombre(g, m.Name);
            p.MemberData.push_back(m);
        }
        p.NumAccounts = int32(p.MemberData.size());
        Enviar(p.Write());
    }

    // SMSG_GUILD_EVENT: u8 tipo, u8 n, cstr*, [u64 si entra/sale/conecta/desconecta]
    void GuildEvent335(Rd& r, size_t tam)
    {
        uint8 tipo = r.get<uint8>(), n = r.get<uint8>();
        std::vector<std::string> par;
        for (uint8 i = 0; i < n && r.p < tam; ++i) par.push_back(r.cstr());
        uint64 g = r.p + 8 <= tam ? r.get<uint64>() : 0;
        auto P = [&](size_t i) { return i < par.size() ? par[i] : std::string(); };
        switch (tipo)
        {
            case 2: { WorldPackets::Guild::GuildEventMotd p; p.MotdText = P(0); Enviar(p.Write()); break; }
            case 3: { WorldPackets::Guild::GuildEventPlayerJoined p; p.Guid = mundo->ToTc(g); p.Name = P(0); p.VirtualRealmAddress = realmAddress; Enviar(p.Write()); break; }
            case 4: case 5:
            {
                WorldPackets::Guild::GuildEventPlayerLeft p;
                p.Removed = tipo == 5; p.LeaverName = P(0); p.LeaverGUID = mundo->ToTc(g); p.LeaverVirtualRealmAddress = realmAddress;
                if (p.Removed) { p.RemoverName = P(1); p.RemoverVirtualRealmAddress = realmAddress; }
                Enviar(p.Write());
                break;
            }
            case 7:
            {
                WorldPackets::Guild::GuildEventNewLeader p;
                p.OldLeaderName = P(0); p.NewLeaderName = P(1);
                p.OldLeaderVirtualRealmAddress = realmAddress; p.NewLeaderVirtualRealmAddress = realmAddress;
                Enviar(p.Write());
                break;
            }
            case 8: { WorldPackets::Guild::GuildEventDisbanded p; Enviar(p.Write()); break; }
            case 12: case 13:
            {
                WorldPackets::Guild::GuildEventPresenceChange p;
                p.Guid = mundo->ToTc(g); p.Name = P(0); p.LoggedOn = tipo == 12; p.VirtualRealmAddress = realmAddress;
                Enviar(p.Write());
                break;
            }
            case 0: case 1: case 10: case 11: { WorldPackets::Guild::GuildEventRanksUpdated p; Enviar(p.Write()); break; }
            default: break;
        }
    }

    // ---- correo
    std::mutex _mMail;
    uint64 _buzon = 0;

    // SMSG_MAIL_LIST_RESULT (AC HandleGetMailList): se escribe a mano con el formato de TrinityCore
    // (sus MailListEntry/MailAttachedItem solo se construyen a partir de Mail*/Item*)
    void MailList335(Rd& r)
    {
        uint32 total = r.get<uint32>();
        uint8 n = r.get<uint8>();
        ByteBuffer cartas;
        uint32 contadas = 0;
        for (uint8 i = 0; i < n && r.p < r.b.size(); ++i)
        {
            r.get<uint16>();
            uint32 id = r.get<uint32>();
            uint8 tipo = r.get<uint8>();
            ObjectGuid remitente; int32 alt = 0;
            if (tipo == 0) remitente = mundo->ToTc(r.get<uint64>());
            else alt = int32(r.get<uint32>());
            uint32 cod = r.get<uint32>(); r.get<uint32>(); int32 papel = int32(r.get<uint32>()); uint32 dinero = r.get<uint32>();
            int32 banderas = int32(r.get<uint32>()); float dias = r.get<float>(); int32 plantilla = int32(r.get<uint32>());
            std::string asunto = r.cstr(), cuerpo = r.cstr();
            uint8 ni = r.get<uint8>();
            ByteBuffer adjuntos;
            for (uint8 k = 0; k < ni && r.p < r.b.size(); ++k)
            {
                uint8 pos = r.get<uint8>(); uint32 guidBajo = r.get<uint32>(); uint32 entrada = r.get<uint32>();
                std::vector<WorldPackets::Item::ItemEnchantData> enc;
                for (int j = 0; j < 7; ++j)
                {
                    uint32 eid = r.get<uint32>(), dur = r.get<uint32>(), car = r.get<uint32>();
                    if (eid) enc.emplace_back(int32(eid), dur, int32(car), uint8(j));
                }
                int32 prop = int32(r.get<uint32>()); r.get<uint32>();
                int32 cuenta = int32(r.get<uint32>()), cargas = int32(r.get<uint32>());
                uint32 maxDur = r.get<uint32>(); int32 dur = int32(r.get<uint32>()); r.get<uint8>();
                WorldPackets::Item::ItemInstance inst; inst.ItemID = entrada; inst.RandomPropertiesID = prop;
                adjuntos << uint8(pos) << uint64(guidBajo) << int32(cuenta) << int32(cargas) << uint32(maxDur) << int32(dur);
                adjuntos << inst;
                adjuntos.WriteBits(enc.size(), 4); adjuntos.WriteBits(0, 2); adjuntos.WriteBit(true); adjuntos.FlushBits();
                for (auto const& e : enc) adjuntos << e;
            }
            cartas << uint64(id) << uint32(tipo) << uint64(cod) << int32(papel) << uint64(dinero) << int32(banderas) << float(dias) << int32(plantilla) << uint32(ni);
            if (tipo == 0) cartas << remitente; else cartas << int32(alt);
            cartas.WriteBits(asunto.size(), 8); cartas.WriteBits(cuerpo.size(), 13); cartas.FlushBits();
            cartas.append(adjuntos);
            cartas.WriteString(asunto); cartas.WriteString(cuerpo);
            ++contadas;
        }
        WorldPacket pkt(SMSG_MAIL_LIST_RESULT, 8 + cartas.size());
        pkt << uint32(contadas) << int32(total);
        pkt.append(cartas);
        Enviar(&pkt);
    }

    // ---- vuelos y duelos
    std::mutex _mTaxi;
    uint32 _nodoActual = 0;
    std::vector<uint32> _taxiConocidos;              // máscara 3.3.5 de nodos conocidos (bit n-1)

    bool NodoConocido(uint32 n)
    {
        std::lock_guard<std::mutex> l(_mTaxi);
        uint32 c = (n - 1) / 32;
        return n && c < _taxiConocidos.size() && (_taxiConocidos[c] & (1u << ((n - 1) % 32)));
    }

    // Dijkstra sobre TaxiPath.db2 (como TaxiPathGraph de TrinityCore): nodos intermedios solo si el personaje los conoce
    std::vector<uint32> RutaTaxi(uint32 desde, uint32 hasta)
    {
        TaxiNodesEntry const* a = sTaxiNodesStore.LookupEntry(desde);
        TaxiNodesEntry const* z = sTaxiNodesStore.LookupEntry(hasta);
        if (!a || !z || desde == hasta) return {};
        static std::unordered_map<uint32, std::vector<uint32>> const vecinos = []
        {
            std::unordered_map<uint32, std::vector<uint32>> v;
            for (TaxiPathEntry const* p : sTaxiPathStore) v[p->FromTaxiNode].push_back(p->ToTaxiNode);
            return v;
        }();
        auto dist = [](TaxiNodesEntry const* x, TaxiNodesEntry const* y)
        {
            float dx = x->Pos.X - y->Pos.X, dy = x->Pos.Y - y->Pos.Y, dz = x->Pos.Z - y->Pos.Z;
            return std::sqrt(dx * dx + dy * dy + dz * dz);
        };
        std::unordered_map<uint32, float> d; std::unordered_map<uint32, uint32> previo;
        using E = std::pair<float, uint32>;
        std::priority_queue<E, std::vector<E>, std::greater<E>> cola;
        d[desde] = 0; cola.push({ 0.0f, desde });
        while (!cola.empty())
        {
            auto [c, n] = cola.top(); cola.pop();
            if (n == hasta) break;
            if (c > d[n]) continue;
            auto it = vecinos.find(n);
            if (it == vecinos.end()) continue;
            TaxiNodesEntry const* en = sTaxiNodesStore.LookupEntry(n);
            for (uint32 m : it->second)
            {
                TaxiNodesEntry const* em = sTaxiNodesStore.LookupEntry(m);
                if (!en || !em || em->ContinentID != a->ContinentID) continue;
                if (m != hasta && !NodoConocido(m)) continue;
                float nc = c + dist(en, em);
                auto f = d.find(m);
                if (f == d.end() || nc < f->second) { d[m] = nc; previo[m] = n; cola.push({ nc, m }); }
            }
        }
        if (!previo.count(hasta)) return {};
        std::vector<uint32> ruta{ hasta };
        for (uint32 n = hasta; n != desde; n = previo[n]) ruta.push_back(previo[n]);
        std::reverse(ruta.begin(), ruta.end());
        return ruta;
    }
    ObjectGuid _arbitroDuelo;

    // SMSG_CONTACT_LIST: u32 banderas, u32 n, (u64, u32 banderas, cstr nota, [u8 estado, [u32 zona, u32 nivel, u32 clase]])*
    void Contactos335(Rd& r)
    {
        WorldPackets::Social::ContactList p;
        p.Flags = r.get<uint32>();
        uint32 n = r.get<uint32>();
        for (uint32 i = 0; i < n && r.p < r.b.size(); ++i)
        {
            uint64 g = r.get<uint64>();
            uint32 fl = r.get<uint32>();
            std::string nota = r.cstr();
            uint8 st = 0; uint32 area = 0, nivel = 0, clase = 0;
            if (fl & 0x1) { st = r.get<uint8>(); if (st) { area = r.get<uint32>(); nivel = r.get<uint32>(); clase = r.get<uint32>(); } }
            // ContactInfo no tiene constructor por defecto: se construye con datos vacíos y se rellena
            FriendInfo fi;
            WorldPackets::Social::ContactInfo c(mundo->ToTc(g), fi);
            c.WowAccountGuid = ObjectGuid::Create<HighGuid::WowAccount>(0);
            c.VirtualRealmAddr = realmAddress; c.NativeRealmAddr = realmAddress;
            c.TypeFlags = fl; c.Notes = nota; c.Status = st; c.AreaID = area; c.Level = nivel; c.ClassID = clase;
            p.Contacts.push_back(c);
        }
        Enviar(p.Write());
    }

    // ---- grupos
    std::mutex _mGrupo;
    std::unordered_map<uint64, WorldPackets::Party::PartyMemberStats> _miembros;
    std::unordered_map<uint64, int32> _rangoMiembro;                          // rango de hermandad de cada miembro (lista)   // último estado conocido de cada miembro
    int32 _secGrupo = 0;

    ObjectGuid GuidPorNombre(std::string const& n)
    {
        std::lock_guard<std::mutex> l(_mNombres);
        for (auto const& [g, nom] : _nombres) if (nom == n) return ObjectGuid::Create<HighGuid::Player>(uint32(g));
        return ObjectGuid::Empty;
    }
    std::string NombreDe(uint64 g)
    {
        std::lock_guard<std::mutex> l(_mNombres);
        auto it = _nombres.find(g);
        return it != _nombres.end() ? it->second : std::string();
    }

    // SMSG_GROUP_LIST (AC Group::SendUpdateToPlayer). En 3.4.3 la lista incluye al propio jugador
    void GroupList335(Rd& r)
    {
        WorldPackets::Party::PartyUpdate p;
        uint8 tipo = r.get<uint8>(), miSub = r.get<uint8>(), misFlags = r.get<uint8>(), misRoles = r.get<uint8>();
        if (tipo & 0x08) { r.get<uint8>(); r.get<uint32>(); }
        uint64 gGrupo = r.get<uint64>();
        r.get<uint32>();
        uint32 n = r.get<uint32>();
        p.PartyGUID = ObjectGuid::Create<HighGuid::Party>(gGrupo & 0xFFFFFFFF);
        _grupoTc = p.PartyGUID;
        p.PartyIndex = 0;
        p.SequenceNum = ++_secGrupo;
        if (n == 0 && (tipo & 0x10 || gGrupo == 0))      // sin grupo
        {
            p.PartyFlags = 0x10;                         // GROUP_FLAG_DESTROYED
            p.MyIndex = -1;
            _enGrupo = false;
            Enviar(p.Write());
            return;
        }
        _enGrupo = true;
        p.PartyType = 1;                                 // GROUP_TYPE_NORMAL
        p.PartyFlags = tipo & (0x02 | 0x04 | 0x08);
        auto clase = [&](uint64 g) { return M335::Octeto(mundo->V335(g, M335::U_BYTES_0), 1); };
        WorldPackets::Party::PartyPlayerInfo yo;
        yo.GUID = Yo(); yo.Name = NombreDe(yo335); yo.Class = clase(yo335); yo.Subgroup = miSub; yo.Flags = misFlags;
        yo.RolesAssigned = misRoles; yo.Connected = true; yo.FactionGroup = 0;
        p.PlayerList.push_back(yo);
        for (uint32 i = 0; i < n && r.p < r.b.size(); ++i)
        {
            WorldPackets::Party::PartyPlayerInfo m;
            m.Name = r.cstr();
            uint64 g = r.get<uint64>();
            m.GUID = mundo->ToTc(g);
            m.Connected = (r.get<uint8>() & 0x01) != 0;
            m.Subgroup = r.get<uint8>(); m.Flags = r.get<uint8>(); m.RolesAssigned = r.get<uint8>();
            m.Class = clase(g);
            if (!m.Name.empty()) Nombre(g, m.Name);
            p.PlayerList.push_back(m);
        }
        p.LeaderGUID = mundo->ToTc(r.get<uint64>());
        p.MyIndex = 0;
        if (n && r.p < r.b.size())
        {
            p.LootSettings.emplace();
            p.LootSettings->Method = r.get<uint8>(); p.LootSettings->LootMaster = mundo->ToTc(r.get<uint64>()); p.LootSettings->Threshold = r.get<uint8>();
            p.DifficultySettings.emplace();
            uint8 dung = r.get<uint8>(), raid = r.get<uint8>();
            p.DifficultySettings->DungeonDifficultyID = dung + 1u;           // 0/1 -> Normal(1)/Heroico(2)
            p.DifficultySettings->RaidDifficultyID = raid + 3u;              // 0..3 -> 10N(3) 25N(4) 10H(5) 25H(6)
            p.DifficultySettings->LegacyRaidDifficultyID = raid + 3u;
        }
        Enviar(p.Write());
    }

    // SMSG_PARTY_MEMBER_STATS(_FULL): [u8], packguid, u32 máscara, campos por bit. 3.4.3 solo tiene el estado completo: se acumula
    void MemberStats335(Rd& r, bool completo)
    {
        if (completo) r.get<uint8>();
        uint8 m = r.get<uint8>(); uint64 g = 0;
        for (int i = 0; i < 8; ++i) if (m & (1 << i)) g |= uint64(r.get<uint8>()) << (i * 8);
        uint32 f = r.get<uint32>();
        WorldPackets::Party::PartyMemberFullState p;
        {
            std::lock_guard<std::mutex> l(_mGrupo);
            auto& s = _miembros[g];
            if (f & 0x1) s.Status = r.get<uint16>();
            if (f & 0x2) s.CurrentHealth = int32(r.get<uint32>());
            if (f & 0x4) s.MaxHealth = int32(r.get<uint32>());
            if (f & 0x8) s.PowerType = r.get<uint8>();
            if (f & 0x10) s.CurrentPower = r.get<uint16>();
            if (f & 0x20) s.MaxPower = r.get<uint16>();
            if (f & 0x40) s.Level = r.get<uint16>();
            if (f & 0x80) s.ZoneID = r.get<uint16>();
            if (f & 0x100) { s.PositionX = int16(r.get<uint16>()); s.PositionY = int16(r.get<uint16>()); }
            if (f & 0x200)
            {
                uint64 am = r.get<uint64>();
                s.Auras.clear();
                for (int i = 0; i < 64; ++i)
                    if (am & (uint64(1) << i))
                    {
                        WorldPackets::Party::PartyMemberAuraStates a; a.SpellID = int32(r.get<uint32>()); r.get<uint8>();
                        a.Flags = 0x0001; a.ActiveFlags = 1;
                        if (a.SpellID) s.Auras.push_back(a);
                    }
            }
            if (f & 0x400) { uint64 pg = r.get<uint64>(); if (pg) { if (!s.PetStats) s.PetStats.emplace(); s.PetStats->GUID = mundo->ToTc(pg); } else s.PetStats.reset(); }
            if (f & 0x800) { std::string nn = r.cstr(); if (s.PetStats) s.PetStats->Name = nn; }
            if (f & 0x1000) { int16 mdl = int16(r.get<uint16>()); if (s.PetStats) s.PetStats->ModelId = mdl; }
            if (f & 0x2000) { int32 h = int32(r.get<uint32>()); if (s.PetStats) s.PetStats->CurrentHealth = h; }
            if (f & 0x4000) { int32 h = int32(r.get<uint32>()); if (s.PetStats) s.PetStats->MaxHealth = h; }
            if (f & 0x8000) r.get<uint8>();
            if (f & 0x10000) r.get<uint16>();
            if (f & 0x20000) r.get<uint16>();
            if (f & 0x40000) { uint64 am = r.get<uint64>(); for (int i = 0; i < 64; ++i) if (am & (uint64(1) << i)) { r.get<uint32>(); r.get<uint8>(); } }
            if (f & 0x80000) s.VehicleSeat = int32(r.get<uint32>());
            s.Phases.PhaseShiftFlags = 0x08;
            p.MemberStats = s;
        }
        p.MemberGuid = mundo->ToTc(g);
        Enviar(p.Write());
    }

    std::mutex _mNombres;
    std::unordered_map<uint64, std::string> _nombres;
    std::unordered_map<uint64, std::vector<WorldPackets::Chat::Chat>> _pendientes;

    void Sistema(std::string const& texto)
    {
        WorldPackets::Chat::Chat c;
        c.SlashCmd = CHAT_MSG_SYSTEM;
        c.ChatText = texto;
        Enviar(c.Write());
    }

    // tipo de chat 3.3.5 -> 3.4.3 (iguales hasta 0x2B; los de campo de batalla pasan a "instancia")
    static uint8 TipoChat(uint8 t)
    {
        switch (t)
        {
            case 0x2C: return CHAT_MSG_INSTANCE_CHAT;
            case 0x2D: return CHAT_MSG_INSTANCE_CHAT_LEADER;
            case 0x2E: return CHAT_MSG_RESTRICTED;
            case 0x2F: return CHAT_MSG_BATTLENET;
            case 0x30: return CHAT_MSG_ACHIEVEMENT;
            case 0x31: return CHAT_MSG_GUILD_ACHIEVEMENT;
            case 0x32: return CHAT_MSG_ARENA_POINTS;
            case 0x33: return CHAT_MSG_PARTY_LEADER;
            default:   return t;
        }
    }

    // SMSG_MESSAGECHAT (AC ChatHandler::BuildChatPacket)
    void Chat335(Rd& r, bool gm)
    {
        WorldPackets::Chat::Chat c;
        uint8 tipo = r.get<uint8>();
        c.SlashCmd = TipoChat(tipo);
        c._Language = r.get<uint32>();
        uint64 emisor = r.get<uint64>();
        r.get<uint32>();
        uint64 receptor = 0;
        switch (tipo)
        {
            case 0x0C: case 0x0D: case 0x0E: case 0x0F: case 0x10: case 0x29: case 0x2A: case 0x2F:
                r.get<uint32>(); c.SenderName = r.cstr();
                receptor = r.get<uint64>();
                if (receptor && uint16(receptor >> 48) != 0x0000 && uint16(receptor >> 48) != 0xF140) { r.get<uint32>(); c.TargetName = r.cstr(); }
                break;
            case 0x08:
                r.get<uint32>(); c.SenderName = r.cstr(); receptor = r.get<uint64>();
                break;
            case 0x24: case 0x25: case 0x26:
                receptor = r.get<uint64>();
                if (receptor && uint16(receptor >> 48) != 0x0000) { r.get<uint32>(); c.TargetName = r.cstr(); }
                break;
            case 0x30: case 0x31:
                receptor = r.get<uint64>();
                break;
            default:
                if (gm) { r.get<uint32>(); c.SenderName = r.cstr(); }
                if (tipo == 0x11) c._Channel = r.cstr();
                receptor = r.get<uint64>();
                break;
        }
        r.get<uint32>();
        c.ChatText = r.cstr();
        c._ChatFlags = r.get<uint8>();
        if (tipo == 0x30 || tipo == 0x31) c.AchievementID = r.get<uint32>();
        // LANG_ADDON: en 3.3.5 es 0xFFFFFFFF y el texto va "prefijo\ttexto"; en 3.4.3 es 183 y el prefijo va en su campo
        if (c._Language == 0xFFFFFFFFu)
        {
            size_t tab = c.ChatText.find('\t');
            if (tab == std::string::npos || tab == 0 || tab > 16) return;   // sin prefijo válido: se descarta
            c.Prefix = c.ChatText.substr(0, tab);
            c.ChatText.erase(0, tab + 1);
            if (c.ChatText.size() > 4095) return;                          // ChatText va con 12 bits en SMSG_CHAT
            c._Language = LANG_ADDON;                                        // 183 (SharedDefines.h:1109 de TC 3.4.3)
        }
        c.SenderGUID = mundo->ToTc(emisor);
        c.TargetGUID = mundo->ToTc(receptor);
        bool jugador = emisor && uint16(emisor >> 48) == 0x0000;
        if (jugador)
        {
            c.SenderVirtualAddress = realmAddress; c.TargetVirtualAddress = realmAddress;
            c.SenderAccountGUID = ObjectGuid::Create<HighGuid::WowAccount>(0);
            if (c.SenderName.empty())
            {
                std::lock_guard<std::mutex> l(_mNombres);
                auto it = _nombres.find(emisor);
                if (it != _nombres.end()) c.SenderName = it->second;
                else                                         // nombre desconocido: se pide y el mensaje espera
                {
                    bool pedir = _pendientes.find(emisor) == _pendientes.end();
                    _pendientes[emisor].push_back(c);
                    if (pedir) { Wr w; w.put<uint64>(emisor); Srv(0x050, w.b); }
                    return;
                }
            }
        }
        Enviar(c.Write());
    }

    // SMSG_CHANNEL_NOTIFY: u8 tipo, cstr canal, ...
    void Canal335(Rd& r)
    {
        uint8 tipo = r.get<uint8>();
        std::string canal = r.cstr();
        if (tipo == 0x02)                                // YOU_JOINED: u8 banderas, u32 id, u32 0
        {
            WorldPackets::Channel::ChannelNotifyJoined p;
            p._Channel = canal;
            p._ChannelFlags = r.get<uint8>();
            p.ChatChannelID = int32(r.get<uint32>());
            Enviar(p.Write());
        }
        else if (tipo == 0x03)                           // YOU_LEFT: u32 id, u8 constante
        {
            WorldPackets::Channel::ChannelNotifyLeft p;
            p.Channel = canal;
            p.ChatChannelID = int32(r.get<uint32>());
            Enviar(p.Write());
        }
    }

    // DIALOG_STATUS 3.3.5 -> QuestGiverStatus 3.4.3 (banderas)
    static ::QuestGiverStatus EstadoMision(uint8 s)
    {
        static uint64 const t[] = { 0x0, 0x2, 0x4, 0x8, 0x10, 0x20, 0x100, 0x200, 0x400, 0x800, 0x1000 };
        return ::QuestGiverStatus(s < std::size(t) ? t[s] : 0);
    }

    // SMSG_INITIAL_SPELLS: u8 0, u16 n, (u32 hechizo, u16 0)*, u16 n recargas, ...
    void InitialSpells(Rd& r)
    {
        WorldPackets::Spells::SendKnownSpells p;
        p.InitialLogin = true;
        r.get<uint8>();
        uint16 n = r.get<uint16>();
        for (uint16 i = 0; i < n; ++i) { p.KnownSpells.push_back(r.get<uint32>()); r.get<uint16>(); }
        Enviar(p.Write());
        {
            // colección de monturas (CollectionMgr de TC): AC 3.3.5 solo enseña el hechizo; el cliente 3.4.3 llena el diario de
            // monturas y su información con SMSG_ACCOUNT_MOUNT_UPDATE. Se mandan las de Mount.db2 que ya conoce el personaje.
            MountContainer monturas;
            for (uint32 s : p.KnownSpells) if (sDB2Manager.GetMount(s)) monturas.emplace(s, MOUNT_STATUS_NONE);
            WorldPackets::Misc::AccountMountUpdate m; m.IsFullUpdate = true; m.Mounts = &monturas;
            Enviar(m.Write());
            Log("[%s] %u monturas en la colección", acct.c_str(), (uint32)monturas.size());
        }
        Log("[%s] %u hechizos conocidos", acct.c_str(), (uint32)n);
        // detrás, como Player::SendInitialPacketsBeforeAddToMap de TrinityCore: olvidados, historial, cargas y glifos activos
        { WorldPackets::Spells::SendUnlearnSpells u; Enviar(u.Write()); }
        {
            // 3.3.5: u16 n, (u32 hechizo, u16 objeto, u16 categoría, i32 recarga ms, i32 recarga de categoría ms)*
            WorldPackets::Spells::SendSpellHistory h;
            uint16 nc = r.p + 2 <= r.b.size() ? r.get<uint16>() : 0;
            for (uint16 i = 0; i < nc && r.p + 16 <= r.b.size(); ++i)
            {
                WorldPackets::Spells::SpellHistoryEntry e;
                e.SpellID = r.get<uint32>(); e.ItemID = r.get<uint16>(); e.Category = r.get<uint16>();
                e.RecoveryTime = int32(r.get<uint32>()); e.CategoryRecoveryTime = int32(r.get<uint32>());
                h.Entries.push_back(e);
            }
            Enviar(h.Write());
        }
        { WorldPackets::Spells::SendSpellCharges c; Enviar(c.Write()); }
        { WorldPackets::Talent::ActiveGlyphs g; g.IsFullUpdate = true; Enviar(g.Write()); }
    }

    // SMSG_ACTION_BUTTONS: u8 estado, 144 x u32 (acción | tipo << 24) -> 180 x u64 (acción | tipo << 56)
    void ActionButtons(Rd& r)
    {
        WorldPackets::Spells::UpdateActionButtons p;
        p.Reason = r.get<uint8>();
        if (p.Reason != 2)
            for (int i = 0; i < 144; ++i)
            {
                uint32 v = r.get<uint32>();
                p.ActionButtons[i] = uint64(v);          // acción | tipo << 24, como packedData de Wrathion
            }
        if (p.Reason != 2) { _botones = p.ActionButtons; _hayBotones = true; }   // se reenvían tras crear/recrear al jugador
        Enviar(p.Write());
    }
public:
    // el bloque de creación del jugador trae la barra vacía (el Gate no tiene m_actionButtons) y pisa la que llegó antes
    // SMSG_REQUEST_PVP_REWARDS_RESPONSE (RequestPvPRewardsResponse de Wrathion): 8 x LfgPlayerQuestReward = primera y N-ésima
    // victoria/derrota de aleatorio, puntuado, arenas 2c2/3c3/5c5. LfgPlayerQuestReward: u8 máscara, i32 dinero, i32 xp, u32 n objetos,
    // u32 n monedas, u32 n monedas extra, (i32 moneda, i32 cantidad)*, 4 bits (hechizo, 2 sin uso, honor), [i32 honor].
    // AC solo da los valores de ahora (según haya ganado ya hoy), que van a la primera y a la N-ésima: la ventana elige con
    // HasRandomWinToday de la lista. La arena va como moneda 1900 (CLASSIC_ARENA_POINTS_CURRENCY_ID, battlefieldframe.lua).
    uint32 _bgGanaHonor = 0, _bgGanaArena = 0, _bgPierdeHonor = 0;
    void RecompensasJcJ()
    {
        WorldPacket w(SMSG_REQUEST_PVP_REWARDS_RESPONSE, 8 * 32);
        for (int i = 0; i < 8; ++i)
        {
            bool gana = i == 0 || i == 2, aleatorio = i < 4;
            uint32 honor = !aleatorio ? 0 : gana ? _bgGanaHonor : _bgPierdeHonor;
            bool arena = aleatorio && gana && _bgGanaArena;
            w << uint8(0) << int32(0) << int32(0) << uint32(0) << uint32(arena ? 1 : 0) << uint32(0);
            if (arena) w << int32(1900) << int32(_bgGanaArena);
            w.WriteBit(false); w.WriteBit(false); w.WriteBit(false); w.WriteBit(true); w.FlushBits();
            w << int32(honor * 10);                  // el 3.4.3 lo lee en décimas de honor (Wrathion: HONOR_MUL = 124 * 10)
        }
        Enviar(&w);
    }
    // al entrar: lista de misiones entregadas (CMSG_QUERY_QUESTS_COMPLETED 0x500 -> 0x501 u32 n, u32 misión*)
    void PedirMisionesHechas() { Srv(0x500, {}); }
    void ReenviarBotones()
    {
        if (!_hayBotones) return;
        WorldPackets::Spells::UpdateActionButtons p; p.ActionButtons = _botones; p.Reason = 1;
        Enviar(p.Write());
    }
private:
    std::array<uint64, WorldPackets::Spells::UpdateActionButtons::NumActionButtons> _botones = { };
    bool _hayBotones = false;


    // SMSG_INITIALIZE_FACTIONS: u32 128, (u8 banderas, u32 reputación)*
    void Factions(Rd& r)
    {
        WorldPackets::Reputation::InitializeFactions p;
        uint32 n = r.get<uint32>();
        for (uint32 i = 0; i < n && i < WorldPackets::Reputation::FactionCount; ++i)
        {
            p.FactionFlags[i] = r.get<uint8>();
            p.FactionStandings[i] = int32(r.get<uint32>());
        }
        Enviar(p.Write());
        { WorldPackets::Misc::SetupCurrency c; Enviar(c.Write()); }   // TrinityCore la manda siempre tras las facciones
    }

    // CastID de cada aura (TrinityCore: Aura::GetCastId). El 3.3.5 no lo manda y con la guid vacía el cliente 3.4.3 casca
    // (0x578 al entrar con Piel de demonio puesta). Se guarda uno estable por unidad y casilla mientras el hechizo no cambie.
    std::map<std::pair<ObjectGuid, uint8>, std::pair<uint32, ObjectGuid>> _castIdAuras;
    ObjectGuid CastIdAura(ObjectGuid const& unidad, uint8 casilla, uint32 spell)
    {
        auto& e = _castIdAuras[{ unidad, casilla }];
        if (e.first != spell || e.second.IsEmpty())
        {
            std::lock_guard<std::mutex> l(_mCast);
            e = { spell, ObjectGuid::Create<HighGuid::Cast>(kFuenteNormal, uint16(mundo->mapa), spell, ++_castSerie) };
        }
        return e.second;
    }
    void QuitaCastIdAura(ObjectGuid const& unidad, uint8 casilla) { _castIdAuras.erase({ unidad, casilla }); }

    // SMSG_SPELLLOGEXECUTE: packguid lanzador, u32 hechizo, u32 n efectos, (u32 efecto, u32 n objetivos, datos según el efecto)*
    // Qué escribe cada efecto: Spell::ExecuteLogEffect* de AzerothCore; a qué lista va en 3.4.3: los mismos de TrinityCore.
    // El de interrumpir (68) en 3.4.3 va aparte, en SMSG_SPELL_INTERRUPT_LOG.
    void Ejecucion335(Rd& r)
    {
        ObjectGuid lanzador = mundo->ToTc(Pack(r));
        int32 spell = int32(r.get<uint32>());
        uint32 nEf = r.get<uint32>();
        std::vector<SpellLogEffect> efectos;
        for (uint32 e = 0; e < nEf && r.p + 8 <= r.b.size(); ++e)
        {
            SpellLogEffect le; le.Effect = int32(r.get<uint32>());
            uint32 n = r.get<uint32>();
            for (uint32 i = 0; i < n && r.p < r.b.size(); ++i)
            {
                switch (le.Effect)
                {
                    case 8: case 62:                     // drenar / quemar poder: packguid, u32 puntos, u32 tipo, float
                    {
                        SpellLogEffectPowerDrainParams x; x.Victim = mundo->ToTc(Pack(r)); x.Points = r.get<uint32>(); x.PowerType = r.get<uint32>(); x.Amplitude = r.get<float>();
                        if (!le.PowerDrainTargets) le.PowerDrainTargets.emplace();
                        le.PowerDrainTargets->push_back(x);
                        break;
                    }
                    case 19:                             // ataques extra: packguid, u32
                    {
                        SpellLogEffectExtraAttacksParams x; x.Victim = mundo->ToTc(Pack(r)); x.NumAttacks = r.get<uint32>();
                        if (!le.ExtraAttacksTargets) le.ExtraAttacksTargets.emplace();
                        le.ExtraAttacksTargets->push_back(x);
                        break;
                    }
                    case 68:                             // interrumpir: packguid, u32 hechizo interrumpido
                    {
                        WorldPackets::CombatLog::SpellInterruptLog p;
                        // mismo reparto que Spell::ExecuteLogEffectInterruptCast de TC y Wrathion: InterruptedSpellID = el que interrumpe
                        p.Caster = lanzador; p.Victim = mundo->ToTc(Pack(r)); p.SpellID = int32(r.get<uint32>()); p.InterruptedSpellID = spell;
                        Enviar(p.Write());
                        break;
                    }
                    case 111:                            // durabilidad: packguid, i32 objeto, i32 hueco
                    {
                        SpellLogEffectDurabilityDamageParams x; x.Victim = mundo->ToTc(Pack(r)); x.ItemID = r.get<int32>(); x.Amount = r.get<int32>();
                        if (!le.DurabilityDamageTargets) le.DurabilityDamageTargets.emplace();
                        le.DurabilityDamageTargets->push_back(x);
                        break;
                    }
                    case 24: case 59: case 157:          // crear objeto: u32
                    {
                        SpellLogEffectTradeSkillItemParams x; x.ItemID = int32(r.get<uint32>());
                        if (!le.TradeSkillTargets) le.TradeSkillTargets.emplace();
                        le.TradeSkillTargets->push_back(x);
                        break;
                    }
                    case 101:                            // alimentar mascota: u32 objeto
                    {
                        SpellLogEffectFeedPetParams x; x.ItemID = int32(r.get<uint32>());
                        if (!le.FeedPetTargets) le.FeedPetTargets.emplace();
                        le.FeedPetTargets->push_back(x);
                        break;
                    }
                    default:                             // resucitar, abrir, invocar objeto/mascota, duelo, despedir: packguid
                    {
                        SpellLogEffectGenericVictimParams x; x.Victim = mundo->ToTc(Pack(r));
                        if (!le.GenericVictimTargets) le.GenericVictimTargets.emplace();
                        le.GenericVictimTargets->push_back(x);
                        break;
                    }
                }
            }
            if (le.Effect != 68) efectos.push_back(std::move(le));
        }
        if (efectos.empty()) return;
        WorldPackets::CombatLog::SpellExecuteLog p;
        p.Caster = lanzador; p.SpellID = spell; p.Effects = &efectos;
        Enviar(p.Write());
    }

    // ---- H7e: calendario
    static constexpr time_t CAL_SIN_RESPUESTA = 946684800;           // 01/01/2000 00:00: "aún sin respuesta" en AC y en TC
    uint8 _calNivelMin = 1;                                         // nivel mínimo de la última invitación masiva (CMSG_CALENDAR_COMMUNITY_INVITE)

    // hora empaquetada de AC -> WowTime. Mismo formato de bits; los 2 de flags quedan a -1, como en todo WowTime que manda TC
    static WowTime HoraCal(uint32 empaquetada)
    {
        WowTime t; t.SetPackedTime(empaquetada & 0x1FFFFFFF); t.SetFlags(-1);
        return t;
    }
    // WowTime del cliente -> hora empaquetada que lee AC (ReadPackedTime: año, mes, día, hora y minuto; sin los bits de flags)
    static uint32 HoraA335(WowTime const& t) { return t.GetPackedTime() & 0x1FFFFFFF; }
    // hora unix -> empaquetada en hora local, igual que ByteBuffer::AppendPackedTime de AC
    static uint32 HoraLocal335(time_t t)
    {
        tm lt = TimeBreakdown(t);
        return uint32((lt.tm_year - 100) << 24 | lt.tm_mon << 20 | (lt.tm_mday - 1) << 14 | lt.tm_wday << 11 | lt.tm_hour << 6 | lt.tm_min);
    }
    // dificultad 3.3.5 -> DifficultyID 3.4.3, como SMSG_RAID_INSTANCE_INFO (banda 0..3 -> 10N/25N/10H/25H; mazmorra 0/1 -> normal/heroico).
    // Si el mapa no tiene esa dificultad en 3.4.3 (bandas de 40 o de 20 de classic) se usa la suya por defecto
    static uint32 DificultadCal(uint32 mapa, uint32 d)
    {
        uint32 id = Mundo::EsBanda(mapa) ? d + 3 : d + 1;
        if (!sDB2Manager.GetMapDifficultyData(mapa, Difficulty(id)))
        {
            Difficulty def = DIFFICULTY_NONE;
            if (sDB2Manager.GetDefaultMapDifficulty(mapa, &def)) id = def;
        }
        return id;
    }
    // EventClubID de 3.4.3 = id de la hermandad (Wrathion). 3.3.5 solo lo da en SEND_EVENT; en el resto, si el evento es de
    // hermandad (flags 0x400 evento, 0x40 anuncio) se pone la del propio jugador
    uint64 ClubCal(uint32 flags) { return (flags & 0x440) ? mundo->V335(yo335, M335::P_GUILDID) : 0; }
    // corta a n bytes sin partir un carácter UTF-8
    static std::string RecorteUtf8(std::string t, size_t n)
    {
        if (t.size() <= n) return t;
        size_t i = n;
        while (i > 0 && (uint8(t[i]) & 0xC0) == 0x80) --i;
        t.resize(i);
        return t;
    }

    // SMSG_CALENDAR_SEND_CALENDAR (AC WorldSession::HandleCalendarGetCalendar)
    void Calendario335(Rd& r)
    {
        WorldPackets::Calendar::CalendarSendCalendar p;
        size_t const fin = r.b.size();
        uint32 n = r.get<uint32>();
        for (uint32 i = 0; i < n && r.p < fin; ++i)          // u64 evento, u64 invitación, u8 estado, u8 rango, u8 de hermandad, packguid creador (o remitente)
        {
            WorldPackets::Calendar::CalendarSendCalendarInviteInfo& v = p.Invites.emplace_back();
            v.EventID = r.get<uint64>(); v.InviteID = r.get<uint64>();
            v.Status = r.get<uint8>(); v.Moderator = r.get<uint8>(); v.InviteType = r.get<uint8>();
            v.InviterGuid = mundo->ToTc(Pack(r));
        }
        n = r.get<uint32>();
        for (uint32 i = 0; i < n && r.p < fin; ++i)          // u64 evento, cstr título, u32 tipo, hora, u32 flags, i32 mazmorra, packguid creador
        {
            WorldPackets::Calendar::CalendarSendCalendarEventInfo& e = p.Events.emplace_back();
            e.EventID = r.get<uint64>(); e.EventName = r.cstr(); e.EventType = uint8(r.get<uint32>());
            e.Date = HoraCal(r.get<uint32>()); e.Flags = r.get<uint32>(); e.TextureID = int32(r.get<uint32>());
            e.OwnerGuid = mundo->ToTc(Pack(r));
            e.EventClubID = ClubCal(e.Flags);
        }
        r.get<uint32>();                                      // hora del servidor (unix)
        p.ServerTime = HoraCal(r.get<uint32>());             // hora de zona, empaquetada
        n = r.get<uint32>();
        for (uint32 i = 0; i < n && r.p < fin; ++i)          // u32 mapa, u32 dificultad, u32 segundos, u64 guid de instancia
        {
            WorldPackets::Calendar::CalendarSendCalendarRaidLockoutInfo& l = p.RaidLockouts.emplace_back();
            l.MapID = int32(r.get<uint32>());
            l.DifficultyID = DificultadCal(uint32(l.MapID), r.get<uint32>());
            l.ExpireTime = int32(r.get<uint32>());
            l.InstanceID = r.get<uint64>() & 0xFFFFFFFF;
        }
        // Lo que queda se descarta: u32 base de reinicios, u32 n + (i32 mapa, i32 periodo, i32 desfase)* y los festivos.
        // RaidResets los rellena CalendarSendCalendar::Write (la tabla de Wrathion, capturada en 3.4.3): si se añadieran los de AC
        // saldrían repetidos. Los festivos no van en este paquete en 3.4.3 (el cliente los saca de Holidays.db2).
        Enviar(p.Write());
    }

    // SMSG_CALENDAR_SEND_EVENT (AC CalendarMgr::SendCalendarEvent)
    void EventoCalendario335(Rd& r)
    {
        WorldPackets::Calendar::CalendarSendEvent p;
        p.EventType = r.get<uint8>();                        // tipo de envío (0 consulta, 1 alta, 2 copia): así lo usa Wrathion
        p.OwnerGuid = mundo->ToTc(Pack(r));
        p.EventID = r.get<uint64>(); p.EventName = r.cstr(); p.Description = r.cstr();
        p.GetEventType = r.get<uint8>();                     // tipo de evento (banda, mazmorra, JcJ, reunión, otro)
        r.get<uint8>(); r.get<uint32>();                     // repetición y máx. invitaciones: no existen en 3.4.3
        p.TextureID = int32(r.get<uint32>()); p.Flags = r.get<uint32>();
        p.Date = HoraCal(r.get<uint32>()); p.LockDate = HoraCal(r.get<uint32>());
        p.EventClubID = r.get<uint32>();                     // id de hermandad
        uint32 n = r.get<uint32>();
        for (uint32 i = 0; i < n && r.p < r.b.size(); ++i)   // packguid, u8 nivel, u8 estado, u8 rango, u8 de hermandad, u64 invitación, hora, cstr nota
        {
            WorldPackets::Calendar::CalendarEventInviteInfo& v = p.Invites.emplace_back();
            v.Guid = mundo->ToTc(Pack(r)); v.Level = r.get<uint8>(); v.Status = r.get<uint8>(); v.Moderator = r.get<uint8>();
            v.InviteType = r.get<uint8>(); v.InviteID = r.get<uint64>(); v.ResponseTime = HoraCal(r.get<uint32>()); v.Notes = r.cstr();
        }
        Enviar(p.Write());
    }

    // ---- H7f: equipos de arena
    static constexpr uint16 ARENA_INFO_335 = 1256;   // PLAYER_FIELD_ARENA_TEAM_INFO_1_1 (UNIT_END 0x94 + 0x454); 7 campos por hueco, el 0 es el id
    struct EquipoInsp { uint32 id = 0, rating = 0, jugadas = 0, ganadas = 0, misJugadas = 0, miRating = 0; };
    std::mutex _mArena;
    std::unordered_map<uint32, std::array<uint32, 6>> _arenaStats;   // id -> índice, semana jugadas, ganadas, temporada jugadas, ganadas, puesto
    std::set<uint32> _arenaPend;                                     // listas pedidas que aún no han llegado
    std::unordered_map<std::string, uint32> _arenaNombres;           // nombre -> id (de las consultas), para los eventos
    std::unordered_map<uint32, uint32> _arenaTipo;                   // id -> tamaño (2, 3, 5)
    // valor que pone el cliente en la invitación: el id del equipo que tiene en su tabla; si no es uno conocido,
    // se interpreta como tamaño (2/3/5) o hueco (0-2 / 1-3) y se busca el equipo propio de ese tamaño
    uint32 EquipoArenaDe(uint32 v)
    {
        std::lock_guard<std::mutex> l(_mArena);
        if (_arenaTipo.count(v)) return v;
        uint32 tam = (v == 2 || v == 3 || v == 5) ? v : v <= 2 ? (v == 0 ? 2 : v == 1 ? 3 : 5) : 0;
        for (auto const& [id, t] : _arenaTipo) if (t == tam) return id;
        return v;
    }
    uint64 _inspArenaDe = 0;
    std::array<EquipoInsp, 3> _inspArena;

    // lo que manda el cliente 3.4.3 -> id de equipo 3.3.5. Si coincide con uno de mis equipos es un id; si no, y es 0-2,
    // es un hueco (2c2, 3c3, 5c5). Ambigüedad: un id 1 o 2 que no es mío se toma como hueco.
    uint32 EquipoArenaId(uint32 v)
    {
        uint32 ids[3];
        for (uint8 i = 0; i < 3; ++i)
        {
            ids[i] = mundo->V335(yo335, uint16(ARENA_INFO_335 + i * 7));
            if (ids[i] && ids[i] == v) return v;
        }
        return v < 3 ? ids[v] : v;
    }

    // pide la lista de un equipo; con consulta (o si no hay estadísticas guardadas) antes pide 0x34B, cuya respuesta
    // trae SMSG_ARENA_TEAM_STATS: llega antes que la lista porque el servidor contesta en orden
    void PedirEquipoArena(uint32 id, bool consulta)
    {
        {
            std::lock_guard<std::mutex> l(_mArena);
            if (!_arenaStats.count(id)) consulta = true;
            _arenaPend.insert(id);
        }
        Wr w; w.put<uint32>(id);
        if (consulta) Srv(0x34B, w.b);
        Srv(0x34D, w.b);
    }

    // hueco sin equipo: lista vacía con el tamaño del hueco (lo mismo que contesta Wrathion)
    void ListaArenaVacia(uint32 hueco)
    {
        static uint32 const tam[3] = { 2, 3, 5 };
        WorldPacket w(SMSG_ARENA_TEAM_ROSTER, 40);
        w << uint32(0) << uint32(hueco < 3 ? tam[hueco] : 0);
        for (int i = 0; i < 6; ++i) w << uint32(0);
        w << int32(0);
        w.WriteBit(false); w.FlushBits();
        Enviar(&w);
    }

    // SMSG_ARENA_TEAM_ROSTER 3.3.5: u32 id, u8 extra, u32 n, u32 tipo, n x (u64, u8 conectado, cstr nombre, u32 rango (0 capitán),
    // u8 nivel, u8 clase, u32 semana jugadas, ganadas, temporada jugadas, ganadas, u32 índice personal, [2 x float si extra])
    // 3.4.3: u32 id, u32 tamaño, u32 semana jugadas, ganadas, temporada jugadas, ganadas, u32 índice, u32 puesto, i32 n,
    // bit descalificado (VERIFICADO: Wrathion + WPP 4.4.0); por miembro (DERIVADO de WPP 4.4.0): guid, u8 conectado,
    // u32 rango, u8 nivel, u8 clase, 5 x u32, bits6 largo, bit GDF, bit varianza GDF, nombre.
    // Las estadísticas del equipo no vienen en la lista 3.3.5: salen de SMSG_ARENA_TEAM_STATS guardado.
    void EquipoArenaLista335(Rd& r)
    {
        uint32 id = r.get<uint32>(); uint8 extra = r.get<uint8>();
        uint32 n = r.get<uint32>(), tipo = r.get<uint32>();
        struct Miembro { uint64 g = 0; uint8 conectado = 0; std::string nombre; uint32 rango = 0; uint8 nivel = 0, clase = 0; uint32 d[5] = { }; };
        std::vector<Miembro> v;
        for (uint32 i = 0; i < n && r.p + 8 <= r.b.size(); ++i)
        {
            Miembro m;
            m.g = r.get<uint64>(); m.conectado = r.get<uint8>(); m.nombre = r.cstr(); m.rango = r.get<uint32>();
            m.nivel = r.get<uint8>(); m.clase = r.get<uint8>();
            for (uint32& x : m.d) x = r.get<uint32>();
            if (extra) { r.get<float>(); r.get<float>(); }
            v.push_back(std::move(m));
        }
        std::array<uint32, 6> st = { };
        {
            std::lock_guard<std::mutex> l(_mArena);
            auto it = _arenaStats.find(id);
            if (it != _arenaStats.end()) st = it->second;
            _arenaPend.erase(id);
        }
        for (Miembro const& m : v) if (!m.nombre.empty()) Nombre(m.g, m.nombre);   // sirve luego a LEADER / REMOVE
        WorldPacket w(SMSG_ARENA_TEAM_ROSTER, 40 + v.size() * 64);
        w << uint32(id) << uint32(tipo) << uint32(st[1]) << uint32(st[2]) << uint32(st[3]) << uint32(st[4]) << uint32(st[0]) << uint32(st[5]);
        w << int32(v.size());
        w.WriteBit(false); w.FlushBits();
        for (Miembro const& m : v)
        {
            w << mundo->ToTc(m.g) << uint8(m.conectado) << uint32(m.rango) << uint8(m.nivel) << uint8(m.clase);
            for (uint32 x : m.d) w << uint32(x);
            std::string nom = m.nombre.substr(0, 63);
            w.WriteBits(nom.size(), 6); w.WriteBit(false); w.WriteBit(false); w.FlushBits();
            w.WriteString(nom);
        }
        Enviar(&w);
    }

    // SMSG_ARENA_TEAM_EVENT 3.3.5: u8 evento, u8 n, n x cstr, [u64]. Sin formato 3.4.3 conocido: texto de sistema
    // (los ERR_ARENA_TEAM_* del cliente) y se vuelve a pedir la lista para que el cliente la refresque.
    void EventoArena335(Rd& r)
    {
        uint8 ev = r.get<uint8>(), n = r.get<uint8>();
        std::string c[3];
        for (uint8 i = 0; i < n && i < 3; ++i) c[i] = r.cstr();
        std::string t;
        switch (ev)
        {
            case 3: t = c[0] + " se ha unido a " + c[1] + "."; break;                                   // JOIN_SS
            case 4: t = c[0] + " ha abandonado " + c[1] + "."; break;                                   // LEAVE_SS
            case 5: t = c[0] + " ha sido expulsado de " + c[1] + " por " + c[2] + "."; break;           // REMOVE_SSS
            case 6: t = c[0] + " es el capitán de " + c[1] + "."; break;                                // LEADER_IS_SS
            case 7: t = c[0] + " ha nombrado a " + c[1] + " nuevo capitán de " + c[2] + "."; break;     // LEADER_CHANGED_SSS
            case 8: t = c[0] + " ha disuelto " + c[1] + "."; break;                                     // DISBANDED_S
            default: break;
        }
        if (!t.empty()) Sistema(t);
        uint32 id = 0;
        {
            std::lock_guard<std::mutex> l(_mArena);
            auto it = _arenaNombres.find(ev == 7 ? c[2] : c[1]);
            if (it != _arenaNombres.end()) id = it->second;
            if (ev == 8) { if (id) _arenaStats.erase(id); return; }   // disuelto: no hay lista que pedir
        }
        if (id) { PedirEquipoArena(id, false); return; }
        for (uint8 i = 0; i < 3; ++i)
            if (uint32 x = mundo->V335(yo335, uint16(ARENA_INFO_335 + i * 7))) PedirEquipoArena(x, false);
    }

    // SMSG_INSPECT_PVP (VERIFICADO: Wrathion 3.4.3 + WPP 3.4.4): guid, u32 n corchetes, bits2 n equipos, corchetes
    // (u8 corchete, 17 x i32, bit descalificado), equipos (guid, u32 índice, jugadas, ganadas, jugadas del jugador, índice personal).
    // Como Wrathion: corchetes 0, 1, 2 y el 4 vacío, y siempre 3 equipos (vacíos si no hay).
    // Corchete: posiciones de las i32 según WPP; la 12.ª es la "UnknownRating" de Wrathion (= mejor índice de temporada).
    void EnviarInspeccionJcJ(uint64 g, std::array<EquipoInsp, 3> const& e)
    {
        static uint8 const corchetes[4] = { 0, 1, 2, 4 };
        WorldPacket w(SMSG_INSPECT_PVP, 400);
        w << mundo->ToTc(g) << uint32(4);
        w.WriteBits(3, 2); w.FlushBits();
        for (uint8 c : corchetes)
        {
            EquipoInsp x = c < 3 ? e[c] : EquipoInsp();
            w << uint8(c) << int32(0) << int32(x.miRating) << int32(0) << int32(0) << int32(0)   // -, índice, puesto, semana jugadas, ganadas
              << int32(x.misJugadas) << int32(0) << int32(x.miRating) << int32(x.miRating)        // temporada jugadas, ganadas, mejor semana, mejor temporada
              << int32(0) << int32(0) << int32(x.miRating)                                         // nivel JcJ, -, UnknownRating
              << int32(0) << int32(0) << int32(0) << int32(0) << int32(0);
            w.WriteBit(false); w.FlushBits();
        }
        for (EquipoInsp const& x : e)
            w << (x.id ? ObjectGuid::Create<HighGuid::ArenaTeam>(x.id) : ObjectGuid::Empty)
              << uint32(x.rating) << uint32(x.jugadas) << uint32(x.ganadas) << uint32(x.misJugadas) << uint32(x.miRating);
        Enviar(&w);
    }

    // SMSG_ITEM_REFUND_*: u32 oro, u32 honor, u32 arena, 5 x (u32 objeto, u32 cantidad). Honor y arena son monedas en 3.4.3 (1901 / 1900).
    void ContenidoCompra(Rd& r, WorldPackets::Item::ItemPurchaseContents& c)
    {
        c.Money = r.get<uint32>();
        uint32 honor = r.get<uint32>(), arena = r.get<uint32>();
        for (int i = 0; i < 5; ++i) { c.Items[i].ItemID = int32(r.get<uint32>()); c.Items[i].ItemCount = int32(r.get<uint32>()); }
        int k = 0;
        if (honor) { c.Currencies[k].CurrencyID = 1901; c.Currencies[k++].CurrencyCount = int32(honor); }
        if (arena) { c.Currencies[k].CurrencyID = 1900; c.Currencies[k++].CurrencyCount = int32(arena); }
    }

    // SMSG_MIRRORIMAGE_DATA: u64, u32 modelo, u8 raza, sexo, clase, [jugador: u8 piel, cara, pelo, color, vello, u32 hermandad, 11 x u32 modelo de objeto]
    void ImagenReflejada335(Rd& r, size_t tam)
    {
        ObjectGuid g = mundo->ToTc(r.get<uint64>());
        int32 modelo = int32(r.get<uint32>());
        uint8 raza = r.get<uint8>(), sexo = r.get<uint8>(), clase = r.get<uint8>();
        if (tam < 8 + 4 + 3 + 5 + 4 + 44 || !raza)
        {
            WorldPackets::Spells::MirrorImageCreatureData p; p.UnitGUID = g; p.DisplayID = modelo; Enviar(p.Write());
            return;
        }
        WorldPackets::Spells::MirrorImageComponentedData p;
        p.UnitGUID = g; p.DisplayID = modelo; p.RaceID = raza; p.Gender = sexo; p.ClassID = clase;
        uint8 v[5]; for (uint8& x : v) x = r.get<uint8>();            // piel, cara, pelo, color, vello
        for (UF::ChrCustomizationChoice const& c : Apariencia::ToCustom(raza, sexo, v)) p.Customizations.push_back(c);
        uint32 hermandad = r.get<uint32>();
        if (hermandad) p.GuildGUID = ObjectGuid::Create<HighGuid::Guild>(hermandad);
        for (int i = 0; i < 11; ++i) p.ItemDisplayID.push_back(int32(r.get<uint32>()));
        Enviar(p.Write());
    }

    // MSG_LIST_STABLED_PETS: u64 maestro, u8 n, u8 huecos, (u32 número, u32 criatura, u32 nivel, cstr nombre, u8 1 activa | 2 establo)*
    // En 3.4.3 el establo va en ActivePlayerData::PetStable. StablePetInfo de TrinityCore es la de retail y no coincide:
    // se escribe a mano como Player::SendStable de Wrathion (capturas del 3.4.3.54261).
    struct MascotaEstablo { uint32 numero, criatura, nivel; std::string nombre; };
    void Establo335(Rd& r)
    {
        uint64 maestro = r.get<uint64>();
        uint8 n = r.get<uint8>(); r.get<uint8>();
        std::optional<MascotaEstablo> actual;
        std::array<std::optional<MascotaEstablo>, 4> establo;
        size_t k = 0;
        for (uint8 i = 0; i < n && r.p < r.b.size(); ++i)
        {
            MascotaEstablo m; m.numero = r.get<uint32>(); m.criatura = r.get<uint32>(); m.nivel = r.get<uint32>(); m.nombre = r.cstr();
            uint8 fl = r.get<uint8>();
            if (fl == 1 && !actual) actual = m;
            else if (k < establo.size()) establo[k++] = m;
        }
        if (maestro) Abrir(maestro, PlayerInteractionType::StableMaster);

        WorldPacket pkt(SMSG_UPDATE_OBJECT);
        pkt << uint32(1);
        pkt << uint16(mundo->mapa);
        pkt << uint8(0);                                 // sin objetos que quitar
        pkt << uint32(0);                                // tamaño de los datos: se corrige al final
        pkt << uint8(0);                                 // UPDATETYPE_VALUES
        pkt << mundo->yo->GetGUID();
        size_t posCampos = pkt.size();
        pkt << uint32(0);                                // tamaño de los campos: se corrige al final
        pkt << uint32(128);
        pkt << uint32(8);
        pkt << uint16(0);
        pkt << uint32(1073741828);
        pkt << uint8(128);
        pkt << uint32(224);
        size_t tam = std::count_if(establo.begin(), establo.end(), [](auto const& e) { return e.has_value(); }) + (actual ? 1 : 0);
        pkt << uint8(tam ? 32 * tam + 32 - 1 : 31);
        size_t primero = 0;
        for (int8 i = int8(establo.size()) - 1; i >= 0; --i)
        {
            auto const& e = establo[i];
            if (!e) { primero += 1; continue; }
            pkt.WriteBits(255, 8); pkt.FlushBits();
            pkt << uint32(e->numero) << uint32(e->criatura) << uint32(0) << uint32(e->nivel);
            if (i == int8(establo.size()) - 1 - int8(primero) || i == 0) pkt << uint8(0xFF);
            else pkt << uint8(i + 1);
            pkt << uint8(0);
            pkt.WriteBits(e->nombre.size(), 8); pkt.WriteString(e->nombre); pkt.FlushBits();
        }
        if (actual)
        {
            pkt.WriteBits(255, 8); pkt.FlushBits();
            pkt << uint32(actual->numero) << uint32(actual->criatura) << uint32(0) << uint32(actual->nivel) << uint8(0) << uint8(0);
            pkt.WriteBits(actual->nombre.size(), 8); pkt.WriteString(actual->nombre); pkt.FlushBits();
        }
        if (maestro) pkt << mundo->ToTc(maestro);
        pkt.put<uint32>(7, uint32(pkt.size() - 11));
        pkt.put<uint32>(posCampos, uint32(pkt.size() - posCampos - 4));
        Enviar(&pkt);
    }

    // SMSG_AURA_UPDATE(_ALL): packguid, (u8 casilla, u32 hechizo, [u8 banderas, u8 nivel, u8 cargas, [packguid lanzador], [u32 máx, u32 resto]])*
    // Montura del cliente 3.4.3 que calcula su texto ($j1g/$j1f) con Points: índice del efecto MOUNTED (aura 78)
    // si el hechizo lleva SPELL_ATTR8_AURA_POINTS_ON_CLIENT (Attributes[8] & 0x1000); -1 si no.
    static int EfectoMontura(uint32 spell)
    {
        static std::unordered_map<uint32, int> const m = []
        {
            std::unordered_map<uint32, uint32> attr8;
            for (SpellMiscEntry const* e : sSpellMiscStore) if (e->DifficultyID == 0) attr8[e->SpellID] = uint32(e->Attributes[8]);
            std::unordered_map<uint32, int> r;
            for (SpellEffectEntry const* e : sSpellEffectStore)
                if (e->EffectAura == SPELL_AURA_MOUNTED && e->DifficultyID == 0 && (attr8[e->SpellID] & 0x1000))
                    r[e->SpellID] = e->EffectIndex;
            return r;
        }();
        auto it = m.find(spell);
        return it != m.end() ? it->second : -1;
    }

    // MountCapability cuyo ModSpellAuraID (86457-86461, 86496) da la velocidad REAL de AzerothCore para esa montura
    uint32 CapacidadMontura(uint32 spell)
    {
        static std::unordered_map<uint32, std::pair<int16, int16>> const vel = []
        {
            std::unordered_map<uint32, std::pair<int16, int16>> v;
            for (VelMontura const& x : kVelMontura) v[x.hechizo] = { x.tierra, x.vuelo };
            return v;
        }();
        uint32 mapa = mundo->mapa;
        auto it = vel.find(spell);
        int tierra = it != vel.end() ? it->second.first : -1, vuelo = it != vel.end() ? it->second.second : -1;
        if (vuelo >= 310) return mapa == 571 ? 240 : 243;
        if (vuelo >= 280) return mapa == 571 ? 239 : 242;
        if (vuelo > 0)    return mapa == 571 ? 238 : 241;
        if (tierra >= 100) return 227;
        if (tierra > 0)   return 226;
        if (tierra == 0)  return 231;
        // monturas que escalan por script en AC (48025, 71342, 75614...): como TC GetMountCapability, con la
        // equitación del jugador (skill 762) y el mapa; sin comprobar zona ni agua
        uint32 tipo = 0;
        if (MountEntry const* me = sDB2Manager.GetMount(spell)) tipo = me->MountTypeID;
        else for (SpellEffectEntry const* e : sSpellEffectStore)
            if (e->SpellID == spell && e->EffectAura == SPELL_AURA_MOUNTED) { tipo = uint32(e->EffectMiscValue[1]); break; }
        auto const* caps = tipo ? sDB2Manager.GetMountCapabilities(tipo) : nullptr;
        if (!caps || !mundo->yo) return 0;
        uint16 equitacion = 0;
        for (uint32 i = 0; i < 256; ++i)   // como Player::GetSkillValue (TC Player.cpp:5901): m_activePlayerData->Skill->...
            if (mundo->yo->m_activePlayerData->Skill->SkillLineID[i] == 762) { equitacion = mundo->yo->m_activePlayerData->Skill->SkillRank[i]; break; }
        MapEntry const* me = sMapStore.LookupEntry(mapa);
        for (MountTypeXCapabilityEntry const* x : *caps)
            if (MountCapabilityEntry const* c = sMountCapabilityStore.LookupEntry(x->MountCapabilityID))
                if (equitacion >= c->ReqRidingSkill && (c->ReqMapID == -1 || c->ReqMapID == int32(mapa) ||
                    (me && (me->ParentMapID == c->ReqMapID || me->CosmeticParentMapID == c->ReqMapID))))
                    return c->ID;
        return 0;
    }

    // Máscara de efectos de aura (bits 0-2) de un hechizo del cliente 3.4.3 con SPELL_ATTR8_AURA_POINTS_ON_CLIENT; 0 = no aplica
    static uint8 EfectosConPuntos(uint32 spell)
    {
        static std::unordered_map<uint32, uint8> const m = []
        {
            std::unordered_map<uint32, uint32> attr8;
            for (SpellMiscEntry const* e : sSpellMiscStore) if (e->DifficultyID == 0) attr8[e->SpellID] = uint32(e->Attributes[8]);
            std::unordered_map<uint32, uint8> r;
            for (SpellEffectEntry const* e : sSpellEffectStore)
                if (e->DifficultyID == 0 && e->EffectAura && e->EffectAura != SPELL_AURA_MOUNTED && e->EffectIndex < 3 && (attr8[e->SpellID] & 0x1000))
                    r[e->SpellID] |= uint8(1u << e->EffectIndex);
            return r;
        }();
        auto it = m.find(spell);
        return it != m.end() ? it->second : 0;
    }

    // Valor del efecto como SpellEffectInfo::CalcValue de AzerothCore (SpellInfo.cpp:410-447), sin talentos ni guiones:
    // BasePoints + mínimo del dado + RealPointsPerLevel * niveles del lanzador (CastLevel que manda AC) y × pila
    static bool PuntosDe(uint32 spell, uint8 ef, uint16 nivelLanzador, uint8 aplicaciones, float& valor)
    {
        static std::unordered_map<uint32, PuntosAura const*> const m = []
        {
            std::unordered_map<uint32, PuntosAura const*> r;
            for (PuntosAura const& x : kPuntosAura) r[x.hechizo] = &x;
            return r;
        }();
        auto it = m.find(spell);
        if (it == m.end() || ef >= 3) return false;
        PuntosAura const& p = *it->second;
        int32 v = p.bp[ef];
        if (p.porNivel[ef] != 0.f)
        {
            int32 nivel = nivelLanzador;
            if (p.nivelMax && nivel > int32(p.nivelMax)) nivel = p.nivelMax;
            else if (nivel < int32(p.nivelBase)) nivel = p.nivelBase;
            nivel -= int32(std::max(p.nivelBase, p.nivelHechizo));
            v += int32(nivel * p.porNivel[ef]);
        }
        if (p.dado[ef]) v += 1;                       // AC tira 1..DieSides: con 1 es exacto; con más, el mínimo
        if (p.pila && aplicaciones > 1) v *= aplicaciones;  // AC: amount *= GetStackAmount() (SpellAuraEffects.cpp:611)
        valor = float(v);
        return true;
    }

    // hechizo con el que el cliente enseña un aura: las monturas que escalan en AC (Invencible, Corcel celestial...) llevan
    // variantes internas (72281-72284) que no existen en el 3.4.3 y el buff no salía; se enseña la montura con su nombre
    // variantes internas de una montura que escala (72286 -> 72281..72284): al quitar el aura que el cliente ve como la
    // base hay que cancelar la que tiene de verdad el jugador en AC
    static std::vector<uint32> const& VariantesDe(uint32 base)
    {
        static std::unordered_map<uint32, std::vector<uint32>> const m = []
        {
            std::unordered_set<uint32> sinVelocidad;
            for (VelMontura const& x : kVelMontura) if (x.tierra < 0 && x.vuelo < 0) sinVelocidad.insert(x.hechizo);
            std::unordered_map<uint32, std::vector<uint32>> r;
            for (MonturaHermana const& x : kMonturaHermana)
                if (!sinVelocidad.count(x.hechizo) && sinVelocidad.count(x.hermana)) r[x.hermana].push_back(x.hechizo);
            return r;
        }();
        static std::vector<uint32> const vacio;
        auto it = m.find(base);
        return it != m.end() ? it->second : vacio;
    }

    static uint32 HechizoVisible(uint32 spell)
    {
        // la base de una montura que escala no lleva auras de velocidad ({-1, -1} en kVelMontura). Sus variantes se
        // enseñan siempre como la base: aunque existan por hotfix (72282...), el cliente no pinta su aura
        static std::unordered_map<uint32, uint32> const base = []
        {
            std::unordered_set<uint32> sinVelocidad;
            for (VelMontura const& x : kVelMontura) if (x.tierra < 0 && x.vuelo < 0) sinVelocidad.insert(x.hechizo);
            std::unordered_map<uint32, uint32> m;
            for (MonturaHermana const& x : kMonturaHermana)
                if (!sinVelocidad.count(x.hechizo) && sinVelocidad.count(x.hermana)) m[x.hechizo] = x.hermana;
            return m;
        }();
        auto it = base.find(spell);
        if (it != base.end() && sSpellNameStore.HasRecord(it->second)) return it->second;
        return spell;
    }

    // mensaje de addon 3.4.3 -> CMSG_MESSAGECHAT 0x095: u32 tipo, u32 LANG_ADDON(0xFFFFFFFF), [cstr destino], cstr "prefijo\ttexto"
    bool _susurroAddonVisto = false;
    void AddonA335(WorldPackets::Chat::ChatAddonMessageParams const& pa, std::string const& destino)
    {
        if (pa.Prefix.empty() || pa.Prefix.size() > 16) return;
        std::string texto = pa.Prefix + '\t' + pa.Text.substr(0, pa.Text.find('\0'));
        if (texto.size() > 255) return;                   // AC lo descarta igual (ChatHandler.cpp:292)
        uint32 tipo;
        switch (int32(pa.Type))                           // AC solo acepta LANG_ADDON en estos (ChatHandler.cpp:189-195)
        {
            case CHAT_MSG_PARTY:         tipo = 0x02; break;
            case CHAT_MSG_RAID:          tipo = 0x03; break;
            case CHAT_MSG_GUILD:         tipo = 0x04; break;
            case CHAT_MSG_WHISPER:       tipo = 0x07; break;
            case CHAT_MSG_INSTANCE_CHAT: tipo = 0x2C; break;   // 3.4.3 0x3E -> 3.3.5 BATTLEGROUND
            default: return;                               // OFFICER, CHANNEL: AC no los admite con LANG_ADDON
        }
        Wr w; w.put<uint32>(tipo).put<uint32>(0xFFFFFFFFu);
        if (tipo == 0x07)
        {
            if (destino.empty()) return;
            w.cstr(destino.substr(0, destino.find('-')));  // "Nombre-Reino" -> "Nombre", como CMSG_CHAT_MESSAGE_WHISPER
        }
        w.cstr(texto);
        Srv(0x095, w.b);
    }

    void Auras(Rd& r, bool todas)
    {
        WorldPackets::Spells::AuraUpdate p;
        p.UpdateAll = todas;
        p.UnitGUID = mundo->ToTc(Pack(r));
        while (r.p < r.b.size())
        {
            WorldPackets::Spells::AuraInfo a;
            a.Slot = r.get<uint8>();
            uint32 spell = r.get<uint32>();
            if (spell)
            {
                uint8 f = r.get<uint8>();
                WorldPackets::Spells::AuraDataInfo d;
                uint32 visible = HechizoVisible(spell);
                d.SpellID = int32(visible);
                d.Visual.SpellXSpellVisualID = VisualDe(visible);
                d.CastLevel = r.get<uint8>();
                d.Applications = r.get<uint8>();
                d.ActiveFlags = f & 0x07;
                uint16 fl = 0;
                if (f & 0x08) fl |= 0x0001;                  // sin lanzador (propia)
                if (f & 0x10) fl |= 0x0100 | 0x0002;         // positiva y cancelable
                if (f & 0x80) fl |= 0x0010;                  // negativa
                if (!(f & 0x08)) d.CastUnit = mundo->ToTc(Pack(r));
                if (f & 0x20) { fl |= 0x0004; d.Duration = int32(r.get<uint32>()); d.Remaining = int32(r.get<uint32>()); }
                d.Flags = fl;
                // montura: en 3.4.3 el texto "Increases ground/flight speed by X%" ($j1g/$j1f) sale de
                // Points[efecto MOUNTED] = MountCapability.ID (TC SpellAuraEffects.cpp:688, SpellAuras.cpp:151/270)
                if (int ef = EfectoMontura(visible); ef >= 0)
                {
                    if (uint32 cap = CapacidadMontura(spell))
                    {
                        d.Flags |= 0x0008;                         // AFLAG_SCALABLE
                        d.Points.assign(std::size_t(ef) + 1, 0.f);
                        d.Points[ef] = float(cap);
                        d.ActiveFlags = 1u << ef;                  // como TC: solo los efectos que existen en el hechizo 3.4.3
                    }
                }
                // resto de SPELL_ATTR8_AURA_POINTS_ON_CLIENT (PW:F, Blessing of Wisdom, Drink, Water Shield...): el cliente
                // saca $s/$w/$m del aura de Points (TC SpellAuras.cpp:151 y 267-275: Points[i] = GetAmount() de cada efecto aplicado)
                else if (uint8 efs = EfectosConPuntos(spell))
                {
                    std::vector<float> pts;
                    bool ok = true;
                    for (uint8 i = 0; i < 3 && ok; ++i)
                        if ((efs & (1u << i)) && (f & (1u << i)))  // efecto de aura en 3.4.3 y aplicado por AC (AFLAG_EFF_INDEX_i)
                        {
                            float v = 0.f;
                            ok = PuntosDe(spell, i, d.CastLevel, d.Applications, v);
                            if (pts.size() <= i) pts.resize(i + 1, 0.f);
                            pts[i] = v;
                        }
                    if (ok && !pts.empty()) { d.Flags |= 0x0008; d.Points = std::move(pts); }
                }
                d.CastID = CastIdAura(p.UnitGUID, a.Slot, spell);
                a.AuraData = d;
            }
            else
                QuitaCastIdAura(p.UnitGUID, a.Slot);
            p.Auras.push_back(a);
        }
        Enviar(p.Write());
    }

public:
    // SpellXSpellVisual por defecto de un hechizo (dificultad 0, menor prioridad): sin él el cliente no pinta nada
    static uint32 VisualDe(uint32 spell)
    {
        static std::unordered_map<uint32, std::pair<uint32, int32>> const mapa = []
        {
            std::unordered_map<uint32, std::pair<uint32, int32>> m;
            for (SpellXSpellVisualEntry const* e : sSpellXSpellVisualStore)
            {
                // como SpellMgr/SpellInfo::GetSpellXSpellVisualId de TrinityCore: sin condición de lanzador, gana el último de la tabla
                if (e->DifficultyID != 0 || e->CasterPlayerConditionID || e->CasterUnitConditionID) continue;
                m[e->SpellID] = { e->ID, e->Priority };
            }
            return m;
        }();
        auto it = mapa.find(spell);
        return it != mapa.end() ? it->second.first : 0;
    }
};
