/*
 * AutoPilotThreadOperations.h
 *
 *  Created on: May 13, 2017
 *      Author: andrei
 */

#ifndef AUTOPILOTTHREADOPERATIONS_H_
#define AUTOPILOTTHREADOPERATIONS_H_


#include <pthread.h>
#include <vector>
#include<iostream>

using namespace std;

void createAllThreads(const vector<void*(*)(void*)>& threadRoutines,vector<pthread_t>& threads,vector<int>& threadsCreationResults);

void printThreadsCreationResults(const vector<void*(*)(void*)>& threadRoutines,const vector<int>& threadCreationResult);

void joinThreads(const vector<pthread_t>& threads);

#endif
