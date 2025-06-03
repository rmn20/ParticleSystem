#pragma once

#include <mutex>
#include <vector>
#include <random>

#define MAX_EFFECTS 2048
#define PRT_PER_EFFECT 64
#define MAX_PRT_THREADS 16

typedef struct
{
	float x;
	float y;
	float alpha;
	float speedX;
	float speedY;
	float speedAlpha;
} Particle;

typedef struct
{
	int aliveParticles;
	Particle particles[PRT_PER_EFFECT];
} PrtEffect;

typedef struct
{
	PrtEffect efxPool[MAX_EFFECTS];

	//Per thread effects lists
	std::vector<std::vector<int>> efxIds;
	std::vector<int> usedEfx;
	std::vector<int> maxEfx;
} PrtEffectBuffer;

class EffectSystem
{
private:
	//Effect buffers
	std::mutex swapMutex;

	int renderBufferId = -1;
	int lastBufferId = 0;
	int nextBufferId = 1;

	PrtEffectBuffer* effectBuffers[3];

	//Thread safe random
	int threads;
	std::vector<std::mt19937> rnds;

	//Effect creation control
	bool addFx = false;
	float addFxX, addFxY;

public:
	EffectSystem(int threads);
	~EffectSystem();

	void update(float dTime, float scrW, float scrH);
	void add_effect(float x, float y);

	PrtEffectBuffer* get_render_buffer();

	//void print_info();
};