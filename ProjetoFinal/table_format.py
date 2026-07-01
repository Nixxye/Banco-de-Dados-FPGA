from __future__ import annotations

from pathlib import Path
import struct


MAGIC = b"TBL8"
VERSION = 1
MAX_COLUMNS = 16
CELL_SIZE = 8
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

    row_width = len(columns) * CELL_SIZE
    normalized_rows = []
    for row in rows:
        if len(row) != len(columns):
            raise TableFormatError("Todas as linhas precisam ter a mesma quantidade de colunas.")
        normalized_rows.append(b"".join(_encode_fixed(cell, CELL_SIZE) for cell in row))

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
        if row_width != column_count * CELL_SIZE:
            raise TableFormatError("Row width inconsistente com a quantidade de colunas.")

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


def create_sample_tables(root: str | Path) -> list[Path]:
    target = Path(root)
    target.mkdir(parents=True, exist_ok=True)

    created = [
        write_table(
            target / "alunos.tbl8",
            ["id", "nome", "curso", "nota"],
            [
                ["00000001", "ANA", "COMP", "9.5"],
                ["00000002", "BIA", "ELEC", "8.7"],
                ["00000003", "CARLOS", "MEC", "7.9"],
                ["00000004", "DAVI", "COMP", "9.9"],
            ],
        ),
        write_table(
            target / "leituras.tbl8",
            ["sensor", "valor", "data"],
            [
                ["TEMP", "23.4", "20260530"],
                ["UMID", "45.1", "20260530"],
                ["PRESS", "1013", "20260530"],
                ["TEMP", "24.0", "20260531"],
            ],
        ),
    ]
    return created