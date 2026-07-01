#include "network_tasks.h"
#include "sd_driver.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>
#include <unistd.h>

#include "system.h"
#include "io.h"
#include "alt_types.h"

// Inclusões exclusivas do NicheStack
#include "ipport.h"
#include "tcpport.h"
#include "osport.h"

#define HTTP_PORT 80
#define DEFAULT_QUERY_LIMIT 8
#define MAX_RESPONSE_BODY_SIZE 8192
#define TABLE_HEADER_BYTES (24 + (TABLE_MAX_COLUMNS * TABLE_CELL_SIZE))
#define TABLE_FILE_SUFFIX ".tbl8"

#ifndef USER_HW_0_BASE
#define USER_HW_0_BASE 0x00000000
#endif

#define HW_REG_CONTROL_OFFSET 0
#define HW_REG_DATA_IN_OFFSET 4
#define HW_REG_STATUS_OFFSET 8
#define HW_REG_DATA_OUT_OFFSET 12

#define HW_CONTROL_WRITE_STROBE 0x01
#define HW_CONTROL_INPUT_EOF 0x02
#define HW_CONTROL_CLEAR 0x04

#define HW_STATUS_INPUT_FULL 0x01
#define HW_STATUS_OUTPUT_VALID 0x02
#define HW_STATUS_BUSY 0x04
#define HW_STATUS_DONE 0x08

#define QUERY_CAPTURE_BUFFER_SIZE \
    (QUERY_MAX_ROWS * TABLE_MAX_COLUMNS * TABLE_CELL_SIZE)

#define QUERY_SELECT_MAX_LEN QUERY_SQL_MAX_LEN
#define QUERY_WHERE_MAX_LEN QUERY_SQL_MAX_LEN
#define QUERY_DESCRIPTOR_BUFFER_SIZE 1024
#define HW_STALL_MAX_CYCLES 1000000
#define HW_USE_QUERY_DESCRIPTOR 0

#define QUERY_META_MAGIC 0x31595251UL

#define QUERY_TLV_TABLE_NAME 1UL
#define QUERY_TLV_RAW_SQL 2UL
#define QUERY_TLV_SELECT_LIST 3UL
#define QUERY_TLV_WHERE_CLAUSE 4UL
#define QUERY_TLV_LIMIT 5UL
#define QUERY_TLV_SOURCE_COLUMNS 6UL
#define QUERY_TLV_SOURCE_COLUMN_COUNT 7UL
#define QUERY_TLV_SOURCE_ROW_COUNT 8UL
#define QUERY_TLV_SOURCE_ROW_WIDTH 9UL
#define QUERY_TLV_SOURCE_CELL_SIZE 10UL

#define MIN_ALT_U32(a, b) ((a) < (b) ? (a) : (b))

typedef struct
{
    char sql[QUERY_SQL_MAX_LEN];
    char table[QUERY_TABLE_MAX_LEN];
    char select_list[QUERY_SELECT_MAX_LEN];
    char where_clause[QUERY_WHERE_MAX_LEN];
    alt_u32 limit;
} QueryRequest;

typedef struct
{
    alt_u32 version;
    alt_u32 column_count;
    alt_u32 row_count;
    alt_u32 row_width;
    char columns[TABLE_MAX_COLUMNS][TABLE_CELL_SIZE + 1];
} TableSchema;

typedef struct
{
    FILE *file;
    const unsigned char *memory;
    alt_u32 memory_size;
    alt_u32 memory_offset;
} StreamSource;

static OS_STK rx_task_stk[TASK_STACKSIZE];
static OS_STK tx_task_stk[TASK_STACKSIZE];

static OS_EVENT *query_ready_sem;

static volatile int current_client_socket = -1;
static QueryRequest pending_request;

static char rx_request_buffer[QUERY_REQUEST_BUFFER_SIZE];
static unsigned char captured_output_buffer[QUERY_CAPTURE_BUFFER_SIZE];
static unsigned char query_descriptor_buffer[QUERY_DESCRIPTOR_BUFFER_SIZE];
static char http_response_body[MAX_RESPONSE_BODY_SIZE];

static int load_table_schema(FILE *table_file, TableSchema *schema);

static alt_u32 read_le32(const unsigned char *raw)
{
    return ((alt_u32)raw[0]) |
           ((alt_u32)raw[1] << 8) |
           ((alt_u32)raw[2] << 16) |
           ((alt_u32)raw[3] << 24);
}

static void trim_copy(char *dst, int dst_size, const char *src, int src_len)
{
    int start = 0;
    int end = src_len;
    int write_len;

    if (dst_size <= 0)
    {
        return;
    }

    while (start < end && isspace((unsigned char)src[start]))
    {
        start++;
    }

    while (end > start)
    {
        unsigned char ch = (unsigned char)src[end - 1];
        if (ch == '\0' || ch == ' ' || ch == '\r' || ch == '\n' || ch == '\t')
        {
            end--;
        }
        else
        {
            break;
        }
    }

    write_len = end - start;
    if (write_len >= (dst_size - 1))
    {
        write_len = dst_size - 1;
    }

    if (write_len > 0)
    {
        memcpy(dst, src + start, write_len);
    }
    dst[write_len] = '\0';
}

static int appendf(char *dst, int dst_size, int *offset, const char *fmt, ...)
{
    int written;
    va_list args;

    if (*offset >= dst_size)
    {
        return 0;
    }

    va_start(args, fmt);
    written = vsnprintf(dst + *offset, dst_size - *offset, fmt, args);
    va_end(args);

    if (written < 0 || written >= (dst_size - *offset))
    {
        return 0;
    }

    *offset += written;
    return 1;
}

static int sanitize_identifier(const char *value)
{
    int index = 0;

    if (value[0] == '\0')
    {
        return 0;
    }

    while (value[index] != '\0')
    {
        unsigned char ch = (unsigned char)value[index];
        if (!(isalnum(ch) || ch == '_'))
        {
            return 0;
        }
        index++;
    }

    return 1;
}

static int ascii_strncasecmp_local(const char *lhs, const char *rhs, int count)
{
    int index;

    for (index = 0; index < count; index++)
    {
        unsigned char left = (unsigned char)lhs[index];
        unsigned char right = (unsigned char)rhs[index];

        if (tolower(left) != tolower(right))
        {
            return (int)tolower(left) - (int)tolower(right);
        }
    }

    return 0;
}

static int is_sql_boundary(unsigned char ch)
{
    return ch == '\0' || isspace(ch) || ch == ',' || ch == '(' || ch == ')';
}

static const char *find_sql_keyword(const char *sql, const char *keyword)
{
    int keyword_len = (int)strlen(keyword);
    const char *cursor = sql;

    while (*cursor != '\0')
    {
        unsigned char before = (cursor == sql) ? ' ' : (unsigned char)cursor[-1];
        unsigned char after = (unsigned char)cursor[keyword_len];

        if (is_sql_boundary(before) &&
            ascii_strncasecmp_local(cursor, keyword, keyword_len) == 0 &&
            is_sql_boundary(after))
        {
            return cursor;
        }

        cursor++;
    }

    return NULL;
}

static const char *earliest_sql_clause_end(const char *fallback_end,
                                           const char *candidate_a,
                                           const char *candidate_b,
                                           const char *candidate_c)
{
    const char *result = fallback_end;

    if (candidate_a != NULL && candidate_a < result)
    {
        result = candidate_a;
    }
    if (candidate_b != NULL && candidate_b < result)
    {
        result = candidate_b;
    }
    if (candidate_c != NULL && candidate_c < result)
    {
        result = candidate_c;
    }

    return result;
}

static void extract_sql_fragments(const char *sql, QueryRequest *request)
{
    const char *sql_end = sql + strlen(sql);
    const char *select_pos = find_sql_keyword(sql, "SELECT");
    const char *from_pos = find_sql_keyword(sql, "FROM");
    const char *where_pos = find_sql_keyword(sql, "WHERE");
    const char *limit_pos = find_sql_keyword(sql, "LIMIT");
    const char *order_pos = find_sql_keyword(sql, "ORDER");
    const char *group_pos = find_sql_keyword(sql, "GROUP");
    const char *where_end;

    request->select_list[0] = '\0';
    request->where_clause[0] = '\0';

    if (select_pos != NULL && from_pos != NULL && from_pos > (select_pos + 6))
    {
        trim_copy(request->select_list,
                  sizeof(request->select_list),
                  select_pos + 6,
                  (int)(from_pos - (select_pos + 6)));
    }

    if (request->select_list[0] == '\0')
    {
        strcpy(request->select_list, "*");
    }

    if (where_pos == NULL)
    {
        return;
    }

    where_end = earliest_sql_clause_end(sql_end,
                                        (limit_pos != NULL && limit_pos > where_pos) ? limit_pos : NULL,
                                        (order_pos != NULL && order_pos > where_pos) ? order_pos : NULL,
                                        (group_pos != NULL && group_pos > where_pos) ? group_pos : NULL);

    if (where_end > (where_pos + 5))
    {
        trim_copy(request->where_clause,
                  sizeof(request->where_clause),
                  where_pos + 5,
                  (int)(where_end - (where_pos + 5)));
    }
}

static void write_le32_bytes(unsigned char *dst, alt_u32 value)
{
    dst[0] = (unsigned char)(value & 0xFF);
    dst[1] = (unsigned char)((value >> 8) & 0xFF);
    dst[2] = (unsigned char)((value >> 16) & 0xFF);
    dst[3] = (unsigned char)((value >> 24) & 0xFF);
}

static int descriptor_append_tlv(unsigned char *dst,
                                 int dst_size,
                                 int *offset,
                                 alt_u32 type,
                                 const unsigned char *value,
                                 int value_len)
{
    int padded_len = (value_len + 3) & ~3;

    if ((*offset + 8 + padded_len) > dst_size)
    {
        return 0;
    }

    write_le32_bytes(dst + *offset, type);
    write_le32_bytes(dst + *offset + 4, (alt_u32)value_len);
    *offset += 8;

    if (value_len > 0)
    {
        memcpy(dst + *offset, value, value_len);
    }
    if (padded_len > value_len)
    {
        memset(dst + *offset + value_len, 0, padded_len - value_len);
    }

    *offset += padded_len;
    return 1;
}

static int descriptor_append_text(unsigned char *dst,
                                  int dst_size,
                                  int *offset,
                                  alt_u32 type,
                                  const char *value)
{
    return descriptor_append_tlv(dst,
                                 dst_size,
                                 offset,
                                 type,
                                 (const unsigned char *)value,
                                 (int)strlen(value));
}

static int descriptor_append_u32(unsigned char *dst,
                                 int dst_size,
                                 int *offset,
                                 alt_u32 type,
                                 alt_u32 value)
{
    unsigned char raw[4];

    write_le32_bytes(raw, value);
    return descriptor_append_tlv(dst, dst_size, offset, type, raw, sizeof(raw));
}

static int build_source_columns_csv(const TableSchema *schema, char *dst, int dst_size)
{
    int offset = 0;
    alt_u32 index;

    if (dst_size <= 0)
    {
        return 0;
    }

    dst[0] = '\0';
    for (index = 0; index < schema->column_count; index++)
    {
        int written = snprintf(dst + offset,
                               dst_size - offset,
                               "%s%s",
                               (index == 0) ? "" : ",",
                               schema->columns[index]);

        if (written < 0 || written >= (dst_size - offset))
        {
            return 0;
        }

        offset += written;
    }

    return 1;
}

static int build_query_descriptor(const QueryRequest *request,
                                  const TableSchema *schema,
                                  unsigned char *buffer,
                                  int buffer_size,
                                  alt_u32 *descriptor_size)
{
    char columns_csv[(TABLE_MAX_COLUMNS * (TABLE_CELL_SIZE + 1)) + 1];
    int offset = 8;

    if (buffer_size < 8)
    {
        return 0;
    }

    if (!build_source_columns_csv(schema, columns_csv, sizeof(columns_csv)))
    {
        return 0;
    }

    memset(buffer, 0, buffer_size);
    write_le32_bytes(buffer, QUERY_META_MAGIC);
    write_le32_bytes(buffer + 4, 0);

    if (!descriptor_append_text(buffer, buffer_size, &offset, QUERY_TLV_TABLE_NAME, request->table) ||
        !descriptor_append_text(buffer, buffer_size, &offset, QUERY_TLV_RAW_SQL, request->sql) ||
        !descriptor_append_text(buffer, buffer_size, &offset, QUERY_TLV_SELECT_LIST, request->select_list) ||
        !descriptor_append_text(buffer, buffer_size, &offset, QUERY_TLV_WHERE_CLAUSE, request->where_clause) ||
        !descriptor_append_u32(buffer, buffer_size, &offset, QUERY_TLV_LIMIT, request->limit) ||
        !descriptor_append_text(buffer, buffer_size, &offset, QUERY_TLV_SOURCE_COLUMNS, columns_csv) ||
        !descriptor_append_u32(buffer, buffer_size, &offset, QUERY_TLV_SOURCE_COLUMN_COUNT, schema->column_count) ||
        !descriptor_append_u32(buffer, buffer_size, &offset, QUERY_TLV_SOURCE_ROW_COUNT, schema->row_count) ||
        !descriptor_append_u32(buffer, buffer_size, &offset, QUERY_TLV_SOURCE_ROW_WIDTH, schema->row_width) ||
        !descriptor_append_u32(buffer, buffer_size, &offset, QUERY_TLV_SOURCE_CELL_SIZE, TABLE_CELL_SIZE))
    {
        return 0;
    }

    write_le32_bytes(buffer + 4, (alt_u32)offset);
    *descriptor_size = (alt_u32)offset;
    return 1;
}

static int extract_line_value(const char *body,
                              const char *prefix,
                              char *dst,
                              int dst_size)
{
    const char *cursor = body;
    int prefix_len = (int)strlen(prefix);

    while (*cursor != '\0')
    {
        const char *line_end = strchr(cursor, '\n');
        int line_len;

        if (line_end == NULL)
        {
            line_end = cursor + strlen(cursor);
        }

        line_len = (int)(line_end - cursor);
        if (line_len >= prefix_len && strncmp(cursor, prefix, prefix_len) == 0)
        {
            trim_copy(dst, dst_size, cursor + prefix_len, line_len - prefix_len);
            return 1;
        }

        cursor = (*line_end == '\0') ? line_end : (line_end + 1);
    }

    return 0;
}

static int parse_request_body(const char *body, QueryRequest *request)
{
    char limit_buffer[16];
    unsigned long parsed_limit;

    memset(request, 0, sizeof(*request));
    request->limit = DEFAULT_QUERY_LIMIT;

    if (!extract_line_value(body, "TABLE=", request->table, sizeof(request->table)))
    {
        return 0;
    }

    if (!sanitize_identifier(request->table))
    {
        return 0;
    }

    if (!extract_line_value(body, "SQL=", request->sql, sizeof(request->sql)))
    {
        return 0;
    }

    if (extract_line_value(body, "LIMIT=", limit_buffer, sizeof(limit_buffer)))
    {
        parsed_limit = strtoul(limit_buffer, NULL, 10);
        if (parsed_limit > 0)
        {
            request->limit = (alt_u32)parsed_limit;
        }
    }

    if (request->limit > QUERY_MAX_ROWS)
    {
        request->limit = QUERY_MAX_ROWS;
    }

    if (request->limit == 0)
    {
        request->limit = DEFAULT_QUERY_LIMIT;
    }

    extract_sql_fragments(request->sql, request);

    return 1;
}

static void send_http_response(int client_fd,
                               int status_code,
                               const char *status_text,
                               const char *body)
{
    char header[256];
    int body_len = (int)strlen(body);
    int header_len;

    header_len = snprintf(header,
                          sizeof(header),
                          "HTTP/1.1 %d %s\r\n"
                          "Content-Type: text/plain; charset=utf-8\r\n"
                          "Content-Length: %d\r\n"
                          "Access-Control-Allow-Origin: *\r\n"
                          "Connection: close\r\n"
                          "\r\n",
                          status_code,
                          status_text,
                          body_len);

    if (header_len > 0)
    {
        send(client_fd, header, header_len, 0);
    }
    if (body_len > 0)
    {
        send(client_fd, (char *)body, body_len, 0);
    }
}

static void send_error_response(int client_fd,
                                int status_code,
                                const char *status_text,
                                const char *message)
{
    snprintf(http_response_body,
             sizeof(http_response_body),
             "STATUS=ERROR\nMESSAGE=%s\n",
             message);
    send_http_response(client_fd, status_code, status_text, http_response_body);
}

/* Buffer estatico para arquivo .tbl8 lido do SD */
static unsigned char sd_file_buffer[SD_FILE_MAX_SIZE];

/*
 * Carrega schema a partir dos primeiros TABLE_HEADER_BYTES do buffer.
 * Retorna 1 em sucesso, 0 em falha.
 */
static int load_table_schema_from_buf(const unsigned char *buf,
                                      int buf_bytes,
                                      TableSchema *schema)
{
    alt_u32 index;

    if (buf_bytes < TABLE_HEADER_BYTES)
        return 0;

    if (memcmp(buf, "TBL8", 4) != 0)
        return 0;

    memset(schema, 0, sizeof(*schema));
    schema->version = read_le32(buf + 4);
    schema->column_count = read_le32(buf + 8);
    schema->row_count = read_le32(buf + 12);
    schema->row_width = read_le32(buf + 16);

    if (schema->version != 1)
        return 0;
    if (schema->column_count == 0 || schema->column_count > TABLE_MAX_COLUMNS)
        return 0;
    if (schema->row_width != (schema->column_count * TABLE_CELL_SIZE))
        return 0;

    for (index = 0; index < schema->column_count; index++)
    {
        trim_copy(schema->columns[index],
                  sizeof(schema->columns[index]),
                  (const char *)(buf + 24 + (index * TABLE_CELL_SIZE)),
                  TABLE_CELL_SIZE);
    }

    return 1;
}

/*
 * Le tabela do SD card usando sd_driver, valida schema e preenche
 * source com ponteiro de memoria para os dados (apos o header).
 * Retorna 1 em sucesso, 0 em falha.
 */
static int load_table_from_sd(const char *table_name,
                              TableSchema *schema,
                              StreamSource *source,
                              alt_u32 *total_bytes)
{
    int file_bytes;
    char fname[QUERY_TABLE_MAX_LEN + 8]; /* nome + ".tbl8" + \0 */
    int written;
    alt_u32 available_bytes;
    alt_u32 available_rows;

    if (!sanitize_identifier(table_name))
        return 0;

    /* Primeiro tenta com sufixo .tbl8 */
    written = snprintf(fname, sizeof(fname), "%s%s", table_name, TABLE_FILE_SUFFIX);
    if (written <= 0 || written >= (int)sizeof(fname))
        return 0;

    file_bytes = sd_read_file(fname, sd_file_buffer, sizeof(sd_file_buffer));

    /* Se nao encontrou, tenta sem sufixo (nome puro) */
    if (file_bytes < 0)
    {
        file_bytes = sd_read_file(table_name, sd_file_buffer, sizeof(sd_file_buffer));
    }

    if (file_bytes < TABLE_HEADER_BYTES)
        return 0;

    if (!load_table_schema_from_buf(sd_file_buffer, file_bytes, schema))
        return 0;

    available_bytes = (alt_u32)(file_bytes - TABLE_HEADER_BYTES);
    available_rows = (schema->row_width == 0) ? 0 : (available_bytes / schema->row_width);

    if (available_rows == 0)
        return 0;

    if (available_rows < schema->row_count)
    {
        printf("SD: tabela truncada no buffer (%lu de %lu linhas).\n",
               (unsigned long)available_rows,
               (unsigned long)schema->row_count);
        schema->row_count = available_rows;
    }

    source->file = NULL;
    source->memory = sd_file_buffer + TABLE_HEADER_BYTES;
    source->memory_size = schema->row_count * schema->row_width;
    source->memory_offset = 0;
    *total_bytes = source->memory_size;

    return 1;
}

static alt_u32 hw_read_status(void)
{
    return IORD_32DIRECT(USER_HW_0_BASE, HW_REG_STATUS_OFFSET);
}

static void hw_clear(void)
{
    IOWR_32DIRECT(USER_HW_0_BASE, HW_REG_CONTROL_OFFSET, HW_CONTROL_CLEAR);
}

static void hw_write_word(alt_u32 word)
{
    IOWR_32DIRECT(USER_HW_0_BASE, HW_REG_DATA_IN_OFFSET, word);
    IOWR_32DIRECT(USER_HW_0_BASE, HW_REG_CONTROL_OFFSET, HW_CONTROL_WRITE_STROBE);
}

static void hw_signal_eof(void)
{
    IOWR_32DIRECT(USER_HW_0_BASE, HW_REG_CONTROL_OFFSET, HW_CONTROL_INPUT_EOF);
}

static void close_client_socket(void)
{
    if (current_client_socket >= 0)
    {
        close(current_client_socket);
        current_client_socket = -1;
    }
}

static int drain_output_words(unsigned char *capture_buffer,
                              int capture_limit_bytes,
                              int *captured_size)
{
    alt_u32 status = hw_read_status();

    while ((status & HW_STATUS_OUTPUT_VALID) != 0)
    {
        alt_u32 word = IORD_32DIRECT(USER_HW_0_BASE, HW_REG_DATA_OUT_OFFSET);

        if (*captured_size < capture_limit_bytes)
        {
            memcpy(capture_buffer + *captured_size, &word, sizeof(word));
            *captured_size += sizeof(word);
        }

        status = hw_read_status();
    }

    return 1;
}

/*
 * Verifica se o user_hw implementa o protocolo FIFO/descriptor esperado.
 * Retorna 1 quando compativel, 0 quando aparenta protocolo legado.
 */
static int probe_user_hw_protocol(void)
{
    alt_u32 status;
    alt_u32 echoed;

    hw_clear();

    /* Envia uma palavra de teste como payload (nao magic/descriptor). */
    hw_write_word(0xA55A1234u);
    status = hw_read_status();

    /*
     * No protocolo FIFO atual, apos um push deve haver dado valido
     * (bit 1) e contador interno em [15:8] > 0.
     */
    if (((status & HW_STATUS_OUTPUT_VALID) == 0) || (((status >> 8) & 0xFFu) == 0))
    {
        return 0;
    }

    echoed = IORD_32DIRECT(USER_HW_0_BASE, HW_REG_DATA_OUT_OFFSET);
    (void)echoed;

    hw_clear();
    return 1;
}

static int source_read_word(StreamSource *source, unsigned char *dst)
{
    if (source->file != NULL)
    {
        return fread(dst, 1, 4, source->file) == 4;
    }

    if ((source->memory_offset + 4) > source->memory_size)
    {
        return 0;
    }

    memcpy(dst, source->memory + source->memory_offset, 4);
    source->memory_offset += 4;
    return 1;
}

static void debug_dump_bytes(const char *label,
                             const unsigned char *data,
                             int byte_count)
{
    int index;

    printf("%s", label);
    for (index = 0; index < byte_count; index++)
    {
        printf(" %02X", data[index]);
    }
    printf("\n");
}

static int buffer_has_visible_data(const unsigned char *data, int byte_count)
{
    int index;

    for (index = 0; index < byte_count; index++)
    {
        unsigned char ch = data[index];
        if (ch != 0 && ch != ' ')
        {
            return 1;
        }
    }

    return 0;
}

static int feed_source_to_hardware(StreamSource *source,
                                   alt_u32 total_bytes,
                                   int signal_eof_at_end,
                                   int capture_limit_bytes,
                                   unsigned char *capture_buffer,
                                   int *captured_size)
{
    alt_u32 bytes_sent = 0;
    int stall_guard = 0;
    unsigned char raw_word[4];

    while (bytes_sent < total_bytes)
    {
        alt_u32 status = hw_read_status();

        if ((status & HW_STATUS_INPUT_FULL) != 0)
        {
            if (!drain_output_words(capture_buffer, capture_limit_bytes, captured_size))
            {
                return 0;
            }
            OSTimeDlyHMSM(0, 0, 0, 1);
            stall_guard++;
            if (stall_guard > HW_STALL_MAX_CYCLES)
            {
                return 0;
            }
            continue;
        }

        if (!source_read_word(source, raw_word))
        {
            return -1;
        }

        hw_write_word(read_le32(raw_word));
        bytes_sent += 4;
        stall_guard = 0;

        if (!drain_output_words(capture_buffer, capture_limit_bytes, captured_size))
        {
            return 0;
        }
    }

    if (!signal_eof_at_end)
    {
        return 1;
    }

    hw_signal_eof();

    for (stall_guard = 0; stall_guard < HW_STALL_MAX_CYCLES; stall_guard++)
    {
        alt_u32 status;

        if (!drain_output_words(capture_buffer, capture_limit_bytes, captured_size))
        {
            return 0;
        }

        status = hw_read_status();
        if (((status & HW_STATUS_OUTPUT_VALID) == 0) && ((status & HW_STATUS_DONE) != 0))
        {
            return 1;
        }

        OSTimeDlyHMSM(0, 0, 0, 1);
    }

    return 0;
}

static void cell_to_text(char *dst, int dst_size, const unsigned char *src)
{
    int index;
    int end = TABLE_CELL_SIZE;

    while (end > 0)
    {
        unsigned char ch = src[end - 1];
        if (ch == '\0' || ch == ' ')
        {
            end--;
        }
        else
        {
            break;
        }
    }

    if (end >= dst_size)
    {
        end = dst_size - 1;
    }

    for (index = 0; index < end; index++)
    {
        unsigned char ch = src[index];
        if (ch < 32 || ch > 126 || ch == '|' || ch == '\r' || ch == '\n')
        {
            dst[index] = '_';
        }
        else
        {
            dst[index] = (char)ch;
        }
    }

    dst[end] = '\0';
}

static int build_success_body(const char *mode,
                              const QueryRequest *request,
                              const TableSchema *schema,
                              const unsigned char *captured_rows,
                              int captured_size)
{
    alt_u32 row_index;
    alt_u32 col_index;
    alt_u32 returned_rows = 0;
    int offset = 0;

    if (schema->row_width != 0)
    {
        returned_rows = (alt_u32)(captured_size / (int)schema->row_width);
    }

    if (returned_rows > request->limit)
    {
        returned_rows = request->limit;
    }

    if (!appendf(http_response_body, sizeof(http_response_body), &offset, "STATUS=OK\n"))
    {
        return 0;
    }
    if (!appendf(http_response_body, sizeof(http_response_body), &offset, "MODE=%s\n", mode))
    {
        return 0;
    }
    if (!appendf(http_response_body, sizeof(http_response_body), &offset, "TABLE=%s\n", request->table))
    {
        return 0;
    }
    if (!appendf(http_response_body, sizeof(http_response_body), &offset, "SQL=%s\n", request->sql))
    {
        return 0;
    }
    if (!appendf(http_response_body,
                 sizeof(http_response_body),
                 &offset,
                 "SCANNED_ROWS=%lu\n",
                 (unsigned long)schema->row_count))
    {
        return 0;
    }
    if (!appendf(http_response_body,
                 sizeof(http_response_body),
                 &offset,
                 "RETURNED_ROWS=%lu\n",
                 (unsigned long)returned_rows))
    {
        return 0;
    }
    if (!appendf(http_response_body,
                 sizeof(http_response_body),
                 &offset,
                 "COLUMNS="))
    {
        return 0;
    }

    for (col_index = 0; col_index < schema->column_count; col_index++)
    {
        if (!appendf(http_response_body,
                     sizeof(http_response_body),
                     &offset,
                     "%s%s",
                     (col_index == 0) ? "" : ",",
                     schema->columns[col_index]))
        {
            return 0;
        }
    }

    if (!appendf(http_response_body, sizeof(http_response_body), &offset, "\n"))
    {
        return 0;
    }

    for (row_index = 0; row_index < returned_rows; row_index++)
    {
        if (!appendf(http_response_body, sizeof(http_response_body), &offset, "ROW="))
        {
            return 0;
        }

        for (col_index = 0; col_index < schema->column_count; col_index++)
        {
            char value[TABLE_CELL_SIZE + 1];
            int value_offset = (int)((row_index * schema->row_width) + (col_index * TABLE_CELL_SIZE));

            cell_to_text(value, sizeof(value), captured_rows + value_offset);
            if (!appendf(http_response_body,
                         sizeof(http_response_body),
                         &offset,
                         "%s%s",
                         (col_index == 0) ? "" : "|",
                         value))
            {
                return 0;
            }
        }

        if (!appendf(http_response_body, sizeof(http_response_body), &offset, "\n"))
        {
            return 0;
        }
    }

    return 1;
}

static void process_request(void)
{
    TableSchema schema;
    StreamSource source;
    StreamSource descriptor_source;
    const unsigned char *result_rows = captured_output_buffer;
    int result_size = 0;
    alt_u32 total_bytes = 0;
    alt_u32 descriptor_bytes = 0;
    int capture_limit_bytes = 0;
    int captured_size = 0;
    const char *mode = "board-sdcard";

    memset(&schema, 0, sizeof(schema));
    memset(&source, 0, sizeof(source));
    memset(&descriptor_source, 0, sizeof(descriptor_source));
    memset(captured_output_buffer, 0, sizeof(captured_output_buffer));

    if (USER_HW_0_BASE == 0)
    {
        send_error_response(current_client_socket,
                            500,
                            "Internal Server Error",
                            "USER_HW_0_BASE ausente no system.h.");
        return;
    }

    load_table_from_sd(pending_request.table, &schema, &source, &total_bytes);

    if (source.memory == NULL)
    {
        int offset = 0;

        http_response_body[0] = '\0';
        if (!appendf(http_response_body,
                     sizeof(http_response_body),
                     &offset,
                     "Tabela '%s' nao encontrada no SD.\n"
                     "Coloque '%s.tbl8' na raiz ou em /tables/ (FAT16).",
                     pending_request.table,
                     pending_request.table))
        {
            strcpy(http_response_body, "Tabela nao encontrada no SD.");
        }

        send_error_response(current_client_socket, 404, "Not Found",
                            http_response_body);
        return;
    }

    capture_limit_bytes = (int)(MIN_ALT_U32(pending_request.limit, schema.row_count) * schema.row_width);

    if (capture_limit_bytes > (int)sizeof(captured_output_buffer))
    {
        capture_limit_bytes = sizeof(captured_output_buffer);
    }

    if (!build_query_descriptor(&pending_request,
                                &schema,
                                query_descriptor_buffer,
                                sizeof(query_descriptor_buffer),
                                &descriptor_bytes))
    {
        send_error_response(current_client_socket,
                            500,
                            "Internal Server Error",
                            "Falha ao montar o descritor da consulta para o hardware.");
        return;
    }

    descriptor_source.memory = query_descriptor_buffer;
    descriptor_source.memory_size = descriptor_bytes;
    descriptor_source.memory_offset = 0;

    hw_clear();

    if (!probe_user_hw_protocol())
    {
        printf("Aviso: sonda de protocolo user_hw inconclusiva; seguindo com processamento.\n");
    }

    printf("HW feed: rows=%lu row_width=%lu total_bytes=%lu limit=%lu\n",
           (unsigned long)schema.row_count,
           (unsigned long)schema.row_width,
           (unsigned long)total_bytes,
           (unsigned long)pending_request.limit);

    if (source.memory != NULL && total_bytes >= schema.row_width)
    {
        int preview_bytes = (schema.row_width < 32) ? (int)schema.row_width : 32;
        debug_dump_bytes("SRC row0:", source.memory, preview_bytes);
    }

    if (HW_USE_QUERY_DESCRIPTOR)
    {
        int descriptor_status = feed_source_to_hardware(&descriptor_source,
                                                        descriptor_bytes,
                                                        total_bytes == 0,
                                                        0,
                                                        captured_output_buffer,
                                                        &captured_size);

        if (descriptor_status < 0)
        {
            send_error_response(current_client_socket,
                                500,
                                "Internal Server Error",
                                "Descritor da consulta truncado antes do envio completo ao hardware.");
            return;
        }

        if (descriptor_status == 0)
        {
            send_error_response(current_client_socket,
                                504,
                                "Gateway Timeout",
                                "Timeout aguardando o hardware consumir ou devolver o descritor da consulta.");
            return;
        }
    }

    if (total_bytes > 0)
    {
        int data_status = feed_source_to_hardware(&source,
                                                  total_bytes,
                                                  1,
                                                  capture_limit_bytes,
                                                  captured_output_buffer,
                                                  &captured_size);

        if (data_status < 0)
        {
            send_error_response(current_client_socket,
                                500,
                                "Internal Server Error",
                                "Dados da tabela truncados antes do envio completo ao hardware.");
            return;
        }

        if (data_status == 0)
        {
            printf("Timeout no user_hw durante feed de dados; usando payload bruto do SD.\n");
            captured_size = 0;
        }

        if (captured_size >= (int)schema.row_width)
        {
            int preview_bytes = (schema.row_width < 32) ? (int)schema.row_width : 32;
            debug_dump_bytes("OUT row0:", captured_output_buffer, preview_bytes);
        }
    }

    result_rows = captured_output_buffer;
    result_size = captured_size;

    if ((result_size < (int)schema.row_width ||
         !buffer_has_visible_data(result_rows, result_size)) &&
        source.memory != NULL &&
        source.memory_size >= schema.row_width)
    {
        printf("HW output vazio; usando payload bruto do SD para resposta HTTP.\n");
        result_rows = source.memory;
        result_size = (int)source.memory_size;
        mode = "board-sdcard-raw";
    }

    if (!build_success_body(mode,
                            &pending_request,
                            &schema,
                            result_rows,
                            result_size))
    {
        send_error_response(current_client_socket,
                            500,
                            "Internal Server Error",
                            "Falha ao montar a resposta HTTP.");
        return;
    }

    send_http_response(current_client_socket, 200, "OK", http_response_body);
}

void init_network_tasks(void)
{
    query_ready_sem = OSSemCreate(0);

    OSTaskCreateExt(rx_task,
                    NULL,
                    (void *)&rx_task_stk[TASK_STACKSIZE - 1],
                    RX_TASK_PRIORITY,
                    RX_TASK_PRIORITY,
                    rx_task_stk,
                    TASK_STACKSIZE,
                    NULL,
                    0);

    OSTaskCreateExt(tx_task,
                    NULL,
                    (void *)&tx_task_stk[TASK_STACKSIZE - 1],
                    TX_TASK_PRIORITY,
                    TX_TASK_PRIORITY,
                    tx_task_stk,
                    TASK_STACKSIZE,
                    NULL,
                    0);
}

void rx_task(void *pdata)
{
    int server_fd, client_fd;
    struct sockaddr_in server_addr, client_addr;
    int client_len = sizeof(client_addr);
    QueryRequest parsed_request;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0)
    {
        printf("Erro ao criar socket do servidor de consultas.\n");
        OSTaskDel(OS_PRIO_SELF);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(HTTP_PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr));
    listen(server_fd, 5);

    printf("Servidor HTTP aguardando consultas na porta %d...\n", HTTP_PORT);

    while (1)
    {
        client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd >= 0)
        {
            int received;

            if (current_client_socket >= 0)
            {
                send_error_response(client_fd,
                                    503,
                                    "Service Unavailable",
                                    "O firmware atende uma consulta por vez.");
                close(client_fd);
                continue;
            }

            memset(rx_request_buffer, 0, sizeof(rx_request_buffer));
            received = recv(client_fd, rx_request_buffer, sizeof(rx_request_buffer) - 1, 0);
            if (received <= 0)
            {
                close(client_fd);
                continue;
            }

            if (!parse_request_body(strstr(rx_request_buffer, "\r\n\r\n") != NULL ? strstr(rx_request_buffer, "\r\n\r\n") + 4 : rx_request_buffer,
                                    &parsed_request))
            {
                send_error_response(client_fd,
                                    400,
                                    "Bad Request",
                                    "Corpo esperado: TABLE=...\\nLIMIT=...\\nSQL=...\\n");
                close(client_fd);
                continue;
            }

            pending_request = parsed_request;
            current_client_socket = client_fd;
            OSSemPost(query_ready_sem);
        }
    }
}

void tx_task(void *pdata)
{
    alt_u8 err;

    while (1)
    {
        OSSemPend(query_ready_sem, 0, &err);

        if (current_client_socket >= 0)
        {
            process_request();
            close_client_socket();
        }
    }
}
