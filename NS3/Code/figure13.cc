/* sixth_fig13.cc  — COMPILE-FIXED VERSION for ns-3.39
 *
 * Figure 13: Throughput of BR-TCP and New Reno vs RTT
 *
 * COMPILE FIX:
 *   TcpL4Protocol "NewConnectionCreated" trace signature is:
 *     void(Ptr<Socket>, const Address&)   -- NOT (Ptr<Socket>, Address, uint32_t)
 *   We pass the extra data (brtcpSS, rttSec) via a global struct
 *   instead of MakeBoundCallback, which avoids the type mismatch.
 *
 * DESIGN:
 *   - BulkSendApplication (not TutorialApp) — respects TCP cwnd
 *   - Buffer = 2 * BDP per RTT point — cwnd is the bottleneck
 *   - BR-TCP ssthresh override via SlowStartThreshold trace
 *   - RTT sweep: 10, 20, 30, 40, 50, 60 ms
 *   - 120 s sim, 20 s warmup discarded
 */

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/tcp-socket-base.h"
#include "ns3/tcp-l4-protocol.h"

#include <vector>
#include <numeric>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <algorithm>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("Fig13ThroughputVsRTT");

// ---------------------------------------------------------------
// Constants
// ---------------------------------------------------------------
static const double LINK_BPS   = 40e6;
static const double PKT_BYTES  = 1500.0;
static const double SIM_SEC    = 120.0;
static const double WARMUP_SEC =  20.0;
static const double SAMPLE_SEC =   1.0;
static const double T_START    =   1.0;

static uint32_t BRssthresh(double rttSec)
{
    double pkts = (LINK_BPS / (PKT_BYTES * 8.0)) * rttSec;
    pkts = std::max(pkts, 2.0);
    return (uint32_t)(std::round(pkts) * PKT_BYTES);
}

// ---------------------------------------------------------------
// Global context passed to NewConnectionCreated callback.
// We use a global because MakeBoundCallback with the correct
// (Ptr<Socket>, const Address&) signature cannot carry extra args
// without a wrapper struct in ns-3.39.
// ---------------------------------------------------------------
struct RunCtx {
    bool     useBRTCP;
    uint32_t brtcpSS;
};
static RunCtx g_ctx;

// ---------------------------------------------------------------
// Per-run throughput state
// ---------------------------------------------------------------
static uint64_t            g_lastRx = 0;
static std::vector<double> g_samples;

// ---------------------------------------------------------------
// BR-TCP ssthresh override.
// Connected to SlowStartThreshold trace of the sender socket.
// Every time Reno would set ssthresh = cwnd/2, we overwrite it.
// ---------------------------------------------------------------
static void OnSsthreshChange(uint32_t           brtcpSS,
                              uint32_t           /*oldSS*/,
                              uint32_t           newSS)
{
    if (newSS == brtcpSS) return;   // already correct, skip

    // Defer write to avoid modifying TCP state inside its own callback
    Simulator::ScheduleNow([brtcpSS]()
    {
        // Override via Config wildcard on all TCP sockets on node 0
        Config::Set(
            "/NodeList/0/$ns3::TcpL4Protocol/SocketList/*/"
            "InitialSlowStartThreshold",
            UintegerValue(brtcpSS));

        // Also directly patch ssThresh in the TcpSocketState objects
        Config::Set(
            "/NodeList/0/$ns3::TcpL4Protocol/SocketList/*/SsThresh",
            UintegerValue(brtcpSS));
    });
}

// ---------------------------------------------------------------
// Called when BulkSend opens its TCP connection.
// Signature MUST match TcpL4Protocol NewConnectionCreated trace:
//   void(Ptr<Socket>, const Address&)
// ---------------------------------------------------------------
static void OnNewConn(Ptr<Socket> sock, const Address& /*addr*/)
{
    if (!g_ctx.useBRTCP) return;

    Ptr<TcpSocketBase> tcpSock = DynamicCast<TcpSocketBase>(sock);
    if (!tcpSock) return;

    uint32_t ss = g_ctx.brtcpSS;

    // Hook the ssthresh trace on this specific socket
    tcpSock->TraceConnectWithoutContext(
        "SlowStartThreshold",
        MakeBoundCallback(&OnSsthreshChange, ss));

    // Set initial ssthresh immediately on the socket state
    Simulator::ScheduleNow([tcpSock, ss]()
    {
        tcpSock->SetAttribute("InitialSlowStartThreshold",
                              UintegerValue(ss));
    });
}

// ---------------------------------------------------------------
// Throughput sampler — records Mbps after warm-up
// ---------------------------------------------------------------
static void Sample(Ptr<PacketSink> sink)
{
    uint64_t rx   = sink->GetTotalRx();
    uint64_t diff = rx - g_lastRx;
    g_lastRx      = rx;

    double now = Simulator::Now().GetSeconds();
    if (now >= T_START + WARMUP_SEC + SAMPLE_SEC)
    {
        double mbps = (diff * 8.0) / (SAMPLE_SEC * 1e6);
        g_samples.push_back(mbps);
    }

    if (now + SAMPLE_SEC < T_START + SIM_SEC)
        Simulator::Schedule(Seconds(SAMPLE_SEC), &Sample, sink);
}

// ---------------------------------------------------------------
// Run one simulation; return average throughput [Mbps]
// ---------------------------------------------------------------
static double RunOne(double delayMs, bool useBRTCP)
{
    g_lastRx = 0;
    g_samples.clear();

    double   rttSec  = 2.0 * delayMs / 1000.0;
    uint32_t bdp     = (uint32_t)std::ceil(LINK_BPS * rttSec / 8.0);
    uint32_t sockBuf = std::max(bdp * 2, (uint32_t)32000);
    uint32_t initSS  = useBRTCP ? BRssthresh(rttSec) : (uint32_t)0xFFFFFFFF;

    // Store context for the NewConnectionCreated callback
    g_ctx.useBRTCP = useBRTCP;
    g_ctx.brtcpSS  = initSS;

    // ---- TCP global defaults ----
    Config::SetDefault("ns3::TcpL4Protocol::SocketType",
                       TypeIdValue(TcpNewReno::GetTypeId()));
    Config::SetDefault("ns3::TcpSocket::SegmentSize",
                       UintegerValue((uint32_t)PKT_BYTES));
    Config::SetDefault("ns3::TcpSocket::InitialCwnd",
                       UintegerValue(1));
    // BDP-scaled buffers — makes cwnd the bottleneck, not the buffer
    Config::SetDefault("ns3::TcpSocket::SndBufSize",
                       UintegerValue(sockBuf));
    Config::SetDefault("ns3::TcpSocket::RcvBufSize",
                       UintegerValue(sockBuf));
    Config::SetDefault("ns3::TcpSocket::InitialSlowStartThreshold",
                       UintegerValue(initSS));

    // ---- Topology ----
    NodeContainer nodes;
    nodes.Create(2);

    PointToPointHelper p2p;
    p2p.SetDeviceAttribute("DataRate", StringValue("40Mbps"));
    {
        std::ostringstream s;
        s << std::fixed << std::setprecision(4) << delayMs << "ms";
        p2p.SetChannelAttribute("Delay", StringValue(s.str()));
    }
    p2p.SetQueue("ns3::DropTailQueue", "MaxSize", StringValue("100p"));

    NetDeviceContainer devs = p2p.Install(nodes);

    InternetStackHelper stack;
    stack.Install(nodes);

    Ipv4AddressHelper ip;
    ip.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer ifaces = ip.Assign(devs);

    // ---- Sink ----
    uint16_t port = 9;
    PacketSinkHelper sinkH(
        "ns3::TcpSocketFactory",
        InetSocketAddress(Ipv4Address::GetAny(), port));
    ApplicationContainer sinkApp = sinkH.Install(nodes.Get(1));
    sinkApp.Start(Seconds(0.0));
    sinkApp.Stop(Seconds(T_START + SIM_SEC + 2.0));

    // ---- BulkSend — respects TCP cwnd (unlike TutorialApp) ----
    BulkSendHelper bulkH(
        "ns3::TcpSocketFactory",
        InetSocketAddress(ifaces.GetAddress(1), port));
    bulkH.SetAttribute("MaxBytes", UintegerValue(0));   // unlimited
    bulkH.SetAttribute("SendSize", UintegerValue((uint32_t)PKT_BYTES));

    ApplicationContainer senderApp = bulkH.Install(nodes.Get(0));
    senderApp.Start(Seconds(T_START));
    senderApp.Stop(Seconds(T_START + SIM_SEC));

    // ---- BR-TCP hook via NewConnectionCreated trace ----
    // Fires when BulkSend opens its connection, giving us the socket ptr.
    // Callback signature: void(Ptr<Socket>, const Address&)
    if (useBRTCP)
    {
        Ptr<TcpL4Protocol> tcp =
            nodes.Get(0)->GetObject<TcpL4Protocol>();
        tcp->TraceConnectWithoutContext(
            "NewConnectionCreated",
            MakeCallback(&OnNewConn));
    }

    // ---- Throughput sampler ----
    Ptr<PacketSink> sink = DynamicCast<PacketSink>(sinkApp.Get(0));
    Simulator::Schedule(
        Seconds(T_START + SAMPLE_SEC), &Sample, sink);

    // ---- Run ----
    Simulator::Stop(Seconds(T_START + SIM_SEC + 0.5));
    Simulator::Run();
    Simulator::Destroy();

    if (g_samples.empty()) return 0.0;
    double sum = std::accumulate(g_samples.begin(), g_samples.end(), 0.0);
    return sum / (double)g_samples.size();
}

// ---------------------------------------------------------------
// Main — RTT sweep
// ---------------------------------------------------------------
int main(int argc, char *argv[])
{
    CommandLine cmd;
    cmd.Parse(argc, argv);

    std::vector<double> rttMs = {10.0, 20.0, 30.0, 40.0, 50.0, 60.0};

    AsciiTraceHelper ascii;
    Ptr<OutputStreamWrapper> out =
        ascii.CreateFileStream("figure13_throughput_vs_rtt.tr");
    *out->GetStream() << "# rtt_ms\tnewreno_mbps\tbrtcp_mbps\n";

    for (double rtt : rttMs)
    {
        double   rttSec = rtt / 1000.0;
        uint32_t bdp    = (uint32_t)(LINK_BPS * rttSec / 8.0);
        uint32_t buf    = std::max(bdp * 2, (uint32_t)32000);
        double   ss     = (LINK_BPS / (PKT_BYTES * 8.0)) * rttSec;

        std::cout << "\n=== RTT=" << rtt << "ms"
                  << "  BDP=" << bdp << "B"
                  << "  buf=" << buf << "B"
                  << "  BR-ssthresh=" << ss << " pkts ==="
                  << std::endl;

        double nr = RunOne(rtt / 2.0, false);
        double br = RunOne(rtt / 2.0, true);

        std::cout << "  New Reno : " << nr << " Mbps\n"
                  << "  BR-TCP   : " << br << " Mbps"
                  << std::endl;

        *out->GetStream()
            << rtt << "\t" << nr << "\t" << br << "\n";
    }

    std::cout << "\nDone. Results in figure13_throughput_vs_rtt.tr\n";
    return 0;
}