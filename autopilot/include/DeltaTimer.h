/*
 * DeltaTimer.h
 *
 *  Created on: Apr 22, 2017
 *      Author: andrei
 */

#pragma once

#ifndef _GLIBCXX_CHRONO
#include <chrono>
#include <ratio>
#endif

class DeltaTimer
{
private:
	std::chrono::steady_clock::time_point startTime;
	std::chrono::steady_clock::time_point stopTime;
	std::chrono::duration<double, std::micro> deltaTime;
public:
	DeltaTimer();
	virtual ~DeltaTimer();
	unsigned long long getDeltaTimeUs();
	unsigned long long getDeltaTimeMs();
	void resetDeltaTimer();
};
