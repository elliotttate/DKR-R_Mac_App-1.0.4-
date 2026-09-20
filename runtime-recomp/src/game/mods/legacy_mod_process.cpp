#include "legacy_mod_process.hpp"
#include "legacy_mod_format.hpp"
#include <thread>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <spawn.h>
#if defined(__linux__)
#include <sys/prctl.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#endif
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace dkr::mods {
namespace {
constexpr std::size_t WorkerMemory=1024*MiB;
#if defined(_WIN32)
struct Handle {
    HANDLE value=nullptr;
    ~Handle(){if(value && value!=INVALID_HANDLE_VALUE) CloseHandle(value);}
};
std::wstring quoted(const std::filesystem::path& path) {
    // Windows CRT command-line rules, including backslashes before quotes.
    std::wstring result=L"\"";
    unsigned slash=0;
    for(const auto c : path.native()) {
        if(c==L'\\') {++slash;continue;}
        result.append(c==L'"'?slash*2+1:slash,L'\\');slash=0;
        result+=c;
    }
    result.append(slash*2,L'\\');return result+L'"';
}
#endif
}
void constrain_import_worker() {
#if !defined(_WIN32)
    const auto parent=getppid();
#if defined(__linux__)
    if(parent==1 || prctl(PR_SET_PDEATHSIG,SIGKILL) || getppid()!=parent)
        throw Error("The import worker lost its parent process.");
#else
    if(parent==1) throw Error("The import worker lost its parent process.");
#endif
    const rlimit memory{WorkerMemory,WorkerMemory}, cpu{120,120},
        output{MaxStaged+2*MiB,MaxStaged+2*MiB}, no_core{0,0};
    if(setrlimit(RLIMIT_CPU,&cpu) || setrlimit(RLIMIT_FSIZE,&output) ||
        setrlimit(RLIMIT_CORE,&no_core))
        throw Error("Could not establish the import worker resource limits.");
#if defined(__linux__)
    if(setrlimit(RLIMIT_AS,&memory) || prctl(PR_SET_NO_NEW_PRIVS,1,0,0,0))
        throw Error("Could not establish the import worker resource limits.");
#elif defined(__APPLE__)
    // Darwin has no PR_SET_PDEATHSIG and does not enforce RLIMIT_AS. Bound
    // resident memory and parent lifetime in this private, single-job process.
    std::thread([parent] {
        for (;;) {
            mach_task_basic_info_data_t info{};
            mach_msg_type_number_t count=MACH_TASK_BASIC_INFO_COUNT;
            if(getppid()!=parent || task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                reinterpret_cast<task_info_t>(&info), &count)!=KERN_SUCCESS ||
                info.resident_size>WorkerMemory) _exit(1);
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }
    }).detach();
#endif
#endif
}
WorkerResult run_worker(const std::filesystem::path& executable,const std::filesystem::path& request,
    const std::function<bool()>& keep_running,std::chrono::milliseconds timeout) {
    if(!executable.is_absolute() || !request.is_absolute() || timeout.count()<=0)
        throw Error("Importer requires absolute private executable/request paths and a bounded timeout.");
    if(keep_running && !keep_running()) return {WorkerOutcome::Cancelled,0};
    const auto deadline=std::chrono::steady_clock::now()+timeout;
#if defined(_WIN32)
    Handle job{CreateJobObjectW(nullptr,nullptr)};
    if(!job.value) throw Error("Could not create an isolated import job.");
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE |
        JOB_OBJECT_LIMIT_PROCESS_MEMORY | JOB_OBJECT_LIMIT_PROCESS_TIME |
        JOB_OBJECT_LIMIT_ACTIVE_PROCESS;
    limits.BasicLimitInformation.ActiveProcessLimit=1;
    limits.BasicLimitInformation.PerProcessUserTimeLimit.QuadPart=120LL*10000000;
    limits.ProcessMemoryLimit=WorkerMemory;
    if(!SetInformationJobObject(job.value,JobObjectExtendedLimitInformation,&limits,sizeof(limits)))
        throw Error("Could not set the import job's resource limits.");
    auto command=quoted(executable)+L" "+quoted(request);
    STARTUPINFOW startup{};startup.cb=sizeof(startup);
    PROCESS_INFORMATION info{};
    if(!CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,FALSE,
        CREATE_NO_WINDOW|CREATE_SUSPENDED,nullptr,executable.parent_path().c_str(),&startup,&info))
        throw Error("The private mod importer could not be started.");
    Handle process{info.hProcess},thread{info.hThread};
    if(!AssignProcessToJobObject(job.value,process.value) || ResumeThread(thread.value)==DWORD(-1)) {
        TerminateProcess(process.value,1);WaitForSingleObject(process.value,5000);
        throw Error("The import worker could not enter its bounded job.");
    }
    // Declared after process so exceptions from callbacks terminate/reap before
    // its handle closes. The job also kills the worker if the app exits.
    struct Reap {
        HANDLE job,process;bool done=false;
        ~Reap(){if(!done){TerminateJobObject(job,1);WaitForSingleObject(process,5000);}}
    } reap{job.value,process.value};
    while(true) {
        const auto status=WaitForSingleObject(process.value,25);
        if(status==WAIT_OBJECT_0) {
            DWORD code=1;
            if(!GetExitCodeProcess(process.value,&code)) throw Error("Could not read import completion.");
            reap.done=true;
            return {code?WorkerOutcome::Failed:WorkerOutcome::Completed,code};
        }
        if(status==WAIT_FAILED) throw Error("Could not monitor the import worker.");
        if(keep_running && !keep_running()) return {WorkerOutcome::Cancelled,0};
        if(std::chrono::steady_clock::now()>=deadline) return {WorkerOutcome::TimedOut,0};
    }
#else
    auto exe=executable.native(),argument=request.native();
    char* arguments[]={exe.data(),argument.data(),nullptr};
    posix_spawnattr_t attributes;
    if(posix_spawnattr_init(&attributes)) throw Error("Could not initialize the import process.");
    struct Attr {posix_spawnattr_t& value;~Attr(){posix_spawnattr_destroy(&value);}} attr{attributes};
    if(posix_spawnattr_setflags(&attributes,POSIX_SPAWN_SETPGROUP) || posix_spawnattr_setpgroup(&attributes,0))
        throw Error("Could not isolate the import process group.");
    pid_t child=0;
    if(posix_spawn(&child,exe.c_str(),nullptr,&attributes,arguments,environ))
        throw Error("The private mod importer could not be started.");
    struct Reap {
        pid_t child;bool done=false;
        ~Reap(){if(!done){kill(-child,SIGKILL);int status;while(waitpid(child,&status,0)<0 && errno==EINTR){}}}
    } reap{child};
    while(true) {
        int status=0;
        const auto waited=waitpid(child,&status,WNOHANG);
        if(waited==child) {
            reap.done=true;
            if(WIFEXITED(status)) return {WEXITSTATUS(status)?WorkerOutcome::Failed:WorkerOutcome::Completed,
                                         static_cast<unsigned>(WEXITSTATUS(status))};
            return {WorkerOutcome::Failed,static_cast<unsigned>(WIFSIGNALED(status)?128+WTERMSIG(status):1)};
        }
        if(waited<0 && errno!=EINTR) throw Error("Could not monitor the import worker.");
        if(keep_running && !keep_running()) return {WorkerOutcome::Cancelled,0};
        if(std::chrono::steady_clock::now()>=deadline) return {WorkerOutcome::TimedOut,0};
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
#endif
}
} // namespace dkr::mods
