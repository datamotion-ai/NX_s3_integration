#include "timer.h"
#include <chrono>
#include <thread>

Timer::Timer() : running(false) {}

Timer::~Timer() 
{
    stop();
}

// Stop the timer
void Timer::stop() 
{
    if (running) 
    {
        running = false;
        if (timerThread.joinable()) 
        {
            timerThread.join();
        }
    }
}

// Restart the timer with the same member function and interval
void Timer::restart() 
{
    if (callback) 
    {
        stop(); // Stop the timer if it's already running
        this->interval = interval;
        running = true;
        timerThread = std::thread(&Timer::timerThreadFunction, this);
    }
}

void Timer::setInterval(int timeout)
{
    this->interval = timeout;
}

// Timer thread function
void Timer::timerThreadFunction() 
{
    while (running) 
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(interval));
        if (running && callback) 
        {
            callback();
        }
    }
}
