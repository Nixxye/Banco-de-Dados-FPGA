import argparse
import re
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

from table_format import TableFormatError, create_sample_tables, read_table


DEFAULT_ROOT = Path("mock_sd") / "tables"


def sanitize_identifier(value: str) -> bool:
    return bool(re.fullmatch(r"[A-Za-z0-9_]+", value or ""))


def parse_request_body(body: str) -> dict:
    fields = {"TABLE": "", "LIMIT": "8", "SQL": ""}
    for raw_line in body.splitlines():
        if "=" not in raw_line:
            continue
        key, value = raw_line.split("=", 1)
        fields[key.strip().upper()] = value.strip()

    if not sanitize_identifier(fields["TABLE"]):
        raise ValueError("TABLE invalida. Use apenas letras, numeros e underscore.")

    try:
        limit = int(fields["LIMIT"])
    except ValueError as exc:
        raise ValueError("LIMIT precisa ser inteiro.") from exc

    if limit <= 0:
        raise ValueError("LIMIT precisa ser maior que zero.")

    fields["LIMIT"] = min(limit, 16)
    if not fields["SQL"]:
        raise ValueError("SQL ausente.")
    return fields


def build_response_body(mode: str, table: str, sql: str, columns: list[str], rows: list[list[str]], scanned_rows: int) -> str:
    lines = [
        "STATUS=OK",
        f"MODE={mode}",
        f"TABLE={table}",
        f"SQL={sql}",
        f"SCANNED_ROWS={scanned_rows}",
        f"RETURNED_ROWS={len(rows)}",
        f"COLUMNS={','.join(columns)}",
    ]
    lines.extend(f"ROW={'|'.join(row)}" for row in rows)
    return "\n".join(lines) + "\n"


class QueryHandler(BaseHTTPRequestHandler):
    table_root = DEFAULT_ROOT

    def _send_text(self, status_code: int, body: str) -> None:
        payload = body.encode("utf-8")
        self.send_response(status_code)
        self.send_header("Content-Type", "text/plain; charset=utf-8")
        self.send_header("Content-Length", str(len(payload)))
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(payload)

    def do_OPTIONS(self) -> None:
        self.send_response(204)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.end_headers()

    def do_POST(self) -> None:
        if self.path not in ("/", "/query"):
            self._send_text(404, "STATUS=ERROR\nMESSAGE=Endpoint invalido. Use /query.\n")
            return

        length = int(self.headers.get("Content-Length", "0"))
        body = self.rfile.read(length).decode("utf-8", "replace")

        try:
            request = parse_request_body(body)
            table_path = self.table_root / f"{request['TABLE']}.tbl8"
            data = read_table(table_path, limit=request["LIMIT"])
            response = build_response_body(
                mode="mock-http",
                table=request["TABLE"],
                sql=request["SQL"],
                columns=data["columns"],
                rows=data["rows"],
                scanned_rows=data["row_count"],
            )
            self._send_text(200, response)
        except (ValueError, TableFormatError) as exc:
            self._send_text(400, f"STATUS=ERROR\nMESSAGE={exc}\n")
        except FileNotFoundError:
            self._send_text(404, "STATUS=ERROR\nMESSAGE=Arquivo de tabela nao encontrado.\n")

    def log_message(self, fmt: str, *args) -> None:
        print(f"[mock-http] {self.address_string()} - {fmt % args}")


def main() -> None:
    parser = argparse.ArgumentParser(description="Mock local do servidor HTTP da DE2.")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8080)
    parser.add_argument("--root", default=str(DEFAULT_ROOT), help="Diretorio das tabelas .tbl8")
    args = parser.parse_args()

    table_root = Path(args.root)
    create_sample_tables(table_root)
    QueryHandler.table_root = table_root

    server = ThreadingHTTPServer((args.host, args.port), QueryHandler)
    print(f"Mock HTTP ouvindo em http://{args.host}:{args.port}/query")
    print(f"Tabelas disponiveis em {table_root.resolve()}")

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("Encerrando mock local.")
    finally:
        server.server_close()


if __name__ == "__main__":
    main()