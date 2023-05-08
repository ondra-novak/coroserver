#include <coroserver/shared_lockable_ptr.h>
#include <iostream>
#include <memory>

class TestObject {
public:
    int val;
    TestObject(int val):val(val) {
        std::cout << "TestObject construct: " <<  val << std::endl;
    }
    ~TestObject() {
        std::cout << "TestObject destruct: " <<  val << std::endl;
    }
    void print() const {
        std::cout << "TestObject const print: " <<  val << std::endl;
    }
    void print() {
        std::cout << "TestObject non-const print: " <<  val << std::endl;
    }
    TestObject(const TestObject &) = delete;
};

int main() {

    {
        auto x = coroserver::make_shared_lockable<TestObject>(10);

        x.lock()->print();
        x.lock_shared()->print();
    }

    {
        auto y = coroserver::shared_lockable_ptr<TestObject>(new TestObject(20));

        y.lock()->print();
        y.lock_shared()->print();
    }

    coroserver::weak_lockable_ptr<TestObject> w;
    {
        auto z = coroserver::shared_lockable_ptr<TestObject>(new TestObject(30), [](TestObject *ptr){
            std::cout << "Custom deleter." << std::endl;
            delete ptr;
        });

        w = z;
        z.lock()->print();
        z.lock_shared()->print();
        auto a = w.lock();
        std::cout << "from weak ";
        a.lock()->print();
    }

    {
        auto a = w.lock();
        if (!a) {
            std::cout << "a == nullptr" << std::endl;
        }
    }



    return 0;
}
