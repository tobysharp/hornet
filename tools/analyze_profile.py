import argparse
import pandas as pd
import matplotlib.pyplot as plt
import os
import sys

def analyze_profile(csv_path, output_dir, bin_size=1000):
    if not os.path.exists(csv_path):
        print(f"Error: File {csv_path} not found.")
        return

    print(f"Loading {csv_path}... (this may take a moment)")
    try:
        # Read the CSV
        df = pd.read_csv(csv_path)
    except Exception as e:
        print(f"Error reading CSV: {e}")
        return

    # Ensure output directory exists
    os.makedirs(output_dir, exist_ok=True)

    print("Processing data...")
    # Convert microseconds to milliseconds
    df['Duration(ms)'] = df['Duration(us)'] / 1000.0
    
    # Define operation groups
    pipeline_ops = ['Parse', 'Append', 'Query', 'Fetch']
    
    # --- Plot 1: Pipeline Operations (Binned Statistics) ---
    print(f"Generating Pipeline Operations plot (bin size: {bin_size})...")
    
    pipeline_df = df[df['Operation'].isin(pipeline_ops)].copy()
    
    if not pipeline_df.empty:
        # Create bins
        pipeline_df['HeightBin'] = (pipeline_df['Height'] // bin_size) * bin_size
        
        # Aggregate median and percentiles (10th and 90th)
        # Using percentiles is more robust to outliers than mean/std
        def p10(x): return x.quantile(0.10)
        def p90(x): return x.quantile(0.90)
        
        agg = pipeline_df.groupby(['Operation', 'HeightBin'])['Duration(ms)'].agg(['median', p10, p90]).reset_index()
        
        plt.figure(figsize=(14, 8))
        
        # Plot each operation
        for op in pipeline_ops:
            data = agg[agg['Operation'] == op]
            if data.empty: continue
            
            # Plot median (no connecting line)
            line = plt.plot(
                data['HeightBin'], 
                data['median'], 
                marker='o',
                markersize=4,
                linestyle='None',
                label=op,
                alpha=0.5
            )
            color = line[0].get_color()

            # Plot 10th percentile (lower marker)
            plt.plot(
                data['HeightBin'], 
                data['p10'], 
                marker='^',
                markersize=4,
                linestyle='None',
                color=color,
                alpha=0.5
            )

            # Plot 90th percentile (upper marker)
            plt.plot(
                data['HeightBin'], 
                data['p90'], 
                marker='v',
                markersize=4,
                linestyle='None',
                color=color,
                alpha=0.5
            )
            
        plt.title(f'Pipeline Operations: Median Duration with 10th-90th Percentiles (per {bin_size} blocks)')
        plt.ylabel('Duration (ms)')
        plt.xlabel('Block Height')
        plt.ylim(0, 25) # Limit Y axis to focus on typical performance
        plt.legend(loc='upper left')
        plt.grid(True, which='both', linestyle='--', alpha=0.6)
        plt.tight_layout()
        plt.savefig(os.path.join(output_dir, 'pipeline_stats.png'))
        plt.close()
    else:
        print("No pipeline operations found.")

    # --- Plot 2: Flush Operations ---
    print("Generating Flush Operations plot...")
    flush_df = df[df['Operation'] == 'Flush'].copy()
    
    if not flush_df.empty:
        flush_df['HeightBin'] = (flush_df['Height'] // bin_size) * bin_size
        agg = flush_df.groupby('HeightBin')['Duration(ms)'].agg(['mean', 'std']).reset_index()
        
        # Calculate asymmetric error bars
        lower_err = agg[['mean', 'std']].min(axis=1)
        upper_err = agg['std']

        plt.figure(figsize=(12, 6))
        
        # Plot median (using mean here as per original logic)
        plt.plot(
            agg['HeightBin'], 
            agg['mean'], 
            marker='o',
            markersize=4,
            linestyle='None',
            label='Flush', 
            color='purple', 
            alpha=0.5
        )
        
        # Plot error markers
        # y_lower = mean - lower_err
        plt.plot(agg['HeightBin'], agg['mean'] - lower_err, marker='^', linestyle='None', color='purple', alpha=0.5)
        # y_upper = mean + upper_err
        plt.plot(agg['HeightBin'], agg['mean'] + upper_err, marker='v', linestyle='None', color='purple', alpha=0.5)
        
        plt.title(f'Flush Operation: Mean Duration ± StdDev (per {bin_size} blocks)')
        plt.ylabel('Duration (ms)')
        plt.xlabel('Block Height')
        plt.grid(True, which='both', linestyle='--', alpha=0.6)
        plt.tight_layout()
        plt.savefig(os.path.join(output_dir, 'flush_stats.png'))
        plt.close()

    # --- Plot 3: Merge Analysis ---
    print("Generating Merge Analysis plot...")
    # Note: For Merge, the 'Height' column actually contains the Level/Index
    merge_df = df[df['Operation'] == 'Merge'].copy()
    
    if not merge_df.empty:
        plt.figure(figsize=(10, 6))
        
        # Prepare data for boxplot
        levels = sorted(merge_df['Height'].unique())
        data_to_plot = [merge_df[merge_df['Height'] == l]['Duration(ms)'].values for l in levels]
        
        plt.boxplot(data_to_plot, labels=levels)
        
        plt.title('Merge Operation Duration by Level')
        plt.ylabel('Duration (ms)')
        plt.xlabel('Level (Index)')
        plt.yscale('log')
        plt.grid(True, which='both', linestyle='--', alpha=0.6)
        plt.tight_layout()
        plt.savefig(os.path.join(output_dir, 'merge_stats.png'))
        plt.close()

    print(f"Analysis complete. Plots saved to {output_dir}")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Analyze UTXO Profiler CSV")
    parser.add_argument("csv_file", nargs="?", default="utxo_profile.csv", help="Path to the CSV file")
    parser.add_argument("--output", "-o", default="profile_plots", help="Output directory for plots")
    parser.add_argument("--bin-size", "-b", type=int, default=1000, help="Bin size for block height aggregation")
    
    args = parser.parse_args()
    
    analyze_profile(args.csv_file, args.output, args.bin_size)
