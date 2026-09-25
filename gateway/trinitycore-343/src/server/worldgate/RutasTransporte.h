#pragma once
// Tiempos de los transportes (barcos, zepelines, cañoneras) en los dos lados de la pasarela.
//
// AzerothCore 3.3.5 mueve el transporte con su TransportMgr::GeneratePath (fotogramas por nodo, aceleración y frenada
// entre paradas) y le manda al cliente su reloj de ruta (PathProgress). El cliente 3.4.3 recorre la misma ruta
// (TaxiPathNode idéntico) con el generador de TrinityCore 3.4.3 (tramos por mapa y segmentos entre paradas), que
// reparte el tiempo de otra forma: la vuelta dura lo mismo, pero no se pasa por cada nodo en el mismo instante. Si se
// le da al cliente el reloj de AC tal cual, el transporte va desfasado y salta (se queda en el aire al final de un
// tramo, aparece de golpe en la torre...).
//
// Aquí: RutaAc::Generar es el GeneratePath de AzerothCore (source/src/server/game/Maps/TransportMgr.cpp) portado a
// las DB2 del cliente, solo lo que da los instantes de llegada y salida de cada nodo; Correspondencia empareja cada
// nodo de AC con el instante en que la ruta del cliente pasa por él (muestreando TransportTemplate::ComputePosition
// de TrinityCore) y convierte cualquier instante de AC en el del cliente interpolando entre nodos.

#include "DB2Stores.h"
#include "TransportMgr.h"
#include "Spline.h"
#include "MoveSplineInitArgs.h"
#include "G3D/Vector3.h"
#include <cmath>
#include <cstdint>
#include <vector>

namespace RutaAc
{
    struct Nodo
    {
        uint32 mapa = 0;
        G3D::Vector3 pos;
        bool parada = false;                             // AC: KeyFrame::IsStopFrame() = actionFlag == 2
        bool salto = false;                              // KeyFrame::Teleport
        uint32 espera = 0;                               // TaxiPathNode.Delay (s)
        float distPrev = -1.f, distDesde = -1.f, distHasta = -1.f, tHasta = 0.f;
        uint32 llega = 0, sale = 0;                      // ArriveTime / DepartureTime (ms)
    };

    struct Ruta
    {
        std::vector<Nodo> nodos;
        uint32 periodo = 0;                              // TransportTemplate::pathTime de AC
    };

    // TransportMgr::GeneratePath de AzerothCore, mismo orden de operaciones (incluidas sus rarezas)
    inline bool Generar(uint32 pathId, float speed, float accel, Ruta& r)
    {
        if (pathId >= sTaxiPathNodesByPath.size() || speed <= 0.f || accel <= 0.f) return false;
        TaxiPathNodeList const& path = sTaxiPathNodesByPath[pathId];
        if (path.size() < 3) return false;
        std::vector<Nodo>& k = r.nodos;
        Movement::PointsArray splinePath;
        bool mapChange = false;
        for (std::size_t i = 0; i < path.size(); ++i)
        {
            if (!mapChange)
            {
                TaxiPathNodeEntry const* n = path[i];
                if (i != path.size() - 1 && ((n->Flags & 1) || n->ContinentID != path[i + 1]->ContinentID))
                {
                    if (!k.empty()) k.back().salto = true;
                    mapChange = true;
                }
                else
                {
                    Nodo x;
                    x.mapa = n->ContinentID;
                    x.pos = G3D::Vector3(n->Loc.X, n->Loc.Y, n->Loc.Z);
                    x.parada = n->Flags == 2;
                    x.espera = n->Delay;
                    k.push_back(x);
                    splinePath.push_back(x.pos);
                }
            }
            else
                mapChange = false;
        }
        if (splinePath.size() >= 2)                      // quita los puntos especiales de la catmull-rom
        {
            splinePath.erase(splinePath.begin()); k.erase(k.begin());
            splinePath.pop_back(); k.pop_back();
        }
        if (k.size() < 2) return false;
        k.back().salto = true;                           // del último al primero siempre es "salto"

        float const accelDist = 0.5f * speed * speed / accel;
        int32 firstStop = -1, lastStop = -1;
        k[0].distPrev = 0;
        if (k[0].parada) firstStop = lastStop = 0;

        std::size_t start = 0;
        for (std::size_t i = 1; i < k.size(); ++i)
        {
            if (k[i - 1].salto || i + 1 == k.size())
            {
                std::size_t extra = !k[i - 1].salto ? 1 : 0;
                if (i - start + extra >= 2)
                {
                    TransportSpline sp;
                    sp.init_spline(&splinePath[start], i - start + extra, Movement::SplineBase::ModeCatmullrom);
                    sp.initLengths();
                    for (std::size_t j = start; j < i + extra; ++j)
                        k[j].distPrev = float(sp.length(j - start, j + 1 - start));
                }
                if (k[i - 1].salto) k[i].distPrev = 0.f;
                start = i;
            }
            if (k[i].parada)
            {
                if (firstStop == -1) firstStop = int32(i);
                lastStop = int32(i);
            }
        }
        if (firstStop == -1 || lastStop == -1) firstStop = lastStop = 0;

        float tmp = 0.f;
        for (std::size_t i = 0; i < k.size(); ++i)
        {
            int32 j = int32((i + lastStop) % k.size());
            if (k[j].parada || j == lastStop) tmp = 0.f;
            else tmp += k[j].distPrev;
            k[j].distDesde = tmp;
        }
        tmp = 0.f;
        for (int32 i = int32(k.size()) - 1; i >= 0; --i)
        {
            int32 j = int32((i + firstStop) % k.size());
            tmp += k[(j + 1) % k.size()].distPrev;
            k[j].distHasta = tmp;
            if (k[j].parada || j == firstStop) tmp = 0.f;
        }
        for (Nodo& n : k)
        {
            float total = n.distDesde + n.distHasta;
            if (total < 2 * accelDist)
            {
                if (n.distDesde < n.distHasta)
                    n.tHasta = 2.0f * std::sqrt((n.distHasta + n.distDesde) / accel) - std::sqrt(2 * n.distDesde / accel);
                else
                    n.tHasta = std::sqrt(2 * n.distHasta / accel);
            }
            else if (n.distDesde < accelDist)
                n.tHasta = (n.distHasta + n.distDesde) / speed + (speed / accel) - std::sqrt(2 * n.distDesde / accel);
            else if (n.distHasta < accelDist)
                n.tHasta = std::sqrt(2 * n.distHasta / accel);
            else
                n.tHasta = (n.distHasta / speed) + (0.5f * speed / accel);
        }

        k[0].llega = 0;
        float cur = 0.f;
        if (k[0].parada)
        {
            cur = float(k[0].espera);
            k[0].sale = uint32(cur * 1000);
        }
        for (std::size_t i = 1; i < k.size(); ++i)
        {
            cur += k[i - 1].tHasta;
            if (k[i].parada)
            {
                k[i].llega = uint32(cur * 1000);
                cur += float(k[i].espera);
                k[i].sale = uint32(cur * 1000);
            }
            else
            {
                cur -= k[i].tHasta;
                k[i].llega = uint32(cur * 1000);
                k[i].sale = k[i].llega;
            }
        }
        r.periodo = k.back().sale;
        return r.periodo > 0;
    }
}

// Instante de AC -> instante del cliente, por nodos
struct CorrespondenciaTransporte
{
    uint32 periodoAc = 0, periodoCli = 0;
    std::vector<std::pair<double, double>> anclas;       // (instante AC, instante cliente sin envolver)

    // empareja cada nodo de AC con el paso de la ruta del cliente por su posición (muestras cada 50 ms)
    bool Construir(RutaAc::Ruta const& ac, TransportTemplate const& cli)
    {
        periodoAc = ac.periodo; periodoCli = cli.TotalPathTime;
        if (!periodoAc || !periodoCli) return false;
        struct Muestra { uint32 t; uint32 mapa; Position p; bool parado; };
        std::vector<Muestra> m;
        for (uint32 t = 0; t < periodoCli; t += 50)
        {
            size_t leg = 0; TransportMovementState st = TransportMovementState::Moving;
            Optional<Position> p = cli.ComputePosition(t, &st, &leg);
            if (p && leg < cli.PathLegs.size()) m.push_back({ t, cli.PathLegs[leg].MapId, *p, st != TransportMovementState::Moving });
        }
        if (m.empty()) return false;
        size_t n = m.size(), cursor = 0;
        double vuelta = 0;                               // se suma un periodo cada vez que el cliente da la vuelta
        bool primero = true;
        size_t perdidos = 0;
        for (RutaAc::Nodo const& nd : ac.nodos)
        {
            // primera pasada por el nodo desde el cursor (como mucho una vuelta), y en paradas hasta que se va
            size_t i0 = SIZE_MAX, i1 = SIZE_MAX; double mejor = 1e30;
            for (size_t k = 0; k < n; ++k)
            {
                size_t i = (cursor + k) % n;
                if (m[i].mapa != nd.mapa) { if (i0 != SIZE_MAX) break; continue; }
                double d = m[i].p.GetExactDist(nd.pos.x, nd.pos.y, nd.pos.z);
                // en paradas cuenta solo el tiempo parado del cliente (con el radio entraban la frenada y el arranque: ~3 s de más)
                bool vale = nd.parada ? (d < 4.0 && m[i].parado) : d < 4.0;
                if (vale)
                {
                    if (i0 == SIZE_MAX) i0 = i;
                    if (nd.parada || d < mejor) { i1 = i; mejor = std::min(mejor, d); }
                }
                else if (i0 != SIZE_MAX) break;
            }
            if (i0 == SIZE_MAX) { ++perdidos; continue; }   // nodo que la ruta del cliente no pisa (p. ej. recortado)
            if (!nd.parada) i0 = i1;                     // en marcha: el punto más cercano del paso
            auto instante = [&](size_t i) { if (!primero && i < cursor) vuelta += periodoCli; return vuelta + m[i].t; };
            double t0 = instante(i0);
            anclas.emplace_back(double(nd.llega), t0);
            if (nd.parada && nd.sale > nd.llega)
                anclas.emplace_back(double(nd.sale), t0 + (i1 >= i0 ? m[i1].t - m[i0].t : m[i1].t + periodoCli - m[i0].t));
            cursor = (i1 + 1) % n;
            primero = false;
        }
        return anclas.size() >= 2 && perdidos * 4 <= ac.nodos.size();
    }

    // instante del cliente para un instante de AC (dentro de su vuelta)
    uint32 Convertir(uint32 tAc) const
    {
        if (anclas.size() < 2) return periodoCli ? uint32(uint64(tAc) * periodoCli / periodoAc) % periodoCli : 0;
        double t = double(tAc % periodoAc);
        double base = anclas.front().first;
        if (t < base) t += periodoAc;
        for (size_t i = 0; i + 1 < anclas.size(); ++i)
        {
            auto const& a = anclas[i]; auto const& b = anclas[i + 1];
            if (t >= a.first && t <= b.first)
            {
                double f = b.first > a.first ? (t - a.first) / (b.first - a.first) : 0.0;
                return uint32(std::fmod(a.second + f * (b.second - a.second), double(periodoCli)));
            }
        }
        // entre el último nodo y el primero de la vuelta siguiente
        auto const& a = anclas.back();
        std::pair<double, double> b{ anclas.front().first + periodoAc, anclas.front().second + periodoCli };
        double f = b.first > a.first ? (t - a.first) / (b.first - a.first) : 0.0;
        return uint32(std::fmod(std::max(0.0, a.second + f * (b.second - a.second)), double(periodoCli)));
    }
};
