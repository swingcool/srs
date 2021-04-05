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

#ifndef SRS_APP_SERVER_HPP
#define SRS_APP_SERVER_HPP

#include <srs_core.hpp>

#include <vector>
#include <string>

#include <srs_app_st.hpp>
#include <srs_app_reload.hpp>
#include <srs_app_source.hpp>
#include <srs_app_hls.hpp>
#include <srs_app_listener.hpp>
#include <srs_app_conn.hpp>
#include <srs_service_st.hpp>
#include <srs_app_gb28181.hpp>
#include <srs_app_gb28181_sip.hpp>
#include <srs_app_hourglass.hpp>
#include <srs_app_hybrid.hpp>
#include <srs_app_rtc_api.hpp>

class SrsServer;
class SrsHttpServeMux;
class SrsHttpServer;
class SrsIngester;
class SrsHttpHeartbeat;
class SrsKbps;
class SrsConfDirective;
class ISrsTcpHandler;
class ISrsUdpHandler;
class SrsUdpListener;
class SrsTcpListener;
class SrsAppCasterFlv;
class SrsRtspCaster;
class SrsResourceManager;
class SrsGb28181Caster;


// The listener type for server to identify the connection,
// that is, use different type to process the connection.
enum SrsListenerType
{
    // RTMP client,
    SrsListenerRtmpStream = 0,
    // HTTP api,
    SrsListenerHttpApi = 1,
    // HTTP stream, HDS/HLS/DASH
    SrsListenerHttpStream = 2,
    // UDP stream, MPEG-TS over udp.
    SrsListenerMpegTsOverUdp = 3,
    // TCP stream, RTSP stream.
    SrsListenerRtsp = 4,
    // TCP stream, FLV stream over HTTP.
    SrsListenerFlv = 5,
    // UDP stream, gb28181 ps stream over rtp, 
    SrsListenerGb28181RtpMux = 6,
    // UDP gb28181 sip server
    SrsListenerGb28181Sip = 7,
    // HTTPS api,
    SrsListenerHttpsApi = 8,
    // HTTPS stream,
    SrsListenerHttpsStream = 9,
};

// To mux the tcp handler.
class ISrsTcpMuxHandler
{
public:
    ISrsTcpMuxHandler();
    virtual ~ISrsTcpMuxHandler();
public:
    // Accept the TCP client, which is identified by type.
    virtual srs_error_t accept_tcp_client(SrsListenerType type, srs_netfd_t stfd) = 0;
};

// A common tcp listener, for RTMP/HTTP server.
class SrsListener
{
protected:
    SrsListenerType type;
protected:
    std::string ip;
    int port;
    ISrsTcpMuxHandler* server;
public:
    SrsListener(ISrsTcpMuxHandler* svr, SrsListenerType t);
    virtual ~SrsListener();
public:
    virtual SrsListenerType listen_type();
    virtual srs_error_t listen(std::string i, int p) = 0;
};

// A buffered TCP listener.
class SrsBufferListener : virtual public SrsListener, virtual public ISrsTcpHandler
{
private:
    SrsTcpListener* listener;
public:
    SrsBufferListener(ISrsTcpMuxHandler* server, SrsListenerType type);
    virtual ~SrsBufferListener();
public:
    virtual srs_error_t listen(std::string ip, int port);
// Interface ISrsTcpHandler
public:
    virtual srs_error_t on_tcp_client(srs_netfd_t stfd);
};

// A TCP listener, for rtsp server.
class SrsRtspListener : virtual public SrsListener, virtual public ISrsTcpHandler
{
private:
    SrsTcpListener* listener;
    SrsRtspCaster* caster;
public:
    SrsRtspListener(SrsServer* svr, SrsListenerType t, SrsConfDirective* c);
    virtual ~SrsRtspListener();
public:
    virtual srs_error_t listen(std::string i, int p);
// Interface ISrsTcpHandler
public:
    virtual srs_error_t on_tcp_client(srs_netfd_t stfd);
};

// A TCP listener, for flv stream server.
class SrsHttpFlvListener : virtual public SrsListener, virtual public ISrsTcpHandler
{
private:
    SrsTcpListener* listener;
    SrsAppCasterFlv* caster;
public:
    SrsHttpFlvListener(SrsServer* svr, SrsListenerType t, SrsConfDirective* c);
    virtual ~SrsHttpFlvListener();
public:
    virtual srs_error_t listen(std::string i, int p);
// Interface ISrsTcpHandler
public:
    virtual srs_error_t on_tcp_client(srs_netfd_t stfd);
};

// A UDP listener, for udp server.
class SrsUdpStreamListener : public SrsListener
{
protected:
    SrsUdpListener* listener;
    ISrsUdpHandler* caster;
public:
    SrsUdpStreamListener(SrsServer* svr, SrsListenerType t, ISrsUdpHandler* c);
    virtual ~SrsUdpStreamListener();
public:
    virtual srs_error_t listen(std::string i, int p);
};

// A UDP listener, for udp stream caster server.
class SrsUdpCasterListener : public SrsUdpStreamListener
{
public:
    SrsUdpCasterListener(SrsServer* svr, SrsListenerType t, SrsConfDirective* c);
    virtual ~SrsUdpCasterListener();
};

#ifdef SRS_GB28181

// A UDP gb28181 listener, for sip and rtp stream mux server.
class SrsGb28181Listener :  public SrsUdpStreamListener
{
public:
    SrsGb28181Listener(SrsServer* svr, SrsListenerType t, SrsConfDirective* c);
    virtual ~SrsGb28181Listener();
};

class SrsGb28181TcpListener : virtual public SrsListener, virtual public ISrsTcpHandler
{
private:
	SrsTcpListener* listener;
	SrsGb28181Caster* caster;
public:
	SrsGb28181TcpListener(SrsServer* svr, SrsListenerType t, SrsConfDirective* c);
	virtual ~SrsGb28181TcpListener();
public:
	virtual srs_error_t listen(std::string i, int p);
// Interface ISrsTcpHandler
public:
	virtual srs_error_t on_tcp_client(srs_netfd_t stfd);
};

#endif

// Convert signal to io,
// @see: st-1.9/docs/notes.html
class SrsSignalManager : public ISrsCoroutineHandler
{
private:
    // Per-process pipe which is used as a signal queue.
    // Up to PIPE_BUF/sizeof(int) signals can be queued up.
    int sig_pipe[2];
    srs_netfd_t signal_read_stfd;
private:
    SrsServer* server;
    SrsCoroutine* trd;
public:
    SrsSignalManager(SrsServer* s);
    virtual ~SrsSignalManager();
public:
    virtual srs_error_t initialize();
    virtual srs_error_t start();
// Interface ISrsEndlessThreadHandler.
public:
    virtual srs_error_t cycle();
private:
    // Global singleton instance
    static SrsSignalManager* instance;
    // Signal catching function.
    // Converts signal event to I/O event.
    static void sig_catcher(int signo);
};

// Auto reload by inotify.
// @see https://github.com/ossrs/srs/issues/1635
class SrsInotifyWorker : public ISrsCoroutineHandler
{
private:
    SrsServer* server;
    SrsCoroutine* trd;
    srs_netfd_t inotify_fd;
public:
    SrsInotifyWorker(SrsServer* s);
    virtual ~SrsInotifyWorker();
public:
    virtual srs_error_t start();
// Interface ISrsEndlessThreadHandler.
public:
    virtual srs_error_t cycle();
};

// A handler to the handle cycle in SRS RTMP server.
class ISrsServerCycle
{
public:
    ISrsServerCycle();
    virtual ~ISrsServerCycle();
public:
    // Initialize the cycle handler.
    virtual srs_error_t initialize() = 0;
    // Do on_cycle while server doing cycle.
    virtual srs_error_t on_cycle() = 0;
    // Callback the handler when got client.
    virtual srs_error_t on_accept_client(int max, int cur) = 0;
    // Callback for logrotate.
    virtual void on_logrotate() = 0;
};

// TODO: FIXME: Rename to SrsLiveServer.
// SRS RTMP server, initialize and listen, start connection service thread, destroy client.
class SrsServer : virtual public ISrsReloadHandler, virtual public ISrsSourceHandler
    , virtual public ISrsResourceManager, virtual public ISrsCoroutineHandler
    , virtual public ISrsHourGlass, public ISrsTcpMuxHandler
{
private:
    SrsHttpServer* http_server;
    SrsHttpHeartbeat* http_heartbeat;
    SrsIngester* ingester;
    SrsResourceManager* conn_manager;
    SrsCoroutine* trd_;
    SrsHourGlass* timer_;
private:
    // The pid file fd, lock the file write when server is running.
    // @remark the init.d script should cleanup the pid file, when stop service,
    //       for the server never delete the file; when system startup, the pid in pid file
    //       maybe valid but the process is not SRS, the init.d script will never start server.
    int pid_fd;
    // All listners, listener manager.
    std::vector<SrsListener*> listeners;
    // Signal manager which convert gignal to io message.
    SrsSignalManager* signal_manager;
    // Handle in server cycle.
    ISrsServerCycle* handler;
    // User send the signal, convert to variable.
    bool signal_reload;
    bool signal_persistence_config;
    bool signal_gmc_stop;
    bool signal_fast_quit;
    bool signal_gracefully_quit;
    // Parent pid for asprocess.
    int ppid;
public:
    SrsServer();
    virtual ~SrsServer();
private:
    // The destroy is for gmc to analysis the memory leak,
    // if not destroy global/static data, the gmc will warning memory leak.
    // In service, server never destroy, directly exit when restart.
    virtual void destroy();
    // When SIGTERM, SRS should do cleanup, for example,
    // to stop all ingesters, cleanup HLS and dvr.
    virtual void dispose();
    // Close listener to stop accepting new connections,
    // then wait and quit when all connections finished.
    virtual void gracefully_dispose();
// server startup workflow, @see run_master()
public:
    // Initialize server with callback handler ch.
    // @remark user must free the handler.
    virtual srs_error_t initialize(ISrsServerCycle* ch);
    virtual srs_error_t initialize_st();
    virtual srs_error_t initialize_signal();
    virtual srs_error_t acquire_pid_file();
    virtual srs_error_t listen();
    virtual srs_error_t register_signal();
    virtual srs_error_t ingest();
    virtual srs_error_t start();
// interface ISrsCoroutineHandler
public:
    virtual srs_error_t cycle();
// server utilities.
public:
    // The callback for signal manager got a signal.
    // The signal manager convert signal to io message,
    // whatever, we will got the signo like the orignal signal(int signo) handler.
    // @param signo the signal number from user, where:
    //      SRS_SIGNAL_FAST_QUIT, the SIGTERM, do essential dispose then quit.
    //      SRS_SIGNAL_GRACEFULLY_QUIT, the SIGQUIT, do careful dispose then quit.
    //      SRS_SIGNAL_REOPEN_LOG, the SIGUSR1, reopen the log file.
    //      SRS_SIGNAL_RELOAD, the SIGHUP, reload the config.
    //      SRS_SIGNAL_PERSISTENCE_CONFIG, application level signal, persistence config to file.
    // @remark, for SIGINT:
    //       no gmc, fast quit, do essential dispose then quit.
    //       for gmc, set the variable signal_gmc_stop, the cycle will return and cleanup for gmc.
    // @remark, maybe the HTTP RAW API will trigger the on_signal() also.
    virtual void on_signal(int signo);
private:
    // The server thread main cycle,
    // update the global static data, for instance, the current time,
    // the cpu/mem/network statistic.
    virtual srs_error_t do_cycle();
// interface ISrsHourGlass
private:
    virtual srs_error_t setup_ticks();
    virtual srs_error_t notify(int event, srs_utime_t interval, srs_utime_t tick);
private:
    // listen at specified protocol.
    virtual srs_error_t listen_rtmp();
    virtual srs_error_t listen_http_stream();
    virtual srs_error_t listen_https_stream();
    virtual srs_error_t listen_stream_caster();
#ifdef SRS_GB28181
    virtual srs_error_t listen_gb28181_sip(SrsConfDirective* c);
#endif
    // Close the listeners for specified type,
    // Remove the listen object from manager.
    virtual void close_listeners(SrsListenerType type);
    // Resample the server kbs.
    virtual void resample_kbps();
private:
    virtual srs_error_t accept_tcp_client(SrsListenerType type, srs_netfd_t stfd);
    virtual srs_error_t fd_to_resource(SrsListenerType type, srs_netfd_t stfd, ISrsStartableConneciton** pr);
// Interface ISrsResourceManager
private:
    // A callback for connection to remove itself.
    // When connection thread cycle terminated, callback this to delete connection.
    // @see SrsTcpConnection.on_thread_stop().
    virtual void remove(ISrsResource* c);
// Interface ISrsReloadHandler.
public:
    virtual srs_error_t on_reload_listen();
    virtual srs_error_t on_reload_pid();
    virtual srs_error_t on_reload_vhost_added(std::string vhost);
    virtual srs_error_t on_reload_vhost_removed(std::string vhost);
    virtual srs_error_t on_reload_http_api_enabled();
    virtual srs_error_t on_reload_http_api_disabled();
    virtual srs_error_t on_reload_http_stream_enabled();
    virtual srs_error_t on_reload_http_stream_disabled();
    virtual srs_error_t on_reload_http_stream_updated();
// Interface ISrsSourceHandler
public:
    virtual srs_error_t on_publish(SrsSource* s, SrsRequest* r);
    virtual void on_unpublish(SrsSource* s, SrsRequest* r);
};

// The SRS server adapter, the master server.
class SrsServerAdapter : public ISrsHybridServer
{
private:
    SrsServer* srs;
public:
    SrsServerAdapter();
    virtual ~SrsServerAdapter();
public:
    virtual srs_error_t initialize();
    virtual srs_error_t run();
    virtual void stop();
public:
    virtual SrsServer* instance();
};

// The HTTP API server.
class SrsApiServer : public ISrsTcpMuxHandler, public ISrsResourceManager, public ISrsRtcServer
{
private:
    SrsBufferListener* http_;
    SrsBufferListener* https_;
    SrsHttpServeMux* http_api_mux_;
    SrsResourceManager* conn_manager_;
public:
    SrsApiServer();
    virtual ~SrsApiServer();
public:
    virtual srs_error_t initialize();
private:
    virtual srs_error_t listen_http_api();
    virtual srs_error_t listen_https_api();
private:
    virtual srs_error_t accept_tcp_client(SrsListenerType type, srs_netfd_t stfd);
    virtual srs_error_t fd_to_resource(SrsListenerType type, srs_netfd_t stfd, ISrsStartableConneciton** pr);
    virtual void remove(ISrsResource* c);
private:
    virtual srs_error_t http_handle();
    srs_error_t listen_api();
private:
    virtual srs_error_t create_session(
        SrsRequest* req, const SrsSdp& remote_sdp, SrsSdp& local_sdp, const std::string& mock_eip,
        bool publish, bool dtls, bool srtp,
        SrsRtcConnection** psession
    );
public:
    static srs_error_t start(void* arg);
private:
    srs_error_t do_start();
};

#endif

