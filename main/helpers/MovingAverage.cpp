#include "MovingAverage.h"

MovingAverage::MovingAverage()
{
    clear();
}

bool MovingAverage::addSample(unsigned short value) {
    unsigned long sum = sum_ + value;

    if (count_ < size_) {
        count_++;
    } else {
        sum -= samples_[index_];
    }

    sum_ = sum;
    samples_[index_] = value;
    index_ = (index_ + 1) % size_;

    return true;
}

void MovingAverage::clear() {
    count_ = 0;
    index_ = 0;
    sum_ = 0;
}

bool MovingAverage::getOutput(unsigned short& value) const {
    if (count_ == 0) return false;
    
    value = (unsigned short)((sum_ + count_/2) / count_);
    return true;
}

bool MovingAverage::getOutput(int32_t& value) const {
    if (count_ == 0) return false;
    
    value = (int32_t)((sum_ + count_/2) / count_);
    return true;
}