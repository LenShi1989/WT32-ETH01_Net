#include <WiFi.h>
#include <ETH.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>

// 全局變數
WebServer server(80);

// 網路模式
enum NetworkMode
{
  MODE_AP,   // AP模式（初始設定）
  MODE_WIFI, // WiFi模式
  MODE_ETH,  // 乙太網路模式
  MODE_BOTH  // 雙模式（同時啟用）
};

NetworkMode currentMode = MODE_AP;

// 網路配置
String ap_ssid = "WT32-ETH01_Config";
String ap_password = "12345678";
String wifi_ssid = "";
String wifi_password = "";
bool eth_dhcp = true;
String eth_ip = "192.168.1.100";
String eth_gateway = "192.168.1.1";
String eth_subnet = "255.255.255.0";
String eth_dns1 = "8.8.8.8";
String eth_dns2 = "8.8.4.4";

// 網路狀態
bool wifi_connected = false;
bool eth_connected = false;
String wifi_ip = "";
String eth_ip_current = "";

// 設定檔案路徑
const char *CONFIG_FILE = "/config.json";

// 函數聲明
void startAPMode();
void setupWebServer();
void handleScanWifi();
void handleConnectWifi();
void handleSetNetworkMode();
void handleEthConfig();
void handleNetworkStatus();
void handleReboot();
void handleSaveConfig();
void handleLoadConfig();
void handleFileList();
void saveConfigToSPIFFS();
bool loadConfigFromSPIFFS();
void connectToWiFi();
void setupEthernet();
void WiFiEvent(WiFiEvent_t event);
void listSPIFFSFiles();
String getEncryptionName(int encryptionType);

void setup()
{
  Serial.begin(115200);
  Serial.println("\n\n=== WT32-ETH01 網路配置系統 ===");

  // 初始化SPIFFS
  if (!SPIFFS.begin(true))
  {
    Serial.println("SPIFFS 初始化失敗");
    // 嘗試格式化
    Serial.println("嘗試格式化SPIFFS...");
    SPIFFS.format();
    if (!SPIFFS.begin(true))
    {
      Serial.println("SPIFFS 格式化失敗");
    }
    else
    {
      Serial.println("SPIFFS 格式化成功");
    }
  }
  else
  {
    Serial.println("SPIFFS 初始化成功");
  }

  // 列出SPIFFS中的檔案
  listSPIFFSFiles();

  // 載入配置
  if (!loadConfigFromSPIFFS())
  {
    Serial.println("使用預設設定");
    // 如果載入失敗，使用預設值
    currentMode = MODE_AP;
    eth_dhcp = true;
    eth_ip = "192.168.1.100";
    eth_gateway = "192.168.1.1";
    eth_subnet = "255.255.255.0";

    // 儲存預設設定
    saveConfigToSPIFFS();
  }

  // 設置網路事件處理
  WiFi.onEvent(WiFiEvent);

  // 根據配置啟動網路
  switch (currentMode)
  {
  case MODE_AP:
    Serial.println("啟動AP模式進行初始設定");
    startAPMode();
    break;

  case MODE_WIFI:
    Serial.println("啟動WiFi模式");
    connectToWiFi();
    break;

  case MODE_ETH:
    Serial.println("啟動乙太網路模式");
    setupEthernet();
    break;

  case MODE_BOTH:
    Serial.println("啟動雙網路模式");
    setupEthernet();
    connectToWiFi();
    break;
  }

  // 設置Web服務器
  setupWebServer();

  Serial.println("系統啟動完成");
}

void loop()
{
  server.handleClient();

  // 監控網路狀態（移除定期掃描）
  static unsigned long lastCheck = 0;
  if (millis() - lastCheck > 5000)
  {
    if (currentMode == MODE_WIFI || currentMode == MODE_BOTH)
    {
      wifi_connected = (WiFi.status() == WL_CONNECTED);
      if (wifi_connected)
      {
        wifi_ip = WiFi.localIP().toString();
      }
    }

    if (currentMode == MODE_ETH || currentMode == MODE_BOTH)
    {
      eth_connected = (ETH.localIP().toString() != "0.0.0.0");
      if (eth_connected)
      {
        eth_ip_current = ETH.localIP().toString();
      }
    }

    lastCheck = millis();
  }
}

String getEncryptionName(int encryptionType)
{
  switch (encryptionType)
  {
  case WIFI_AUTH_OPEN:
    return "開放";
  case WIFI_AUTH_WEP:
    return "WEP";
  case WIFI_AUTH_WPA_PSK:
    return "WPA-PSK";
  case WIFI_AUTH_WPA2_PSK:
    return "WPA2-PSK";
  case WIFI_AUTH_WPA_WPA2_PSK:
    return "WPA/WPA2-PSK";
  case WIFI_AUTH_WPA2_ENTERPRISE:
    return "WPA2-Enterprise";
  case WIFI_AUTH_WPA3_PSK:
    return "WPA3-PSK";
  case WIFI_AUTH_WPA2_WPA3_PSK:
    return "WPA2/WPA3-PSK";
  default:
    return "未知";
  }
}

void listSPIFFSFiles()
{
  Serial.println("=== SPIFFS 檔案列表 ===");

  File root = SPIFFS.open("/");
  if (!root)
  {
    Serial.println("無法開啟根目錄");
    return;
  }

  File file = root.openNextFile();
  while (file)
  {
    Serial.print("檔案: ");
    Serial.print(file.name());
    Serial.print(" | 大小: ");
    Serial.print(file.size());
    Serial.println(" bytes");

    file = root.openNextFile();
  }

  root.close();
  Serial.println("=== 檔案列表結束 ===");
}

void startAPMode()
{
  Serial.println("設置AP模式...");

  // 斷開所有網路連接
  WiFi.disconnect(true);
  delay(100);

  // 設置AP+STA模式
  WiFi.mode(WIFI_AP_STA);

  // 啟動AP
  WiFi.softAP(ap_ssid.c_str(), ap_password.c_str());

  // 配置AP的IP
  IPAddress apIP(192, 168, 4, 1);
  IPAddress gateway(192, 168, 4, 1);
  IPAddress subnet(255, 255, 255, 0);
  WiFi.softAPConfig(apIP, gateway, subnet);

  Serial.print("AP已啟動，SSID: ");
  Serial.println(ap_ssid);
  Serial.print("AP IP地址: ");
  Serial.println(WiFi.softAPIP().toString());
  Serial.print("AP密碼: ");
  Serial.println(ap_password);
}

void connectToWiFi()
{
  if (wifi_ssid.length() == 0)
  {
    Serial.println("未設置WiFi SSID，跳過連接");
    return;
  }

  Serial.print("嘗試連接WiFi: ");
  Serial.println(wifi_ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(wifi_ssid.c_str(), wifi_password.c_str());

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30)
  {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    wifi_connected = true;
    wifi_ip = WiFi.localIP().toString();
    Serial.println("\nWiFi連接成功!");
    Serial.print("IP地址: ");
    Serial.println(wifi_ip);
  }
  else
  {
    Serial.println("\nWiFi連接失敗");
  }
}

void setupEthernet()
{
  Serial.println("初始化乙太網路...");
  ETH.begin();

  if (!eth_dhcp && eth_ip.length() > 0)
  {
    IPAddress ip, gateway, subnet, dns1, dns2;

    if (ip.fromString(eth_ip.c_str()) &&
        gateway.fromString(eth_gateway.c_str()) &&
        subnet.fromString(eth_subnet.c_str()))
    {
      // 嘗試解析 DNS 地址
      bool hasDNS1 = dns1.fromString(eth_dns1.c_str());
      bool hasDNS2 = dns2.fromString(eth_dns2.c_str());

      if (hasDNS1 && hasDNS2)
      {
        ETH.config(ip, gateway, subnet, dns1, dns2);
        Serial.println("設置靜態IP配置（含DNS）");
      }
      else if (hasDNS1)
      {
        ETH.config(ip, gateway, subnet, dns1);
        Serial.println("設置靜態IP配置（含主要DNS）");
      }
      else
      {
        ETH.config(ip, gateway, subnet);
        Serial.println("設置靜態IP配置");
      }
    }
  }
  else
  {
    Serial.println("使用DHCP模式");
  }
}

void setupWebServer()
{
  // 靜態文件服務
  server.serveStatic("/", SPIFFS, "/index.html");
  // server.serveStatic("/css", SPIFFS, "/css/");
  // server.serveStatic("/js", SPIFFS, "/js/");
  // server.serveStatic("/images", SPIFFS, "/images/");
  // server.serveStatic("/favicon.ico", SPIFFS, "/favicon.ico");

  server.enableCORS(true);
  server.enableCrossOrigin(true);

  // ============ 新增 OPTIONS 處理 ============
  server.on("/api/scan", HTTP_OPTIONS, []()
            {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
    server.send(200); });
  // ==========================================

  // API端點
  server.on("/api/scan", HTTP_GET, handleScanWifi);
  server.on("/api/connect", HTTP_POST, handleConnectWifi);
  server.on("/api/mode", HTTP_POST, handleSetNetworkMode);
  server.on("/api/eth/config", HTTP_POST, handleEthConfig);
  server.on("/api/network/status", HTTP_GET, handleNetworkStatus);
  server.on("/api/reboot", HTTP_POST, handleReboot);
  server.on("/api/config/save", HTTP_POST, handleSaveConfig);
  server.on("/api/config/load", HTTP_GET, handleLoadConfig);
  server.on("/api/files/list", HTTP_GET, handleFileList);

  server.on("/api/connect", HTTP_OPTIONS, []()
            {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
    server.send(200); });

  // 處理根路徑 - 自動重定向到index.html
  server.on("/", HTTP_GET, []()
            {
    if (SPIFFS.exists("/index.html")) {
      server.sendHeader("Location", "/index.html", true);
      server.send(302, "text/plain", "");
    } else {
      // 如果index.html不存在，顯示簡易資訊
      String message = "WT32-ETH01 網路配置系統\n\n";
      message += "目前模式: ";
      switch(currentMode) {
        case MODE_AP: message += "AP模式"; break;
        case MODE_WIFI: message += "WiFi模式"; break;
        case MODE_ETH: message += "乙太網路模式"; break;
        case MODE_BOTH: message += "雙網路模式"; break;
      }
      message += "\n\n請上傳index.html到SPIFFS以使用完整功能\n";
      message += "可用API:\n";
      message += "GET /api/scan - 掃描WiFi網路\n";
      message += "POST /api/connect - 連接WiFi\n";
      message += "POST /api/mode - 設置網路模式\n";
      message += "POST /api/eth/config - 設置乙太網路\n";
      message += "GET /api/network/status - 網路狀態\n";
      message += "POST /api/reboot - 重啟系統\n";
      message += "GET /api/files/list - 列出檔案\n";
      server.send(200, "text/plain", message);
    } });

  // 處理未知路徑 - 返回404或重定向
  server.onNotFound([]()
                    {
    if (SPIFFS.exists("/index.html")) {
      server.sendHeader("Location", "/index.html", true);
      server.send(302, "text/plain", "");
    } else {
      server.send(404, "text/plain", "404: Not Found");
    } });

  server.begin();

  Serial.print("Web服務器啟動在端口 ");
  Serial.println(80);

  if (currentMode == MODE_AP)
  {
    Serial.print("請連接WiFi: ");
    Serial.print(ap_ssid);
    Serial.print(" 密碼: ");
    Serial.println(ap_password);
    Serial.print("然後瀏覽器訪問: http://");
    Serial.println(WiFi.softAPIP().toString());
  }
  else if (wifi_connected)
  {
    Serial.print("WiFi IP: ");
    Serial.println(wifi_ip);
    Serial.print("訪問地址: http://");
    Serial.println(wifi_ip);
  }
  else if (eth_connected)
  {
    Serial.print("乙太網路 IP: ");
    Serial.println(eth_ip_current);
    Serial.print("訪問地址: http://");
    Serial.println(eth_ip_current);
  }
}

void handleScanWifi()
{
  Serial.println("收到WiFi掃描請求，開始掃描...");

  // 添加CORS標頭，允許跨來源請求
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
  // ====================================

  // 保存當前狀態
  wl_status_t originalStatus = WiFi.status();
  String originalSSID = WiFi.SSID();

  // 設置為AP+STA模式進行掃描
  WiFi.mode(WIFI_AP_STA);
  WiFi.disconnect();
  delay(100);

  // 開始掃描
  int n = WiFi.scanNetworks();
  Serial.printf("掃描到 %d 個WiFi網路\n", n);

  DynamicJsonDocument doc(4096);
  JsonArray networks = doc.to<JsonArray>();

  if (n == 0)
  {
    Serial.println("沒有發現任何WiFi網路");
  }
  else
  {
    for (int i = 0; i < n; i++)
    {
      JsonObject network = networks.createNestedObject();
      String ssid = WiFi.SSID(i);
      int rssi = WiFi.RSSI(i);
      int channel = WiFi.channel(i);
      int encryption = WiFi.encryptionType(i);

      network["ssid"] = ssid;
      network["rssi"] = rssi;
      network["channel"] = channel;
      network["encryption"] = encryption;
      network["encryption_name"] = getEncryptionName(encryption);

      // 計算訊號強度等級 (0-4)
      int signalLevel = 0;
      if (rssi > -50)
        signalLevel = 4;
      else if (rssi > -60)
        signalLevel = 3;
      else if (rssi > -70)
        signalLevel = 2;
      else if (rssi > -80)
        signalLevel = 1;
      network["signal_level"] = signalLevel;

      Serial.printf("  %d. %s (訊號: %d dBm, 通道: %d, 加密: %s)\n",
                    i + 1, ssid.c_str(), rssi, channel, getEncryptionName(encryption).c_str());
    }

    // 釋放掃描結果
    WiFi.scanDelete();
  }

  // 恢復原來的網路模式
  if (currentMode == MODE_AP)
  {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(ap_ssid.c_str(), ap_password.c_str());
  }
  else if (originalStatus == WL_CONNECTED && originalSSID.length() > 0)
  {
    // 如果原本有WiFi連接，重新連接
    WiFi.begin(wifi_ssid.c_str(), wifi_password.c_str());
  }

  String jsonResponse;
  serializeJson(doc, jsonResponse);
  server.send(200, "application/json", jsonResponse);

  Serial.println("掃描結果已發送");
}

void handleConnectWifi()
{
  if (server.hasArg("plain"))
  {
    DynamicJsonDocument doc(256);
    DeserializationError error = deserializeJson(doc, server.arg("plain"));

    if (error)
    {
      server.send(400, "application/json", "{\"success\": false, \"message\": \"JSON解析錯誤\"}");
      return;
    }

    const char *ssid = doc["ssid"];
    const char *password = doc["password"];

    if (ssid == nullptr || strlen(ssid) == 0)
    {
      server.send(400, "application/json", "{\"success\": false, \"message\": \"SSID不能為空\"}");
      return;
    }

    wifi_ssid = ssid;
    wifi_password = password ? password : "";

    // 保存配置
    saveConfigToSPIFFS();

    // 嘗試連接
    WiFi.begin(ssid, password);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30)
    {
      delay(500);
      Serial.print(".");
      attempts++;
    }

    if (WiFi.status() == WL_CONNECTED)
    {
      wifi_connected = true;
      wifi_ip = WiFi.localIP().toString();

      String response = "{\"success\": true, \"message\": \"WiFi連接成功\", \"ip\": \"" + wifi_ip + "\"}";
      server.send(200, "application/json", response);
      Serial.printf("\nWiFi連接成功: %s\n", wifi_ip.c_str());
    }
    else
    {
      String response = "{\"success\": false, \"message\": \"WiFi連接失敗\"}";
      server.send(200, "application/json", response);
      Serial.println("\nWiFi連接失敗");
    }
  }
  else
  {
    server.send(400, "application/json", "{\"success\": false, \"message\": \"無請求資料\"}");
  }
}

void handleSetNetworkMode()
{
  if (server.hasArg("plain"))
  {
    DynamicJsonDocument doc(128);
    DeserializationError error = deserializeJson(doc, server.arg("plain"));

    if (error)
    {
      server.send(400, "application/json", "{\"success\": false, \"message\": \"JSON解析錯誤\"}");
      return;
    }

    int mode = doc["mode"];

    if (mode >= MODE_AP && mode <= MODE_BOTH)
    {
      currentMode = (NetworkMode)mode;

      // 保存配置
      saveConfigToSPIFFS();

      String response = "{\"success\": true, \"message\": \"網路模式已設置\"}";
      server.send(200, "application/json", response);

      Serial.print("網路模式設置為: ");
      switch (currentMode)
      {
      case MODE_AP:
        Serial.println("AP模式");
        break;
      case MODE_WIFI:
        Serial.println("WiFi模式");
        break;
      case MODE_ETH:
        Serial.println("乙太網路模式");
        break;
      case MODE_BOTH:
        Serial.println("雙網路模式");
        break;
      }
    }
    else
    {
      server.send(400, "application/json", "{\"success\": false, \"message\": \"無效的模式\"}");
    }
  }
  else
  {
    server.send(400, "application/json", "{\"success\": false, \"message\": \"無請求資料\"}");
  }
}

void handleEthConfig()
{
  if (server.hasArg("plain"))
  {
    DynamicJsonDocument doc(256);
    DeserializationError error = deserializeJson(doc, server.arg("plain"));

    if (error)
    {
      server.send(400, "application/json", "{\"success\": false, \"message\": \"JSON解析錯誤\"}");
      return;
    }

    eth_dhcp = doc["dhcp"] | true;

    if (!eth_dhcp)
    {
      eth_ip = doc["ip"] | "";
      eth_gateway = doc["gateway"] | "";
      eth_subnet = doc["subnet"] | "";
      eth_dns1 = doc["dns1"] | "8.8.8.8";
      eth_dns2 = doc["dns2"] | "8.8.4.4";
    }
    else
    {
      // DHCP 模式下使用預設 DNS
      eth_dns1 = "8.8.8.8";
      eth_dns2 = "8.8.4.4";
    }

    // 保存配置
    saveConfigToSPIFFS();

    // 重新配置乙太網路
    if (currentMode == MODE_ETH || currentMode == MODE_BOTH)
    {
      setupEthernet();
    }

    server.send(200, "application/json", "{\"success\": true, \"message\": \"乙太網路設定已保存\"}");
    Serial.println("乙太網路設定已更新");
  }
  else
  {
    server.send(400, "application/json", "{\"success\": false, \"message\": \"無請求資料\"}");
  }
}

void handleNetworkStatus()
{
  DynamicJsonDocument doc(512);

  doc["current_mode"] = (int)currentMode;

  // WiFi狀態
  if (currentMode == MODE_WIFI || currentMode == MODE_BOTH)
  {
    wifi_connected = (WiFi.status() == WL_CONNECTED);
    doc["wifi_connected"] = wifi_connected;
    if (wifi_connected)
    {
      doc["wifi_ssid"] = WiFi.SSID();
      doc["wifi_ip"] = WiFi.localIP().toString();
      doc["wifi_rssi"] = WiFi.RSSI();
    }
  }

  // 乙太網路狀態
  if (currentMode == MODE_ETH || currentMode == MODE_BOTH)
  {
    eth_connected = (ETH.localIP().toString() != "0.0.0.0");
    doc["eth_connected"] = eth_connected;
    if (eth_connected)
    {
      doc["eth_ip"] = ETH.localIP().toString();
      doc["eth_gateway"] = ETH.gatewayIP().toString();
      doc["eth_subnet"] = ETH.subnetMask().toString();
    }
  }

  // 當前IP（用於訪問）
  if (currentMode == MODE_AP)
  {
    doc["ap_ip"] = WiFi.softAPIP().toString();
  }
  else if (wifi_connected)
  {
    doc["current_ip"] = wifi_ip;
  }
  else if (eth_connected)
  {
    doc["current_ip"] = eth_ip_current;
  }

  // SPIFFS狀態
  doc["spiffs_total"] = SPIFFS.totalBytes();
  doc["spiffs_used"] = SPIFFS.usedBytes();

  String jsonResponse;
  serializeJson(doc, jsonResponse);
  server.send(200, "application/json", jsonResponse);
}

void handleReboot()
{
  server.send(200, "application/json", "{\"success\": true, \"message\": \"系統將重新啟動\"}");
  Serial.println("收到重啟命令，3秒後重啟...");
  delay(3000);
  ESP.restart();
}

void handleSaveConfig()
{
  saveConfigToSPIFFS();
  server.send(200, "application/json", "{\"success\": true, \"message\": \"配置已保存\"}");
}

void handleLoadConfig()
{
  if (loadConfigFromSPIFFS())
  {
    DynamicJsonDocument doc(512);
    doc["mode"] = (int)currentMode;
    doc["wifi_ssid"] = wifi_ssid;
    doc["wifi_password"] = wifi_password;
    doc["eth_dhcp"] = eth_dhcp;
    doc["eth_ip"] = eth_ip;
    doc["eth_gateway"] = eth_gateway;
    doc["eth_subnet"] = eth_subnet;
    doc["eth_dns1"] = eth_dns1;
    doc["eth_dns2"] = eth_dns2;

    String jsonResponse;
    serializeJson(doc, jsonResponse);
    server.send(200, "application/json", jsonResponse);
  }
  else
  {
    server.send(500, "application/json", "{\"success\": false, \"message\": \"載入配置失敗\"}");
  }
}

void handleFileList()
{
  DynamicJsonDocument doc(4096);
  JsonArray files = doc.to<JsonArray>();

  File root = SPIFFS.open("/");
  if (root)
  {
    File file = root.openNextFile();
    while (file)
    {
      JsonObject fileInfo = files.createNestedObject();
      fileInfo["name"] = file.name();
      fileInfo["size"] = file.size();
      fileInfo["is_directory"] = file.isDirectory();

      file = root.openNextFile();
    }
    root.close();
  }

  String jsonResponse;
  serializeJson(doc, jsonResponse);
  server.send(200, "application/json", jsonResponse);
}

void saveConfigToSPIFFS()
{
  // 建立設定JSON
  DynamicJsonDocument doc(1024);

  doc["mode"] = (int)currentMode;
  doc["wifi_ssid"] = wifi_ssid;
  doc["wifi_password"] = wifi_password;
  doc["eth_dhcp"] = eth_dhcp;
  doc["eth_ip"] = eth_ip;
  doc["eth_gateway"] = eth_gateway;
  doc["eth_subnet"] = eth_subnet;
  doc["ap_ssid"] = ap_ssid;
  doc["ap_password"] = ap_password;
  doc["eth_dns1"] = eth_dns1;
  doc["eth_dns2"] = eth_dns2;

  // 儲存到SPIFFS
  File configFile = SPIFFS.open(CONFIG_FILE, "w");
  if (!configFile)
  {
    Serial.println("無法開啟設定檔案寫入");
    return;
  }

  // 序列化JSON到檔案
  if (serializeJson(doc, configFile) == 0)
  {
    Serial.println("寫入設定檔案失敗");
    configFile.close();
    return;
  }

  configFile.close();
  Serial.println("設定已儲存到SPIFFS");

  // 顯示設定內容
  Serial.println("設定檔案內容:");
  serializeJsonPretty(doc, Serial);
  Serial.println();
}

bool loadConfigFromSPIFFS()
{
  // 檢查設定檔案是否存在
  if (!SPIFFS.exists(CONFIG_FILE))
  {
    Serial.println("設定檔案不存在");
    return false;
  }

  // 開啟設定檔案
  File configFile = SPIFFS.open(CONFIG_FILE, "r");
  if (!configFile)
  {
    Serial.println("無法開啟設定檔案");
    return false;
  }

  // 檢查檔案大小
  size_t size = configFile.size();
  if (size > 1024)
  {
    Serial.println("設定檔案太大");
    configFile.close();
    return false;
  }

  // 讀取檔案內容
  String jsonString = "";
  while (configFile.available())
  {
    jsonString += (char)configFile.read();
  }
  configFile.close();

  // 解析JSON
  DynamicJsonDocument doc(1024);
  DeserializationError error = deserializeJson(doc, jsonString);

  if (error)
  {
    Serial.print("JSON解析錯誤: ");
    Serial.println(error.c_str());
    return false;
  }

  // 讀取設定值
  currentMode = (NetworkMode)(doc["mode"] | MODE_AP);
  wifi_ssid = doc["wifi_ssid"] | "";
  wifi_password = doc["wifi_password"] | "";
  eth_dhcp = doc["eth_dhcp"] | true;
  eth_ip = doc["eth_ip"] | "192.168.1.100";
  eth_gateway = doc["eth_gateway"] | "192.168.1.1";
  eth_subnet = doc["eth_subnet"] | "255.255.255.0";
  ap_ssid = doc["ap_ssid"] | "WT32-ETH01_Config";
  ap_password = doc["ap_password"] | "12345678";
  eth_dns1 = doc["eth_dns1"] | "8.8.8.8";
  eth_dns2 = doc["eth_dns2"] | "8.8.4.4";

  Serial.println("設定已從SPIFFS載入");
  Serial.println("載入的設定:");
  serializeJsonPretty(doc, Serial);
  Serial.println();

  return true;
}

void WiFiEvent(WiFiEvent_t event)
{
  switch (event)
  {
  case ARDUINO_EVENT_ETH_START:
    Serial.println("乙太網路開始");
    ETH.setHostname("wt32-eth01");
    break;

  case ARDUINO_EVENT_ETH_CONNECTED:
    Serial.println("乙太網路已連接");
    break;

  case ARDUINO_EVENT_ETH_GOT_IP:
    eth_ip_current = ETH.localIP().toString();
    Serial.print("乙太網路獲取IP: ");
    Serial.println(eth_ip_current);
    eth_connected = true;
    break;

  case ARDUINO_EVENT_ETH_DISCONNECTED:
    Serial.println("乙太網路斷開連接");
    eth_connected = false;
    break;

  case ARDUINO_EVENT_WIFI_STA_CONNECTED:
    Serial.println("WiFi已連接");
    break;

  case ARDUINO_EVENT_WIFI_STA_GOT_IP:
    wifi_ip = WiFi.localIP().toString();
    Serial.print("WiFi獲取IP: ");
    Serial.println(wifi_ip);
    wifi_connected = true;
    break;

  case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
    Serial.println("WiFi斷開連接");
    wifi_connected = false;
    break;

  case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
    Serial.println("有裝置連接到AP");
    break;

  case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
    Serial.println("裝置從AP斷開");
    break;
  }
}