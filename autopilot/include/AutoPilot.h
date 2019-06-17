/*
 * AutoPilot.h
 *
 *  Created on: May 7, 2017
 *      Author: andrei
 */
#pragma once

#include <atomic>
#include <stdint.h>

#define setBit(a,b) ((a) |= (1ULL<<(b)))
#define clearBit(a,b) ((a) &= ~(1ULL<<(b)))

#define ADDRESS 			(uint8_t) 0x04
#define SPEED_VALUE_FLAG 	(uint8_t) 0xA1
#define DIR_VALUE_FLAG 		(uint8_t) 0xB2

std::string deviceName = "MYRIAD";

static const char *devName = "/dev/i2c-1";
int16_t I2CFileStream;
bool lightsOn = false;
bool stopOn = false;

void getFrame();
void showFrame();
void detectLanes();
void detectTraffic();
void detectCars();
void arduinoI2C();
void exitRoutine (void);

std::atomic<int16_t> speed;
std::atomic<int8_t> steer;

template <typename T>
void unused(T &&)
{ }
