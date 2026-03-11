import pandas as pd
import matplotlib.pyplot as plt
import sys

def plot_log_data(file_path):
    try:
        df = pd.read_csv(file_path, header=None, names=['Time', 'Latency'])

        df['Time'] = pd.to_datetime(df['Time'])

        plt.figure(figsize=(10, 6))
        plt.plot(df['Time'], df['Latency'], linestyle='-', color='r')

        plt.title('Latency Over Time', fontsize=14)
        plt.xlabel('Time of Day', fontsize=12)
        plt.ylabel('Latency (ms)', fontsize=12)
        plt.grid(True, linestyle='--', alpha=0.6)
        
        plt.xticks(rotation=45)
        plt.tight_layout()

        #plt.savefig('log_plot.png')
        plt.show()

    except Exception as e:
        print(f"An error occurred: {e}")

if __name__ == "__main__":
    plot_log_data(sys.argv[1])