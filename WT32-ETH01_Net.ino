/**************************************
1.Arduino版本：Arduino1.8.19
2.開發版：ESP32 Dev Module version_3.3.5
3.功能：預設AP模式，自動導入設定頁面
**************************************/

#include <WiFi.h>
#include <ETH.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <Update.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

// 乙太網路配置
#define ETH_PHY_ADDR 1   // LAN8720 PHY 地址（0 或 1）
#define ETH_PHY_MDC 23   // 固定引腳
#define ETH_PHY_MDIO 18  // 固定引腳
#define ETH_PHY_POWER 16 // 電源使能引腳（可能是 GPIO16）
#define ETH_PHY_TYPE ETH_PHY_LAN8720
#define ETH_CLK_MODE ETH_CLOCK_GPIO0_IN // WT32-ETH01 使用此時鐘模式

// #define ETH_ADDR 1
// #define ETH_POWER_PIN 16
// #define ETH_MDC_PIN 23
// #define ETH_MDIO_PIN 18
// #define ETH_TYPE ETH_PHY_LAN8720
// #define ETH_CLK_MODE ETH_CLOCK_GPIO0_IN

// 全局變數
WebServer server(80);

// 任務句柄
TaskHandle_t Task1; // WiFi 任務
TaskHandle_t Task2; // 乙太網路任務

// OTA更新狀態
bool otaStarted = false;

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

// 網路狀態（使用互斥鎖保護）
SemaphoreHandle_t wifiMutex;
SemaphoreHandle_t ethMutex;
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
void handleOtaUpload();
void startOTA();
void setupOTA();
void printEthStatus();
void resetEthernet();

// ============ 新增任務函數 ============
void Task1code(void *parameter); // WiFi任務
void Task2code(void *parameter); // 乙太網路任務

void setup()
{
  Serial.begin(115200);
  delay(2000); // 增加延遲確保串口穩定
  Serial.println("\n\n=== WT32-ETH01 雙線程網路配置系統 ===");
  Serial.printf("SDK版本: %s\n", ESP.getSdkVersion());
  Serial.printf("晶片型號: %s\n", ESP.getChipModel());
  Serial.printf("晶片版本: %d\n", ESP.getChipRevision());
  Serial.printf("CPU頻率: %d MHz\n", ESP.getCpuFreqMHz());
  Serial.printf("閃存大小: %d KB\n", ESP.getFlashChipSize() / 1024);
  Serial.printf("可用堆內存: %d bytes\n", ESP.getFreeHeap());

  // 初始化互斥鎖
  wifiMutex = xSemaphoreCreateMutex();
  ethMutex = xSemaphoreCreateMutex();

  if (wifiMutex == NULL || ethMutex == NULL)
  {
    Serial.println("互斥鎖創建失敗，系統可能不穩定");
  }

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

  // 打印配置
  Serial.println("\n=== 載入的配置 ===");
  Serial.printf("網路模式: %d\n", currentMode);
  Serial.printf("WiFi SSID: %s\n", wifi_ssid.c_str());
  Serial.printf("乙太網路 DHCP: %s\n", eth_dhcp ? "啟用" : "禁用");
  if (!eth_dhcp)
  {
    Serial.printf("靜態 IP: %s\n", eth_ip.c_str());
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

  // ============ 創建雙線程 ============
  Serial.println("\n=== 創建網路線程 ===");

  xTaskCreatePinnedToCore(
      Task1code,  /* 任務函數 */
      "WiFiTask", /* 任務名稱 */
      16384,      /* 增加堆棧大小 */
      NULL,       /* 參數 */
      1,          /* 優先級 */
      &Task1,     /* 任務句柄 */
      0);         /* 運行在核心0 */

  delay(500); // 等待WiFi線程創建

  xTaskCreatePinnedToCore(
      Task2code,      /* 任務函數 */
      "EthernetTask", /* 任務名稱 */
      16384,          /* 增加堆棧大小 */
      NULL,           /* 參數 */
      1,              /* 優先級 */
      &Task2,         /* 任務句柄 */
      1);             /* 運行在核心1 */

  Serial.println("線程創建完成");

  // 設置Web服務器（在主線程中運行）
  delay(1000); // 等待線程初始化
  setupWebServer();

  Serial.println("系統啟動完成");
}

void loop()
{
  server.handleClient();

  // 如果有 OTA 請求，處理 OTA
  if (otaStarted)
  {
    ArduinoOTA.handle();
  }

  // 定期打印乙太網路狀態
  static unsigned long lastEthPrint = 0;
  if (millis() - lastEthPrint > 30000)
  { // 每30秒打印一次
    if (currentMode == MODE_ETH || currentMode == MODE_BOTH)
    {
      printEthStatus();
    }
    lastEthPrint = millis();
  }

  // 監控網路狀態（移除定期掃描）
  // static unsigned long lastCheck = 0;
  // if (millis() - lastCheck > 5000)
  // {
  //   if (currentMode == MODE_WIFI || currentMode == MODE_BOTH)
  //   {
  //     wifi_connected = (WiFi.status() == WL_CONNECTED);
  //     if (wifi_connected)
  //     {
  //       wifi_ip = WiFi.localIP().toString();
  //     }
  //   }

  //   if (currentMode == MODE_ETH || currentMode == MODE_BOTH)
  //   {
  //     eth_connected = (ETH.localIP().toString() != "0.0.0.0");
  //     if (eth_connected)
  //     {
  //       eth_ip_current = ETH.localIP().toString();
  //     }
  //   }

  //   lastCheck = millis();
  // }

  // 延遲以釋放CPU時間
  delay(10);
}

// ============ WiFi任務函數 ============
void Task1code(void *parameter)
{
  Serial.print("WiFi任務運行在核心: ");
  Serial.println(xPortGetCoreID());

  while (true)
  {
    // 根據網路模式處理WiFi
    if (currentMode == MODE_AP || currentMode == MODE_WIFI || currentMode == MODE_BOTH)
    {

      if (currentMode == MODE_AP)
      {
        // AP模式處理
        static bool apStarted = false;
        if (!apStarted)
        {
          if (xSemaphoreTake(wifiMutex, portMAX_DELAY))
          {
            startAPMode();
            apStarted = true;
            xSemaphoreGive(wifiMutex);
          }
        }
      }
      else if (currentMode == MODE_WIFI || currentMode == MODE_BOTH)
      {
        // WiFi連接模式處理
        static bool wifiConnecting = false;

        if (xSemaphoreTake(wifiMutex, portMAX_DELAY))
        {
          bool wasConnected = wifi_connected;
          wifi_connected = (WiFi.status() == WL_CONNECTED);

          if (!wifi_connected && wifi_ssid.length() > 0 && !wifiConnecting)
          {
            Serial.println("[WiFi任務] 嘗試連接WiFi...");
            wifiConnecting = true;

            // 先釋放鎖再連接，避免阻塞
            xSemaphoreGive(wifiMutex);

            // 連接WiFi
            WiFi.mode(WIFI_STA);
            WiFi.begin(wifi_ssid.c_str(), wifi_password.c_str());

            int attempts = 0;
            while (WiFi.status() != WL_CONNECTED && attempts < 20)
            {
              delay(500);
              Serial.print(".");
              attempts++;
            }

            if (WiFi.status() == WL_CONNECTED)
            {
              wifi_ip = WiFi.localIP().toString();
              Serial.printf("\n[WiFi任務] WiFi連接成功! IP: %s\n", wifi_ip.c_str());
            }
            else
            {
              Serial.println("\n[WiFi任務] WiFi連接失敗");
            }

            wifiConnecting = false;
            continue; // 跳過本次循環的後續部分
          }
          else if (wifi_connected && !wasConnected)
          {
            wifi_ip = WiFi.localIP().toString();
            Serial.printf("[WiFi任務] WiFi已連接，IP: %s\n", wifi_ip.c_str());
          }

          xSemaphoreGive(wifiMutex);
        }
      }
    }

    // 任務延遲
    delay(1000);
  }
}

// ============ 乙太網路任務函數 ============
void Task2code(void *parameter)
{
  Serial.print("乙太網路任務運行在核心: ");
  Serial.println(xPortGetCoreID());

  // 延遲啟動，避免與WiFi同時初始化
  delay(3000);

  while (true)
  {
    // 根據網路模式處理乙太網路
    if (currentMode == MODE_ETH || currentMode == MODE_BOTH)
    {

      static bool ethInitialized = false;

      if (!ethInitialized)
      {
        Serial.println("[乙太網路任務] 初始化乙太網路...");

        // 使用互斥鎖保護初始化
        if (xSemaphoreTake(ethMutex, portMAX_DELAY))
        {

          // 先確保WiFi完全關閉
          WiFi.disconnect(true);
          delay(100);
          WiFi.mode(WIFI_OFF);
          delay(100);

          // 初始化乙太網路 - 使用正確的引腳定義
          Serial.println("設置乙太網路引腳...");

          // 使用 ETH.begin() 並指定正確參數
          bool ethStarted = ETH.begin(
              ETH_PHY_TYPE,  // PHY 類型
              ETH_PHY_ADDR,  // PHY 地址
              ETH_PHY_POWER, // 電源引腳
              ETH_PHY_MDC,   // MDC 引腳
              ETH_PHY_MDIO,  // MDIO 引
              ETH_CLK_MODE   // 時鐘模式
          );

          if (!ethStarted)
          {
            Serial.println("[乙太網路任務] ETH.begin() 失敗，重試...");
            xSemaphoreGive(ethMutex);
            delay(5000); // 等待5秒後重試
            continue;
          }

          Serial.println("[乙太網路任務] ETH.begin() 成功");

          // 設置主機名
          ETH.setHostname("wt32-eth01");

          // 配置靜態IP（如果需要）
          if (!eth_dhcp && eth_ip.length() > 0)
          {
            Serial.println("[乙太網路任務] 配置靜態IP...");

            IPAddress ip, gateway, subnet, dns1, dns2;

            if (ip.fromString(eth_ip.c_str()) &&
                gateway.fromString(eth_gateway.c_str()) &&
                subnet.fromString(eth_subnet.c_str()))
            {

              bool hasDNS1 = dns1.fromString(eth_dns1.c_str());
              bool hasDNS2 = dns2.fromString(eth_dns2.c_str());

              if (hasDNS1 && hasDNS2)
              {
                ETH.config(ip, gateway, subnet, dns1, dns2);
                Serial.println("[乙太網路任務] 靜態IP配置完成（含DNS）");
              }
              else if (hasDNS1)
              {
                ETH.config(ip, gateway, subnet, dns1);
                Serial.println("[乙太網路任務] 靜態IP配置完成（含主要DNS）");
              }
              else
              {
                ETH.config(ip, gateway, subnet);
                Serial.println("[乙太網路任務] 靜態IP配置完成");
              }
            }
          }
          else
          {
            Serial.println("[乙太網路任務] 使用DHCP模式");
          }

          ethInitialized = true;
          xSemaphoreGive(ethMutex);
        }
      }
      else
      {
        // 已初始化，檢查連接狀態
        if (xSemaphoreTake(ethMutex, portMAX_DELAY))
        {
          bool wasConnected = eth_connected;

          // 檢查鏈路狀態和IP
          bool linkUp = ETH.linkUp();
          bool hasIP = (ETH.localIP().toString() != "0.0.0.0");
          eth_connected = linkUp && hasIP;

          if (eth_connected)
          {
            String currentIP = ETH.localIP().toString();
            if (currentIP != eth_ip_current)
            {
              eth_ip_current = currentIP;
              Serial.printf("[乙太網路任務] IP: %s, Link: %s\n",
                            eth_ip_current.c_str(),
                            linkUp ? "Up" : "Down");
            }
          }
          else if (wasConnected)
          {
            Serial.println("[乙太網路任務] 乙太網路斷開連接");
            eth_ip_current = "";
          }

          xSemaphoreGive(ethMutex);
        }
      }
    }

    // 任務延遲
    delay(2000);
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
  Serial.println("[主線程] 設置AP模式...");

  // 斷開所有網路連接
  WiFi.disconnect(true);
  delay(100);

  // 設置AP模式
  WiFi.mode(WIFI_AP);

  // 啟動AP
  WiFi.softAP(ap_ssid.c_str(), ap_password.c_str());

  // 配置AP的IP
  IPAddress apIP(192, 168, 4, 1);
  IPAddress gateway(192, 168, 4, 1);
  IPAddress subnet(255, 255, 255, 0);
  WiFi.softAPConfig(apIP, gateway, subnet);

  Serial.print("[主線程] AP已啟動，SSID: ");
  Serial.println(ap_ssid);
  Serial.print("[主線程] AP IP地址: ");
  Serial.println(WiFi.softAPIP().toString());
}

void connectToWiFi()
{
  // 這個函數現在由WiFi任務處理
  Serial.println("[主線程] WiFi連接請求已轉發到WiFi任務");
}

void setupEthernet()
{
  // 這個函數現在由乙太網路任務處理
  Serial.println("[主線程] 乙太網路初始化請求已轉發到乙太網路任務");
}

void setupWebServer()
{
  // 靜態文件服務
  server.serveStatic("/", SPIFFS, "/index.html");
  handleNetworkStatus();
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

  // 添加 OTA 端點
  server.on("/api/ota/upload", HTTP_POST, []()
            { server.send(200, "application/json", "{\"success\": true, \"message\": \"OTA上傳完成\"}"); }, handleOtaUpload);

  server.on("/api/ota/start", HTTP_POST, []()
            {
    startOTA();
    server.send(200, "application/json", "{\"success\": true, \"message\": \"OTA模式已啟動\"}"); });

  server.on("/api/ota/status", HTTP_GET, []()
            {
    DynamicJsonDocument doc(256);
    doc["ota_started"] = otaStarted;
    doc["ota_port"] = 3232;
    
    String jsonResponse;
    serializeJson(doc, jsonResponse);
    server.send(200, "application/json", jsonResponse); });

  // 添加 OTA 的 OPTIONS 處理
  server.on("/api/ota/upload", HTTP_OPTIONS, []()
            {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Access-Control-Allow-Methods", "POST, OPTIONS");
    server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
    server.send(200); });

  // 添加 eth 的 GET 處理
  server.on("/api/eth/diagnose", HTTP_GET, []()
            {
  DynamicJsonDocument doc(512);
  
  doc["phy_address"] = ETH_PHY_ADDR;
  doc["phy_type"] = "LAN8720";
  doc["clock_mode"] = "GPIO0_IN";
  doc["link_status"] = ETH.linkUp();
  doc["auto_negotiation"] = ETH.autoNegotiation();
  doc["speed_mbps"] = ETH.linkSpeed();
  doc["duplex"] = ETH.fullDuplex();
  doc["ip_address"] = ETH.localIP().toString();
  doc["subnet_mask"] = ETH.subnetMask().toString();
  doc["gateway"] = ETH.gatewayIP().toString();
  doc["dns_server"] = ETH.dnsIP().toString();
  doc["mac_address"] = ETH.macAddress();
  
  String jsonResponse;
  serializeJson(doc, jsonResponse);
  server.send(200, "application/json", jsonResponse); });

  server.on("/api/eth/reset", HTTP_POST, []()
            {
  resetEthernet();
  server.send(200, "application/json", "{\"success\": true, \"message\": \"乙太網路已重置\"}"); });

  server.on("/api/system/info", HTTP_GET, []()
            {
  DynamicJsonDocument doc(512);
  
  doc["sdk_version"] = ESP.getSdkVersion();
  doc["chip_model"] = ESP.getChipModel();
  doc["chip_revision"] = ESP.getChipRevision();
  doc["cpu_freq_mhz"] = ESP.getCpuFreqMHz();
  doc["flash_size_kb"] = ESP.getFlashChipSize() / 1024;
  doc["free_heap"] = ESP.getFreeHeap();
  doc["sketch_size"] = ESP.getSketchSize();
  doc["free_sketch_space"] = ESP.getFreeSketchSpace();
  
  String jsonResponse;
  serializeJson(doc, jsonResponse);
  server.send(200, "application/json", jsonResponse); });

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
      // 更新網路模式
      currentMode = (NetworkMode)mode;

      // 根據新模式重置網路狀態
      if (xSemaphoreTake(wifiMutex, pdMS_TO_TICKS(100)))
      {
        if (mode == MODE_ETH)
        {
          // 切換到乙太網路模式時關閉WiFi
          WiFi.disconnect(true);
          wifi_connected = false;
          wifi_ip = "";
        }
        xSemaphoreGive(wifiMutex);
      }

      if (xSemaphoreTake(ethMutex, pdMS_TO_TICKS(100)))
      {
        if (mode == MODE_WIFI)
        {
          // 切換到WiFi模式時停止乙太網路
          eth_connected = false;
          eth_ip_current = "";
        }
        xSemaphoreGive(ethMutex);
      }

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

  // WiFi狀態（使用互斥鎖保護）
  if (xSemaphoreTake(wifiMutex, pdMS_TO_TICKS(100)))
  {
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
    xSemaphoreGive(wifiMutex);
  }

  // 乙太網路狀態（使用互斥鎖保護）
  if (xSemaphoreTake(ethMutex, pdMS_TO_TICKS(100)))
  {
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
    xSemaphoreGive(ethMutex);
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

  // 線程信息
  doc["wifi_task_core"] = xPortGetCoreID() == 0 ? "核心0" : "核心1";
  doc["eth_task_core"] = xPortGetCoreID() == 1 ? "核心1" : "核心0";

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
  // 使用互斥鎖保護共享資源
  if (xSemaphoreTake(wifiMutex, pdMS_TO_TICKS(50)))
  {
    switch (event)
    {
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      Serial.println("[事件] WiFi已連接");
      break;

    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      wifi_ip = WiFi.localIP().toString();
      Serial.print("[事件] WiFi獲取IP: ");
      Serial.println(wifi_ip);
      wifi_connected = true;
      break;

    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      Serial.println("[事件] WiFi斷開連接");
      wifi_connected = false;
      break;

    case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
      Serial.println("[事件] 有裝置連接到AP");
      break;

    case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
      Serial.println("[事件] 裝置從AP斷開");
      break;
    }
    xSemaphoreGive(wifiMutex);
  }
}

// 新增 OTA 上傳處理函數
void handleOtaUpload()
{
  HTTPUpload &upload = server.upload();

  if (upload.status == UPLOAD_FILE_START)
  {
    Serial.printf("開始OTA上傳: %s\n", upload.filename.c_str());

    // 檢查檔案類型
    if (!upload.filename.endsWith(".bin"))
    {
      Serial.println("錯誤：僅支援 .bin 檔案");
      return;
    }

    // 開始更新
    if (!Update.begin(UPDATE_SIZE_UNKNOWN))
    {
      Update.printError(Serial);
    }
  }
  else if (upload.status == UPLOAD_FILE_WRITE)
  {
    // 寫入資料
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize)
    {
      Update.printError(Serial);
    }
    Serial.printf("寫入資料: %d bytes\n", upload.currentSize);
  }
  else if (upload.status == UPLOAD_FILE_END)
  {
    // 完成上傳
    if (Update.end(true))
    {
      Serial.printf("OTA更新成功，檔案大小: %u bytes\n", upload.totalSize);
    }
    else
    {
      Update.printError(Serial);
    }
  }
  else if (upload.status == UPLOAD_FILE_ABORTED)
  {
    Update.abort();
    Serial.println("OTA上傳中斷");
  }

  yield();
}

// 新增啟動 OTA 函數
void startOTA()
{
  if (!otaStarted)
  {
    Serial.println("啟動OTA模式...");
    otaStarted = true;

    // 設置OTA
    setupOTA();

    Serial.println("OTA模式已啟動");
    Serial.println("可通過以下方式更新：");
    Serial.println("1. Arduino IDE: 工具 -> 端口 -> 網路端口 -> wt32-eth01.local:3232");
    Serial.println("2. 網頁上傳: http://設備IP地址/ota");
    Serial.println("注意: OTA期間請勿斷電！");
  }
}

// 新增 OTA 設置函數
void setupOTA()
{
  // 設置 OTA
  ArduinoOTA.setPort(3232);
  ArduinoOTA.setHostname("wt32-eth01");

  // 無密碼
  ArduinoOTA.setPassword("admin123");

  ArduinoOTA.onStart([]()
                     {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_SPIFFS
      type = "filesystem";
    }
    Serial.println("開始OTA更新: " + type); });

  ArduinoOTA.onEnd([]()
                   { Serial.println("\nOTA更新完成"); });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total)
                        { Serial.printf("更新進度: %u%%\r", (progress / (total / 100))); });

  ArduinoOTA.onError([](ota_error_t error)
                     {
    Serial.printf("錯誤[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("認證失敗");
    else if (error == OTA_BEGIN_ERROR) Serial.println("開始失敗");
    else if (error == OTA_CONNECT_ERROR) Serial.println("連接失敗");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("接收失敗");
    else if (error == OTA_END_ERROR) Serial.println("結束失敗"); });

  ArduinoOTA.begin();
  Serial.println("OTA服務已啟動");
}

// 實現診斷函數
void printEthStatus()
{
  Serial.println("\n=== 乙太網路狀態診斷 ===");
  Serial.printf("PHY 地址: %d\n", ETH_PHY_ADDR);
  Serial.printf("時鐘模式: %s\n", (ETH_CLK_MODE == ETH_CLOCK_GPIO0_IN) ? "GPIO0_IN" : "Unknown");
  Serial.printf("鏈路狀態: %s\n", ETH.linkUp() ? "Up" : "Down");
  Serial.printf("自動協商: %s\n", ETH.autoNegotiation() ? "Enabled" : "Disabled");
  Serial.printf("速度: %d Mbps\n", ETH.linkSpeed());
  Serial.printf("雙工模式: %s\n", ETH.fullDuplex() ? "Full" : "Half");
  Serial.printf("IP地址: %s\n", ETH.localIP().toString().c_str());
  Serial.printf("子網掩碼: %s\n", ETH.subnetMask().toString().c_str());
  Serial.printf("閘道器: %s\n", ETH.gatewayIP().toString().c_str());
  Serial.printf("DNS服務器: %s\n", ETH.dnsIP().toString().c_str());
  Serial.println("========================\n");
}

void resetEthernet()
{
  Serial.println("重置乙太網路...");

  if (xSemaphoreTake(ethMutex, portMAX_DELAY))
  {
    // 停止乙太網路
    ETH.config(IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0));
    delay(100);

    // 重新初始化
    ETH.begin(
        ETH_PHY_TYPE,
        ETH_PHY_ADDR,
        ETH_PHY_MDC,
        ETH_PHY_MDIO,
        ETH_PHY_POWER,
        ETH_CLK_MODE);

    Serial.println("乙太網路已重置");
    xSemaphoreGive(ethMutex);
  }
}