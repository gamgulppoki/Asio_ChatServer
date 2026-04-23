"""
ORMTest codegen.

Scans Entities/*.h for [[db::entity]] attributes and renders
EntitiesGenerated.h via a Jinja2 template.

Usage:
    python codegen.py <entities_dir> <output_dir>

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


def parse_members(body: str):
    fields = []
    for raw in body.split(';'):
        stmt = ACCESS_SPEC.sub('', raw)
        stmt = ATTRIBUTE.sub('', stmt).strip()
        if not stmt:
            continue

        # FK(XXX) 매크로 추출 후 제거. '(' 스킵 체크 이전에 처리해야 함
        fk_match = FK_MACRO.search(stmt)
        fk_column = fk_match.group(1) if fk_match else None
        if fk_match:
            stmt = FK_MACRO.sub('', stmt).strip()

        if '(' in stmt:              # skip methods
            continue
        stmt = re.sub(r'=.*$', '', stmt).strip()
        tokens = stmt.split()
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
        })
    return fields


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
        members = parse_members(body)
        pk = next((f['name'] for f in members if f.get('is_pk')), None)
        entities.append({'name': name, 'members': members, 'pk': pk})
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