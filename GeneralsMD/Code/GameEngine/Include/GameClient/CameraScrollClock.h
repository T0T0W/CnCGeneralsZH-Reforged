/* Command & Conquer Generals Zero Hour - GPL-3.0-or-later */
#ifndef CAMERA_SCROLL_CLOCK_H
#define CAMERA_SCROLL_CLOCK_H

#include <stdint.h>

// Sample once per render tick, even when the camera is stationary. Each view owns its clock.
class CameraScrollClock
{
public:
	CameraScrollClock() : m_previousTime(0), m_started(false) {}

	float sample(uint32_t now, int tickMilliseconds)
	{
		const uint32_t elapsed = now - m_previousTime;
		m_previousTime = now;
		if (!m_started)
		{
			m_started = true;
			return 0.0f;
		}
		const float factor = elapsed / (float)tickMilliseconds;
		return factor > 3.0f ? 3.0f : factor;
	}

private:
	uint32_t m_previousTime;
	bool m_started;
};

#endif
