/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef IpNetwork_h__
#define IpNetwork_h__

#include "AsioHacksFwd.h"
#include "Define.h"
#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/network_v4.hpp>
#include <boost/asio/ip/network_v6.hpp>
#include "IpAddress.h"
#include "Optional.h"
#include <span>

namespace Acore::Net
{
AC_COMMON_API bool IsInLocalNetwork(boost::asio::ip::address const& clientAddress);

AC_COMMON_API bool IsInNetwork(boost::asio::ip::network_v4 const& network, boost::asio::ip::address_v4 const& clientAddress);

AC_COMMON_API bool IsInNetwork(boost::asio::ip::network_v6 const& network, boost::asio::ip::address_v6 const& clientAddress);

AC_COMMON_API Optional<std::size_t> SelectAddressForClient(boost::asio::ip::address const& clientAddress, std::span<boost::asio::ip::address const> const& addresses);

AC_COMMON_API void ScanLocalNetworks();

// --- helpers inline heredados de AzerothCore
    inline bool IsInNetwork(boost::asio::ip::address_v4 const& networkAddress, boost::asio::ip::address_v4 const& mask, boost::asio::ip::address_v4 const& clientAddress)
    {
        boost::asio::ip::network_v4 network = boost::asio::ip::make_network_v4(networkAddress, mask);
        boost::asio::ip::address_v4_range hosts = network.hosts();
        return hosts.find(clientAddress) != hosts.end();
    }
    inline boost::asio::ip::address_v4 GetDefaultNetmaskV4(boost::asio::ip::address_v4 const& networkAddress)
    {
        if ((address_to_uint(networkAddress) & 0x80000000) == 0)
        {
            return boost::asio::ip::address_v4(0xFF000000);
        }
        if ((address_to_uint(networkAddress) & 0xC0000000) == 0x80000000)
        {
            return boost::asio::ip::address_v4(0xFFFF0000);
        }
        if ((address_to_uint(networkAddress) & 0xE0000000) == 0xC0000000)
        {
            return boost::asio::ip::address_v4(0xFFFFFF00);
        }
        return boost::asio::ip::address_v4(0xFFFFFFFF);
    }
}

#endif // IpNetwork_h__
