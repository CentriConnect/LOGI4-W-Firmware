#ifndef MOVING_AVERAGE_HPP_
#define MOVING_AVERAGE_HPP_

#include "stdint.h"

class MovingAverage {
public:
    static const unsigned char CAPACITY = 5;
    
    explicit MovingAverage();
    
    bool addSample(unsigned short value);
    void clear();
    bool getOutput(unsigned short& value) const;
    bool getOutput(int32_t& value) const;
    
    bool isEmpty() const { return count_ == 0; }
    bool isFull() const { return size_ > 0 && count_ == size_; }

private:
    unsigned short samples_[CAPACITY];
    unsigned int size_ = CAPACITY;
    unsigned int sum_;
    unsigned char count_;
    unsigned char index_;
};

#endif // MOVING_AVERAGE_HPP_