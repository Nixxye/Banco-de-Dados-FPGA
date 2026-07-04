#ifndef NETWORK_TASKS_H_
#define NETWORK_TASKS_H_

#include "includes.h"

// Prioridades para as threads de rede
#define RX_TASK_PRIORITY 10
#define TX_TASK_PRIORITY 11

#define TASK_STACKSIZE 2048

#define QUERY_REQUEST_BUFFER_SIZE 1536
#define QUERY_SQL_MAX_LEN 256
#define QUERY_TABLE_MAX_LEN 32
#define QUERY_MAX_ROWS 256

#define TABLE_MAX_COLUMNS 4
#define TABLE_CELL_SIZE 1

// Prototipos
void init_network_tasks(void);
void rx_task(void *pdata);
void tx_task(void *pdata);

#endif /* NETWORK_TASKS_H_ */
