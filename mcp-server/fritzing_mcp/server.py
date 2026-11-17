"""FastMCP Server exposing Fritzing EDA tools to AI models."""

from __future__ import annotations

import base64
from pathlib import Path
from typing import Any

from mcp.server.mcpserver import Image, MCPServer

from .catalog import DEFAULT_DB_PATH, PartsCatalog
from .client import BridgeError, FritzingBridgeClient

mcp = MCPServer("fritzing-mcp")

_bridge = FritzingBridgeClient()
_catalog = PartsCatalog(DEFAULT_DB_PATH)


def configure_dependencies(
    bridge: FritzingBridgeClient | None = None,
    catalog: PartsCatalog | None = None,
) -> None:
    """Allow overriding bridge and catalog instances (e.g. in unit tests)."""
    global _bridge, _catalog
    if bridge is not None:
        _bridge = bridge
    if catalog is not None:
        _catalog = catalog


@mcp.tool()
def check_connection() -> dict[str, Any]:
    """Check whether the live Fritzing application bridge and offline parts.db are available."""
    online = _bridge.is_online()
    db_exists = _catalog.db_path.is_file()
    methods: list[str] = []
    if online:
        health_info = _bridge.health()
        methods = list(health_info.get("methods", []))
    return {
        "ok": True,
        "fritzingOnline": online,
        "bridgeUrl": _bridge.base_url,
        "offlinePartsDbAvailable": db_exists,
        "partsDbPath": str(_catalog.db_path),
        "supportedMethods": methods,
    }


@mcp.tool()
def get_sketch_state(view: str = "") -> dict[str, Any]:
    """Get all placed parts, pins, and wires in the current Fritzing sketch."""
    params: dict[str, Any] = {}
    if view.strip():
        params["view"] = view.strip().lower()
    return _bridge.call("sketch.get_state", params)


@mcp.tool()
def set_active_view(view: str) -> dict[str, Any]:
    """Switch the active Fritzing view ('breadboard', 'schematic', or 'pcb')."""
    return _bridge.call("sketch.set_view", {"view": view})


@mcp.tool()
def undo_action() -> dict[str, Any]:
    """Undo the last operation in the active Fritzing sketch."""
    return _bridge.call("sketch.undo")


@mcp.tool()
def redo_action() -> dict[str, Any]:
    """Redo the last undone operation in the active Fritzing sketch."""
    return _bridge.call("sketch.redo")


@mcp.tool()
def capture_view_screenshot(
    view: str = "",
    width: int = 1200,
    height: int = 900,
    crop_to_items: bool = True,
    output_path: str = "",
) -> Image:
    """Render the active or specified Fritzing view as a PNG image for visual inspection."""
    params: dict[str, Any] = {
        "format": "png",
        "width": width,
        "height": height,
        "cropToItems": crop_to_items,
    }
    if view.strip():
        params["view"] = view.strip().lower()
    if output_path.strip():
        params["outputPath"] = str(Path(output_path.strip()))
    res = _bridge.call("sketch.render_image", params)
    b64_data = str(res.get("imageBase64", ""))
    png_bytes = base64.b64decode(b64_data)
    return Image(data=png_bytes, format="png")


@mcp.tool()
def search_parts(query: str, limit: int = 25) -> dict[str, Any]:
    """Search the Fritzing parts library by keyword, title, family, or property."""
    if _bridge.is_online():
        try:
            return _bridge.call("parts.search", {"query": query, "limit": limit})
        except BridgeError:
            pass
    return _catalog.search_parts(query, limit)


@mcp.tool()
def get_part_info(module_id: str) -> dict[str, Any]:
    """Get detailed information, properties, and connector pins for a part moduleID."""
    if _bridge.is_online():
        try:
            return _bridge.call("parts.get_info", {"moduleID": module_id})
        except BridgeError:
            pass
    return _catalog.get_part_info(module_id)


@mcp.tool()
def add_part(
    module_id: str,
    x: float = 0.0,
    y: float = 0.0,
    view: str = "",
) -> dict[str, Any]:
    """Place a part by moduleID at scene coordinates (x, y) and return its ID, title, and pins."""
    params: dict[str, Any] = {"moduleID": module_id, "x": x, "y": y}
    if view.strip():
        params["view"] = view.strip().lower()
    return _bridge.call("part.add", params)


@mcp.tool()
def move_part(
    part: str,
    x: float,
    y: float,
    relative: bool = False,
) -> dict[str, Any]:
    """Move a part (by instanceTitle like 'R1' or numeric ID) to (x, y) or by (dx, dy)."""
    if relative:
        params: dict[str, Any] = {"part": part, "relative": True, "dx": x, "dy": y}
    else:
        params = {"part": part, "relative": False, "x": x, "y": y}
    return _bridge.call("part.move", params)


@mcp.tool()
def rotate_part(part: str, degrees: float = 90.0) -> dict[str, Any]:
    """Rotate a part by a multiple of 45 degrees (e.g. 90, -90, 180, 45)."""
    return _bridge.call("part.rotate", {"part": part, "degrees": degrees})


@mcp.tool()
def delete_part(part: str) -> dict[str, Any]:
    """Delete a part and clean up its attached wires from the sketch."""
    return _bridge.call("part.delete", {"part": part})


@mcp.tool()
def set_part_property(part: str, property_name: str, value: str) -> dict[str, Any]:
    """Change a property (e.g. 'resistance', 'capacitance', 'title') on a placed part."""
    return _bridge.call(
        "part.set_property",
        {"part": part, "property": property_name, "value": value},
    )


@mcp.tool()
def connect_pins(
    from_part: str,
    from_pin: str,
    to_part: str,
    to_pin: str,
    color: str = "",
) -> dict[str, Any]:
    """Connect two component pins with a wire/trace in the active view."""
    params: dict[str, Any] = {
        "fromPart": from_part,
        "fromPin": from_pin,
        "toPart": to_part,
        "toPin": to_pin,
    }
    if color.strip():
        params["color"] = color.strip()
    return _bridge.call("wire.connect", params)


@mcp.tool()
def delete_wire(wire_id: int) -> dict[str, Any]:
    """Delete a wire by its numeric wireId."""
    return _bridge.call("wire.delete", {"wireId": wire_id})


@mcp.tool()
def get_netlist() -> dict[str, Any]:
    """Retrieve all electrical nets (connected component pins) in the sketch."""
    return _bridge.call("wire.get_netlist")


@mcp.tool()
def open_sketch(file_path: str) -> dict[str, Any]:
    """Open an existing Fritzing sketch (.fz or .fzz) from disk."""
    return _bridge.call("sketch.open", {"filePath": str(Path(file_path.strip()))})


@mcp.tool()
def save_sketch(file_path: str) -> dict[str, Any]:
    """Save the active Fritzing sketch to disk (.fzz archive or .fz file)."""
    return _bridge.call("sketch.save", {"filePath": str(Path(file_path.strip()))})


@mcp.tool()
def get_pcb_board_info() -> dict[str, Any]:
    """Retrieve all PCB boards in the sketch along with dimensions (mm) and layer count."""
    return _bridge.call("pcb.get_board_info")


@mcp.tool()
def resize_pcb_board(
    width_mm: float,
    height_mm: float,
    board: str = "",
) -> dict[str, Any]:
    """Resize a rectangular PCB board in millimeters (5.0 to 1000.0 mm)."""
    params: dict[str, Any] = {"widthMm": width_mm, "heightMm": height_mm}
    if board.strip():
        params["board"] = board.strip()
    return _bridge.call("pcb.resize_board", params)


@mcp.tool()
def set_pcb_layers(layers: int) -> dict[str, Any]:
    """Switch the PCB board between single-layer (1) and double-layer (2)."""
    return _bridge.call("pcb.set_layers", {"layers": layers})


@mcp.tool()
def run_design_rules_check() -> dict[str, Any]:
    """Run the PCB Design Rules Check (DRC) and return any overlapping/clearance collisions."""
    return _bridge.call("pcb.run_drc")


@mcp.tool()
def set_pcb_copper_fill(mode: str = "ground") -> dict[str, Any]:
    """Generate or remove PCB copper/ground pour ('ground', 'copper', or 'remove')."""
    return _bridge.call("pcb.copper_fill", {"mode": mode.strip().lower()})


@mcp.tool()
def get_spice_netlist() -> dict[str, Any]:
    """Generate the SPICE netlist for circuit simulation of the current sketch."""
    return _bridge.call("sim.get_spice_netlist")


@mcp.tool()
def control_simulation(action: str = "start") -> dict[str, Any]:
    """Control the built-in ngspice circuit simulator ('start', 'stop', or 'status')."""
    return _bridge.call("sim.control", {"action": action.strip().lower()})


@mcp.tool()
def export_bom_csv(output_path: str = "") -> dict[str, Any]:
    """Export the Bill of Materials (BOM) as structured JSON rows and optional CSV file."""
    params: dict[str, Any] = {}
    if output_path.strip():
        params["outputPath"] = str(Path(output_path.strip()))
    return _bridge.call("export.bom", params)


@mcp.tool()
def export_gerber(output_dir: str, prefix: str = "") -> dict[str, Any]:
    """Export RS-274X Gerber and Excellon drill files for PCB manufacturing."""
    params: dict[str, Any] = {"outputDir": str(Path(output_dir.strip()))}
    if prefix.strip():
        params["prefix"] = prefix.strip()
    return _bridge.call("export.gerber", params)


def main() -> None:  # pragma: no cover
    """Run the Fritzing MCP server over stdio."""
    mcp.run(transport="stdio")


if __name__ == "__main__":  # pragma: no cover
    main()
