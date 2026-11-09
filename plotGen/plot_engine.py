import matplotlib.pyplot as plt
import matplotlib.patheffects as pe
from matplotlib.lines import Line2D
import numpy as np
import pandas as pd
import os
from textwrap import fill
from debug_logger import dbg, dbg_dataframe, dbg_dict

# plt.rcParams['text.usetex'] = True
# plt.rcParams['font.family'] = 'serif'

MARKER_SIZE = 8
LINE_WIDTH = 2


def calculate_x_positions(x_values, method="linear"):
    """
    Generic X-axis positioning logic using numpy (formerly calculate_thread_positions).
    """
    dbg(f"calculate_x_positions: raw x_values={x_values}, method='{method}'")
    x_values = sorted(list(set(x_values)))
    if not x_values:
        dbg("x_values is empty after dedup/sort, returning empty structures.")
        return {}, [], []

    x_arr = np.array(x_values)

    if method == "log":
        with np.errstate(divide='ignore', invalid='ignore'):
            positions = np.log2(np.where(x_arr <= 0, np.nan, x_arr))
            positions = np.nan_to_num(positions, nan=0.0, posinf=0.0, neginf=0.0)

        if positions.max() > positions.min():
            positions = 5 * (positions - positions.min()) / (positions.max() - positions.min())
        else:
            positions = np.zeros_like(positions)
        dbg(f"Log-scale positions computed: {positions}")
    else:
        if x_arr.max() > x_arr.min():
            positions = 5 * (x_arr - x_arr.min()) / (x_arr.max() - x_arr.min())
        else:
            positions = np.zeros_like(x_arr)
        dbg(f"Linear-scale positions computed: {positions}")

    mapping = {x: pos for x, pos in zip(x_values, positions)}
    dbg_dict(mapping, "X position mapping")
    return mapping, list(positions), [str(x) for x in x_values]


def aggregate_metric(df_group, plot_def):
    """
    Universal Aggregator: Trim -> Calculate -> Aggregate
    """
    dbg(f"aggregate_metric: group size={len(df_group)}")

    # 1. TRIM logic
    trim_col = plot_def.get('trim_by', 'metrics.result_ops')
    k = plot_def.get('trim_k', 0)
    dbg(f"Trim: trim_col='{trim_col}', k={k}")

    try:
        df_sorted = df_group.sort_values(by=trim_col)
    except KeyError:
        print(f"[WARN] Trim column '{trim_col}' not found. Skipping trim.")
        dbg(f"Trim column '{trim_col}' missing from columns: {list(df_group.columns)}")
        df_sorted = df_group

    n = len(df_sorted)
    if n > 2 * k:
        df_trimmed = df_sorted.iloc[k: n - k]
        dbg(f"Trimmed {k} top and {k} bottom rows. Before={n}, After={len(df_trimmed)}")
    else:
        df_trimmed = df_sorted
        dbg(f"Not enough rows to trim (n={n}, 2k={2*k}), using all rows.")

    if df_trimmed.empty:
        dbg("df_trimmed is empty after trim, returning NaN.")
        return np.nan

    # 2. CALCULATE logic
    y_def = plot_def['data_source']['y_value']
    dbg(f"Y value definition: '{y_def}'")

    try:
        if "expression:" in y_def:
            expr = y_def.replace("expression:", "").strip()
            dbg(f"Evaluating Y expression: '{expr}'")
            values = df_trimmed.eval(expr)
        else:
            values = df_trimmed[y_def]

        values = values.replace([np.inf, -np.inf], np.nan).fillna(0)
        dbg(f"Values after cleanup: count={len(values)}, mean={values.mean():.4f}, std={values.std():.4f}")
    except Exception as e:
        print(f"[ERROR] Failed to evaluate expression '{y_def}': {e}")
        dbg(f"Exception during Y evaluation: {e}")
        return np.nan

    # 3. AGGREGATE logic
    agg_type = plot_def['data_source'].get('aggregator', 'mean').lower()
    dbg(f"Aggregator type: '{agg_type}'")

    if agg_type == 'mean':
        result = values.mean()
    else:
        dbg(f"Unknown aggregator '{agg_type}', falling back to mean.")
        result = values.mean()

    dbg(f"aggregate_metric result: {result:.6f}")
    return result


def slide_markers_along_curves(fig, ax, items, max_slide_pts, overlap_factor=1.2):
    """
    Resolves marker overlaps by sliding markers along their OWN curves, locally and minimally.

    - Overlap is measured on screen: markers of different algorithms collide when their
      centers are closer than `overlap_factor * MARKER_SIZE` points.
    - The shift is computed independently for every group of colliding markers (every x
      position / region). Only the markers that actually collide are moved, and only by the
      minimum needed to separate them. If two curves stop colliding at some point, their
      markers there stay exactly on the data point; if they start colliding again further
      on, the shift starts again there.
    - Within a group, markers are separated along the local direction of the curves, so
      the order along that direction is preserved and the slide is as small as possible.
    - Lines, data points, axis scale and ticks are never touched. Markers always remain on
      their own polyline (a slide never goes beyond 40% of the neighbouring segment, nor
      beyond `max_slide_pts`).

    items: list of (marker_artist, xs, ys, alg_idx)
    """
    fig.canvas.draw()  # finalize limits & layout so the data->screen transform is final
    trans = ax.transData
    inv = trans.inverted()
    px_per_pt = fig.dpi / 72.0
    thr = overlap_factor * MARKER_SIZE * px_per_pt     # min center distance between markers
    cap = max_slide_pts * px_per_pt

    screen = [trans.transform(np.column_stack([xs, ys])) for _, xs, ys, _ in items]

    # ---- Per-marker geometry (screen space, pixels) ----
    markers = []   # dicts: a (item), v (vertex), alg, p, t_out, L_out, t_in, L_in
    for a, pts in enumerate(screen):
        n = len(pts)
        for v in range(n):
            t_out = t_in = None
            L_out = L_in = 0.0
            if v < n - 1:
                vec = pts[v + 1] - pts[v]
                L = float(np.hypot(*vec))
                if L > 0:
                    t_out, L_out = vec / L, L
            if v > 0:
                vec = pts[v] - pts[v - 1]
                L = float(np.hypot(*vec))
                if L > 0:
                    t_in, L_in = vec / L, L
            markers.append(dict(a=a, v=v, alg=items[a][3], p=pts[v].copy(),
                                t_out=t_out, L_out=L_out, t_in=t_in, L_in=L_in))
    N = len(markers)

    # ---- Group markers that are close to each other (transitive, different algs only) ----
    parent = list(range(N))

    def find(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i

    group_radius = 2.0 * thr
    for i in range(N):
        for j in range(i + 1, N):
            if markers[i]['alg'] == markers[j]['alg']:
                continue
            if np.hypot(*(markers[i]['p'] - markers[j]['p'])) < group_radius:
                ri, rj = find(i), find(j)
                if ri != rj:
                    parent[ri] = rj

    groups = {}
    for i in range(N):
        groups.setdefault(find(i), []).append(i)

    new_pos = [m['p'].copy() for m in markers]
    moved = 0

    for members in groups.values():
        if len({markers[i]['alg'] for i in members}) < 2:
            continue

        # Local direction of the curves in this group
        e = np.zeros(2)
        for i in members:
            t = markers[i]['t_out'] if markers[i]['t_out'] is not None else markers[i]['t_in']
            if t is not None:
                e += t
        if np.hypot(*e) == 0:
            continue
        e /= np.hypot(*e)
        nrm = np.array([-e[1], e[0]])

        # Movement bounds along e (a marker must stay on its own polyline)
        lo, hi, u = {}, {}, {}
        for i in members:
            m = markers[i]
            u[i] = float(m['p'] @ e)
            h = 0.0
            if m['t_out'] is not None and float(m['t_out'] @ e) > 0.1:
                proj = float(m['t_out'] @ e)
                h = min(0.4 * m['L_out'], cap) * proj
            l = 0.0
            if m['t_in'] is not None and float(m['t_in'] @ e) > 0.1:
                proj = float(m['t_in'] @ e)
                l = -min(0.4 * m['L_in'], cap) * proj
            lo[i], hi[i] = l, h

        # Pair constraints: along-e gap needed so that the 2D distance is >= thr
        cons = []
        for x in range(len(members)):
            for y in range(x + 1, len(members)):
                i, j = members[x], members[y]
                if markers[i]['alg'] == markers[j]['alg']:
                    continue
                w = abs(float((markers[i]['p'] - markers[j]['p']) @ nrm))
                if w >= thr:
                    continue
                g = float(np.sqrt(thr * thr - w * w))
                # order along e; ties broken by plotting order for a deterministic result
                if (u[i], markers[i]['alg']) <= (u[j], markers[j]['alg']):
                    cons.append((i, j, g))
                else:
                    cons.append((j, i, g))

        # Minimal-displacement separation (iterative projection)
        delta = {i: 0.0 for i in members}
        for _ in range(300):
            changed = False
            for i, j, g in cons:
                viol = g - ((u[j] + delta[j]) - (u[i] + delta[i]))
                if viol <= 1e-6:
                    continue
                changed = True
                ni = min(max(delta[i] - viol / 2, lo[i]), hi[i])
                rem = viol - (delta[i] - ni)
                nj = min(max(delta[j] + rem, lo[j]), hi[j])
                rem -= (nj - delta[j])
                if rem > 1e-9:
                    ni = min(max(ni - rem, lo[i]), hi[i])
                delta[i], delta[j] = ni, nj
            if not changed:
                break

        # Convert along-e displacement into a slide along each marker's own polyline
        for i in members:
            d = delta[i]
            if abs(d) < 1e-6:
                continue
            m = markers[i]
            if d > 0 and m['t_out'] is not None:
                new_pos[i] = m['p'] + m['t_out'] * (d / float(m['t_out'] @ e))
                moved += 1
            elif d < 0 and m['t_in'] is not None:
                new_pos[i] = m['p'] - m['t_in'] * (-d / float(m['t_in'] @ e))
                moved += 1

    dbg(f"Marker overlap: {moved} of {N} markers moved")

    # ---- Write back (all other markers stay exactly on their data points) ----
    for a, (artist, xs, ys, _) in enumerate(items):
        pts = np.array([new_pos[k] for k, m in enumerate(markers) if m['a'] == a])
        data = inv.transform(pts)
        artist.set_data(data[:, 0], data[:, 1])


def render_plot(df, plot_def, global_styles, output_dir):
    """
    Renders a single plot based on the definition.
    """
    dbg(f"render_plot: output_dir='{output_dir}', plot_def keys={list(plot_def.keys())}")
    dbg_dataframe(df, "render_plot input DataFrame")

    plot_df = df.copy()

    # --- X-AXIS LOGIC ---
    x_def = plot_def['data_source'].get('x_value', 'threads')
    dbg(f"X definition: '{x_def}'")

    if isinstance(x_def, str) and x_def.startswith("expression:"):
        expr = x_def.replace("expression:", "").strip()
        dbg(f"Evaluating X expression: '{expr}'")
        try:
            temp_x = plot_df.eval(expr)
            plot_df['_x_col'] = temp_x.replace([np.inf, -np.inf], np.nan).fillna(0)
            x_col = '_x_col'
            dbg(f"X expression evaluated OK. Sample values: {plot_df['_x_col'].head().tolist()}")
        except Exception as e:
            print(f"[ERROR] Failed to evaluate X expression '{x_def}': {e}")
            dbg(f"Exception during X expression evaluation: {e}")
            return
    else:
        x_col = x_def

    if x_col not in plot_df.columns:
        print(f"[ERROR] X-Axis column '{x_col}' not found in data.")
        dbg(f"Available columns: {list(plot_df.columns)}")
        return

    # Order algorithms
    available_algs = plot_df['algorithm'].unique()
    ordered_algs = [alg for alg in global_styles.get('order', []) if alg in available_algs]
    remaining = [alg for alg in available_algs if alg not in ordered_algs]
    final_alg_order = ordered_algs + remaining
    dbg(f"Algorithm render order: {final_alg_order}")

    # Calculate X positions
    all_x_vals = sorted(plot_df[x_col].unique())
    normalization = global_styles.get('normalization', 'linear')
    dbg(f"X values: {all_x_vals}, normalization='{normalization}'")
    pos_map, x_ticks, x_labels = calculate_x_positions(all_x_vals, method=normalization)

    max_y_val = 0
    color_map = global_styles.get('color_map', {})
    marker_map = global_styles.get('marker_map', {})
    aggregator = plot_def['data_source'].get('aggregator', 'mean').lower()
    dbg(f"color_map={color_map}, aggregator='{aggregator}'")

    # Marker slide: max slide (in points) of a marker along its curve; 0 disables the feature
    slide_step = global_styles.get('marker_slide_pts', 0.0)
    overlap_factor = global_styles.get('marker_overlap_factor', 1.2)
    dbg(f"marker_slide_pts={slide_step}, marker_overlap_factor={overlap_factor}")

    fig = plt.gcf()
    ax = plt.gca()
    marker_items = []     # (artist, xs, ys, slide_pts) -> repositioned after layout is final
    legend_handles = []   # proxy handles: legend always shows the marker centered on the line

    # --- Plot Algorithms ---
    for alg_idx, alg in enumerate(final_alg_order):
        alg_df = plot_df[plot_df['algorithm'] == alg]
        if alg_df.empty:
            dbg(f"Algorithm '{alg}' has no data after filtering, skipping.")
            continue

        dbg(f"Processing algorithm '{alg}': {len(alg_df)} rows")
        xs = []
        ys = []

        for x_val, group in alg_df.groupby(x_col):
            val = aggregate_metric(group, plot_def)
            if not np.isnan(val):
                xs.append(pos_map[x_val])
                ys.append(val)
                max_y_val = max(max_y_val, val)
                dbg(f"  x_val={x_val} -> pos={pos_map[x_val]:.3f}, agg_val={val:.4f}")
            else:
                dbg(f"  x_val={x_val} -> NaN result, skipped.")

        if xs:
            zipped = sorted(zip(xs, ys))
            xs, ys = zip(*zipped)

            color = color_map.get(alg, None)
            m_style = marker_map.get(alg, {'marker': 'o', 'fillstyle': 'full', 'linestyle': '-'})
            linestyle = m_style.get('linestyle', '-')
            label = rf"\textsc{{{alg}}}"
            dbg(f"Plotting '{alg}': color={color}, marker={m_style}, linestyle='{linestyle}', {len(xs)} points")

            # 1) The curve itself: exact data points, no markers
            line, = plt.plot(xs, ys, color=color, linestyle=linestyle,
                             linewidth=LINE_WIDTH, zorder=2)
            # Resolve the actual color used (matters when color_map has no entry)
            real_color = line.get_color()

            # 2) Markers only, drawn on top; repositioned later along the curve
            marker_artist, = plt.plot(xs, ys, color=real_color, linestyle='none',
                                      marker=m_style['marker'], fillstyle=m_style['fillstyle'],
                                      markersize=MARKER_SIZE, zorder=3)

            marker_items.append((marker_artist, list(xs), list(ys), alg_idx))

            # 3) Legend proxy (line + centered marker, same style as the plot)
            legend_handles.append(Line2D([], [], color=real_color, linestyle=linestyle,
                                         linewidth=LINE_WIDTH, marker=m_style['marker'],
                                         fillstyle=m_style['fillstyle'], markersize=MARKER_SIZE,
                                         label=label))
        else:
            dbg(f"Algorithm '{alg}' produced no valid (x, y) pairs, nothing plotted.")

    # --- Styling ---
    if global_styles.get('show_title', False):
        raw_title = plot_def.get('title', '')
        dbg(f"Title: '{raw_title}'")
        plt.title(fill(raw_title, width=50), fontsize=16)

    raw_xlabel = plot_def.get('x_label', x_def.replace('expression:', '').strip())
    raw_ylabel = plot_def.get('y_label', 'Value')
    dbg(f"Labels: x='{raw_xlabel}', y='{raw_ylabel}'")

    plt.xlabel(fill(raw_xlabel, width=50), fontsize=14)
    plt.ylabel(fill(raw_ylabel, width=30), fontsize=14)
    plt.xticks(x_ticks, x_labels, fontsize=12)
    plt.grid(True, alpha=0.3)

    ylim_config = plot_def.get('ylim', None)
    auto_ylim = max_y_val * 1.1 if max_y_val > 0 else None

    if ylim_config is not None:
        ylim_val = float(ylim_config)
        if auto_ylim is not None:
            final_ylim = min(ylim_val, auto_ylim)
        else:
            final_ylim = ylim_val

        dbg(f"Applying min(manual, auto) ylim: (0, {final_ylim:.4f})")
        plt.ylim(0, final_ylim)
    elif auto_ylim is not None:
        dbg(f"Auto ylim: (0, {auto_ylim:.4f})")
        plt.ylim(0, auto_ylim)

    if global_styles.get('show_legend', True):
        ncols = min(5, len(final_alg_order))
        dbg(f"Legend: ncol={ncols}")
        leg = plt.legend(handles=legend_handles, loc='lower center', bbox_to_anchor=(0.5, 1.15),
                         ncol=ncols, fancybox=True, shadow=True, fontsize=12)

        for text in leg.get_texts():
            text.set_path_effects([pe.Stroke(linewidth=0.2, foreground='black'), pe.Normal()])

    plt.tight_layout()

    # Layout is final -> detect overlapping markers and slide only those along their curves
    if slide_step:
        slide_markers_along_curves(fig, ax, marker_items, slide_step, overlap_factor)

    out_filename = plot_def.get('output_file', 'plot.png')
    out_path = os.path.join(output_dir, out_filename)
    dbg(f"Saving plot to: '{out_path}'")
    plt.savefig(out_path, dpi=600, bbox_inches='tight')
    plt.close()
    print(f"[PLOT] Saved: {out_path}")