#include <csignal>
#include "core/CoreRunner.hpp"

namespace cst {
class Runner {
    CoreRunner mCoreRunner;
public:
    Runner() :
        mCoreRunner()
    {
        std::signal(SIGINT,  handlesig);
        std::signal(SIGTERM, handlesig);
    }

    void run() {
        mCoreRunner.run();
    }

    void terminate() {
        mCoreRunner.terminate();
    }

    static Runner& instance() {
        static Runner instance;
        return instance;
    }

private:
    static void handlesig(int sig) {
        // std::cout << "Runner received signal " << sig << std::endl;
        instance().terminate();
    }
};
}