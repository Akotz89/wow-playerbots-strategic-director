"""Keep the compiled schema snapshot semantically identical to shipped schemas."""
import json
from pathlib import Path

root = Path(__file__).resolve().parents[1]
header = (root / "src/canonical_schemas.hpp").read_text(encoding="utf-8")
start, end = 'R"SCHEMAS(', ')SCHEMAS"'
assert header.count(start) == 1 and header.count(end) == 1, "schema embedding delimiters changed"
embedded = json.loads(header.split(start, 1)[1].split(end, 1)[0])
shipped = {path.name: json.loads(path.read_text(encoding="utf-8"))
           for path in sorted((root / "schemas").glob("*.json"))}
assert shipped and embedded == shipped, "compiled schema snapshot differs from shipped schema documents"
print(f"schema parity: {len(shipped)} documents")
