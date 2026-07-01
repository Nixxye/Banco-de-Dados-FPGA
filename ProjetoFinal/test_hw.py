import argparse
import http.client
import re


def derive_table(sql: str) -> str:
    match = re.search(r"\bfrom\s+([A-Za-z0-9_]+)", sql, re.IGNORECASE)
    if not match:
        raise ValueError("Nao foi possivel identificar a tabela a partir do trecho FROM.")
    return match.group(1)


def derive_limit(sql: str) -> int:
    match = re.search(r"\blimit\s+(\d+)", sql, re.IGNORECASE)
    if not match:
        return 8
    return min(max(int(match.group(1)), 1), 16)


def parse_response(text: str) -> dict:
    result = {"rows": []}
    for line in text.splitlines():
        if line.startswith("ROW="):
            result["rows"].append(line[4:].split("|"))
        elif "=" in line:
            key, value = line.split("=", 1)
            result[key] = value
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description="Cliente HTTP para testar o firmware da DE2 ou o mock local.")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8080)
    parser.add_argument("--path", default="/query")
    parser.add_argument("--sql", default="SELECT id, nome, curso FROM alunos LIMIT 4")
    parser.add_argument("--table", default="")
    parser.add_argument("--limit", type=int, default=0)
    args = parser.parse_args()

    sql = " ".join(args.sql.split())
    table = args.table or derive_table(sql)
    limit = args.limit or derive_limit(sql)

    body = f"TABLE={table}\nLIMIT={limit}\nSQL={sql}\n"

    print(f"Conectando em http://{args.host}:{args.port}{args.path}")
    print("Payload enviado:")
    print(body)

    connection = http.client.HTTPConnection(args.host, args.port, timeout=10)
    connection.request(
        "POST",
        args.path,
        body=body.encode("utf-8"),
        headers={"Content-Type": "text/plain; charset=utf-8"},
    )

    response = connection.getresponse()
    text = response.read().decode("utf-8", "replace")
    connection.close()

    print(f"HTTP {response.status} {response.reason}\n")
    print(text)

    parsed = parse_response(text)
    if parsed.get("STATUS") == "OK" and parsed.get("rows"):
        columns = parsed.get("COLUMNS", "").split(",")
        print("Tabela retornada:")
        print(" | ".join(columns))
        print("-" * max(12, len(" | ".join(columns))))
        for row in parsed["rows"]:
            print(" | ".join(row))


if __name__ == "__main__":
    main()
