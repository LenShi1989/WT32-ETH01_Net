#include <WiFi.h>
#include <ETH.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <Preferences.h>

// 全局变量
WebServer server(80);
Preferences preferences;

// 网络状态
bool eth_connected = false;
bool wifi_connected = false;

// 网络配置结构体
struct NetworkConfig
{
  bool wifi_enabled;
  char wifi_ssid[32];
  char wifi_password[64];

  bool eth_dhcp;
  char eth_ip[16];
  char eth_gateway[16];
  char eth_subnet[16];
  char eth_dns1[16];
  char eth_dns2[16];
};

NetworkConfig networkConfig;

void setupWebServer()
{
  // 静态文件服务
  server.serveStatic("/", SPIFFS, "/index.html");
  server.serveStatic("/css", SPIFFS, "/css/");
  server.serveStatic("/js", SPIFFS, "/js/");

  // API端点
  server.on("/api/scan", HTTP_GET, handleScanWifi);
  server.on("/api/connect", HTTP_POST, handleConnectWifi);
  server.on("/api/eth/config", HTTP_GET, handleGetEthConfig);
  server.on("/api/eth/config", HTTP_POST, handleSetEthConfig);
  server.on("/api/network/status", HTTP_GET, handleNetworkStatus);
  server.on("/api/reboot", HTTP_POST, handleReboot);

  server.begin();
}

void scanWiFiNetworks()
{
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);

  int n = WiFi.scanNetworks();
  DynamicJsonDocument doc(4096);
  JsonArray networks = doc.to<JsonArray>();

  for (int i = 0; i < n; i++)
  {
    JsonObject network = networks.createNestedObject();
    network["ssid"] = WiFi.SSID(i);
    network["rssi"] = WiFi.RSSI(i);
    network["channel"] = WiFi.channel(i);
    network["encryption"] = WiFi.encryptionType(i);
  }

  String jsonResponse;
  serializeJson(doc, jsonResponse);
  server.send(200, "application/json", jsonResponse);
}

bool connectToWiFi(const char *ssid, const char *password)
{
  WiFi.begin(ssid, password);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20)
  {
    delay(500);
    attempts++;
  }

  return WiFi.status() == WL_CONNECTED;
}

void setupEthernet()
{
  WiFi.onEvent(WiFiEvent);
  ETH.begin();
}

void WiFiEvent(WiFiEvent_t event)
{
  switch (event)
  {
  case ARDUINO_EVENT_ETH_START:
    Serial.println("ETH Started");
    break;
  case ARDUINO_EVENT_ETH_CONNECTED:
    Serial.println("ETH Connected");
    break;
  case ARDUINO_EVENT_ETH_GOT_IP:
    Serial.print("ETH MAC: ");
    Serial.print(ETH.macAddress());
    Serial.print(", IPv4: ");
    Serial.print(ETH.localIP());
    eth_connected = true;
    break;
  case ARDUINO_EVENT_ETH_DISCONNECTED:
    Serial.println("ETH Disconnected");
    eth_connected = false;
    break;
  case ARDUINO_EVENT_ETH_STOP:
    Serial.println("ETH Stopped");
    eth_connected = false;
    break;
  }
}

void loadNetworkConfig()
{
  preferences.begin("network", true);

  networkConfig.wifi_enabled = preferences.getBool("wifi_enabled", false);
  preferences.getString("wifi_ssid", networkConfig.wifi_ssid, 32);
  preferences.getString("wifi_pass", networkConfig.wifi_password, 64);

  networkConfig.eth_dhcp = preferences.getBool("eth_dhcp", true);
  preferences.getString("eth_ip", networkConfig.eth_ip, 16);
  preferences.getString("eth_gateway", networkConfig.eth_gateway, 16);
  preferences.getString("eth_subnet", networkConfig.eth_subnet, 16);
  preferences.getString("eth_dns1", networkConfig.eth_dns1, 16);
  preferences.getString("eth_dns2", networkConfig.eth_dns2, 16);

  preferences.end();
}

void saveNetworkConfig()
{
  preferences.begin("network", false);

  preferences.putBool("wifi_enabled", networkConfig.wifi_enabled);
  preferences.putString("wifi_ssid", networkConfig.wifi_ssid);
  preferences.putString("wifi_pass", networkConfig.wifi_password);

  preferences.putBool("eth_dhcp", networkConfig.eth_dhcp);
  preferences.putString("eth_ip", networkConfig.eth_ip);
  preferences.putString("eth_gateway", networkConfig.eth_gateway);
  preferences.putString("eth_subnet", networkConfig.eth_subnet);
  preferences.putString("eth_dns1", networkConfig.eth_dns1);
  preferences.putString("eth_dns2", networkConfig.eth_dns2);

  preferences.end();
}