#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include <time.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>

// --- CONFIGURACIÓN CON #define (CORREGIDO) ---
#define SERVER_PORT 9090      // Puerto donde escucha el Hub de Python
#define SERVER_IP "127.0.0.1" // localhost

#define NUM_CHANNELS 8
#define SAMPLES_PER_CHANNEL 1000
#define TOTAL_POINTS (NUM_CHANNELS * SAMPLES_PER_CHANNEL) 
#define LOOP_DELAY_US 50000 

// Buffer de datos global (ahora válido)
char data_buffer[TOTAL_POINTS * 15];


int connect_to_server() {
    int sock_fd;
    struct sockaddr_in serv_addr;

    printf("[C Client] Intentando conectar a %s:%d...\n", SERVER_IP, SERVER_PORT);

    if ((sock_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Error al crear socket");
        return -1;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(SERVER_PORT);

    if(inet_pton(AF_INET, SERVER_IP, &serv_addr.sin_addr) <= 0) {
        perror("Dirección IP inválida");
        return -1;
    }

    // Bucle de reconexión
    while (connect(sock_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Error de conexión");
        printf("[C Client] Reintentando en 2 segundos...\n");
        sleep(2);
    }

    printf("[C Client] ¡Conectado al Hub de Python!\n");
    return sock_fd;
}


int main() {
    int sock_fd = -1;
    static double global_time = 0;

    srand(time(NULL));

    while (1) {
        if (sock_fd < 0) {
            sock_fd = connect_to_server();
            if (sock_fd < 0) {
                printf("[C Client] Falló la conexión, saliendo.\n");
                return 1;
            }
        }

        int offset = 0;
        for (int s = 0; s < SAMPLES_PER_CHANNEL; s++) {
            for (int c = 0; c < NUM_CHANNELS; c++) {
                double base_signal = sin(global_time * (c + 1) * 0.1 + s / 20.0);
                double noise = ((double)rand() / RAND_MAX - 0.5) * 0.2;
                double final_val = base_signal + noise;

                offset += sprintf(data_buffer + offset, "%.4f,", final_val);
            }
        }
        
        data_buffer[offset - 1] = '\n'; // Reemplaza la última coma
        data_buffer[offset] = '\0';

        if (send(sock_fd, data_buffer, offset, 0) < 0) {
            perror("Error al enviar (send)");
            printf("[C Client] Hub desconectado. Intentando reconectar...\n");
            close(sock_fd);
            sock_fd = -1;
        }
        
        global_time += 0.1;
        usleep(LOOP_DELAY_US);
    }

    close(sock_fd);
    return 0;
}
```eof

### 2. El "Hub" de Python (Ajustado para Flask)

Este es el cambio más grande. Ya no usamos CherryPy. Usamos **Flask** (para definir las rutas) y **threading** (para correr el servidor TCP que escucha a C). Gunicorn será el *motor* que ejecute este archivo.

Guarda esto como `app.py` (Gunicorn busca `app` por defecto).

```python:app.py
import os
import socket
import threading
from flask import Flask, send_from_directory, Response

# --- CONFIGURACIÓN ---
DATA_PORT = 9090        # Puerto donde escuchamos al cliente C
DATA_HOST = '127.0.0.1' # Escuchar solo en localhost para el cliente C
DIRECTORY = os.getcwd() # Directorio del proyecto

# Variable global para guardar la última línea de datos recibida
LATEST_DATA_LINE = "No data received yet."
# "Lock" para acceso seguro a la variable desde múltiples hilos
DATA_LOCK = threading.Lock()

# 1. Crear la aplicación Flask
app = Flask(__name__)

# --- Lógica del Servidor TCP (para hablar con C) ---

def run_tcp_server_thread():
    """
    Este hilo corre el SERVIDOR TCP para escuchar al cliente C.
    """
    global LATEST_DATA_LINE, DATA_LOCK
    
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        s.bind((DATA_HOST, DATA_PORT))
        s.listen()
        print(f"[Py Hub - TCP] Escuchando datos de C en {DATA_HOST}:{DATA_PORT}...")
        
        while True: # Bucle para aceptar conexiones (si C se reinicia)
            conn, addr = s.accept()
            print(f"[Py Hub - TCP] Cliente C conectado desde {addr}")
            with conn:
                f = conn.makefile('r')
                while True:
                    line = f.readline()
                    if not line:
                        print("[Py Hub - TCP] Cliente C desconectado.")
                        break # Salir del bucle de lectura
                    
                    with DATA_LOCK:
                        LATEST_DATA_LINE = line

# --- Rutas del Servidor Web (Flask) ---

@app.route('/')
def index():
    """
    Sirve el archivo webgl_graph.html.
    Nginx normalmente hará esto, pero es bueno tenerlo como respaldo.
    """
    return send_from_directory(DIRECTORY, 'webgl_graph.html')

@app.route('/data')
def data():
    """
    Este es el endpoint que el navegador llama para obtener los datos.
    """
    global LATEST_DATA_LINE, DATA_LOCK
    
    with DATA_LOCK:
        rsp = LATEST_DATA_LINE
    
    # Devolver los datos como texto plano
    return Response(rsp, mimetype='text/plain')

# --- Punto de Entrada ---
if __name__ == '__main__':
    # Esto es solo para pruebas. En producción, Gunicorn iniciará 'app'.
    
    # 1. Iniciar el Hilo del Servidor TCP
    tcp_thread = threading.Thread(target=run_tcp_server_thread, daemon=True)
    tcp_thread.start()
    
    # 2. Iniciar el servidor de desarrollo de Flask
    print("[Py Hub - Web] Iniciando servidor de DESARROLLO Flask en http://0.0.0.0:8080")
    print("ADVERTENCIA: Este servidor es solo para pruebas. Usa Gunicorn en producción.")
    app.run(host='0.0.0.0', port=8080, debug=True)
```eof

### 3. El Cliente Web HTML (Sin cambios)

Este archivo (`webgl_graph.html`) es **exactamente el mismo** que te di en la respuesta anterior. No necesita ningún cambio, ya que el *endpoint* al que llama (`/data`) y el formato de los datos siguen siendo los mismos.

```html:webgl_graph.html
<!doctype html>
<html>
   <body onload=start_graph()>
      <div id="canvas-container"></div>
      <button id="single_btn"   onclick="run_single(this)">Single</button>
      <button id="run_stop_btn" onclick="run_stop(this)"  >Run</button>
      <select id="sel_nchans" onchange="sel_nchans()"></select>
      <select id="sel_srce"   onchange="sel_srce()"  ></select>
      <pre id="status" style="font-size: 14px; margin: 8px"></pre>
      
      <style>
        body { font-family: sans-serif; background-color: #333; color: #eee; }
        canvas { background-color: #000; margin-bottom: 5px; width: 95%; }
        button, select { font-size: 16px; margin: 5px; }
      </style>

      <script>
        // --- CONSTANTES ---
        const WEBGL2 = true;
        const NORM_XMIN=-1.0, NORM_XMAX=1.0, NORM_YMIN=-1.0, NORM_YMAX=1.0;
        const XMARGIN=20, YMARGIN=90, MIN_CHANS=1, MAX_CHANS=16;
        const NCHANS = 8;
        
        var data_src = "data"; // El endpoint de Flask
        // ... (El resto del script es idéntico) ...
        var sim_srce = "sim"; 
        var gl; 
        var canvas; 
        var clear_colour = [0.1, 0.1, 0.1, 1.0]; 
        var trace_colours = [[0.6, 0.7, 0.6, 1],
            colr(0x00FF00), colr(0xFF0000), colr(0x0000FF), colr(0xFFFF00),
            colr(0x00FFFF), colr(0xFF00FF), colr(0xFF8000), colr(0xFFFFFF),
            colr(0x969696), colr(0xffffcc), colr(0x000000), colr(0x800000),
            colr(0xff0000), colr(0xff9900), colr(0xffff00), colr(0x00ff00)];
        var program, running=false, num_chans = NCHANS;
        var grid_vertices = []; 
        var trace_vertices = []; 
        var all_canvases = []; 
        var all_gl_contexts = []; 
        var frag_code, vert_code; 

         if (WEBGL2)
            frag_code = `#version 300 es
                precision mediump float;
                in vec4 v_colour;
                out vec4 o_colour;
                void main() {
                    o_colour = v_colour;
                }`;
        else
            frag_code = `
                precision mediump float;
                varying vec4 v_colour;
                void main(void) {
                    gl_FragColor = v_colour;
                }`;
         if (WEBGL2)
            vert_code = `#version 300 es
                in vec3 a_coords;
                out vec4 v_colour;
                `;
        else
            vert_code = `
                attribute vec3 a_coords;
                varying vec4 v_colour;
                `;
        vert_code +=
           `#define MAX_CHANS ${MAX_CHANS}
            uniform vec4 u_colours[MAX_CHANS];
            void main(void) {
               int zint = int(a_coords.z);
               gl_Position = vec4(a_coords.x, a_coords.y, 0, 1);
               v_colour = u_colours[zint];
            }`;

        function start_graph() {
            var container = elem("canvas-container");
            for (var i=0; i < NCHANS; i++) {
                var new_canvas = document.createElement('canvas');
                new_canvas.id = "graph_canvas_" + i;
                new_canvas.width = 1000;
                new_canvas.height = 100;
                container.appendChild(new_canvas);
                all_canvases.push(new_canvas);
            }
            canvas = all_canvases[0];
            
            var sel = document.getElementById("sel_nchans");
            for (var n=MIN_CHANS; n<=MAX_CHANS; n++)
                sel.options.add(new Option(n+" channel"+(n>1?"s":""), value=n));
            sel.selectedIndex = NCHANS-1;
            
            var sel = document.getElementById("sel_srce");
            sel.options.add(new Option("from Pi (Live)", value=data_src));
            sel_srce();
            
            try {
                for (var i=0; i < NCHANS; i++) {
                    init_graph(all_canvases[i]);
                }
            } catch (e) {
                alert("Error: "+e);
            }
            
            gl = all_gl_contexts[0];
            program = gl.program;
            gl.useProgram(program);
            
            var clrs = gl.getUniformLocation(program, 'u_colours');
            gl.uniform4fv(clrs, new Float32Array(trace_colours.flat()));
            draw_grid(grid_vertices, 10, 8, 0);
            
            for (var i=0; i < NCHANS; i++) {
                trace_vertices.push([]);
            }
            window.addEventListener("resize", resize_canvas);
            resize_canvas();
            disp_status(location.host ? "Loaded from "+location.host : "Loaded from filesystem, no data available");
            redraw_graph();
        }

        function init_graph(canvas_elem) {
            var local_gl = canvas_elem.getContext(WEBGL2 ? 'webgl2' : 'experimental-webgl');
            if (!local_gl) { throw "WebGL not supported"; }
            var vertex_buffer = local_gl.createBuffer();
            local_gl.bindBuffer(local_gl.ARRAY_BUFFER, vertex_buffer);
            var frag_shader = compile_shader(local_gl, local_gl.FRAGMENT_SHADER, frag_code);
            var vert_shader = compile_shader(local_gl, local_gl.VERTEX_SHADER, vert_code);
            var local_program = local_gl.createProgram();
            local_gl.attachShader(local_program, vert_shader);
            local_gl.attachShader(local_program, frag_shader);
            local_gl.linkProgram(local_program);
            local_gl.useProgram(local_program);
            local_gl.bindBuffer(local_gl.ARRAY_BUFFER, vertex_buffer);
            var coord = local_gl.getAttribLocation(local_program, "a_coords");
            local_gl.vertexAttribPointer(coord, 3, local_gl.FLOAT, false, 0, 0);
            local_gl.enableVertexAttribArray(coord);
            local_gl.program = local_program;
            all_gl_contexts.push(local_gl);
        }

        function compile_shader(gl_ctx, typ, source) {
            var s = gl_ctx.createShader(typ);
            gl_ctx.shaderSource(s, source);
            gl_ctx.compileShader(s);
            if (!gl_ctx.getShaderParameter(s, gl_ctx.COMPILE_STATUS))
                throw "Could not compile " + (typ==gl_ctx.VERTEX_SHADER ? "vertex" : "fragment") + " shader:\n\n"+gl_ctx.getShaderInfoLog(s);
            return(s);
        }

        function set_point(vts, x, y, z) { vts.push(x, y, z); }
        function draw_line(vts, x1, y1, x2, y2, z) { vts.push(x1, y1, z, x2, y2, z); }

        function draw_traces(vals) {
            var x, y, zval, np = vals.length / num_chans;
            for (var chan=0; chan<num_chans; chan++) {
                trace_vertices[chan] = [];
                zval = chan + 1;
                var vts = trace_vertices[chan];
                var ymin=1e9, ymax=-1e9;
                for (var n=0; n<np; n++) {
                    y = vals[chan + n*num_chans];
                    if (y < ymin) ymin = y;
                    if (y > ymax) ymax = y;
                }
                var yrange = (ymax - ymin) * 1.1; 
                if (yrange < 0.1) yrange = 0.1;
                var ymid = (ymax + ymin) / 2.0;
                for (var n=0; n<np; n++) {
                    if (n > 1)
                        set_point(vts, x, y, zval);
                    x = NORM_XMIN + (NORM_XMAX-NORM_XMIN) * n / (np - 1);
                    y = (vals[chan + n*num_chans] - ymid) / (yrange / 2.0);
                    set_point(vts, x, y, zval);
                }
            }
        }

        function redraw_data() {
            for (var i=0; i < NCHANS; i++) {
                var gl_ctx = all_gl_contexts[i];
                gl_ctx.useProgram(gl_ctx.program);
                var graph_vertices = [];
                graph_vertices.push(...grid_vertices);
                if (trace_vertices[i]) {
                    graph_vertices.push(...trace_vertices[i]);
                }
                gl_ctx.bufferData(gl_ctx.ARRAY_BUFFER, new Float32Array(graph_vertices), gl_ctx.STATIC_DRAW);
            }
        }

        function redraw_graph() {
            for (var i=0; i < NCHANS; i++) {
                var gl_ctx = all_gl_contexts[i];
                var canvas_elem = all_canvases[i];
                gl_ctx.viewport(0, 0, canvas_elem.width, canvas_elem.height);
                gl_ctx.clearColor(...clear_colour);
                gl_ctx.clear(gl_ctx.COLOR_BUFFER_BIT);
                var num_grid_verts = grid_vertices.length / 3;
                var num_trace_verts = trace_vertices[i] ? trace_vertices[i].length / 3 : 0;
                if (num_trace_verts > 0)
                    gl_ctx.drawArrays(gl_ctx.LINES, 0, num_grid_verts + num_trace_verts);
            }
            if (running)
                get_data(data_src)
        }

        function draw_grid(vts, nx, ny, z) {
            for (var i=0; i<=nx; i++) {
                var x = NORM_XMIN + (NORM_XMAX-NORM_XMIN) * i / nx;
                draw_line(vts, x, NORM_YMIN, x, NORM_YMAX, z);
            }
            for (var i=0; i<=ny; i++) {
                var y = NORM_YMIN + (NORM_YMAX-NORM_YMIN) * i / ny;
                draw_line(vts, NORM_XMIN, y, NORM_XMAX, y, z);
            }
        }

        function get_data(fname) {
            fetch(fname).then((response) => { return response.ok ? response.text() : "" })
            .then(data => {
                var vals = csv_decode(data);
                if (vals.length > 0) {
                    disp_status(vals.length+" samples (" + vals.length / num_chans + " per chan)");
                    draw_traces(vals); 
                } else {
                    disp_status("No data");
                }
                redraw_data();
                redraw_graph();
            })
            .catch(error => {
                disp_status("Can't load data: " + error);
                if (running) { setTimeout(() => get_data(data_src), 500); }
            });
        }

        function run_single(btn) {
            if (running) run_stop(elem('run_stop_btn'));
            get_data(data_src);
        }
        function run_stop(btn) {
            running = !running;
            btn.innerText = running ? "Stop" : "Run";
            if (running) window.requestAnimationFrame(redraw_graph);
        }

        function disp_status(s) { elem('status').innerHTML = s.trim(); }

        function resize_canvas() {
            var w = window.innerWidth - XMARGIN;
            var h = (window.innerHeight - YMARGIN - (NCHANS * 5)) / NCHANS;
            if (h < 50) h = 50;
            for (var i=0; i < NCHANS; i++) {
                all_canvases[i].width  = w;
                all_canvases[i].height = h;
            }
            redraw_graph();
        }

        function sel_nchans() {
            var sel = document.getElementById("sel_nchans");
            var new_n = sel.options[sel.selectedIndex].value;
            if (new_n != num_chans) {
                alert("This demo is locked to " + NCHANS + " channels.");
                sel.selectedIndex = NCHANS - 1;
            }
        }

        function sel_srce() {
            var sel = elem("sel_srce");
            data_src = sel.options[sel.selectedIndex].value;
        }

        function csv_decode(s) {
            data = s.trim().split(',');
            if (data[data.length-1] == "") { data.pop(); }
            return data.map(x => parseFloat(x));
        }

        function colr(x) { return([(x>>16&255)/255.0, (x>>8&255)/255.0, (x&255)/255.0, 1.0]); }
        function elem(id) { return document.getElementById(id); }
      </script>
   </body>
</html>
```eof

---

### Cómo Agregar Nginx (Instalación y Configuración)

Esta es la parte nueva. Sigue estos pasos en tu consola de Raspberry Pi para instalar y configurar Nginx como un "proxy inverso".

**Paso 1: Instalar Nginx y Gunicorn**

Primero, instala el software necesario.
```bash
# Actualizar la lista de paquetes
sudo apt-get update

# Instalar Nginx (el servidor web) y Gunicorn (el motor de Flask)
sudo apt-get install nginx gunicorn
