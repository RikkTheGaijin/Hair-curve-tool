# -*- coding: utf-8 -*-
"""
Spiderweb PLY importer for Maya 2022 (Python 2)
- Reads ASCII PLY exported by the SpiderWeb editor (vertices + anchor flag)
- Creates one straight NURBS curve per web strand using Maya API 2.0 for speed
"""

from __future__ import print_function

try:
    xrange  # type: ignore[name-defined]
except NameError:  # Python 3 linters complain, but Maya 2022 runs Python 2
    xrange = range  # type: ignore[assignment]

import maya.cmds as cmds
import maya.api.OpenMaya as om
import time

# -----------------------------------------------------------------------------
# PLY parsing helpers
# -----------------------------------------------------------------------------

def _parse_ply_header(handle):
    meta = {
        'format': 'ascii',
        'vertex_count': 0,
        'vertex_props': [],
        'points_per_strand': None,
        'layers': [],
        'header_end': 0
    }
    in_vertex_element = False

    while True:
        line = handle.readline()
        if not line:
            break
        decoded = line.decode('ascii', 'ignore') if hasattr(line, 'decode') else line
        stripped = decoded.strip()

        if stripped == 'end_header':
            meta['header_end'] = handle.tell()
            break
        if stripped.startswith('format'):
            meta['format'] = stripped.split()[1]
        elif stripped.startswith('comment') and 'points_per_strand' in stripped:
            try:
                meta['points_per_strand'] = int(stripped.split()[-1])
            except Exception:
                pass
        elif stripped.startswith('comment layer '):
            try:
                rest = stripped[len('comment layer '):].strip()
                parts = rest.split(None, 1)
                lid = int(parts[0])
                name = ''
                color = None
                visible = True
                if len(parts) > 1:
                    tail = parts[1].strip()
                    if tail.startswith('"'):
                        end = tail.find('"', 1)
                        if end > 0:
                            name = tail[1:end]
                            tail = tail[end+1:].strip()
                    if tail:
                        nums = tail.split()
                        if len(nums) >= 3:
                            color = (float(nums[0]), float(nums[1]), float(nums[2]))
                        if len(nums) >= 4:
                            visible = (int(nums[3]) != 0)
                meta['layers'].append({'id': lid, 'name': name, 'color': color, 'visible': visible})
            except Exception:
                pass
        elif stripped.startswith('element vertex'):
            meta['vertex_count'] = int(stripped.split()[-1])
            in_vertex_element = True
        elif stripped.startswith('element'):
            in_vertex_element = False
        elif stripped.startswith('property') and in_vertex_element:
            parts = stripped.split()
            meta['vertex_props'].append(parts[2])

    return meta

def _read_ascii_vertices(handle, meta):
    props = meta['vertex_props']
    try:
        ix = props.index('x')
        iy = props.index('y')
        iz = props.index('z')
    except ValueError:
        raise RuntimeError('PLY file missing x/y/z properties')

    anchor_idx = props.index('anchor') if 'anchor' in props else None
    curve_id_idx = props.index('curve_id') if 'curve_id' in props else None
    layer_id_idx = props.index('layer_id') if 'layer_id' in props else None
    vertices = []

    # The web tool imports OBJs with a 0.01 scale factor (cm -> m).
    # We apply the inverse (100.0) here to restore original units.
    scale_factor = 100.0

    for _ in xrange(meta['vertex_count']):
        parts = handle.readline().strip().split()
        if len(parts) < 3:
            continue
        x = float(parts[ix]) * scale_factor
        y = float(parts[iy]) * scale_factor
        z = float(parts[iz]) * scale_factor
        anchor = int(parts[anchor_idx]) if anchor_idx is not None else 0
        curve_id = int(parts[curve_id_idx]) if curve_id_idx is not None else 0
        layer_id = int(parts[layer_id_idx]) if layer_id_idx is not None else 0
        vertices.append((x, y, z, anchor, curve_id, layer_id))

    return vertices

def _read_ply_vertices(path):
    with open(path, 'rb') as handle:
        meta = _parse_ply_header(handle)
        if meta['format'] != 'ascii':
            raise RuntimeError('Only ASCII PLY exported by SpiderWeb is supported')
        handle.seek(meta['header_end'])
        points = _read_ascii_vertices(handle, meta)
    return meta, points

# -----------------------------------------------------------------------------
# Curve creation helpers
# -----------------------------------------------------------------------------

def _create_curve(points, degree, parent, name_prefix='GuideCurve'):
    m_points = om.MPointArray()
    for p in points:
        m_points.append(om.MPoint(p[0], p[1], p[2]))

    num = len(points)
    degree = min(degree, num - 1) if num > 1 else 1

    if degree <= 1:
        knots = [i for i in range(num)]
        curve_degree = 1
    else:
        knots = [i for i in range(num - degree + 1)]
        curve_degree = degree

    transform_obj = om.MFnTransform().create(parent)
    shape_fn = om.MFnNurbsCurve()
    shape_fn.create(
        m_points,
        knots,
        curve_degree,
        om.MFnNurbsCurve.kOpen,
        False,
        False,
        transform_obj
    )

    om.MFnTransform(transform_obj).setName('{0}#'.format(name_prefix))
    return transform_obj

# -----------------------------------------------------------------------------
# Public entry point
# -----------------------------------------------------------------------------

def _build_group_name(base_name, source_path):
    name = base_name
    if source_path:
        safe = source_path.replace(':', '_').replace('/', '_').replace('\\', '_')
        name = '{0}_{1}'.format(base_name, safe.split('_')[-1])
    candidate = name
    suffix = 1
    while cmds.objExists(candidate):
        candidate = '{0}_{1:02d}'.format(name, suffix)
        suffix += 1
    return candidate


def import_spiderweb_ply(batch_size=5000,
                         degree=1,
                         group_name='Curves_grp'):
    """Import a SpiderWeb PLY file and rebuild strands as curves."""

    file_filter = 'PLY Files (*.ply);;All Files (*.*)'
    result = cmds.fileDialog2(fileFilter=file_filter, dialogStyle=2, fileMode=1)
    if not result:
        return
    path = result[0]

    start_time = time.time()
    print('\n=== Importing SpiderWeb PLY ===')
    print('Reading file: {0}'.format(path))

    meta, vertices = _read_ply_vertices(path)

    # New format: variable-length strands via per-vertex curve_id.
    has_curve_id = 'curve_id' in (meta.get('vertex_props') or [])
    has_layer_id = 'layer_id' in (meta.get('vertex_props') or [])
    layer_names = {l.get('id', 0): (l.get('name') or ('Layer {0}'.format(l.get('id', 0)))) for l in (meta.get('layers') or [])}
    if has_curve_id:
        # Preserve appearance order within each curve_id.
        strands = {}
        strand_layers = {}
        order = []
        for v in vertices:
            cid = v[4]
            lid = v[5] if has_layer_id else 0
            if cid not in strands:
                strands[cid] = []
                order.append(cid)
                strand_layers[cid] = lid
            strands[cid].append(v)
        strand_ids = sorted(order)
        print('→ {0} strands detected (variable points per strand)'.format(len(strand_ids)))
    else:
        # Backward compatibility: fixed points_per_strand chunking.
        pts_per_strand = meta.get('points_per_strand') or 2
        if pts_per_strand < 2:
            pts_per_strand = 2

        total = len(vertices)
        if total % pts_per_strand != 0:
            print('Warning: vertex count ({0}) is not divisible by points_per_strand ({1}).'.format(total, pts_per_strand))

        strand_count = total // pts_per_strand
        print('→ {0} strands detected (points per strand = {1})'.format(strand_count, pts_per_strand))

    unique_group_name = _build_group_name(group_name, path)
    group = cmds.group(em=True, n=unique_group_name)
    cmds.addAttr(group, ln='spiderWebSource', dt='string')
    cmds.setAttr('{}.spiderWebSource'.format(group), path, type='string')
    group_dag = om.MSelectionList().add(group).getDagPath(0)
    group_node = group_dag.node()

    cmds.undoInfo(state=False)
    cmds.refresh(suspend=True)

    try:
        created = 0
        layer_groups = {}

        def _layer_group(layer_id):
            if layer_id not in layer_groups:
                name = layer_names.get(layer_id, 'Layer {0}'.format(layer_id))
                lg = cmds.group(em=True, n=name, parent=group)
                layer_groups[layer_id] = lg
            return layer_groups[layer_id]

        if has_curve_id:
            total_strands = len(strand_ids)
            for cid in strand_ids:
                chunk = strands.get(cid) or []
                if len(chunk) < 2:
                    continue
                lid = strand_layers.get(cid, 0)
                positions = [(p[0], p[1], p[2]) for p in chunk]
                parent = _layer_group(lid)
                parent_dag = om.MSelectionList().add(parent).getDagPath(0).node()
                _create_curve(positions, degree, parent_dag)
                created += 1
                if created % batch_size == 0:
                    print('  Built {0}/{1} strands...'.format(created, total_strands))
        else:
            for i in xrange(strand_count):
                start = i * pts_per_strand
                chunk = vertices[start:start + pts_per_strand]
                if len(chunk) < 2:
                    continue
                lid = chunk[0][5] if has_layer_id else 0
                positions = [(p[0], p[1], p[2]) for p in chunk]
                parent = _layer_group(lid)
                parent_dag = om.MSelectionList().add(parent).getDagPath(0).node()
                _create_curve(positions, degree, parent_dag)
                created += 1
                if created % batch_size == 0:
                    print('  Built {0}/{1} strands...'.format(created, strand_count))

        print('✅ Imported {0} strands in {1:.2f}s'.format(created, time.time() - start_time))
    finally:
        cmds.refresh(suspend=False)
        cmds.undoInfo(state=True)
        cmds.select(group)


if __name__ == '__main__':
    import_spiderweb_ply()
