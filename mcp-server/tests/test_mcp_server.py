"""Comprehensive unit and edge-case tests for fritzing_mcp."""

from __future__ import annotations

import base64
from pathlib import Path
import sqlite3
from unittest.mock import MagicMock, patch
import zipfile

import httpx
import pytest

from fritzing_mcp.catalog import PartsCatalog
from fritzing_mcp.client import BridgeError, FritzingBridgeClient
from fritzing_mcp.part_generator import build_fzpz_archive, validate_custom_part_spec
from fritzing_mcp import server as mcp_server


# ---------------------------------------------------------------------------
# Fixtures
# ---------------------------------------------------------------------------


@pytest.fixture()
def sample_parts_db(tmp_path: Path) -> Path:
    """Create a realistic temporary SQLite parts.db for testing."""
    db_file = tmp_path / "parts.db"
    conn = sqlite3.connect(db_file)
    conn.executescript(
        """
        CREATE TABLE parts (
            id INTEGER PRIMARY KEY,
            moduleID TEXT,
            title TEXT,
            family TEXT,
            description TEXT,
            replacedby TEXT
        );
        CREATE TABLE properties (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT,
            value TEXT,
            part_id INTEGER
        );
        CREATE TABLE connectors (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            connectorid TEXT,
            name TEXT,
            description TEXT,
            type TEXT,
            part_id INTEGER
        );
        CREATE TABLE tags (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            tag TEXT,
            part_id INTEGER
        );
        """
    )
    conn.execute(
        "INSERT INTO parts VALUES "
        "(1, 'resistor_220', '220 Ohm Resistor', 'Resistor', 'Standard 220Ω resistor', '')"
    )
    conn.execute(
        "INSERT INTO parts VALUES "
        "(2, 'led_red_5mm', 'Red LED 5mm', 'LED', 'Standard red indicator LED', NULL)"
    )
    conn.execute(
        "INSERT INTO parts VALUES (3, 'old_part', 'Obsolete Part', 'Legacy', 'Old', 'resistor_220')"
    )
    conn.execute("INSERT INTO properties (name, value, part_id) VALUES ('resistance', '220Ω', 1)")
    conn.execute("INSERT INTO properties (name, value, part_id) VALUES ('color', 'red', 2)")
    conn.execute("INSERT INTO tags (tag, part_id) VALUES ('passive', 1)")
    conn.execute(
        "INSERT INTO connectors (connectorid, name, description, type, part_id) "
        "VALUES ('connector0', 'Pin 0', 'Anode', '1', 1)"
    )
    conn.execute(
        "INSERT INTO connectors (connectorid, name, description, type, part_id) "
        "VALUES ('connector1', 'Pin 1', 'Cathode', '1', 1)"
    )
    conn.commit()
    conn.close()
    return db_file


# ---------------------------------------------------------------------------
# 1. FritzingBridgeClient Unit & Edge-Case Tests
# ---------------------------------------------------------------------------


def test_client_init_validation() -> None:
    client = FritzingBridgeClient("http://127.0.0.1:8765/", timeout=5.0)
    assert client.base_url == "http://127.0.0.1:8765"
    assert client.timeout == 5.0

    with pytest.raises(ValueError, match="base_url"):
        FritzingBridgeClient("")
    with pytest.raises(ValueError, match="base_url"):
        FritzingBridgeClient("ftp://localhost:8765")
    with pytest.raises(ValueError, match="timeout"):
        FritzingBridgeClient("http://localhost:8765", timeout=0)
    with pytest.raises(ValueError, match="timeout"):
        FritzingBridgeClient("http://localhost:8765", timeout=float("nan"))
    with pytest.raises(ValueError, match="timeout"):
        FritzingBridgeClient("http://localhost:8765", timeout=float("inf"))


def test_client_health_and_is_online() -> None:
    client = FritzingBridgeClient("http://127.0.0.1:8765")
    mock_resp = httpx.Response(200, json={"ok": True, "methods": ["sketch.undo"]})
    with patch("httpx.Client.get", return_value=mock_resp):
        res = client.health()
        assert res["ok"] is True
        assert client.is_online() is True

    with patch("httpx.Client.get", side_effect=httpx.ConnectError("refused")):
        assert client.is_online() is False
        with pytest.raises(BridgeError) as exc_info:
            client.health()
        assert exc_info.value.code == "CONNECTION_ERROR"


def test_client_call_validation_and_errors() -> None:
    client = FritzingBridgeClient("http://127.0.0.1:8765")
    with pytest.raises(ValueError, match="method"):
        client.call("   ")
    with pytest.raises(ValueError, match="params"):
        client.call("sketch.undo", params=["not-a-dict"])  # type: ignore[arg-type]

    with patch("httpx.Client.post", side_effect=httpx.TimeoutException("timed out")):
        with pytest.raises(BridgeError) as exc_info:
            client.call("sketch.undo")
        assert exc_info.value.code == "CONNECTION_ERROR"


def test_client_decode_response_edge_cases() -> None:
    client = FritzingBridgeClient("http://127.0.0.1:8765")

    # Non-JSON response
    bad_json_resp = httpx.Response(502, text="<html>Bad Gateway</html>")
    with patch("httpx.Client.post", return_value=bad_json_resp):
        with pytest.raises(BridgeError) as exc_info:
            client.call("sketch.undo")
        assert exc_info.value.code == "INVALID_JSON"

    # JSON array instead of object
    array_resp = httpx.Response(200, json=[1, 2, 3])
    with patch("httpx.Client.post", return_value=array_resp):
        with pytest.raises(BridgeError) as exc_info:
            client.call("sketch.undo")
        assert exc_info.value.code == "INVALID_JSON"

    # Application-level error (ok=False)
    err_resp = httpx.Response(
        200, json={"ok": False, "error": "Part not found", "code": "NOT_FOUND"}
    )
    with patch("httpx.Client.post", return_value=err_resp):
        with pytest.raises(BridgeError) as exc_info:
            client.call("part.delete", {"part": "R99"})
        assert exc_info.value.code == "NOT_FOUND"
        assert "Part not found" in str(exc_info.value)

    # HTTP 400 without error message field
    http_err_resp = httpx.Response(400, json={"ok": False})
    with patch("httpx.Client.post", return_value=http_err_resp):
        with pytest.raises(BridgeError) as exc_info:
            client.call("part.delete")
        assert exc_info.value.code == "HTTP_400"


# ---------------------------------------------------------------------------
# 2. PartsCatalog SQLite Unit & Edge-Case Tests
# ---------------------------------------------------------------------------


def test_catalog_missing_db(tmp_path: Path) -> None:
    catalog = PartsCatalog(tmp_path / "nonexistent.db")
    with pytest.raises(FileNotFoundError):
        catalog.search_parts("resistor")


def test_catalog_search_and_validation(sample_parts_db: Path) -> None:
    catalog = PartsCatalog(sample_parts_db)

    with pytest.raises(ValueError, match="query"):
        catalog.search_parts("   ")
    with pytest.raises(ValueError, match="limit"):
        catalog.search_parts("resistor", limit=0)
    with pytest.raises(ValueError, match="limit"):
        catalog.search_parts("resistor", limit=501)
    with pytest.raises(ValueError, match="limit"):
        catalog.search_parts("resistor", limit=True)  # type: ignore[arg-type]

    # Search by title / Unicode property value
    res = catalog.search_parts("220Ω", limit=10)
    assert res["ok"] is True
    assert res["totalFound"] == 1
    assert res["results"][0]["moduleID"] == "resistor_220"
    assert res["results"][0]["properties"]["resistance"] == "220Ω"

    # Obsolete parts (with replacedby set) must be excluded
    obs = catalog.search_parts("Obsolete")
    assert obs["totalFound"] == 0

    # SQL injection attempt must be safely parameterized and return 0 matches
    sqli = catalog.search_parts("' OR 1=1 --")
    assert sqli["totalFound"] == 0


def test_catalog_get_part_info(sample_parts_db: Path) -> None:
    catalog = PartsCatalog(sample_parts_db)

    with pytest.raises(ValueError, match="module_id"):
        catalog.get_part_info("")
    with pytest.raises(LookupError, match="not found"):
        catalog.get_part_info("unknown_mod_id")

    info = catalog.get_part_info("resistor_220")
    assert info["ok"] is True
    assert info["moduleID"] == "resistor_220"
    assert len(info["connectors"]) == 2
    assert info["connectors"][0]["id"] == "connector0"


# ---------------------------------------------------------------------------
# 3. MCP Server Tools Unit & Edge-Case Tests
# ---------------------------------------------------------------------------


def test_server_tools_with_mock_bridge(sample_parts_db: Path, tmp_path: Path) -> None:
    mock_bridge = MagicMock(spec=FritzingBridgeClient)
    mock_bridge.base_url = "http://127.0.0.1:8765"
    mock_bridge.is_online.return_value = True
    mock_bridge.health.return_value = {"ok": True, "methods": ["sketch.get_state", "part.add"]}

    catalog = PartsCatalog(sample_parts_db)
    mcp_server.configure_dependencies(bridge=mock_bridge, catalog=catalog)

    status = mcp_server.check_connection()
    assert status["fritzingOnline"] is True
    assert status["offlinePartsDbAvailable"] is True
    assert "part.add" in status["supportedMethods"]

    # Offline check_connection branch
    mock_bridge.is_online.return_value = False
    status_off = mcp_server.check_connection()
    assert status_off["fritzingOnline"] is False
    assert status_off["supportedMethods"] == []

    # Sketch & View tools
    mock_bridge.is_online.return_value = True
    mock_bridge.call.return_value = {"ok": True, "view": "breadboard", "parts": [], "wires": []}
    assert mcp_server.get_sketch_state("")["ok"] is True
    mock_bridge.call.assert_called_with("sketch.get_state", {})

    mcp_server.get_sketch_state(" Schematic ")
    mock_bridge.call.assert_called_with("sketch.get_state", {"view": "schematic"})

    mcp_server.set_active_view("pcb")
    mock_bridge.call.assert_called_with("sketch.set_view", {"view": "pcb"})

    mcp_server.undo_action()
    mock_bridge.call.assert_called_with("sketch.undo")

    mcp_server.redo_action()
    mock_bridge.call.assert_called_with("sketch.redo")

    # Screenshot tool
    fake_png = b"\x89PNG\r\n\x1a\nfakedata"
    mock_bridge.call.return_value = {
        "ok": True,
        "imageBase64": base64.b64encode(fake_png).decode("ascii"),
    }
    out_img_path = str(tmp_path / "shot.png")
    img = mcp_server.capture_view_screenshot(
        view="breadboard", width=800, height=600, crop_to_items=False, output_path=out_img_path
    )
    assert img.data == fake_png

    img_default = mcp_server.capture_view_screenshot()
    assert img_default.data == fake_png


def test_server_part_and_wire_tools(sample_parts_db: Path) -> None:
    mock_bridge = MagicMock(spec=FritzingBridgeClient)
    catalog = PartsCatalog(sample_parts_db)
    mcp_server.configure_dependencies(bridge=mock_bridge, catalog=catalog)

    # Online search_parts & get_part_info
    mock_bridge.is_online.return_value = True
    mock_bridge.call.return_value = {"ok": True, "results": [{"moduleID": "online_part"}]}
    assert mcp_server.search_parts("led", 5)["results"][0]["moduleID"] == "online_part"
    assert mcp_server.get_part_info("online_part")["ok"] is True

    # Fallback to offline SQLite catalog when BridgeError occurs or Fritzing is offline
    mock_bridge.call.side_effect = BridgeError("offline")
    fallback_search = mcp_server.search_parts("220", 5)
    assert fallback_search["results"][0]["moduleID"] == "resistor_220"

    fallback_info = mcp_server.get_part_info("resistor_220")
    assert fallback_info["moduleID"] == "resistor_220"

    mock_bridge.is_online.return_value = False
    assert mcp_server.search_parts("Red LED", 5)["totalFound"] == 1
    assert mcp_server.get_part_info("led_red_5mm")["family"] == "LED"

    # Part manipulation tools
    mock_bridge.call.side_effect = None
    mock_bridge.call.return_value = {"ok": True, "id": 100, "title": "R1"}

    mcp_server.add_part("resistor_220", x=50.0, y=60.0, view="breadboard")
    mock_bridge.call.assert_called_with(
        "part.add", {"moduleID": "resistor_220", "x": 50.0, "y": 60.0, "view": "breadboard"}
    )

    mcp_server.add_part("resistor_220")
    mock_bridge.call.assert_called_with(
        "part.add", {"moduleID": "resistor_220", "x": 0.0, "y": 0.0}
    )

    mcp_server.move_part("R1", 100.0, 200.0, relative=False)
    mock_bridge.call.assert_called_with(
        "part.move", {"part": "R1", "relative": False, "x": 100.0, "y": 200.0}
    )

    mcp_server.move_part("R1", 10.0, -10.0, relative=True)
    mock_bridge.call.assert_called_with(
        "part.move", {"part": "R1", "relative": True, "dx": 10.0, "dy": -10.0}
    )

    mcp_server.rotate_part("R1", 90.0)
    mock_bridge.call.assert_called_with("part.rotate", {"part": "R1", "degrees": 90.0})

    mcp_server.delete_part("R1")
    mock_bridge.call.assert_called_with("part.delete", {"part": "R1"})

    mcp_server.set_part_property("R1", "resistance", "4.7kΩ")
    mock_bridge.call.assert_called_with(
        "part.set_property", {"part": "R1", "property": "resistance", "value": "4.7kΩ"}
    )

    # Wiring tools
    mcp_server.connect_pins("R1", "connector0", "LED1", "connector1", color="blue")
    mock_bridge.call.assert_called_with(
        "wire.connect",
        {
            "fromPart": "R1",
            "fromPin": "connector0",
            "toPart": "LED1",
            "toPin": "connector1",
            "color": "blue",
        },
    )

    mcp_server.connect_pins("R1", "connector0", "LED1", "connector1")
    mock_bridge.call.assert_called_with(
        "wire.connect",
        {
            "fromPart": "R1",
            "fromPin": "connector0",
            "toPart": "LED1",
            "toPin": "connector1",
        },
    )

    mcp_server.delete_wire(42)
    mock_bridge.call.assert_called_with("wire.delete", {"wireId": 42})

    mcp_server.get_netlist()
    mock_bridge.call.assert_called_with("wire.get_netlist")


def test_server_stage2_pcb_sim_and_export_tools(sample_parts_db: Path, tmp_path: Path) -> None:
    mock_bridge = MagicMock(spec=FritzingBridgeClient)
    mock_bridge.call.return_value = {"ok": True}
    catalog = PartsCatalog(sample_parts_db)
    mcp_server.configure_dependencies(bridge=mock_bridge, catalog=catalog)

    fzz_path = str(tmp_path / "demo.fzz")
    mcp_server.open_sketch(fzz_path)
    mock_bridge.call.assert_called_with("sketch.open", {"filePath": fzz_path})

    mcp_server.save_sketch(fzz_path)
    mock_bridge.call.assert_called_with("sketch.save", {"filePath": fzz_path})

    mcp_server.get_pcb_board_info()
    mock_bridge.call.assert_called_with("pcb.get_board_info")

    mcp_server.resize_pcb_board(80.0, 50.0)
    mock_bridge.call.assert_called_with(
        "pcb.resize_board", {"widthMm": 80.0, "heightMm": 50.0}
    )

    mcp_server.resize_pcb_board(100.0, 60.0, board="PCB1")
    mock_bridge.call.assert_called_with(
        "pcb.resize_board", {"widthMm": 100.0, "heightMm": 60.0, "board": "PCB1"}
    )

    mcp_server.set_pcb_layers(2)
    mock_bridge.call.assert_called_with("pcb.set_layers", {"layers": 2})

    mcp_server.run_design_rules_check()
    mock_bridge.call.assert_called_with("pcb.run_drc")

    mcp_server.set_pcb_copper_fill("ground")
    mock_bridge.call.assert_called_with("pcb.copper_fill", {"mode": "ground"})

    mcp_server.get_spice_netlist()
    mock_bridge.call.assert_called_with("sim.get_spice_netlist")

    mcp_server.control_simulation("status")
    mock_bridge.call.assert_called_with("sim.control", {"action": "status"})

    mcp_server.export_bom_csv()
    mock_bridge.call.assert_called_with("export.bom", {})

    bom_path = str(tmp_path / "bom.csv")
    mcp_server.export_bom_csv(bom_path)
    mock_bridge.call.assert_called_with("export.bom", {"outputPath": bom_path})

    gerber_dir = str(tmp_path / "gerber")
    mcp_server.export_gerber(gerber_dir)
    mock_bridge.call.assert_called_with("export.gerber", {"outputDir": gerber_dir})

    mcp_server.export_gerber(gerber_dir, prefix="rev1")
    mock_bridge.call.assert_called_with(
        "export.gerber", {"outputDir": gerber_dir, "prefix": "rev1"}
    )


def test_custom_part_generator_and_edge_cases(tmp_path: Path) -> None:
    with pytest.raises(ValueError, match="title"):
        validate_custom_part_spec("   ", ["VCC", "GND"])
    with pytest.raises(ValueError, match="package"):
        validate_custom_part_spec("MyIC", ["VCC", "GND"], package="bga")
    with pytest.raises(ValueError, match="between 2 and 64"):
        validate_custom_part_spec("MyIC", ["ONLY_ONE"])
    with pytest.raises(ValueError, match="even number"):
        validate_custom_part_spec("MyIC", ["P1", "P2", "P3"], package="dip")

    dip_spec = validate_custom_part_spec(
        "NE555 Timer & <Sensor>",
        ["GND", "TRIG", "OUT", "RESET", "CTRL", "THR", "DIS", "VCC"],
        package="dip",
    )
    dip_out = tmp_path / "ne555.fzpz"
    res = build_fzpz_archive(dip_spec, dip_out)
    assert res["ok"] is True
    assert res["pinCount"] == 8
    assert dip_out.is_file()

    with zipfile.ZipFile(dip_out, "r") as zf:
        names = set(zf.namelist())
        assert f"part.{dip_spec.base_name}.fzp" in names
        assert f"svg.pcb.{dip_spec.base_name}.svg" in names
        fzp_text = zf.read(f"part.{dip_spec.base_name}.fzp").decode("utf-8")
        assert "&amp; &lt;Sensor&gt;" in fzp_text

    hdr_spec = validate_custom_part_spec("3-Pin Sensor", ["VCC", "GND", "SIG"], package="header")
    hdr_out = tmp_path / "hdr3.fzpz"
    assert build_fzpz_archive(hdr_spec, hdr_out)["package"] == "header"


def test_server_stage3_tools(sample_parts_db: Path, tmp_path: Path) -> None:
    mock_bridge = MagicMock(spec=FritzingBridgeClient)
    mock_bridge.is_online.return_value = True
    mock_bridge.call.return_value = {"ok": True, "moduleID": "custom_ic"}
    catalog = PartsCatalog(sample_parts_db)
    mcp_server.configure_dependencies(bridge=mock_bridge, catalog=catalog)

    mcp_server.autoroute_pcb(24)
    mock_bridge.call.assert_called_with("pcb.autoroute", {"maxCycles": 24})

    mcp_server.clear_pcb_traces()
    mock_bridge.call.assert_called_with("pcb.clear_traces")

    mcp_server.set_pcb_trace_style(10, 32.0, "top")
    mock_bridge.call.assert_called_with(
        "pcb.set_trace_style", {"wireId": 10, "widthMils": 32.0, "layer": "top"}
    )

    mcp_server.set_pcb_trace_style(10, 16.0)
    mock_bridge.call.assert_called_with(
        "pcb.set_trace_style", {"wireId": 10, "widthMils": 16.0}
    )

    custom_fzpz = str(tmp_path / "gen.fzpz")
    attiny_pins = ["PB5", "PB3", "PB4", "GND", "PB0", "PB1", "PB2", "VCC"]
    gen_res = mcp_server.generate_custom_part("ATtiny85", attiny_pins, output_path=custom_fzpz)
    assert gen_res["ok"] is True
    assert "importedPart" in gen_res

    mock_bridge.is_online.return_value = False
    gen_default = mcp_server.generate_custom_part("Header2", ["A", "B"], package="header")
    assert gen_default["ok"] is True

    mcp_server.clear_sketch()
    mock_bridge.call.assert_called_with("sketch.clear")

    mcp_server.add_sketch_note("Test Note", 10.0, 20.0, view="schematic")
    mock_bridge.call.assert_called_with(
        "sketch.add_note", {"text": "Test Note", "x": 10.0, "y": 20.0, "view": "schematic"}
    )

    mcp_server.add_sketch_note("Default View Note")
    mock_bridge.call.assert_called_with(
        "sketch.add_note", {"text": "Default View Note", "x": 0.0, "y": 0.0}
    )

    mcp_server.add_power_symbol("vcc", "3.3V", 50.0, 60.0)
    mock_bridge.call.assert_called_with(
        "sketch.add_power_symbol",
        {"symbolType": "vcc", "voltage": "3.3V", "x": 50.0, "y": 60.0},
    )

    svg_out = str(tmp_path / "out.svg")
    mcp_server.export_view_svg(svg_out, view="pcb")
    mock_bridge.call.assert_called_with("export.svg", {"outputPath": svg_out, "view": "pcb"})

    mcp_server.export_view_svg(svg_out)
    mock_bridge.call.assert_called_with("export.svg", {"outputPath": svg_out})

    pdf_out = str(tmp_path / "out.pdf")
    mcp_server.export_view_pdf(pdf_out, view="schematic", remove_background=False)
    mock_bridge.call.assert_called_with(
        "export.pdf",
        {"outputPath": pdf_out, "removeBackground": False, "view": "schematic"},
    )

    mcp_server.export_view_pdf(pdf_out)
    mock_bridge.call.assert_called_with(
        "export.pdf", {"outputPath": pdf_out, "removeBackground": True}
    )
