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

void MovingAverage::saveState(State& state) const {
    for (unsigned int i = 0; i < CAPACITY; ++i) {
        state.samples[i] = samples_[i];
    }
    state.size = size_;
    state.sum = sum_;
    state.count = count_;
    state.index = index_;
}

bool MovingAverage::restoreState(const State& state) {
    if (state.size == 0 || state.size > CAPACITY ||
        state.count > state.size || state.index >= state.size) {
        return false;
    }

    unsigned int calculated_sum = 0;
    for (unsigned int i = 0; i < state.count; ++i) {
        calculated_sum += state.samples[i];
    }
    if (calculated_sum != state.sum) {
        return false;
    }

    for (unsigned int i = 0; i < CAPACITY; ++i) {
        samples_[i] = state.samples[i];
    }
    size_ = state.size;
    sum_ = state.sum;
    count_ = state.count;
    index_ = state.index;
    return true;
}
