/*
 * AutoPilotThreadOperations.cpp
 *
 *  Created on: May 13, 2017
 *      Author: andrei
 */
#include "AutoPilotThreadOperations.h"

#include <pthread.h>
#include <vector>

void createAllThreads(const vector<void*(*)(void*)>& threadRoutines,vector<pthread_t>& threads,
		vector<int>& threadsCreationResults)
{
	for (unsigned short k = 0; k < threadRoutines.size(); ++k)
	{
		threadsCreationResults[k] = pthread_create(&threads[k], NULL,threadRoutines[k], NULL);

		if (threadsCreationResults[k]) {
			fprintf(stderr, "Error - pthread_create() return code: %d\n",threadsCreationResults[k]);
			exit(EXIT_FAILURE);
		}
	}
}

void printThreadsCreationResults(const vector<void*(*)(void*)>& threadRoutines,
		const vector<int>& threadCreationResult)
{
	for (unsigned short k = 0; k < threadRoutines.size(); ++k)
	{
		cout << "pthread_create() for thread " << k << " returns "<< threadCreationResult[k] << endl;
	}
}

void joinThreads(const vector<pthread_t>& threads)
{
	for (auto thr : threads)
	{
		pthread_join(thr, NULL);
	}
}




