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

#ifndef SRS_APP_THREADS_HPP
#define SRS_APP_THREADS_HPP

#include <srs_core.hpp>

#include <srs_kernel_file.hpp>
#include <srs_kernel_flv.hpp>
#include <srs_app_rtc_dtls.hpp>
#include <srs_app_rtc_conn.hpp>
#include <srs_app_listener.hpp>
#include <srs_app_st.hpp>

#include <pthread.h>

#include <vector>
#include <string>

class SrsThreadPool;
class SrsAsyncSRTPTask;
class SrsAsyncSRTPPacket;
class SrsSecurityTransport;
class SrsProcSelfStat;

// The thread mutex wrapper, without error.
class SrsThreadMutex
{
private:
    pthread_mutex_t lock_;
    pthread_mutexattr_t attr_;
public:
    SrsThreadMutex();
    virtual ~SrsThreadMutex();
public:
    void lock();
    void unlock();
};

// The thread mutex locker.
#define SrsThreadLocker(instance) \
    impl__SrsThreadLocker _SRS_free_##instance(instance)

class impl__SrsThreadLocker
{
private:
    SrsThreadMutex* lock;
public:
    impl__SrsThreadLocker(SrsThreadMutex* l) {
        lock = l;
        lock->lock();
    }
    virtual ~impl__SrsThreadLocker() {
        lock->unlock();
    }
};

// Thread-safe queue.
template<typename T>
class SrsThreadQueue
{
private:
    std::vector<T*> dirty_;
    SrsThreadMutex* lock_;
public:
    // SrsThreadQueue::SrsThreadQueue
    SrsThreadQueue() {
        lock_ = new SrsThreadMutex();
    }
    // SrsThreadQueue::~SrsThreadQueue
    virtual ~SrsThreadQueue() {
        srs_freep(lock_);
        for (int i = 0; i < (int)dirty_.size(); i++) {
            T* msg = dirty_.at(i);
            srs_freep(msg);
        }
    }
public:
    // SrsThreadQueue::push_back
    void push_back(T* msg) {
        SrsThreadLocker(lock_);
        dirty_.push_back(msg);
    }
    // SrsThreadQueue::push_back
    void push_back(std::vector<T*>& flying) {
        SrsThreadLocker(lock_);
        dirty_.insert(dirty_.end(), flying.begin(), flying.end());
    }
    // SrsThreadQueue::swap
    void swap(std::vector<T*>& flying) {
        SrsThreadLocker(lock_);
        dirty_.swap(flying);
    }
    // SrsThreadQueue::size
    size_t size() {
        SrsThreadLocker(lock_);
        return dirty_.size();
    }
};

#ifdef SRS_OSX
    typedef uint64_t cpu_set_t;
    #define CPU_ZERO(p) *p = 0
#endif

// The information for a thread.
class SrsThreadEntry
{
public:
    SrsThreadPool* pool;
    std::string label;
    std::string name;
    srs_error_t (*start)(void* arg);
    void* arg;
    int num;
    // @see https://man7.org/linux/man-pages/man2/gettid.2.html
    pid_t tid;
public:
    // The thread object.
    pthread_t trd;
    // The exit error of thread.
    srs_error_t err;
public:
    // @see https://man7.org/linux/man-pages/man3/pthread_setaffinity_np.3.html
    cpu_set_t cpuset; // Config value.
    cpu_set_t cpuset2; // Actual value.
    bool cpuset_ok;
public:
    SrsProcSelfStat* stat;
public:
    // The received UDP packets.
    SrsThreadQueue<SrsUdpMuxSocket>* received_packets_;
    // The packets cooked by async SRTP manager.
    SrsThreadQueue<SrsAsyncSRTPPacket>* cooked_packets_;
public:
    SrsThreadEntry();
    virtual ~SrsThreadEntry();
};

// Allocate a(or almost) fixed thread poll to execute tasks,
// so that we can take the advantage of multiple CPUs.
class SrsThreadPool
{
private:
    SrsThreadEntry* entry_;
    srs_utime_t interval_;
private:
    SrsThreadMutex* lock_;
    std::vector<SrsThreadEntry*> threads_;
private:
    // The hybrid server entry, the cpu percent used for circuit breaker.
    SrsThreadEntry* hybrid_;
    // Reset the water-level when CPU is low for N times.
    // @note To avoid the CPU change rapidly.
    int hybrid_high_water_level_;
    int hybrid_critical_water_level_;
    int hybrid_dying_water_level_;
private:
    // The config for high/critical water level.
    int high_threshold_;
    int high_pulse_;
    int critical_threshold_;
    int critical_pulse_;
    int dying_threshold_;
    int dying_pulse_;
public:
    SrsThreadPool();
    virtual ~SrsThreadPool();
public:
    // Whether hybrid server water-level is high.
    bool hybrid_high_water_level();
    bool hybrid_critical_water_level();
    bool hybrid_dying_water_level();
    // Setup the thread-local variables.
    static void setup();
    // Initialize the thread pool.
    srs_error_t initialize();
    // Execute start function with label in thread.
    srs_error_t execute(std::string label, srs_error_t (*start)(void* arg), void* arg);
    // Run in the primordial thread, util stop or quit.
    srs_error_t run();
    // Stop the thread pool and quit the primordial thread.
    void stop();
public:
    SrsThreadEntry* self();
    SrsThreadEntry* hybrid();
private:
    static void* start(void* arg);
};

// The global thread pool.
extern SrsThreadPool* _srs_thread_pool;

// Async file writer, it's thread safe.
class SrsAsyncFileWriter : public ISrsWriter
{
    friend class SrsAsyncLogManager;
private:
    std::string filename_;
    SrsFileWriter* writer_;
private:
    // The thread-queue, to flush to disk by dedicated thread.
    SrsThreadQueue<SrsSharedPtrMessage>* chunks_;
private:
    SrsAsyncFileWriter(std::string p);
    virtual ~SrsAsyncFileWriter();
public:
    // Open file writer, in truncate mode.
    virtual srs_error_t open();
    // Open file writer, in append mode.
    virtual srs_error_t open_append();
    // Close current writer.
    virtual void close();
// Interface ISrsWriteSeeker
public:
    virtual srs_error_t write(void* buf, size_t count, ssize_t* pnwrite);
    virtual srs_error_t writev(const iovec* iov, int iovcnt, ssize_t* pnwrite);
public:
    // Flush thread-queue to disk, generally by dedicated thread.
    srs_error_t flush();
};

// The async log file writer manager, use a thread to flush multiple writers,
// and reopen all log files when got LOGROTATE signal.
class SrsAsyncLogManager
{
private:
    // The async flush interval.
    srs_utime_t interval_;
private:
    // The async reopen event.
    bool reopen_;
private:
    SrsThreadMutex* lock_;
    std::vector<SrsAsyncFileWriter*> writers_;
public:
    SrsAsyncLogManager();
    virtual ~SrsAsyncLogManager();
public:
    // Initialize the async log manager.
    srs_error_t initialize();
    // Run the async log manager thread.
    static srs_error_t start(void* arg);
    // Create a managed writer, user should never free it.
    srs_error_t create_writer(std::string filename, SrsAsyncFileWriter** ppwriter);
    // Reopen all log files, asynchronously.
    virtual void reopen();
public:
    // Get the summary of this manager.
    std::string description();
private:
    srs_error_t do_start();
};

// The global async log manager.
extern SrsAsyncLogManager* _srs_async_log;

// The async SRTP codec.
class SrsAsyncSRTP : public SrsSRTP
{
    friend class SrsAsyncSRTPTask;
private:
    SrsAsyncSRTPTask* task_;
    SrsSecurityTransport* transport_;
public:
    SrsAsyncSRTP(SrsSecurityTransport* transport);
    virtual ~SrsAsyncSRTP();
public:
    srs_error_t initialize(std::string recv_key, std::string send_key);
    srs_error_t protect_rtp(void* packet, int* nb_cipher);
    srs_error_t protect_rtcp(void* packet, int* nb_cipher);
    srs_error_t unprotect_rtp(void* packet, int* nb_plaintext);
    srs_error_t unprotect_rtcp(void* packet, int* nb_plaintext);
public:
    void dig_tunnel(SrsUdpMuxSocket* skt);
};

// The async SRTP task, bind to the codec, managed by SrsAsyncSRTPManager,
// which alive longer than either SrsAsyncSRTPTask or SrsAsyncSRTP.
class SrsAsyncSRTPTask
{
private:
    SrsAsyncSRTP* codec_;
    SrsSRTP* impl_;
    // For disposing, only set a flag, free it in future.
    int disposing_;
private:
    // For tunnel, srtp-send.
    SrsUdpMuxSocket* sendonly_skt_;
    SrsThreadMutex* lock_;
public:
    // The thread entry, to which the listener belongs.
    SrsThreadEntry* entry_;
    // For multiple SRTP thread to use the same task to cook packets.
    SrsThreadMutex* srtp_lock_;
public:
    SrsAsyncSRTPTask(SrsAsyncSRTP* codec);
    virtual ~SrsAsyncSRTPTask();
public:
    srs_error_t initialize(std::string recv_key, std::string send_key);
    void dispose();
public:
    srs_error_t cook(SrsAsyncSRTPPacket* pkt);
    srs_error_t consume(SrsAsyncSRTPPacket* pkt);
public:
    void dig_tunnel(SrsUdpMuxSocket* skt);
    // Try to consume by tunnel.
    bool consume_by_tunnel(SrsAsyncSRTPPacket* pkt);
};

// The async SRTP packet, handle by task.
class SrsAsyncSRTPPacket
{
public:
    bool is_rtp_;
    bool do_decrypt_;
    SrsAsyncSRTPTask* task_;
    SrsSharedPtrMessage* msg_;
public:
    int nb_consumed_;
public:
    SrsAsyncSRTPPacket(SrsAsyncSRTPTask* task);
    virtual ~SrsAsyncSRTPPacket();
};

// The async SRTP manager, to start a thread to consume packets.
class SrsAsyncSRTPManager
{
private:
    std::vector<SrsAsyncSRTPTask*> tasks_;
    SrsThreadMutex* lock_;
private:
    SrsThreadQueue<SrsAsyncSRTPPacket>* srtp_packets_;
private:
    // Whether enabled tunnel.
    bool tunnel_enabled_;
public:
    SrsAsyncSRTPManager();
    virtual ~SrsAsyncSRTPManager();
public:
    // Enable or disable the tunnel.
    // SrsAsyncSRTPManager::set_tunnel_enabled()
    void set_tunnel_enabled(bool v) { tunnel_enabled_ = v; }
    void register_task(SrsAsyncSRTPTask* task);
    void on_srtp_codec_destroy(SrsAsyncSRTPTask* task);
    void add_packet(SrsAsyncSRTPPacket* pkt);
    int size();
    static srs_error_t start(void* arg);
private:
    srs_error_t do_start();
public:
    // Consume cooked SRTP packets. Must call in worker/service thread.
    virtual srs_error_t consume(SrsThreadEntry* entry, int* nn_consumed);
};

// The global async SRTP manager.
extern SrsAsyncSRTPManager* _srs_async_srtp;

// A thread-safe UDP listener.
// TODO: FIXME: Use st_recvfrom to recv if thread-safe ST is ok.
class SrsThreadUdpListener
{
public:
    SrsUdpMuxSocket* skt_;
public:
    // The thread entry, to which the listener belongs.
    SrsThreadEntry* entry_;
public:
    SrsThreadUdpListener();
    SrsThreadUdpListener(srs_netfd_t fd, ISrsUdpMuxHandler* handler);
    virtual ~SrsThreadUdpListener();
public:
    SrsThreadUdpListener* copy();
};

// The tunnel for recv to directly consume packets to SRTP decrypt.
class SrsRecvTunnels
{
private:
    // The tunnel for recv to directly consume packets to SRTP decrypt.
    // Key is fast_id of UDP packet, value is SRTP task.
    std::map<uint64_t, SrsAsyncSRTPTask*> tunnels_;
    SrsThreadMutex* lock_;
public:
    SrsRecvTunnels();
    virtual ~SrsRecvTunnels();
public:
    // Dig a tunnel for recv thread.
    void dig_tunnel(uint64_t fast_id, SrsAsyncSRTPTask* task);
    // Get the SRTP task to handle packet.
    SrsAsyncSRTPTask* find(uint64_t fast_id);
};

// The async RECV manager, to recv UDP packets.
class SrsAsyncRecvManager
{
private:
    // The tunnel for recv to directly consume packets to SRTP decrypt.
    SrsRecvTunnels* tunnels_;
private:
    // If exceed max queue, drop packet.
    int max_recv_queue_;
    // Whether enabled tunnel.
    bool tunnel_enabled_;
private:
    std::vector<SrsThreadUdpListener*> listeners_;
    SrsThreadMutex* lock_;
public:
    SrsAsyncRecvManager();
    virtual ~SrsAsyncRecvManager();
public:
    // Get the recv tunnels.
    // SrsAsyncRecvManager::tunnels()
    SrsRecvTunnels* tunnels() { return tunnels_; }
    // Enable or disable the tunnel.
    // SrsAsyncRecvManager::set_tunnel_enabled()
    void set_tunnel_enabled(bool v) { tunnel_enabled_ = v; }
    // Set the max queue size.
    // SrsAsyncRecvManager::set_max_recv_queue()
    void set_max_recv_queue(int v) { max_recv_queue_ =v; }
    // Add listener to recv from.
    void add_listener(SrsThreadUdpListener* listener);
public:
    // Start the thread.
    static srs_error_t start(void* arg);
private:
    srs_error_t do_start();
    void deep_copy_listeners(std::vector<SrsThreadUdpListener*>& cp);
public:
    // Consume received UDP packets. Must call in worker/service thread.
    virtual srs_error_t consume(SrsThreadEntry* entry, int* nn_consumed);
private:
    // Try to consume by tunnel.
    bool consume_by_tunnel(SrsUdpMuxSocket* skt);
};

// The global async RECV manager.
extern SrsAsyncRecvManager* _srs_async_recv;

// The async UDP packet.
class SrsAsyncUdpPacket
{
public:
    SrsUdpMuxSocket* skt_;
    char* data_;
    int size_;
public:
    SrsAsyncUdpPacket();
    virtual ~SrsAsyncUdpPacket();
public:
    void from(SrsUdpMuxSocket* skt, char* data, int size);
};

// The async SEND manager, to send UDP packets.
class SrsAsyncSendManager
{
private:
    // By config.
    bool enabled_;
    // The UDP packets to sending.
    SrsThreadQueue<SrsAsyncUdpPacket>* sending_packets_;
public:
    SrsAsyncSendManager();
    virtual ~SrsAsyncSendManager();
public:
    // Whether the async manager is enabled.
    bool enabled() { return enabled_; }
    void set_enabled(bool v) { enabled_ = v; }
    // Send the packet.
    void add_packet(SrsAsyncUdpPacket* pkt);
    // Get the size of packets queue.
    // SrsAsyncSendManager::size()
    int size() { return sending_packets_->size(); }
    // Start the thread.
    static srs_error_t start(void* arg);
private:
    srs_error_t do_start();
};

// The global async SEND manager.
extern SrsAsyncSendManager* _srs_async_send;

#endif
