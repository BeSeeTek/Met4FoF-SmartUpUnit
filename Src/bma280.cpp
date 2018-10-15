/*
 * bma280.cpp
 *
 *  Created on: 08.10.2018
 *      Author: seeger01
 */


/*
 * BMA280.cpp
 *
 *
 *  Created on: 01.08.2018
 *      Author: seeger01
 *
 *
 * 06/16/2017 Copyright Tlera Corporation
 *
 *  Created by Kris Winer
 *
 *  The BMA280 is an inexpensive (~$1), three-axis, high-resolution (14-bit) acclerometer in a tiny 2 mm x 2 mm LGA12 package with 32-slot FIFO,
 *  two multifunction interrupts and widely configurable sample rate (15 - 2000 Hz), full range (2 - 16 g), low power modes,
 *  and interrupt detection behaviors. This accelerometer is nice choice for low-frequency sound and vibration analysis,
 *  tap detection and simple orientation estimation.
 *
 *  Library may be used freely and without limit with attribution.
 *
 */
#include <stdint.h>
#include <cstring>
#include "bma280.h"
#include "stm32f7xx_hal.h"
#include "spi.h"
#include "gpio.h"
#include "main.h"

BMA280::BMA280(GPIO_TypeDef* SPICSTypeDef,
		uint16_t SPICSPin,
		SPI_HandleTypeDef* bmaspi):_SPICSTypeDef(SPICSTypeDef),_SPICSPin(SPICSPin),_bmaspi(bmaspi) {
;
}

uint8_t BMA280::getChipID() {
	uint8_t c = readByte( BMA280_BGW_CHIPID);
	return c;
}

uint8_t BMA280::getTapType() {
	uint8_t c = readByte(BMA280_INT_STATUS_0);
	return c;
}

uint8_t BMA280::getTapStatus() {
	uint8_t c = readByte( BMA280_INT_STATUS_2);
	return c;
}

float BMA280::getConversionfactor() {
	float conversionfactor=0;
	switch (_aRes) {
	// Possible accelerometer scales (and their register bit settings) are:
	// 2 Gs , 4 Gs , 8 Gs , and 16 Gs .
	case AFS_2G:
		conversionfactor= 2.0f / 8192.0f;   // per data sheet
		return conversionfactor;
		break;
	case AFS_4G:
		conversionfactor = 4.0f / 8192.0f;
		return conversionfactor;
		break;
	case AFS_8G:
		conversionfactor = 8.0f / 8192.0f;
		return conversionfactor;
		break;
	case AFS_16G:
		conversionfactor = 16.0f / 8192.0f;
		return conversionfactor;
		break;
	default:
		return 0;
	}
}

void BMA280::initBMA280(uint8_t aRes, uint8_t BW, uint8_t power_Mode, uint8_t sleep_dur) {
	_aRes=aRes;
	_conversionfactor=getConversionfactor();
	writeByte(BMA280_PMU_RANGE, aRes);         // set full-scale range
	writeByte(BMA280_PMU_BW, BW);     // set bandwidth (and thereby sample rate)
	writeByte(BMA280_PMU_LPW, power_Mode << 5 | sleep_dur << 1); // set power mode and sleep duration

	writeByte(BMA280_INT_EN_1, 0x10);        // set data ready interrupt (bit 4)
	writeByte(BMA280_INT_MAP_1, 0x01); // map data ready interrupt to INT1 (bit 0)
	//writeByte(BMA280_INT_EN_0,  0x20 | 0x10);    // set single tap interrupt (bit 5) and double tap interrupt (bit 4)
	//writeByte(BMA280_INT_MAP_2, 0x20 | 0x10);    // map single and double tap interrupts to INT2 (bits 4 and 5)
	//writeByte(BMA280_INT_9, 0x0A);               // set tap threshold to 10 x 3.125% of full range
	writeByte(BMA280_INT_OUT_CTRL, 0x04 | 0x01); // interrupts push-pull, active HIGH (bits 0:3)
}

void BMA280::fastCompensationBMA280() {
	printf("hold flat and motionless for bias calibration");

	//delay(5000);

	uint8_t rawData[2];  // x/y/z accel register data stored here
	float FCres = 7.8125f; // fast compensation offset mg/LSB

	writeByte(BMA280_OFC_SETTING, 0x20 | 0x01); // set target data to 0g, 0g, and +1 g, cutoff at 1% of bandwidth
	writeByte(BMA280_OFC_CTRL, 0x20); // x-axis calibration
	while (!(0x10 & readByte(BMA280_OFC_CTRL))) {
	}; // HAL_Delay for calibration completion
	writeByte(BMA280_OFC_CTRL, 0x40); // y-axis calibration
	while (!(0x10 & readByte(BMA280_OFC_CTRL))) {
	}; // HAL_Delay for calibration completion
	writeByte(BMA280_OFC_CTRL, 0x60); // z-axis calibration
	while (!(0x10 & readByte(BMA280_OFC_CTRL))) {
	}; // HAL_Delay for calibration completion

	readBytes( BMA280_OFC_OFFSET_X, 2, &rawData[0]);
	int offsetX = ((int) rawData[1] << 8) | 0x00;
	printf("x-axis offset = %f mg", (float) (offsetX) * FCres / 256.0f);
	readBytes( BMA280_OFC_OFFSET_Y, 2, &rawData[0]);
	int offsetY = ((int) rawData[1] << 8) | 0x00;
	printf("y-axis offset = %f mg", (float) (offsetY) * FCres / 256.0f);
	readBytes( BMA280_OFC_OFFSET_Z, 2, &rawData[0]);
	int offsetZ = ((int) rawData[1] << 8) | 0x00;
	printf("z-axis offset = %f mg", (float) (offsetZ) * FCres / 256.0f);
}

void BMA280::resetBMA280() {
	writeByte(BMA280_BGW_SOFTRESET, 0xB6); // software reset the BMA280
}

void BMA280::selfTestBMA280() {
	uint8_t  rawData[2];  // x/y/z accel register data stored here

	writeByte(BMA280_PMU_RANGE, AFS_4G); // set full-scale range to 4G
	float STres = 4000.0f / 8192.0f; // mg/LSB for 4 g full scale

	// x-axis test
	writeByte(BMA280_PMU_SELF_TEST, 0x10 | 0x04 | 0x01); // positive x-axis
	HAL_Delay(0.1);
	readBytes( BMA280_ACCD_X_LSB, 2, &rawData[0]);
	int posX = ((int) rawData[1] << 8) | rawData[0];

	writeByte(BMA280_PMU_SELF_TEST, 0x10 | 0x00 | 0x01); // negative x-axis
	HAL_Delay(0.1);
	readBytes( BMA280_ACCD_X_LSB, 2, &rawData[0]);
	int negX = ((int) rawData[1] << 8) | rawData[0];

	printf("x-axis self test = %f mg, should be > 800 mg",
			(float) (posX - negX) * STres / 4.0f);
	// y-axis test
	writeByte(BMA280_PMU_SELF_TEST, 0x10 | 0x04 | 0x02); // positive y-axis
	HAL_Delay(0.1);
	readBytes( BMA280_ACCD_Y_LSB, 2, &rawData[0]);
	int posY = ((int) rawData[1] << 8) | rawData[0];

	writeByte(BMA280_PMU_SELF_TEST, 0x10 | 0x00 | 0x02); // negative y-axis
	HAL_Delay(0.1);
	readBytes( BMA280_ACCD_Y_LSB, 2, &rawData[0]);
	int negY = ((int) rawData[1] << 8) | rawData[0];

	printf("x-axis self test = %f mg, should be > 800 mg",
			(float) (posY - negY) * STres / 4.0f);

	// z-axis test
	writeByte(BMA280_PMU_SELF_TEST, 0x10 | 0x04 | 0x03); // positive z-axis
	HAL_Delay(0.1);
	readBytes( BMA280_ACCD_Z_LSB, 2, &rawData[0]);
	int posZ = ((int) rawData[1] << 8) | rawData[0];
	writeByte(BMA280_PMU_SELF_TEST, 0x10 | 0x00 | 0x03); // negative z-axis
	HAL_Delay(0.1);
	readBytes( BMA280_ACCD_Z_LSB, 2, &rawData[0]);
	int negZ = ((int) rawData[1] << 8) | rawData[0];

	printf("x-axis self test = %f mg, should be > 400 mg",
			(float) (posZ - negZ) * STres / 4.0f);

	writeByte(BMA280_PMU_SELF_TEST, 0x00); // disable self test
	/* end of self test*/
}

void BMA280::readBMA280AccelDataRaw(int16_t * destination) {
	uint8_t  rawData[6];  // x/y/z accel register data stored here
	readBytes( BMA280_ACCD_X_LSB, 6, &rawData[0]); // Read the 6 raw data registers into data array
	int16_t DebugArray[3]={0};
	//destination[0] = ((signed uint8_t) rawData[1]) <<6)|(signed uint8_t) (rawData[0] >> 2); // Turn the MSB and LSB into a signed 14-bit value
	//destination[1] = ((signed uint8_t) rawData[3]) * 64
	//		+ (signed uint8_t) (rawData[2] >> 2);
	//destination[2] = ((signed uint8_t) rawData[5]) * 64
	//		+ (signed uint8_t) (rawData[4] >> 2);
	destination[0] = ((int16_t)rawData[1] << 8) | (rawData[0]);
	destination[1] = ((int16_t)rawData[3] << 8) | (rawData[2]);
	destination[2] = ((int16_t)rawData[5] << 8) | (rawData[4]);
}
float BMA280::getTemperature(){
	float temp=0;
	int8_t raw=readByte(BMA280_ACCD_TEMP);
	temp=23+raw*0.5;
	return temp;
}

void BMA280::readBMA280AccelData(float * destination){
	int16_t rawArray[3]={0,0,0};
	readBMA280AccelDataRaw(rawArray);
	int16_t x=(rawArray[0]&0xFFFC);
	int16_t y=(rawArray[1]&0xFFFC);
	int16_t z=(rawArray[2]&0xFFFC);
	destination[0]=float(x)/4*_conversionfactor*g_to_ms2;
	destination[1]=float(y)/4*_conversionfactor*g_to_ms2;
	destination[2]=float(z)/4*_conversionfactor*g_to_ms2;
}
void BMA280::activateDataRDYINT() {
	writeByte(BMA280_INT_EN_1, 0x10);
	writeByte(BMA280_INT_MAP_1, 0x80);
}

int BMA280::readBMA280GyroTempData() {
	uint8_t temp = readByte( BMA280_ACCD_TEMP);  // Read the raw data register
	return (((int) temp << 8) | 0x00) >> 8; // Turn into signed 8-bit temperature value
}

// SPI read/write functions for the BMA280

void BMA280::writeByte(uint8_t subAddress, uint8_t data) {
	uint8_t buffer[2] = { BMA280_SPI_WRITE | subAddress, data };
	HAL_GPIO_WritePin(_SPICSTypeDef, _SPICSPin, GPIO_PIN_RESET);
	HAL_SPI_Transmit(_bmaspi, buffer, 2, SPI_TIMEOUT);
	HAL_GPIO_WritePin(_SPICSTypeDef, _SPICSPin, GPIO_PIN_SET);
}

uint8_t BMA280::readByte(uint8_t subAddress) {
	uint8_t tx[2] = {(BMA280_SPI_READ | subAddress),0};
	uint8_t rx[2] = {0,0};

	HAL_GPIO_WritePin(_SPICSTypeDef, _SPICSPin, GPIO_PIN_RESET);
	HAL_SPI_TransmitReceive(_bmaspi,tx, rx, 2, SPI_TIMEOUT);
	//HAL_SPI_Transmit(bmaspi, tx, 2, SPI_TIMEOUT);
	//HAL_SPI_Receive(bmaspi, rx, 1, SPI_TIMEOUT);
	HAL_GPIO_WritePin(_SPICSTypeDef, _SPICSPin, GPIO_PIN_SET);

	return rx[1];
}

void BMA280::readBytes(uint8_t subAddress,uint8_t count, uint8_t* dest) {
	uint8_t tx[count+1]={0};
	uint8_t rx[count+1]={0};
	tx[0] = {BMA280_SPI_READ | subAddress};
	HAL_GPIO_WritePin(_SPICSTypeDef, _SPICSPin, GPIO_PIN_RESET);
	HAL_SPI_TransmitReceive(_bmaspi,tx, rx, count+1, SPI_TIMEOUT);
	//HAL_SPI_Transmit(bmaspi, tx, 1, SPI_TIMEOUT);
	//HAL_SPI_Receive(bmaspi, dest, count, SPI_TIMEOUT);
	HAL_GPIO_WritePin(_SPICSTypeDef, _SPICSPin, GPIO_PIN_SET);

	//Wire.transfer(address, &subAddress, 1, dest, count);
	//Read acceleration on all 3 axis
	//uint8_t rx[count];
	//I2CBus.write(I2CADR_W(address),(const uint8_t *)subAddress, 1);
	//I2CBus.read(I2CADR_R(address), rx, count);
	memcpy(dest, &rx[1], count);
}

