"""요청마다 JSON 한 줄(JSON Lines). 오디오·키·토큰은 기록하지 않는다."""
from __future__ import annotations

import json
import threading
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

_lock = threading.Lock()


def write_log(path: Path, record: dict[str, Any]) -> None:
    record = {"time": datetime.now(timezone.utc).isoformat(timespec="milliseconds"), **record}
    path.parent.mkdir(parents=True, exist_ok=True)
    line = json.dumps(record, ensure_ascii=False, default=str)
    with _lock, path.open("a", encoding="utf-8") as f:
        f.write(line + "\n")
