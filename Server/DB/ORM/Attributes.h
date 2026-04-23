#pragma once

// ORM codegen markers.
// These expand to nothing — they exist only for tools/codegen.py to scan.
// Having them lets the compiler see plain C++ while the generator sees markers.

#define DB_ENTITY
#define FK(col)