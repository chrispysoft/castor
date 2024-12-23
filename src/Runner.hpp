#include <csignal>

#define CST_ENGINE true

#ifdef CST_ENGINE
#include "engine/EngineRunner.hpp"
#else
#include "core/CoreRunner.hpp"
#endif

namespace cst {
class Runner {
    #ifdef CST_ENGINE
    EngineRunner mRunner;
    #else
    CoreRunner mRunner;
    #endif
public:
    Runner() :
        mRunner()
    {
        std::signal(SIGINT,  handlesig);
        std::signal(SIGTERM, handlesig);
        std::signal(SIGPIPE, handlesig);
    }

    void run() {
        mRunner.run();
    }

    void terminate() {
        mRunner.terminate();
    }

    static Runner& instance() {
        static Runner instance;
        return instance;
    }

private:
    static void handlesig(int sig) {
        log.debug() << "Runner received signal " << sig;
        if (sig == SIGPIPE) {
            log.error() << "Broken pipe";
        } else {
            instance().terminate();
        }
    }
};
}