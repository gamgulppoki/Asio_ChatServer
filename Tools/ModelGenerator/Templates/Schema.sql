IF NOT EXISTS (SELECT * FROM sys.tables WHERE name = '{{ table }}')
BEGIN
    CREATE TABLE {{ table }} (
{%- for col in columns %}
        {{ col.sql_decl }}{% if not loop.last %},{% endif %}
{%- endfor %}
    );
END