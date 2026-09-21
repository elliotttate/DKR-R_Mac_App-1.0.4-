#include "../src/macos/crash_log.hpp"
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sys/wait.h>
#include <unistd.h>

int main() {
    char name[]="/tmp/dkr-crash-test.XXXXXX";
    const char* created=mkdtemp(name);
    if (!created) return 1;
    const std::filesystem::path root=created;
    auto fail=[&](const char* message) { std::cerr<<message<<'\n'; std::filesystem::remove_all(root); return 1; };
    std::filesystem::create_directories(root/"logs");
    for (int i=0;i<12;++i) {
        std::ofstream(root/"logs/runtime.log")<<"launch "<<i<<'\n';
        dkr::macos::archive_runtime_log(root/"logs");
    }
    std::size_t count=0; bool newest=false;
    for (const auto& file: std::filesystem::directory_iterator(root/"logs/history")) {
        ++count; std::ifstream input(file.path()); std::string text; std::getline(input,text);
        newest |= text=="launch 11";
    }
    if (count!=8 || !newest) return fail("history retention failed");
    for (bool enabled: {true,false}) {
        const auto dir=root/(enabled?"enabled":"disabled");
        const pid_t child=fork();
        if (child<0) return fail("fork failed");
        if (child==0) {
            dkr::macos::install_crash_logging(dir,enabled);
            raise(SIGABRT); _exit(99);
        }
        int status=0; if (waitpid(child,&status,0)!=child || !WIFSIGNALED(status) || WTERMSIG(status)!=SIGABRT)
            return fail("handler suppressed native signal termination");
        bool record=false;
        if (std::filesystem::exists(dir)) for (const auto& file:std::filesystem::directory_iterator(dir)) {
            std::ifstream input(file.path()); std::string text((std::istreambuf_iterator<char>(input)),{});
            record |= text.find("signal=0x")!=std::string::npos && text.find("image_base=0x")!=std::string::npos && text.find("pc=0x")!=std::string::npos;
        }
        if (record!=enabled) return fail("crash record preference or contents failed");
    }
    std::filesystem::remove_all(root);
    std::cout<<"PASS: bounded log history, fatal record and native SIGABRT, disabled record preference\n";
}
