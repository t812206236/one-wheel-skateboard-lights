#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include <SPIFFS.h>
#include <EEPROM.h>
#include <ArduinoJson.h>
#define FASTLED_FORCE_SOFTWARE_SPI
#include <FastLED.h>
#include <Arduino.h>
#include <SoftwareSerial.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"


#define LED1_GPIO 12
#define LED2_GPIO 13
#define UART_READ 16
#define UART_SEND 17

#define ANIM_START \
  int animBakup=*req.rgbAnim; \
  int animDelay; \
  int animBrightness; \
  int animWaveBeat;
  
#define ANIM_LOOP \
  (animBakup==*req.rgbAnim) \
  && (currContextualModel==req.withContext) \
  && ((*req.isDel)==0)
  
#define ANIM_DELAY \
  animDelay=map(*req.animSpeed,0,10,100,15); \
  vTaskDelay(animDelay / portTICK_PERIOD_MS);

#define ANIM_SETBRIGHTNESS \
  animBrightness=*req.brightness; \
  FastLED.setBrightness(animBrightness*255/100);

#define ANIM_WAVEBEAT \
  round(map(*req.animSpeed,0,10,20,60));

/**
 * 渐暗的幅度和循环的频率、每分钟的拍数有关
 * 拍数是根据指定的动画速度
 * 循环的频率也必须确定，5ms是个不错的值，速度够快也不会占用过多
 */
#define ANIM_WAVEDELAY \
  vTaskDelay(5 / portTICK_PERIOD_MS);

/**
 * 60*1000/animWaveBeat 一个周期多久毫秒
 * /2 半个周期，也就是灯条从头到尾所用的时间
 * /i 灯珠跑1/i所用的时间
 * /5 有多少次渐淡的次数
 * 255除上面的结果就是没轮应该渐淡多少
 * 理想是灯珠跑一半最开始亮的灯珠从最亮（255）已经暗没了
 */
#define ANIM_WAVEFADE(animWaveBeat,i) \
  round(255/(60*1000/(animWaveBeat)/2/(i)/5))
  

SoftwareSerial vescSerial(UART_READ, UART_SEND);

const char* ssid = "Vanwheel led config server";
const char* password = "qwer7890";

enum contextualModel{
  CONTEXT_NONE=0,
  CONTEXT_BRAKE,
  CONTEXT_STANDBY
};
struct RGBConfig{
  char initFlag;
  char brightness;
  char reverse;
  char headOnOff;
  char tailOnOff;
  int headRGBSingleColor[3];// ffffff
  int tailRGBSingleColor[3];
  int headRGBAnim;
  int tailRGBAnim;
  int animSpeed;
  char pinARGBNum;
  char pinBRGBNum;
  char autoHead;
  char standbyShow;
};

RGBConfig rgbConfig;

struct TaskReqInfo{
  char *taskName;
  CRGB *leds;
  char  ledNum;
  int* rgb;
  char* brightness;
  char* onOff;
  char isHead;//是否是头
  int* animSpeed;
  int* rgbAnim;
  /*
   * 是否是场景伴随，0代表无场景
   * 主要是用来让动画循环停止的标志
   * 因为有的时候头尾职责没变但是场景变了
   * 场景变了，需要显示的内容就变了，所以需要退出循环
   */
  char withContext;
  /**
   * 删除任务时先置isDel为1，等待任务自己退出后置isExit为1
   */
  char* isDel;
  char* isExit;
};
enum RGBAnim{
  ANIM_OFF=0,
  ANIM_LRSCAN2CENTER,
  ANIM_RAINBOW,
  ANIM_BREATHING,
  ANIM_PINGPONG,
  ANIM_CRYSTALGLASS,
  ANIM_POLICELIGHT,
  ANIM_RANDOMRAINBOW,
  ANIM_SINWAVE1,
  ANIM_SINWAVE2,
  ANIM_SINWAVE3
};

void singleColorApp(TaskReqInfo req);
void scanLeftAndRight2CenterApp(TaskReqInfo req);
void rainbowStreamApp(TaskReqInfo req);
void breathingApp(TaskReqInfo req);
void pingpongApp(TaskReqInfo req);
void crystalglassApp(TaskReqInfo req);
void policeLightApp(TaskReqInfo req);
void randomRainbowApp(TaskReqInfo req);
void sinWave1(TaskReqInfo req);
void sinWave2(TaskReqInfo req);
void sinWave3(TaskReqInfo req);

/**
 * 要和枚举RGBAnim一一对应
 */
void (*animMapArray[])(TaskReqInfo)={
  singleColorApp,
  scanLeftAndRight2CenterApp,
  rainbowStreamApp,
  breathingApp,
  pingpongApp,
  crystalglassApp,
  policeLightApp,
  randomRainbowApp,
  sinWave1,
  sinWave2,
  sinWave3
};

WebServer server(80);

CRGB *leds1;
CRGB *leds2;
SemaphoreHandle_t mutex1;
SemaphoreHandle_t mutex2;

String vescOutput="Nothing!";
float vescPitch=0.0;
float vescSpeed=0.0;
char currContextualModel=CONTEXT_NONE;
String logStr="";

/*
 * atoi 将字符串数字转换为int型数字，比如"123" 为123，类似的还有atof
 * sprintf 数字输出为字符串
 * sscanf 字符串输出为数字
 */

void handleUpdateConfig(){
  int start;
  char buf[20];
  for(int i=0;i<server.args();i++){
    
    Serial.println("更新参数"+server.argName(i)+" "+server.arg(i));
    server.argName(i).toCharArray(buf,20);
    
    if(!strcmp("brightness",buf)){
      rgbConfig.brightness=server.arg(i).toInt();
      start=0+offsetof(RGBConfig,brightness);
      EEPROM.put(start,rgbConfig.brightness);
    }
    if(!strcmp("reverse",buf)){
      rgbConfig.reverse=server.arg(i).toInt();
      start=0+offsetof(RGBConfig,reverse);
      EEPROM.put(start,rgbConfig.reverse);
    }
    if(!strcmp("headOnOff",buf)){
      rgbConfig.headOnOff=server.arg(i).toInt();
      start=0+offsetof(RGBConfig,headOnOff);
      EEPROM.put(start,rgbConfig.headOnOff);
    }
    if(!strcmp("tailOnOff",buf)){
      rgbConfig.tailOnOff=server.arg(i).toInt();
      start=0+offsetof(RGBConfig,tailOnOff);
      EEPROM.put(start,rgbConfig.tailOnOff);
    }
    /*
     * 这个sscanf有问题，%02X的时候虽然是指定2个十六进制，但实际占位4个字节
     * 即使使用%2X的时候也是占位4个字节，网上说%hhX可以解决问题
     * 但是试过了以后还是不行，实在是拗不过它，我服软了，用int存两个字节的数据
     */
    if(!strcmp("headRGBSingleColor",buf)){
      server.arg(i).toCharArray(buf,20);
      sscanf(buf,"%02x%02x%02x",\
      &rgbConfig.headRGBSingleColor[0],\
      &rgbConfig.headRGBSingleColor[1],\
      &rgbConfig.headRGBSingleColor[2]);
      start=0+offsetof(RGBConfig,headRGBSingleColor);
      EEPROM.put(start,rgbConfig.headRGBSingleColor);
    }
    if(!strcmp("tailRGBSingleColor",buf)){
      server.arg(i).toCharArray(buf,20);
      sscanf(buf,"%02x%02x%02x",\
      &rgbConfig.tailRGBSingleColor[0],\
      &rgbConfig.tailRGBSingleColor[1],\
      &rgbConfig.tailRGBSingleColor[2]);
      start=0+offsetof(RGBConfig,tailRGBSingleColor);
      EEPROM.put(start,rgbConfig.tailRGBSingleColor);
    }  
    if(!strcmp("pinARGBNum",buf)){
      rgbConfig.pinARGBNum=server.arg(i).toInt();
      start=0+offsetof(RGBConfig,pinARGBNum);
      EEPROM.put(start,rgbConfig.pinARGBNum);
    }
    if(!strcmp("pinBRGBNum",buf)){
      rgbConfig.pinBRGBNum=server.arg(i).toInt();
      start=0+offsetof(RGBConfig,pinBRGBNum);
      EEPROM.put(start,rgbConfig.pinBRGBNum);
    }
    if(!strcmp("autoHead",buf)){
      rgbConfig.autoHead=server.arg(i).toInt();
      start=0+offsetof(RGBConfig,autoHead);
      EEPROM.put(start,rgbConfig.autoHead);
    }
    
    if(!strcmp("headRGBAnim",buf)){
      rgbConfig.headRGBAnim=server.arg(i).toInt();
      start=0+offsetof(RGBConfig,headRGBAnim);
      EEPROM.put(start,rgbConfig.headRGBAnim);
    }
    
    if(!strcmp("tailRGBAnim",buf)){
      rgbConfig.tailRGBAnim=server.arg(i).toInt();
      start=0+offsetof(RGBConfig,tailRGBAnim);
      EEPROM.put(start,rgbConfig.tailRGBAnim);
    }
    
    if(!strcmp("animSpeed",buf)){
      rgbConfig.animSpeed=server.arg(i).toInt();
      start=0+offsetof(RGBConfig,animSpeed);
      EEPROM.put(start,rgbConfig.animSpeed);
    }
    
    if(!strcmp("standbyShow",buf)){
      rgbConfig.standbyShow=server.arg(i).toInt();
      start=0+offsetof(RGBConfig,standbyShow);
      EEPROM.put(start,rgbConfig.standbyShow);
    }
  }
  EEPROM.commit();
  server.send(200, "text/plain", "ok!");
}

void handleGetConfig(){
  StaticJsonDocument<512> doc;
  char jsonStr[512];
  char buf[6];
  
  sprintf(buf,"%d",rgbConfig.brightness);
  doc["brightness"]=String(buf);
  
  sprintf(buf,"%d",rgbConfig.reverse);
  doc["reverse"]=String(buf);
  
  sprintf(buf,"%d",rgbConfig.headOnOff);
  doc["headOnOff"]=String(buf);
  
  sprintf(buf,"%d",rgbConfig.tailOnOff);
  doc["tailOnOff"]=String(buf);
  
  sprintf(buf,"%02x%02x%02x",rgbConfig.headRGBSingleColor[0],\
  rgbConfig.headRGBSingleColor[1],\
  rgbConfig.headRGBSingleColor[2]);
  doc["headRGBSingleColor"]=String(buf);
  
  sprintf(buf,"%02x%02x%02x",rgbConfig.tailRGBSingleColor[0],\
  rgbConfig.tailRGBSingleColor[1],\
  rgbConfig.tailRGBSingleColor[2]);
  doc["tailRGBSingleColor"]=String(buf);

  sprintf(buf,"%d",rgbConfig.pinARGBNum);
  doc["pinARGBNum"]=String(buf);

  sprintf(buf,"%d",rgbConfig.pinBRGBNum);
  doc["pinBRGBNum"]=String(buf);

  sprintf(buf,"%d",rgbConfig.autoHead);
  doc["autoHead"]=String(buf);

  sprintf(buf,"%d",rgbConfig.headRGBAnim);
  doc["headRGBAnim"]=String(buf);
  
  sprintf(buf,"%d",rgbConfig.tailRGBAnim);
  doc["tailRGBAnim"]=String(buf);
  
  sprintf(buf,"%d",rgbConfig.animSpeed);
  doc["animSpeed"]=String(buf);
  
  sprintf(buf,"%d",rgbConfig.standbyShow);
  doc["standbyShow"]=String(buf);

  serializeJson(doc,jsonStr);
  server.sendHeader("Content-Type", "application/json");
  server.send(200, "application/json", jsonStr);
}

void handleGetAnimList(){
  String jsonString;
  StaticJsonDocument<1024> doc;
  
  JsonObject option1 = doc.createNestedObject();
  option1["id"] = ANIM_OFF;
  option1["name"] = "单色无动画";
  
  JsonObject option2 = doc.createNestedObject();
  option2["id"] = ANIM_LRSCAN2CENTER;
  option2["name"] = "左右向中间扫描（基于底色）";

  JsonObject option3 = doc.createNestedObject();
  option3["id"] = ANIM_RAINBOW;
  option3["name"] = "彩虹流（固有颜色）";

  JsonObject option4 = doc.createNestedObject();
  option4["id"] = ANIM_BREATHING;
  option4["name"] = "呼吸灯（基于底色）";
  
  JsonObject option5 = doc.createNestedObject();
  option5["id"] = ANIM_PINGPONG;
  option5["name"] = "乒乓（基于底色）";

  JsonObject option6 = doc.createNestedObject();
  option6["id"] = ANIM_CRYSTALGLASS;
  option6["name"] = "晶格玻璃（基于底色）";

  JsonObject option7 = doc.createNestedObject();
  option7["id"] = ANIM_POLICELIGHT;
  option7["name"] = "警闪（固有颜色、频率）";

  JsonObject option8 = doc.createNestedObject();
  option8["id"] = ANIM_RANDOMRAINBOW;
  option8["name"] = "随机彩虹（固有颜色）";

  JsonObject option9 = doc.createNestedObject();
  option9["id"] = ANIM_SINWAVE1;
  option9["name"] = "双波叠加拖尾（固有颜色、频率）";

  JsonObject option10 = doc.createNestedObject();
  option10["id"] = ANIM_SINWAVE2;
  option10["name"] = "双波叠加彩虹（固有颜色、频率）";

  JsonObject option11 = doc.createNestedObject();
  option11["id"] = ANIM_SINWAVE3;
  option11["name"] = "三波交叉（固有颜色）";
  
  serializeJson(doc, jsonString);
  server.sendHeader("Content-Type", "application/json");
  server.send(200, "application/json", jsonString);
}

String getContentType(String filename) {
  if (filename.endsWith(".html")) return "text/html";
  else if (filename.endsWith(".css")) return "text/css";
  else if (filename.endsWith(".js")) return "application/javascript";
  else if (filename.endsWith(".png")) return "image/png";
  else if (filename.endsWith(".gif")) return "image/gif";
  else if (filename.endsWith(".jpg") || filename.endsWith(".jpeg")) return "image/jpeg";
  else if (filename.endsWith(".ico")) return "image/x-icon";
  else if (filename.endsWith(".svg")) return "image/svg+xml";
  else if (filename.endsWith(".pdf")) return "application/pdf";
  else if (filename.endsWith(".zip")) return "application/zip";
  else if (filename.endsWith(".gz")) return "application/gzip";
  else if (filename.endsWith(".json")) return "application/json";
  else if (filename.endsWith(".mp3")) return "audio/mpeg";
  else if (filename.endsWith(".mp4")) return "video/mp4";
  else if (filename.endsWith(".txt")) return "text/plain";
  else return "application/octet-stream";
}

void calVescOutput(){
  String tmp=vescOutput;
  
  int start = tmp.indexOf('{') + 1;
  int end = tmp.indexOf('}');
  if(end>start){
    String first = tmp.substring(start, end);
    float firstValue = atof(first.c_str());
    vescPitch = firstValue * 180 / PI;
  }else{return;}

  start = tmp.indexOf('{', end) + 1;
  end = tmp.indexOf('}', start);
  if(end>start){
    String second = tmp.substring(start, end);
    vescSpeed = atof(second.c_str());
  }else{return;}
  
}

void setup(void) {
  Serial.begin(115200);
  SPIFFS.begin(true);
  WiFi.softAP(ssid, password);
  EEPROM.begin(512);
  EEPROM.get(0,rgbConfig);
  
  if(rgbConfig.initFlag!=1){
    Serial.println("eeprom init");
    rgbConfig.initFlag=1;
    rgbConfig.brightness=50;
    rgbConfig.reverse=0;
    rgbConfig.headOnOff=1;
    rgbConfig.tailOnOff=1;
    memset(rgbConfig.headRGBSingleColor,0,\
      sizeof(rgbConfig.headRGBSingleColor));
    memset(rgbConfig.tailRGBSingleColor,0,\
      sizeof(rgbConfig.tailRGBSingleColor));
    rgbConfig.headRGBAnim=0;
    rgbConfig.tailRGBAnim=0;
    rgbConfig.animSpeed=5;
    rgbConfig.pinARGBNum=5;
    rgbConfig.pinBRGBNum=5;
    rgbConfig.autoHead=0;
    rgbConfig.standbyShow=0;
    EEPROM.put(0,rgbConfig);
    EEPROM.commit();
  }
  
  server.onNotFound([]() {
    if (server.uri() == "/favicon.ico") {
      server.sendHeader("Location", "/res?file=favicon.ico");
      server.send(301);
    } else {
      server.send(404, "text/plain", "404 Not Found");
    }
  });
  
  server.on("/updateConfig",handleUpdateConfig);
  server.on("/getConfig",handleGetConfig);
  server.on("/getAnimList",handleGetAnimList);

  server.on("/getLog", HTTP_GET, []() {
    server.send(200, "text/plain", logStr);
  });
  
  server.on("/getVescOutput", HTTP_GET, []() {
    char buf[100];
    sprintf(buf,"vescOutput %s pitch %f speed %f",vescOutput.c_str(),\
    vescPitch,vescSpeed);
    server.send(200, "text/plain", String(buf));
  });
  
  server.on("/", HTTP_GET, []() {
    File file = SPIFFS.open("/main.html", "r");
    if (!file) {
      server.send(404, "text/plain", "File not found!");
      return;
    }
    server.streamFile(file, "text/html");
    file.close();
  });

  /*
   * 不支持通配符，什么垃圾webserver
   */
  server.on("/res", HTTP_GET, []() {
    if(server.hasArg("file")){
      String filename=server.arg("file");
      //Serial.println(filename);
      File file = SPIFFS.open("/" + filename, "r");
      if (!file) {
        server.send(404, "text/plain", "File not found!");
        return;
      }
      String contentType = getContentType(filename);
      server.streamFile(file, contentType);
      file.close();
    }else{
      server.send(404, "text/plain", "No filename specified!");
    }
  });

  //文件更新
  server.on("/updateFile", HTTP_POST, []() {
      File file = SPIFFS.open("/" + server.upload().filename, "r");
      if (file) {
        int fileSize = file.size();
        file.close();
        server.send(200, "text/plain", "File uploaded successfully. Size: " + String(fileSize) + " bytes.");
      } else {
        Serial.println("Failed to open file for reading");
        server.send(500, "text/plain", "Failed to save uploaded file.");
      }
    },[](){
      HTTPUpload& upload = server.upload();
      if (upload.status == UPLOAD_FILE_START) {
        if (SPIFFS.exists("/" + upload.filename)) {
          SPIFFS.remove("/" + upload.filename);
        }
        File file = SPIFFS.open("/" + upload.filename, "w");
        if (!file) {
          Serial.println("Failed to open file for writing");
          return;
        }
      } else if (upload.status == UPLOAD_FILE_WRITE) {
        File file = SPIFFS.open("/" + upload.filename, "a");
        if (file) {
          file.write(upload.buf, upload.currentSize);
          file.close();
        } else {
          Serial.println("Failed to open file for writing");
        }
      } else if (upload.status == UPLOAD_FILE_END) {
          Serial.printf("File %s uploaded successfully\n", upload.filename.c_str());
      }
  });

  //固件更新
  server.on("/updateFirmware", HTTP_POST, []() {
    server.sendHeader("Connection", "close");
    server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
    ESP.restart();
  }, []() {
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
      Serial.printf("Update: %s\n", upload.filename.c_str());
      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) { //start with max available size
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      /* flashing firmware to ESP*/
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_END) {
      if (Update.end(true)) { //true to set the size to the current progress
        Serial.printf("Update Success: %u\nRebooting...\n", upload.totalSize);
      } else {
        Update.printError(Serial);
      }
    }
  });
  server.begin();

  leds1=new CRGB[rgbConfig.pinARGBNum];
  leds2=new CRGB[rgbConfig.pinBRGBNum];
  FastLED.addLeds<WS2812B, LED1_GPIO,GRB>(leds1, rgbConfig.pinARGBNum);
  FastLED.addLeds<WS2812B, LED2_GPIO,GRB>(leds2, rgbConfig.pinBRGBNum);
  
  vescSerial.begin(57600);
  xTaskCreatePinnedToCore(task3, "task3",4096,NULL,1,NULL,1);//绑协处理器

  mutex1 = xSemaphoreCreateMutex();
  mutex2 = xSemaphoreCreateMutex();
}


struct TaskInit{
  char taskId;
  char duty;
  char isDel;
  char isExit;
};

unsigned long previousMillis = 0;
const long interval = 500; 
char times=0;
TaskHandle_t task1Handle = NULL;
TaskHandle_t task2Handle = NULL; 
char taskArray[2][2]={0,0,0,0};
TaskInit task1Tmp;
TaskInit task2Tmp;

/**
 * 循环模板
 */


/**
 * 搞一个雨滴
 * 随机落点
 * 随机强度，强的涟漪大，亮度渐强渐弱
 * 随机颜色
 */


void sinWave3(TaskReqInfo req){
  ANIM_START
  char animBeatNum;
  uint8_t sinBeat;
  uint8_t sinBeat2;
  uint8_t sinBeat3;
  while(ANIM_LOOP){
    animBeatNum=ANIM_WAVEBEAT;
    
    sinBeat = beatsin8(animBeatNum, 0, req.ledNum - 1, 0, 0);
    sinBeat2 = beatsin8(animBeatNum, 0, req.ledNum - 1, 0, \
      floor(pow(2,8)/3));
    sinBeat3 = beatsin8(animBeatNum, 0, req.ledNum - 1, 0, \
      floor(pow(2,8)/3*2));

    if(*req.onOff){
      req.leds[sinBeat]   = CRGB::Blue;
      req.leds[sinBeat2]  = CRGB::Red;
      req.leds[sinBeat3]  = CRGB::White;
    }
    ANIM_SETBRIGHTNESS
    FastLED.show();
    //这个13没什么依据，就是根据调试结果的感觉来的
    //理论上来说 1/3 够，但是拖尾可能刚消失又被填充了
    //翻一倍 1/6 能和上一个波分离一半的位置
    //还要考虑人眼残影的影响，再翻一倍 1/12
    //还要再考虑前面这些操作所耗用的时间 1/13
    //哈哈，自圆其说
    fadeToBlackBy(req.leds, req.ledNum, ANIM_WAVEFADE(animBeatNum,13));
    ANIM_WAVEDELAY
  }
}

void sinWave2(TaskReqInfo req){
  ANIM_START
  while(ANIM_LOOP){
    uint8_t beatA = beatsin8(30, 0, 255);
    uint8_t beatB = beatsin8(60, 0, 255);
    if(*req.onOff){
      fill_rainbow(req.leds, req.ledNum, (beatA+beatB)/2, 8);
    }else{
      fill_solid(req.leds,req.ledNum,CRGB::Black);
    }
    ANIM_SETBRIGHTNESS
    FastLED.show();
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

/**
 * 正弦遍历可以让动画更流畅、柔和，比等时间间隔的遍历更好
 * 波形映射示意图如下
 * https://github.com/FastLED/FastLED/wiki/FastLED-Wave-Functions
 */
void sinWave1(TaskReqInfo req){
  ANIM_START
  while(ANIM_LOOP){
    uint8_t posBeat = beatsin8(30, 0, req.ledNum - 1, 0, 0);
    uint8_t posBeat2 = beatsin8(60, 0, req.ledNum - 1, 0, 0);
    
    uint8_t colBeat  = beatsin8(45, 0, 255, 0, 0);
    if(*req.onOff){
      /**
       * 两个不同频率的正弦波叠加
       * 相当于一个周期为2pi的和一个周期为pi的叠加
       */
      req.leds[(posBeat + posBeat2) / 2]  = CHSV(colBeat, 255, 255);
    }
    ANIM_SETBRIGHTNESS
    FastLED.show();
    fadeToBlackBy(req.leds, req.ledNum, 10);
    vTaskDelay(5 / portTICK_PERIOD_MS);
  }
}

//随机彩虹
void randomRainbowApp(TaskReqInfo req){
  ANIM_START
  uint8_t   noiseData[req.ledNum];
  CRGBPalette16 party = PartyColors_p;
  uint8_t   octaveVal   = 2; //噪声的粗细
  uint16_t  xVal        = 0;
  int       scaleVal    = 50;//噪声的幅度
  uint16_t  timeVal     = 0;
  while(ANIM_LOOP){
    timeVal = millis() / 4;
    memset(noiseData, 0, req.ledNum);
    /**
     * 产生一个随时间变化而变化的随机数组
     */
    fill_raw_noise8(noiseData, req.ledNum, octaveVal, \
      xVal, scaleVal, timeVal);
    for (int i = 0; i < req.ledNum; i++) {
      /**
       * 通过随机数组挑选 调色盘中的颜色以及亮度
       */
      if(*req.onOff){
        req.leds[i] = ColorFromPalette(party, noiseData[i], \
          noiseData[req.ledNum - i - 1]);
      }else{
        req.leds[i] = CRGB::Black;
      }
      
    }
    ANIM_SETBRIGHTNESS
    FastLED.show();
    ANIM_DELAY
  }
}

//警车闪烁
//固定颜色、频率
void policeLightApp(TaskReqInfo req){
  ANIM_START
  char delay1=50;
  char delay2=250;
  
  while(ANIM_LOOP){
    ANIM_SETBRIGHTNESS
    if(*req.onOff){
      //循环两次
      for(char a=0;a<2;a++){
        //左闪4次红
        for(char b=0;b<4;b++){
          for(char c=0;c<req.ledNum/2;c++){
            req.leds[c] = CRGB::DarkRed;
          }
          FastLED.show();
          vTaskDelay(delay1 / portTICK_PERIOD_MS);
          for(char c=0;c<req.ledNum/2;c++){
            req.leds[c] = CRGB::Black;
          }
          FastLED.show();
          vTaskDelay(delay1 / portTICK_PERIOD_MS);
        }
        //右闪4次蓝
        for(char b=0;b<4;b++){
          for(char c=req.ledNum/2;c<req.ledNum;c++){
            req.leds[c] = CRGB::Blue;
          }
          FastLED.show();
          vTaskDelay(delay1 / portTICK_PERIOD_MS);
          for(char c=req.ledNum/2;c<req.ledNum;c++){
            req.leds[c] = CRGB::Black;
          }
          FastLED.show();
          vTaskDelay(delay1 / portTICK_PERIOD_MS);
        }
      }
      vTaskDelay(delay2 / portTICK_PERIOD_MS);
      //一起闪8次
      for(char a=0;a<8;a++){
        for(int b=0;b<req.ledNum;b++){
          if(b<req.ledNum/2){
            req.leds[b] = CRGB::DarkRed;
          }else{
            req.leds[b] = CRGB::Blue;
          }
        }
        FastLED.show();
        vTaskDelay(delay2 / portTICK_PERIOD_MS);
        for(int b=0;b<req.ledNum;b++){
          req.leds[b] = CRGB::Black;
        }
        FastLED.show();
        vTaskDelay(delay2 / portTICK_PERIOD_MS);
      }
    }else{
      vTaskDelay(delay2 / portTICK_PERIOD_MS);
    }
  }
}

//晶格玻璃
void crystalglassApp(TaskReqInfo req){
  ANIM_START
  char flag=0;
  while(ANIM_LOOP){
    for(int i=0;i<req.ledNum;i++){
      if(*req.onOff){
        if(!flag){
          if(i%2==0){
            req.leds[i].r=req.rgb[0];
            req.leds[i].g=req.rgb[1];
            req.leds[i].b=req.rgb[2];
          }else{
            req.leds[i] = CRGB::Black;
          }
        }else{
          if(i%2!=0){
            req.leds[i].r=req.rgb[0];
            req.leds[i].g=req.rgb[1];
            req.leds[i].b=req.rgb[2];
          }else{
            req.leds[i] = CRGB::Black;
          }
        }
      }else{
        req.leds[i] = CRGB::Black;
      }
    }
    ANIM_SETBRIGHTNESS
    FastLED.show();
    ANIM_DELAY
    flag=flag?0:1;
  }
}

//乒乓，顾名思义，左边到右边，右边到左边
void pingpongApp(TaskReqInfo req){
  ANIM_START
  char animBeatNum;
  uint8_t sinBeat;
  while(ANIM_LOOP){
    animBeatNum=ANIM_WAVEBEAT;
    sinBeat=beatsin8(animBeatNum, 0, req.ledNum-1,0,0);
    if(*req.onOff){
      req.leds[sinBeat].r=req.rgb[0];
      req.leds[sinBeat].g=req.rgb[1];
      req.leds[sinBeat].b=req.rgb[2];
    }else{
      req.leds[sinBeat] = CRGB::Black;
    }
    ANIM_SETBRIGHTNESS
    FastLED.show();
    fadeToBlackBy(req.leds, req.ledNum, ANIM_WAVEFADE(animBeatNum,4));
    ANIM_WAVEDELAY
  }
}

//呼吸灯
void breathingApp(TaskReqInfo req){
  ANIM_START
  int brightness=0;
  char flag=0;
  ANIM_SETBRIGHTNESS
  while(ANIM_LOOP){
    for(int i=0;i<req.ledNum;i++){
      if(*req.onOff){
        req.leds[i].r=req.rgb[0];
        req.leds[i].g=req.rgb[1];
        req.leds[i].b=req.rgb[2];
        CHSV hsv = rgb2hsv_approximate(req.leds[i]);
        //亮度受限于全局亮度，所以不用担心不受全局亮度的管理
        hsv.v = brightness;
        req.leds[i] = hsv;
      }else{
        req.leds[i] = CRGB::Black;
      }
    }
    FastLED.show();
    if(!flag){
      brightness += 5;
      if (brightness >= 255) {
        flag=1;
      }
    }else{
      brightness -= 5;
      if (brightness <= 0) {
        flag=0;
      }
    }
    ANIM_DELAY
  }
}

//彩虹流
void rainbowStreamApp(TaskReqInfo req){
  ANIM_START
  uint8_t hue = 0;
  while(ANIM_LOOP){
    if(*req.onOff){
      fill_rainbow(req.leds,req.ledNum,hue,7); //7种颜色
      hue+=2;
    }else{
      for(int i=0;i<req.ledNum;i++){
        req.leds[i]=CRGB::Black;
      }
    }
    ANIM_SETBRIGHTNESS
    FastLED.show();
    ANIM_DELAY
  }
}

//左右向内扫描
void scanLeftAndRight2CenterApp(TaskReqInfo req){
  ANIM_START
  int i,j;
  
  while(ANIM_LOOP)
  {
    ANIM_SETBRIGHTNESS
    if(*req.onOff){   
      i = 0;
      j = req.ledNum - 1;
      while (i < j) {
        req.leds[i].r=req.rgb[0];
        req.leds[i].g=req.rgb[1];
        req.leds[i].b=req.rgb[2];
        
        req.leds[j].r=req.rgb[0];
        req.leds[j].g=req.rgb[1];
        req.leds[j].b=req.rgb[2];
              
        FastLED.show();
        ANIM_DELAY
        req.leds[i] = CRGB::Black;
        req.leds[j] = CRGB::Black;
        i++; // 扫描点向中间移动
        j--;
        FastLED.show();
        ANIM_DELAY
      }
    }
  
    for (int k = 0; k < req.ledNum; k++) {
      req.leds[k] = CRGB::Black;
    }
    FastLED.show();
    ANIM_DELAY
  }
}

//顾名思义，该应用只有一种颜色，不属于动画
void singleColorApp(TaskReqInfo req){
  ANIM_START
  
  while(ANIM_LOOP){
    for(int i=0;i<req.ledNum;i++){
      if(*req.onOff){
        req.leds[i].r=req.rgb[0];
        req.leds[i].g=req.rgb[1];
        req.leds[i].b=req.rgb[2];
      }else{
        req.leds[i]=CRGB::Black;
      }
    }
    ANIM_SETBRIGHTNESS
    FastLED.show();
    //不属于动画，不需要频繁刷新
    vTaskDelay(500 / portTICK_PERIOD_MS);
  }
}

//空，当你想清空灯带显示的时候运行此app
void blankApp(TaskReqInfo req){
  ANIM_START
  while(ANIM_LOOP){
    for(int i=0;i<req.ledNum;i++){
      req.leds[i]=CRGB::Black;
    }
    FastLED.show();
    //不属于动画，不需要频繁刷新
    vTaskDelay(500 / portTICK_PERIOD_MS);
  }
}

/*
每一个灯光效果都要完成 1）当关闭时 2）亮度调整时 的动作
灯光效果不能死循环，需要有条件退出，这样才可以根据情景显示
这样做的目的是，为了应对当板头板尾职责不变的时候，情景发生改变
灯光效果要保持纯粹，即只做灯光效果，其他什么也不做
*/
void doTask(TaskReqInfo req){
  Serial.printf("from %s R: %d G: %d B: %d onoff %d\n",req.taskName,
      req.rgb[0],req.rgb[1],req.rgb[2],*req.onOff);

  //app和场景的选择实现处
  while((*req.isDel)==0){
    *req.isExit=0;

    /**
     * 使用互斥避免因为另一个任务提前交替导致两个线程同时
     * 对一个灯带进行操作进而产生无法预测的结果
     */
    SemaphoreHandle_t *mutexP;
    
    if(!strncmp(req.taskName,"task1",5)){
      mutexP=&mutex1;
    }else{ //task2
      mutexP=&mutex2;
    }

    if(xSemaphoreTake(*mutexP, portMAX_DELAY) == pdTRUE) {
      //情景
      if(currContextualModel==CONTEXT_STANDBY){
        req.withContext=CONTEXT_STANDBY;
        blankApp(req);
        continue;
      }
    
      //app
      req.withContext=CONTEXT_NONE;
      animMapArray[*req.rgbAnim](req);
    }
    
    xSemaphoreGive(*mutexP);
    *req.isExit=1;
  }

  /**
   * 任务执行完不让任务自己结束而是等待外部任务来结束
   * 这样做的好处：
   * 防止因为任务自己已经结束但外部不知晓的情况下再次调用vTaskDelete()，
   * 这种情况会导致系统奔溃（在这里翻船，检查了一天）
   * 免去了在vTaskDelete()之前检查任务的状态，如果任务在结束后等待，那么任务
   * 必然存在
   */
  while(true){vTaskDelay(20 / portTICK_PERIOD_MS);}
}

char *taskName1="task1";
char *taskName2="task2";
/*
填充每个led显示请求的基本信息，当配置在网页被更改的时候，
应该要重新进入这里填充，但一般情况下，板头板尾职责确定一次，
信息只需要填充一次
*/
void task12(void *param){
  TaskInit *t = (TaskInit *)param;
  char duty=t->duty;
  char taskId=t->taskId;
  
  TaskReqInfo req;
  req.taskName=taskId?taskName2:taskName1;
  req.leds=taskId?leds2:leds1;
  req.ledNum=taskId?rgbConfig.pinBRGBNum:rgbConfig.pinARGBNum;
  req.brightness=&rgbConfig.brightness;
  req.animSpeed=&rgbConfig.animSpeed;
  req.isDel=&t->isDel;
  req.isExit=&t->isExit;
  
  if(duty==0){
    req.rgb=rgbConfig.headRGBSingleColor;
    req.rgbAnim=&rgbConfig.headRGBAnim;
    req.onOff=&rgbConfig.headOnOff;
    req.isHead=1;
  }else{
    req.rgb=rgbConfig.tailRGBSingleColor;
    req.rgbAnim=&rgbConfig.tailRGBAnim;
    req.onOff=&rgbConfig.tailOnOff;
    req.isHead=0;
  }
  doTask(req);
}
void task3(void *param){
  char tmpMax=100;
  char tmp[tmpMax];
  int index=0;
  while(true){
    if(vescSerial.available()) {
      char inChar = (char)vescSerial.read();
      if(index==tmpMax){
        index=0;
        continue;
      }

      if(inChar=='\n' && index>=2){
        byte lowChecksum=tmp[index-2];
        byte highChecksum=tmp[index-1];
        int checksum = lowChecksum + (highChecksum << 8);

        int sum = 0;
        for (int i = 0; i < index - 2; i++) {
          sum += tmp[i];
        }
        sum %= 65536;
        sum ^= 0xFFFF;

        tmp[index-2]='\n';
        tmp[index-1]='\0';
        index = 0;
        if (sum != checksum) {
          //数据包错误
          continue;
        }
        //数据包正确，处理想要做的事
        vescOutput=String(tmp);
        calVescOutput();
      }else if(inChar=='\0'){
        //do nothing
      }else{
        tmp[index++]=inChar;
      }
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}
/*
 * a 任务，b 职责
 */
void killAndCreateTask(int a,int b){
  eTaskState taskState;
  
  //防重入
  if(taskArray[a][b]==1){
    //检查任务的存活状态
    if(a==0){
      taskState=eTaskGetState(task1Handle);
    }else if(a==1){
      taskState=eTaskGetState(task2Handle);
    }
    if (taskState == eDeleted) {
      //任务已经死亡，放过
    }else{
      //任务还存在，直接退出
      return;
    }
    
  }
  taskArray[a][b]=1;
  taskArray[a][(b==0?1:0)]=0;
  
  Serial.printf("killAndCreateTask %d %d\n",a,b);
  
  if(a==0){
    if(task1Handle!=NULL){
      task1Tmp.isDel=1;
      while(task1Tmp.isExit==0){delay(5);}
      vTaskDelete(task1Handle);
      task1Handle=NULL;
    }
    task1Tmp.taskId=a;
    task1Tmp.duty=b;
    task1Tmp.isDel=0;
    task1Tmp.isExit=1;

    xTaskCreatePinnedToCore(task12, 
    "task1",
    4096, 
    (void*)&task1Tmp, 1, &task1Handle, 0);//绑主处理器
  }else{
    if(task2Handle!=NULL){
      task2Tmp.isDel=1;
      while(task2Tmp.isExit==0){delay(5);}
      vTaskDelete(task2Handle);
      task2Handle=NULL;
    }
    task2Tmp.taskId=a;
    task2Tmp.duty=b;
    task2Tmp.isDel=0;
    task2Tmp.isExit=1;
    
    xTaskCreatePinnedToCore(task12, 
    "task2",
    4096, 
    (void*)&task2Tmp, 1, &task2Handle, 0);//绑主处理器
  }
}

/*
在灯条还未指定任何职责时，清空灯条的显示，避免因为灯条信号线
的干扰造成的异常显示，在指定职责后
*/
void clearLedsAfterCheck(){
  if(task1Handle==NULL && task2Handle==NULL){
    FastLED.clear();
    FastLED.show();
  }
}

void loop(void) {
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;
    if(times++==2){
      //Serial.println("esp32 alive~");
      times=0;
    }
    
    //以下代码计算当前处于哪种情景之下
    if(!rgbConfig.standbyShow && abs(vescPitch)>10){
      currContextualModel=CONTEXT_STANDBY;
    }else{
      currContextualModel=CONTEXT_NONE;
    }

    /*
    以下代码只负责 任务中职责的切换，不涉及职责的内容！
    简单说就是 只告诉某条led，你的职责是什么，板头还是板尾
    但具体你怎么显示，情景也好，动画也好，都跟我无关
    */
    if(rgbConfig.reverse==0){  //先判头尾是否符合理想中的方向
      if(rgbConfig.autoHead==0){  //是否打开了自动根据前进方向切灯
        //默认的方案，此时没有所谓头尾，不过就是随意指定某led某职责
        killAndCreateTask(0,0);  
        killAndCreateTask(1,1);
      }else{
        //只有当串口数据做判断条件时，才有了头尾的概念
        if(abs(vescPitch)<5){
          if(vescSpeed>0){
            killAndCreateTask(0,0);
            killAndCreateTask(1,1);
          }else if(vescSpeed<0){
            killAndCreateTask(0,1);
            killAndCreateTask(1,0);
          }
        }
      }
    }else{
      if(rgbConfig.autoHead==0){
        killAndCreateTask(0,1);
        killAndCreateTask(1,0);
      }else{
        if(abs(vescPitch)<5){
          if(vescSpeed>0){
            killAndCreateTask(0,1);
            killAndCreateTask(1,0);
          }else if(vescSpeed<0){
            killAndCreateTask(0,0);
            killAndCreateTask(1,1);
          }
        }
      }
    }
    clearLedsAfterCheck();
  }
  server.handleClient();
  
  delay(1);
}
