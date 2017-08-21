/*****************************************************************************
 *   A demo example using several of the peripherals on the base board
 *
 *   Copyright(C) 2011, EE2024
 *   All rights reserved.
 *
 ******************************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include "lpc17xx_pinsel.h"
#include "lpc17xx_gpio.h"
#include "lpc17xx_i2c.h"
#include "lpc17xx_ssp.h"
#include "lpc17xx_timer.h"
#include "lpc17xx_uart.h" //All of UART functions is available from CMSIS //remove J54 why?
#include "Math.h"

#include "rotary.h"
#include "pca9532.h"
#include "acc.h"
#include "oled.h"
#include "rgb.h"
#include "led7seg.h"
#include "light.h"
#include "temp.h"
#include "string.h"

#define RED   0x01
#define BLUE  0x02
#define TEMP_HIGH_WARNING 450
#define LIGHT_LOW_WARNING 50
#define ACC_THRESHOLD 7
#define TIME_UNIT 250

#define NOTE_PIN_HIGH() GPIO_SetValue(0, 1<<26);
#define NOTE_PIN_LOW()  GPIO_ClearValue(0, 1<<26);

static char* darkMsg = NULL;
static char* fireMsg = NULL;
static char* monitorMsg = NULL;

const uint32_t lightLoLimit = 50;
const uint32_t lightHiLimit = 972;
static const int FLARE_INTENSITY = 2000;
static uint8_t barPos = 2;
static uint32_t TICK_RATE_ONE_SEC = 1000;

int isDark = 0;
int isFire = 0;
int isWalkingDark = 0;

volatile uint32_t msTicks; // counter for 1ms SysTicks
volatile uint32_t oneSecondTicks, sw4PressedTicks;
int score = 0;
int prevScore = 1000;

char sevenseg_Display;
char sevenseg_Display_Game;
int sevenseg_Count = 0;
int sevenseg_Count_Game = 0;
uint32_t prevVal;

int N[3] = { 0, 0, 0 };
int8_t x = 0, y = 0, z = 0;
uint32_t temperature = 0;
uint32_t light = 0;
unsigned char result[100] = "";
unsigned char scoreStr[100] = "";
unsigned char scoreOled[100] = "";

//Strings for OLED
int8_t OLED_MODE[15];
int8_t OLED_X[15];
int8_t OLED_Y[15];
int8_t OLED_Z[15];
int8_t OLED_LIGHT[15];
int8_t OLED_TEMPERATURE[15];

typedef enum {
	STABLE, MONITOR, GAME
} system_mode_t;
volatile system_mode_t mode;

void setRGB(uint8_t ledMask) {
	if (ledMask == RED) {
		GPIO_SetValue(2, (1 << 0));
	} else {
		GPIO_ClearValue(2, (1 << 0));
	}
	if (ledMask == BLUE) {
		GPIO_SetValue(0, (1 << 26));
	} else {
		GPIO_ClearValue(0, (1 << 26));
	}
}
void blink_RED(){
	setRGB(RED);
	Timer0_Wait(333);
	setRGB(0);
	Timer0_Wait(333);
}

void blink_BLUE(){
	setRGB(BLUE);
	Timer0_Wait(333);
	setRGB(0);
	Timer0_Wait(333);
}

void blink_PURPLE(){
	GPIO_SetValue(2, (1 << 0));
	GPIO_SetValue(0, (1 << 26));
	Timer0_Wait(333);
	GPIO_ClearValue(2, (1 << 0));
	GPIO_ClearValue(0, (1 << 26));
	Timer0_Wait(333);
}

static void moveBar(uint8_t steps, uint8_t dir) {
	uint16_t ledOn = 0;

	if (barPos == 0)
		ledOn = (1 << 0) | (3 << 14);
	else if (barPos == 1)
		ledOn = (3 << 0) | (1 << 15);
	else
		ledOn = 0x07 << (barPos - 2);

	barPos += (dir * steps);
	barPos = (barPos % 16);

	pca9532_setLeds(ledOn, 0xffff);
}

static uint32_t notes[] = { 2272, // A - 440 Hz
		2024, // B - 494 Hz
		3816, // C - 262 Hz
		3401, // D - 294 Hz
		3030, // E - 330 Hz
		2865, // F - 349 Hz
		2551, // G - 392 Hz
		1136, // a - 880 Hz
		1012, // b - 988 Hz
		1912, // c - 523 Hz
		1703, // d - 587 Hz
		1517, // e - 659 Hz
		1432, // f - 698 Hz
		1275, // g - 784 Hz
};

static void playNote(uint32_t note, uint32_t durationMs) {
	uint32_t t = 0;

	if (note > 0) {
		while (t < (durationMs * 1000)) {
			NOTE_PIN_HIGH();
			Timer0_us_Wait(note / 2); // us timer

			NOTE_PIN_LOW()
			;
			Timer0_us_Wait(note / 2);

			t += note;
		}
	} else {
		Timer0_Wait(durationMs); // ms timer
	}
}

static uint32_t getNote(uint8_t ch) {
	if (ch >= 'A' && ch <= 'G')
		return notes[ch - 'A'];

	if (ch >= 'a' && ch <= 'g')
		return notes[ch - 'a' + 7];

	return 0;
}

static uint32_t getDuration(uint8_t ch) {
	if (ch < '0' || ch > '9')
		return 400;
	/* number of ms */
	return (ch - '0') * 200;
}

static uint32_t getPause(uint8_t ch) {
	switch (ch) {
	case '+':
		return 0;
	case ',':
		return 5;
	case '.':
		return 20;
	case '_':
		return 30;
	default:
		return 5;
	}
}

static void playSong(uint8_t *song) {
	uint32_t note = 0;
	uint32_t dur = 0;
	uint32_t pause = 0;
	setRGB(NULL);
	/*
	 * A song is a collection of tones where each tone is
	 * a note, duration and pause, e.g.
	 *
	 * "E2,F4,"
	 */

	while (*song != '\0') {
		note = getNote(*song++);
		if (*song == '\0')
			break;
		dur = getDuration(*song++);
		if (*song == '\0')
			break;
		pause = getPause(*song++);

		playNote(note, dur);
		Timer0_Wait(pause);
	}
}

static uint8_t * champ = (uint8_t*) "C2.C2.C2,D1,E1,D1.D1.D1,";
static uint8_t * win = (uint8_t*) "b1,c1,d1,e1,f1,g1,";

static void init_ssp(void) {
	SSP_CFG_Type SSP_ConfigStruct;
	PINSEL_CFG_Type PinCfg;

	/*
	 * Initialize SPI pin connect
	 * P0.7 - SCK;
	 * P0.8 - MISO
	 * P0.9 - MOSI
	 * P2.2 - SSEL - used as GPIO
	 */
	PinCfg.Funcnum = 2;
	PinCfg.OpenDrain = 0;
	PinCfg.Pinmode = 0;
	PinCfg.Portnum = 0;
	PinCfg.Pinnum = 7;
	PINSEL_ConfigPin(&PinCfg);
	PinCfg.Pinnum = 8;
	PINSEL_ConfigPin(&PinCfg);
	PinCfg.Pinnum = 9;
	PINSEL_ConfigPin(&PinCfg);
	PinCfg.Funcnum = 0;
	PinCfg.Portnum = 2;
	PinCfg.Pinnum = 2;
	PINSEL_ConfigPin(&PinCfg);

	SSP_ConfigStructInit(&SSP_ConfigStruct);

	// Initialize SSP peripheral with parameter given in structure above
	SSP_Init(LPC_SSP1, &SSP_ConfigStruct);

	// Enable SSP peripheral
	SSP_Cmd(LPC_SSP1, ENABLE);

}

static void init_i2c(void) {
	PINSEL_CFG_Type PinCfg;

	/* Initialize I2C2 pin connect */
	PinCfg.Funcnum = 2;
	PinCfg.Pinnum = 10;
	PinCfg.Portnum = 0;
	PINSEL_ConfigPin(&PinCfg);
	PinCfg.Pinnum = 11;
	PINSEL_ConfigPin(&PinCfg);

	// Initialize I2C peripheral
	I2C_Init(LPC_I2C2, 100000);

	/* Enable I2C1 operation */
	I2C_Cmd(LPC_I2C2, ENABLE);
}

static void init_GPIO(void) {

	//Initialize button sw4
	PINSEL_CFG_Type PinCfg;
	PinCfg.Funcnum = 0;
	PinCfg.OpenDrain = 0;
	PinCfg.Pinmode = 0;
	PinCfg.Portnum = 1;
	PinCfg.Pinnum = 31;
	PINSEL_ConfigPin(&PinCfg);
	GPIO_SetDir(1, 1 << 31, 0);

	//Initialize button sw3
	PinCfg.Funcnum = 0;
	PinCfg.OpenDrain = 0;
	PinCfg.Pinmode = 0;
	PinCfg.Portnum = 2;
	PinCfg.Pinnum = 10;
	PINSEL_ConfigPin(&PinCfg);
	GPIO_SetDir(2, 1 << 10, 0);

	//light sensor int
	PinCfg.Portnum = 2;
	PinCfg.Pinnum = 5;
	PinCfg.Funcnum = 0;
	PINSEL_ConfigPin(&PinCfg);
	GPIO_SetDir(2, 1 << 5, 0);


	//Speaker
	GPIO_SetDir(0, 1 << 26, 1); // Main tone signal : P0.26
	GPIO_SetDir(0, 1 << 27, 1); //LM4811-clk
	GPIO_SetDir(0, 1 << 28, 1); //LM4811-up/dn
	GPIO_SetDir(2, 1 << 13, 1); //LM4811-shutdn
	GPIO_ClearValue(0, 1 << 27); //LM4811-clk
	GPIO_ClearValue(0, 1 << 28); //LM4811-up/dn
	GPIO_ClearValue(2, 1 << 13); //LM4811-shutdn

}

//SysTick_Handler - just increment SysTick counter
void SysTick_Handler(void) {
	msTicks++;
}

uint32_t getTicks() {
	return msTicks;
}

//Reads sensors
void readSensors(uint32_t* temperature, uint32_t* light, int8_t* x, int8_t* y,
		int8_t* z) {
	*temperature = temp_read();
	*light = light_read();
	acc_read(&*x, &*y, &*z);
}

//Gets Acceleration Magnitudes
int getAccMagnitude(int x, int y, int z) {
	//	acc_read(&*x, &*y, &*z);
	return sqrt(pow(x, 2) + pow(y, 2) + pow(z, 2));
}


int rotary_dir;
//Sets rotary direction to change between STABLE and GAME modes
void setRotaryDirection(int flag){

	if(flag == 1){
		rotary_dir = rotary_read();
		if((rotary_dir == 1)){
			mode = GAME;
			score = 0;

		}
		if((rotary_dir == 2)){
			mode = STABLE;
		}
		else if((rotary_dir == 0)){
			if(prevVal == 1){
				mode = GAME;
				score = 0;
			}
			else if(prevVal==2){
				mode = STABLE;
			}
		}
		LPC_GPIOINT->IO0IntClr = 1<<24;
		LPC_GPIOINT->IO0IntClr = 1<<25;
	}
}

//Prints 0-F in 7seg
void sevenSeg(){

	//increment 7 segment
	if ((msTicks - oneSecondTicks) >= TICK_RATE_ONE_SEC) {
		oneSecondTicks = msTicks;
		if (sevenseg_Count > 15) { 	//If it is 'F' then rewind to 0
			sevenseg_Count = 0;
		}

		if (sevenseg_Count >= 10) {
			sevenseg_Count += 55;
			sevenseg_Display = '0' + sevenseg_Count;
			led7seg_setChar(sevenseg_Count, FALSE); //Uses character 65 and after
			sevenseg_Count = (sevenseg_Count + 1 - 55);
		}

		else {
			sevenseg_Display = '0' + sevenseg_Count;
			led7seg_setChar(sevenseg_Display, FALSE); //Uses characters '1' and after
			sevenseg_Count = (sevenseg_Count + 1);
		}
	}

}

//EINT3 Interrupt Handler, GPIO0 in NVIC with EINT3
void EINT3_IRQHandler(void) {
	//light sensor
	if ((LPC_GPIOINT ->IO2IntStatF >> 5) & 0x1) {
		isDark = 1;
		light_setLoThreshold(0);
		LPC_GPIOINT ->IO2IntClr |= (1 << 5); //clear GPIO
		light_getIrqStatus(); //Clears I2C
	}

	//rotary switch
	if (((LPC_GPIOINT->IO0IntStatF>>24)&0x1) | ((LPC_GPIOINT->IO0IntStatF>>25)&0x1)) {
		LPC_GPIOINT->IO0IntClr = 1<<24;
		LPC_GPIOINT->IO0IntClr = 1<<25;
		setRotaryDirection(1);
	}

}

void pinsel_uart3(void) {
	PINSEL_CFG_Type PinCfg;
	PinCfg.Funcnum = 2;
	PinCfg.Pinnum = 0;
	PinCfg.Portnum = 0;
	PINSEL_ConfigPin(&PinCfg);
	PinCfg.Pinnum = 1;
	PINSEL_ConfigPin(&PinCfg);
}

void init_uart(void) {
	UART_CFG_Type uartCfg;
	uartCfg.Baud_rate = 115200; //rate of data transfer
	uartCfg.Databits = UART_DATABIT_8;
	uartCfg.Parity = UART_PARITY_NONE;
	uartCfg.Stopbits = UART_STOPBIT_1;
	//pin select for uart3
	pinsel_uart3();

	//Configure Xbee *Baudrate = 9600bps 8N1
	UART_ConfigStructInit(&uartCfg);

	//supply power and setup working parts for uart3
	UART_Init(LPC_UART3, &uartCfg);
	//enable transmit for uart3
	UART_TxCmd(LPC_UART3, ENABLE);
}

int main(void) {
	/* Enable and setup SysTick Timer at a periodic rate 1 msec*/
	if (SysTick_Config(SystemCoreClock / 1000)) {
		while (1)
			;  // Capture error
	}
	oneSecondTicks = msTicks;	// read current tick counter
	sw4PressedTicks = msTicks;	// read current tick counter
	int oldAccMagnitude;

	uint8_t dir = 1;
	uint8_t wait = 0;
	uint8_t btnSW4 = 1;
	uint8_t btnSW3 = 1;

	int8_t xNew = 0;
	int8_t yNew = 0;
	int8_t zNew = 0;

	int8_t xOld = 0;
	int8_t yOld = 0;
	int8_t zOld = 0;

	int count;
	int countLed = 0;

	prevVal = 0;

	init_i2c();
	init_ssp();
	init_GPIO();
	init_uart();

	pca9532_init();
	acc_init();
	oled_init();
	led7seg_init();
	temp_init(getTicks);
	rgb_init();

	// Assume base board in zero-g position when reading first value.
	acc_read(&x, &y, &z);

	if (y < 0) {
		dir = 1;
		y = -y;
	} else {
		dir = -1;
	}

	if (y > 1 && wait++ > (40 / (1 + (y / 10)))) {
		moveBar(1, dir);
		wait = 0;
	}

	/* ---- Speaker ------> */
	moveBar(1, dir);
	GPIO_SetDir(2, 1 << 0, 1);
	GPIO_SetDir(2, 1 << 1, 1);

	GPIO_SetDir(0, 1 << 27, 1);
	GPIO_SetDir(0, 1 << 28, 1);
	GPIO_SetDir(2, 1 << 13, 1);
	GPIO_SetDir(0, 1 << 26, 1);

	GPIO_ClearValue(0, 1 << 27); //LM4811-clk
	GPIO_ClearValue(0, 1 << 28); //LM4811-up/dn
	GPIO_ClearValue(2, 1 << 13); //LM4811-shutdn

	// Setup light limit for triggering interrupt
	light_setRange(LIGHT_RANGE_1000);
	light_setLoThreshold(lightLoLimit);
	light_setHiThreshold(lightHiLimit);
	light_setIrqInCycles(LIGHT_CYCLE_1);
	light_clearIrqStatus();

	LPC_GPIOINT ->IO2IntClr |= 1 << 5;
	LPC_GPIOINT ->IO2IntEnF |= 1 << 5; //light sensor
	light_enable();

	//	LPC_GPIOINT ->IO2IntClr = 1 << 10; //SW3
	//	LPC_GPIOINT ->IO2IntEnF |= 1 << 10; //switch
	NVIC_ClearPendingIRQ(EINT3_IRQn);
	NVIC_EnableIRQ(EINT3_IRQn);

	pca9532_setBlink0Period(0);
	rotary_init();
	light_init();
	oled_clearScreen(OLED_COLOR_BLACK);
	led7seg_setChar(NULL, FALSE);

	//rotary
	LPC_GPIOINT->IO0IntClr = 1<<24;
	LPC_GPIOINT->IO0IntEnF |= 1<<24;
	LPC_GPIOINT->IO0IntClr = 1<<25;
	LPC_GPIOINT->IO0IntEnF |= 1<<25;

	NVIC_ClearPendingIRQ(EINT3_IRQn);
	NVIC_EnableIRQ(EINT3_IRQn);

	readSensors(&temperature, &light, &x, &y, &z);

	while (1) {
		btnSW4 = (GPIO_ReadValue(1) >> 31) & 0x01;

		if ((btnSW4 == 0) && (msTicks - sw4PressedTicks >= 500)) {
			sw4PressedTicks = msTicks;
			if ((mode == STABLE) | (mode == GAME)) {
				mode = MONITOR;
				monitorMsg = "Entering MONITOR Mode.\r\n";
				UART_Send(LPC_UART3, (uint8_t *) monitorMsg, strlen(monitorMsg),
						BLOCKING);
				sevenseg_Count = 0;
			} else {
				mode = STABLE;
			}
		}
		switch (mode) {
		case STABLE:
			led7seg_setChar(NULL, FALSE);
			oled_clearScreen(OLED_COLOR_BLACK);
			isFire = 0;
			isWalkingDark = 0;

			break;

			//Guess the Number Game
		case GAME:
			oled_clearScreen(OLED_COLOR_BLACK);
			oled_putString(30, 0, "GAME", OLED_COLOR_WHITE,
					OLED_COLOR_BLACK);
			oled_putString(10, 10, "-------------", OLED_COLOR_WHITE,
					OLED_COLOR_BLACK);
			oled_putString(30, 20, "GUESS", OLED_COLOR_WHITE,
					OLED_COLOR_BLACK);
			oled_putString(30, 30, "THE", OLED_COLOR_WHITE,
					OLED_COLOR_BLACK);
			oled_putString(30, 40, "NUMBER!", OLED_COLOR_WHITE,
					OLED_COLOR_BLACK);

			count = 1;
			countLed = 0;
			while (count <= 65535) {

				if(rotary_dir == 2){
					mode = STABLE;
					break;
				}

				//if after press SW3 random number and led number are equal, then win!
				btnSW3 = (GPIO_ReadValue(2) >> 10) & 0x01;
				if ((btnSW3 == 0) && (countLed == sevenseg_Count_Game)) {
					oled_clearScreen(OLED_COLOR_BLACK);
					sprintf(scoreOled, "Score: %d\r\n", score);
					oled_putString(10, 10, scoreOled, OLED_COLOR_WHITE,
							OLED_COLOR_BLACK);
					oled_putString(10, 20, "-------------", OLED_COLOR_WHITE,
							OLED_COLOR_BLACK);
					oled_putString (20, 50, "YOU WIN!", OLED_COLOR_WHITE, OLED_COLOR_BLACK);
					playSong(win);
					//if it is the highest score, then send UART
					if(score < prevScore){
						prevScore = score;
						sprintf(scoreStr, "CONGRATULATIONS! Your Highest Score is now %d.\r\n", prevScore);
						UART_Send(LPC_UART3, (uint8_t *) scoreStr, strlen(scoreStr),
								BLOCKING);
						playSong(champ);
						score = 0;
					}
				}
				if ((msTicks - oneSecondTicks) >= TICK_RATE_ONE_SEC) {
					oneSecondTicks = msTicks;
					score++;
					pca9532_setLeds(count, 0xffff);
					count = (2 * count) + 1;
					countLed++;
					sevenseg_Count_Game = rand() % 16 + 0;
					if (sevenseg_Count_Game >= 10) {
						sevenseg_Count_Game += 55;
						sevenseg_Display_Game = '0' + sevenseg_Count_Game;
						led7seg_setChar(sevenseg_Count_Game, FALSE); //Uses character 65 and after
					}

					else {
						sevenseg_Display_Game = '0' + sevenseg_Count_Game;
						led7seg_setChar(sevenseg_Display_Game, FALSE); //Uses characters '1' and after
					}
				}
			}
			break;

		case MONITOR:
			sevenSeg();

			//Indicate Fire
			if (temp_read() > TEMP_HIGH_WARNING) {
				isFire = 1;
			}

			//Indicate walking in dark
			acc_read(&xNew, &yNew, &zNew);
			if ((isDark = 1) && ((oldAccMagnitude - getAccMagnitude(xNew, yNew, zNew)) > ACC_THRESHOLD)) {
				isWalkingDark = 1;
			}

			if(isFire && isWalkingDark){
				blink_PURPLE();
			}

			else if(isFire){
				blink_RED();
			}

			else if(isWalkingDark){
				blink_BLUE();
			}

			//Send transmission to UART at 'F'
			if (sevenseg_Count == 16) {
				if (isFire) {
					fireMsg = "Fire was Detected.\r\n";
					UART_Send(LPC_UART3, (uint8_t *) fireMsg, strlen(fireMsg),
							BLOCKING);
				}
				if (isWalkingDark) {
					darkMsg = "Movement in darkness was Detected.\r\n";
					UART_Send(LPC_UART3, (uint8_t *) darkMsg, strlen(darkMsg),
							BLOCKING);
				}
				sprintf(result, "%d%d%d_-_T%.1f_L%d_AX%d_AY%d_AZ%d\r\n", N[0],
						N[1], N[2], temperature / 10.0, light, x, y, z);
				UART_Send(LPC_UART3, (uint8_t *) result, strlen(result),
						BLOCKING);

				//Setup counter for NNN in Transmission
				N[2]++;
				if (N[2] == 10) {
					N[2] = 0;
					N[1]++;
				}

				if (N[1] == 10) {
					N[1] = 0;
					N[0]++;
				}
			}
			//Update sensors at 5, 'A', 'F'
			if (sevenseg_Count == 6 | sevenseg_Count == 11
					| sevenseg_Count == 16) {
				readSensors(&temperature, &light, &x, &y, &z);
			}

			sprintf(OLED_MODE, "MONITOR");
			sprintf(OLED_LIGHT, "L = %d", light);
			sprintf(OLED_TEMPERATURE, "TEMP = %.1f", temperature / 10.0);
			sprintf(OLED_X, "X = %d", x);
			sprintf(OLED_Y, "Y = %d", y);
			sprintf(OLED_Z, "Z = %d", z);
			oled_putString(30, 0, (uint8_t *) OLED_MODE, OLED_COLOR_WHITE,
					OLED_COLOR_BLACK);
			oled_putString(10, 10, "-------------", OLED_COLOR_WHITE,
					OLED_COLOR_BLACK);
			oled_putString(0, 20, (uint8_t *) OLED_LIGHT, OLED_COLOR_WHITE,
					OLED_COLOR_BLACK);
			oled_putString(0, 30, (uint8_t *) OLED_TEMPERATURE,
					OLED_COLOR_WHITE, OLED_COLOR_BLACK);
			oled_putString(0, 40, (uint8_t *) OLED_X, OLED_COLOR_WHITE,
					OLED_COLOR_BLACK);
			oled_putString(50, 40, (uint8_t *) OLED_Y, OLED_COLOR_WHITE,
					OLED_COLOR_BLACK);
			oled_putString(30, 50, (uint8_t *) OLED_Z, OLED_COLOR_WHITE,
					OLED_COLOR_BLACK);
			break;
		}

		acc_read(&xOld, &yOld, &zOld);
		oldAccMagnitude = getAccMagnitude(xOld, yOld, zOld);
	}
}
void check_failed(uint8_t *file, uint32_t line) {
	/* User can add his own implementation to report the file name and line number,
	 ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */

	/* Infinite loop */
	while (1);
}