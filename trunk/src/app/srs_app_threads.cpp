/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2020 Winlin
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <srs_app_threads.hpp>

#include <srs_kernel_error.hpp>
#include <srs_app_config.hpp>
#include <srs_app_log.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_app_utility.hpp>
#include <srs_app_hybrid.hpp>
#include <srs_app_source.hpp>
#include <srs_app_rtc_server.hpp>
#include <srs_app_conn.hpp>
#include <srs_app_rtc_source.hpp>
#include <srs_kernel_rtc_rtp.hpp>
#include <srs_app_pithy_print.hpp>

#include <unistd.h>
#include <fcntl.h>

#ifdef SRS_OSX
    pid_t gettid() {
        return 0;
    }
#else
    #if __GLIBC__ == 2 && __GLIBC_MINOR__ < 30
        #include <sys/syscall.h>
        #define gettid() syscall(SYS_gettid)
    #endif
#endif

using namespace std;

#include <srs_protocol_kbps.hpp>

extern SrsPps* _srs_pps_rloss;
extern SrsPps* _srs_pps_aloss;
extern SrsPps* _srs_pps_aloss2;

extern SrsPps* _srs_pps_snack2;
extern SrsPps* _srs_pps_snack3;
extern SrsPps* _srs_pps_snack4;

SrsPps* _srs_thread_sync_10us = new SrsPps();
SrsPps* _srs_thread_sync_100us = new SrsPps();
SrsPps* _srs_thread_sync_1000us = new SrsPps();
SrsPps* _srs_thread_sync_plus = new SrsPps();

SrsPps* _srs_tunnel_recv_raw = new SrsPps();
SrsPps* _srs_tunnel_recv_hit = new SrsPps();
SrsPps* _srs_tunnel_send_raw = new SrsPps();
SrsPps* _srs_tunnel_send_hit = new SrsPps();

extern bool srs_is_rtp_or_rtcp(const uint8_t* data, size_t len);
extern bool srs_is_rtcp(const uint8_t* data, size_t len);

uint64_t srs_covert_cpuset(cpu_set_t v)
{
#ifdef SRS_OSX
    return v;
#else
    uint64_t iv = 0;
    for (int i = 0; i <= 63; i++) {
        if (CPU_ISSET(i, &v)) {
            iv |= uint64_t(1) << i;
        }
    }
    return iv;
#endif
}

SrsPipe::SrsPipe()
{
    pipes_[0] = pipes_[1] = -1;
}

SrsPipe::~SrsPipe()
{
    // Close the FDs because we might not open it as stfd.
    if (pipes_[0] > 0) {
        ::close(pipes_[0]);
    }
    if (pipes_[1] > 0) {
        ::close(pipes_[1]);
    }
}

srs_error_t SrsPipe::initialize()
{
    srs_error_t err = srs_success;

    if (pipes_[0] > 0) {
        return err;
    }

    if (pipe(pipes_) < 0) {
        return srs_error_new(ERROR_SYSTEM_CREATE_PIPE, "create pipe");
    }

    return err;
}

int SrsPipe::read_fd()
{
    return pipes_[0];
}

int SrsPipe::write_fd()
{
    return pipes_[1];
}

SrsThreadPipe::SrsThreadPipe()
{
    stfd_ = NULL;
}

SrsThreadPipe::~SrsThreadPipe()
{
    srs_close_stfd(stfd_);
}

srs_error_t SrsThreadPipe::initialize(int fd)
{
    srs_error_t err = srs_success;

    if (stfd_) {
        return err;
    }

    if ((stfd_ = srs_netfd_open(fd)) == NULL) {
        return srs_error_new(ERROR_PIPE_OPEN, "open pipe");
    }

    return err;
}

srs_error_t SrsThreadPipe::read(void* buf, size_t size, ssize_t* nread)
{
    ssize_t nn = srs_read(stfd_, buf, size, SRS_UTIME_NO_TIMEOUT);

    if (nread) {
        *nread = nn;
    }

    if (nn < 0) {
        return srs_error_new(ERROR_PIPE_READ, "read");
    }

    return srs_success;
}

srs_error_t SrsThreadPipe::write(void* buf, size_t size, ssize_t* nwrite)
{
    ssize_t nn = srs_write(stfd_, buf, size, SRS_UTIME_NO_TIMEOUT);

    if (nwrite) {
        *nwrite = nn;
    }

    if (nn < 0) {
        return srs_error_new(ERROR_PIPE_WRITE, "write");
    }

    return srs_success;
}

SrsThreadPipePair::SrsThreadPipePair()
{
    pipe_ = new SrsPipe();
    rpipe_ = new SrsThreadPipe();
    wpipe_ = new SrsThreadPipe();
}

SrsThreadPipePair::~SrsThreadPipePair()
{
    close_read();
    close_write();
    srs_freep(pipe_);
}

srs_error_t SrsThreadPipePair::initialize()
{
    return pipe_->initialize();
}

srs_error_t SrsThreadPipePair::open_read()
{
    return rpipe_->initialize(pipe_->read_fd());
}

srs_error_t SrsThreadPipePair::open_write()
{
    return wpipe_->initialize(pipe_->write_fd());
}

void SrsThreadPipePair::close_read()
{
    srs_freep(rpipe_);
}

void SrsThreadPipePair::close_write()
{
    srs_freep(wpipe_);
}

srs_error_t SrsThreadPipePair::read(void* buf, size_t size, ssize_t* nread)
{
    return rpipe_->read(buf, size, nread);
}

srs_error_t SrsThreadPipePair::write(void* buf, size_t size, ssize_t* nwrite)
{
    return wpipe_->write(buf, size, nwrite);
}

SrsThreadPipeChannel::SrsThreadPipeChannel()
{
    initiator_ = new SrsThreadPipePair();
    responder_ = new SrsThreadPipePair();

    trd_ = new SrsFastCoroutine("chan", this);
    handler_ = NULL;
}

SrsThreadPipeChannel::~SrsThreadPipeChannel()
{
    srs_freep(trd_);
    srs_freep(initiator_);
    srs_freep(responder_);
}

SrsThreadPipePair* SrsThreadPipeChannel::initiator()
{
    return initiator_;
}

SrsThreadPipePair* SrsThreadPipeChannel::responder()
{
    return responder_;
}

srs_error_t SrsThreadPipeChannel::start(ISrsThreadResponder* h)
{
    handler_ = h;
    return trd_->start();
}

srs_error_t SrsThreadPipeChannel::cycle()
{
    srs_error_t err = srs_success;

    while (true) {
        if ((err = trd_->pull()) != srs_success) {
            return srs_error_wrap(err, "pull");
        }

        // Here we're responder, read from initiator.
        SrsThreadMessage m;
        if ((err = initiator_->read(&m, sizeof(m), NULL)) != srs_success) {
            srs_warn("read err %s", srs_error_desc(err).c_str());
            srs_freep(err); // Ignore any error.
            continue;
        }

        // Consume the message, the responder can write response to responder.
        if (handler_ && (err = handler_->on_thread_message(&m, this)) != srs_success) {
            srs_warn("consume err %s", srs_error_desc(err).c_str());
            srs_freep(err); // Ignore any error.
            continue;
        }
    }

    return err;
}

SrsThreadPipeSlot::SrsThreadPipeSlot(int slots)
{
    nn_channels_ = slots;
    channels_ = new SrsThreadPipeChannel[slots];

    index_ = 0;
    lock_ = new SrsThreadMutex();
}

SrsThreadPipeSlot::~SrsThreadPipeSlot()
{
    srs_freepa(channels_);
    srs_freep(lock_);
}

srs_error_t SrsThreadPipeSlot::initialize()
{
    srs_error_t err = srs_success;

    for (int i = 0; i < nn_channels_; i++) {
        SrsThreadPipeChannel* channel = &channels_[i];

        // Here we're responder, but it's ok to initialize the initiator.
        if ((err = channel->initiator()->initialize()) != srs_success) {
            return srs_error_wrap(err, "init %d initiator", i);
        }
        if ((err = channel->responder()->initialize()) != srs_success) {
            return srs_error_wrap(err, "init %d responder", i);
        }
    }

    return err;
}

srs_error_t SrsThreadPipeSlot::open_responder(ISrsThreadResponder* h)
{
    srs_error_t err = srs_success;

    for (int i = 0; i < nn_channels_; i++) {
        SrsThreadPipeChannel* channel = &channels_[i];

        // We're responder, read from initiator, write to responder.
        if ((err = channel->initiator()->open_read()) != srs_success) {
            return srs_error_wrap(err, "open read");
        }
        if ((err = channel->responder()->open_write()) != srs_success) {
            return srs_error_wrap(err, "open write");
        }

        // OK, we start the cycle coroutine for responder.
        if ((err = channel->start(h)) != srs_success) {
            return srs_error_wrap(err, "start %d consume coroutine", i);
        }
    }

    return err;
}

SrsThreadPipeChannel* SrsThreadPipeSlot::allocate()
{
    SrsThreadLocker(lock_);
    return index_ < nn_channels_? &channels_[index_++] : NULL;
}

ISrsThreadResponder::ISrsThreadResponder()
{
}

ISrsThreadResponder::~ISrsThreadResponder()
{
}

SrsThreadMutex::SrsThreadMutex()
{
    // https://man7.org/linux/man-pages/man3/pthread_mutexattr_init.3.html
    int r0 = pthread_mutexattr_init(&attr_);
    srs_assert(!r0);

    // https://man7.org/linux/man-pages/man3/pthread_mutexattr_gettype.3p.html
    r0 = pthread_mutexattr_settype(&attr_, PTHREAD_MUTEX_ERRORCHECK);
    srs_assert(!r0);

    // https://michaelkerrisk.com/linux/man-pages/man3/pthread_mutex_init.3p.html
    r0 = pthread_mutex_init(&lock_, &attr_);
    srs_assert(!r0);
}

SrsThreadMutex::~SrsThreadMutex()
{
    int r0 = pthread_mutex_destroy(&lock_);
    srs_assert(!r0);

    r0 = pthread_mutexattr_destroy(&attr_);
    srs_assert(!r0);
}

void SrsThreadMutex::lock()
{
    // https://man7.org/linux/man-pages/man3/pthread_mutex_lock.3p.html
    //        EDEADLK
    //                 The mutex type is PTHREAD_MUTEX_ERRORCHECK and the current
    //                 thread already owns the mutex.
    int r0 = pthread_mutex_lock(&lock_);
    srs_assert(!r0);
}

void SrsThreadMutex::unlock()
{
    int r0 = pthread_mutex_unlock(&lock_);
    srs_assert(!r0);
}

SrsThreadEntry::SrsThreadEntry()
{
    pool = NULL;
    start = NULL;
    arg = NULL;
    num = 0;
    tid = 0;

    err = srs_success;

    // Set affinity mask to include CPUs 0 to 7
    CPU_ZERO(&cpuset);
    CPU_ZERO(&cpuset2);
    cpuset_ok = false;

    stat = new SrsProcSelfStat();
    slot_ = NULL;

    received_packets_ = new SrsThreadQueue<SrsUdpMuxSocket>();
    cooked_packets_ = new SrsThreadQueue<SrsAsyncSRTPPacket>();
}

SrsThreadEntry::~SrsThreadEntry()
{
    srs_freep(stat);
    srs_freep(err);

    // TODO: FIXME: Before free slot, we MUST close pipes in threads that open them.
    srs_freep(slot_);

    srs_freep(received_packets_);
    srs_freep(cooked_packets_);

    // TODO: FIXME: Should dispose trd.
}

SrsThreadPool::SrsThreadPool()
{
    entry_ = NULL;
    lock_ = new SrsThreadMutex();
    hybrid_ = NULL;
    hybrid_high_water_level_ = 0;
    hybrid_critical_water_level_ = 0;
    hybrid_dying_water_level_ = 0;

    high_threshold_ = 0;
    high_pulse_ = 0;
    critical_threshold_ = 0;
    critical_pulse_ = 0;
    dying_threshold_ = 0;
    dying_pulse_ = 0;

    // Add primordial thread, current thread itself.
    SrsThreadEntry* entry = new SrsThreadEntry();
    threads_.push_back(entry);
    entry_ = entry;

    entry->pool = this;
    entry->label = "primordial";
    entry->start = NULL;
    entry->arg = NULL;
    entry->num = 1;
    entry->trd = pthread_self();
    entry->tid = gettid();

    char buf[256];
    snprintf(buf, sizeof(buf), "srs-master-%d", entry->num);
    entry->name = buf;

    pid_fd = -1;
}

// TODO: FIMXE: If free the pool, we should stop all threads.
SrsThreadPool::~SrsThreadPool()
{
    srs_freep(lock_);

    if (pid_fd > 0) {
        ::close(pid_fd);
        pid_fd = -1;
    }
}

bool SrsThreadPool::hybrid_high_water_level()
{
    return hybrid_critical_water_level() || hybrid_high_water_level_;
}

bool SrsThreadPool::hybrid_critical_water_level()
{
    return hybrid_dying_water_level() || hybrid_critical_water_level_;
}

bool SrsThreadPool::hybrid_dying_water_level()
{
    return dying_pulse_ && hybrid_dying_water_level_ >= dying_pulse_;
}

// Thread local objects.
extern const int LOG_MAX_SIZE;
extern __thread char* _srs_log_data;
extern __thread SrsStageManager* _srs_stages;

// Setup the thread-local variables, MUST call when each thread starting.
srs_error_t SrsThreadPool::setup()
{
    srs_error_t err = srs_success;

    // Initialize the log shared buffer for threads.
    srs_assert(!_srs_log_data);
    _srs_log_data = new char[LOG_MAX_SIZE];

    // MUST init ST for each thread, because ST is thread-local now.
    if ((err = srs_st_init()) != srs_success) {
        return srs_error_wrap(err, "init st");
    }

    // Create the hybrid RTMP/HTTP/RTC server.
    _srs_hybrid = new SrsHybridServer();

    // Create the source manager for server.
    _srs_sources = new SrsSourceManager();

    // The blackhole for RTC server.
    _srs_blackhole = new SrsRtcBlackhole();

    // The resource manager for RTC server.
    _srs_rtc_manager = new SrsResourceManager("RTC", true);

    // The source manager for RTC streams.
    _srs_rtc_sources = new SrsRtcStreamManager();

    // The object cache for RTC server.
    _srs_rtp_cache = new SrsRtpObjectCacheManager<SrsRtpPacket2>(sizeof(SrsRtpPacket2));
    _srs_rtp_raw_cache = new SrsRtpObjectCacheManager<SrsRtpRawPayload>(sizeof(SrsRtpRawPayload));
    _srs_rtp_fua_cache = new SrsRtpObjectCacheManager<SrsRtpFUAPayload2>(sizeof(SrsRtpFUAPayload2));
    _srs_rtp_msg_cache_buffers = new SrsRtpObjectCacheManager<SrsSharedPtrMessage>(sizeof(SrsSharedPtrMessage) + kRtpPacketSize);
    _srs_rtp_msg_cache_objs = new SrsRtpObjectCacheManager<SrsSharedPtrMessage>(sizeof(SrsSharedPtrMessage));

    // The pithy print for each thread.
    _srs_stages = new SrsStageManager();

    return err;
}

srs_error_t SrsThreadPool::initialize()
{
    srs_error_t err = srs_success;

    // Initialize global shared thread-safe objects once.
    if ((err = _srs_rtc_dtls_certificate->initialize()) != srs_success) {
        return srs_error_wrap(err, "rtc dtls certificate initialize");
    }

    if ((err = acquire_pid_file()) != srs_success) {
        return srs_error_wrap(err, "acquire pid file");
    }

    // Initialize the master primordial thread.
    SrsThreadEntry* entry = (SrsThreadEntry*)entry_;
#ifndef SRS_OSX
    // Load CPU affinity from config.
    int cpu_start = 0, cpu_end = 0;
    entry->cpuset_ok = _srs_config->get_threads_cpu_affinity("master", &cpu_start, &cpu_end);
    for (int i = cpu_start; entry->cpuset_ok && i <= cpu_end; i++) {
        CPU_SET(i, &entry->cpuset);
    }
#endif

    int r0 = 0, r1 = 0;
#ifndef SRS_OSX
    if (entry->cpuset_ok) {
        r0 = pthread_setaffinity_np(pthread_self(), sizeof(entry->cpuset), &entry->cpuset);
    }
    r1 = pthread_getaffinity_np(pthread_self(), sizeof(entry->cpuset2), &entry->cpuset2);
#endif

    interval_ = _srs_config->get_threads_interval();
    high_threshold_ = _srs_config->get_high_threshold();
    high_pulse_ = _srs_config->get_high_pulse();
    critical_threshold_ = _srs_config->get_critical_threshold();
    critical_pulse_ = _srs_config->get_critical_pulse();
    dying_threshold_ = _srs_config->get_dying_threshold();
    dying_pulse_ = _srs_config->get_dying_pulse();
    int async_srtp = _srs_config->get_threads_async_srtp();

    int recv_queue = _srs_config->get_threads_max_recv_queue();
    _srs_async_recv->set_max_recv_queue(recv_queue);

    int async_send = _srs_config->get_threads_async_send();
    _srs_async_send->set_enabled(async_send > 0);

    bool async_tunnel = _srs_config->get_threads_async_tunnel();
    _srs_async_recv->set_tunnel_enabled(async_tunnel);
    _srs_async_srtp->set_tunnel_enabled(async_tunnel);

    srs_trace("Thread #%d(%s): init name=%s, interval=%dms, async_srtp=%d, cpuset=%d/%d-0x%" PRIx64 "/%d-0x%" PRIx64 ", water_level=%dx%d,%dx%d,%dx%d recvQ=%d, aSend=%d, tunnel=%d",
        entry->num, entry->label.c_str(), entry->name.c_str(), srsu2msi(interval_), async_srtp,
        entry->cpuset_ok, r0, srs_covert_cpuset(entry->cpuset), r1, srs_covert_cpuset(entry->cpuset2),
        high_pulse_, high_threshold_, critical_pulse_, critical_threshold_, dying_pulse_, dying_threshold_,
        recv_queue, async_send, async_tunnel);

    return err;
}

srs_error_t SrsThreadPool::acquire_pid_file()
{
    std::string pid_file = _srs_config->get_pid_file();

    // -rw-r--r--
    // 644
    int mode = S_IRUSR | S_IWUSR |  S_IRGRP | S_IROTH;

    int fd;
    // open pid file
    if ((fd = ::open(pid_file.c_str(), O_WRONLY | O_CREAT, mode)) == -1) {
        return srs_error_new(ERROR_SYSTEM_PID_ACQUIRE, "open pid file=%s", pid_file.c_str());
    }

    // require write lock
    struct flock lock;

    lock.l_type = F_WRLCK; // F_RDLCK, F_WRLCK, F_UNLCK
    lock.l_start = 0; // type offset, relative to l_whence
    lock.l_whence = SEEK_SET;  // SEEK_SET, SEEK_CUR, SEEK_END
    lock.l_len = 0;

    if (fcntl(fd, F_SETLK, &lock) == -1) {
        if(errno == EACCES || errno == EAGAIN) {
            ::close(fd);
            srs_error("srs is already running!");
            return srs_error_new(ERROR_SYSTEM_PID_ALREADY_RUNNING, "srs is already running");
        }
        return srs_error_new(ERROR_SYSTEM_PID_LOCK, "access to pid=%s", pid_file.c_str());
    }

    // truncate file
    if (ftruncate(fd, 0) != 0) {
        return srs_error_new(ERROR_SYSTEM_PID_TRUNCATE_FILE, "truncate pid file=%s", pid_file.c_str());
    }

    // write the pid
    string pid = srs_int2str(getpid());
    if (write(fd, pid.c_str(), pid.length()) != (int)pid.length()) {
        return srs_error_new(ERROR_SYSTEM_PID_WRITE_FILE, "write pid=%s to file=%s", pid.c_str(), pid_file.c_str());
    }

    // auto close when fork child process.
    int val;
    if ((val = fcntl(fd, F_GETFD, 0)) < 0) {
        return srs_error_new(ERROR_SYSTEM_PID_GET_FILE_INFO, "fcntl fd=%d", fd);
    }
    val |= FD_CLOEXEC;
    if (fcntl(fd, F_SETFD, val) < 0) {
        return srs_error_new(ERROR_SYSTEM_PID_SET_FILE_INFO, "lock file=%s fd=%d", pid_file.c_str(), fd);
    }

    srs_trace("write pid=%s to %s success!", pid.c_str(), pid_file.c_str());
    pid_fd = fd;

    return srs_success;
}

srs_error_t SrsThreadPool::execute(string label, srs_error_t (*start)(void* arg), void* arg)
{
    srs_error_t err = srs_success;

    SrsThreadEntry* entry = new SrsThreadEntry();

    // Update the hybrid thread entry for circuit breaker.
    if (label == "hybrid") {
        hybrid_ = entry;
        hybrids_.push_back(entry);
    }

    // To protect the threads_ for executing thread-safe.
    if (true) {
        SrsThreadLocker(lock_);
        threads_.push_back(entry);
    }

    entry->pool = this;
    entry->label = label;
    entry->start = start;
    entry->arg = arg;

    // The id of thread, should equal to the debugger thread id.
    // For gdb, it's: info threads
    // For lldb, it's: thread list
    static int num = entry_->num + 1;
    entry->num = num++;

    char buf[256];
    snprintf(buf, sizeof(buf), "srs-%s-%d", entry->label.c_str(), entry->num);
    entry->name = buf;

#ifndef SRS_OSX
    // Load CPU affinity from config.
    int cpu_start = 0, cpu_end = 0;
    entry->cpuset_ok = _srs_config->get_threads_cpu_affinity(label, &cpu_start, &cpu_end);
    for (int i = cpu_start; entry->cpuset_ok && i <= cpu_end; i++) {
        CPU_SET(i, &entry->cpuset);
    }
#endif

    // https://man7.org/linux/man-pages/man3/pthread_create.3.html
    pthread_t trd;
    int r0 = pthread_create(&trd, NULL, SrsThreadPool::start, entry);
    if (r0 != 0) {
        entry->err = srs_error_new(ERROR_THREAD_CREATE, "create thread %s, r0=%d", label.c_str(), r0);
        return srs_error_copy(entry->err);
    }

    entry->trd = trd;

    return err;
}

srs_error_t SrsThreadPool::run()
{
    srs_error_t err = srs_success;

    while (true) {
        vector<SrsThreadEntry*> threads;
        if (true) {
            SrsThreadLocker(lock_);
            threads = threads_;
        }

        // Check the threads status fastly.
        int loops = (int)(interval_ / SRS_UTIME_SECONDS);
        for (int i = 0; i < loops; i++) {
            for (int i = 0; i < (int)threads.size(); i++) {
                SrsThreadEntry* entry = threads.at(i);
                if (entry->err != srs_success) {
                    err = srs_error_copy(entry->err);
                    err = srs_error_wrap(err, "thread #%d(%s)", entry->num, entry->label.c_str());
                    return err;
                }
            }

            // For Circuit-Breaker to update the SNMP, ASAP.
            srs_update_udp_snmp_statistic();

            // Update thread CPUs per 1s.
            for (int i = 0; i < (int)threads.size(); i++) {
                SrsThreadEntry* entry = threads.at(i);
                if (!entry->tid) {
                    continue;
                }

                srs_update_thread_proc_stat(entry->stat, entry->tid);
            }

            // Update the Circuit-Breaker by water-level.
            // TODO: FIXME: Should stat all hybrid servers.
            if (hybrid_ && hybrid_->stat) {
                // Reset the high water-level when CPU is low for N times.
                if (hybrid_->stat->percent * 100 > high_threshold_) {
                    hybrid_high_water_level_ = high_pulse_;
                } else if (hybrid_high_water_level_ > 0) {
                    hybrid_high_water_level_--;
                }

                // Reset the critical water-level when CPU is low for N times.
                if (hybrid_->stat->percent * 100 > critical_threshold_) {
                    hybrid_critical_water_level_ = critical_pulse_;
                } else if (hybrid_critical_water_level_ > 0) {
                    hybrid_critical_water_level_--;
                }

                // Reset the dying water-level when CPU is low for N times.
                if (hybrid_->stat->percent * 100 > dying_threshold_) {
                    hybrid_dying_water_level_ = srs_min(dying_pulse_ + 1, hybrid_dying_water_level_ + 1);
                } else if (hybrid_dying_water_level_ > 0) {
                    hybrid_dying_water_level_ = 0;
                }
            }

            sleep(1);
        }

        // In normal state, gather status and log it.
        static char buf[128];
        string async_logs = _srs_async_log->description();

        string queue_desc;
        if (true) {
            // TODO: FIXME: Collect the size of RECV and SRTP queue.
            snprintf(buf, sizeof(buf), ", queue=%d,%d", _srs_async_srtp->size(), _srs_async_send->size());
            queue_desc = buf;
        }

        string sync_desc;
        _srs_thread_sync_10us->update(); _srs_thread_sync_100us->update();
        _srs_thread_sync_1000us->update(); _srs_thread_sync_plus->update();
        if (_srs_thread_sync_10us->r10s() || _srs_thread_sync_100us->r10s() || _srs_thread_sync_1000us->r10s() || _srs_thread_sync_plus->r10s()) {
            snprintf(buf, sizeof(buf), ", sync=%d,%d,%d,%d", _srs_thread_sync_10us->r10s(), _srs_thread_sync_100us->r10s(), _srs_thread_sync_1000us->r10s(), _srs_thread_sync_plus->r10s());
            sync_desc = buf;
        }

        string tunnel_desc;
        _srs_tunnel_recv_raw->update(); _srs_tunnel_recv_hit->update();
        _srs_tunnel_send_raw->update(); _srs_tunnel_send_hit->update();
        if (_srs_tunnel_recv_raw->r10s() || _srs_tunnel_recv_hit->r10s() || _srs_tunnel_send_raw->r10s() || _srs_tunnel_send_hit->r10s()) {
            snprintf(buf, sizeof(buf), ", tunnel=(r:%d/%d,s:%d/%d)", _srs_tunnel_recv_raw->r10s(), _srs_tunnel_recv_hit->r10s(), _srs_tunnel_send_raw->r10s(), _srs_tunnel_send_hit->r10s());
            tunnel_desc = buf;
        }

        // Show statistics for RTC server.
        SrsProcSelfStat* u = srs_get_self_proc_stat();
        // Resident Set Size: number of pages the process has in real memory.
        int memory = (int)(u->rss * 4 / 1024);

        // The hybrid thread cpu and memory.
        float thread_percent = 0.0f, top_percent = 0.0f;
        if (hybrid_ && hybrid_->stat) {
            thread_percent = hybrid_->stat->percent * 100;
        }
        for (int i = 0; i < (int)threads.size(); i++) {
            SrsThreadEntry* entry = threads.at(i);
            if (!entry->stat || entry->stat->percent <= 0) {
                continue;
            }
            top_percent = srs_max(top_percent, entry->stat->percent * 100);
        }

        string circuit_breaker;
        if (hybrid_high_water_level() || hybrid_critical_water_level() || _srs_pps_aloss->r1s() || _srs_pps_rloss->r1s() || _srs_pps_snack2->r10s()) {
            snprintf(buf, sizeof(buf), ", break=%d,%d,%d, cond=%d,%d,%.2f%%, snk=%d,%d,%d",
                hybrid_high_water_level(), hybrid_critical_water_level(), hybrid_dying_water_level(), // Whether Circuit-Break is enable.
                _srs_pps_rloss->r1s(), _srs_pps_aloss->r1s(), thread_percent, // The conditions to enable Circuit-Breaker.
                _srs_pps_snack2->r10s(), _srs_pps_snack3->r10s(), // NACK packet,seqs sent.
                _srs_pps_snack4->r10s() // NACK drop by Circuit-Break.
            );
            circuit_breaker = buf;
        }

        srs_trace("Process: cpu=%.2f%%,%dMB, threads=%d,%.2f%%,%.2f%%%s%s%s%s%s",
            u->percent * 100, memory, (int)threads_.size(), top_percent, thread_percent,
            async_logs.c_str(), sync_desc.c_str(), queue_desc.c_str(), circuit_breaker.c_str(),
            tunnel_desc.c_str());
    }

    return err;
}

void SrsThreadPool::stop()
{
    // TODO: FIXME: Should notify other threads to do cleanup and quit.
}

SrsThreadEntry* SrsThreadPool::self()
{
    std::vector<SrsThreadEntry*> threads;

    if (true) {
        SrsThreadLocker(lock_);
        threads = threads_;
    }

    for (int i = 0; i < (int)threads.size(); i++) {
        SrsThreadEntry* entry = threads.at(i);
        if (entry->trd == pthread_self()) {
            return entry;
        }
    }

    return NULL;
}

SrsThreadEntry* SrsThreadPool::hybrid()
{
    return hybrid_;
}

vector<SrsThreadEntry*> SrsThreadPool::hybrids()
{
    return hybrids_;
}

void* SrsThreadPool::start(void* arg)
{
    srs_error_t err = srs_success;

    SrsThreadEntry* entry = (SrsThreadEntry*)arg;

    // Initialize thread-local variables.
    if ((err = SrsThreadPool::setup()) != srs_success) {
        entry->err = err;
        return NULL;
    }

    // Set the thread local fields.
    entry->tid = gettid();

    int r0 = 0, r1 = 0;
#ifndef SRS_OSX
    // https://man7.org/linux/man-pages/man3/pthread_setname_np.3.html
    pthread_setname_np(pthread_self(), entry->name.c_str());
    if (entry->cpuset_ok) {
        r0 = pthread_setaffinity_np(pthread_self(), sizeof(entry->cpuset), &entry->cpuset);
    }
    r1 = pthread_getaffinity_np(pthread_self(), sizeof(entry->cpuset2), &entry->cpuset2);
#else
    pthread_setname_np(entry->name.c_str());
#endif

    srs_trace("Thread #%d: run with tid=%d, entry=%p, label=%s, name=%s, cpuset=%d/%d-0x%" PRIx64 "/%d-0x%" PRIx64,
        entry->num, (int)entry->tid, entry, entry->label.c_str(), entry->name.c_str(), entry->cpuset_ok,
        r0, srs_covert_cpuset(entry->cpuset), r1, srs_covert_cpuset(entry->cpuset2));

    if ((err = entry->start(entry->arg)) != srs_success) {
        entry->err = err;
    }

    // We do not use the return value, the err has been set to entry->err.
    return NULL;
}

// It MUST be thread-safe, global and shared object.
SrsThreadPool* _srs_thread_pool = new SrsThreadPool();

SrsAsyncFileWriter::SrsAsyncFileWriter(std::string p)
{
    filename_ = p;
    writer_ = new SrsFileWriter();
    chunks_ = new SrsThreadQueue<SrsSharedPtrMessage>();
}

// TODO: FIXME: Before free the writer, we must remove it from the manager.
SrsAsyncFileWriter::~SrsAsyncFileWriter()
{
    // TODO: FIXME: Should we flush dirty logs?
    srs_freep(writer_);
    srs_freep(chunks_);
}

srs_error_t SrsAsyncFileWriter::open()
{
    return writer_->open(filename_);
}

srs_error_t SrsAsyncFileWriter::open_append()
{
    return writer_->open_append(filename_);
}

void SrsAsyncFileWriter::close()
{
    writer_->close();
}

srs_error_t SrsAsyncFileWriter::write(void* buf, size_t count, ssize_t* pnwrite)
{
    srs_error_t err = srs_success;

    if (count <= 0) {
        return err;
    }

    char* cp = new char[count];
    memcpy(cp, buf, count);

    SrsSharedPtrMessage* msg = new SrsSharedPtrMessage();
    msg->wrap(cp, count);

    chunks_->push_back(msg);

    if (pnwrite) {
        *pnwrite = count;
    }

    return err;
}

srs_error_t SrsAsyncFileWriter::writev(const iovec* iov, int iovcnt, ssize_t* pnwrite)
{
    srs_error_t err = srs_success;

    for (int i = 0; i < iovcnt; i++) {
        const iovec* p = iov + i;

        ssize_t nn = 0;
        if ((err = write(p->iov_base, p->iov_len, &nn)) != srs_success) {
            return srs_error_wrap(err, "write %d iov %d bytes", i, p->iov_len);
        }

        if (pnwrite) {
            *pnwrite += nn;
        }
    }

    return err;
}

srs_error_t SrsAsyncFileWriter::flush()
{
    srs_error_t err = srs_success;

    // The time to wait here, is the time to wait there, because they wait for the same lock
    // at queue to push_back or swap all messages.
    srs_utime_t now = srs_update_system_time();

    vector<SrsSharedPtrMessage*> flying_chunks;
    if (true) {
        chunks_->swap(flying_chunks);
    }

    // Stat the sync wait of locks.
    srs_utime_t elapsed = srs_update_system_time() - now;
    if (elapsed <= 10) {
        ++_srs_thread_sync_10us->sugar;
    } else if (elapsed <= 100) {
        ++_srs_thread_sync_100us->sugar;
    } else if (elapsed <= 1000) {
        ++_srs_thread_sync_1000us->sugar;
    } else {
        ++_srs_thread_sync_plus->sugar;
    }

    // Flush the chunks to disk.
    for (int i = 0; i < (int)flying_chunks.size(); i++) {
        SrsSharedPtrMessage* msg = flying_chunks.at(i);

        srs_error_t r0 = writer_->write(msg->payload, msg->size, NULL);

        // Choose a random error to return.
        if (err == srs_success) {
            err = r0;
        } else {
            srs_freep(r0);
        }

        srs_freep(msg);
    }

    return err;
}

SrsAsyncLogManager::SrsAsyncLogManager()
{
    interval_ = 0;

    reopen_ = false;
    lock_ = new SrsThreadMutex();
}

// TODO: FIXME: We should stop the thread first, then free the manager.
SrsAsyncLogManager::~SrsAsyncLogManager()
{
    srs_freep(lock_);

    for (int i = 0; i < (int)writers_.size(); i++) {
        SrsAsyncFileWriter* writer = writers_.at(i);
        srs_freep(writer);
    }
}

// @remark Note that we should never write logs, because log is not ready not.
srs_error_t SrsAsyncLogManager::initialize()
{
    srs_error_t err =  srs_success;

    interval_ = _srs_config->srs_log_flush_interval();
    if (interval_ <= 0) {
        return srs_error_new(ERROR_SYSTEM_LOGFILE, "invalid interval=%dms", srsu2msi(interval_));
    }

    return err;
}

// @remark Now, log is ready, and we can print logs.
srs_error_t SrsAsyncLogManager::start(void* arg)
{
    SrsAsyncLogManager* log = (SrsAsyncLogManager*)arg;
    return log->do_start();
}

srs_error_t SrsAsyncLogManager::create_writer(std::string filename, SrsAsyncFileWriter** ppwriter)
{
    srs_error_t err = srs_success;

    SrsAsyncFileWriter* writer = new SrsAsyncFileWriter(filename);

    if (true) {
        SrsThreadLocker(lock_);
        writers_.push_back(writer);
    }

    if ((err = writer->open()) != srs_success) {
        return srs_error_wrap(err, "open file %s fail", filename.c_str());
    }

    *ppwriter = writer;
    return err;
}

void SrsAsyncLogManager::reopen()
{
    SrsThreadLocker(lock_);
    reopen_ = true;
}

std::string SrsAsyncLogManager::description()
{
    SrsThreadLocker(lock_);

    int nn_logs = 0;
    int max_logs = 0;
    for (int i = 0; i < (int)writers_.size(); i++) {
        SrsAsyncFileWriter* writer = writers_.at(i);

        int nn = (int)writer->chunks_->size();
        nn_logs += nn;
        max_logs = srs_max(max_logs, nn);
    }

    static char buf[128];
    snprintf(buf, sizeof(buf), ", logs=%d/%d/%d", (int)writers_.size(), nn_logs, max_logs);

    return buf;
}

srs_error_t SrsAsyncLogManager::do_start()
{
    srs_error_t err = srs_success;

    // Never quit for this thread.
    while (true) {
        // Reopen all log files.
        if (reopen_) {
            SrsThreadLocker(lock_);
            reopen_ = false;

            for (int i = 0; i < (int)writers_.size(); i++) {
                SrsAsyncFileWriter* writer = writers_.at(i);

                writer->close();
                if ((err = writer->open()) != srs_success) {
                    srs_error_reset(err); // Ignore any error for reopen logs.
                }
            }
        }

        // Flush all logs from cache to disk.
        if (true) {
            SrsThreadLocker(lock_);

            for (int i = 0; i < (int)writers_.size(); i++) {
                SrsAsyncFileWriter* writer = writers_.at(i);

                if ((err = writer->flush()) != srs_success) {
                    srs_error_reset(err); // Ignore any error for flushing logs.
                }
            }
        }

        // We use the system primordial sleep, not the ST sleep, because
        // this is a system thread, not a coroutine.
        timespec tv = {0};
        tv.tv_sec = interval_ / SRS_UTIME_SECONDS;
        tv.tv_nsec = (interval_ % SRS_UTIME_SECONDS) * 1000;
        nanosleep(&tv, NULL);
    }

    return err;
}

// It MUST be thread-safe, global shared object.
SrsAsyncLogManager* _srs_async_log = new SrsAsyncLogManager();

SrsAsyncSRTP::SrsAsyncSRTP(SrsSecurityTransport* transport)
{
    task_ = NULL;
    transport_ = transport;
}

SrsAsyncSRTP::~SrsAsyncSRTP()
{
    _srs_async_srtp->on_srtp_codec_destroy(task_);
}

srs_error_t SrsAsyncSRTP::initialize(std::string recv_key, std::string send_key)
{
    srs_error_t err = srs_success;

    srs_assert(!task_);
    task_ = new SrsAsyncSRTPTask(this);
    _srs_async_srtp->register_task(task_);

    if ((err = task_->initialize(recv_key, send_key)) != srs_success) {
        return srs_error_wrap(err, "init async srtp");
    }

    return err;
}

srs_error_t SrsAsyncSRTP::protect_rtp(void* packet, int* nb_cipher)
{
    if (!task_) {
        return srs_error_new(ERROR_RTC_SRTP_UNPROTECT, "not ready");
    }

    int nb_plaintext = *nb_cipher;

    // Note that we must allocate more bytes than the size of packet, because SRTP
    // will write checksum at the end of buffer.
    char* buf = new char[kRtcpPacketSize];
    memcpy(buf, packet, nb_plaintext);

    SrsAsyncSRTPPacket* pkt = new SrsAsyncSRTPPacket(task_);
    pkt->msg_->wrap(buf, nb_plaintext);
    pkt->is_rtp_ = true;
    pkt->do_decrypt_ = false;
    _srs_async_srtp->add_packet(pkt);

    // Do the job asynchronously.
    *nb_cipher = 0;

    return srs_success;
}

srs_error_t SrsAsyncSRTP::protect_rtcp(void* packet, int* nb_cipher)
{
    if (!task_) {
        return srs_error_new(ERROR_RTC_SRTP_UNPROTECT, "not ready");
    }

    int nb_plaintext = *nb_cipher;

    // Note that we must allocate more bytes than the size of packet, because SRTP
    // will write checksum at the end of buffer.
    char* buf = new char[kRtcpPacketSize];
    memcpy(buf, packet, nb_plaintext);

    SrsAsyncSRTPPacket* pkt = new SrsAsyncSRTPPacket(task_);
    pkt->msg_->wrap(buf, nb_plaintext);
    pkt->is_rtp_ = false;
    pkt->do_decrypt_ = false;
    _srs_async_srtp->add_packet(pkt);

    // Do the job asynchronously.
    *nb_cipher = 0;

    return srs_success;
}

srs_error_t SrsAsyncSRTP::unprotect_rtp(void* packet, int* nb_plaintext)
{
    if (!task_) {
        return srs_error_new(ERROR_RTC_SRTP_UNPROTECT, "not ready");
    }

    int nb_cipher = *nb_plaintext;
    char* buf = new char[nb_cipher];
    memcpy(buf, packet, nb_cipher);

    SrsAsyncSRTPPacket* pkt = new SrsAsyncSRTPPacket(task_);
    pkt->msg_->wrap(buf, nb_cipher);
    pkt->is_rtp_ = true;
    pkt->do_decrypt_ = true;
    _srs_async_srtp->add_packet(pkt);

    // Do the job asynchronously.
    *nb_plaintext = 0;

    ++_srs_tunnel_recv_raw->sugar;

    return srs_success;
}

srs_error_t SrsAsyncSRTP::unprotect_rtcp(void* packet, int* nb_plaintext)
{
    if (!task_) {
        return srs_error_new(ERROR_RTC_SRTP_UNPROTECT, "not ready");
    }

    int nb_cipher = *nb_plaintext;
    char* buf = new char[nb_cipher];
    memcpy(buf, packet, nb_cipher);

    SrsAsyncSRTPPacket* pkt = new SrsAsyncSRTPPacket(task_);
    pkt->msg_->wrap(buf, nb_cipher);
    pkt->is_rtp_ = false;
    pkt->do_decrypt_ = true;
    _srs_async_srtp->add_packet(pkt);

    // Do the job asynchronously.
    *nb_plaintext = 0;

    ++_srs_tunnel_recv_raw->sugar;

    return srs_success;
}

void SrsAsyncSRTP::dig_tunnel(SrsUdpMuxSocket* skt)
{
    if (!task_) {
        return;
    }

    uint64_t fast_id = skt->fast_id();
    if (!fast_id) {
        return;
    }

    _srs_async_recv->tunnels()->dig_tunnel(fast_id, task_);
    task_->dig_tunnel(skt);
}

SrsAsyncSRTPTask::SrsAsyncSRTPTask(SrsAsyncSRTP* codec)
{
    codec_ = codec;
    impl_ = new SrsSRTP();
    disposing_ = false;
    sendonly_skt_ = NULL;
    lock_ = new SrsThreadMutex();
    srtp_lock_ = new SrsThreadMutex();

    entry_ = _srs_thread_pool->self();
}

SrsAsyncSRTPTask::~SrsAsyncSRTPTask()
{
    srs_freep(impl_);
    srs_freep(sendonly_skt_);
    srs_freep(lock_);
    srs_freep(srtp_lock_);
}

srs_error_t SrsAsyncSRTPTask::initialize(std::string recv_key, std::string send_key)
{
    srs_error_t err = srs_success;

    if ((err = impl_->initialize(recv_key, send_key)) != srs_success) {
        return srs_error_wrap(err, "init srtp impl");
    }

    return err;
}

void SrsAsyncSRTPTask::dispose()
{
    // TODO: FIXME: Do cleanup in future.
    // TODO: FIXME: Memory leak here, use lazy free to avoid lock for each packet.
    disposing_ = true;

    // It's safe to set the codec to NULl, because it has been freed.
    codec_ = NULL;
}

srs_error_t SrsAsyncSRTPTask::cook(SrsAsyncSRTPPacket* pkt)
{
    srs_error_t err = srs_success;

    // It's safe, because here we do not use the codec.
    if (disposing_) {
        return err;
    }

    // To sync more than one SRTP threads.
    SrsThreadLocker(srtp_lock_);

    pkt->nb_consumed_ = pkt->msg_->size;
    if (pkt->do_decrypt_) {
        if (pkt->is_rtp_) {
            err = impl_->unprotect_rtp(pkt->msg_->payload, &pkt->nb_consumed_);
        } else {
            err = impl_->unprotect_rtcp(pkt->msg_->payload, &pkt->nb_consumed_);
        }
    } else {
        if (pkt->is_rtp_) {
            err = impl_->protect_rtp(pkt->msg_->payload, &pkt->nb_consumed_);
        } else {
            err = impl_->protect_rtcp(pkt->msg_->payload, &pkt->nb_consumed_);
        }
    }
    if (err != srs_success) {
        return err;
    }

    return err;
}

srs_error_t SrsAsyncSRTPTask::consume(SrsAsyncSRTPPacket* pkt)
{
    srs_error_t err = srs_success;

    // It's safe, because the dispose and consume are in the same thread hybrid.
    if (disposing_) {
        return err;
    }

    char* payload = pkt->msg_->payload;

    if (pkt->do_decrypt_) {
        if (pkt->is_rtp_) {
            err = codec_->transport_->on_rtp_plaintext(payload, pkt->nb_consumed_);
        } else {
            err = codec_->transport_->on_rtcp_plaintext(payload, pkt->nb_consumed_);
        }
    } else {
        if (pkt->is_rtp_) {
            err = codec_->transport_->on_rtp_cipher(payload, pkt->nb_consumed_);
        } else {
            err = codec_->transport_->on_rtcp_cipher(payload, pkt->nb_consumed_);
        }
    }

    return err;
}

void SrsAsyncSRTPTask::dig_tunnel(SrsUdpMuxSocket* skt)
{
    if (!skt) {
        return;
    }

    SrsThreadLocker(lock_);
    srs_freep(sendonly_skt_);
    sendonly_skt_ = skt->copy_sendonly();
}

bool SrsAsyncSRTPTask::consume_by_tunnel(SrsAsyncSRTPPacket* src)
{
    // If decrypt, we should consume by hybrid.
    if (src->do_decrypt_) {
        return false;
    }

    // No tunnel established, ignore.
    if (!sendonly_skt_) {
        return false;
    }

    SrsAsyncUdpPacket* pkt = new SrsAsyncUdpPacket();
    if (true) {
        SrsThreadLocker(lock_);
        pkt->from(sendonly_skt_, (char*)src->msg_->payload, src->nb_consumed_);
    }

    _srs_async_send->add_packet(pkt);
    srs_freep(src);

    ++_srs_tunnel_send_hit->sugar;
    return true;
}

SrsAsyncSRTPPacket::SrsAsyncSRTPPacket(SrsAsyncSRTPTask* task)
{
    srs_assert(task);

    task_ = task;
    msg_ = new SrsSharedPtrMessage();
    is_rtp_ = false;
    do_decrypt_ = false;
    nb_consumed_ = 0;
}

SrsAsyncSRTPPacket::~SrsAsyncSRTPPacket()
{
    srs_freep(msg_);
}

SrsAsyncSRTPManager::SrsAsyncSRTPManager()
{
    lock_ = new SrsThreadMutex();
    srtp_packets_ = new SrsThreadQueue<SrsAsyncSRTPPacket>();
    tunnel_enabled_ = false;
}

// TODO: FIXME: We should stop the thread first, then free the manager.
SrsAsyncSRTPManager::~SrsAsyncSRTPManager()
{
    srs_freep(lock_);
    srs_freep(srtp_packets_);

    vector<SrsAsyncSRTPTask*>::iterator it;
    for (it = tasks_.begin(); it != tasks_.end(); ++it) {
        SrsAsyncSRTPTask* task = *it;
        srs_freep(task);
    }
}

void SrsAsyncSRTPManager::register_task(SrsAsyncSRTPTask* task)
{
    if (!task) {
        return;
    }

    SrsThreadLocker(lock_);
    tasks_.push_back(task);
}

void SrsAsyncSRTPManager::on_srtp_codec_destroy(SrsAsyncSRTPTask* task)
{
    if (!task) {
        return;
    }

    SrsThreadLocker(lock_);
    vector<SrsAsyncSRTPTask*>::iterator it;
    if ((it = std::find(tasks_.begin(), tasks_.end(), task)) != tasks_.end()) {
        tasks_.erase(it);

        // TODO: FIXME: Do cleanup in future.
        task->dispose();
    }
}

// TODO: FIXME: We could use a coroutine queue, then cook all packet in RTC server timer.
void SrsAsyncSRTPManager::add_packet(SrsAsyncSRTPPacket* pkt)
{
    srtp_packets_->push_back(pkt);
}

int SrsAsyncSRTPManager::size()
{
    return srtp_packets_->size();
}

srs_error_t SrsAsyncSRTPManager::start(void* arg)
{
    SrsAsyncSRTPManager* srtp = (SrsAsyncSRTPManager*)arg;
    return srtp->do_start();
}

srs_error_t SrsAsyncSRTPManager::do_start()
{
    srs_error_t err = srs_success;

    // TODO: FIXME: Config it?
    srs_utime_t interval = 10 * SRS_UTIME_MILLISECONDS;

    while (true) {
        vector<SrsAsyncSRTPPacket*> flying_srtp_packets;
        srtp_packets_->swap(flying_srtp_packets);

        for (int i = 0; i < (int)flying_srtp_packets.size(); i++) {
            SrsAsyncSRTPPacket* pkt = flying_srtp_packets.at(i);

            if ((err = pkt->task_->cook(pkt)) != srs_success) {
                srs_error_reset(err); // Ignore any error.
            }

            // Try to consume the packet by tunnel.
            if (tunnel_enabled_ && pkt->task_->consume_by_tunnel(pkt)) {
                continue;
            }

            pkt->task_->entry_->cooked_packets_->push_back(pkt);

            ++_srs_tunnel_send_raw->sugar;
        }

        // If got packets, maybe more packets in queue.
        if (!flying_srtp_packets.empty()) {
            continue;
        }

        // TODO: FIXME: Maybe we should use cond wait?
        timespec tv = {0};
        tv.tv_sec = interval / SRS_UTIME_SECONDS;
        tv.tv_nsec = (interval % SRS_UTIME_SECONDS) * 1000;
        nanosleep(&tv, NULL);
    }

    return err;
}

srs_error_t SrsAsyncSRTPManager::consume(SrsThreadEntry* entry, int* nn_consumed)
{
    srs_error_t err = srs_success;

    // How many messages to run a yield.
    uint32_t nn_msgs_for_yield = 0;

    // TODO: FIXME: Sort the packets, to avoid NACK.
    vector<SrsAsyncSRTPPacket*> flying_cooked_packets;
    entry->cooked_packets_->swap(flying_cooked_packets);

    if (flying_cooked_packets.empty()) {
        return err;
    }

    *nn_consumed += (int)flying_cooked_packets.size();

    for (int i = 0; i < (int)flying_cooked_packets.size(); i++) {
        SrsAsyncSRTPPacket* pkt = flying_cooked_packets.at(i);

        if ((err = pkt->task_->consume(pkt)) != srs_success) {
            srs_error_reset(err);
        }

        srs_freep(pkt);

        // Yield to another coroutines.
        // @see https://github.com/ossrs/srs/issues/2194#issuecomment-777485531
        if (++nn_msgs_for_yield > 10) {
            nn_msgs_for_yield = 0;
            srs_thread_yield();
        }
    }

    return err;
}

SrsAsyncSRTPManager* _srs_async_srtp = new SrsAsyncSRTPManager();

SrsThreadUdpListener::SrsThreadUdpListener()
{
    skt_ = NULL;
    entry_ = NULL;
}

SrsThreadUdpListener::SrsThreadUdpListener(srs_netfd_t fd, ISrsUdpMuxHandler* handler)
{
    skt_ = new SrsUdpMuxSocket(fd);
    skt_->set_handler(handler);

    entry_ = _srs_thread_pool->self();
}

SrsThreadUdpListener::~SrsThreadUdpListener()
{
    srs_freep(skt_);
}

SrsThreadUdpListener* SrsThreadUdpListener::copy()
{
    SrsThreadUdpListener* cp = new SrsThreadUdpListener();
    cp->skt_ = skt_->copy();
    cp->entry_ = entry_;
    return cp;
}

SrsRecvTunnels::SrsRecvTunnels()
{
    lock_ = new SrsThreadMutex();
}

// TODO: FIXME: Cleanup SRTP tasks.
SrsRecvTunnels::~SrsRecvTunnels()
{
    srs_freep(lock_);
}

void SrsRecvTunnels::dig_tunnel(uint64_t fast_id, SrsAsyncSRTPTask* task)
{
    SrsThreadLocker(lock_);
    tunnels_[fast_id] = task;
}

SrsAsyncSRTPTask* SrsRecvTunnels::find(uint64_t fast_id)
{
    SrsThreadLocker(lock_);
    std::map<uint64_t, SrsAsyncSRTPTask*>::iterator it = tunnels_.find(fast_id);
    if (it != tunnels_.end()) {
        return it->second;
    }
    return NULL;
}

SrsAsyncRecvManager::SrsAsyncRecvManager()
{
    lock_ = new SrsThreadMutex();
    max_recv_queue_ = 0;
    tunnels_ = new SrsRecvTunnels();
    tunnel_enabled_ = false;
}

// TODO: FIXME: We should stop the thread first, then free the manager.
SrsAsyncRecvManager::~SrsAsyncRecvManager()
{
    srs_freep(lock_);
    srs_freep(tunnels_);

    vector<SrsThreadUdpListener*>::iterator it;
    for (it = listeners_.begin(); it != listeners_.end(); ++it) {
        SrsThreadUdpListener* listener = *it;
        srs_freep(listener);
    }
}

void SrsAsyncRecvManager::add_listener(SrsThreadUdpListener* listener)
{
    SrsThreadLocker(lock_);
    listeners_.push_back(listener);
}

srs_error_t SrsAsyncRecvManager::start(void* arg)
{
    SrsAsyncRecvManager* recv = (SrsAsyncRecvManager*)arg;
    return recv->do_start();
}

srs_error_t SrsAsyncRecvManager::do_start()
{
    srs_error_t err = srs_success;

    // TODO: FIXME: Config it?
    srs_utime_t interval = 10 * SRS_UTIME_MILLISECONDS;

    // We must deep copy listeners if changed.
    vector<SrsThreadUdpListener*> listeners;

    while (true) {
        if (true) {
            SrsThreadLocker(lock_);
            if (listeners.size() != listeners_.size()) {
                deep_copy_listeners(listeners);
            }
        }

        bool got_packets = false;
        for (int i = 0; i < (int)listeners.size(); i++) {
            SrsThreadUdpListener* listener = listeners.at(i);

            for (int j = 0; j < 128; j++) {
                // TODO: FIXME: Use st_recvfrom to recv if thread-safe ST is ok.
                int nread = listener->skt_->raw_recvfrom();
                if (nread <= 0) {
                    break;
                }

                // Get the queue of source thread.
                SrsThreadQueue<SrsUdpMuxSocket>* queue = listener->entry_->received_packets_;

                // Drop packet if queue is critical full.
                int nb_packets = (int)queue->size();
                if (nb_packets >= max_recv_queue_) {
                    ++_srs_pps_aloss->sugar;
                    continue;
                }

                // OK, we got packets.
                got_packets = true;

                // Try to consume the packet by tunnel.
                if (tunnel_enabled_ && consume_by_tunnel(listener->skt_)) {
                    continue;
                }

                // If got packet, copy to the queue.
                queue->push_back(listener->skt_->copy());
            }
        }

        // Once there is packets in kernel buffer, we MUST read it ASAP.
        if (got_packets) {
            continue;
        }

        // TODO: FIXME: Maybe we should use cond wait?
        timespec tv = {0};
        tv.tv_sec = interval / SRS_UTIME_SECONDS;
        tv.tv_nsec = (interval % SRS_UTIME_SECONDS) * 1000;
        nanosleep(&tv, NULL);
    }

    return err;
}

void SrsAsyncRecvManager::deep_copy_listeners(vector<SrsThreadUdpListener*>& cp)
{
    // Free it first.
    for (int i = 0; i < (int)cp.size(); i++) {
        SrsThreadUdpListener* p = cp.at(i);
        srs_freep(p);
    }
    cp.clear();

    // Deep copy all listeners.
    for (int i = 0; i < (int)listeners_.size(); i++) {
        SrsThreadUdpListener* p = listeners_.at(i);
        cp.push_back(p->copy());
    }
}

srs_error_t SrsAsyncRecvManager::consume(SrsThreadEntry* entry, int* nn_consumed)
{
    srs_error_t err = srs_success;

    // How many messages to run a yield.
    uint32_t nn_msgs_for_yield = 0;

    vector<SrsUdpMuxSocket*> flying_received_packets;
    entry->received_packets_->swap(flying_received_packets);

    if (flying_received_packets.empty()) {
        return err;
    }

    *nn_consumed += (int)flying_received_packets.size();

    for (int i = 0; i < (int)flying_received_packets.size(); i++) {
        SrsUdpMuxSocket* pkt = flying_received_packets.at(i);

        ISrsUdpMuxHandler* handler = pkt->handler();
        if (handler && (err = handler->on_udp_packet(pkt)) != srs_success) {
            srs_error_reset(err); // Ignore any error.
        }

        srs_freep(pkt);

        // Yield to another coroutines.
        // @see https://github.com/ossrs/srs/issues/2194#issuecomment-777485531
        if (++nn_msgs_for_yield > 10) {
            nn_msgs_for_yield = 0;
            srs_thread_yield();
        }
    }

    return err;
}

bool SrsAsyncRecvManager::consume_by_tunnel(SrsUdpMuxSocket* skt)
{
    uint64_t fast_id = skt->fast_id();
    SrsAsyncSRTPTask* task = tunnels_->find(fast_id);
    if (!task) {
        return false;
    }

    char* data = skt->data(); int size = skt->size();
    bool is_rtp_or_rtcp = srs_is_rtp_or_rtcp((uint8_t*)data, size);
    bool is_rtcp = srs_is_rtcp((uint8_t*)data, size);
    if (!is_rtp_or_rtcp) {
        return false;
    }

    int nb_cipher = size;
    char* buf = new char[nb_cipher];
    memcpy(buf, data, nb_cipher);

    SrsAsyncSRTPPacket* pkt = new SrsAsyncSRTPPacket(task);
    pkt->msg_->wrap(buf, nb_cipher);
    pkt->is_rtp_ = !is_rtcp;
    pkt->do_decrypt_ = true;
    _srs_async_srtp->add_packet(pkt);

    ++_srs_tunnel_recv_hit->sugar;
    return true;
}

SrsAsyncRecvManager* _srs_async_recv = new SrsAsyncRecvManager();

SrsAsyncUdpPacket::SrsAsyncUdpPacket()
{
    skt_ = NULL;
    data_ = NULL;
    size_ = 0;
}

SrsAsyncUdpPacket::~SrsAsyncUdpPacket()
{
    srs_freep(skt_);
    srs_freepa(data_);
}

void SrsAsyncUdpPacket::from(SrsUdpMuxSocket* skt, char* data, int size)
{
    skt_ = skt->copy();
    size_ = size;

    if (size) {
        data_ = new char[size];
        memcpy(data_, data, size);
    }
}

SrsAsyncSendManager::SrsAsyncSendManager()
{
    enabled_ = false;
    sending_packets_ = new SrsThreadQueue<SrsAsyncUdpPacket>();
}

SrsAsyncSendManager::~SrsAsyncSendManager()
{
    srs_freep(sending_packets_);
}

void SrsAsyncSendManager::add_packet(SrsAsyncUdpPacket* pkt)
{
    sending_packets_->push_back(pkt);
}

srs_error_t SrsAsyncSendManager::start(void* arg)
{
    SrsAsyncSendManager* send = (SrsAsyncSendManager*)arg;
    return send->do_start();
}

srs_error_t SrsAsyncSendManager::do_start()
{
    srs_error_t err = srs_success;

    // TODO: FIXME: Config it?
    srs_utime_t interval = 10 * SRS_UTIME_MILLISECONDS;

    while (true) {
        vector<SrsAsyncUdpPacket*> flying_sending_packets;
        sending_packets_->swap(flying_sending_packets);

        for (int i = 0; i < (int)flying_sending_packets.size(); i++) {
            SrsAsyncUdpPacket* pkt = flying_sending_packets.at(i);

            int r0 = pkt->skt_->raw_sendto(pkt->data_, pkt->size_);
            if (r0 <= 0) {
                // Ignore any error.
            }

            srs_freep(pkt);
        }

        // Once there are packets to send, we MUST send it ASAP.
        if (!flying_sending_packets.empty()) {
            continue;
        }

        // TODO: FIXME: Maybe we should use cond wait?
        timespec tv = {0};
        tv.tv_sec = interval / SRS_UTIME_SECONDS;
        tv.tv_nsec = (interval % SRS_UTIME_SECONDS) * 1000;
        nanosleep(&tv, NULL);
    }

    return err;
}

SrsAsyncSendManager* _srs_async_send = new SrsAsyncSendManager();