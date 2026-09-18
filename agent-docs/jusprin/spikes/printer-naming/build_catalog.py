#!/usr/bin/env python3
"""Dump the packaged printer catalogue as the flat list the spike puts in the prompt.

Mirrors PrinterCatalog::load: one entry per machine model, build volume and the
nozzle sizes taken from the machine presets that name that model.
"""
import json, os, sys, glob, collections

ROOT = sys.argv[1] if len(sys.argv) > 1 else "resources/profiles"

def load(path):
    try:
        with open(path) as fh: return json.load(fh)
    except Exception: return None

def volume(machine):
    area = machine.get("printable_area") or []
    pts = []
    for p in area:
        if not isinstance(p, str): continue
        bits = p.split("x")
        if len(bits) != 2: continue
        try: pts.append((float(bits[0]), float(bits[1])))
        except ValueError: pass
    if not pts: return ""
    h = machine.get("printable_height")
    if isinstance(h, list): h = h[0] if h else None
    if not h: return ""
    xs = [p[0] for p in pts]; ys = [p[1] for p in pts]
    def clean(v):
        v = float(v)
        return str(int(v)) if v == int(v) else f"{v:.1f}"
    return f"{clean(max(xs)-min(xs))}x{clean(max(ys)-min(ys))}x{clean(h)}"

entries = {}
for vendor_file in sorted(glob.glob(os.path.join(ROOT, "*.json"))):
    index = load(vendor_file)
    if not index or "machine_model_list" not in index: continue
    vendor_dir = os.path.splitext(vendor_file)[0]
    vendor_name = index.get("name", os.path.basename(vendor_dir))
    models = {}
    for m in index.get("machine_model_list") or []:
        rec = load(os.path.join(vendor_dir, m.get("sub_path", "")))
        if not rec: continue
        models[rec.get("name", m["name"])] = rec
    # Presets inherit most of their settings, the bed among them, from a
    # parent named in "inherits"; resolve the chain the way PrinterCatalog does.
    raw = {}
    for m in index.get("machine_list") or []:
        preset = load(os.path.join(vendor_dir, m.get("sub_path", "")))
        if preset: raw[m.get("name", preset.get("name"))] = preset
    def resolve(name, seen=()):
        preset = raw.get(name)
        if preset is None or name in seen: return {}
        merged = dict(resolve(preset.get("inherits", ""), seen + (name,)))
        merged.update(preset)
        return merged
    for m in index.get("machine_list") or []:
        machine = resolve(m.get("name", ""))
        if not machine: continue
        model_name = machine.get("printer_model")
        if model_name not in models: continue
        rec = models[model_name]
        key = f"{vendor_name}/{model_name}"
        e = entries.setdefault(key, {
            "catalogId": key,
            "vendor": vendor_name,
            "model": model_name,
            "buildVolume": "",
            "nozzles": set(),
            "material": (rec.get("default_materials") or "").split(";")[0],
        })
        if not e["buildVolume"]:
            e["buildVolume"] = volume(machine)
        for n in (machine.get("nozzle_diameter") or []):
            try: e["nozzles"].add(float(n))
            except (TypeError, ValueError): pass

out = []
for e in entries.values():
    e["nozzles"] = sorted(e.pop("nozzles"))
    out.append(e)
out.sort(key=lambda e: (e["vendor"], e["model"]))
json.dump(out, sys.stdout, indent=1)
