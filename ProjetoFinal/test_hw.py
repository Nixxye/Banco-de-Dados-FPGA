import argparse
import re

# Simulated Avalon-MM memory map based on new specification
AVALON_MM = {
    "CONTROL": 0x00,
    "DATA_IN": 0x04,
    "INSTRUCTION": 0x08,
    "STATUS": 0x0C,
    "DATA_OUT": 0x10,
    "ACCUMULATOR": 0x14,
}

class HardwareMock:
    def __init__(self):
        self.memory = {v: 0 for v in AVALON_MM.values()}

    def write(self, address: int, data: int):
        self.memory[address] = data
        if address == AVALON_MM["INSTRUCTION"]:
            self.decode_instruction(data)

    def read(self, address: int) -> int:
        return self.memory.get(address, 0)

    def decode_instruction(self, inst: int):
        opcode = (inst >> 28) & 0xF
        if opcode == 1:
            col_idx = (inst >> 11) & 0x3F
            val = (inst >> 3) & 0xFF
            op = inst & 0x7
            op_str = ["==", "!=", "<", "<=", ">", ">="]
            op_s = op_str[op] if op < len(op_str) else str(op)
            print(f"[HW MOCK] INSTRUCTION WHERE: col_idx={col_idx}, val={val}, op={op_s} (Raw: 0x{inst:08X})")
        elif opcode == 2:
            limit = inst & 0xFF
            print(f"[HW MOCK] INSTRUCTION LIMIT: {limit} (Raw: 0x{inst:08X})")
        elif opcode == 3:
            col_idx = inst & 0x3F
            print(f"[HW MOCK] INSTRUCTION COUNT: col_idx={col_idx} (Raw: 0x{inst:08X})")
        else:
            print(f"[HW MOCK] UNKNOWN INSTRUCTION: opcode={opcode} (Raw: 0x{inst:08X})")

def compile_and_send_instructions(hw: HardwareMock, sql: str, schema: dict):
    # Derive LIMIT
    limit = 8
    match_limit = re.search(r"\blimit\s+(\d+)", sql, re.IGNORECASE)
    if match_limit:
        limit = min(max(int(match_limit.group(1)), 1), 16)
    
    inst_limit = (2 << 28) | (limit & 0xFF)
    hw.write(AVALON_MM["INSTRUCTION"], inst_limit)

    # Derive WHERE
    match_where = re.search(r"\bwhere\s+([a-zA-Z0-9_]+)\s*([=!<>]+)\s*(\d+)", sql, re.IGNORECASE)
    if match_where:
        col_name = match_where.group(1)
        op_str = match_where.group(2)
        val = int(match_where.group(3))
        
        col_idx = schema.get(col_name, 0)
        
        op_code = 0
        if op_str in ("=", "=="): op_code = 0
        elif op_str == "!=": op_code = 1
        elif op_str == "<": op_code = 2
        elif op_str == "<=": op_code = 3
        elif op_str == ">": op_code = 4
        elif op_str == ">=": op_code = 5

        inst_where = (1 << 28) | ((col_idx & 0x3F) << 11) | ((val & 0xFF) << 3) | (op_code & 0x7)
        hw.write(AVALON_MM["INSTRUCTION"], inst_where)

    # Derive SELECT COUNT
    match_count = re.search(r"\bselect\s+count\s*\(\s*([a-zA-Z0-9_]+)\s*\)", sql, re.IGNORECASE)
    if match_count:
        col_name = match_count.group(1)
        col_idx = schema.get(col_name, 0)
        inst_count = (3 << 28) | (col_idx & 0x3F)
        hw.write(AVALON_MM["INSTRUCTION"], inst_count)

def main() -> None:
    parser = argparse.ArgumentParser(description="Script Python para mockar envio de instrucoes 32-bit para Avalon-MM.")
    parser.add_argument("--sql", default="SELECT COUNT(curso) FROM alunos WHERE nota >= 5 LIMIT 4")
    args = parser.parse_args()

    sql = " ".join(args.sql.split())

    # Simulated schema
    schema = {
        "id": 0,
        "nome": 1,
        "curso": 2,
        "nota": 3
    }

    hw = HardwareMock()
    print(f"Mocking query: {sql}")
    compile_and_send_instructions(hw, sql, schema)

if __name__ == "__main__":
    main()
