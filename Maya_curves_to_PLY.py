# -*- coding: utf-8 -*-
"""HairTool curve exporter for Maya (Python 2/3)

Select one or more NURBS curves, run this script, and it will export an ASCII
PLY in the exact format HairTool expects:

ply
format ascii 1.0
element vertex N
property float x
property float y
property float z
property uchar anchor
property int curve_id
end_header
...

Notes:
- HairTool works in meters; Maya works in centimeters. We export cm -> m (divide by 100).
- Each selected curve becomes one HairTool guide (curve_id).
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

    curves = list(_iter_selected_nurbs_curve_shapes())
    if not curves:
        cmds.warning('Select at least one NURBS curve (shape or transform).')
        return

    # Sample all curves first to know vertex count
    sampled = []
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

    vertex_count = sum(len(pts) for pts in sampled)

    # Write as ASCII bytes to avoid Py2/Py3 text/bytes differences.
    with open(path, 'wb') as f:
        f.write(_to_ascii_bytes('ply\n'))
        f.write(_to_ascii_bytes('format ascii 1.0\n'))
        f.write(_to_ascii_bytes('element vertex {0}\n'.format(vertex_count)))
        f.write(_to_ascii_bytes('property float x\n'))
        f.write(_to_ascii_bytes('property float y\n'))
        f.write(_to_ascii_bytes('property float z\n'))
        f.write(_to_ascii_bytes('property uchar anchor\n'))
        f.write(_to_ascii_bytes('property int curve_id\n'))
        f.write(_to_ascii_bytes('end_header\n'))

        cid = 0
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
