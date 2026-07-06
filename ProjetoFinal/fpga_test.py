import urllib.request
import urllib.error
import re
import argparse
import time
import os

try:
    import table_format
except ImportError:
    table_format = None

def parse_conditions(where_clause):
    conditions = []
    if where_clause:
        parts = where_clause.split(' AND ')
        for p in parts:
            p = p.strip()
            match = re.match(r"(\w+)\s*([<>=!]+)\s*(\w+)", p)
            if match:
                conditions.append((match.group(1), match.group(2), match.group(3)))
    return conditions

def evaluate_row(row_dict, conditions):
    for col, op, val in conditions:
        if col not in row_dict:
            return False
        c_val = row_dict[col]
        try:
            cv = float(c_val)
            vv = float(val)
        except:
            cv = c_val
            vv = val

        if op == '=':
            if cv != vv: return False
        elif op == '!=':
            if cv == vv: return False
        elif op == '>':
            if cv <= vv: return False
        elif op == '<':
            if cv >= vv: return False
        elif op == '>=':
            if cv < vv: return False
        elif op == '<=':
            if cv > vv: return False
    return True

def run_query(sql, host, expect_error=False):
    url = f"http://{host}:80/query"
    print(f"\n{'-'*60}")
    print(f"Executando: {sql}")
    
    # Extrair tabela
    match_from = re.search(r"\bFROM\s+([a-zA-Z0-9_]+)", sql, re.IGNORECASE)
    if not match_from:
        print("Erro: Tabela nao encontrada na query!")
        return
    table = match_from.group(1)
    
    # Extrair where e limit para o gabarito
    match_where = re.search(r"\bWHERE\s+(.*?)(?:\s+LIMIT|\Z)", sql, re.IGNORECASE)
    where_clause = match_where.group(1) if match_where else ""
    
    match_limit = re.search(r"\bLIMIT\s+(\d+)", sql, re.IGNORECASE)
    limit = int(match_limit.group(1)) if match_limit else 0
    
    is_count = "COUNT(" in sql.upper()
    is_sum = "SUM(" in sql.upper()
    sum_col = None
    if is_sum:
        m = re.search(r"SUM\((\w+)\)", sql, re.IGNORECASE)
        if m: sum_col = m.group(1)
    
    
    payload = f"TABLE={table}\nLIMIT={limit}\nSQL={sql}"
    
    # Rodar Gabarito Python
    gabarito_rows = []
    gabarito_expected_val = None
    gabarito_found = False
    
    if table_format is not None:
        tbl_path = f"DE2_NET/sd_card/{table}.tbl"
        if os.path.exists(tbl_path):
            try:
                table_data = table_format.read_table(tbl_path, limit=255)
                columns = table_data['columns']
                rows = table_data['rows']
                conditions = parse_conditions(where_clause)
                
                matched = []
                for r in rows:
                    row_dict = dict(zip(columns, r))
                    if evaluate_row(row_dict, conditions):
                        matched.append(r)
                
                if is_count:
                    gabarito_expected_val = str(len(matched))
                elif is_sum:
                    total = 0
                    sum_idx = columns.index(sum_col)
                    for r in matched:
                        total += int(r[sum_idx])
                    gabarito_expected_val = str(total)
                else:
                    if limit > 0: matched = matched[:limit]
                    for r in matched:
                        gabarito_rows.append("|".join(r))
                gabarito_found = True
            except Exception as e:
                print(f"[GABARITO] Falha ao ler {tbl_path}: {e}")
    
    for attempt in range(3):
        try:
            req = urllib.request.Request(url, data=payload.encode('utf-8'), method='POST')
            with urllib.request.urlopen(req, timeout=5) as response:
                result = response.read().decode('utf-8')
                print("\nResposta da Placa:")
                print(result.strip())
                
                test_passed = False
                if gabarito_found:
                    print("\n--- VERIFICACAO DO GABARITO ---")
                    lines = result.strip().split('\n')
                    fpga_lines = [l for l in lines if l.startswith("ROW=")]
                    
                    if is_count or is_sum:
                        fpga_val = fpga_lines[0].split("=")[1] if fpga_lines else "N/A"
                        if gabarito_expected_val == fpga_val:
                            print(f"[PASS] Agregador perfeito! Esperado: {gabarito_expected_val}, Hardware: {fpga_val}")
                            test_passed = True
                        else:
                            print(f"[FAIL] Agregador erro! Esperado: {gabarito_expected_val}, Hardware: {fpga_val}")
                            test_passed = False
                    else:
                        fpga_contents = [fl.split("=", 1)[1] for fl in fpga_lines if "=" in fl]
                        if len(gabarito_rows) != len(fpga_contents):
                            print(f"[FAIL] Quantidade de linhas difere! Esperado: {len(gabarito_rows)}, Hardware: {len(fpga_contents)}")
                            test_passed = False
                        else:
                            failed = False
                            for i, (exp, fpg) in enumerate(zip(gabarito_rows, fpga_contents)):
                                if exp != fpg:
                                    print(f"[FAIL] Linha divergente! Esp: {exp} | Hw: {fpg}")
                                    failed = True
                            if not failed:
                                if expect_error:
                                    print("[FAIL] A query deveria ter retornado erro (Negative Test), mas a placa aceitou!")
                                    test_passed = False
                                else:
                                    print(f"[PASS] {len(gabarito_rows)} linhas processadas de forma IDENTICA ao Gabarito Python!")
                                    test_passed = True
                            else:
                                test_passed = False

                time.sleep(0.5)
                return test_passed
        except urllib.error.HTTPError as e:
            if e.code == 503:
                print(f"Placa ocupada (503). Tentativa {attempt+1}/3. Aguardando 1s...")
                time.sleep(1)
            else:
                if expect_error and (e.code == 400 or e.code == 404 or e.code == 500):
                    err_body = e.read().decode('utf-8', errors='ignore')
                    print(f"\n[PASS] Erro corretamente barrado pela placa (Negative Test)!\nPlaca Respondeu ({e.code}):\n{err_body.strip()}")
                    return True
                else:
                    print(f"Erro HTTP {e.code}: {e.reason}")
                    time.sleep(0.5)
                    return False
        except Exception as e:
            print(f"Erro na requisicao para {url}: {e}")
            time.sleep(0.5)
            return False

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Script de testes automatizados para a FPGA.")
    parser.add_argument("--ip", default="192.168.0.14", help="Endereco IP da placa DE2")
    args = parser.parse_args()

    TEST_QUERIES = [
        "SELECT * FROM alunos",
        "SELECT * FROM alunos LIMIT 2",
        "SELECT * FROM produtos LIMIT 5",
        
        "SELECT * FROM alunos WHERE c = C LIMIT 10",
        "SELECT * FROM alunos WHERE t > 5 LIMIT 10",
        "SELECT * FROM leituras WHERE v = 3",
        "SELECT * FROM leituras WHERE v > 5",
        "SELECT * FROM sensores WHERE s = 1",
        "SELECT * FROM vendas WHERE v < 5",
        "SELECT * FROM produtos WHERE q >= 5",
        "SELECT * FROM produtos WHERE q <= 3",
        "SELECT * FROM produtos WHERE t != A LIMIT 10",
        
        "SELECT * FROM alunos WHERE c = C AND t >= 7 LIMIT 10",
        "SELECT * FROM leituras WHERE s = T AND v > 3 AND d < 8",
        "SELECT * FROM sensores WHERE l = N AND s = 1 AND t = A",
        "SELECT * FROM vendas WHERE v > 3 AND v < 8",
        
        "SELECT * FROM alunos WHERE t > 9",
        "SELECT * FROM leituras WHERE v > 9",
        "SELECT * FROM sensores WHERE l = Z",
        "SELECT * FROM vendas WHERE v > 5 AND v < 2",
        "SELECT * FROM produtos WHERE q = 9 AND q = 1",
        
        "SELECT COUNT(i) FROM alunos",
        "SELECT COUNT(s) FROM leituras WHERE s = T",
        "SELECT COUNT(i) FROM produtos WHERE t = A",
        "SELECT SUM(v) FROM leituras WHERE s = T",
        "SELECT SUM(q) FROM produtos WHERE t = B",
        "SELECT SUM(v) FROM vendas WHERE c = A",
        
        # Testes Negativos
        ("SELECT * FROM alunos WHERE coluna_fantasma = 1", True),
        ("SELECT coluna_imaginaria FROM alunos", True),
        ("SELECT i, coluna_imaginaria FROM alunos", True),
        ("SELECT SUM(n) FROM alunos", True),
        ("SELECT * FROM tabela_fantasma", True),
        ("SELECT COUNT(x) FROM alunos", True),
    ]

    print(f"Iniciando Bateria de {len(TEST_QUERIES)} Testes na placa {args.ip} ...\n")
    
    passed_count = 0
    failed_count = 0

    for sql in TEST_QUERIES:
        if isinstance(sql, tuple):
            query, expect_err = sql
        else:
            query, expect_err = sql, False
            
        if run_query(query, args.ip, expect_error=expect_err):
            passed_count += 1
        else:
            failed_count += 1
        
    print("\n" + "-"*60)
    print(f"Testes finalizados! Passaram: {passed_count} | Falharam: {failed_count}")