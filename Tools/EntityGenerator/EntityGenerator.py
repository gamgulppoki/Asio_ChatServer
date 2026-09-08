"""
ORM codegen.

Server/DB/Entities/*.h 에서 DB_ENTITY 표식이 붙은 struct 를 스캔해
Server/DB/Generated/EntitiesGenerated.h (describe_entity / Col<T> 특수화) 를 Jinja2 템플릿으로 렌더링한다.

읽는 표식 (Server/DB/ORM/Attributes.h):
    DB_ENTITY                       엔티티
    FK(col)                         Navigation<T> 의 외래키 컬럼
    LEN(n)                          문자열 컬럼 길이 → NVARCHAR(n)
    INDEX / UNIQUE                  단일 컬럼 (유니크) 인덱스
    COMPOSITE_INDEX(a, b, ...)      복합 인덱스 (struct 본문 문장)
    COMPOSITE_UNIQUE(a, b, ...)     복합 유니크 인덱스

인덱스 이름 규칙: IX_<Table>_<Col1>_<Col2>, 유니크는 UX_ 접두.

Usage:
    python EntityGenerator.py <entities_dir> <output_dir>

Dependencies:
    pip install jinja2
"""
import re
import sys
from pathlib import Path

try:
    from jinja2 import Environment, FileSystemLoader, StrictUndefined
except ImportError:
    print('jinja2 required: pip install jinja2', file=sys.stderr)
    sys.exit(1)


ENTITY_DECL   = re.compile(
    r'\bDB_ENTITY\b\s+(?:class|struct)\s+(\w+)'
)
ATTRIBUTE     = re.compile(r'\[\[[^\]]*\]\]')
BLOCK_COMMENT = re.compile(r'/\*.*?\*/', re.DOTALL)
LINE_COMMENT  = re.compile(r'//[^\n]*')
ACCESS_SPEC   = re.compile(r'\b(?:public|private|protected)\s*:')

HERE          = Path(__file__).resolve().parent
TEMPLATE_DIR  = HERE / 'Templates'
TEMPLATE_NAME = 'EntitiesGenerated.h'


def strip_comments(text: str) -> str:
    text = BLOCK_COMMENT.sub('', text)
    text = LINE_COMMENT.sub('', text)
    return text


def find_matching_brace(text: str, open_pos: int) -> int:
    depth = 0
    for i in range(open_pos, len(text)):
        ch = text[i]
        if ch == '{':
            depth += 1
        elif ch == '}':
            depth -= 1
            if depth == 0:
                return i
    return -1


PROPERTY_INNER   = re.compile(r'^\s*(?:Primary)?Property\s*<\s*(.+?)\s*>\s*$')
NAVIGATION_INNER = re.compile(r'^\s*Navigation\s*<\s*(.+?)\s*>\s*$')
FK_MACRO         = re.compile(r'\bFK\s*\(\s*(\w+)\s*\)')
LEN_MACRO        = re.compile(r'\bLEN\s*\(\s*(\d+)\s*\)')
COMPOSITE_MACRO  = re.compile(r'\b(COMPOSITE_INDEX|COMPOSITE_UNIQUE)\s*\(([^)]*)\)')
INDEX_WORDS      = {'INDEX': False, 'UNIQUE': True}   # 단어 → unique 여부


def parse_members(body: str):
    """struct 본문 → (fields, indexes). indexes 는 {'unique': bool, 'columns': [..]} 목록 (이름은 나중에)."""
    fields = []
    indexes = []
    for raw in body.split(';'):
        stmt = ACCESS_SPEC.sub('', raw)
        stmt = ATTRIBUTE.sub('', stmt).strip()
        if not stmt:
            continue

        # 복합 인덱스 문장: COMPOSITE_UNIQUE(A, B) — 필드가 아니므로 여기서 소비
        comp = COMPOSITE_MACRO.search(stmt)
        if comp:
            columns = [c.strip() for c in comp.group(2).split(',') if c.strip()]
            indexes.append({'unique': comp.group(1) == 'COMPOSITE_UNIQUE', 'columns': columns})
            continue

        # FK(XXX) / LEN(n) 매크로 추출 후 제거. '(' 스킵 체크 이전에 처리해야 함
        fk_match = FK_MACRO.search(stmt)
        fk_column = fk_match.group(1) if fk_match else None
        if fk_match:
            stmt = FK_MACRO.sub('', stmt).strip()

        len_match = LEN_MACRO.search(stmt)
        max_len = int(len_match.group(1)) if len_match else 0
        if len_match:
            stmt = LEN_MACRO.sub('', stmt).strip()

        if '(' in stmt:              # skip methods
            continue
        stmt = re.sub(r'=.*$', '', stmt).strip()
        tokens = stmt.split()

        # INDEX / UNIQUE 단어 추출
        index_flags = [INDEX_WORDS[t] for t in tokens if t in INDEX_WORDS]
        tokens = [t for t in tokens if t not in INDEX_WORDS]

        if len(tokens) < 2:
            continue
        name = tokens[-1]
        type_str = ' '.join(tokens[:-1])

        # Navigation<T> 판별
        nav_match = NAVIGATION_INNER.match(type_str)
        is_nav = bool(nav_match)

        # inner T 추출: Navigation<T> → T, Property<T>/PrimaryProperty<T> → T
        if is_nav:
            inner_type = nav_match.group(1).strip()
        else:
            m = PROPERTY_INNER.match(type_str)
            inner_type = m.group(1).strip() if m else type_str

        # PK 판별: PrimaryProperty<T> 로 선언된 필드
        is_pk = type_str.lstrip().startswith('PrimaryProperty')

        fields.append({
            'type': type_str,
            'name': name,
            'inner_type': inner_type,
            'is_pk': is_pk,
            'is_nav': is_nav,
            'fk_column': fk_column,
            'max_len': max_len,
        })

        if index_flags and not is_nav:
            # UNIQUE 와 INDEX 를 같이 쓰면 UNIQUE 가 이긴다
            indexes.append({'unique': any(index_flags), 'columns': [name]})
    return fields, indexes


def index_name(entity: str, index: dict) -> str:
    prefix = 'UX' if index['unique'] else 'IX'
    return f"{prefix}_{entity}_{'_'.join(index['columns'])}"


def parse_entities(text: str):
    text = strip_comments(text)
    entities = []
    for m in ENTITY_DECL.finditer(text):
        name = m.group(1)
        brace_start = text.find('{', m.end())
        if brace_start < 0:
            continue
        brace_end = find_matching_brace(text, brace_start)
        if brace_end < 0:
            continue
        body = text[brace_start + 1:brace_end]
        members, indexes = parse_members(body)
        pk = next((f['name'] for f in members if f.get('is_pk')), None)

        field_names = {f['name'] for f in members if not f['is_nav']}
        for ix in indexes:
            unknown = [c for c in ix['columns'] if c not in field_names]
            if unknown:
                print(f"error: {name}: index refers to unknown column(s) {unknown}", file=sys.stderr)
                sys.exit(1)
            ix['name'] = index_name(name, ix)

        entities.append({'name': name, 'members': members, 'pk': pk, 'indexes': indexes})
    return entities


def main():
    if len(sys.argv) < 3:
        print('usage: EntityGenerator.py <entities_dir> <output_dir>', file=sys.stderr)
        sys.exit(2)
    entities_dir = Path(sys.argv[1])
    output_dir   = Path(sys.argv[2])
    output_dir.mkdir(parents=True, exist_ok=True)

    all_entities = []
    files_with_entities = []
    for header in sorted(entities_dir.glob('*.h')):
        text = header.read_text(encoding='utf-8')
        ents = parse_entities(text)
        if ents:
            all_entities.extend(ents)
            files_with_entities.append(header.name)

    env = Environment(
        loader=FileSystemLoader(str(TEMPLATE_DIR)),
        undefined=StrictUndefined,
        trim_blocks=True,
        lstrip_blocks=False,
        keep_trailing_newline=True,
    )
    template = env.get_template(TEMPLATE_NAME)
    out_text = template.render(entities=all_entities, entity_files=files_with_entities)

    out_path = output_dir / 'EntitiesGenerated.h'
    if out_path.exists() and out_path.read_text(encoding='utf-8') == out_text:
        print(f'codegen: up to date ({len(all_entities)} entities)')
        return
    out_path.write_text(out_text, encoding='utf-8')
    print(f'codegen: wrote {out_path} ({len(all_entities)} entities)')


if __name__ == '__main__':
    main()
