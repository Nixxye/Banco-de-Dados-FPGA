from __future__ import annotations

from pathlib import Path
import struct


MAGIC = b"TBL8"
VERSION = 1
MAX_COLUMNS = 4
CELL_SIZE = 1
HEADER_SIZE = 24 + (MAX_COLUMNS * CELL_SIZE)


class TableFormatError(Exception):
    pass


def _encode_fixed(text: str, size: int) -> bytes:
    encoded = text.encode("latin-1")
    if len(encoded) > size:
        raise TableFormatError(f"Valor excede {size} bytes: {text!r}")
    return encoded.ljust(size, b" ")


def _decode_fixed(raw: bytes) -> str:
    return raw.decode("latin-1", "replace").rstrip(" \x00")


def write_table(path: str | Path, columns: list[str], rows: list[list[str]]) -> Path:
    output = Path(path)
    output.parent.mkdir(parents=True, exist_ok=True)

    if not columns:
        raise TableFormatError("A tabela precisa ter ao menos uma coluna.")
    if len(columns) > MAX_COLUMNS:
        raise TableFormatError(f"Maximo de {MAX_COLUMNS} colunas por tabela.")

    row_width = MAX_COLUMNS * CELL_SIZE
    normalized_rows = []
    for row in rows:
        if len(row) != len(columns):
            raise TableFormatError("Todas as linhas precisam ter a mesma quantidade de colunas.")
        padded_row = list(row)
        while len(padded_row) < MAX_COLUMNS:
            padded_row.append("")
        normalized_rows.append(b"".join(_encode_fixed(cell, CELL_SIZE) for cell in padded_row))

    header = struct.pack(
        "<4sIIIII",
        MAGIC,
        VERSION,
        len(columns),
        len(normalized_rows),
        row_width,
        0,
    )
    names = b"".join(_encode_fixed(column, CELL_SIZE) for column in columns)
    names += b"\x00" * ((MAX_COLUMNS - len(columns)) * CELL_SIZE)

    with output.open("wb") as handle:
        handle.write(header)
        handle.write(names)
        for row in normalized_rows:
            handle.write(row)

    return output


def read_table(path: str | Path, limit: int | None = None) -> dict:
    source = Path(path)
    with source.open("rb") as handle:
        header = handle.read(HEADER_SIZE)
        if len(header) != HEADER_SIZE:
            raise TableFormatError("Cabecalho incompleto.")

        magic, version, column_count, row_count, row_width, _reserved = struct.unpack("<4sIIIII", header[:24])
        if magic != MAGIC:
            raise TableFormatError("Magic invalido. Esperado TBL8.")
        if version != VERSION:
            raise TableFormatError(f"Versao nao suportada: {version}")
        if column_count == 0 or column_count > MAX_COLUMNS:
            raise TableFormatError("Quantidade de colunas invalida.")
        if row_width != MAX_COLUMNS * CELL_SIZE:
            raise TableFormatError("Row width inconsistente com a quantidade de colunas maxima esperada.")

        names_blob = header[24:]
        columns = []
        for index in range(column_count):
            begin = index * CELL_SIZE
            end = begin + CELL_SIZE
            columns.append(_decode_fixed(names_blob[begin:end]))

        rows_to_read = row_count if limit is None else min(limit, row_count)
        rows = []
        for _ in range(rows_to_read):
            row_blob = handle.read(row_width)
            if len(row_blob) != row_width:
                raise TableFormatError("Payload truncado.")

            row = []
            for index in range(column_count):
                begin = index * CELL_SIZE
                end = begin + CELL_SIZE
                row.append(_decode_fixed(row_blob[begin:end]))
            rows.append(row)

    return {
        "columns": columns,
        "rows": rows,
        "row_count": row_count,
        "row_width": row_width,
    }


import random

def create_sample_tables(root: str | Path) -> list[Path]:
    target = Path(root)
    target.mkdir(parents=True, exist_ok=True)
    
    random.seed(42) # For deterministic tests
    
    # 1. alunos.tbl (i, n, c, t)
    alunos_rows = []
    for i in range(1, 51):
        n = chr(65 + (i % 26)) # A-Z
        c = random.choice(["C", "E", "M"])
        t = str(random.randint(0, 9))
        # Keep 'i' as single digit or modulo 10 if we strictly need 1 char, but wait, if it's 50 rows, 'i' will be up to "50" which is 2 chars!
        # The schema allows 1 char per cell. Let's use ASCII characters for ID?
        # Actually, let's just make 'i' modulo 10 to keep it single digit. Or A-z. Let's use modulo 10.
        alunos_rows.append([str(i % 10), n, c, t])
        
    # Let's override the first 4 to match old tests so they don't break
    alunos_rows[0] = ["1", "A", "C", "9"]
    alunos_rows[1] = ["2", "B", "E", "8"]
    alunos_rows[2] = ["3", "C", "M", "7"]
    alunos_rows[3] = ["4", "D", "C", "9"]

    # 2. leituras.tbl (s, v, d)
    leituras_rows = []
    for i in range(50):
        s = random.choice(["T", "U", "P"])
        v = str(random.randint(0, 9))
        d = str(random.randint(0, 9))
        leituras_rows.append([s, v, d])
        
    leituras_rows[0] = ["T", "3", "0"]
    leituras_rows[1] = ["U", "5", "0"]
    leituras_rows[2] = ["P", "1", "0"]
    leituras_rows[3] = ["T", "4", "1"]

    # 3. produtos.tbl (i, t, q, v)
    produtos_rows = []
    for i in range(50):
        produtos_rows.append([str(i % 10), random.choice(["A", "B", "C"]), str(random.randint(1, 9)), str(random.randint(1, 9))])

    # 4. vendas.tbl (i, c, v, d)
    vendas_rows = []
    for i in range(50):
        vendas_rows.append([str(i % 10), chr(65 + random.randint(0, 5)), str(random.randint(1, 9)), str(random.randint(1, 7))])

    # 5. sensores.tbl (i, l, s, t)
    sensores_rows = []
    for i in range(50):
        sensores_rows.append([str(i % 10), random.choice(["N", "S", "E", "W"]), random.choice(["0", "1"]), random.choice(["A", "B"])])

    created = [
        write_table(target / "alunos.tbl", ["i", "n", "c", "t"], alunos_rows),
        write_table(target / "leituras.tbl", ["s", "v", "d"], leituras_rows),
        write_table(target / "produtos.tbl", ["i", "t", "q", "v"], produtos_rows),
        write_table(target / "vendas.tbl", ["i", "c", "v", "d"], vendas_rows),
        write_table(target / "sensores.tbl", ["i", "l", "s", "t"], sensores_rows),
    ]
    return created