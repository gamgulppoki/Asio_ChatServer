import argparse
import json
import os
import jinja2


# 스칼라 타입 매핑: C++ 타입, SQL 타입, C++ 기본값
SCALAR_TYPES = {
    "int32":  {"cpp": "int32",  "sql": "INT",      "default": "0"},
    "int64":  {"cpp": "int64",  "sql": "BIGINT",   "default": "0"},
    "int16":  {"cpp": "int16",  "sql": "SMALLINT", "default": "0"},
    "float":  {"cpp": "float",  "sql": "FLOAT",    "default": "0.0f"},
    "double": {"cpp": "double", "sql": "FLOAT",    "default": "0.0"},
    "bool":   {"cpp": "bool",   "sql": "BIT",      "default": "false"},
}


def build_column_strings(col, struct_name):
    """컬럼 dict에 C++/SQL 파생 문자열 4개를 채워 넣는다."""
    name = col["name"]
    t = col["type"]
    primary = col.get("primary", False)
    autoinc = col.get("autoincrement", False)

    if t == "wchar":
        size = col["size"]
        col["cpp_decl"]     = f"WCHAR {name}[{size}] = {{}};"
        col["cpp_addcol"]   = f'Model.AddColumn(L"{name}", &{struct_name}::{name});'
        col["cpp_col_decl"] = (
            f'static inline StringColumn<{size}, {struct_name}> '
            f'{name}{{L"{name}", &{struct_name}::{name}}};'
        )
        col["sql_decl"]     = f"{name} NVARCHAR({size})"
        return

    if t in SCALAR_TYPES:
        spec = SCALAR_TYPES[t]
        col["cpp_decl"] = f"{spec['cpp']} {name} = {spec['default']};"

        autoinc_flag = ", true" if autoinc else ""
        col["cpp_addcol"] = f'Model.AddColumn(L"{name}", &{struct_name}::{name}{autoinc_flag});'

        col["cpp_col_decl"] = (
            f'static inline Column<{spec["cpp"]}, {struct_name}> '
            f'{name}{{L"{name}", &{struct_name}::{name}}};'
        )

        sql_parts = [f"{name} {spec['sql']}"]
        if autoinc:
            sql_parts.append("IDENTITY(1,1)")
        if primary:
            sql_parts.append("PRIMARY KEY")
        col["sql_decl"] = " ".join(sql_parts)
        return

    raise ValueError(f"Unknown column type: {t}")


def main():
    arg_parser = argparse.ArgumentParser(description="ModelGenerator")
    arg_parser.add_argument("--input",  required=True, help="schema JSON file path")
    arg_parser.add_argument("--db-dir", required=True, help="Server/DB directory")
    args = arg_parser.parse_args()

    with open(args.input, "r", encoding="utf-8") as f:
        data = json.load(f)

    table = data["table"]
    struct_name = data["struct_name"]
    columns = data["columns"]

    for col in columns:
        build_column_strings(col, struct_name)

    env = jinja2.Environment(
        loader=jinja2.FileSystemLoader("Templates"),
        keep_trailing_newline=True,
    )

    context = {"table": table, "struct_name": struct_name, "columns": columns}

    models_dir = os.path.join(args.db_dir, "Models")
    schema_dir = os.path.join(args.db_dir, "Schema")
    os.makedirs(models_dir, exist_ok=True)
    os.makedirs(schema_dir, exist_ok=True)

    # .h는 UTF-8 with BOM (rules.md), .sql은 UTF-8
    outputs = [
        ("Model.h",    os.path.join(models_dir, f"{struct_name}Model.h"), "utf-8-sig"),
        ("Cols.h",     os.path.join(models_dir, f"{struct_name}Cols.h"),  "utf-8-sig"),
        ("Schema.sql", os.path.join(schema_dir, f"{table}.sql"),          "utf-8"),
    ]

    for template_name, out_path, enc in outputs:
        rendered = env.get_template(template_name).render(**context)
        with open(out_path, "w", encoding=enc) as f:
            f.write(rendered)
        print(f"Generated {out_path}")


if __name__ == "__main__":
    main()