#include <atomic>
#include <iostream>
#include <map>
#include <thread>

using namespace std;

template <int I>
struct MTIndex {};

// 测试结果记录
struct MTTest {
    static inline int result = 0;

    template <class TestStruct>
    void runTest(int expected_ans) {
        static const int TEST_TIMES = 100000;
        int success = 0;
        map<int, int> res_cnts_mp;
        for (int i = 0; i < TEST_TIMES; ++i) {
            TestStruct test;
            result = 0;
            // 创建两个线程
            thread t1([&test]() { test.entry(MTIndex<0>{}); });
            thread t2([&test]() { test.entry(MTIndex<1>{}); });
            t1.join();
            t2.join();
            ++res_cnts_mp[result];
            if (result == expected_ans) [[likely]] {
                ++success;
            }
        }
        cout << typeid(TestStruct).name() << " 中：" << success << "/" << TEST_TIMES << " ("
             << (success * 100.0 / TEST_TIMES) << "%)\n";
        for (const auto& [v, cnt] : res_cnts_mp) {
            cout << "  " << v << ": " << cnt << " times.\n";
        }
    }
};

// 测试用例
struct TestNaive {
    alignas(64) int data = 0;
    char space[64];
    alignas(64) int ready = 0;
    void entry(MTIndex<0>) {
        data = 42;
        ready = 1;
    }
    void entry(MTIndex<1>) {
        while (ready == 0)
            ;
        MTTest::result = data;
    }
};

struct TestVolatile {
    alignas(64) int data = 0;
    char space[64];
    alignas(64) volatile int ready = 0;
    void entry(MTIndex<0>) {
        data = 42;
        ready = 1;
    }
    void entry(MTIndex<1>) {
        while (ready == 0)
            ;
        MTTest::result = data;
    }
};

struct TestRelaxed {
    alignas(64) int data = 0;
    char space[64];
    alignas(64) atomic_int ready{0};
    void entry(MTIndex<0>) {
        data = 42;
        ready.store(1, memory_order::relaxed);
    }
    void entry(MTIndex<1>) {
        while (ready.load(memory_order::relaxed) == 0)
            ;
        MTTest::result = data;
    }
};

struct TestAcquireRelease {
    alignas(64) int data = 0;
    char space[64];
    alignas(64) atomic_int ready{0};
    void entry(MTIndex<0>) {
        data = 42;
        ready.store(1, memory_order::release);
    }
    void entry(MTIndex<1>) {
        while (ready.load(memory_order::acquire) == 0)
            ;
        MTTest::result = data;
    }
};

struct TestSeqCst {
    alignas(64) int data = 0;
    char space[64];
    alignas(64) atomic_int ready{0};
    void entry(MTIndex<0>) {
        data = 42;
        ready.store(1, memory_order::seq_cst);
    }
    void entry(MTIndex<1>) {
        while (ready.load(memory_order::seq_cst) == 0)
            ;
        MTTest::result = data;
    }
};

int main() {
    cout << "开始内存序测试...(可能比较久)\n";
    MTTest t;
    t.runTest<TestNaive>(42);          // 预期失败率高
    t.runTest<TestVolatile>(42);       // 预期可能失败
    t.runTest<TestRelaxed>(42);        // 预期可能失败
    t.runTest<TestAcquireRelease>(42); // 预期100%成功
    t.runTest<TestSeqCst>(42);         // 预期100%成功
    cout << "\n测试完成！\n";
    return 0;
}