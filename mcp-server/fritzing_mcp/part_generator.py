"""Deterministic .fzpz Custom Part Generator for Fritzing."""

from __future__ import annotations

from dataclasses import dataclass
from html import escape
from pathlib import Path
import re
from typing import Any
import zipfile


@dataclass(frozen=True)
class CustomPartSpec:
    """Validated specification for generating a custom Fritzing .fzpz part."""

    title: str
    module_id: str
    base_name: str
    package: str
    pins: list[str]
    label: str = "U"


def _sanitize_slug(raw: str) -> str:
    slug = re.sub(r"[^a-zA-Z0-9_]+", "_", raw.strip().lower()).strip("_")
    return slug or "custom_part"


def _normalize_pins(pins: list[str], pkg: str) -> list[str]:
    clean_pins = [str(p).strip() for p in pins if str(p).strip()]
    if len(clean_pins) < 2 or len(clean_pins) > 64:
        raise ValueError("pins must contain between 2 and 64 non-empty pin names")
    if pkg == "dip" and len(clean_pins) % 2 != 0:
        raise ValueError("DIP package requires an even number of pins")
    return clean_pins


def validate_custom_part_spec(
    title: str,
    pins: list[str],
    package: str = "dip",
    module_id: str = "",
) -> CustomPartSpec:
    """Validate and normalize inputs for custom part generation."""
    clean_title = title.strip()
    if not clean_title:
        raise ValueError("title must be a non-empty string")
    pkg = package.strip().lower()
    if pkg not in ("dip", "header"):
        raise ValueError("package must be 'dip' or 'header'")
    clean_pins = _normalize_pins(pins, pkg)
    base = _sanitize_slug(module_id or clean_title)
    mod_id = module_id.strip() or f"{base}_custom_v1"
    return CustomPartSpec(
        title=clean_title,
        module_id=mod_id,
        base_name=f"{base}_v1",
        package=pkg,
        pins=clean_pins,
    )


def _build_connector_xml(idx: int, pin_name: str) -> str:
    esc_name = escape(pin_name, quote=True)
    cid = f"connector{idx}"
    tid = f"connector{idx}terminal"
    return (
        f'    <connector id="{cid}" type="male" name="{esc_name}">\n'
        f"      <description>Pin {idx + 1}: {esc_name}</description>\n"
        "      <views>\n"
        "        <breadboardView>\n"
        f'          <p layer="breadboard" svgId="{cid}pin" terminalId="{tid}"/>\n'
        "        </breadboardView>\n"
        "        <schematicView>\n"
        f'          <p layer="schematic" svgId="{cid}pin" terminalId="{tid}"/>\n'
        "        </schematicView>\n"
        "        <pcbView>\n"
        f'          <p layer="copper0" svgId="{cid}pin"/>\n'
        f'          <p layer="copper1" svgId="{cid}pin"/>\n'
        "        </pcbView>\n"
        "      </views>\n"
        "    </connector>"
    )


def generate_fzp_xml(spec: CustomPartSpec) -> str:
    """Generate the Fritzing .fzp XML definition for a CustomPartSpec."""
    esc_title = escape(spec.title, quote=True)
    esc_mod = escape(spec.module_id, quote=True)
    bname = spec.base_name
    connectors = "\n".join(_build_connector_xml(i, p) for i, p in enumerate(spec.pins))
    return (
        '<?xml version="1.0" encoding="UTF-8"?>\n'
        f'<module moduleId="{esc_mod}" fritzingVersion="1.0.4">\n'
        "  <version>1</version>\n"
        "  <author>Fritzing MCP Generator</author>\n"
        f"  <title>{esc_title}</title>\n"
        f"  <label>{escape(spec.label, quote=True)}</label>\n"
        "  <date>2026-01-01</date>\n"
        f"  <description>Custom {spec.package.upper()} part ({len(spec.pins)} pins)</description>\n"
        "  <properties>\n"
        '    <property name="family">Custom MCP IC</property>\n'
        f'    <property name="package">{spec.package.upper()}</property>\n'
        f'    <property name="pins">{len(spec.pins)}</property>\n'
        "  </properties>\n"
        "  <views>\n"
        f'    <iconView><layers image="icon/{bname}.svg">'
        '<layer layerId="icon"/></layers></iconView>\n'
        f'    <breadboardView><layers image="breadboard/{bname}.svg">'
        '<layer layerId="breadboard"/></layers></breadboardView>\n'
        f'    <schematicView><layers image="schematic/{bname}.svg">'
        '<layer layerId="schematic"/></layers></schematicView>\n'
        f'    <pcbView><layers image="pcb/{bname}.svg">'
        '<layer layerId="copper0"/><layer layerId="copper1"/>'
        '<layer layerId="silkscreen"/></layers></pcbView>\n'
        "  </views>\n"
        "  <connectors>\n"
        f"{connectors}\n"
        "  </connectors>\n"
        "</module>\n"
    )


def _pin_grid_coords(idx: int, total: int, package: str) -> tuple[float, float]:
    """Return (x_mils, y_mils) on a 100-mil grid for pin idx."""
    if package == "header":
        return (100.0 + idx * 100.0, 200.0)
    half = total // 2
    if idx < half:
        return (100.0 + idx * 100.0, 400.0)
    top_idx = total - 1 - idx
    return (100.0 + top_idx * 100.0, 100.0)


def generate_breadboard_svg(spec: CustomPartSpec) -> str:
    """Generate the breadboard/icon SVG with 100-mil pitch pins."""
    cols = len(spec.pins) if spec.package == "header" else len(spec.pins) // 2
    width = (cols + 1) * 100
    height = 300 if spec.package == "header" else 500
    elements: list[str] = [
        f'<rect x="50" y="130" width="{width - 100}" height="{height - 260}" '
        'fill="#2b2b2b" stroke="#141414" stroke-width="6"/>',
        f'<text x="{width // 2}" y="{height // 2 + 12}" fill="#ffffff" '
        'font-family="Droid Sans" font-size="38" text-anchor="middle">'
        f"{escape(spec.title)}</text>",
    ]
    for i, _ in enumerate(spec.pins):
        px, py = _pin_grid_coords(i, len(spec.pins), spec.package)
        elements.append(
            f'<rect id="connector{i}pin" x="{px - 15:.1f}" y="{py - 15:.1f}" '
            'width="30" height="30" fill="#8c8c8c"/>'
        )
        elements.append(
            f'<rect id="connector{i}terminal" x="{px - 5:.1f}" y="{py - 5:.1f}" '
            'width="10" height="10" fill="none"/>'
        )
    body = "\n    ".join(elements)
    return (
        '<?xml version="1.0" encoding="UTF-8"?>\n'
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width / 1000:.3f}in" '
        f'height="{height / 1000:.3f}in" viewBox="0 0 {width} {height}">\n'
        '  <g id="breadboard">\n'
        '    <g id="icon">\n'
        f"    {body}\n"
        "    </g>\n"
        "  </g>\n"
        "</svg>\n"
    )


def _schematic_pin_svg(i: int, name: str, rows: int, total: int, package: str) -> list[str]:
    is_left = package == "header" or i < rows
    row = i if is_left else (total - 1 - i)
    py = 150 + row * 100
    x1, x2, tx, term_x = (0, 100, 125, 0) if is_left else (500, 600, 475, 590)
    anchor = "start" if is_left else "end"
    return [
        f'<line id="connector{i}pin" x1="{x1}" y1="{py}" x2="{x2}" y2="{py}" '
        'stroke="#555555" stroke-width="10" stroke-linecap="round"/>',
        f'<rect id="connector{i}terminal" x="{term_x}" y="{py - 5}" '
        'width="10" height="10" fill="none"/>',
        f'<text x="{tx}" y="{py + 12}" font-family="Droid Sans" '
        f'font-size="30" text-anchor="{anchor}">{escape(name)}</text>',
    ]


def generate_schematic_svg(spec: CustomPartSpec) -> str:
    """Generate an IEC-style schematic box SVG with labeled pins."""
    total = len(spec.pins)
    rows = total if spec.package == "header" else total // 2
    height = (rows + 2) * 100
    elements: list[str] = [
        f'<rect x="100" y="50" width="400" height="{height - 100}" '
        'fill="#ffffff" stroke="#000000" stroke-width="10"/>',
        '<text x="300" y="40" font-family="Droid Sans" font-size="35" '
        f'text-anchor="middle">{escape(spec.title)}</text>',
    ]
    for i, name in enumerate(spec.pins):
        elements.extend(_schematic_pin_svg(i, name, rows, total, spec.package))
    body = "\n    ".join(elements)
    return (
        '<?xml version="1.0" encoding="UTF-8"?>\n'
        f'<svg xmlns="http://www.w3.org/2000/svg" width="0.600in" '
        f'height="{height / 1000:.3f}in" viewBox="0 0 600 {height}">\n'
        '  <g id="schematic">\n'
        f"    {body}\n"
        "  </g>\n"
        "</svg>\n"
    )


def generate_pcb_svg(spec: CustomPartSpec) -> str:
    """Generate the PCB footprint SVG with copper0/copper1 THT pads and silkscreen."""
    cols = len(spec.pins) if spec.package == "header" else len(spec.pins) // 2
    width = (cols + 1) * 100
    height = 300 if spec.package == "header" else 500
    pads: list[str] = []
    for i, _ in enumerate(spec.pins):
        px, py = _pin_grid_coords(i, len(spec.pins), spec.package)
        pads.append(
            f'<circle id="connector{i}pin" cx="{px:.1f}" cy="{py:.1f}" r="29" '
            'fill="none" stroke="#F7BD13" stroke-width="20"/>'
        )
    pad_block = "\n      ".join(pads)
    return (
        '<?xml version="1.0" encoding="UTF-8"?>\n'
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width / 1000:.3f}in" '
        f'height="{height / 1000:.3f}in" viewBox="0 0 {width} {height}">\n'
        '  <g id="silkscreen">\n'
        f'    <rect x="40" y="50" width="{width - 80}" height="{height - 100}" '
        'fill="none" stroke="#ffffff" stroke-width="8"/>\n'
        "  </g>\n"
        '  <g id="copper1">\n'
        '    <g id="copper0">\n'
        f"      {pad_block}\n"
        "    </g>\n"
        "  </g>\n"
        "</svg>\n"
    )


def build_fzpz_archive(spec: CustomPartSpec, output_path: Path) -> dict[str, Any]:
    """Write the .fzp and all 4 view SVGs into a bundled .fzpz archive."""
    output_path.parent.mkdir(parents=True, exist_ok=True)
    bname = spec.base_name
    bb_svg = generate_breadboard_svg(spec)
    with zipfile.ZipFile(output_path, "w", compression=zipfile.ZIP_DEFLATED) as zf:
        zf.writestr(f"part.{bname}.fzp", generate_fzp_xml(spec))
        zf.writestr(f"svg.icon.{bname}.svg", bb_svg)
        zf.writestr(f"svg.breadboard.{bname}.svg", bb_svg)
        zf.writestr(f"svg.schematic.{bname}.svg", generate_schematic_svg(spec))
        zf.writestr(f"svg.pcb.{bname}.svg", generate_pcb_svg(spec))

    return {
        "ok": True,
        "moduleID": spec.module_id,
        "title": spec.title,
        "package": spec.package,
        "pinCount": len(spec.pins),
        "fzpzPath": str(output_path.resolve()),
    }
