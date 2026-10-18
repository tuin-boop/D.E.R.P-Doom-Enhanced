// Persistent workers for independent frame-lighting jobs. The render thread
// participates, then waits before consuming results or changing scene data.
#ifndef DOOM_FRAME_WORKERS_H
#define DOOM_FRAME_WORKERS_H
#include <condition_variable>
#include <mutex>
#include <exception>
#include <algorithm>
#include <atomic>
#include <functional>
#include <thread>
#include <vector>
namespace doom3d {
class FrameWorkers {
    std::mutex mutex;
    std::condition_variable wake, finished;
    std::vector<std::thread> workers;
    std::atomic<int> next{0};
    int count=0, remaining=0;
    unsigned generation=0;
    bool stopping=false;
    std::function<void(int)> job;
    std::exception_ptr error;
    void run() {
        try { for(int i;(i=next.fetch_add(1,std::memory_order_relaxed))<count;)job(i); }
        catch(...) {std::lock_guard<std::mutex> lock(mutex);if(!error)error=std::current_exception();}
    }
public:
    explicit FrameWorkers(unsigned limit=8) {
        unsigned total=std::clamp(std::thread::hardware_concurrency(),1u,std::max(1u,limit));
        workers.reserve(total-1);
        for(unsigned i=1;i<total;++i)try { workers.emplace_back([this] {
            unsigned seen=0;
            std::unique_lock<std::mutex> lock(mutex);
            for(;;) {
                wake.wait(lock,[&]{return stopping||generation!=seen;});
                if(stopping)return;
                seen=generation;lock.unlock();run();lock.lock();
                if(--remaining==0)finished.notify_one();
            }
        }); } catch(const std::system_error&) {break;} // Safely retain fewer workers if unavailable.
    }
    ~FrameWorkers() {
        {std::lock_guard<std::mutex> lock(mutex);stopping=true;}
        wake.notify_all();for(auto &worker:workers)worker.join();
    }
    // Called only by the render thread; workers must not recursively dispatch.
    void each(int size,const std::function<void(int)> &work) {
        if(size<2||workers.empty()){for(int i=0;i<size;++i)work(i);return;}
        {std::lock_guard<std::mutex> lock(mutex);
            count=size;next=0;job=work;error=nullptr;remaining=(int)workers.size();++generation;}
        wake.notify_all();run();
        std::unique_lock<std::mutex> lock(mutex);
        finished.wait(lock,[&]{return remaining==0;});
        job={};if(error)std::rethrow_exception(error);
    }
};
inline void frameParallelFor(int count,const std::function<void(int)> &work) {
    if(M_CheckParm((char*)"-singlethreadlights")){for(int i=0;i<count;++i)work(i);return;}
    static FrameWorkers pool;pool.each(count,work);
}
}
#endif
