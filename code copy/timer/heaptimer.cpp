#include "heaptimer.h"

void HeapTimer::SwapNode_(size_t i, size_t j)
{
    assert(i >= 0 && i < heap_.size());
    assert(j >= 0 && j < heap_.size());
    swap(heap_[i], heap_[j]);
    ref_[heap_[j].id] = i;
    ref_[heap_[i].id] = j;
}

void HeapTimer::siftup_(size_t i)
{
    assert(i >= 0 && i < heap_.size());
    size_t parent = (i - 1) / 2;
    while (parent >= 0)
    {
        if (heap_[parent] > heap_[i])
        {
            SwapNode_(parent, i);
            i = parent;
            parent = (i - 1) / 2;
        }
        else
        {
            break;
        }
    }
}

bool HeapTimer::siftdown_(size_t i, size_t n)
{
    assert(i >= 0 && i < heap_.size());
    assert(n >= 0 && n <= heap_.size());

    size_t child = i * 2 + 1, index = i;
    while (child < n)
    {
        if (child + 1 < n && heap_[child] > heap_[child + 1])
        {
            child++;
        }
        if (heap_[i] > heap_[child])
        {
            SwapNode_(i, child);
            i = child;
            child = 2 * i + 1;
        }
        else
        {
            break;
        }
    }
    return index < i;
}

void HeapTimer::del_(size_t i)
{
    assert(i >= 0 && i < heap_.size());
    size_t n = heap_.size() - 1;
    // 判断i是否是末尾
    if (i != n)
    {
        SwapNode_(i, n);
        ref_[heap_.back().id] = i;
        if (!siftdown_(i, n))
        {
            siftup_(i);
        }
    }
    ref_.erase(heap_.back().id);
    heap_.pop_back();
}

void HeapTimer::adjust(int id, int newExpires)
{
    assert(!heap_.empty() && ref_.count(id));
    heap_[ref_[id]].expires = Clock::now() + MS(newExpires);
    siftdown_(ref_[id], heap_.size());
}

void HeapTimer::add(int id, int timeOut, const TimeoutCallBack &cb)
{
    TimerNode tn;
    tn.id = id;
    tn.expires = Clock::now() + MS(timeOut);
    tn.cb = cb;
    ref_[id] = heap_.size();
    heap_.push_back(tn);
    siftup_(heap_.size() - 1);
}

void HeapTimer::doWork(int id)
{
    if (heap_.empty() || ref_.count(id) == 0)
    {
        return;
    }
    size_t i = ref_[id];
    auto node = heap_[i];
    node.cb();
    del_(i);
}

void HeapTimer::clear()
{
    heap_.clear();
    ref_.clear();
}

void HeapTimer::tick()
{
    if (heap_.empty())
        return;
    else
    {
        // TimeStamp nowTime = Clock::now();
        while (!heap_.empty())
        {
            TimerNode node = heap_.front();
            if (std::chrono::duration_cast<MS>(node.expires - Clock::now()).count() > 0)
            {
                break;
            }
            node.cb();
            pop();
        }
    }
}

void HeapTimer::pop()
{
    assert(!heap_.empty());
    del_(0);
}

// 下次超时间隔
int HeapTimer::GetNextTick()
{
    int nextTick = 0;
    tick();
    if (!heap_.empty())
        nextTick = std::chrono::duration_cast<MS>(heap_.front().expires - Clock::now()).count();
    if (nextTick < 0)
        nextTick = 0;
    return nextTick;
}