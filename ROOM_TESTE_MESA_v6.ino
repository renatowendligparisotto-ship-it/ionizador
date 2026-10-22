#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <Firebase_ESP_Client.h>
#include <WiFiClientSecure.h>
#include <HTTPUpdate.h>
#include "esp_mac.h"
#include "credentials.h" 

#define PINO_RELE 2
#define PINO_BOTAO 0 

// =========================================================================
// ATENÇÃO PARA OS PRÓXIMOS FIRMWARES: ALTERAR AS DUAS LINHAS ABAIXO
// =========================================================================
#define VERSAO_DESTA_PLACA "TESTE_MESA_v6"   // Ex: Mude para "v1" nos novos
#define MODELO_HARDWARE "ESP32_WROOM"        // Ex: Mude para "ESP32_S3" ou "ESP32_S3_ZERO" nos outros códigos
// =========================================================================

WebServer server(80);
DNSServer dnsServer;
Preferences memory;

const byte DNS_PORT = 53;
const char* AP_SSID = "TTRW_Config";
const char* AP_PASS = "12345678"; 

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

String deviceID;
String basePath;
bool firebaseIniciado = false;

// VARIÁVEIS GLOBAIS DE ESTADO (Partilhadas entre os Cores)
volatile bool estadoBomba = false; 
volatile int temperaturaAlvo = 28; 
volatile bool alteracaoLocal = false; 

TaskHandle_t TaskRede_Handle = NULL;
TaskHandle_t TaskHardware_Handle = NULL;

String htmlPaginaCompleta = "";

// ==========================================
// HTML DO PAINEL PRINCIPAL (CONTROLO LOCAL + TEMPERATURA)
// ==========================================
String getPainelHTML() {
  String html = "<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">";
  html += "<title>Painel da Chácara</title><style>";
  html += "body { font-family: Arial, sans-serif; background-color: #121212; color: #ffffff; text-align: center; margin: 0; padding: 20px; }";
  html += ".caixa { background-color: #1e1e1e; max-width: 350px; margin: 40px auto; padding: 30px; border-radius: 10px; box-shadow: 0 4px 10px rgba(0,0,0,0.5); }";
  
  html += ".btn-rele { padding: 20px; border: none; border-radius: 8px; cursor: pointer; width: 100%; font-size: 20px; font-weight: bold; margin-bottom: 20px; transition: 0.3s; }";
  html += ".btn-rele.on { background-color: #04d361; color: white; box-shadow: 0 0 15px #04d361; }";
  html += ".btn-rele.off { background-color: #ff3b30; color: white; }";
  
  html += ".temp-box { background-color: #29292e; padding: 20px; border-radius: 8px; margin-bottom: 20px; }";
  html += ".temp-box h3 { margin: 0 0 15px 0; font-size: 16px; color: #a8a8b3; text-transform: uppercase; }";
  html += ".temp-controls { display: flex; justify-content: space-between; align-items: center; }";
  html += ".btn-temp { background-color: #444; color: white; border: none; border-radius: 5px; width: 50px; height: 50px; font-size: 24px; cursor: pointer; display: flex; justify-content: center; align-items: center; transition: 0.2s; }";
  html += ".btn-temp:hover { background-color: #555; }";
  html += ".temp-val { font-size: 36px; font-weight: bold; color: #008CBA; }";
  
  html += ".btn-wifi { background-color: #008CBA; color: white; padding: 15px; border: none; border-radius: 8px; cursor: pointer; width: 100%; font-size: 16px; margin-top: 10px; font-weight: bold; }";
  html += ".status { font-size: 14px; color: #a8a8b3; margin-bottom: 25px; }";
  html += "</style></head><body><div class=\"caixa\">";
  
  html += "<h2>Controlo Local</h2>";
  html += "<p class=\"status\">Status: " + String(WiFi.status() == WL_CONNECTED ? "Nuvem Conectada ☁️" : "Modo Offline 📵") + "</p>";
  
  if (estadoBomba) {
    html += "<button class=\"btn-rele on\" onclick=\"window.location.href='/toggle'\">BOMBA: LIGADA</button>";
  } else {
    html += "<button class=\"btn-rele off\" onclick=\"window.location.href='/toggle'\">BOMBA: DESLIGADA</button>";
  }
  
  html += "<div class=\"temp-box\">";
  html += "<h3>Temperatura Alvo</h3>";
  html += "<div class=\"temp-controls\">";
  html += "<button class=\"btn-temp\" onclick=\"window.location.href='/temp_menos'\">-</button>";
  html += "<span class=\"temp-val\">" + String(temperaturaAlvo) + "°C</span>";
  html += "<button class=\"btn-temp\" onclick=\"window.location.href='/temp_mais'\">+</button>";
  html += "</div></div>";
  
  html += "<hr style=\"border-color: #333; margin: 25px 0;\">";
  html += "<button class=\"btn-wifi\" onclick=\"window.location.href='/config'\">⚙️ Configurações de Wi-Fi</button>";
  html += "</div></body></html>";
  return html;
}

// ==========================================
// HTML DA PÁGINA DE CONFIGURAÇÃO WI-FI
// ==========================================
const char* htmlTopo = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Configuração Wi-Fi</title>
<style>
  body { font-family: Arial, sans-serif; background-color: #121212; color: #ffffff; text-align: center; margin: 0; padding: 20px; }
  .caixa { background-color: #1e1e1e; max-width: 350px; margin: 40px auto; padding: 30px; border-radius: 10px; box-shadow: 0 4px 10px rgba(0,0,0,0.5); }
  input { width: 90%; padding: 12px; margin: 10px 0; border: none; border-radius: 5px; background: #2c2c2c; color: #fff; font-size: 16px; box-sizing: border-box; }
  .linha-rede { display: flex; justify-content: center; align-items: center; width: 90%; margin: 10px auto; gap: 8px; }
  select { width: 100%; padding: 12px; border: none; border-radius: 5px; background: #2c2c2c; color: #fff; font-size: 16px; box-sizing: border-box; margin: 0; }
  .btn-refresh { background-color: #444; color: white; border: none; border-radius: 5px; cursor: pointer; padding: 10px 14px; font-size: 18px; display: flex; align-items: center; }
  .btn-salvar { background-color: #008CBA; color: white; padding: 14px 20px; border: none; border-radius: 5px; cursor: pointer; width: 100%; font-size: 18px; margin-top: 15px; font-weight: bold; box-sizing: border-box; }
  .btn-voltar { background-color: #444; color: white; padding: 10px; border: none; border-radius: 5px; cursor: pointer; width: 100%; font-size: 14px; margin-bottom: 20px; }
  .btn-apagar { background-color: #ff3b30; color: white; padding: 12px; border: none; border-radius: 5px; cursor: pointer; width: 100%; font-size: 16px; margin-top: 20px; font-weight: bold; }
</style>
</head><body>
  <div class="caixa">
    <button class="btn-voltar" onclick="window.location.href='/'">⬅️ Voltar ao Painel</button>
    <h2>Vincular Rede</h2>
    <form action="/salvar" method="POST">
      <div class="linha-rede">
        <button type="button" class="btn-refresh" onclick="window.location.href='/atualizar_redes'">🔄</button>
        <select name="ssid" required>
          <option value="" disabled selected>Escolha a rede...</option>
)rawliteral";

const char* htmlFundo = R"rawliteral(
        </select>
      </div>
      <input type="password" name="pass" placeholder="Senha do Wi-Fi" required>
      <button type="submit" class="btn-salvar">Guardar e Reiniciar</button>
    </form>
    <hr style="border-color: #333; margin: 25px 0;">
    <button class="btn-apagar" onclick="if(confirm('Tem a certeza que deseja apagar a rede guardada e reiniciar a placa?')) window.location.href='/apagar_wifi'">🗑️ Apagar Wi-Fi Salvo</button>
  </div>
</body></html>
)rawliteral";

// ==========================================
// FUNÇÃO DE PESQUISA DE REDES
// ==========================================
void atualizarListaDeRedes() {
  Serial.println("\n[WIFI-SCAN] A pesquisar redes...");
  int n = WiFi.scanNetworks();
  
  htmlPaginaCompleta = String(htmlTopo);
  if (n == 0) {
    htmlPaginaCompleta += "<option value=\"\" disabled>Nenhuma rede encontrada</option>";
  } else {
    for (int i = 0; i < n; ++i) {
      htmlPaginaCompleta += "<option value=\"" + WiFi.SSID(i) + "\">" + WiFi.SSID(i) + "</option>";
    }
  }
  htmlPaginaCompleta += String(htmlFundo);
}

// ==========================================
// FUNÇÃO DE ATUALIZAÇÃO OTA (CORE 0)
// ==========================================
void executarOTAGitHub(String urlBinario) {
  Serial.println("\n[OTA] A iniciar transferência via Nuvem...");
  Serial.println("[OTA] Link: " + urlBinario);

  WiFiClientSecure client;
  client.setInsecure(); // Ignora a verificação rígida de certificados SSL (Essencial para GitHub)
  
  // SOLUÇÃO PARA O ERRO HTTP -104 (Segue redirecionamentos do GitHub)
  httpUpdate.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  t_httpUpdate_return ret = httpUpdate.update(client, urlBinario);

  switch (ret) {
    case HTTP_UPDATE_FAILED:
      Serial.printf("[OTA] FALHA (%d): %s\n", httpUpdate.getLastError(), httpUpdate.getLastErrorString().c_str());
      break;
    case HTTP_UPDATE_NO_UPDATES:
      Serial.println("[OTA] Nenhuma atualização disponível.");
      break;
    case HTTP_UPDATE_OK:
      Serial.println("[OTA] SUCESSO! A reiniciar automaticamente com o novo firmware...");
      break;
  }
}

// ==========================================
// CORE 0: REDES (PORTAL CATIVO + FIREBASE + OTA)
// ==========================================
void TaskRede(void * parameter) {
  unsigned long tempoInicio = millis();
  const unsigned long TEMPO_LIMITE_PORTAL = 300000; // 5 minutos
  bool portalAtivo = true;
  unsigned long ultimoMilisFirebase = 0;
  bool wifiAvisado = false; // Controlo de log do Wi-Fi

  for(;;) {
    // 1. GESTÃO DO PORTAL CATIVO
    if (portalAtivo) {
      dnsServer.processNextRequest();
      server.handleClient();

      if (millis() - tempoInicio > TEMPO_LIMITE_PORTAL) {
        if (WiFi.status() == WL_CONNECTED) {
          Serial.println("\n[SISTEMA] WiFi conectado há 5 min. Desligando Ponto de Acesso (AP)...");
          dnsServer.stop();
          server.stop();
          WiFi.softAPdisconnect(true); 
          WiFi.mode(WIFI_STA);         
          portalAtivo = false; 
          Serial.println("[SISTEMA] AP desligado. Core 0 focado apenas no Firebase/OTA.");
        }
      }
    }

    // 2. GESTÃO DO FIREBASE E OTA
    if (WiFi.status() == WL_CONNECTED) {
      if (!wifiAvisado) {
        Serial.println("\n[WIFI] Conectado com sucesso!");
        Serial.print("[WIFI] IP Local: ");
        Serial.println(WiFi.localIP());
        wifiAvisado = true;
      }

      if (!firebaseIniciado) {
        config.api_key = FIREBASE_API_KEY;
        config.database_url = FIREBASE_URL;
        config.signer.test_mode = true; 
        Firebase.begin(&config, &auth);
        Firebase.reconnectWiFi(true);
        firebaseIniciado = true;

        // =========================================================================
        // ADICIONADO: A PLACA REPORTA A SUA VERSÃO E O SEU HARDWARE AO FIREBASE
        // =========================================================================
        Firebase.RTDB.setString(&fbdo, basePath + "/versao_atual", VERSAO_DESTA_PLACA);
        Firebase.RTDB.setString(&fbdo, basePath + "/modelo_hardware", MODELO_HARDWARE);
      }

      // Sync Nuvem -> Local Imediato
      if (alteracaoLocal) {
        Firebase.RTDB.setInt(&fbdo, basePath + "/rele_bomba", estadoBomba ? 1 : 0);
        Firebase.RTDB.setInt(&fbdo, basePath + "/temperatura_alvo", temperaturaAlvo);
        
        alteracaoLocal = false; 
        ultimoMilisFirebase = millis(); 
      } 
      // Leituras de Rotina (Sync Nuvem -> Local)
      else {
        if (millis() - ultimoMilisFirebase > 2000) {
          ultimoMilisFirebase = millis(); 
          
          if (Firebase.RTDB.getInt(&fbdo, basePath + "/rele_bomba")) {
            estadoBomba = fbdo.intData(); 
          }
          if (Firebase.RTDB.getInt(&fbdo, basePath + "/temperatura_alvo")) {
            temperaturaAlvo = fbdo.intData();
          }
          
          // VERIFICAÇÃO OTA 
          if (Firebase.RTDB.getBool(&fbdo, basePath + "/comando_ota")) {
            if (fbdo.boolData() == true) {
              Serial.println("\n[OTA] Comando de atualização detetado!");
              Firebase.RTDB.setBool(&fbdo, basePath + "/comando_ota", false); 
              
              String caminhoHardware = "/config/arquivos_ota/" + String(MODELO_HARDWARE);
              if (Firebase.RTDB.getString(&fbdo, caminhoHardware)) {
                executarOTAGitHub(fbdo.stringData());
              }
            }
          }
        }
      }
    } else {
      wifiAvisado = false;
      firebaseIniciado = false; 
    }

    vTaskDelay(10 / portTICK_PERIOD_MS); 
  }
}

// ==========================================
// CORE 1: EXCLUSIVO PARA O HARDWARE E FÍSICA
// ==========================================
void TaskHardware(void * parameter) {
  uint32_t tempoBotaoPressionado = 0;
  bool estadoFisicoBomba = !estadoBomba; 

  for(;;) {
    // 1. Ler Botão Físico (Reset)
    if (digitalRead(PINO_BOTAO) == LOW) {
      tempoBotaoPressionado++;
      if (tempoBotaoPressionado > 250) {  // AUMENTADO PARA 5 SEGUNDOS PARA EVITAR RESET ACIDENTAL
        Serial.println("\n[SISTEMA] BOTÃO FÍSICO: FORMATANDO MEMORIA WI-FI...");
        memory.begin("wifi_config", false);
        memory.clear();
        memory.end();
        delay(1000);
        ESP.restart();
      }
    } else {
      tempoBotaoPressionado = 0;
    }

    // 2. Operação do Equipamento Físico
    if (estadoBomba != estadoFisicoBomba) {
      estadoFisicoBomba = estadoBomba;
      digitalWrite(PINO_RELE, estadoFisicoBomba);
    }

    vTaskDelay(20 / portTICK_PERIOD_MS); 
  }
}

// ==========================================
// SETUP
// ==========================================
void setup() {
  Serial.begin(115200);
  delay(1000); 

  pinMode(PINO_RELE, OUTPUT);
  pinMode(PINO_BOTAO, INPUT_PULLUP);
  digitalWrite(PINO_RELE, LOW);

  uint8_t efuseMac[6];
  esp_efuse_mac_get_default(efuseMac);
  char efuseStr[13];
  snprintf(efuseStr, 13, "%02X%02X%02X%02X%02X%02X", efuseMac[0], efuseMac[1], efuseMac[2], efuseMac[3], efuseMac[4], efuseMac[5]);
  deviceID = "TTRW01P-" + String(efuseStr).substring(6); 
  basePath = "/chacara/dispositivos/" + deviceID;

  memory.begin("wifi_config", true);
  String ssidSalvo = memory.getString("ssid", "");
  String passSalva = memory.getString("pass", "");
  memory.end();
  
  WiFi.mode(WIFI_AP_STA);
  IPAddress ip(192, 168, 4, 1);
  IPAddress gateway(192, 168, 4, 1);
  IPAddress subnet(255, 255, 255, 0);
  WiFi.softAPConfig(ip, gateway, subnet);
  WiFi.softAP(AP_SSID, AP_PASS); 

  // LÓGICA DE BOOT INTELIGENTE
  if (ssidSalvo != "") {
    Serial.println("\n[SISTEMA] Rede guardada encontrada: " + ssidSalvo);
    Serial.println("[SISTEMA] A iniciar ligação em segundo plano...");
    WiFi.begin(ssidSalvo.c_str(), passSalva.c_str());
    
    // Preenche o HTML base sem travar o boot da placa
    htmlPaginaCompleta = String(htmlTopo) + "<option value=\"\" disabled>Clique no 🔄 para pesquisar redes</option>" + String(htmlFundo);
  } else {
    Serial.println("\n[SISTEMA] Nenhuma rede guardada. A pesquisar redes para o Portal...");
    atualizarListaDeRedes(); // Só pesquisa redes se a memória estiver vazia
  }

  dnsServer.start(DNS_PORT, "*", ip);
  
  // --- ROTAS DO PAINEL LOCAL ---
  server.on("/", HTTP_GET, []() { 
    server.send(200, "text/html", getPainelHTML()); 
  });

  server.on("/toggle", HTTP_GET, []() {
    estadoBomba = !estadoBomba; 
    alteracaoLocal = true; 
    server.sendHeader("Location", "/"); 
    server.send(303);
  });

  server.on("/temp_mais", HTTP_GET, []() {
    temperaturaAlvo++;
    alteracaoLocal = true; 
    server.sendHeader("Location", "/"); 
    server.send(303);
  });

  server.on("/temp_menos", HTTP_GET, []() {
    temperaturaAlvo--;
    alteracaoLocal = true; 
    server.sendHeader("Location", "/"); 
    server.send(303);
  });
  
  // --- ROTAS DA CONFIGURAÇÃO WI-FI ---
  server.on("/config", HTTP_GET, []() {
    server.send(200, "text/html", htmlPaginaCompleta);
  });
  
  server.on("/atualizar_redes", HTTP_GET, []() {
    atualizarListaDeRedes();
    server.send(200, "text/html", htmlPaginaCompleta);
  });
  
  server.on("/salvar", HTTP_POST, []() {
    String novoSSID = server.arg("ssid");
    String novaPass = server.arg("pass");
    
    memory.begin("wifi_config", false);
    memory.putString("ssid", novoSSID);
    memory.putString("pass", novaPass);
    memory.end();
    
    server.send(200, "text/html", "<h2 style='color:white; font-family:sans-serif; text-align:center; padding:50px; background:#121212;'>Credenciais guardadas!<br>O ESP32 vai reiniciar.</h2>");
    delay(2000);
    ESP.restart(); 
  });

  server.on("/apagar_wifi", HTTP_GET, []() {
    memory.begin("wifi_config", false);
    memory.clear();
    memory.end();
    
    server.send(200, "text/html", "<h2 style='color:white; font-family:sans-serif; text-align:center; padding:50px; background:#121212;'>Memória apagada!<br>O ESP32 vai reiniciar e criar a rede de configuração.</h2>");
    delay(2000);
    ESP.restart(); 
  });
  
  server.onNotFound([]() { 
    server.send(200, "text/html", getPainelHTML()); 
  });
  
  server.begin();

  // CORE 0 para Redes/OTA e CORE 1 para Hardware
  xTaskCreatePinnedToCore(TaskRede, "Redes_e_Firebase", 8192, NULL, 1, &TaskRede_Handle, 0);
  xTaskCreatePinnedToCore(TaskHardware, "Operacao_Equipamento", 4096, NULL, 1, &TaskHardware_Handle, 1);
}

void loop() {
  vTaskDelete(NULL); 
}