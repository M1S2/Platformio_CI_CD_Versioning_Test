#ifndef UPDATE_QUEUE_H
#define UPDATE_QUEUE_H

#include <Arduino.h>

template <typename T, size_t SIZE>
class GenericQueue
{
public:
    bool push(const T &item)
    {
        if (count >= SIZE)
        {
            return false;
        }
        queue[tail] = item;
        tail = (tail + 1) % SIZE;
        count++;
        return true;
    }

    bool pop(T &item)
    {
        if (count == 0)
        {
            return false;
        }
        item = queue[head];
        head = (head + 1) % SIZE;
        count--;
        return true;
    }

    bool isEmpty() const { return count == 0; }
    size_t size() const { return count; }
    
    void clear()
    {
        head = 0;
        tail = 0;
        count = 0;
    }

    bool getAt(size_t index, T &item) const
    {
        if (index >= count)
        {
            return false;
        }
        item = queue[(head + index) % SIZE];
        return true;
    }

private:
    T queue[SIZE];
    size_t head = 0;
    size_t tail = 0;
    size_t count = 0;
};

#endif