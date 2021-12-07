/*
 * Project name: T-rex-duino
 * Description: T-rex game from Chrome browser rewritten for Arduino
 * Project page: https://github.com/AlexIII/t-rex-duino
 * Author: github.com/AlexIII
 * E-mail: endoftheworld@bk.ru
 * License: MIT


//PRESS A button on start to clear HIGH SCORE
//PRESS B button on start to autoplay


/* Hardware Connections */
#define VERT_DISPLAY_OFFSET 20

/* Misc. Settings */
//#define PRINT_DEBUG_INFO

bool autoPlay = false;

/* Game Balance Settings */
#define PLAYER_SAFE_ZONE_WIDTH 32 //minimum distance between obstacles (px)
#define CACTI_RESPAWN_RATE 50 //lower -> more frequent, max 255
#define GROUND_CACTI_SCROLL_SPEED 3 //pixels per game cycle
#define PTERODACTY_SPEED 5 //pixels per game cycle
#define PTERODACTY_RESPAWN_RATE 255 //lower -> more frequent, max 255
#define INCREASE_FPS_EVERY_N_SCORE_POINTS 256 //better to be power of 2
#define LIVES_START 3
#define LIVES_MAX 5
#define SPAWN_NEW_LIVE_MIN_CYCLES 800
#define DAY_NIGHT_SWITCH_CYCLES 1024 //better to be power of 2
#define TARGET_FPS_START 23
#define TARGET_FPS_MAX 48 //gradually increase FPS to that value to make the game faster and harder

/* Display Settings */
#define LCD_HEIGHT 64U
#define LCD_WIDTH 128U

/* Includes */
#include <ESP_EEPROM.h>
#include "array.h"
#include "TrexPlayer.h"
#include "Ground.h"
#include "Cactus.h"
#include "Pterodactyl.h"
#include "HeartLive.h"
#include "lib/ESPboyInit.h"
#include "lib/ESPboyInit.cpp"


/* Defines and globals */
#define EEPROM_HI_SCORE 16 //2 bytes
#define LCD_BYTE_SZIE (LCD_WIDTH*LCD_HEIGHT/8)

ESPboyInit myESPboy;

uint8_t lcdBuff[LCD_HEIGHT*LCD_WIDTH/8];
bool night = false;

static uint16_t hiScore = 0;
static bool firstStart = true;


void writePixel(int16_t x, int16_t y, uint8_t color){
  if (x < 0 || x > (LCD_WIDTH-1) || y < 0 || y > (LCD_HEIGHT-1)){
    return;
  }
  uint8_t row = (uint8_t)y / 8;
  if (color){
    lcdBuff[(row*LCD_WIDTH) + (uint8_t)x] |=   _BV((uint8_t)y % 8);
  }
  else{
    lcdBuff[(row*LCD_WIDTH) + (uint8_t)x] &= ~ _BV((uint8_t)y % 8);
  }
}

void adafruitDrawBitmap(int16_t x, int16_t y, const uint8_t bitmap[], int16_t w, int16_t h, uint16_t color) {

  int16_t byteWidth = (w + 7) / 8; // Bitmap scanline pad = whole byte
  uint8_t byte = 0;
  
  for (int16_t j = 0; j < h; j++, y++) {
    for (int16_t i = 0; i < w; i++) {
      if (i & 7)
        byte <<= 1;
      else
        byte = pgm_read_byte(&bitmap[j * byteWidth + i / 8]);
      if (byte & 0x80)
        writePixel(x + i, y, color);
    }
  }
}



void display_display(){ 
  static uint16_t oBuffer[LCD_WIDTH*16];
  static uint8_t currentDataByte;
  static uint16_t foregroundColor, backgroundColor, xPos, yPos, kPos, kkPos, addr;

  if(!night){
    backgroundColor = TFT_BLACK;
    foregroundColor = TFT_YELLOW;}
  else{
    backgroundColor = TFT_YELLOW;
    foregroundColor = TFT_BLACK;};
 
  for(kPos = 0; kPos<4; kPos++){  //if exclude this 4 parts screen devision and process all the big oBuffer, EPS8266 resets (
    kkPos = kPos<<1;
    for (xPos = 0; xPos < LCD_WIDTH; xPos++) {
      for (yPos = 0; yPos < 16; yPos++) {    
        if (!(yPos % 8)) currentDataByte = lcdBuff[xPos + ((yPos>>3)+kkPos) * LCD_WIDTH];
        addr =  yPos*LCD_WIDTH+xPos;
            if (currentDataByte & 0x01) oBuffer[addr] = foregroundColor;
            else oBuffer[addr] = backgroundColor;
      currentDataByte = currentDataByte >> 1;
    }
    }
    myESPboy.tft.pushImage(0, VERT_DISPLAY_OFFSET+kPos*16, LCD_WIDTH, 16, oBuffer);
  }
}


/* Misc Functions */

bool isPressedJump() {
  return myESPboy.getKeys()&PAD_UP?1:0;
}

bool isPressedDuck() {
   return myESPboy.getKeys()&PAD_DOWN?1:0;
}

uint8_t randByte() {
  //static uint16_t c = 0xA7E2;
  //c = (c << 1) | (c >> 15);
  //c = (c << 1) | (c >> 15);
  //c = (c << 1) | (c >> 15);
  //c = analogRead(A2) ^ analogRead(A3) ^ analogRead(A4) ^ analogRead(A5) ^ analogRead(A6) ^ analogRead(A7) ^ c;
  return random(255);
}

/* Main Functions */

void renderNumber(BitCanvas& canvas, Point2Di8 point, const uint16_t number) {
  uint16_t base = 10000;
  while(base) {
    const uint8_t digit = (number/base)%10;
    canvas.render(numbers.getSprite(digit, point));
    base /= 10;
    point.x += numbers.getWidth() + 1;
  }
}

void gameLoop(uint16_t &hiScore) {

  delay(0);
  
  VirtualBitCanvas bitCanvas (VirtualBitCanvas::VIRTUAL_WIDTH, lcdBuff, LCD_HEIGHT, LCD_WIDTH, LCD_WIDTH);

  SpawnHold spawnHolder;

  //dinamic sprites
  TrexPlayer trex;
  Ground ground1(-1);
  Ground ground2(63);
  Ground ground3(127);
  Cactus cactus1(spawnHolder);
  Cactus cactus2(spawnHolder);
  Pterodactyl pterodactyl1(spawnHolder);
  HeartLive heartLive;
  const array<SpriteAnimated*, 8> sprites{{&ground1, &ground2, &ground3, &cactus1, &cactus2, &pterodactyl1, &heartLive, &trex}};
  const array<SpriteAnimated*, 3> enemies{{&cactus1, &cactus2, &pterodactyl1}};

  //static sprites
  const Sprite gameOverSprite(&game_overver_bm, {15, 12});
  const Sprite restartIconSprite(&restart_icon_bm, {55, 25});
  const Sprite hiSprite(&hi_score, {44, 0});
  Sprite heartsSprite(&hearts_5x_bm, {95, 8});

  //game variables
  uint32_t prvT = 0;
  bool gameOver = false;
  uint16_t score = 0;
  uint8_t targetFPS = TARGET_FPS_START;
  uint8_t lives = LIVES_START;
  //lcd.setInverse(night);

  //main cycle
  while(1) {
    delay(0);
    //render cycle
    while(1) {
      delay(0);
      //score
      bitCanvas.render(hiSprite);
      renderNumber(bitCanvas, {60, 0}, hiScore);
      renderNumber(bitCanvas, {95, 0}, score);
      bitCanvas.render(heartsSprite);
      //game objects
      for(uint8_t i = 0; i < sprites.size(); ++i)
        bitCanvas.render(*sprites[i]);
      //game over
      if(gameOver) {
        bitCanvas.render(gameOverSprite);
        bitCanvas.render(restartIconSprite);
      }
      //update screen
      display_display();
      //lcd.fillScreen(lcdBuff, LCD_PART_BUFF_SZ, LCD_IF_VIRTUAL_WIDTH(LCD_PART_BUFF_WIDTH, 0));
      if(bitCanvas.nextPart()) break;
    }

    //exit game on game over
    if(gameOver) {
      if(score > hiScore) hiScore = score;
      return;
    }

    //collision detection
    if(!trex.isBlinking() && CollisionDetector::check(trex, enemies.data, enemies.size())) {
      if(lives) {
        trex.blink();
        --lives;
        myESPboy.playTone(20,100);
        //delay(100);
        
      } else {
        trex.die();
        myESPboy.playTone(100,500);
        delay(100);
        myESPboy.playTone(50,300);
        delay(500);
        myESPboy.playTone(10,1100);
        delay(1000);        
        gameOver = true;
        continue;
      }
    }
    if(lives < LIVES_MAX && CollisionDetector::check(trex, heartLive)) {
      ++lives;
      heartLive.eat();
      myESPboy.playTone(200,100);
      //delay(100);
    }

if (!autoPlay){
    //constrols
    if(isPressedJump()) {if(!trex.isJumping())myESPboy.playTone(100,50); trex.jump();}
    bool prstDuck = isPressedDuck();
    if((prstDuck && trex.state != TrexPlayer::DUCK) && !trex.isJumping()) myESPboy.playTone(50,50);
    trex.duck(prstDuck);}
else{
    const int8_t trexXright = trex.bitmap->width + trex.position.x;
    //auto jump
    if(
      (cactus1.position.x <= trexXright + 5 && cactus1.position.x > trexXright) || 
      (cactus2.position.x <= trexXright + 5 && cactus2.position.x > trexXright) || 
      (pterodactyl1.position.y > 30 && pterodactyl1.position.x <= trexXright + 5 && pterodactyl1.position.x > trexXright)
    ) trex.jump();
    //auto duck
    trex.duck(
      (pterodactyl1.position.y <= 30 && pterodactyl1.position.y > 20 && pterodactyl1.position.x <= trexXright + 15 && pterodactyl1.position.x > trex.position.x)
    );
}

    //logic and animation step
    for(uint8_t i = 0; i < sprites.size(); ++i)
      sprites[i]->step();
    //score keeping
    if(score < 0xFFFE) ++score;
    //make game progressively faster
    if(!(score%INCREASE_FPS_EVERY_N_SCORE_POINTS) && targetFPS < TARGET_FPS_MAX) ++targetFPS;
    heartsSprite.limitRenderWidthTo = 6*lives + 1;
    //switch day and night
    if(!(score%DAY_NIGHT_SWITCH_CYCLES)) night = !night;

    const uint8_t frameTime = 1000/targetFPS;
#ifdef PRINT_DEBUG_INFO
    //print CPU load statistics
    const uint32_t dt = millis() - prvT;
    uint32_t left = frameTime > dt? frameTime - dt : 0;
    Serial.print("CPU: ");
    Serial.print(100 - 100*left / frameTime);
    Serial.print("% ");
    Serial.println(dt);
#endif

    //throttle
    while(millis() - prvT < frameTime);
    prvT = millis();
  } 
}

void spalshScreen() {
  adafruitDrawBitmap(0, 0, trexsplash, 128, 64, TFT_YELLOW); 
  display_display();
  while (!myESPboy.getKeys()) delay(100);
}

void setup() {
  Serial.begin(115200);
  EEPROM.begin(100);
  myESPboy.begin("T-rex duino");
  spalshScreen();
  srand((randByte()<<8) | randByte());

  if(myESPboy.getKeys()&PAD_ACT){
    EEPROM.put(EEPROM_HI_SCORE, hiScore);
    EEPROM.commit();}

  if(myESPboy.getKeys()&PAD_ESC)
    autoPlay = true;

  EEPROM.get(EEPROM_HI_SCORE, hiScore);
  if(hiScore == 0xFFFF) hiScore = 0;
}

void loop() {
  if(firstStart || isPressedJump()) {
    firstStart = false;
    gameLoop(hiScore);
    EEPROM.put(EEPROM_HI_SCORE, hiScore);
    EEPROM.commit();
    //wait until the jump button is released
    while(isPressedJump()) delay(100);
    delay(500);
  }
}
