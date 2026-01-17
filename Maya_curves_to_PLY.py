# -*- coding: utf-8 -*-
"""HairTool curve exporter for Maya (Python 2/3)

Select one or more NURBS curves, run this script, and it will export an ASCII
PLY in the exact format HairTool expects (with layers):

ply
format ascii 1.0
element vertex N
property float x
property float y
property float z
property uchar anchor
property int layer_id
property int curve_id
end_header
...

Notes:
- HairTool works in meters; Maya works in centimeters. We export cm -> m (divide by 100).
- Each selected curve becomes one HairTool guide (curve_id).
- If curves are organized in layer sub-groups, layer_id is exported too.
"""

from __future__ import print_function

try:
    xrange  # type: ignore[name-defined]
except NameError:
    xrange = range  # type: ignore[assignment]

import sys

import maya.cmds as cmds
import maya.api.OpenMaya as om
import time


try:
    text_type = unicode  # type: ignore[name-defined]
except NameError:
    text_type = str


def _to_ascii_bytes(s):
    """Return ASCII bytes for writing to a binary PLY file (Py2/Py3 safe)."""
    if isinstance(s, bytes):
        return s
    if isinstance(s, text_type):
        return s.encode('ascii')
    return text_type(s).encode('ascii')


def _iter_selected_nurbs_curve_shapes():
    """Yield MDagPath for nurbsCurve shapes from current selection."""
    sel = cmds.ls(sl=True, long=True) or []
    shapes = []
    for node in sel:
        if cmds.nodeType(node) == 'nurbsCurve':
            shapes.append(node)
        else:
            child_shapes = cmds.listRelatives(node, shapes=True, fullPath=True) or []
            for s in child_shapes:
                if cmds.nodeType(s) == 'nurbsCurve':
                    shapes.append(s)

    # Deduplicate, preserve order
    seen = set()
    ordered = []
    for s in shapes:
        if s in seen:
            continue
        seen.add(s)
        ordered.append(s)

    for s in ordered:
        sl = om.MSelectionList()
        sl.add(s)
        yield sl.getDagPath(0)


def _collect_layer_groups_from_selection():
    """Return list of (layer_name, [curve_shapes]) if selection contains layer sub-groups."""
    sel = cmds.ls(sl=True, long=True) or []
    layer_groups = []
    seen_group = set()

    def _curves_under(node):
        shapes = cmds.listRelatives(node, ad=True, type='nurbsCurve', fullPath=True) or []
        return shapes

    for node in sel:
        if cmds.nodeType(node) == 'nurbsCurve':
            continue
        if node in seen_group:
            continue
        seen_group.add(node)
        child_groups = cmds.listRelatives(node, children=True, type='transform', fullPath=True) or []
        for cg in child_groups:
            # Treat only group nodes (no direct nurbsCurve shape) as layer containers.
            direct_shapes = cmds.listRelatives(cg, shapes=True, type='nurbsCurve', fullPath=True) or []
            if direct_shapes:
                continue
            curves = _curves_under(cg)
            if curves:
                layer_groups.append((cg, curves))

    # Deduplicate layer groups by name
    uniq = []
    seen = set()
    for name, curves in layer_groups:
        if name in seen:
            continue
        seen.add(name)
        uniq.append((name, curves))
    return uniq


def _layer_color(idx):
    # Simple HSV->RGB with golden-ratio hues for distinct colors
    h = (idx * 0.61803398875) % 1.0
    s = 0.65
    v = 0.95
    i = int(h * 6.0)
    f = h * 6.0 - i
    p = v * (1.0 - s)
    q = v * (1.0 - s * f)
    t = v * (1.0 - s * (1.0 - f))
    i = i % 6
    if i == 0:
        return (v, t, p)
    if i == 1:
        return (q, v, p)
    if i == 2:
        return (p, v, t)
    if i == 3:
        return (p, q, v)
    if i == 4:
        return (t, p, v)
    return (v, p, q)


def _curve_world_points(curve_dag, mode='cvs', samples_per_span=4, min_samples=2):
    """Return list of (x,y,z) world positions for exporting.

    mode:
      - 'cvs' (default): export the curve CVs (control vertices). This preserves
        the exact point count you see in Maya.
      - 'sampled': uniformly sample along the curve.
    """
    fn = om.MFnNurbsCurve(curve_dag)

    if mode == 'cvs':
        cvs = fn.cvPositions(om.MSpace.kWorld)
        pts = [(p.x, p.y, p.z) for p in cvs]
        return pts

    # Fallback: uniform sampling
    spans = fn.numSpans

    sample_count = max(int(spans) * int(samples_per_span) + 1, int(min_samples))
    if sample_count < 2:
        sample_count = 2

    pmin, pmax = fn.knotDomain
    if pmax <= pmin:
        pmin = 0.0
        pmax = 1.0

    pts = []
    for i in xrange(sample_count):
        t = float(i) / float(sample_count - 1)
        param = pmin + (pmax - pmin) * t
        p = fn.getPointAtParam(param, om.MSpace.kWorld)
        pts.append((p.x, p.y, p.z))

    return pts


def export_selected_curves_to_ply(mode='cvs', samples_per_span=4, min_samples=2):
    file_filter = 'PLY Files (*.ply);;All Files (*.*)'
    result = cmds.fileDialog2(fileFilter=file_filter, dialogStyle=2, fileMode=0)
    if not result:
        return
    path = result[0]
    if not path.lower().endswith('.ply'):
        path = path + '.ply'

    start_time = time.time()

    layer_groups = _collect_layer_groups_from_selection()
    has_layer_info = bool(layer_groups)
    if layer_groups:
        curves = []
        layer_map = []
        for idx, (layer_node, curve_shapes) in enumerate(layer_groups):
            layer_name = cmds.ls(layer_node, shortNames=True) or [layer_node]
            layer_name = layer_name[0]
            layer_map.append({'id': idx, 'name': layer_name})
            for s in curve_shapes:
                sl = om.MSelectionList()
                sl.add(s)
                curves.append((idx, sl.getDagPath(0)))
    else:
        curves = [dag for dag in _iter_selected_nurbs_curve_shapes()]
        layer_map = []

    if not curves:
        cmds.warning('Select at least one NURBS curve (shape or transform).')
        return

    # Sample all curves first to know vertex count
    sampled = []
    if has_layer_info:
        for layer_id, dag in curves:
            pts_cm = _curve_world_points(dag, mode=mode, samples_per_span=samples_per_span, min_samples=min_samples)
            if len(pts_cm) < 2:
                continue
            sampled.append((layer_id, pts_cm))
    else:
        for dag in curves:
            pts_cm = _curve_world_points(dag, mode=mode, samples_per_span=samples_per_span, min_samples=min_samples)
            if len(pts_cm) < 2:
                continue
            sampled.append(pts_cm)

    if not sampled:
        cmds.warning('No valid curves found to export.')
        return

    # Maya is centimeters; HairTool is meters
    scale = 0.01

    vertex_count = sum(len(pts) for pts in sampled) if not has_layer_info else sum(len(pts) for _, pts in sampled)

    # Write as ASCII bytes to avoid Py2/Py3 text/bytes differences.
    with open(path, 'wb') as f:
        f.write(_to_ascii_bytes('ply\n'))
        f.write(_to_ascii_bytes('format ascii 1.0\n'))
        # Layer metadata for round-trip
        if has_layer_info:
            for lm in layer_map:
                col = _layer_color(lm['id'])
                f.write(_to_ascii_bytes('comment layer {0} "{1}" {2} {3} {4} 1\n'.format(
                    lm['id'], lm['name'], col[0], col[1], col[2]
                )))
        f.write(_to_ascii_bytes('element vertex {0}\n'.format(vertex_count)))
        f.write(_to_ascii_bytes('property float x\n'))
        f.write(_to_ascii_bytes('property float y\n'))
        f.write(_to_ascii_bytes('property float z\n'))
        f.write(_to_ascii_bytes('property uchar anchor\n'))
        if has_layer_info:
            f.write(_to_ascii_bytes('property int layer_id\n'))
        f.write(_to_ascii_bytes('property int curve_id\n'))
        f.write(_to_ascii_bytes('end_header\n'))

        cid = 0
        if has_layer_info:
            for layer_id, pts_cm in sampled:
                for i, p in enumerate(pts_cm):
                    x = float(p[0]) * scale
                    y = float(p[1]) * scale
                    z = float(p[2]) * scale
                    anchor = 1 if i == 0 else 0
                    f.write(_to_ascii_bytes('{0} {1} {2} {3} {4} {5}\n'.format(x, y, z, anchor, layer_id, cid)))
                cid += 1
        else:
            for pts_cm in sampled:
                for i, p in enumerate(pts_cm):
                    x = float(p[0]) * scale
                    y = float(p[1]) * scale
                    z = float(p[2]) * scale
                    anchor = 1 if i == 0 else 0
                    f.write(_to_ascii_bytes('{0} {1} {2} {3} {4}\n'.format(x, y, z, anchor, cid)))
                cid += 1

    print('✅ Exported {0} curves ({1} vertices) to: {2} ({3:.2f}s)'.format(len(sampled), vertex_count, path, time.time() - start_time))


if __name__ == '__main__':
    export_selected_curves_to_ply()
