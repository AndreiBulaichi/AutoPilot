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

std::string networkPath = "../../../models/pedestrian_and_vehicles/origin/mobilenet_iter_73000.xml";
std::string deviceName = "MYRIAD";
constexpr float confidenceThreshold = 0.5f;

static const char *devName = "/dev/i2c-1";
int16_t I2CFileStream;
bool lightsOn = false;
bool stopOn = false;

void *arduinoI2CThread(void *ptr);
void *laneDetectorThread(void *ptr);
void *ssdDetectorThread(void *ptr);
void exitRoutine (void);

std::atomic<int16_t> speed;
std::atomic<int8_t> steer;

template <typename T>
void unused(T &&)
{ }