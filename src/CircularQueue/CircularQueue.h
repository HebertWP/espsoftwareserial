/*
CircularQueue.h - Implementation of a lock-free circular queue for EspSoftwareSerial.
Copyright (c) 2019 Dirk O. Kaar. All rights reserved.

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 2.1 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

*/

#ifndef Circular_Queue_h
#define Circular_Queue_h

#include <atomic>
#include <memory>

template< typename T > class CircularQueue
{
public:
	CircularQueue() = delete;
	CircularQueue(size_t capacity) : m_bufSize(capacity + 1), m_buffer(new std::atomic<T>[m_bufSize])
	{
		m_inPosT.store(0);
		m_outPos.store(0);
	}
	~CircularQueue()
	{
		m_buffer.reset();
	}
	CircularQueue(const CircularQueue&) = delete;
	CircularQueue& operator=(const CircularQueue&) = delete;

	void flush() {
		m_outPos.store(m_inPosT.load());
	}

	size_t available()
	{
		ssize_t avail = m_inPosT.load() - m_outPos.load();
		if (avail < 0) avail += m_bufSize;
		return avail;
	}

	size_t availableForWrite()
	{
		ssize_t avail = (m_outPos.load() - m_inPosT.load() - 1);
		if (avail < 0) avail += m_bufSize;
		return avail;
	}

	T peek()
	{
		auto outPos = m_outPos.load();
		return (m_inPosT.load() == outPos) ? defaultValue : m_buffer[outPos].load();
	}

	bool ICACHE_RAM_ATTR push(T val)
	{
		auto inPos = m_inPosT.load();
		int next = (inPos + 1) % m_bufSize;
		if (next == m_outPos.load()) {
			return false;
		}
		m_inPosT.store(next);

		m_buffer[inPos].store(val);

		return true;
	}

	T pop()
	{
		auto outPos = m_outPos.load();
		if (m_inPosT.load() == outPos) return defaultValue;
		auto val = m_buffer[outPos].load();
#ifdef ESP8266
		m_outPos.store((outPos + 1) % m_bufSize);
#else
		m_outPos.exchange((outPos + 1) % m_bufSize);
#endif
		return val;
	}

	size_t pop_n(T* buffer, size_t size) {
		size_t avail = size = min(size, available());
		if (!avail) return 0;
		auto outPos = m_outPos.load();
		size_t n = min(avail, static_cast<size_t>(m_bufSize - outPos));
		buffer = std::copy_n(m_buffer.get() + outPos, n, buffer);
		avail -= n;
		if (0 < avail) {
			buffer = std::copy_n(m_buffer.get(), avail, buffer);
		}
#ifdef ESP8266
		m_outPos.store((outPos + size) % m_bufSize);
#else
		m_outPos.exchange((outPos + size) % m_bufSize);
#endif
		return size;
	}

protected:
	const T defaultValue = {};
	int m_bufSize;
	std::unique_ptr<std::atomic<T>[] > m_buffer;
	std::atomic<int> m_inPosT;
	std::atomic<int> m_outPos;
};

template< typename T > class CircularQueueMP : protected CircularQueue<T>
{
public:
	CircularQueueMP(size_t capacity) : CircularQueue<T>(capacity)
	{
		m_inPosL.store(0);
		m_inPosC.store(0);
	}
	using CircularQueue<T>::flush;
	using CircularQueue<T>::available;
	using CircularQueue<T>::peek;
	using CircularQueue<T>::pop;
	using CircularQueue<T>::pop_n;

	size_t availableForWrite()
	{
		ssize_t avail = (CircularQueue<T>::m_outPos.load() - m_inPosL.load() - 1);
		if (avail < 0) avail += CircularQueue<T>::m_bufSize;
		return avail;
	}

	bool ICACHE_RAM_ATTR push(T val)
#ifdef ESP8266
	{
		uint32_t savedPS = xt_rsil(15);
		auto inPos = CircularQueue<T>::m_inPosT.load();
		int next = (inPos + 1) % CircularQueue<T>::m_bufSize;
		if (next == CircularQueue<T>::m_outPos.load()) {
			xt_wsr_ps(savedPS);
			return false;
		}
		CircularQueue<T>::m_inPosT.store(next);

		CircularQueue<T>::m_buffer[inPos].store(val);

		xt_wsr_ps(savedPS);
		return true;
	}
#else
	{
		int nextL;
		auto inPos = m_inPosL.load();
		do {
			nextL = (inPos + 1) % CircularQueue<T>::m_bufSize;
			if (nextL == CircularQueue<T>::m_outPos.load()) return false;
		} while (!m_inPosL.compare_exchange_weak(inPos, nextL));

		CircularQueue<T>::m_buffer[inPos].store(val);

		int wrappedC;
		auto inPosC = m_inPosC.load();
		do {
			wrappedC = (inPosC + 1) % CircularQueue<T>::m_bufSize;
		} while (!m_inPosC.compare_exchange_weak(inPosC, wrappedC));

		if (m_inPosL.compare_exchange_strong(wrappedC, wrappedC)) {
			CircularQueue<T>::m_inPosT.store(wrappedC);
		}

		return true;
	}
#endif

protected:
	std::atomic<int> m_inPosL;
	std::atomic<int> m_inPosC;
};

#endif // Circular_Queue_h
