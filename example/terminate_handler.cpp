// Copyright Antony Polukhin, 2016-2017.
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)

#include <boost/array.hpp>
BOOST_NOINLINE void foo(int i);
BOOST_NOINLINE void bar(int i);
 
BOOST_NOINLINE void bar(int i) {
    boost::array<int, 5> a = {{-1, -231, -123, -23, -32}};
    if (i >= 0) {
        foo(a[i]);
    } else {
        std::terminate();
    }
}

BOOST_NOINLINE void foo(int i) {
    bar(--i);
}

#if defined(BOOST_GCC) && defined(BOOST_WINDOWS)

// MinGW workaround
#include <cstdlib>      // ::_Exit
namespace std { using ::_Exit; }

#endif

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//[getting_started_terminate_handlers

#include <signal.h>     // ::signal
#include <cstdlib>      // std::_Exit
#include <boost/stacktrace.hpp>

void my_signal_handler(int signum) {
    ::signal(signum, SIG_DFL);
    boost::stacktrace::safe_dump_to("./backtrace.dump");
    std::_Exit(-1);
}
//]

void setup_handlers() {
//[getting_started_setup_handlers
    ::signal(SIGSEGV, &my_signal_handler);
    ::signal(SIGABRT, &my_signal_handler);
//]
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

BOOST_CONSTEXPR_OR_CONST std::size_t shared_memory_size = 4096 * 8;

//[getting_started_terminate_handlers_shmem
#include <boost/stacktrace.hpp>
#include <boost/interprocess/shared_memory_object.hpp>
#include <boost/interprocess/mapped_region.hpp>

boost::interprocess::shared_memory_object g_shm; // inited at program start
boost::interprocess::mapped_region g_region;     // inited at program start


void my_signal_handler2(int signum) {
    ::signal(signum, SIG_DFL);
    bool* b = static_cast<bool*>(g_region.get_address());
    *b = true;                                  // flag that memory constains stacktrace
    boost::stacktrace::safe_dump_to(b + 1, g_region.get_size() - sizeof(bool));
    std::_Exit(-1);
}
//]

#include <iostream>     // std::cerr
#include <fstream>     // std::ifstream
#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>


inline void copy_and_run(const char* exec_name, char param, bool not_null) {
    std::cout << "Running with param " << param << std::endl;
    boost::filesystem::path command = exec_name;
    command = command.parent_path() / (command.stem().string() + param + command.extension().string());
    boost::filesystem::copy_file(exec_name, command, boost::filesystem::copy_option::overwrite_if_exists);

    boost::filesystem::path command_args = command;
    command_args += ' ';
    command_args += param;
    const int ret = std::system(command_args.string().c_str());

    std::cout << "End Running with param " << param << "; ret code is " << ret << std::endl;
    boost::filesystem::remove(command);
    if (not_null && !ret) {
        std::exit(97);
    } else if (!not_null && ret) {
        std::exit(ret);
    }
}

int run_1(const char* /*argv*/[]) {
    setup_handlers();
    foo(5);
    return 11;
}

int run_2(const char* argv[]) {
    if (!boost::filesystem::exists("./backtrace.dump")) {
        if (std::string(argv[0]).find("noop") == std::string::npos) {
            return 21;
        }

        boost::stacktrace::stacktrace st = boost::stacktrace::stacktrace::from_dump(std::cin);
        if (st) {
            return 22;
        }
        return 0;
    }

//[getting_started_on_program_restart
    if (boost::filesystem::exists("./backtrace.dump")) {
        // there is a backtrace
        std::ifstream ifs("./backtrace.dump");

        boost::stacktrace::stacktrace st = boost::stacktrace::stacktrace::from_dump(ifs);
        std::cout << "Previous run crashed:\n" << st << std::endl; /*<-*/

        if (!st) {
            return 23;
        } /*->*/

        // cleaning up
        ifs.close();
        boost::filesystem::remove("./backtrace.dump");
    }
//]

    return 0;
}


int run_3(const char* /*argv*/[]) {
    using namespace boost::interprocess;
    {
        shared_memory_object shm_obj(open_or_create, "shared_memory", read_write);
        shm_obj.swap(g_shm);
    }
    g_shm.truncate(shared_memory_size);

    {
        mapped_region m(g_shm, read_write, 0, shared_memory_size);
        m.swap(g_region);
    }
    bool* b = static_cast<bool*>(g_region.get_address());
    *b = false;

    ::signal(SIGSEGV, &my_signal_handler2);
    ::signal(SIGABRT, &my_signal_handler2);
    foo(5);
    return 31;
}

int run_4(const char* argv[]) {
    using namespace boost::interprocess;
    {
        shared_memory_object shm_obj(open_only, "shared_memory", read_write);
        shm_obj.swap(g_shm);
    }

    {
        mapped_region m(g_shm, read_write, 0, shared_memory_size);
        m.swap(g_region);
    }

//[getting_started_on_program_restart_shmem
    bool* b = static_cast<bool*>(g_region.get_address());   // getting flag that memory constains stacktrace
    if (*b) {                                               // checking that memory constains stacktrace
        boost::stacktrace::stacktrace st 
            = boost::stacktrace::stacktrace::from_dump(b + 1, g_region.get_size() - sizeof(bool));

        std::cout << "Previous run crashed and left trace in shared memory:\n" << st << std::endl;
        *b = false; /*<-*/
        shared_memory_object::remove("shared_memory");
        if (std::string(argv[0]).find("noop") == std::string::npos) {
            if (!st) {
                return 43;
            }
        } else {
           if (st) {
                return 44;
            }
        }
    } else {
        return 42; /*->*/
    }
//]


    return 0;
}

#include <sstream>

int test_inplace() {
    const bool is_noop = !boost::stacktrace::stacktrace();

    {
        boost::stacktrace::safe_dump_to("./backtrace2.dump"); 
        boost::stacktrace::stacktrace ss2;
        std::ifstream ifs("./backtrace2.dump");
        boost::stacktrace::stacktrace ss1 = boost::stacktrace::stacktrace::from_dump(ifs);
        ifs.close();
        boost::filesystem::remove("./backtrace2.dump");

        if (ss1.size() != ss2.size()) {
            std::cerr << "Stacktraces differ:\n" << ss1 << "\n vs \n" << ss2 << '\n';
            return 51;
        }

        if (ss1 && ss1[0].name() != ss2[0].name()) {
            std::cerr << "Stacktraces differ:\n" << ss1 << "\n vs \n" << ss2 << '\n';
            return 52;
        }
    }

    {
        void* data[1024];
        boost::stacktrace::safe_dump_to(data, sizeof(data));
        boost::stacktrace::stacktrace ss2;
        boost::stacktrace::stacktrace ss1 = boost::stacktrace::stacktrace::from_dump(data, sizeof(data));

        if (ss1.size() != ss2.size()) {
            std::cerr << "Stacktraces differ:\n" << ss1 << "\n vs \n" << ss2 << '\n';
            return 53;
        }

        if (ss1 && ss1[0].name() != ss2[0].name()) {
            std::cerr << "Stacktraces differ:\n" << ss1 << "\n vs \n" << ss2 << '\n';
            return 54;
        }
    }

    {
        void* data[1024];
        boost::stacktrace::safe_dump_to(1024, data, sizeof(data));
        if (boost::stacktrace::stacktrace::from_dump(data, sizeof(data))) {
            std::cerr << "Stacktrace not empty!\n";
            return 55;
        }
    }

    {
        void* data[1024];
        boost::stacktrace::safe_dump_to(1, data, sizeof(data));
        if (!is_noop && !boost::stacktrace::stacktrace::from_dump(data, sizeof(data))) {
            std::cerr << "Stacktrace empty!\n";
            return 56;
        }
        const std::size_t size_1_skipped = boost::stacktrace::stacktrace::from_dump(data, sizeof(data)).size();
        boost::stacktrace::safe_dump_to(0, data, sizeof(data));
        const std::size_t size_0_skipped = boost::stacktrace::stacktrace::from_dump(data, sizeof(data)).size();

        if (!is_noop && (size_1_skipped + 1 != size_0_skipped)) {
            std::cerr << "failed to skip 1 frame!\n";
            return 57;
        }
    }

    {
        boost::stacktrace::safe_dump_to(0, 1, "./backtrace3.dump");
        std::ifstream ifs("./backtrace3.dump");
        boost::stacktrace::stacktrace ss1 = boost::stacktrace::stacktrace::from_dump(ifs);
        ifs.close();

        boost::stacktrace::safe_dump_to(1, 1, "./backtrace3.dump");
        ifs.open("./backtrace3.dump");
        boost::stacktrace::stacktrace ss2 = boost::stacktrace::stacktrace::from_dump(ifs);
        ifs.close();

        boost::filesystem::remove("./backtrace3.dump");

        if (ss1.size() != ss2.size()) {
            std::cerr << "Stacktraces differ:\n" << ss1 << "\n vs \n" << ss2 << '\n';
            return 58;
        }

        if (!is_noop && ss1.size() != 1) {
            std::cerr << "Stacktraces does not have size 1:\n" << ss1 << '\n';
            return 59;
        }

        if (ss1 && ss1[0].address() == ss2[0].address()) {
            std::cerr << "Stacktraces must differ:\n" << ss1 << "\n vs \n" << ss2 << '\n';
            return 60;
        }
    }

    return 0;
}


int main(int argc, const char* argv[]) {
    if (argc < 2) {
        // We are copying files to make sure that stacktrace printing works independently from executable name
        copy_and_run(argv[0], '1', true);
        copy_and_run(argv[0], '2', false);
        copy_and_run(argv[0], '3', true);
        copy_and_run(argv[0], '4', false);

        return test_inplace();
    }

    switch (argv[1][0]) {
    case '1': return run_1(argv);
    case '2': return run_2(argv);
    case '3': return run_3(argv);
    case '4': return run_4(argv);
    }

    return 404;
}


