import argparse
from pathlib import Path

from table_format import create_sample_tables


def main() -> None:
    parser = argparse.ArgumentParser(description="Gera tabelas .tbl8 para teste local ou copia para o SD.")
    parser.add_argument("--root", default="mock_sd/tables", help="Diretorio onde os arquivos .tbl8 serao gerados.")
    args = parser.parse_args()

    created = create_sample_tables(Path(args.root))
    print("Arquivos gerados:")
    for path in created:
        print(path.resolve())


if __name__ == "__main__":
    main()