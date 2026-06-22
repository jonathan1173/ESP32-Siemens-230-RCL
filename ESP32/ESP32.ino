#include <WiFi.h>
#include <WebServer.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// Configuración del Punto de Acceso (Modo AP)
const char* ap_ssid = "Panel_de_Control";
const char* ap_password = "12345678";

// Pines de hardware
const int corteLineaPin = 19;//19 
const int pulsoFocoPin = 21;//21   
const int pinSensorTemperatura = 27;     

OneWire oneWire(pinSensorTemperatura);
DallasTemperature sensors(&oneWire);

WebServer server(80);

bool lineaCortadaState = false;

// Variables globales para el cálculo de métricas reales del bus
unsigned long lecturasExitosas = 0;
unsigned long lecturasFallidas = 0;
unsigned long totalIntentos = 0;
float temperaturaActual = DEVICE_DISCONNECTED_C;
String sensorIDStr = "No Detectado";

// Variables para cálculo de tasas por segundo
unsigned long ultimoTiempoCalculo = 0;
unsigned long lecturasEnUltimoSegundo = 0;
float throughputReal = 0.0;
float anchoBandaReal = 0.0;

// Obtiene la dirección de hardware real del sensor DS18B20
void buscarDireccionSensor() {
  DeviceAddress direccion;
  sensors.begin();
  if (sensors.getAddress(direccion, 0)) {
    sensorIDStr = "";
    for (uint8_t i = 0; i < 8; i++) {
      if (direccion[i] < 16) sensorIDStr += "0";
      sensorIDStr += String(direccion[i], HEX);
    }
    sensorIDStr.toUpperCase();
  } else {
    sensorIDStr = "DESCONECTADO";
  }
}

String paginaHTML() {
  String tempTexto = "---.- °C";
  if (temperaturaActual != DEVICE_DISCONNECTED_C) {
    tempTexto = String(temperaturaActual, 1) + " °C";
  }

  // Cálculos estadísticos reales acumulados
  float berReal = 0.0;
  float disponibilidadReal = 100.0;
  
  if (totalIntentos > 0) {
    berReal = ((float)lecturasFallidas / (float)totalIntentos) * 100.0;
    disponibilidadReal = ((float)lecturasExitosas / (float)totalIntentos) * 100.0;
  }

  String checkedAttr = lineaCortadaState ? "checked" : "";
  String colorInicialTemp = (temperaturaActual == DEVICE_DISCONNECTED_C) ? "var(--industrial-red)" : "var(--text-highlight)";

  String html = "<!DOCTYPE html><html lang='es'><head>";
  html += "<meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<title>Panel de Control Industrial - SCADA V1</title>";
  html += "<style>";
  html += ":root { --panel-bg: #1e222b; --panel-border: #3a3f4d; --screen-bg: #0d1117; --text-main: #d1d5db; --text-highlight: #00ffcc; --industrial-amber: #ffb703; --industrial-red: #ef4444; --industrial-green: #10b981; }";
  html += "* { box-sizing: border-box; margin: 0; padding: 0; }";
  html += "body { background-color: #11141a; color: var(--text-main); font-family: 'Courier New', Courier, monospace; min-height: 100vh; display: flex; flex-direction: column; align-items: center; padding: 24px; }";
  html += ".panel-header { width: 100%; max-width: 1200px; background: linear-gradient(180deg, #2d323f 0%, #1e222b 100%); border: 3px solid var(--panel-border); border-bottom: none; padding: 16px 24px; border-top-left-radius: 12px; border-top-right-radius: 12px; }";
  html += ".panel-header h1 { font-size: 1.3rem; font-weight: bold; color: var(--industrial-amber); letter-spacing: 2px; text-transform: uppercase; }";
  html += ".main-container { display: flex; flex-direction: row; justify-content: center; align-items: flex-start; gap: 20px; width: 100%; max-width: 1200px; background-color: var(--panel-bg); border: 3px solid var(--panel-border); border-bottom-left-radius: 12px; border-bottom-right-radius: 12px; padding: 24px; flex-wrap: wrap; box-shadow: 0 10px 30px rgba(0,0,0,0.5); }";
  
  // CORRECCIÓN: Columnas ajustadas a un mínimo de 280px para pantallas móviles
  html += ".control-column { flex: 1; min-width: 280px; max-width: 400px; width: 100%; display: flex; flex-direction: column; gap: 20px; }";
  html += ".telemetry-column { flex: 2; min-width: 280px; max-width: 760px; width: 100%; display: flex; flex-direction: column; gap: 20px; }";
  
  html += ".industrial-module { background-color: #161a22; width: 100%; padding: 20px; border-radius: 8px; border: 2px solid #2d323f; box-shadow: inset 0 0 10px rgba(0,0,0,0.6); display: flex; flex-direction: column; gap: 16px; }";
  html += ".module-title { font-size: 0.95rem; color: #8f9cae; font-weight: bold; text-transform: uppercase; letter-spacing: 1px; border-bottom: 1px solid #2d323f; padding-bottom: 8px; }";
  html += ".industrial-btn { background: linear-gradient(180deg, #3a4454 0%, #252c38 100%); border: 2px solid #4a5568; color: #ffffff; width: 100%; height: 60px; border-radius: 6px; font-family: 'Courier New'; font-size: 1.1rem; font-weight: bold; text-transform: uppercase; cursor: pointer; box-shadow: 0 4px 0 #1a202c, 0 6px 10px rgba(0,0,0,0.4); display: flex; align-items: center; justify-content: center; }";
  html += ".industrial-btn:active { transform: translateY(3px); box-shadow: 0 1px 0 #1a202c, 0 2px 5px rgba(0,0,0,0.4); }";
  html += ".digital-readout { background-color: var(--screen-bg); border: 2px solid #222630; border-radius: 6px; padding: 20px; text-align: center; box-shadow: inset 0 0 15px rgba(0,0,0,0.8); position: relative; overflow: hidden; }";
  html += ".digital-value { font-size: 2.8rem; font-weight: bold; color: " + colorInicialTemp + "; text-shadow: 0 0 10px rgba(0,255,204,0.2); }";
  
  // CORRECCIÓN: Cuadrícula con minmax de 140px para que colapse de forma responsiva
  html += ".param-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(140px, 1fr)); gap: 12px; }";
  
  html += ".param-card { background-color: var(--screen-bg); border: 1px solid #222630; padding: 12px; border-radius: 6px; display: flex; flex-direction: column; gap: 6px; }";
  html += ".param-label { font-size: 0.75rem; color: #8f9cae; text-transform: uppercase; }";
  
  // CORRECCIÓN: word-break añadido para evitar desbordes con textos largos (como el ID del hardware)
  html += ".param-value { font-size: 1.1rem; font-weight: bold; color: var(--industrial-amber); word-break: break-word; }";
  
  html += ".param-desc { font-size: 0.7rem; color: #5a6578; }";
  html += ".switch-container { display: flex; justify-content: space-between; align-items: center; background-color: var(--screen-bg); padding: 10px 14px; border-radius: 6px; border: 1px solid #222630; margin-top: 4px; }";
  html += ".switch-label { font-size: 0.8rem; font-weight: bold; color: #8f9cae; letter-spacing: 1px; }";
  html += ".switch-hardware { position: relative; display: inline-block; width: 50px; height: 24px; }";
  html += ".switch-hardware input { opacity: 0; width: 0; height: 0; }";
  html += ".slider { position: absolute; cursor: pointer; top: 0; left: 0; right: 0; bottom: 0; background-color: #2d3748; border: 2px solid #4a5568; transition: .2s; border-radius: 12px; box-shadow: inset 0 2px 4px rgba(0,0,0,0.5); }";
  html += ".slider::before { position: absolute; content: \"\"; height: 14px; width: 14px; left: 3px; bottom: 3px; background-color: #a0aec0; transition: .2s; border-radius: 50%; }";
  html += "input:checked+.slider { background-color: #451a1a; border-color: var(--industrial-red); }";
  html += "input:checked+.slider::before { transform: translateX(26px); background-color: var(--industrial-red); }";
  html += "</style></head><body>";

  html += "<header class='panel-header'><h1>PANEL DE CONTROL </h1></header>";
  html += "<div class='main-container'>";
  
  html += "<div class='control-column'>";
  html += "  <section class='industrial-module'><div class='module-title'>CONTROL DE FOCO</div>";
  html += "    <button class='industrial-btn' onclick='pulsarFoco()'>Pulsar</button>";
  html += "  </section>";
  html += "  <section class='industrial-module'><div class='module-title'>SENSOR TEMP (DS18B20)</div>";
  html += "    <div class='digital-readout'><div class='digital-value' id='tempVal'>" + tempTexto + "</div></div>";
  html += "    <div class='switch-container'><span class='switch-label'>CORTAR LÍNEA</span>";
  html += "      <label class='switch-hardware'>";
  html += "        <input type='checkbox' id='corteSwitch' onchange='enviarCorteLinea()' " + checkedAttr + ">";
  html += "        <span class='slider'></span>";
  html += "      </label>";
  html += "    </div>";
  html += "  </section>";
  html += "</div>";

  html += "<div class='telemetry-column'>";
  html += "  <section class='industrial-module'><div class='module-title'>PARÁMETROS OPERATIVOS (DS18B20)</div>";
  html += "    <div class='param-grid'>";
  html += "      <div class='param-card'><span class='param-label'>Rango Absoluto</span><span class='param-value'>-55°C a +125°C</span><span class='param-desc'>Límites físicos seguros del sensor</span></div>";
  html += "      <div class='param-card'><span class='param-label'>Resolución</span><span class='param-value'>12 bits</span><span class='param-desc'>Precisión del dato (cambios min. de 0.0625°C)</span></div>";
  html += "      <div class='param-card'><span class='param-label'>Tiempo Conversión</span><span class='param-value'>750 ms</span><span class='param-desc'>Retardo/latencia para procesar cada lectura</span></div>";
  html += "      <div class='param-card'><span class='param-label'>ID Único Bus</span><span class='param-value' id='sensorIDCard' style='font-size: 0.85rem; color: var(--text-highlight);'>" + sensorIDStr + "</span><span class='param-desc'>Dirección de hardware real leída del bus</span></div>";
  html += "    </div>";
  html += "  </section>";
  html += "  <section class='industrial-module'><div class='module-title'>MÉTRICAS DE COMUNICACIÓN & BUS </div>";
  html += "    <div class='param-grid'>";
  html += "      <div class='param-card'><span class='param-label'>Tasa Error (BER)</span><span class='param-value' id='berVal'>" + String(berReal, 2) + " %</span><span class='param-desc'>Porcentaje de paquetes corruptos por ruido</span></div>";
  html += "      <div class='param-card'><span class='param-label'>Throughput</span><span class='param-value' id='throughputVal'>" + String(throughputReal, 2) + " lect/s</span><span class='param-desc'>Frecuencia real de datos válidos recibidos</span></div>";
  html += "      <div class='param-card'><span class='param-label'>Ancho Banda Efectivo</span><span class='param-value' id='bwVal'>" + String(anchoBandaReal, 0) + " bps</span><span class='param-desc'>Velocidad neta de transmisión en la línea</span></div>";
  html += "      <div class='param-card'><span class='param-label'>Disponibilidad Bus</span><span class='param-value' id='dispVal'>" + String(disponibilidadReal, 1) + " %</span><span class='param-desc'>Tiempo que el bus responde sin colgarse</span></div>";
  html += "    </div>";
  html += "  </section>";
  html += "</div></div>";

  html += "<script>";
  html += "function pulsarFoco() { fetch('/toggle', { method: 'POST' }); }";
  html += "function enviarCorteLinea() {";
  html += "  const val = document.getElementById('corteSwitch').checked ? 1 : 0;";
  html += "  fetch('/corte?status=' + val, { method: 'POST' }).then(() => {";
  html += "    document.getElementById('tempVal').style.color = val ? 'var(--industrial-red)' : 'var(--text-highlight)';";
  html += "  });";
  html += "}";
  html += "function actualizarDatos() {";
  html += "  fetch('/datos').then(res => res.json()).then(data => {";
  html += "    document.getElementById('tempVal').innerText = data.temperatura + ' °C';";
  html += "    document.getElementById('berVal').innerText = data.ber + ' %';";
  html += "    document.getElementById('throughputVal').innerText = data.throughput + ' lect/s';";
  html += "    document.getElementById('bwVal').innerText = data.bw + ' bps';";
  html += "    document.getElementById('dispVal').innerText = data.disp + ' %';";
  html += "    document.getElementById('sensorIDCard').innerText = data.sensorID;";
  html += "    if(data.temperatura === '---.-') { document.getElementById('tempVal').style.color = 'var(--industrial-red)'; }";
  html += "    else { document.getElementById('tempVal').style.color = 'var(--text-highlight)'; }";
  html += "  });";
  html += "}";
  html += "setInterval(actualizarDatos, 2000);";
  html += "</script></body></html>";
  return html;
}

void handleDatos() {
  float berCalc = 0.0;
  float dispCalc = 100.0;
  
  if (totalIntentos > 0) {
    berCalc = ((float)lecturasFallidas / (float)totalIntentos) * 100.0;
    dispCalc = ((float)lecturasExitosas / (float)totalIntentos) * 100.0;
  }

  String tempValor = "---.-";
  if (temperaturaActual != DEVICE_DISCONNECTED_C) {
    tempValor = String(temperaturaActual, 1);
  }
  
  String json = "{";
  json += "\"temperatura\": \"" + tempValor + "\",";
  json += "\"ber\": \"" + String(berCalc, 2) + "\",";
  json += "\"throughput\": \"" + String(throughputReal, 2) + "\",";
  json += "\"bw\": \"" + String(anchoBandaReal, 0) + "\",";
  json += "\"disp\": \"" + String(dispCalc, 1) + "\",";
  json += "\"sensorID\": \"" + sensorIDStr + "\"";
  json += "}";
  
  server.send(200, "application/json", json);
}

void handleToggle() {
  digitalWrite(pulsoFocoPin, HIGH);
  delay(100); 
  digitalWrite(pulsoFocoPin, LOW);
  server.send(200, "application/json", "{\"status\":\"ok\"}");
}

void handleCorte() {
  if (server.hasArg("status")) {
    String status = server.arg("status");
    if (status == "1") {
      lineaCortadaState = true;
      digitalWrite(corteLineaPin, HIGH); 
    } else {
      lineaCortadaState = false;
      digitalWrite(corteLineaPin, LOW);  
      buscarDireccionSensor(); // Re-escanear ID al reconectar
    }
  }
  server.send(200, "application/json", "{\"status\":\"ok\"}");
}

void handleRoot() {
  server.send(200, "text/html", paginaHTML());
}

void setup() {
  Serial.begin(115200);
  
  pinMode(corteLineaPin, OUTPUT);
  pinMode(pulsoFocoPin, OUTPUT);

  digitalWrite(pulsoFocoPin, LOW);
  digitalWrite(corteLineaPin, LOW); 

  buscarDireccionSensor();

  WiFi.mode(WIFI_AP);
  WiFi.softAP(ap_ssid, ap_password);

  Serial.println("\nModo AP Iniciado con éxito.");
  Serial.print("IP SCADA: ");
  Serial.println(WiFi.softAPIP());

  server.on("/", handleRoot);
  server.on("/toggle", HTTP_POST, handleToggle);
  server.on("/corte", HTTP_POST, handleCorte);
  server.on("/datos", HTTP_GET, handleDatos);
  server.begin();
  
  ultimoTiempoCalculo = millis();
}

void loop() {
  server.handleClient();

  // Muestreo cíclico continuo en el loop para calcular métricas reales de bus
  static unsigned long ultimoMuestreoBus = 0;
  if (millis() - ultimoMuestreoBus >= 1000) { // Muestreo cada 1 segundo
    ultimoMuestreoBus = millis();
    totalIntentos++;

    if (!lineaCortadaState) {
      sensors.requestTemperatures();
      float temp = sensors.getTempCByIndex(0);
      
      if (temp != DEVICE_DISCONNECTED_C) {
        temperaturaActual = temp;
        lecturasExitosas++;
        lecturasEnUltimoSegundo++;
      } else {
        temperaturaActual = DEVICE_DISCONNECTED_C;
        lecturasFallidas++;
      }
    } else {
      temperaturaActual = DEVICE_DISCONNECTED_C;
      lecturasFallidas++;
    }
  }

  // Ventana de tiempo (cada 5 segundos) para promediar Throughput y Ancho de banda reales
  if (millis() - ultimoTiempoCalculo >= 5000) {
    unsigned long tiempoTranscurrido = (millis() - ultimoTiempoCalculo) / 1000;
    
    // Lecturas procesadas por segundo de forma matemática
    throughputReal = (float)lecturasEnUltimoSegundo / (float)tiempoTranscurrido;
    
    // Una trama completa exitosa de lectura de temperatura araña aprox. 80 bits en el protocolo 1-wire
    anchoBandaReal = throughputReal * 80.0; 
    
    // Resetear contadores de ventana
    lecturasEnUltimoSegundo = 0;
    ultimoTiempoCalculo = millis();
  }
}