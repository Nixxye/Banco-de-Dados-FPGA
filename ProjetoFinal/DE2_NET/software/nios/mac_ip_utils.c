#include "ipport.h"
#include "tcpport.h"
#include "alt_iniche_dev.h"
#include <stdio.h>

#define IP4_ADDR(ipaddr, a, b, c, d) ((ipaddr) = htonl((((a) & 0xFF) << 24) | (((b) & 0xFF) << 16) | (((c) & 0xFF) << 8) | ((d) & 0xFF)))

/*
 * Retorna o Endereço MAC (hardcoded para teste).
 */
int get_mac_addr(NET net, unsigned char mac_addr[6])
{
    mac_addr[0] = 0x00;
    mac_addr[1] = 0x07;
    mac_addr[2] = 0xED;
    mac_addr[3] = 0xFF;
    mac_addr[4] = 0x8F;
    mac_addr[5] = 0x11;
    return 0;
}

/*
 * Retorna as configurações de IP.
 * Como ativamos o DHCP no BSP, apenas sinalizamos *use_dhcp = 1.
 */
int get_ip_addr(alt_iniche_dev *p_dev,
                ip_addr *ipaddr,
                ip_addr *netmask,
                ip_addr *gw,
                int *use_dhcp)
{
    (void)p_dev;

    /* Mantem DHCP habilitado, mas garante fallback estatico se houver timeout. */
    IP4_ADDR(*ipaddr, 192, 168, 1, 15);
    IP4_ADDR(*netmask, 255, 255, 255, 0);
    IP4_ADDR(*gw, 192, 168, 1, 1);
    *use_dhcp = 1; /* Força o uso do DHCP */

    printf("Fallback estatico configurado: 192.168.1.15/24 gw 192.168.1.1\n");
    return 1;
}
