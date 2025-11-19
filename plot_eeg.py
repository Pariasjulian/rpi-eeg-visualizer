import numpy as np
import matplotlib.pyplot as plt
import os

# ================= CONFIGURATION =================
FILENAME = 'eeg_data.bin'
NUM_CHANNELS = 8
SAMPLING_RATE = 250
PACKET_SIZE = 27  # (3 header + 24 data)
# =================================================

def check_sync_issues(raw_bytes, packet_size):
    """
    Analyzes the packet counters to detect dropped data.
    Fixes 'OverflowError' by converting to 16-bit integers first.
    """
    # 1. Extract the first byte of every packet (The Counter)
    n_packets = len(raw_bytes) // packet_size
    counters = raw_bytes[0 : n_packets*packet_size : packet_size]
    
    # 2. CRITICAL FIX: Load as uint8, but convert to int16 for math
    #    This allows values to go below 0 or above 255 during calculation
    counters = np.frombuffer(counters, dtype=np.uint8).astype(np.int16)
    
    # 3. Calculate differences
    #    Ideal difference is always 1 (Packet 2 - Packet 1 = 1)
    diffs = np.diff(counters)
    
    # 4. Fix "Rollover" (e.g., Counter going from 255 -> 0)
    #    0 - 255 = -255. We add 256 to make it 1.
    diffs[diffs < 0] += 256
    
    # 5. Count errors (Any difference that is NOT 1 is a lost packet)
    #    We ignore the very first startup difference if necessary, 
    #    but usually, diffs should be all 1s.
    errors = np.sum(diffs != 1)
    
    if errors == 0:
        print(f" [SYNC CHECK] Perfect synchronization! (0 drops in {n_packets} packets)")
    else:
        print(f" [SYNC CHECK] WARNING: {errors} synchronization jumps detected.")
        # Optional: Print where they happened
        error_indices = np.where(diffs != 1)[0]
        if len(error_indices) < 10:
            print(f"              Jumps at packet indices: {error_indices}")
        else:
            print(f"              (Too many jumps to list individually)")

def parse_robust(filename):
    if not os.path.exists(filename):
        print(f"Error: {filename} not found.")
        return None

    with open(filename, 'rb') as f:
        raw_data = f.read()

    file_len = len(raw_data)
    print(f"File Length: {file_len} bytes")
    
    # Force alignment to PACKET_SIZE
    expected_packets = file_len // PACKET_SIZE
    valid_bytes = expected_packets * PACKET_SIZE
    
    # Truncate tail bytes
    raw_data = raw_data[:valid_bytes]
    print(f"Parsing {expected_packets} packets (Discarding {file_len - valid_bytes} tail bytes)...")
    
    if expected_packets == 0:
        print("Error: File is too short.")
        return None
        
    # Run Sync Check
    check_sync_issues(raw_data, PACKET_SIZE)
    
    # Reshape into packets
    packets = np.frombuffer(raw_data, dtype=np.uint8).reshape(-1, PACKET_SIZE)
    
    # Extract Data (Skip first 3 header bytes)
    eeg_bytes = packets[:, 3:]
    
    vals = np.zeros((expected_packets, NUM_CHANNELS), dtype=np.float32)
    
    for ch in range(NUM_CHANNELS):
        # Combine 3 bytes -> 1 int
        b1 = eeg_bytes[:, 3*ch].astype(np.int32)
        b2 = eeg_bytes[:, 3*ch+1].astype(np.int32)
        b3 = eeg_bytes[:, 3*ch+2].astype(np.int32)
        
        val = (b1 << 16) | (b2 << 8) | b3
        
        # 2's Complement
        val[val >= 0x800000] -= 0x1000000
        
        vals[:, ch] = val
        
    return vals

def plot_eeg(data):
    if data is None: return

    duration = len(data) / SAMPLING_RATE
    t = np.linspace(0, duration, len(data))
    
    fig, axes = plt.subplots(NUM_CHANNELS, 1, sharex=True, figsize=(10, 12))
    fig.suptitle(f'EEG Data ({duration:.2f}s) - Sync Verified', fontsize=16)
    
    for i in range(NUM_CHANNELS):
        if i < data.shape[1]:
            axes[i].plot(t, data[:, i], lw=1, color='#2c3e50')
            axes[i].set_ylabel(f'Ch {i+1}', rotation=0, labelpad=15)
            axes[i].grid(True, alpha=0.3)
            # Clean layout
            axes[i].spines['top'].set_visible(False)
            axes[i].spines['right'].set_visible(False)
    
    axes[-1].set_xlabel('Time (seconds)')
    plt.tight_layout()
    plt.show()

if __name__ == "__main__":
    data = parse_robust(FILENAME)
    plot_eeg(data)