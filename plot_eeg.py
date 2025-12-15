import numpy as np
import matplotlib.pyplot as plt
from matplotlib.ticker import ScalarFormatter
import os

# ================= CONFIGURACIÓN =================
FILENAME = 'eeg_data.bin'
# Now supporting 32 EEG channels + 1 event channel
NUM_CHANNELS = 32
SAMPLING_RATE = 250
PACKET_SIZE = 3 + (NUM_CHANNELS * 3)  # 3 header + 3 bytes per channel (24-bit)
# =================================================

def check_sync_issues(raw_bytes, packet_size):
    """
    Analiza los contadores de paquetes para detectar pérdida de datos.
    (Crucial para tu sincronización de biomarcadores).
    """
    n_packets = len(raw_bytes) // packet_size
    # Extraemos el contador (byte 0 de cada paquete)
    counters = raw_bytes[0 : n_packets*packet_size : packet_size]
    
    # CORRECCIÓN CRÍTICA: Convertir a int16 para evitar el error de Overflow
    counters = np.frombuffer(counters, dtype=np.uint8).astype(np.int16)
    
    # Calculamos diferencias
    diffs = np.diff(counters)
    
    # Corregimos el reinicio del contador (ej. 255 -> 0)
    diffs[diffs < 0] += 256
    
    errors = np.sum(diffs != 1)
    
    if errors == 0:
        print(f" [SYNC CHECK] ¡Sincronización Perfecta! (0 drops en {n_packets} paquetes)")
    else:
        print(f" [SYNC CHECK] ALERTA: Se detectaron {errors} saltos de sincronización.")

def parse_robust(filename, extract_events=False):
    if not os.path.exists(filename):
        print(f"Error: {filename} no encontrado.")
        return None

    with open(filename, 'rb') as f:
        raw_data = f.read()

    file_len = len(raw_data)
    expected_packets = file_len // PACKET_SIZE
    valid_bytes = expected_packets * PACKET_SIZE
    
    # Truncamos bytes sobrantes
    raw_data = raw_data[:valid_bytes]
    print(f"Leyendo {expected_packets} paquetes...")
    
    if expected_packets == 0: return None
        
    # 1. Chequeo de Sincronización
    check_sync_issues(raw_data, PACKET_SIZE)
    
    # 2. Parsing de Datos
    packets = np.frombuffer(raw_data, dtype=np.uint8).reshape(-1, PACKET_SIZE)
    eeg_bytes = packets[:, 3:] # Saltamos los 3 bytes de header
    
    vals = np.zeros((expected_packets, NUM_CHANNELS), dtype=np.float32)

    # Prepare event container when requested
    events = []  # list of (packet_index, channel_index, code_byte)
    
    for ch in range(NUM_CHANNELS):
        b1 = eeg_bytes[:, 3*ch].astype(np.int32)
        b2 = eeg_bytes[:, 3*ch+1].astype(np.int32)
        b3 = eeg_bytes[:, 3*ch+2].astype(np.int32)
        # Detect exact marker pattern [nonzero, 0x00, 0x00]
        if extract_events:
            mask = (b1 != 0) & (b2 == 0) & (b3 == 0)
            if np.any(mask):
                # record events as (packet_index, channel_index, code_byte)
                idxs = np.nonzero(mask)[0]
                for i in idxs:
                    events.append((int(i), int(ch), int(b1[i] & 0xFF)))

        # Reconstruct signed 24-bit value for EEG samples
        val = (b1 << 16) | (b2 << 8) | b3
        val[val >= 0x800000] -= 0x1000000
        vals[:, ch] = val
        
    if extract_events:
        return vals, events
    return vals

def simular_eventos(n_muestras):
    """Genera un canal artificial con picos esporádicos"""
    eventos = np.zeros(n_muestras)
    # Generamos 5 eventos aleatorios
    indices = np.random.choice(n_muestras, 5, replace=False)
    eventos[indices] = 100  # Amplitud alta para que se vea el pico
    return eventos

def plot_eeg(data):
    if data is None: return

    n_muestras = len(data)
    duration = n_muestras / SAMPLING_RATE
    t = np.linspace(0, duration, n_muestras)
    
    # Generamos los datos simulados del canal de eventos (canal extra)
    data_eventos = simular_eventos(n_muestras)
    # Configuramos subplots: NUM_CHANNELS EEG + 1 canal de eventos
    fig, axes = plt.subplots(NUM_CHANNELS + 1, 1, sharex=True, figsize=(12, 18))
   #fig.suptitle(f'EEG ({NUM_CHANNELS} canales) + Canal de Eventos — {duration:.2f}s', fontsize=16)

    # --- Graficar Canales EEG (0 .. NUM_CHANNELS-1) ---
    for i in range(NUM_CHANNELS):
        axes[i].plot(t, data[:, i], lw=0.6, color='#2c3e50')
        # Show channel label as CH1, CH2, ... (no amplitude numbers)
        axes[i].set_ylabel(f'CH{i+1}', rotation=0, labelpad=12, fontsize=8)
        # Remove numeric y-tick labels (amplitude numbers) for a cleaner look
        axes[i].set_yticks([])
        # Disable the automatic scientific multiplier (e.g. 1e6)
        fmt = ScalarFormatter()
        fmt.set_scientific(False)
        fmt.set_useOffset(False)
        axes[i].yaxis.set_major_formatter(fmt)
        axes[i].grid(True, alpha=0.25)
        axes[i].spines['top'].set_visible(False)
        axes[i].spines['right'].set_visible(False)
        axes[i].spines['bottom'].set_visible(False) # Ocultar eje X intermedio

    # --- Graficar Canal de Eventos (último subplot) ---
    ax_evt = axes[-1]
    ax_evt.plot(t, data_eventos, color='red', lw=1.5)
    ax_evt.set_ylabel('EVENTOS', rotation=0, labelpad=20, fontsize=9, fontweight='bold')
    ax_evt.grid(True, linestyle='--', alpha=0.5)
    
    # Limpiamos el estilo del canal de eventos
    ax_evt.spines['top'].set_visible(False)
    ax_evt.spines['right'].set_visible(False)
    ax_evt.set_yticks([]) # Ocultar números del eje Y para limpieza
    ax_evt.set_xlabel('Tiempo (segundos)', fontsize=12)

    plt.tight_layout()
    plt.show()

if __name__ == "__main__":
    res = parse_robust(FILENAME, extract_events=True)
    if res is None:
        data = None
        events = []
    elif isinstance(res, tuple):
        data, events = res
    else:
        data = res
        events = []

    print(f"Parsed EEG shape: {None if data is None else data.shape}")
    print(f"Detected events: {len(events)} (first 10) -> {events[:10]}")
    plot_eeg(data)