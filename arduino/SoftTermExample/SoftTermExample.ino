#include "SoftModemLong.h" //ライブラリをインクルード
#include <ctype.h>

SoftModemLong modem; //SoftModemのインスタンスを作る
//int delayTime = 1000;
int txLed = 13;
int rxLed = 12;

void setup()
{
  Serial.begin(57600);
  modem.begin(); // setup()でbeginをコールする
  pinMode(txLed, OUTPUT);
  pinMode(rxLed, OUTPUT);
}

void loop()
{
  while (modem.available()) { //iPhoneからデータ受信しているか確認
    blinkLed(rxLed);
    int c = modem.read(); //1byteリード
    if (isprint(c)) {
      Serial.println((char)c); //PCに送信
    }
    else {
      Serial.print("("); //表示できない文字はHexで表示
      Serial.print(c, HEX);
      Serial.print(")");
    }
  }
  while (Serial.available()) { //PCからデータを受信しているか確認
    blinkLed(txLed);
    char c = Serial.read(); //1byteリード
    modem.write(c); //iPhoneに送信

  }
  
//      blinkLed();
//  modem.write('a');
//  delayMicroseconds(delayTime);
//  modem.write('b');
//  delayMicroseconds(delayTime);
//  modem.write('c');
//  delayMicroseconds(delayTime);
//  modem.write('d');
//  delayMicroseconds(delayTime);
//  modem.write('e');
//  delayMicroseconds(delayTime);
//  modem.write(' ');
//  delay(100);
}

void blinkLed(int led){
  digitalWrite(led, HIGH);
  delay(100);
  digitalWrite(led, LOW);
  delay(100);
}
