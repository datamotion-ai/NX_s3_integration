#ifndef TIMER_H
#define TIMER_H

#include <functional>
#include <thread>

class Timer 
{
public:
    Timer();
    ~Timer();

    // Start the timer with a member function callback and interval in milliseconds
    template <typename T>
    void start(T* instance, void (T::*memberFunction)(), int interval) 
    {
        stop(); // Stop the timer if it's already running

        callback = std::bind(memberFunction, instance);
        this->interval = interval;
        running = true;
        timerThread = std::thread(&Timer::timerThreadFunction, this);
    }

    // Stop the timer
    void stop();

    // Restart the timer with the same member function and interval
    void restart();

    void setInterval(int timeout);

private:
    void timerThreadFunction();

    bool running;
    std::thread timerThread;
    std::function<void()> callback;
    int interval;
};

#endif // TIMER_H
