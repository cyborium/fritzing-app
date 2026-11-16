"""Offline/Direct SQLite Parts Catalog reader for Fritzing parts.db."""

from __future__ import annotations

from contextlib import closing
from pathlib import Path
import sqlite3
from typing import Any

DEFAULT_DB_PATH = Path(r"D:\Workspaces\fritzing-mcp\fritzing-parts\parts.db")


class PartsCatalog:
    """Read-only SQLite interface to Fritzing's parts.db."""

    def __init__(self, db_path: str | Path = DEFAULT_DB_PATH) -> None:
        self.db_path = Path(db_path)

    def _connect(self) -> sqlite3.Connection:
        if not self.db_path.is_file():
            raise FileNotFoundError(f"Fritzing parts database not found at {self.db_path}")
        uri = f"{self.db_path.resolve().as_uri()}?mode=ro"
        conn = sqlite3.connect(uri, uri=True, timeout=5.0)
        conn.row_factory = sqlite3.Row
        return conn

    def search_parts(self, query: str, limit: int = 25) -> dict[str, Any]:
        """Search parts by title, family, moduleID, description, or property values."""
        clean_query = query.strip() if isinstance(query, str) else ""
        if not clean_query:
            raise ValueError("query must be a non-empty string")
        if not isinstance(limit, int) or isinstance(limit, bool) or not (1 <= limit <= 500):
            raise ValueError("limit must be an integer between 1 and 500")

        pattern = f"%{clean_query}%"
        sql = """
            SELECT DISTINCT p.id, p.moduleID, p.title, p.family, p.description
            FROM parts p
            LEFT JOIN properties pr ON pr.part_id = p.id
            LEFT JOIN tags t ON t.part_id = p.id
            WHERE (p.replacedby IS NULL OR p.replacedby = '')
              AND (
                  p.title LIKE ? COLLATE NOCASE
                  OR p.family LIKE ? COLLATE NOCASE
                  OR p.moduleID LIKE ? COLLATE NOCASE
                  OR p.description LIKE ? COLLATE NOCASE
                  OR pr.value LIKE ? COLLATE NOCASE
                  OR t.tag LIKE ? COLLATE NOCASE
              )
            ORDER BY p.title ASC
            LIMIT ?
        """
        with closing(self._connect()) as conn:
            rows = conn.execute(sql, (*([pattern] * 6), limit)).fetchall()
            results = [self._build_part_summary(conn, row) for row in rows]

        return {
            "ok": True,
            "query": clean_query,
            "totalFound": len(results),
            "results": results,
        }

    def get_part_info(self, module_id: str) -> dict[str, Any]:
        """Retrieve full part metadata including properties and connectors by moduleID."""
        clean_id = module_id.strip() if isinstance(module_id, str) else ""
        if not clean_id:
            raise ValueError("module_id must be a non-empty string")

        sql = """
            SELECT id, moduleID, title, family, description
            FROM parts
            WHERE moduleID = ?
            LIMIT 1
        """
        with closing(self._connect()) as conn:
            row = conn.execute(sql, (clean_id,)).fetchone()
            if row is None:
                raise LookupError(f"Part with moduleID '{clean_id}' not found")
            info = self._build_part_summary(conn, row)
            info["connectors"] = self._fetch_connectors(conn, int(row["id"]))
            info["ok"] = True
            return info

    @staticmethod
    def _build_part_summary(conn: sqlite3.Connection, row: sqlite3.Row) -> dict[str, Any]:
        part_id = int(row["id"])
        prop_rows = conn.execute(
            "SELECT name, value FROM properties WHERE part_id = ?",
            (part_id,),
        ).fetchall()
        props = {str(r["name"]): str(r["value"]) for r in prop_rows if r["name"]}
        desc = str(row["description"] or "")
        return {
            "moduleID": str(row["moduleID"] or ""),
            "title": str(row["title"] or ""),
            "family": str(row["family"] or ""),
            "description": desc[:200],
            "properties": props,
        }

    @staticmethod
    def _fetch_connectors(conn: sqlite3.Connection, part_id: int) -> list[dict[str, str]]:
        sql = (
            "SELECT connectorid, name, description, type "
            "FROM connectors WHERE part_id = ? ORDER BY id ASC"
        )
        conn_rows = conn.execute(sql, (part_id,)).fetchall()
        return [
            {
                "id": str(r["connectorid"] or ""),
                "name": str(r["name"] or ""),
                "description": str(r["description"] or ""),
                "type": str(r["type"] or ""),
            }
            for r in conn_rows
        ]
