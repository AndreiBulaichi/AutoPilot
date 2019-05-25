/*
 * DeltaTimer.cpp
 *
 *  Created on: Apr 22, 2017
 *      Author: andrei
 */
#include "DeltaTimer.h"


DeltaTimer::DeltaTimer()
{
	this->startTime=std::chrono::steady_clock::now();
	this->stopTime=std::chrono::steady_clock::now();
}

DeltaTimer::~DeltaTimer(){}

/*
 * getDeltaTime in us
 */
unsigned long long DeltaTimer::getDeltaTimeUs()
{
	stopTime = std::chrono::steady_clock::now();
	deltaTime = stopTime-startTime;
	return deltaTime.count();
}

unsigned long long DeltaTimer::getDeltaTimeMs()
{
	return getDeltaTimeUs()/1000;
}

void DeltaTimer::resetDeltaTimer()
{
	this->startTime=std::chrono::steady_clock::now();
}
