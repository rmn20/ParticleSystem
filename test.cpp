#include <thread>
#include <mutex>
#include <atomic>

#include "./nvToolsExt.h"

#include "test.h"
#include "EffectSystem.h"

static std::atomic_int globalTime;
static std::atomic_bool workerMustExit = false;

static EffectSystem effectSystem(std::max(1, (int) std::thread::hardware_concurrency() - 1));

void WorkerThread(void)
{
	while (!workerMustExit)
	{
		nvtxRangePush(__FUNCTION__);

		static int lastTime = 0;
		const int time = globalTime.load();
		const int delta = time - lastTime;
		lastTime = time;

		//static int nextStats = 0;

		if (delta > 0)
		{
			effectSystem.update(delta / 1000.0f, test::SCREEN_WIDTH, test::SCREEN_HEIGHT);

			/*if (nextStats < time)
			{
				effectSystem.print_info();
				nextStats = time + 3000;
			}*/
		}

		static const int MIN_UPDATE_PERIOD_MS = 10;
		if (delta < MIN_UPDATE_PERIOD_MS)
			std::this_thread::sleep_for(std::chrono::milliseconds(MIN_UPDATE_PERIOD_MS - delta));

		nvtxRangePop();
	}
}


void test::init(void)
{
	std::thread workerThread(WorkerThread);
	workerThread.detach(); // Glut + MSVC = join hangs in atexit()
}

void test::term(void)
{
	workerMustExit = true;
}

void test::render(void)
{
	PrtEffectBuffer* effectBuf = effectSystem.get_render_buffer();

	//Render particles from all thread buffers
	int threadsCount = effectBuf->efxIds.size();

	for (int t = 0; t < threadsCount; ++t)
	{
		std::vector<int>* efxIds = &effectBuf->efxIds[t];

		for (int efxId = 0; efxId < effectBuf->usedEfx[t]; ++efxId)
		{
			PrtEffect* effect = &effectBuf->efxPool[(*efxIds)[efxId]];

			for (int pId = 0; pId < effect->aliveParticles; ++pId)
			{
				Particle* prt = &effect->particles[pId];
				platform::drawPoint(prt->x, prt->y, 1, 1, 1, prt->alpha);
			}
		}
	}
}

void test::update(int dt)
{
	globalTime.fetch_add(dt);
}

void test::on_click(int x, int y)
{
	effectSystem.add_effect(x, SCREEN_HEIGHT - y);
}