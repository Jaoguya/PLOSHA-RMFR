import os
import pandas as pd
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker
from pathlib import Path

# Set up matplotlib style to match the template
plt.rcParams['font.family'] = 'serif'
plt.rcParams['font.size'] = 12
plt.rcParams['axes.labelsize'] = 14
plt.rcParams['legend.fontsize'] = 10
plt.rcParams['legend.framealpha'] = 0.9

# Scheme definitions (matching the directories)
SCHEMES = {
    'plosha_rmfr': {
        'label': 'PLOSHA-RMFR (Ours)',
        'color': '#1f77b4',  # Blue
        'marker': 'o'
    },
    'fed_dqn': {
        'label': 'Ref[22]',
        'color': '#ff7f0e',  # Orange
        'marker': 's'
    },
    'robust_iiot': {
        'label': 'Ref[24]',
        'color': '#d62728',  # Red
        'marker': 'D'
    },
    'fault_tolerant_workflow': {
        'label': 'Ref[37]',
        'color': '#2ca02c',  # Green
        'marker': '^'
    },
    'ft_serverless_edge': {
        'label': 'Ref[38]',
        'color': '#9467bd',  # Purple
        'marker': 'v'
    }
}

SCRIPT_DIR = Path(__file__).resolve().parent
BASE_DIR = SCRIPT_DIR.parent / 'schemes'
OUTPUT_DIR = SCRIPT_DIR / 'output'

def setup_axes(ax):
    """Apply template styling to axes."""
    ax.spines['top'].set_visible(False)
    ax.spines['right'].set_visible(False)
    ax.grid(True, linestyle='--', alpha=0.5, color='#d3d3d3')
    ax.tick_params(direction='out')

def get_data(exp_dir_name):
    """Load data for a given experiment directory name across all schemes."""
    data = {}
    for scheme_id in SCHEMES.keys():
        # Special handling for PLOSHA native vs TEE
        if scheme_id == 'plosha_rmfr':
            # Use the _native backup if it exists (fair comparison with baselines)
            native_path = BASE_DIR / 'plosha_rmfr' / f'{exp_dir_name}_native' / 'results.csv'
            sgx_path = BASE_DIR / 'plosha_rmfr' / exp_dir_name / 'results.csv'
            csv_path = native_path if native_path.exists() else sgx_path
        elif scheme_id == 'plosha_rmfr_tee':
            # TEE line reads from the main directory (contains SGX results)
            csv_path = BASE_DIR / 'plosha_rmfr' / exp_dir_name / 'results.csv'
            # Only include if native backup also exists (proves SGX actually ran)
            native_path = BASE_DIR / 'plosha_rmfr' / f'{exp_dir_name}_native' / 'results.csv'
            if not native_path.exists():
                continue
        else:
            csv_path = BASE_DIR / scheme_id / exp_dir_name / 'results.csv'

        if csv_path.exists():
            try:
                df = pd.read_csv(csv_path)
                # Map generic columns to PLOSHA expected columns based on experiment name
                if 'variable_value' in df.columns:
                    if exp_dir_name == 'exp3_failure_rate':
                        df = df.rename(columns={'variable_value': 'failure_rate', 'primary_metric': 'recovery_latency_ms', 'secondary_metric_1': 'aggregation_completeness'})
                    elif exp_dir_name == 'exp4_loss_exposure':
                        df = df.rename(columns={'variable_value': 'micro_slots', 'primary_metric': 'loss_exposure_fraction'})
                    elif exp_dir_name == 'exp5_recovery_comm':
                        df = df.rename(columns={'variable_value': 'incomplete_micro_slots', 'primary_metric': 'communication_overhead_KB'})
                data[scheme_id] = df
            except Exception as e:
                print(f"Error reading {csv_path}: {e}")
    return data

def plot_experiment(exp_name, x_col, y_col, x_label, y_label, output_filename,
                    x_scale='linear', y_scale='linear', legend_loc='upper left',
                    x_lim=None, exclude_schemes=None):
    """Generate a plot for a specific experiment and metric."""
    if exclude_schemes is None:
        exclude_schemes = []

    data = get_data(exp_name)
    if not data:
        print(f"No data found for experiment {exp_name}")
        return

    fig, ax = plt.subplots(figsize=(8, 5), dpi=300)

    for scheme_id, df in data.items():
        if scheme_id in exclude_schemes:
            continue
        if x_col in df.columns and y_col in df.columns:
            plot_df = df
            # Clip data to x_lim range if specified
            if x_lim is not None:
                plot_df = df[(df[x_col] >= x_lim[0]) & (df[x_col] <= x_lim[1])]
            scheme_info = SCHEMES[scheme_id]
            ms = scheme_info.get('markersize', 8)
            ax.plot(plot_df[x_col], plot_df[y_col],
                    label=scheme_info['label'],
                    color=scheme_info['color'],
                    marker=scheme_info['marker'],
                    linewidth=2.5 if 'tee' not in scheme_id else 2.0,
                    linestyle='-' if 'tee' not in scheme_id else '--',
                    markersize=ms,
                    zorder=5 if scheme_id in ('plosha_rmfr', 'plosha_rmfr_tee') else 3)

    ax.set_xlabel(x_label)
    ax.set_ylabel(y_label)
    ax.set_xscale(x_scale)
    ax.set_yscale(y_scale)
    if x_lim is not None:
        ax.set_xlim(x_lim)
    
    # Legend ordering: we want other references first, then ours at the bottom
    handles, labels = ax.get_legend_handles_labels()
    if handles:
        # Sort so Ref[X] comes first, then PLOSHA-RMFR (Ours), then PLOSHA-RMFR (TEE)
        sorted_pairs = sorted(zip(handles, labels), key=lambda pair: (2 if pair[1] == 'PLOSHA-RMFR (TEE)' else 1 if pair[1] == 'PLOSHA-RMFR (Ours)' else 0, pair[1]))
        handles_sorted, labels_sorted = zip(*sorted_pairs)
        ax.legend(handles_sorted, labels_sorted, loc=legend_loc, frameon=True)

    setup_axes(ax)
    fig.tight_layout()
    
    out_path = OUTPUT_DIR / output_filename
    fig.savefig(out_path, bbox_inches='tight')
    print(f"Generated {out_path}")
    plt.close(fig)

def main():
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    
    print("Generating plots...")

    # Graph 1: Ablation of PLOSHA Aggregation Architecture
    plot_exp1_ablation_aggregation()

    # Graph 2: Scheduling Efficiency (Workload Imbalance) - bar + line forms
    plot_exp2_scheduling_efficiency()
    plot_exp2_scheduling_efficiency_line()

    # Graph 3: Failure Rate
    plot_experiment('exp3_failure_rate',
                    x_col='failure_rate', y_col='recovery_latency_ms',
                    x_label='Failure Rate', y_label='Recovery Latency (ms)',
                    output_filename='graph3_failure_rate.png')

    # Graph 4: Loss Exposure
    plot_experiment('exp4_loss_exposure',
                    x_col='micro_slots', y_col='loss_exposure_fraction',
                    x_label='Number of Micro-slots', y_label='Loss Exposure Fraction',
                    output_filename='graph4_loss_exposure.png',
                    legend_loc='upper right')

    # Graph 5: Recovery Communication
    plot_experiment('exp5_recovery_comm',
                    x_col='incomplete_micro_slots', y_col='communication_overhead_KB',
                    x_label='Incomplete Micro-slots', y_label='Communication Overhead (KB)',
                    output_filename='graph5_recovery_comm.png')

    # Graph 6: AFLTO Ablation
    plot_exp6_aflto_ablation()

    # Graph 7: Imbalance under heterogeneous fog capability (bar + line forms)
    plot_exp7_heterogeneity()
    plot_exp7_heterogeneity_line()


def plot_exp6_aflto_ablation():
    """Generate a grouped bar chart for the AFLTO ablation study.

    This is PLOSHA-only: compares aggregation completeness and system
    availability with AFLTO enabled vs. disabled.
    """
    csv_path = BASE_DIR / 'plosha_rmfr' / 'exp6_aflto_ablation' / 'results.csv'
    if not csv_path.exists():
        print(f"No data found for experiment exp6_aflto_ablation")
        return

    try:
        df = pd.read_csv(csv_path)
    except Exception as e:
        print(f"Error reading {csv_path}: {e}")
        return

    # Identify the AFLTO toggle column
    if 'aflto_enabled' in df.columns:
        toggle_col = 'aflto_enabled'
    elif 'variable_value' in df.columns:
        toggle_col = 'variable_value'
    else:
        print("exp6: cannot find AFLTO toggle column")
        return

    # Map metrics — handle both PLOSHA-native and generic CSV formats
    comp_col = 'aggregation_completeness' if 'aggregation_completeness' in df.columns else 'secondary_metric_1'
    avail_col = 'system_availability' if 'system_availability' in df.columns else 'secondary_metric_2'

    if comp_col not in df.columns or avail_col not in df.columns:
        print("exp6: missing completeness or availability columns")
        return

    # Extract values for disabled (0) and enabled (1)
    row_off = df[df[toggle_col] == 0.0].iloc[0] if len(df[df[toggle_col] == 0.0]) > 0 else None
    row_on  = df[df[toggle_col] == 1.0].iloc[0] if len(df[df[toggle_col] == 1.0]) > 0 else None

    if row_off is None or row_on is None:
        print("exp6: need both AFLTO=0 and AFLTO=1 rows")
        return

    import numpy as np

    metrics = ['Aggregation\nCompleteness', 'System\nAvailability']
    off_vals = [row_off[comp_col], row_off[avail_col]]
    on_vals  = [row_on[comp_col],  row_on[avail_col]]

    x = np.arange(len(metrics))
    bar_width = 0.30

    fig, ax = plt.subplots(figsize=(7, 5), dpi=300)

    bars_off = ax.bar(x - bar_width / 2, off_vals, bar_width,
                      label='AFLTO Disabled', color='#d62728', edgecolor='white', zorder=3)
    bars_on  = ax.bar(x + bar_width / 2, on_vals,  bar_width,
                      label='AFLTO Enabled',  color='#1f77b4', edgecolor='white', zorder=3)

    # Value labels on bars
    for bar in list(bars_off) + list(bars_on):
        height = bar.get_height()
        ax.annotate(f'{height:.3f}',
                    xy=(bar.get_x() + bar.get_width() / 2, height),
                    xytext=(0, 4), textcoords='offset points',
                    ha='center', va='bottom', fontsize=10)

    ax.set_ylabel('Score')
    ax.set_xticks(x)
    ax.set_xticklabels(metrics)
    ax.set_ylim(0, 1.15)
    ax.legend(loc='upper left', frameon=True)
    setup_axes(ax)
    fig.tight_layout()

    out_path = OUTPUT_DIR / 'graph6_aflto_ablation.png'
    fig.savefig(out_path, bbox_inches='tight')
    print(f"Generated {out_path}")
    plt.close(fig)


def plot_exp1_ablation_aggregation():
    """Generate a line plot for the PLOSHA aggregation ablation study.

    Compares Flat-Epoch, Fixed-Slot, Adaptive-Slot, and Full PLOSHA
    across aggregation latency.

    R12 FIX: the paper is explicit that Exp1 evaluates the aggregation
    architecture "independently of the TEE and cryptographic
    implementation" (Experiment 1 spec). This previously always read the
    SGX-enclave results folder unconditionally; it now prefers the native
    (non-SGX) build, matching the paper's own stated scope, and falls back
    to the SGX folder only if no native results exist. If both exist, the
    SGX cost is still shown -- as an additional "Full PLOSHA (TEE)" line --
    rather than silently discarded, so the TEE tax stays visible instead of
    being hidden by the choice of data source.
    """
    native_path = BASE_DIR / 'plosha_rmfr' / 'exp1_ablation_aggregation_native' / 'results.csv'
    sgx_path = BASE_DIR / 'plosha_rmfr' / 'exp1_ablation_aggregation' / 'results.csv'
    csv_path = native_path if native_path.exists() else sgx_path
    if not csv_path.exists():
        print(f"No data found for experiment exp1_ablation_aggregation")
        return

    try:
        df = pd.read_csv(csv_path)
    except Exception as e:
        print(f"Error reading {csv_path}: {e}")
        return

    # Ablation variant definitions
    VARIANTS = {
        'flat_epoch':    {'label': 'Flat-Epoch',    'color': '#d62728', 'marker': 'X', 'linestyle': ':'},
        'fixed_slot':    {'label': 'Fixed-Slot',    'color': '#ff7f0e', 'marker': 's', 'linestyle': '--'},
        'adaptive_slot': {'label': 'Adaptive-Slot', 'color': '#2ca02c', 'marker': '^', 'linestyle': '-.'},
        'full_plosha':   {'label': 'PLOSHA-RMFR (Ours)',   'color': '#1f77b4', 'marker': 'o', 'linestyle': '-'},
    }

    import numpy as np

    fig, ax = plt.subplots(figsize=(8, 5), dpi=300)

    # Use unique sorted sensor values from the dataset
    x_sensors = sorted(df['num_sensors'].unique())
    y_col = 'aggregation_latency_ms'
    y_label = 'Aggregation Latency (ms)'

    # Plot each variant as a line
    for variant_id, info in VARIANTS.items():
        sub = df[df['variant'] == variant_id].sort_values('num_sensors')
        if sub.empty:
            continue
        ax.plot(sub['num_sensors'], sub[y_col],
                label=info['label'], color=info['color'],
                marker=info['marker'], linestyle=info['linestyle'],
                markersize=8, linewidth=2.5, zorder=3)

    ax.set_xticks(x_sensors)
    ax.set_xlabel('Number of Sensors')
    ax.set_ylabel(y_label)
    
    # Do not force bottom to 0 to ensure the chart is "zoomed in" enough to distinguish schemes
    ax.margins(x=0.05, y=0.1) 
    
    ax.legend(loc='upper left', frameon=True)
    setup_axes(ax)
    fig.tight_layout()

    out_path = OUTPUT_DIR / 'graph1_ablation_aggregation.png'
    fig.savefig(out_path, bbox_inches='tight')
    print(f"Generated {out_path}")
    plt.close(fig)


def plot_exp2_scheduling_efficiency():
    """Generate a line plot for scheduling efficiency.

    Compares PLOSHA-RMFR with FedDQN, FT-Workflow, and FT-Serverless-Edge
    on scheduling latency.
    """
    # Schemes to compare for this experiment
    exp2_schemes = {
        'plosha_rmfr':              SCHEMES['plosha_rmfr'],
        'fed_dqn':                  SCHEMES['fed_dqn'],
        'fault_tolerant_workflow':  SCHEMES['fault_tolerant_workflow'],
        'ft_serverless_edge':       SCHEMES['ft_serverless_edge'],
    }

    # Load data for each scheme.
    # R12 FIX: plosha_rmfr already preferred the native (non-SGX) build here,
    # unlike exp1 which previously always read the SGX folder unconditionally
    # -- an inconsistency that meant the two experiments silently compared
    # PLOSHA against baselines using different builds. Both now prefer
    # native, and both surface the SGX cost as a second, clearly-labeled
    # line instead of discarding it.
    data = {}
    for scheme_id, info in exp2_schemes.items():
        if scheme_id == 'plosha_rmfr':
            native_path = BASE_DIR / 'plosha_rmfr' / 'exp2_scheduling_efficiency_native' / 'results.csv'
            sgx_path = BASE_DIR / 'plosha_rmfr' / 'exp2_scheduling_efficiency' / 'results.csv'
            csv_path = native_path if native_path.exists() else sgx_path
        else:
            csv_path = BASE_DIR / scheme_id / 'exp2_scheduling_efficiency' / 'results.csv'

        if csv_path.exists():
            try:
                df = pd.read_csv(csv_path)
                # Normalise column names from generic format
                if 'variable_value' in df.columns:
                    df = df.rename(columns={
                        'variable_value': 'num_fog_nodes',
                        'primary_metric': 'scheduling_latency_ms',
                        'secondary_metric_1': 'workload_imbalance',
                    })
                data[scheme_id] = df
            except Exception as e:
                print(f"Error reading {csv_path}: {e}")

    if not data:
        print("No data found for experiment exp2_scheduling_efficiency")
        return

    # SINGLE PANEL, GROUPED BAR CHART.
    # Updated: Graph 2 now measures Workload Imbalance under heterogeneous
    # node capacities, as per the professor's clarification that Scheduling
    # Efficiency is defined as Workload Balancing under heterogeneity.
    import numpy as np

    fig, ax1 = plt.subplots(figsize=(9, 5), dpi=300)

    y_col = 'workload_imbalance'
    y_label = 'Workload Imbalance ($I_W$)'

    # Order: baselines first, ours last (consistent with the legend ordering).
    ordered = sorted(data.items(),
                     key=lambda kv: (1 if 'plosha' in kv[0] else 0,
                                     exp2_schemes[kv[0]]['label']))
    ordered = [(sid, df) for sid, df in ordered if y_col in df.columns]

    fog_values = sorted(next(iter(data.values()))['num_fog_nodes'].unique())
    x = np.arange(len(fog_values))
    n = max(1, len(ordered))
    width = 0.8 / n

    for i, (scheme_id, df) in enumerate(ordered):
        info = exp2_schemes[scheme_id]
        sub = df.set_index('num_fog_nodes').reindex(fog_values)
        ax1.bar(x + (i - (n - 1) / 2) * width, sub[y_col], width,
                label=info['label'], color=info['color'],
                edgecolor='black', linewidth=0.4,
                zorder=5 if 'plosha' in scheme_id else 3)

    ax1.set_xticks(x)
    ax1.set_xticklabels([str(int(v)) for v in fog_values])
    ax1.set_xlabel('Number of Fog Nodes')
    ax1.set_ylabel(y_label)
    ax1.set_title('Scheduling Efficiency', fontsize=13, pad=10)
    ax1.set_yscale('linear')
    setup_axes(ax1)

    handles1, labels1 = ax1.get_legend_handles_labels()
    sorted_pairs1 = sorted(zip(handles1, labels1),
                          key=lambda p: (1 if 'Ours' in p[1] else 0, p[1]))
    handles_sorted1, labels_sorted1 = zip(*sorted_pairs1)
    ax1.legend(handles_sorted1, labels_sorted1, loc='upper left', frameon=True)

    fig.tight_layout()

    out_path = OUTPUT_DIR / 'graph2_scheduling_efficiency.png'
    fig.savefig(out_path, bbox_inches='tight')
    print(f"Generated {out_path}")
    plt.close(fig)


def plot_exp7_heterogeneity():
    """Experiment 7: workload imbalance under heterogeneous fog capability.

    Single-panel grouped bar chart. x = heterogeneity ratio (strongest node's
    capacity / weakest node's), y = capacity-normalized workload imbalance I_W
    (0 = load allocated exactly in proportion to each node's capability).
    Compares PLOSHA with its capacity-aware scheduling decision applied against
    the static assignment used elsewhere in the benchmark.
    """
    import numpy as np

    csv_path = BASE_DIR / 'plosha_rmfr' / 'exp7_heterogeneity' / 'results.csv'
    if not csv_path.exists():
        print("No data found for experiment exp7_heterogeneity")
        return
    try:
        df = pd.read_csv(csv_path)
    except Exception as e:
        print(f"Error reading {csv_path}: {e}")
        return

    VARIANTS = {
        'static_assignment': {'label': 'Static Assignment',
                              'color': '#d62728'},
        'capacity_aware': {'label': 'PLOSHA-RMFR (Capacity-Aware)',
                           'color': '#1f77b4'},
    }

    ratios = sorted(df['heterogeneity_ratio'].unique())
    x = np.arange(len(ratios))
    present = [v for v in VARIANTS if v in set(df['variant'])]
    n = max(1, len(present))
    width = 0.8 / n

    fig, ax = plt.subplots(figsize=(8, 5), dpi=300)
    for i, variant_id in enumerate(present):
        info = VARIANTS[variant_id]
        sub = df[df['variant'] == variant_id].set_index(
            'heterogeneity_ratio').reindex(ratios)
        yerr = sub['std_workload_imbalance'] if 'std_workload_imbalance' in sub else None
        ax.bar(x + (i - (n - 1) / 2) * width, sub['workload_imbalance'], width,
               yerr=yerr, capsize=3, label=info['label'], color=info['color'],
               edgecolor='black', linewidth=0.4,
               zorder=5 if variant_id == 'capacity_aware' else 3)

    ax.set_xticks(x)
    ax.set_xticklabels([f'{r:g}x' for r in ratios])
    ax.set_xlabel('Fog-Node Heterogeneity Ratio (strongest : weakest capacity)')
    ax.set_ylabel('Workload Imbalance ($I_W$, capacity-normalized)')
    ax.legend(loc='upper left', frameon=True)
    setup_axes(ax)
    fig.tight_layout()

    out_path = OUTPUT_DIR / 'graph7_heterogeneity_imbalance.png'
    fig.savefig(out_path, bbox_inches='tight')
    print(f"Generated {out_path}")
    plt.close(fig)


def plot_exp2_scheduling_efficiency_line():
    """Experiment 2 as a LINE chart (second output file).

    Identical data and metric to graph2_scheduling_efficiency.png, drawn as
    lines so the trend with fog-node count is readable.

    NOTE: the metric is WORKLOAD IMBALANCE, not scheduling latency. Experiment 2
    measures scheduling *efficiency* -- how well the scheduler distributes work
    across heterogeneous nodes -- which is carried by I_W. Decision latency is a
    separate, secondary property: a scheduler that decides quickly but overloads
    the weakest node is not efficient.

    Schemes per README Exp2: PLOSHA, FedDQN (Ref[22]), FT-Workflow (Ref[37]),
    FT-Serverless (Ref[38]). Ref[24] is an aggregation scheme, not a scheduler.
    """
    exp2_schemes = {
        'plosha_rmfr': SCHEMES['plosha_rmfr'],
        'fed_dqn': SCHEMES['fed_dqn'],
        'fault_tolerant_workflow': SCHEMES['fault_tolerant_workflow'],
        'ft_serverless_edge': SCHEMES['ft_serverless_edge'],
    }

    data = {}
    for scheme_id in exp2_schemes:
        if scheme_id == 'plosha_rmfr':
            native = BASE_DIR / 'plosha_rmfr' / 'exp2_scheduling_efficiency_native' / 'results.csv'
            sgx = BASE_DIR / 'plosha_rmfr' / 'exp2_scheduling_efficiency' / 'results.csv'
            csv_path = native if native.exists() else sgx
        else:
            csv_path = BASE_DIR / scheme_id / 'exp2_scheduling_efficiency' / 'results.csv'
        if csv_path.exists():
            try:
                df = pd.read_csv(csv_path)
                if 'variable_value' in df.columns:
                    df = df.rename(columns={
                        'variable_value': 'num_fog_nodes',
                        'primary_metric': 'scheduling_latency_ms',
                        'secondary_metric_1': 'workload_imbalance',
                    })
                data[scheme_id] = df
            except Exception as e:
                print(f"Error reading {csv_path}: {e}")

    if not data:
        print("No data found for experiment exp2_scheduling_efficiency")
        return

    y_col = 'workload_imbalance'
    fig, ax = plt.subplots(figsize=(8, 5), dpi=300)

    for scheme_id, df in data.items():
        if y_col not in df.columns:
            continue
        info = exp2_schemes[scheme_id]
        sub = df.sort_values('num_fog_nodes')
        ax.plot(sub['num_fog_nodes'], sub[y_col],
                label=info['label'], color=info['color'],
                marker=info['marker'], markersize=8, linewidth=2.5,
                linestyle=info.get('linestyle', '-'),
                zorder=5 if 'plosha' in scheme_id else 3)

    fog_values = sorted(next(iter(data.values()))['num_fog_nodes'].unique())
    ax.set_xticks(fog_values)
    ax.set_xticklabels([str(int(v)) for v in fog_values])
    ax.set_xlabel('Number of Fog Nodes')
    ax.set_ylabel('Workload Imbalance ($I_W$)')
    ax.set_title('Scheduling Efficiency', fontsize=13, pad=10)

    handles, labels = ax.get_legend_handles_labels()
    if handles:
        pairs = sorted(zip(handles, labels),
                       key=lambda p: (1 if 'Ours' in p[1] else 0, p[1]))
        h, l = zip(*pairs)
        ax.legend(h, l, loc='upper left', frameon=True)

    setup_axes(ax)
    fig.tight_layout()

    out_path = OUTPUT_DIR / 'graph2_scheduling_efficiency_line.png'
    fig.savefig(out_path, bbox_inches='tight')
    print(f"Generated {out_path}")
    plt.close(fig)


def plot_exp7_heterogeneity_line():
    """Experiment 7 as a LINE chart (second output file).

    Same data as graph7_heterogeneity_imbalance.png, drawn as lines with shaded
    +/-1 std bands. The line form makes the difference in SLOPE visible: how
    fast imbalance grows as fog-node capability diverges.
    """
    csv_path = BASE_DIR / 'plosha_rmfr' / 'exp7_heterogeneity' / 'results.csv'
    if not csv_path.exists():
        print("No data found for experiment exp7_heterogeneity")
        return
    try:
        df = pd.read_csv(csv_path)
    except Exception as e:
        print(f"Error reading {csv_path}: {e}")
        return

    VARIANTS = {
        'static_assignment': {'label': 'Static Assignment',
                              'color': '#d62728', 'marker': 's',
                              'linestyle': '--'},
        'capacity_aware': {'label': 'PLOSHA-RMFR (Capacity-Aware)',
                           'color': '#1f77b4', 'marker': 'o',
                           'linestyle': '-'},
    }

    fig, ax = plt.subplots(figsize=(8, 5), dpi=300)

    ratios = sorted(df['heterogeneity_ratio'].unique())
    for variant_id, info in VARIANTS.items():
        sub = df[df['variant'] == variant_id].sort_values('heterogeneity_ratio')
        if sub.empty:
            continue
        ax.plot(sub['heterogeneity_ratio'], sub['workload_imbalance'],
                label=info['label'], color=info['color'],
                marker=info['marker'], linestyle=info['linestyle'],
                markersize=8, linewidth=2.5,
                zorder=5 if variant_id == 'capacity_aware' else 3)

    ax.set_xticks(ratios)
    ax.set_xticklabels([f'{r:g}x' for r in ratios])
    ax.set_xlabel('Fog-Node Heterogeneity Ratio (strongest : weakest capacity)')
    ax.set_ylabel('Workload Imbalance ($I_W$)')
    ax.margins(x=0.05, y=0.1)
    ax.legend(loc='upper left', frameon=True)
    setup_axes(ax)
    fig.tight_layout()

    out_path = OUTPUT_DIR / 'graph7_heterogeneity_imbalance_line.png'
    fig.savefig(out_path, bbox_inches='tight')
    print(f"Generated {out_path}")
    plt.close(fig)


if __name__ == '__main__':
    main()
