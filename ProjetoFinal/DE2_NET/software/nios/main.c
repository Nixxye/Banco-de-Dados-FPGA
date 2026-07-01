#include <stdio.h>
#include <stdlib.h>
#include "includes.h"
#include "system.h"
#include "network_tasks.h"
#include "dm9000a.h"
#include "sd_driver.h"

// Define prioridades e tamanhos de stack para a task inicial
#define INIT_TASK_PRIORITY 5
#define TASK_STACKSIZE 2048

OS_STK init_task_stk[TASK_STACKSIZE];

// Funcao fornecida pelo BSP (NicheStack) para inicializar a rede
// Quando chamada, levanta a interface e roda DHCP caso configurado
extern void alt_iniche_init(void);

extern int netmain(void);
extern int iniche_net_ready;

// Task inicial: levanta a rede e depois cria as tasks de aplicacao
void init_task(void *pdata)
{
    printf("Inicializando NicheStack TCP/IP e buscando IP via DHCP...\n");

    // Inicializa os drivers e semáforos do NicheStack
    alt_iniche_init();

    // Dá a partida na thread principal do TCP/IP (NicheStack)
    netmain();

    // Aguarda o DHCP negociar o IP com o roteador antes de abrir os sockets
    while (!iniche_net_ready)
    {
        OSTimeDlyHMSM(0, 0, 1, 0); // Espera 1 segundo
    }

    printf("NicheStack inicializado com sucesso e IP obtido!\n");

    // Inicializa a aplicacao (WebSockets + Hardware rx/tx)
    init_network_tasks();

    // Remove a task de inicializacao ja que seu trabalho terminou
    OSTaskDel(OS_PRIO_SELF);
}

int main(void)
{
    // Inicializa o controlador de rede físico (DM9000A) da placa DE2
    DM9000A_INSTANCE(DM9000A_0, dm9000a_0);
    DM9000A_INIT(DM9000A_0, dm9000a_0);

    printf("--- Web Server NIOS na DE2 ---\n");
    printf("Iniciando MicroC/OS-II...\n");

    /* Inicializa o driver do cartao SD (SPI bit-bang via PIO) */
    if (!sd_init())
    {
        printf("AVISO: Cartao SD nao inicializado. Consultas de tabela falharao.\n");
    }

    // Cria a task inicial
    OSTaskCreateExt(init_task,
                    NULL,
                    (void *)&init_task_stk[TASK_STACKSIZE - 1],
                    INIT_TASK_PRIORITY,
                    INIT_TASK_PRIORITY,
                    init_task_stk,
                    TASK_STACKSIZE,
                    NULL,
                    0);

    // Inicia o Scheduler do RTOS
    OSStart();

    return 0;
}
