#ifndef SD_DRIVER_H
#define SD_DRIVER_H

/*
 * sd_driver.h - SD card SPI driver para Altera DE2 / Nios II
 *
 * Acessa o cartao SD via tres PIOs Avalon (bit-bang SPI):
 *   SD_CLK  (output PIO, 1-bit) - clock SPI
 *   SD_CMD  (bidir  PIO, 1-bit) - MOSI
 *   SD_DAT  (bidir  PIO, 1-bit) - MISO (SD_DAT[0])
 *
 * O sinal de Chip Select (SD_DAT[3]) e controlado pelo bit 17 do
 * periferico LED_RED (led_red PIO).  O top-level DE2_NET.v deve ter:
 *   assign SD_DAT[3] = LEDR[17];
 * CS ativo (LOW) = LEDR[17] = 0 ; CS inativo (HIGH) = LEDR[17] = 1.
 *
 * Suporta FAT16 e FAT32.  Busca o arquivo no diretorio raiz e, se nao
 * encontrado, dentro do subdiretorio "tables" (para .tbl8).
 *
 * Tamanho maximo de arquivo lido: SD_FILE_MAX_SIZE bytes.
 */

/* Tamanho maximo do buffer de leitura de arquivo (bytes). */
#define SD_FILE_MAX_SIZE 262144

/*
 * Inicializa o cartao SD em modo SPI.
 * Deve ser chamado antes de qualquer sd_read_file().
 * Retorna 1 em sucesso, 0 em falha.
 */
int sd_init(void);

/*
 * Le um arquivo do cartao SD (FAT16/FAT32).
 *   name    : nome base do arquivo, ex: "alunos.tbl8"
 *   buf     : buffer de destino
 *   buf_size: tamanho maximo do buffer em bytes
 * Retorna o numero de bytes lidos, ou -1 em caso de erro.
 */
int sd_read_file(const char *name, unsigned char *buf, int buf_size);

#endif /* SD_DRIVER_H */
