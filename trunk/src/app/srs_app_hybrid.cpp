/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2021 Winlin
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

#include <srs_app_hybrid.hpp>

#include <srs_app_server.hpp>
#include <srs_app_config.hpp>
#include <srs_kernel_error.hpp>
#include <srs_service_st.hpp>
#include <srs_app_utility.hpp>
#include <srs_app_threads.hpp>
#include <srs_app_rtc_server.hpp>

using namespace std;

extern SrsPps* _srs_pps_cids_get;
extern SrsPps* _srs_pps_cids_set;

extern SrsPps* _srs_pps_timer;
extern SrsPps* _srs_pps_pub;
extern SrsPps* _srs_pps_conn;
extern SrsPps* _srs_pps_dispose;

#if defined(SRS_DEBUG) && defined(SRS_DEBUG_STATS)
extern unsigned long long _st_stat_recvfrom;
extern unsigned long long _st_stat_recvfrom_eagain;
extern unsigned long long _st_stat_sendto;
extern unsigned long long _st_stat_sendto_eagain;
SrsPps* _srs_pps_recvfrom = new SrsPps();
SrsPps* _srs_pps_recvfrom_eagain = new SrsPps();
SrsPps* _srs_pps_sendto = new SrsPps();
SrsPps* _srs_pps_sendto_eagain = new SrsPps();

extern unsigned long long _st_stat_read;
extern unsigned long long _st_stat_read_eagain;
extern unsigned long long _st_stat_readv;
extern unsigned long long _st_stat_readv_eagain;
extern unsigned long long _st_stat_writev;
extern unsigned long long _st_stat_writev_eagain;
SrsPps* _srs_pps_read = new SrsPps();
SrsPps* _srs_pps_read_eagain = new SrsPps();
SrsPps* _srs_pps_readv = new SrsPps();
SrsPps* _srs_pps_readv_eagain = new SrsPps();
SrsPps* _srs_pps_writev = new SrsPps();
SrsPps* _srs_pps_writev_eagain = new SrsPps();

extern unsigned long long _st_stat_recvmsg;
extern unsigned long long _st_stat_recvmsg_eagain;
extern unsigned long long _st_stat_sendmsg;
extern unsigned long long _st_stat_sendmsg_eagain;
SrsPps* _srs_pps_recvmsg = new SrsPps();
SrsPps* _srs_pps_recvmsg_eagain = new SrsPps();
SrsPps* _srs_pps_sendmsg = new SrsPps();
SrsPps* _srs_pps_sendmsg_eagain = new SrsPps();

extern unsigned long long _st_stat_epoll;
extern unsigned long long _st_stat_epoll_zero;
extern unsigned long long _st_stat_epoll_shake;
extern unsigned long long _st_stat_epoll_spin;
SrsPps* _srs_pps_epoll = new SrsPps();
SrsPps* _srs_pps_epoll_zero = new SrsPps();
SrsPps* _srs_pps_epoll_shake = new SrsPps();
SrsPps* _srs_pps_epoll_spin = new SrsPps();

extern unsigned long long _st_stat_sched_15ms;
extern unsigned long long _st_stat_sched_20ms;
extern unsigned long long _st_stat_sched_25ms;
extern unsigned long long _st_stat_sched_30ms;
extern unsigned long long _st_stat_sched_35ms;
extern unsigned long long _st_stat_sched_40ms;
extern unsigned long long _st_stat_sched_80ms;
extern unsigned long long _st_stat_sched_160ms;
extern unsigned long long _st_stat_sched_s;
SrsPps* _srs_pps_sched_15ms = new SrsPps();
SrsPps* _srs_pps_sched_20ms = new SrsPps();
SrsPps* _srs_pps_sched_25ms = new SrsPps();
SrsPps* _srs_pps_sched_30ms = new SrsPps();
SrsPps* _srs_pps_sched_35ms = new SrsPps();
SrsPps* _srs_pps_sched_40ms = new SrsPps();
SrsPps* _srs_pps_sched_80ms = new SrsPps();
SrsPps* _srs_pps_sched_160ms = new SrsPps();
SrsPps* _srs_pps_sched_s = new SrsPps();
#endif

SrsPps* _srs_pps_clock_15ms = new SrsPps();
SrsPps* _srs_pps_clock_20ms = new SrsPps();
SrsPps* _srs_pps_clock_25ms = new SrsPps();
SrsPps* _srs_pps_clock_30ms = new SrsPps();
SrsPps* _srs_pps_clock_35ms = new SrsPps();
SrsPps* _srs_pps_clock_40ms = new SrsPps();
SrsPps* _srs_pps_clock_80ms = new SrsPps();
SrsPps* _srs_pps_clock_160ms = new SrsPps();
SrsPps* _srs_pps_timer_s = new SrsPps();

#if defined(SRS_DEBUG) && defined(SRS_DEBUG_STATS)
extern int _st_active_count;
extern unsigned long long _st_stat_thread_run;
extern unsigned long long _st_stat_thread_idle;
extern unsigned long long _st_stat_thread_yield;
extern unsigned long long _st_stat_thread_yield2;
SrsPps* _srs_pps_thread_run = new SrsPps();
SrsPps* _srs_pps_thread_idle = new SrsPps();
SrsPps* _srs_pps_thread_yield = new SrsPps();
SrsPps* _srs_pps_thread_yield2 = new SrsPps();
#endif

extern SrsPps* _srs_pps_objs_rtps;
extern SrsPps* _srs_pps_objs_rraw;
extern SrsPps* _srs_pps_objs_rfua;
extern SrsPps* _srs_pps_objs_rbuf;
extern SrsPps* _srs_pps_objs_msgs;
extern SrsPps* _srs_pps_objs_rothers;

ISrsHybridServer::ISrsHybridServer()
{
}

ISrsHybridServer::~ISrsHybridServer()
{
}

SrsHybridServer::SrsHybridServer()
{
    // Note that the timer depends on other global variables,
    // so we MUST never create it in constructor.
    timer_ = NULL;

    clock_monitor_ = new SrsClockWallMonitor();

    stream_index_ = -1;
}

SrsHybridServer::~SrsHybridServer()
{
    srs_freep(clock_monitor_);
    srs_freep(timer_);

    vector<ISrsHybridServer*>::iterator it;
    for (it = servers.begin(); it != servers.end(); ++it) {
        ISrsHybridServer* server = *it;
        srs_freep(server);
    }
    servers.clear();
}

void SrsHybridServer::register_server(ISrsHybridServer* svr)
{
    servers.push_back(svr);
}

srs_error_t SrsHybridServer::initialize()
{
    srs_error_t err = srs_success;

    // Create global shared timer.
    timer_ = new SrsFastTimer("hybrid", 20 * SRS_UTIME_MILLISECONDS);

    // Start the timer first.
    if ((err = timer_->start()) != srs_success) {
        return srs_error_wrap(err, "start timer");
    }

    // The hybrid server start a timer, do routines of hybrid server.
    timer_->subscribe(5 * SRS_UTIME_SECONDS, this);

    // A monitor to check the clock wall deviation, per clock tick.
    timer_->subscribe(20 * SRS_UTIME_MILLISECONDS, clock_monitor_);

    vector<ISrsHybridServer*>::iterator it;
    for (it = servers.begin(); it != servers.end(); ++it) {
        ISrsHybridServer* server = *it;

        if ((err = server->initialize()) != srs_success) {
            return srs_error_wrap(err, "init server");
        }
    }

    // Create slots for other threads to communicate with us.
    SrsThreadEntry* self = _srs_thread_pool->self();

    self->slot_ = new SrsThreadPipeSlot(1);

    if ((err = self->slot_->initialize()) != srs_success) {
        return srs_error_wrap(err, "init slot");
    }

    if ((err = self->slot_->open_responder(this)) != srs_success) {
        return srs_error_wrap(err, "init slot");
    }

    return err;
}

srs_error_t SrsHybridServer::run()
{
    srs_error_t err = srs_success;

    // Run all servers, which should never block.
    vector<ISrsHybridServer*>::iterator it;
    for (it = servers.begin(); it != servers.end(); ++it) {
        ISrsHybridServer* server = *it;

        if ((err = server->run()) != srs_success) {
            return srs_error_wrap(err, "run server");
        }
    }

    // Get the entry of self thread.
    SrsThreadEntry* entry = _srs_thread_pool->self();

    // Consume the async UDP/SRTP packets.
    while (true) {
        int consumed = 0;

        // Consume the received UDP packets.
        // Note that this might run in multiple threads, but it's ok.
        if ((err = _srs_async_recv->consume(entry, &consumed)) != srs_success) {
            srs_error_reset(err); // Ignore any error.
        }

        // Consume the cooked SRTP packets.
        // Note that this might run in multiple threads, but it's ok.
        if ((err = _srs_async_srtp->consume(entry, &consumed)) != srs_success) {
            srs_error_reset(err); // Ignore any error.
        }

        // Wait for a while if no packets.
        if (!consumed) {
            srs_usleep(20 * SRS_UTIME_MILLISECONDS);
        }
    }

    return err;
}

void SrsHybridServer::stop()
{
    vector<ISrsHybridServer*>::iterator it;
    for (it = servers.begin(); it != servers.end(); ++it) {
        ISrsHybridServer* server = *it;
        server->stop();
    }
}

SrsFastTimer* SrsHybridServer::timer()
{
    return timer_;
}

srs_error_t SrsHybridServer::on_timer(srs_utime_t interval, srs_utime_t tick)
{
    srs_error_t err = srs_success;

    // Show statistics for RTC server.
    SrsProcSelfStat* u = srs_get_self_proc_stat();
    // Resident Set Size: number of pages the process has in real memory.
    int memory = (int)(u->rss * 4 / 1024);

    static char buf[128];

    string cid_desc;
    _srs_pps_cids_get->update(); _srs_pps_cids_set->update();
    if (_srs_pps_cids_get->r10s() || _srs_pps_cids_set->r10s()) {
        snprintf(buf, sizeof(buf), ", cid=%d,%d", _srs_pps_cids_get->r10s(), _srs_pps_cids_set->r10s());
        cid_desc = buf;
    }

    string timer_desc;
    _srs_pps_timer->update(); _srs_pps_pub->update(); _srs_pps_conn->update();
    if (_srs_pps_timer->r10s() || _srs_pps_pub->r10s() || _srs_pps_conn->r10s()) {
        snprintf(buf, sizeof(buf), ", timer=%d,%d,%d", _srs_pps_timer->r10s(), _srs_pps_pub->r10s(), _srs_pps_conn->r10s());
        timer_desc = buf;
    }

    string free_desc;
    _srs_pps_dispose->update();
    if (_srs_pps_dispose->r10s()) {
        snprintf(buf, sizeof(buf), ", free=%d", _srs_pps_dispose->r10s());
        free_desc = buf;
    }

    string recvfrom_desc;
#if defined(SRS_DEBUG) && defined(SRS_DEBUG_STATS)
    _srs_pps_recvfrom->update(_st_stat_recvfrom); _srs_pps_recvfrom_eagain->update(_st_stat_recvfrom_eagain);
    _srs_pps_sendto->update(_st_stat_sendto); _srs_pps_sendto_eagain->update(_st_stat_sendto_eagain);
    if (_srs_pps_recvfrom->r10s() || _srs_pps_recvfrom_eagain->r10s() || _srs_pps_sendto->r10s() || _srs_pps_sendto_eagain->r10s()) {
        snprintf(buf, sizeof(buf), ", udp=%d,%d,%d,%d", _srs_pps_recvfrom->r10s(), _srs_pps_recvfrom_eagain->r10s(), _srs_pps_sendto->r10s(), _srs_pps_sendto_eagain->r10s());
        recvfrom_desc = buf;
    }
#endif

    string io_desc;
#if defined(SRS_DEBUG) && defined(SRS_DEBUG_STATS)
    _srs_pps_read->update(_st_stat_read); _srs_pps_read_eagain->update(_st_stat_read_eagain);
    _srs_pps_readv->update(_st_stat_readv); _srs_pps_readv_eagain->update(_st_stat_readv_eagain);
    _srs_pps_writev->update(_st_stat_writev); _srs_pps_writev_eagain->update(_st_stat_writev_eagain);
    if (_srs_pps_read->r10s() || _srs_pps_read_eagain->r10s() || _srs_pps_readv->r10s() || _srs_pps_readv_eagain->r10s() || _srs_pps_writev->r10s() || _srs_pps_writev_eagain->r10s()) {
        snprintf(buf, sizeof(buf), ", io=%d,%d,%d,%d,%d,%d", _srs_pps_read->r10s(), _srs_pps_read_eagain->r10s(), _srs_pps_readv->r10s(), _srs_pps_readv_eagain->r10s(), _srs_pps_writev->r10s(), _srs_pps_writev_eagain->r10s());
        io_desc = buf;
    }
#endif

    string msg_desc;
#if defined(SRS_DEBUG) && defined(SRS_DEBUG_STATS)
    _srs_pps_recvmsg->update(_st_stat_recvmsg); _srs_pps_recvmsg_eagain->update(_st_stat_recvmsg_eagain);
    _srs_pps_sendmsg->update(_st_stat_sendmsg); _srs_pps_sendmsg_eagain->update(_st_stat_sendmsg_eagain);
    if (_srs_pps_recvmsg->r10s() || _srs_pps_recvmsg_eagain->r10s() || _srs_pps_sendmsg->r10s() || _srs_pps_sendmsg_eagain->r10s()) {
        snprintf(buf, sizeof(buf), ", msg=%d,%d,%d,%d", _srs_pps_recvmsg->r10s(), _srs_pps_recvmsg_eagain->r10s(), _srs_pps_sendmsg->r10s(), _srs_pps_sendmsg_eagain->r10s());
        msg_desc = buf;
    }
#endif

    string epoll_desc;
#if defined(SRS_DEBUG) && defined(SRS_DEBUG_STATS)
    _srs_pps_epoll->update(_st_stat_epoll); _srs_pps_epoll_zero->update(_st_stat_epoll_zero);
    _srs_pps_epoll_shake->update(_st_stat_epoll_shake); _srs_pps_epoll_spin->update(_st_stat_epoll_spin);
    if (_srs_pps_epoll->r10s() || _srs_pps_epoll_zero->r10s() || _srs_pps_epoll_shake->r10s() || _srs_pps_epoll_spin->r10s()) {
        snprintf(buf, sizeof(buf), ", epoll=%d,%d,%d,%d", _srs_pps_epoll->r10s(), _srs_pps_epoll_zero->r10s(), _srs_pps_epoll_shake->r10s(), _srs_pps_epoll_spin->r10s());
        epoll_desc = buf;
    }
#endif

    string sched_desc;
#if defined(SRS_DEBUG) && defined(SRS_DEBUG_STATS)
    _srs_pps_sched_160ms->update(_st_stat_sched_160ms); _srs_pps_sched_s->update(_st_stat_sched_s);
    _srs_pps_sched_15ms->update(_st_stat_sched_15ms); _srs_pps_sched_20ms->update(_st_stat_sched_20ms);
    _srs_pps_sched_25ms->update(_st_stat_sched_25ms); _srs_pps_sched_30ms->update(_st_stat_sched_30ms);
    _srs_pps_sched_35ms->update(_st_stat_sched_35ms); _srs_pps_sched_40ms->update(_st_stat_sched_40ms);
    _srs_pps_sched_80ms->update(_st_stat_sched_80ms);
    if (_srs_pps_sched_160ms->r10s() || _srs_pps_sched_s->r10s() || _srs_pps_sched_15ms->r10s() || _srs_pps_sched_20ms->r10s() || _srs_pps_sched_25ms->r10s() || _srs_pps_sched_30ms->r10s() || _srs_pps_sched_35ms->r10s() || _srs_pps_sched_40ms->r10s() || _srs_pps_sched_80ms->r10s()) {
        snprintf(buf, sizeof(buf), ", sched=%d,%d,%d,%d,%d,%d,%d,%d,%d", _srs_pps_sched_15ms->r10s(), _srs_pps_sched_20ms->r10s(), _srs_pps_sched_25ms->r10s(), _srs_pps_sched_30ms->r10s(), _srs_pps_sched_35ms->r10s(), _srs_pps_sched_40ms->r10s(), _srs_pps_sched_80ms->r10s(), _srs_pps_sched_160ms->r10s(), _srs_pps_sched_s->r10s());
        sched_desc = buf;
    }
#endif

    string clock_desc;
    _srs_pps_clock_15ms->update(); _srs_pps_clock_20ms->update();
    _srs_pps_clock_25ms->update(); _srs_pps_clock_30ms->update();
    _srs_pps_clock_35ms->update(); _srs_pps_clock_40ms->update();
    _srs_pps_clock_80ms->update(); _srs_pps_clock_160ms->update();
    _srs_pps_timer_s->update();
    if (_srs_pps_clock_15ms->r10s() || _srs_pps_timer_s->r10s() || _srs_pps_clock_20ms->r10s() || _srs_pps_clock_25ms->r10s() || _srs_pps_clock_30ms->r10s() || _srs_pps_clock_35ms->r10s() || _srs_pps_clock_40ms->r10s() || _srs_pps_clock_80ms->r10s() || _srs_pps_clock_160ms->r10s()) {
        snprintf(buf, sizeof(buf), ", clock=%d,%d,%d,%d,%d,%d,%d,%d,%d", _srs_pps_clock_15ms->r10s(), _srs_pps_clock_20ms->r10s(), _srs_pps_clock_25ms->r10s(), _srs_pps_clock_30ms->r10s(), _srs_pps_clock_35ms->r10s(), _srs_pps_clock_40ms->r10s(), _srs_pps_clock_80ms->r10s(), _srs_pps_clock_160ms->r10s(), _srs_pps_timer_s->r10s());
        clock_desc = buf;
    }

    string thread_desc;
#if defined(SRS_DEBUG) && defined(SRS_DEBUG_STATS)
    _srs_pps_thread_run->update(_st_stat_thread_run); _srs_pps_thread_idle->update(_st_stat_thread_idle);
    _srs_pps_thread_yield->update(_st_stat_thread_yield); _srs_pps_thread_yield2->update(_st_stat_thread_yield2);
    if (_st_active_count > 0 || _srs_pps_thread_run->r10s() || _srs_pps_thread_idle->r10s() || _srs_pps_thread_yield->r10s() || _srs_pps_thread_yield2->r10s()) {
        snprintf(buf, sizeof(buf), ", co=%d,%d,%d, yield=%d,%d", _st_active_count, _srs_pps_thread_run->r10s(), _srs_pps_thread_idle->r10s(), _srs_pps_thread_yield->r10s(), _srs_pps_thread_yield2->r10s());
        thread_desc = buf;
    }
#endif

    string objs_desc;
    _srs_pps_objs_rtps->update(); _srs_pps_objs_rraw->update(); _srs_pps_objs_rfua->update(); _srs_pps_objs_rbuf->update(); _srs_pps_objs_msgs->update(); _srs_pps_objs_rothers->update(); _srs_pps_objs_drop->update();
    if (_srs_pps_objs_rtps->r10s() || _srs_pps_objs_rraw->r10s() || _srs_pps_objs_rfua->r10s() || _srs_pps_objs_rbuf->r10s() || _srs_pps_objs_msgs->r10s() || _srs_pps_objs_rothers->r10s() || _srs_pps_objs_drop->r10s()) {
        snprintf(buf, sizeof(buf), ", objs=(pkt:%d,raw:%d,fua:%d,msg:%d,oth:%d,buf:%d,drop:%d)",
            _srs_pps_objs_rtps->r10s(), _srs_pps_objs_rraw->r10s(), _srs_pps_objs_rfua->r10s(),
            _srs_pps_objs_msgs->r10s(), _srs_pps_objs_rothers->r10s(), _srs_pps_objs_rbuf->r10s(), _srs_pps_objs_drop->r10s());
        objs_desc = buf;
    }

    string cache_desc;
    if (_srs_rtp_cache->size() || _srs_rtp_raw_cache->size() || _srs_rtp_fua_cache->size() || _srs_rtp_msg_cache_buffers->size() || _srs_rtp_msg_cache_objs->size()) {
        snprintf(buf, sizeof(buf), ", cache=(pkt:%d-%dw,raw:%d-%dw,fua:%d-%dw,msg:%d-%dw,buf:%d-%dw)",
            _srs_rtp_cache->size(), _srs_rtp_cache->capacity()/10000, _srs_rtp_raw_cache->size(), _srs_rtp_raw_cache->capacity()/10000,
            _srs_rtp_fua_cache->size(), _srs_rtp_fua_cache->capacity()/10000, _srs_rtp_msg_cache_objs->size(), _srs_rtp_msg_cache_objs->capacity()/10000,
            _srs_rtp_msg_cache_buffers->size(), _srs_rtp_msg_cache_buffers->capacity()/10000);
        cache_desc = buf;
    }

    srs_trace("Hybrid cpu=%.2f%%,%dMB%s%s%s%s%s%s%s%s%s%s%s%s",
        u->percent * 100, memory,
        cid_desc.c_str(), timer_desc.c_str(),
        recvfrom_desc.c_str(), io_desc.c_str(), msg_desc.c_str(),
        epoll_desc.c_str(), sched_desc.c_str(), clock_desc.c_str(),
        thread_desc.c_str(), free_desc.c_str(), objs_desc.c_str(), cache_desc.c_str()
    );

    return err;
}

srs_error_t SrsHybridServer::on_thread_message(SrsThreadMessage* msg, SrsThreadPipeChannel* channel)
{
    srs_error_t err = srs_success;

    RtcServerAdapter* adapter = NULL;
    if (true) {
        vector<ISrsHybridServer*> servers = _srs_hybrid->servers;
        for (vector<ISrsHybridServer*>::iterator it = servers.begin(); it != servers.end(); ++it) {
            RtcServerAdapter* server = dynamic_cast<RtcServerAdapter*>(*it);
            if (server) {
                adapter = server;
                break;
            }
        }
    }

    // TODO: FIXME: Response error?
    if (!adapter) {
        return err;
    }

    if (msg->id == (uint64_t)SrsThreadMessageIDRtcCreateSession) {
        SrsThreadMessageRtcCreateSession* s = (SrsThreadMessageRtcCreateSession*)msg->ptr;
        err = adapter->rtc->create_session(s->req, s->remote_sdp, s->local_sdp, s->mock_eip,
            s->publish, s->dtls, s->srtp, &s->session);

        // TODO: FIXME: Response error?
        if (err != srs_success) {
            return srs_error_wrap(err, "create session");
        }

        // TODO: FIXME: Response timeout if error?
        // TODO: FIXME: Response a different message? With trace ID?
        // We're responder, write response to responder.
        err = channel->responder()->write(msg, sizeof(SrsThreadMessage), NULL);
        if (err != srs_success) {
            return srs_error_wrap(err, "response");
        }
    }

    return err;
}

 __thread SrsHybridServer* _srs_hybrid = NULL;

