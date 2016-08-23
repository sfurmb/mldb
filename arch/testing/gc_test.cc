/* gc_test.cc
   Jeremy Barnes, 23 February 2010
   Copyright (c) 2010 Datacratic.  All rights reserved.

   This file is part of MLDB. Copyright 2015 Datacratic. All rights reserved.

   Test of the garbage collector locking.
*/

#define BOOST_TEST_MAIN
#define BOOST_TEST_DYN_LINK

#include "mldb/arch/gc_lock.h"
#include "mldb/jml/utils/string_functions.h"
#include "mldb/base/exc_assert.h"
#include "mldb/jml/utils/guard.h"
#include "mldb/arch/thread_specific.h"
#include "mldb/arch/rwlock.h"
#include "mldb/arch/spinlock.h"
#include "mldb/arch/tick_counter.h"
#include <boost/test/unit_test.hpp>
#include <iostream>
#include <atomic>


using namespace ML;
using namespace Datacratic;
using namespace std;

// Defined in gc_lock.cc
namespace Datacratic {
extern int32_t gcLockStartingEpoch;
};

struct ThreadGroup {
    void create_thread(std::function<void ()> fn)
    {
        threads.emplace_back(std::move(fn));
    }

    void join_all()
    {
        for (auto & t: threads)
            t.join();
        threads.clear();
    }
    std::vector<std::thread> threads;
};

#if 1

BOOST_AUTO_TEST_CASE ( test_gc )
{
    GcLock gc;
    gc.lockShared();

    BOOST_CHECK(gc.isLockedShared());

    std::atomic<int> deferred(false);

    cerr << endl << "before defer" << endl;
    gc.dump();

    gc.defer([&] () { deferred = true; });

    cerr << endl << "after defer" << endl;
    gc.dump();

    gc.unlockShared();

    cerr << endl << "after unlock shared" << endl;
    gc.dump();

    BOOST_CHECK(!gc.isLockedShared());
    BOOST_CHECK(deferred);

    BOOST_REQUIRE(!gc.isLockedByAnyThread());
}

BOOST_AUTO_TEST_CASE ( test_exclusive )
{
    GcLock lock;

    for (unsigned i = 0;  i < 100000;  ++i) {
        GcLock::ExclusiveGuard guard(lock);
    }

    BOOST_REQUIRE(!lock.isLockedByAnyThread());
}

BOOST_AUTO_TEST_CASE(test_mutual_exclusion)
{
    cerr << "testing mutual exclusion" << endl;

    GcLock lock;
    std::atomic<bool> finished(false);
    std::atomic<int> numExclusive(0);
    std::atomic<int> numShared(0);
    std::atomic<int> errors(0);
    std::atomic<int> multiShared(0);
    std::atomic<int> sharedIterations(0);
    std::atomic<uint64_t> exclusiveIterations(0);

    auto sharedThread = [&] ()
        {
            while (!finished) {
                GcLock::SharedGuard guard(lock);
                numShared += 1;

                if (numExclusive > 0) {
                    cerr << "exclusive and shared" << endl;
                    errors += 1;
                }
                if (numShared > 1) {
                    multiShared += 1;
                }

                numShared -= 1;
                sharedIterations += 1;
                std::atomic_thread_fence(std::memory_order_seq_cst);
            }
        };

    auto exclusiveThread = [&] ()
        {
            while (!finished) {
                GcLock::ExclusiveGuard guard(lock);
                numExclusive += 1;

                if (numExclusive > 1) {
                    cerr << "more than one exclusive" << endl;
                    errors += 1;
                }
                if (numShared > 0) {
                    cerr << "exclusive and shared" << endl;
                    multiShared += 1;
                }

                numExclusive -= 1;
                exclusiveIterations += 1;
                std::atomic_thread_fence(std::memory_order_seq_cst);
            }
        };

    lock.getEntry();

    int nthreads = 4;

    {
        cerr << "single shared" << endl;
        sharedIterations = exclusiveIterations = multiShared = finished = 0;
        ThreadGroup tg;
        tg.create_thread(sharedThread);
        sleep(1);
        finished = true;
        tg.join_all();
        BOOST_CHECK_EQUAL(errors, 0);
        cerr << "iterations: shared " << sharedIterations
             << " exclusive " << exclusiveIterations << endl;
        cerr << "multiShared = " << multiShared << endl;

        BOOST_REQUIRE(!lock.isLockedByAnyThread());
    }

    {
        cerr << "multi shared" << endl;
        cerr << "starting at " << lock.currentEpoch() << endl;
        sharedIterations = exclusiveIterations = multiShared = finished = 0;
        ThreadGroup tg;
        for (unsigned i = 0;  i < nthreads;  ++i)
            tg.create_thread(sharedThread);
        sleep(1);
        finished = true;
        tg.join_all();
        BOOST_CHECK_EQUAL(errors, 0);
        if (nthreads > 1)
            BOOST_CHECK_GE(multiShared, 0);
        cerr << "iterations: shared " << sharedIterations
             << " exclusive " << exclusiveIterations << endl;
        cerr << "multiShared = " << multiShared << endl;

        lock.dump();
        BOOST_REQUIRE(!lock.isLockedByAnyThread());
    }

    {
        cerr << "single exclusive" << endl;
        sharedIterations = exclusiveIterations = multiShared = finished = 0;
        ThreadGroup tg;
        tg.create_thread(exclusiveThread);
        sleep(1);
        finished = true;
        tg.join_all();
        BOOST_CHECK_EQUAL(errors, 0);
        cerr << "iterations: shared " << sharedIterations
             << " exclusive " << exclusiveIterations << endl;
        cerr << "multiShared = " << multiShared << endl;

        BOOST_REQUIRE(!lock.isLockedByAnyThread());
    }

    {
        cerr << "multi exclusive" << endl;
        sharedIterations = exclusiveIterations = multiShared = finished = 0;
        ThreadGroup tg;
        for (unsigned i = 0;  i < nthreads;  ++i)
            tg.create_thread(exclusiveThread);
        sleep(1);
        finished = true;
        tg.join_all();
        BOOST_CHECK_EQUAL(errors, 0);
        cerr << "iterations: shared " << sharedIterations
             << " exclusive " << exclusiveIterations << endl;
        cerr << "multiShared = " << multiShared << endl;

        BOOST_REQUIRE(!lock.isLockedByAnyThread());
    }

    {
        cerr << "mixed shared and exclusive" << endl;
        sharedIterations = exclusiveIterations = multiShared = finished = 0;
        ThreadGroup tg;
        for (unsigned i = 0;  i < nthreads;  ++i)
            tg.create_thread(sharedThread);
        for (unsigned i = 0;  i < nthreads;  ++i)
            tg.create_thread(exclusiveThread);
        sleep(1);
        finished = true;
        tg.join_all();
        BOOST_CHECK_EQUAL(errors, 0);
        if (nthreads > 1)
            BOOST_CHECK_GE(multiShared, 0);
        cerr << "iterations: shared " << sharedIterations
             << " exclusive " << exclusiveIterations << endl;
        cerr << "multiShared = " << multiShared << endl;

        BOOST_REQUIRE(!lock.isLockedByAnyThread());
    }

    {
        cerr << "overflow" << endl;
        gcLockStartingEpoch = 0xFFFFFFF0;
        sharedIterations = exclusiveIterations = multiShared = finished = 0;
        ThreadGroup tg;
        tg.create_thread(sharedThread);
        sleep(1);
        finished = true;
        tg.join_all();
        BOOST_CHECK_EQUAL(errors, 0);
        cerr << "iterations: shared " << sharedIterations
             << " exclusive " << exclusiveIterations << endl;
        cerr << "multiShared = " << multiShared << endl;

        BOOST_REQUIRE(!lock.isLockedByAnyThread());
    }

    {
        cerr << "INT_MIN to INT_MAX" << endl;
        gcLockStartingEpoch = 0x7FFFFFF0;
        sharedIterations = exclusiveIterations = multiShared = finished = 0;
        ThreadGroup tg;
        tg.create_thread(sharedThread);
        sleep(1);
        finished = true;
        tg.join_all();
        BOOST_CHECK_EQUAL(errors, 0);
        cerr << "iterations: shared " << sharedIterations
             << " exclusive " << exclusiveIterations << endl;
        cerr << "multiShared = " << multiShared << endl;

        BOOST_REQUIRE(!lock.isLockedByAnyThread());
    }

    {
        cerr << "benign overflow" << endl;
        gcLockStartingEpoch = 0xBFFFFFF0;
        sharedIterations = exclusiveIterations = multiShared = finished = 0;
        ThreadGroup tg;
        tg.create_thread(sharedThread);
        sleep(1);
        finished = true;
        tg.join_all();
        BOOST_CHECK_EQUAL(errors, 0);
        cerr << "iterations: shared " << sharedIterations
             << " exclusive " << exclusiveIterations << endl;
        cerr << "multiShared = " << multiShared << endl;

        BOOST_REQUIRE(!lock.isLockedByAnyThread());
    }
}

#endif

#define USE_MALLOC 1

template<typename T>
struct Allocator {
    Allocator(int nblocks, T def = T())
        : def(def)
    {
        init(nblocks);
        highestAlloc = nallocs = ndeallocs = 0;
    }

    ~Allocator()
    {
#if ( ! USE_MALLOC )
        delete[] blocks;
        delete[] free;
#endif
    }

    T def;
    T * blocks;
    int * free;
    int nfree;
    std::atomic<int> highestAlloc;
    std::atomic<int> nallocs;
    std::atomic<int> ndeallocs;
    ML::Spinlock lock;

    void init(int nblocks)
    {
#if ( ! USE_MALLOC )
        blocks = new T[nblocks];
        free = new int[nblocks];

        std::fill(blocks, blocks + nblocks, def);

        nfree = 0;
        for (int i = nblocks - 1;  i >= 0;  --i)
            free[nfree++] = i;
#endif
    }

    T * alloc()
    {
#if USE_MALLOC
        nallocs += 1;

        // Atomic max operation
        {
            int current = highestAlloc;
            for (;;) {
                auto newValue = std::max(current,
                                         nallocs.load() - ndeallocs.load());
                if (newValue == current)
                    break;
                if (highestAlloc.compare_exchange_weak(current, newValue))
                    break;
            }
        }

        return new T(def);
#else
        std::lock_guard<ML::Spinlock> guard(lock);
        if (nfree == 0)
            throw ML::Exception("none free");
        int i = free[nfree - 1];
        highestAlloc = std::max(highestAlloc, i);
        T * result = blocks + i;
        --nfree;
        ++nallocs;
        return result;
#endif
    }

    void dealloc(T * value)
    {
        if (!value) return;
        *value = def;
#if USE_MALLOC
        delete value;
        ndeallocs += 1;
        return;
#else
        std::lock_guard<ML::Spinlock> guard(lock);
        int i = value - blocks;
        free[nfree++] = i;
        ++ndeallocs;
#endif
    }

    static void doDealloc(void * thisptr, void * blockPtr_, void * blockVar_)
    {
        int * & blockVar = *reinterpret_cast<int **>(blockVar_);
        int * blockPtr = reinterpret_cast<int *>(blockPtr_);
        ExcAssertNotEqual(blockVar, blockPtr);
        //blockVar = 0;
        //std::atomic_thread_fence(std::memory_order_seq_cst);
        //cerr << "blockPtr = " << blockPtr << endl;
        //int * blockPtr = reinterpret_cast<int *>(block);
        reinterpret_cast<Allocator *>(thisptr)->dealloc(blockPtr);
    }

    static void doDeallocAll(void * thisptr, void * blocksPtr_, void * numBlocks_)
    {
        size_t numBlocks = reinterpret_cast<size_t>(numBlocks_);
        int ** blocksPtr = reinterpret_cast<int **>(blocksPtr_);
        Allocator * alloc = reinterpret_cast<Allocator *>(thisptr);

        for (unsigned i = 0;  i != numBlocks;  ++i) {
            if (blocksPtr[i])
                alloc->dealloc(blocksPtr[i]);
        }

        delete[] blocksPtr;
    }
};

struct BlockHolder {
    BlockHolder(int ** p = nullptr)
        : block(p)
    {
    }
    
    BlockHolder & operator = (const BlockHolder & other)
    {
        block = other.block.load();
        return *this;
    }

    std::atomic<int **> block;

    int ** load() const { return block.load(); }

    operator int ** const () { return load(); }
};

template<typename Lock>
struct TestBase {
    TestBase(int nthreads, int nblocks, int nSpinThreads = 0)
        : finished(false),
          nthreads(nthreads),
          nblocks(nblocks),
          nSpinThreads(nSpinThreads),
          allocator(1024 * 1024, -1),
          nerrors(0),
          allBlocks(nthreads)
    {
        for (unsigned i = 0;  i < nthreads;  ++i) {
            allBlocks[i] = new int *[nblocks];
            std::fill(allBlocks[i].load(), allBlocks[i].load() + nblocks, (int *)0);
        }
    }

    ~TestBase()
    {
        for (unsigned i = 0;  i < nthreads;  ++i)
            delete[] allBlocks[i].load();
    }

    std::atomic<bool> finished;
    std::atomic<int> nthreads;
    std::atomic<int> nblocks;
    std::atomic<int> nSpinThreads;
    Allocator<int> allocator;
    Lock gc;
    uint64_t nerrors;

    /* All of the blocks are published here.  Any pointer which is read from
       here by another thread should always refer to exactly the same
       value.
    */
    vector<BlockHolder> allBlocks;

    void checkVisible(int threadNum, unsigned long long start)
    {
        // We're reading from someone else's pointers, so we need to lock here
        //gc.enterCS();
        gc.lockShared();

        for (unsigned i = 0;  i < nthreads;  ++i) {
            for (unsigned j = 0;  j < nblocks;  ++j) {
                //int * val = allBlocks[i][j];
                int * val = allBlocks[i].load()[j];
                if (val) {
                    int atVal = *val;
                    if (atVal != i) {
                        cerr << ML::format("%.6f thread %d: invalid value read "
                                "from thread %d block %d: %d\n",
                                (ticks() - start) / ticks_per_second, threadNum,
                                i, j, atVal);
                        nerrors += 1;
                        //abort();
                    }
                }
            }
        }

        //gc.exitCS();
        gc.unlockShared();
    }

    void doReadThread(int threadNum)
    {
        gc.getEntry();
        unsigned long long start = ticks();
        while (!finished) {
            checkVisible(threadNum, start);
        }
    }

    void doSpinThread()
    {
        while (!finished) {
        }
    }

    void allocThreadDefer(int threadNum)
    {
        gc.getEntry();
        try {
            uint64_t nErrors = 0;

            int ** blocks = allBlocks[threadNum];

            while (!finished) {

                int ** oldBlocks = new int * [nblocks];

                //gc.enterCS();

                for (unsigned i = 0;  i < nblocks;  ++i) {
                    int * block = allocator.alloc();
                    if (*block != -1) {
                        cerr << "old block was allocated" << endl;
                        ++nErrors;
                    }
                    *block = threadNum;
                    std::atomic_thread_fence(std::memory_order_seq_cst);
                    //rcu_set_pointer_sym((void **)&blocks[i], block);
                    int * oldBlock = blocks[i];
                    blocks[i] = block;
                    std::atomic_thread_fence(std::memory_order_seq_cst);
                    oldBlocks[i] = oldBlock;
                }

                gc.defer(Allocator<int>::doDeallocAll, &allocator, oldBlocks,
                         (void *)(size_t)nblocks);

                //gc.exitCS();
            }


            int * oldBlocks[nblocks];

            for (unsigned i = 0;  i < nblocks;  ++i) {
                oldBlocks[i] = blocks[i];
                blocks[i] = 0;
            }

            gc.visibleBarrier();

            //cerr << "at end" << endl;

            for (unsigned i = 0;  i < nblocks;  ++i)
                allocator.dealloc(oldBlocks[i]);

            //cerr << "nErrors = " << nErrors << endl;
        } catch (...) {
            static ML::Spinlock lock;
            lock.acquire();
            //cerr << "threadnum " << threadNum << " inEpoch "
            //     << gc.getEntry().inEpoch << endl;
            gc.dump();
            abort();
        }
    }

    void allocThreadSync(int threadNum)
    {
        gc.getEntry();
        try {
            uint64_t nErrors = 0;

            int ** blocks = allBlocks[threadNum];
            int * oldBlocks[nblocks];

            while (!finished) {

                for (unsigned i = 0;  i < nblocks;  ++i) {
                    int * block = allocator.alloc();
                    if (*block != -1) {
                        cerr << "old block was allocated" << endl;
                        ++nErrors;
                    }
                    *block = threadNum;
                    int * oldBlock = blocks[i];
                    blocks[i] = block;
                    oldBlocks[i] = oldBlock;
                }

                std::atomic_thread_fence(std::memory_order_seq_cst);
                gc.visibleBarrier();

                for (unsigned i = 0;  i < nblocks;  ++i)
                    if (oldBlocks[i]) *oldBlocks[i] = 1234;

                for (unsigned i = 0;  i < nblocks;  ++i)
                    if (oldBlocks[i]) allocator.dealloc(oldBlocks[i]);
            }

            for (unsigned i = 0;  i < nblocks;  ++i) {
                oldBlocks[i] = blocks[i];
                blocks[i] = 0;
            }

            gc.visibleBarrier();

            for (unsigned i = 0;  i < nblocks;  ++i)
                allocator.dealloc(oldBlocks[i]);

            //cerr << "nErrors = " << nErrors << endl;
        } catch (...) {
            static ML::Spinlock lock;
            lock.acquire();
            //cerr << "threadnum " << threadNum << " inEpoch "
            //     << gc.getEntry().inEpoch << endl;
            gc.dump();
            abort();
        }
    }

    void run(std::function<void (int)> allocFn,
             int runTime = 1)
    {
        gc.getEntry();
        ThreadGroup tg;

        for (unsigned i = 0;  i < nthreads;  ++i)
            tg.create_thread(std::bind<void>(&TestBase::doReadThread, this, i));

        for (unsigned i = 0;  i < nthreads;  ++i)
            tg.create_thread(std::bind<void>(allocFn, i));

        for (unsigned i = 0;  i < nSpinThreads;  ++i)
            tg.create_thread(std::bind<void>(&TestBase::doSpinThread, this));

        sleep(runTime);

        finished = true;

        tg.join_all();

        gc.deferBarrier();

        gc.dump();

        BOOST_CHECK_EQUAL(allocator.nallocs, allocator.ndeallocs);
        BOOST_CHECK_EQUAL(nerrors, 0);

        cerr << "allocs " << allocator.nallocs
             << " deallocs " << allocator.ndeallocs << endl;
        cerr << "highest " << allocator.highestAlloc << endl;

        cerr << "gc.currentEpoch() = " << gc.currentEpoch() << endl;
    }
};

#if 1
BOOST_AUTO_TEST_CASE ( test_gc_sync_many_threads_contention )
{
    cerr << "testing contention synchronized GcLock with many threads" << endl;

    int nthreads = 8;
    int nSpinThreads = 16;
    int nblocks = 2;

    TestBase<GcLock> test(nthreads, nblocks, nSpinThreads);
    test.run(std::bind(&TestBase<GcLock>::allocThreadSync, &test,
                       std::placeholders::_1));

    BOOST_REQUIRE(!test.gc.isLockedByAnyThread());
}
#endif

BOOST_AUTO_TEST_CASE ( test_gc_deferred_contention )
{
    cerr << "testing contended deferred GcLock" << endl;

    int nthreads = 8;
    int nSpinThreads = 0;//16;
    int nblocks = 2;

    TestBase<GcLock> test(nthreads, nblocks, nSpinThreads);
    test.run(std::bind(&TestBase<GcLock>::allocThreadDefer, &test,
                       std::placeholders::_1));

    BOOST_REQUIRE(!test.gc.isLockedByAnyThread());
}


#if 1

BOOST_AUTO_TEST_CASE ( test_gc_sync )
{
    cerr << "testing synchronized GcLock" << endl;

    int nthreads = 2;
    int nblocks = 2;

    TestBase<GcLock> test(nthreads, nblocks);
    test.run(std::bind(&TestBase<GcLock>::allocThreadSync, &test,
                       std::placeholders::_1));

    BOOST_REQUIRE(!test.gc.isLockedByAnyThread());
}

BOOST_AUTO_TEST_CASE ( test_gc_sync_many_threads )
{
    cerr << "testing synchronized GcLock with many threads" << endl;

    int nthreads = 8;
    int nblocks = 2;

    TestBase<GcLock> test(nthreads, nblocks);
    test.run(std::bind(&TestBase<GcLock>::allocThreadSync, &test,
                       std::placeholders::_1));

    BOOST_REQUIRE(!test.gc.isLockedByAnyThread());
}

BOOST_AUTO_TEST_CASE ( test_gc_deferred )
{
    cerr << "testing deferred GcLock" << endl;

    int nthreads = 2;
    int nblocks = 2;

    TestBase<GcLock> test(nthreads, nblocks);
    test.run(std::bind(&TestBase<GcLock>::allocThreadDefer, &test,
                       std::placeholders::_1));

    BOOST_REQUIRE(!test.gc.isLockedByAnyThread());
}


struct SharedGcLockProxy : public SharedGcLock {
    static const char* name;
    SharedGcLockProxy() :
        SharedGcLock(GC_OPEN, name)
    {}
};
const char* SharedGcLockProxy::name = "gc_test.dat";

BOOST_AUTO_TEST_CASE( test_shared_lock_sync )
{
    cerr << "testing contention synchronized GcLock with shared lock" << endl;

    SharedGcLock lockGuard(GC_CREATE, SharedGcLockProxy::name);
    Call_Guard unlink_guard([&] { lockGuard.unlink(); });

    int nthreads = 8;
    int nSpinThreads = 16;
    int nblocks = 2;

    TestBase<SharedGcLockProxy> test(nthreads, nblocks, nSpinThreads);
    test.run(std::bind(
                    &TestBase<SharedGcLockProxy>::allocThreadSync, &test,
                    std::placeholders::_1));

}

BOOST_AUTO_TEST_CASE( test_shared_lock_defer )
{
    cerr << "testing contended deferred GcLock with shared lock" << endl;

    SharedGcLock lockGuard(GC_CREATE, SharedGcLockProxy::name);
    Call_Guard unlink_guard([&] { lockGuard.unlink(); });

    int nthreads = 8;
    int nSpinThreads = 16;
    int nblocks = 2;

    TestBase<SharedGcLockProxy> test(nthreads, nblocks, nSpinThreads);
    test.run(std::bind(
                    &TestBase<SharedGcLockProxy>::allocThreadSync, &test,
                    std::placeholders::_1));
}

BOOST_AUTO_TEST_CASE ( test_defer_race )
{
    cerr << "testing defer race" << endl;
    GcLock gc;

    ThreadGroup tg;

    volatile bool finished = false;

    int nthreads = 0;

    std::atomic<int> numStarted(0);

    auto doTestThread = [&] ()
        {
            while (!finished) {
                numStarted += 1;
                while (numStarted != nthreads) ;

                gc.deferBarrier();

                numStarted -= 1;
                while (numStarted != 0) ;
            }
        };


    for (unsigned i = 0;  i < nthreads;  ++i)
        tg.create_thread(doTestThread);

    int runTime = 1;

    sleep(runTime);

    finished = true;

    tg.join_all();

    BOOST_REQUIRE(!gc.isLockedByAnyThread());
}

#endif
