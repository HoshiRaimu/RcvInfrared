/*
 * File:   main.c
 * Author: raimu
 *
 * Created on 2021/03/16, 21:40
 */

// CONFIG1
#pragma config FOSC = INTOSC    // Oscillator Selection (INTOSC oscillator: I/O function on CLKIN pin)
#pragma config WDTE = OFF       // Watchdog Timer Enable (WDT disabled)
#pragma config PWRTE = ON       // Power-up Timer Enable (PWRT enabled)
#pragma config MCLRE = OFF      // MCLR Pin Function Select (MCLR/VPP pin function is digital input)
#pragma config CP = OFF         // Flash Program Memory Code Protection (Program memory code protection is disabled)
#pragma config CPD = OFF        // Data Memory Code Protection (Data memory code protection is disabled)
#pragma config BOREN = ON       // Brown-out Reset Enable (Brown-out Reset enabled)
#pragma config CLKOUTEN = OFF   // Clock Out Enable (CLKOUT function is disabled. I/O or oscillator function on the CLKOUT pin)
#pragma config IESO = OFF       // Internal/External Switchover (Internal/External Switchover mode is disabled)
#pragma config FCMEN = OFF      // Fail-Safe Clock Monitor Enable (Fail-Safe Clock Monitor is disabled)

// CONFIG2
#pragma config WRT = OFF        // Flash Memory Self-Write Protection (Write protection off)
#pragma config PLLEN = OFF      // PLL Enable (4x PLL disabled)
#pragma config STVREN = OFF     // Stack Overflow/Underflow Reset Enable (Stack Overflow or Underflow will not cause a Reset)
#pragma config BORV = LO        // Brown-out Reset Voltage Selection (Brown-out Reset Voltage (Vbor), low trip point selected.)
#pragma config LVP = OFF        // Low-Voltage Programming Enable (High-voltage on MCLR/VPP must be used for programming)

#include <xc.h>
#include <stdint.h>
#include <stdio.h>

#define _XTAL_FREQ 4000000

#define I2C_ACK 0x00
#define I2C_NACK 0xff
#define LCD_I2C_ADDRESS 0x7c

// LCDモジュール表示制御関数
void lcdInitialize(void);               // LCD初期化
void lcdClearDisplay(void);             // ディスプレイ全消去
void lcdSendCommandData(uint8_t);       // コマンド送信
void lcdSendCharacterData(uint8_t);     // 1文字表示
void lcdLocateCursor(uint8_t,uint8_t);  // カーソル位置指定
 
// LCDモジュールI2Cプロトコル関数
void lcdI2CProtocol(uint8_t, uint8_t, uint8_t);
 
// I2Cプロトコル各信号の生成関数
void    i2cProtocolStart(void);        // スタートビット生成
void    i2cProtocolStop(void);         // ストップビット生成
void    i2cProtocolSendData(uint8_t);  // 1バイトデータ送信
uint8_t i2cProtocolCheckAck(void);     // ACK信号チェック

void main(void) {
    //PICマイコンの設定
    OSCCON = 0b01101000;
    ANSELA = 0b00000000;    //すべてデジタル
    TRISA  = 0b00110110;    //RA1とRA2とRA4とRA5を入力に設定
    
    //Timer1の設定
    T1CON = 0b00000000;      //命令クロックを選択、プリスケール値1:1 、Timer1を停止
    TMR1IE  = 0;             // Timer1 オーバーフロー割り込みを無効
    
    //I2Cの設定
    SSP1STAT = 0x80;   // クロック信号は100kHzを使用
    SSP1CON1 = 0x28;   // I2C通信のマスターモードを有効化
    SSP1CON3 = 0x00;   // CON3はデフォルト設定
    SSP1ADD  = 0x09;   //クロック信号速度を100kHzに設定
    
    // LCDモジュール電源安定化時間待ち
    __delay_ms(100);
     
    // LCD初期化
    lcdInitialize();
    
    uint8_t rcv_data[4] = {0, 0, 0, 0};
    
    while(1){
        if(RA4) {
            __delay_ms(50);     //安定化待ち
            LATA0 = 1;          //LED点灯
            __delay_ms(1000);
            
            //リーダーコードの待ち
            while(RA5);
            
            //リーダーコードの長さを測定
            //Timer1を開始
            TMR1H  = 0;
            TMR1L  = 0;
            TMR1ON = 1;
            while(!RA5);
            
            if((TMR1H >= 0x1F) && (TMR1H <= 0x27)) {    //HIGHの時間が8ms-10.0msをNECフォーマットと判断
                //LOWになるのを待つ
                while(RA5);
                
                for(int i = 0; i < 4; i++) {
                    for(int j = 7; j >= 0; j++) {
                        //HIGHの時間つぶし
                        while(!RA5);
                        
                        //LOWの時間を測定
                        TMR1H  = 0;
                        TMR1L  = 0;
                        TMR1ON = 1;
                        while(RA5);
                        
                        //LOWの時間が0x04よりも長ければ1と判断
                        if(TMR1H >= 0x04) {
                            rcv_data[i] = rcv_data[i] | (uint8_t)(0b00000001 << j);
                        }
                    }
                }
                TMR1ON = 0;
                LATA0 = 0; 
            }
            
     
        }
        
        lcdLocateCursor(1, 1);
        lcdSendCharacterData((uint8_t)(rcv_data[0] | 0b00110000));
        lcdLocateCursor(2, 1);
        lcdSendCharacterData((uint8_t)(rcv_data[1] | 0b00110000));
        lcdLocateCursor(3, 1);
        lcdSendCharacterData(rcv_data[2] | 0b00110000);
        lcdLocateCursor(4, 1);
        lcdSendCharacterData(rcv_data[3] | 0b00110000);  
    }
    return;
}

void i2cProtocolStart() {
    // SSP1CON2レジスタのSENビットを1に設定すると
    // スタートコンディションが生成される
    // 発行が完了するとSSP1IFが1になるのでwhile文で待つ
    
    SSP1IF = 0;
    SSP1CON2bits.SEN = 1;
    while(SSP1IF == 0) {};
    SSP1IF = 0;
    
    return;
}

void i2cProtocolStop() {
    // SSP1CON2レジスタのPENビットを1に設定すると
    // ストップコンディションが生成される
    // 発行が完了するとSSP1IFが1になるのでwhile文で待つ
    
    SSP1IF = 0;
    SSP1CON2bits.PEN = 1;
    while(SSP1IF == 0) {};
    SSP1IF = 0;
    
    return;
}

void i2cProtocolSendData(uint8_t data) {
    // SSP1BUFに送信したいデータをセットすると、そのデータが送信される
    // 発行が完了するとSSP1IFが1になるのでwhile文で待つ
    
    SSP1IF = 0;
    SSP1BUF = data;
    while(SSP1IF == 0) {};
    SSP1IF = 0;
    
    return;
}

uint8_t i2cProtocolCheckAck() {
    uint8_t ack_status;
    
    if(SSP1CON2bits.ACKSTAT) {
        ack_status = I2C_NACK;
    }else {
        ack_status = I2C_ACK;
    }
    
    return ack_status;
}

void lcdI2CProtocol(uint8_t address, uint8_t cont_code, uint8_t data) {
    i2cProtocolStart();                 //スタートコンディション
    i2cProtocolSendData(address);       //アドレス送信
    i2cProtocolSendData(cont_code);     //制御コード送信
    i2cProtocolSendData(data);          //データ送信
    i2cProtocolStop();                  //ストップコンディション
    
    return;
}

void lcdSendCommandData(uint8_t command) {
    // コマンドを送信する場合の制御コードは0x00
    lcdI2CProtocol(LCD_I2C_ADDRESS, 0x00, command);
    
    // ウエイト
    //   データシートではウエイト時間は26.3us以上になっているが、
    //   それより長くしないと初期化できないケースがあるため1msのウエイトを入れる
    __delay_ms(1);
    
    return;
}

void lcdSendCharacterData(uint8_t data) {
    // 表示文字のデータを送信する場合の制御コードは0x40
    lcdI2CProtocol(LCD_I2C_ADDRESS, 0x40, data);
    
    // ウエイト
    //   文字表示の場合はウエイトを入れなくても動作しているが
    //   表示されない場合は1ms程度のウエイトを入れる
    // __delay_ms(1);
    
    return;
}

void lcdInitialize() {
    // 初期化コマンド送信
    lcdSendCommandData(0x38); // 2行モードに設定
    lcdSendCommandData(0x39); // 拡張コマンド選択
    lcdSendCommandData(0x14); // 内部クロック周波数設定
    lcdSendCommandData(0x70); // コントラスト設定(C3:C0 = 0b0000に設定)
    lcdSendCommandData(0x56); // 電源電圧が3.3VなのでBooster=ONに設定。コントラスト設定はC5:C4 = 0b10
    lcdSendCommandData(0x6c); // オペアンプのゲイン設定
    
    // モジュール内電源安定化のための時間待ち
    __delay_ms(200);
    
    // 初期化コマンド続き
    lcdSendCommandData(0x38); // 通常コマンド選択
    lcdSendCommandData(0x0c); // ディスプレイ表示
    lcdSendCommandData(0x01); // ディスプレイ表示内容クリア
    
    return;
}

void lcdClearDisplay() {
    lcdSendCommandData(0x01);
    
    return;
}

void lcdLocateCursor(uint8_t pos_x, uint8_t pos_y) {
    lcdSendCommandData(0x80 + 0x40 * (pos_y - 1) + (pos_x - 1));
    
    return;
}

void putch(char character) {
    lcdSendCharacterData(character);
    
    return;
}