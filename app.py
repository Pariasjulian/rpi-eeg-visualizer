import os
import socket
import threading
from flask import Flask, send_from_directory, Response

# --- CONFIGURACIÓN ---
# Puerto donde Gunicorn hablará con Nginx (este es un puerto interno)
# GUNICORN_PORT = 8000 (Este se define en el comando de Gunicorn)

# Puerto donde este script (el Hub) escuchará a tu cliente C
DATA_PORT = 9090        
DATA_HOST = '127.0.0.1' # Escuchar solo en localhost para el cliente C
DIRECTORY = os.getcwd() # Directorio del proyecto

# --- Variables Globales ---

# Variable global para guardar la última línea de datos recibida
LATEST_DATA_LINE = "No data received yet."
# "Lock" (seguro) para acceso seguro a la variable desde múltiples hilos
DATA_LOCK = threading.Lock()


# -------------------------------------------------------------------
# 1. CREACIÓN DE LA APLICACIÓN FLASK
# -------------------------------------------------------------------

app = Flask(__name__)

# -------------------------------------------------------------------
# 2. LÓGICA DEL SERVIDOR TCP (PARA HABLAR CON C)
# -------------------------------------------------------------------

def run_tcp_server_thread():
    """
    Este hilo corre el SERVIDOR TCP para escuchar al cliente C.
    Se ejecuta en segundo plano.
    """
    global LATEST_DATA_LINE, DATA_LOCK
    
    # Creamos el socket TCP
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        # Permite re-utilizar el puerto inmediatamente si el script se reinicia
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        
        try:
            s.bind((DATA_HOST, DATA_PORT))
        except OSError as e:
            print(f"!!! FATAL: No se pudo enlazar al puerto TCP {DATA_PORT}. ¿Ya está en uso? {e}")
            return

        s.listen()
        print(f"[Py Hub - TCP] Escuchando datos de C en {DATA_HOST}:{DATA_PORT}...")
        
        while True: # Bucle infinito para aceptar clientes (si C se reinicia)
            try:
                conn, addr = s.accept()
                print(f"[Py Hub - TCP] Cliente C conectado desde {addr}")
                
                with conn:
                    # 'makefile' nos permite leer línea por línea fácilmente
                    f = conn.makefile('r')
                    while True:
                        line = f.readline()
                        if not line:
                            # Si la línea está vacía, el cliente C se desconectó
                            print("[Py Hub - TCP] Cliente C desconectado.")
                            break # Salir del bucle de lectura
                        
                        # Guardar la línea de datos de forma segura
                        with DATA_LOCK:
                            LATEST_DATA_LINE = line
            except Exception as e:
                print(f"[Py Hub - TCP] Error en el socket: {e}")
                time.sleep(1) # Esperar un segundo antes de reintentar

# -------------------------------------------------------------------
# 3. RUTAS DEL SERVIDOR WEB (FLASK ENDPOINTS)
# -------------------------------------------------------------------

@app.route('/')
def index():
    """
    Sirve el archivo webgl_graph.html.
    En producción, Nginx hará esto, pero es bueno tener esta ruta
    para pruebas y como respaldo.
    """
    try:
        return send_from_directory(DIRECTORY, 'webgl_graph.html')
    except FileNotFoundError:
        return "Error: no se encuentra 'webgl_graph.html'.", 404

@app.route('/data')
def data():
    """
    Este es el endpoint que el JavaScript del navegador llama
    para obtener los datos más recientes del EEG.
    """
    global LATEST_DATA_LINE, DATA_LOCK
    
    # Leer la última línea de datos de forma segura
    with DATA_LOCK:
        rsp = LATEST_DATA_LINE
    
    # Devolver los datos como texto plano
    return Response(rsp, mimetype='text/plain')

# -------------------------------------------------------------------
# 4. PUNTO DE ENTRADA
# -------------------------------------------------------------------

# Iniciar el hilo del servidor TCP inmediatamente cuando Gunicorn carga el módulo
print("[Py Hub] Iniciando el hilo del servidor TCP...")
tcp_thread = threading.Thread(target=run_tcp_server_thread, daemon=True)
tcp_thread.start()

# Esta parte solo se ejecuta si corres 'python3 app.py'
# Gunicorn NO ejecuta este bloque.
if __name__ == '__main__':
    print("---------------------------------------------------------")
    print("[Py Hub - Web] Iniciando servidor de DESARROLLO Flask...")
    print("ADVERTENCIA: Este es un servidor solo para pruebas.")
    print(f"            Para producción, usa: gunicorn --bind 127.0.0.1:8000 app:app")
    print("---------------------------------------------------------")
    
    # Iniciar el servidor de desarrollo de Flask (para pruebas)
    # debug=True es útil para ver errores, pero NUNCA lo uses en producción
    app.run(host='0.0.0.0', port=8080, debug=True)
