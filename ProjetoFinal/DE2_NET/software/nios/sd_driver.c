/*
 * sd_driver.c - Driver SD SPI bit-bang + FAT16/FAT32 para Nios II / DE2
 *
 * Pinos fisicos (Avalon PIO):
 *   SD_CLK_BASE  0x1A82060  - saida (1 bit)
 *   SD_CMD_BASE  0x1A82070  - bidirecional (MOSI)
 *   SD_DAT_BASE  0x1A82080  - bidirecional (MISO = SD_DAT[0])
 *   CS via LED_RED_BASE bit 17 = 0x1A820C0
 *     CS=0 (ativo)  => LEDR[17]=0  => SD_DAT[3]=0
 *     CS=1 (inativo)=> LEDR[17]=1  => SD_DAT[3]=1
 *
 * Em DE2_NET.v a linha deve ser:
 *   assign SD_DAT[3] = LEDR[17];
 * (substituindo assign SD_DAT[3] = 1'b1)
 */

#include "sd_driver.h"

#include "system.h"
#include "io.h"
#include "alt_types.h"

#include <string.h>
#include <stdio.h>
#include <ctype.h>

/* ------------------------------------------------------------------ */
/* Constantes de registradores PIO Avalon                              */
/* ------------------------------------------------------------------ */

#define PIO_DATA 0 /* offset data register  */
#define PIO_DIR 4  /* offset direction reg (1=saida, 0=entrada por bit) */

/* Bit do LED_RED usado como CS */
#define SD_CS_BIT ((alt_u32)(1u << 17))

/* ------------------------------------------------------------------ */
/* Comandos SD SPI                                                     */
/* ------------------------------------------------------------------ */

#define SD_CMD0 0
#define SD_CMD8 8
#define SD_CMD16 16
#define SD_CMD17 17
#define SD_CMD55 55
#define SD_CMD58 58
#define SD_ACMD41 41

/* Bits R1 */
#define SD_R1_IDLE 0x01u
#define SD_R1_ILLEGAL 0x04u
#define SD_R1_ERR_MASK 0xFEu

/* Token de inicio de bloco */
#define SD_TOKEN_START 0xFEu

#define SD_SECTOR_SIZE 512

/* Tipos de cartao */
#define SDCARD_V1 1
#define SDCARD_V2 2
#define SDCARD_V2HC 3 /* SDHC: usa enderecos de bloco */

static int g_card_type = 0;

/* Buffers estaticos para leitura de setores (2 x 512 bytes) */
static alt_u8 g_sec_buf[SD_SECTOR_SIZE]; /* leitura FAT/BPB/dados */
static alt_u8 g_dir_buf[SD_SECTOR_SIZE]; /* leitura de diretorio  */

/* ================================================================== */
/* SPI bit-bang                                                        */
/* ================================================================== */

static void spi_clk(int high)
{
  IOWR_32DIRECT(SD_CLK_BASE, PIO_DATA, high ? 1 : 0);
}

static void spi_mosi(int bit)
{
  IOWR_32DIRECT(SD_CMD_BASE, PIO_DIR, 1);
  IOWR_32DIRECT(SD_CMD_BASE, PIO_DATA, bit ? 1 : 0);
}

static int spi_miso(void)
{
  IOWR_32DIRECT(SD_DAT_BASE, PIO_DIR, 0);
  return (int)(IORD_32DIRECT(SD_DAT_BASE, PIO_DATA) & 1u);
}

static void spi_cs_assert(void)
{
  alt_u32 v = IORD_32DIRECT(LED_RED_BASE, PIO_DATA);
  v &= ~SD_CS_BIT;
  IOWR_32DIRECT(LED_RED_BASE, PIO_DATA, v);
}

static void spi_cs_deassert(void)
{
  alt_u32 v = IORD_32DIRECT(LED_RED_BASE, PIO_DATA);
  v |= SD_CS_BIT;
  IOWR_32DIRECT(LED_RED_BASE, PIO_DATA, v);
}

/* Transfere 1 byte MSB-first; retorna byte recebido. */
static unsigned char spi_xfer(unsigned char out)
{
  int i;
  unsigned char in = 0;
  for (i = 7; i >= 0; i--)
  {
    spi_mosi((out >> i) & 1);
    spi_clk(1);
    in = (unsigned char)((in << 1) | (unsigned char)spi_miso());
    spi_clk(0);
  }
  return in;
}

/* Envia 'count' bytes 0xFF (clocking dummy) */
static void spi_dummy(int count)
{
  int i;
  for (i = 0; i < count; i++)
    spi_xfer(0xFF);
}

/* ================================================================== */
/* Protocolo SD SPI                                                    */
/* ================================================================== */

/*
 * Envia comando de 6 bytes e retorna R1.
 * cmd  : indice (0-63)
 * arg  : argumento 32-bit
 * crc7 : CRC com stop-bit (apenas CMD0/CMD8 verificam CRC em SPI)
 */
static unsigned char sd_cmd(unsigned char cmd, alt_u32 arg, unsigned char crc7)
{
  unsigned char r1;
  int retry;

  spi_xfer((unsigned char)(0x40u | cmd));
  spi_xfer((unsigned char)(arg >> 24));
  spi_xfer((unsigned char)(arg >> 16));
  spi_xfer((unsigned char)(arg >> 8));
  spi_xfer((unsigned char)(arg >> 0));
  spi_xfer(crc7);

  /* Aguarda R1 valido (MSB=0) por ate 8 bytes */
  for (retry = 0; retry < 8; retry++)
  {
    r1 = spi_xfer(0xFF);
    if ((r1 & 0x80u) == 0)
      return r1;
  }
  return 0xFF; /* timeout */
}

/* Aguarda o cartao liberar a linha (sair de 0x00) */
static int sd_wait_ready(int max_retries)
{
  int i;
  for (i = 0; i < max_retries; i++)
  {
    if (spi_xfer(0xFF) != 0x00)
      return 1;
  }
  return 0;
}

/* Aguarda token de inicio de bloco de dados (0xFE) */
static int sd_wait_token(int max_retries)
{
  int i;
  for (i = 0; i < max_retries; i++)
  {
    unsigned char b = spi_xfer(0xFF);
    if (b == SD_TOKEN_START)
      return 1;
    if ((b & 0xF0u) == 0x00u)
      return 0; /* token de erro */
  }
  return 0;
}

/*
 * Le um setor de 512 bytes para buf.
 * Para SDSC v1/v2: address = sector * 512
 * Para SDHC     : address = sector (block address)
 */
static int sd_read_sector(alt_u32 sector, alt_u8 *buf)
{
  unsigned char r1;
  alt_u32 addr;
  int i;

  addr = (g_card_type == SDCARD_V2HC) ? sector : (sector * SD_SECTOR_SIZE);

  spi_cs_assert();
  spi_dummy(1);
  r1 = sd_cmd(SD_CMD17, addr, 0xFF);
  if (r1 != 0x00u)
  {
    spi_dummy(1);
    spi_cs_deassert();
    return 0;
  }

  if (!sd_wait_token(2000))
  {
    spi_dummy(1);
    spi_cs_deassert();
    return 0;
  }

  for (i = 0; i < SD_SECTOR_SIZE; i++)
    buf[i] = spi_xfer(0xFF);

  spi_xfer(0xFF); /* CRC byte 1 - descartado */
  spi_xfer(0xFF); /* CRC byte 2 - descartado */

  spi_dummy(1);
  spi_cs_deassert();
  return 1;
}

/* ================================================================== */
/* sd_init                                                             */
/* ================================================================== */

int sd_init(void)
{
  unsigned char r1;
  unsigned char ocr[4];
  int retry;
  int i;

  g_card_type = 0;

  /* Garante CS desativado e CLK em baixo antes de comecar */
  spi_clk(0);
  spi_cs_deassert();

  /* >= 74 ciclos de clock com CS desativado para power-up */
  spi_dummy(10);

  /* CMD0: reset para modo SPI */
  spi_cs_assert();
  spi_dummy(1);
  r1 = sd_cmd(SD_CMD0, 0, 0x95u);
  spi_dummy(1);
  spi_cs_deassert();

  if (r1 != SD_R1_IDLE)
  {
    printf("SD: CMD0 falhou (R1=0x%02X)\n", (unsigned)r1);
    return 0;
  }

  /* CMD8: verifica suporte SD v2 (arg=0x1AA: 3.3 V + check pattern 0xAA) */
  spi_cs_assert();
  spi_dummy(1);
  r1 = sd_cmd(SD_CMD8, 0x000001AAu, 0x87u);

  if ((r1 & SD_R1_ILLEGAL) == 0)
  {
    /* SD v2: le R7 (4 bytes) */
    for (i = 0; i < 4; i++)
      ocr[i] = spi_xfer(0xFF);
    spi_dummy(1);
    spi_cs_deassert();

    if ((ocr[3] & 0xAAu) != 0xAAu)
    {
      printf("SD: CMD8 tensao invalida\n");
      return 0;
    }

    /* ACMD41 com HCS=1 ate cartao sair do idle */
    for (retry = 0; retry < 2000; retry++)
    {
      spi_cs_assert();
      spi_dummy(1);
      sd_cmd(SD_CMD55, 0, 0xFF);
      spi_dummy(1);
      spi_cs_deassert();

      spi_cs_assert();
      spi_dummy(1);
      r1 = sd_cmd(SD_ACMD41, 0x40000000u, 0xFF);
      spi_dummy(1);
      spi_cs_deassert();

      if (r1 == 0x00u)
        break;
    }

    if (r1 != 0x00u)
    {
      printf("SD: ACMD41 timeout v2 (R1=0x%02X)\n", (unsigned)r1);
      return 0;
    }

    /* CMD58: verifica bit CCS (SDHC vs SDSC) */
    spi_cs_assert();
    spi_dummy(1);
    r1 = sd_cmd(SD_CMD58, 0, 0xFF);
    for (i = 0; i < 4; i++)
      ocr[i] = spi_xfer(0xFF);
    spi_dummy(1);
    spi_cs_deassert();

    g_card_type = ((ocr[0] & 0x40u) != 0) ? SDCARD_V2HC : SDCARD_V2;
  }
  else
  {
    /* SD v1 */
    spi_dummy(1);
    spi_cs_deassert();

    for (retry = 0; retry < 2000; retry++)
    {
      spi_cs_assert();
      spi_dummy(1);
      sd_cmd(SD_CMD55, 0, 0xFF);
      spi_dummy(1);
      spi_cs_deassert();

      spi_cs_assert();
      spi_dummy(1);
      r1 = sd_cmd(SD_ACMD41, 0, 0xFF);
      spi_dummy(1);
      spi_cs_deassert();

      if (r1 == 0x00u)
        break;
    }

    if (r1 != 0x00u)
    {
      printf("SD: ACMD41 timeout v1 (R1=0x%02X)\n", (unsigned)r1);
      return 0;
    }

    /* CMD16: define tamanho de bloco = 512 */
    spi_cs_assert();
    spi_dummy(1);
    r1 = sd_cmd(SD_CMD16, SD_SECTOR_SIZE, 0xFF);
    spi_dummy(1);
    spi_cs_deassert();

    g_card_type = SDCARD_V1;
  }

  printf("SD: OK tipo=%d (%s)\n",
         g_card_type,
         (g_card_type == SDCARD_V2HC) ? "SDHC" : "SDSC");
  return 1;
}

/* ================================================================== */
/* Leitor FAT16 / FAT32                                                */
/* ================================================================== */

typedef struct
{
  alt_u16 bytes_per_sector;
  alt_u8 sectors_per_cluster;
  alt_u16 reserved_sectors;
  alt_u8 num_fats;
  alt_u16 root_entry_count; /* FAT16: > 0;  FAT32: 0 */
  alt_u32 sectors_per_fat;
  alt_u32 root_cluster; /* FAT32 only */
  int is_fat32;

  /* derivados */
  alt_u32 fat_start;
  alt_u32 root_dir_sector;  /* FAT16 apenas */
  alt_u32 root_dir_sectors; /* FAT16 apenas */
  alt_u32 data_start;
} FatBPB;

static alt_u16 ru16(const alt_u8 *p)
{
  return (alt_u16)(p[0] | ((alt_u16)p[1] << 8));
}

static alt_u32 ru32(const alt_u8 *p)
{
  return (alt_u32)(p[0] |
                   ((alt_u32)p[1] << 8) |
                   ((alt_u32)p[2] << 16) |
                   ((alt_u32)p[3] << 24));
}

/*
 * A partir do setor 0 em g_sec_buf, encontra o setor de inicio da
 * particao FAT (VBR).  Retorna 0 se o setor 0 JA e o VBR.
 */
static alt_u32 fat_find_vbr(void)
{
  /* Sinal de jump: 0xEB xx 0x90 ou 0xE9 */
  if (g_sec_buf[0] == 0xEBu || g_sec_buf[0] == 0xE9u)
    return 0;

  /* MBR: assinatura 0x55 0xAA no final */
  if (g_sec_buf[510] == 0x55u && g_sec_buf[511] == 0xAAu)
    return ru32(g_sec_buf + 446 + 8); /* LBA start da 1a particao */

  return 0;
}

/* Analisa BPB do VBR ja carregado em g_sec_buf. */
static int fat_parse_bpb(FatBPB *b, alt_u32 vbr_sector)
{
  const alt_u8 *s = g_sec_buf;
  alt_u16 fat16_size;
  alt_u32 fat32_size;
  alt_u16 total16;
  alt_u32 total32;

  if (s[510] != 0x55u || s[511] != 0xAAu)
    return 0;

  b->bytes_per_sector = ru16(s + 11);
  b->sectors_per_cluster = s[13];
  b->reserved_sectors = ru16(s + 14);
  b->num_fats = s[16];
  b->root_entry_count = ru16(s + 17);
  fat16_size = ru16(s + 22);
  fat32_size = ru32(s + 36);
  total16 = ru16(s + 19);
  total32 = ru32(s + 32);
  b->root_cluster = ru32(s + 44);

  (void)total16;
  (void)total32;

  if (b->bytes_per_sector != SD_SECTOR_SIZE)
    return 0;
  if (b->sectors_per_cluster == 0)
    return 0;
  if (b->num_fats == 0)
    return 0;

  b->sectors_per_fat = (fat16_size != 0) ? (alt_u32)fat16_size : fat32_size;
  b->is_fat32 = (b->root_entry_count == 0) ? 1 : 0;

  b->fat_start = vbr_sector + (alt_u32)b->reserved_sectors;

  b->root_dir_sectors = ((alt_u32)b->root_entry_count * 32u +
                         SD_SECTOR_SIZE - 1u) /
                        SD_SECTOR_SIZE;
  b->root_dir_sector = b->fat_start +
                       (alt_u32)b->num_fats * b->sectors_per_fat;
  b->data_start = b->root_dir_sector + b->root_dir_sectors;

  return 1;
}

/* Converte cluster -> primeiro setor de dados */
static alt_u32 fat_clus2sec(const FatBPB *b, alt_u32 clus)
{
  return b->data_start + (clus - 2u) * (alt_u32)b->sectors_per_cluster;
}

/* Proximo cluster via FAT16 */
static alt_u32 fat16_next(const FatBPB *b, alt_u32 clus)
{
  alt_u32 off = clus * 2u;
  alt_u32 sec = b->fat_start + off / SD_SECTOR_SIZE;
  alt_u32 idx = off % SD_SECTOR_SIZE;
  if (!sd_read_sector(sec, g_sec_buf))
    return 0xFFFFu;
  return (alt_u32)ru16(g_sec_buf + idx);
}

/* Proximo cluster via FAT32 */
static alt_u32 fat32_next(const FatBPB *b, alt_u32 clus)
{
  alt_u32 off = clus * 4u;
  alt_u32 sec = b->fat_start + off / SD_SECTOR_SIZE;
  alt_u32 idx = off % SD_SECTOR_SIZE;
  if (!sd_read_sector(sec, g_sec_buf))
    return 0x0FFFFFF8u;
  return ru32(g_sec_buf + idx) & 0x0FFFFFFFu;
}

static alt_u32 fat_next_cluster(const FatBPB *b, alt_u32 clus)
{
  return b->is_fat32 ? fat32_next(b, clus) : fat16_next(b, clus);
}

static int fat_is_eoc(const FatBPB *b, alt_u32 clus)
{
  if (b->is_fat32)
    return clus >= 0x0FFFFFF8u;
  return clus >= 0xFFF8u;
}

/*
 * Converte nome para formato 8.3 maiusculo sem ponto (11 bytes).
 * Preenche com espacos.
 */
static void to83(const char *name, char *out)
{
  int i;
  const char *dot = NULL;
  int namelen;
  int extlen;

  memset(out, ' ', 11);

  for (i = 0; name[i] != '\0'; i++)
    if (name[i] == '.')
      dot = name + i;

  namelen = (dot != NULL) ? (int)(dot - name) : (int)strlen(name);
  if (namelen > 8)
    namelen = 8;
  for (i = 0; i < namelen; i++)
    out[i] = (char)toupper((unsigned char)name[i]);

  if (dot != NULL)
  {
    extlen = (int)strlen(dot + 1);
    if (extlen > 3)
      extlen = 3;
    for (i = 0; i < extlen; i++)
      out[8 + i] = (char)toupper((unsigned char)dot[1 + i]);
  }
}

/*
 * Gera um palpite de alias curto 8.3 no formato BASE~1.EXT
 * para arquivos com nome longo em FAT (ex.: alunos.tbl8 -> ALUNOS~1.TBL).
 */
static void to83_alias_guess(const char *name, char *out)
{
  int i;
  const char *dot = NULL;
  int namelen;
  int extlen;

  memset(out, ' ', 11);

  for (i = 0; name[i] != '\0'; i++)
    if (name[i] == '.')
      dot = name + i;

  namelen = (dot != NULL) ? (int)(dot - name) : (int)strlen(name);
  if (namelen > 6)
    namelen = 6;

  for (i = 0; i < namelen; i++)
    out[i] = (char)toupper((unsigned char)name[i]);

  out[6] = '~';
  out[7] = '1';

  if (dot != NULL)
  {
    extlen = (int)strlen(dot + 1);
    if (extlen > 3)
      extlen = 3;
    for (i = 0; i < extlen; i++)
      out[8 + i] = (char)toupper((unsigned char)dot[1 + i]);
  }
}

/* Verifica se cluster e valido para iteracao */
static int fat_clus_valid(const FatBPB *b, alt_u32 c)
{
  return (c >= 2u) && !fat_is_eoc(b, c);
}

/*
 * Varre 'max_sectors' setores consecutivos a partir de 'start_sector'
 * ou a cadeia de clusters a partir de 'start_cluster' (se != 0),
 * procurando uma entrada com nome83.
 *
 * want_dir = 0 -> procura arquivo; 1 -> procura subdiretorio
 * file_size e preenchido apenas quando want_dir == 0.
 * Retorna o cluster inicial da entrada ou 0 se nao encontrado.
 */
static alt_u32 fat_scan_dir(const FatBPB *b,
                            alt_u32 start_sector,
                            alt_u32 max_sectors,
                            alt_u32 start_cluster,
                            const char *name83,
                            const char *alt_name83,
                            int want_dir,
                            alt_u32 *file_size)
{
  alt_u32 cluster = start_cluster;
  alt_u32 sec_done = 0;
  int use_chain = (start_cluster >= 2u);

  *file_size = 0;

  for (;;)
  {
    alt_u32 sec;
    alt_u32 s_in_clus;
    alt_u32 clus_secs = use_chain ? (alt_u32)b->sectors_per_cluster : max_sectors;

    for (s_in_clus = 0; s_in_clus < clus_secs; s_in_clus++)
    {
      int entry;

      if (use_chain)
        sec = fat_clus2sec(b, cluster) + s_in_clus;
      else
        sec = start_sector + sec_done;

      if (!sd_read_sector(sec, g_dir_buf))
        return 0;

      for (entry = 0; entry < SD_SECTOR_SIZE / 32; entry++)
      {
        const alt_u8 *de = g_dir_buf + entry * 32;
        alt_u8 attr;

        if (de[0] == 0x00u)
          return 0; /* fim das entradas */
        if (de[0] == 0xE5u)
          continue; /* entrada apagada */

        attr = de[11];
        if (attr == 0x0Fu)
          continue; /* entrada LFN */
        if (attr & 0x08u)
          continue; /* label de volume */
        if (de[0] == '.')
          continue; /* . e .. */

        /* Verifica tipo desejado */
        if (want_dir && !(attr & 0x10u))
          continue;
        if (!want_dir && (attr & 0x10u))
          continue;

        if (memcmp(de, name83, 11) == 0 ||
            (alt_name83 != NULL && memcmp(de, alt_name83, 11) == 0))
        {
          *file_size = ru32(de + 28);
          return (alt_u32)ru16(de + 26);
        }
      }

      sec_done++;
      if (!use_chain && sec_done >= max_sectors)
        return 0;
    }

    if (!use_chain)
      break;

    cluster = fat_next_cluster(b, cluster);
    if (!fat_clus_valid(b, cluster))
      break;
  }

  return 0;
}

/* ================================================================== */
/* sd_read_file                                                        */
/* ================================================================== */

int sd_read_file(const char *name, unsigned char *buf, int buf_size)
{
  FatBPB bpb;
  char name83[11];
  char alias83[11];
  char tables83[11];
  alt_u32 vbr;
  alt_u32 file_cluster;
  alt_u32 file_size;
  alt_u32 cluster;
  int bytes_read;
  int done;

  to83(name, name83);
  to83_alias_guess(name, alias83);
  to83("tables", tables83);

  /* Setor 0: MBR ou VBR */
  if (!sd_read_sector(0, g_sec_buf))
  {
    printf("SD: falha ao ler setor 0\n");
    return -1;
  }

  vbr = fat_find_vbr();
  if (vbr != 0)
  {
    if (!sd_read_sector(vbr, g_sec_buf))
    {
      printf("SD: falha ao ler VBR no setor %lu\n", (unsigned long)vbr);
      return -1;
    }
  }

  if (!fat_parse_bpb(&bpb, vbr))
  {
    printf("SD: BPB FAT invalido\n");
    return -1;
  }

  /* Busca no diretorio raiz */
  if (bpb.is_fat32)
  {
    file_cluster = fat_scan_dir(&bpb, 0, 0, bpb.root_cluster,
                                name83, alias83, 0, &file_size);
  }
  else
  {
    file_cluster = fat_scan_dir(&bpb,
                                bpb.root_dir_sector,
                                bpb.root_dir_sectors,
                                0,
                                name83, alias83, 0, &file_size);
  }

  /* Se nao encontrou na raiz, tenta /tables/ */
  if (file_cluster == 0)
  {
    alt_u32 tables_cluster;
    alt_u32 dummy_size;

    if (bpb.is_fat32)
    {
      tables_cluster = fat_scan_dir(&bpb, 0, 0, bpb.root_cluster,
                                    tables83, NULL, 1, &dummy_size);
    }
    else
    {
      tables_cluster = fat_scan_dir(&bpb,
                                    bpb.root_dir_sector,
                                    bpb.root_dir_sectors,
                                    0,
                                    tables83, NULL, 1, &dummy_size);
    }

    if (tables_cluster != 0)
    {
      file_cluster = fat_scan_dir(&bpb, 0, 0, tables_cluster,
                                  name83, alias83, 0, &file_size);
    }
  }

  if (file_cluster == 0)
  {
    printf("SD: arquivo '%s' nao encontrado\n", name);
    return -1;
  }

  if (file_size == 0)
    return 0;

  /* Le dados seguindo a cadeia de clusters */
  bytes_read = 0;
  done = 0;
  cluster = file_cluster;

  while (!done && fat_clus_valid(&bpb, cluster))
  {
    alt_u32 s;

    for (s = 0; s < (alt_u32)bpb.sectors_per_cluster && !done; s++)
    {
      int i;
      alt_u32 sec = fat_clus2sec(&bpb, cluster) + s;

      if (!sd_read_sector(sec, g_sec_buf))
        return -1;

      for (i = 0; i < SD_SECTOR_SIZE && !done; i++)
      {
        if ((alt_u32)bytes_read >= file_size ||
            bytes_read >= buf_size)
        {
          done = 1;
          break;
        }
        buf[bytes_read++] = g_sec_buf[i];
      }
    }

    cluster = fat_next_cluster(&bpb, cluster);
  }

  return bytes_read;
}
