#include "EffectSystem.h"

#define _USE_MATH_DEFINES
#include <cmath>

#include <cstdlib>
#include <cstring>
#include <utility>
#include <future>

#ifndef M_PI
#define M_PI (3.14159265358979323846)
#endif

EffectSystem::EffectSystem(int threads)
{
	threads = std::min(MAX_PRT_THREADS, threads);
	this->threads = threads;

	for (int t = 0; t < threads; ++t)
		rnds.push_back({});

	//Init all particle buffers
	for (int i = 0; i < 3; ++i)
	{
		PrtEffectBuffer* buffer = new PrtEffectBuffer;
		effectBuffers[i] = buffer;

		//Init effects lists for each thread
		for (int t = 0; t < threads; ++t)
		{
			//Make sure that summ of all effects is corrent
			int effectsCount = 
				(MAX_EFFECTS * (t + 1) / threads) - 
				(MAX_EFFECTS * t / threads);

			buffer->usedEfx.push_back(0);
			buffer->maxEfx.push_back(effectsCount);

			buffer->efxIds.push_back({});
			std::vector<int>* efxIds = &buffer->efxIds.back();

			for (int pId = 0; pId < effectsCount; pId++)
				efxIds->push_back((MAX_EFFECTS * t / threads) + pId);
		}
	}
}

EffectSystem::~EffectSystem()
{
	delete effectBuffers[0];
	delete effectBuffers[1];
	delete effectBuffers[2];
}

static void add_new_effect(
	PrtEffectBuffer* buffer, 
	float x, float y, 
	std::mt19937* rnd, 
	int thread
) {
	if (buffer->usedEfx[thread] == buffer->maxEfx[thread])
		return;

	//Add new effect
	int efxId = buffer->efxIds[thread][buffer->usedEfx[thread]];
	PrtEffect* effect = &buffer->efxPool[efxId];

	buffer->usedEfx[thread]++;
	effect->aliveParticles = PRT_PER_EFFECT;

	for (int i = 0; i < PRT_PER_EFFECT; ++i)
	{
		Particle* prt = &effect->particles[i];

		prt->x = x;
		prt->y = y;
		prt->alpha = 1;

		float deg = ((*rnd)() / (float) rnd->max()) * 2 * (float) M_PI;
		float len = ((*rnd)() / (float) rnd->max()) * 700;

		prt->speedX = cosf(deg) * len;
		prt->speedY = sinf(deg) * len;
		prt->speedAlpha = -(1 / ((*rnd)() / (float) rnd->max() + 1)); //from 1 to 2 seconds decay
	}
}

static void add_existing_effect(
	PrtEffect* lastEffect, PrtEffectBuffer* nextBuffer, 
	float dTime, 
	float scrW, float scrH, 
	std::mt19937* rnd,
	int thread
) {
	//Skip if effect is dead or if thread buffer is full
	if (
		lastEffect->aliveParticles == 0 ||
		nextBuffer->usedEfx[thread] == nextBuffer->maxEfx[thread]
	) {
		return;
	}
	
	//Update and add effect to next buffer
	int efxId = nextBuffer->efxIds[thread][nextBuffer->usedEfx[thread]];
	PrtEffect* nextEffect = &nextBuffer->efxPool[efxId];

	nextBuffer->usedEfx[thread]++;

	int lastPrtCount = lastEffect->aliveParticles;
	int nextPrtCount = 0;

	Particle* lastPrt = &lastEffect->particles[0];
	Particle* nextPrt = &nextEffect->particles[0];
	
	for (int i = 0; i < lastPrtCount; ++i, ++lastPrt)
	{
		float speedX = lastPrt->speedX * powf(0.8f, dTime);
		float speedY = lastPrt->speedY * powf(0.8f, dTime);

		speedY -= 500.0f * dTime; //gravity

		float x = lastPrt->x + speedX * dTime;
		float y = lastPrt->y + speedY * dTime;

		//Remove off screen particles
		if (x < 0 || x > scrW || y < 0 || y > scrH) continue;

		float alpha = lastPrt->alpha + lastPrt->speedAlpha * dTime;

		//Remove dead particles
		if (alpha < 0)
		{
			if (((*rnd)() % 100) < 12)
				add_new_effect(nextBuffer, x, y, rnd, thread);

			continue;
		}

		nextPrt->x = x;
		nextPrt->y = y;
		nextPrt->alpha = alpha;

		nextPrt->speedX = speedX;
		nextPrt->speedY = speedY;
		nextPrt->speedAlpha = lastPrt->speedAlpha;

		nextPrt++;
		nextPrtCount++;
	}

	nextEffect->aliveParticles = nextPrtCount;
}

static void update_particles_part(
	PrtEffectBuffer* lastBuffer, PrtEffectBuffer* nextBuffer,
	float dTime,
	float scrW, float scrH,
	std::mt19937* rnd,
	int thread
) {
	//Update all active effects
	std::memcpy(
		&nextBuffer->efxIds[thread][0], 
		&lastBuffer->efxIds[thread][0], 
		sizeof(int) * lastBuffer->maxEfx[thread]
	);

	int lastFxCount = lastBuffer->usedEfx[thread];
	nextBuffer->usedEfx[thread] = 0;

	for (int i = 0; i < lastFxCount; ++i)
	{
		int efxId = lastBuffer->efxIds[thread][i];
		PrtEffect* lastEffect = &lastBuffer->efxPool[efxId];

		add_existing_effect(lastEffect, nextBuffer, dTime, scrW, scrH, rnd, thread);
	}
}

void EffectSystem::update(float dTime, float scrW, float scrH)
{
	PrtEffectBuffer* lastBuffer = effectBuffers[lastBufferId];
	PrtEffectBuffer* nextBuffer = effectBuffers[nextBufferId];

	//Update effects in all thread lists
	std::future<void> asyncWait[MAX_PRT_THREADS - 1];

	for (int t = 1; t < threads; ++t)
	{
		asyncWait[t - 1] = std::async(
			&update_particles_part,
			lastBuffer, nextBuffer, 
			dTime, 
			scrW, scrH, 
			&rnds[t], 
			t
		);
	}

	update_particles_part(lastBuffer, nextBuffer, dTime, scrW, scrH, &rnds[0], 0);

	for (int t = 1; t < threads; ++t)
		asyncWait[t - 1].wait();

	std::vector<int>* efxIds = &nextBuffer->efxIds[0];
	int* usedEfx = &nextBuffer->usedEfx[0];
	int* maxEfx = &nextBuffer->maxEfx[0];

	//Balance load between threads
	{
		int avgEfxCount = 0;

		for (int t = 0; t < threads; ++t)
			avgEfxCount += usedEfx[t];

		avgEfxCount /= threads;

		for (int t = 0; t < threads; ++t)
		{
			if (usedEfx[t] <= avgEfxCount)
				continue;

			for (int t2 = 0; t2 < threads; ++t2)
			{
				if (t == t2)
					continue;

				//Swap unused efx id with used efx id
				while(
					usedEfx[t2] < avgEfxCount &&
					usedEfx[t2] < maxEfx[t2] &&
					usedEfx[t] > avgEfxCount
				) {

					std::swap(
						efxIds[t][usedEfx[t] - 1],
						efxIds[t2][usedEfx[t2]]
					);

					usedEfx[t]--;
					usedEfx[t2]++;
				}
			}
		}
	}

	//Add effects from main thread and swap buffers
	swapMutex.lock();

	if (addFx)
	{
		int targetThread = 0;

		for (int t = 1; t < threads; ++t)
		{
			if (usedEfx[t] < usedEfx[targetThread])
				targetThread = t;
		}

		add_new_effect(nextBuffer, addFxX, addFxY, &rnds[targetThread], targetThread);
		addFx = false;
	}

	lastBufferId = nextBufferId;

	for (int i = 0; i < 3; ++i)
	{
		if (i != lastBufferId && i != renderBufferId)
		{
			nextBufferId = i;
			break;
		}
	}

	swapMutex.unlock();
}

PrtEffectBuffer* EffectSystem::get_render_buffer()
{
	swapMutex.lock();
	renderBufferId = lastBufferId;
	swapMutex.unlock();

	return effectBuffers[renderBufferId];
}

void EffectSystem::add_effect(float x, float y)
{
	swapMutex.lock();

	addFx = true;
	addFxX = x;
	addFxY = y;

	swapMutex.unlock();
}

/*void EffectSystem::print_info()
{
	printf("Efx per thread: ");

	for (int t = 0; t < threads; t++)
	{
		PrtEffectBuffer* effectBuf = effectBuffers[renderBufferId];
		printf("%d ", effectBuf->usedEfx[t]);
	}

	printf("\n");
}*/