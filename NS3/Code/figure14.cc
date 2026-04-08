/* sixth_fig14.cc
 *
 * Reproduces Figure 14 from the paper:
 *   "Improvement of throughput versus RTT"
 *   at four reserved bandwidths: 10, 20, 30, 40 Mbps
 *
 * Improvement rate = ((BR-TCP - NewReno) / NewReno) * 100 %
 *
 * DESIGN (same fixes as sixth_fig13.cc):
 *   - BulkSendApplication (not TutorialApp) — respects TCP cwnd
 *   - Buffer = 2 * BDP per (bandwidth, RTT) point
 *   - BR-TCP ssthresh override via NewConnectionCreated trace
 *   - Extra data passed via global RunCtx struct (avoids MakeBoundCallback
 *     type mismatch with NewConnectionCreated signature)
 *
 * SWEEP:
 *   Bandwidths : 10, 20, 30, 40 Mbps
 *   RTT values : 10, 20, 30, 40, 50, 60 ms
 *   Variants   : New Reno, BR-TCP
 *   Total runs : 4 * 6 * 2 = 48 simulations
 *
 * BR-TCP ssthresh formula:
 *   ssthresh = (r / (size*8)) * RTT
 *
 * Output: improvement_vs_rtt.tr
 *   Columns: rtt_ms  bw_mbps  newreno_mbps  brtcp_mbps  improvement_pct
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

NS_LOG_COMPONENT_DEFINE("Fig14ImprovementVsRTT");

// ---------------------------------------------------------------
// Constants
// ---------------------------------------------------------------
static const double PKT_BYTES  = 1500.0;
static const double SIM_SEC    = 120.0;
static const double WARMUP_SEC =  20.0;
static const double SAMPLE_SEC =   1.0;
static const double T_START    =   1.0;

// ---------------------------------------------------------------
// BR-TCP ssthresh formula
//   ssthresh = (r / (size*8)) * RTT
// ---------------------------------------------------------------
static uint32_t BRssthresh(double linkBps, double rttSec)
{
    double pkts = (linkBps / (PKT_BYTES * 8.0)) * rttSec;
    pkts = std::max(pkts, 2.0);
    return (uint32_t)(std::round(pkts) * PKT_BYTES);
}

// ---------------------------------------------------------------
// Global run context — avoids MakeBoundCallback type issues with
// NewConnectionCreated trace signature: void(Ptr<Socket>, const Address&)
// ---------------------------------------------------------------
struct RunCtx
{
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
// BR-TCP ssthresh override callback
// Fires on every SlowStartThreshold change — replaces Reno's
// cwnd/2 with the bandwidth-formula value.
// ---------------------------------------------------------------
static void OnSsthreshChange(uint32_t brtcpSS,
                              uint32_t /*oldSS*/,
                              uint32_t  newSS)
{
    if (newSS == brtcpSS) return;

    Simulator::ScheduleNow([brtcpSS]()
    {
        Config::Set(
            "/NodeList/0/$ns3::TcpL4Protocol/SocketList/*/"
            "InitialSlowStartThreshold",
            UintegerValue(brtcpSS));
        Config::Set(
            "/NodeList/0/$ns3::TcpL4Protocol/SocketList/*/SsThresh",
            UintegerValue(brtcpSS));
    });
}

// ---------------------------------------------------------------
// NewConnectionCreated callback
// Signature MUST be: void(Ptr<Socket>, const Address&)
// ---------------------------------------------------------------
static void OnNewConn(Ptr<Socket> sock, const Address& /*addr*/)
{
    if (!g_ctx.useBRTCP) return;

    Ptr<TcpSocketBase> tcpSock = DynamicCast<TcpSocketBase>(sock);
    if (!tcpSock) return;

    uint32_t ss = g_ctx.brtcpSS;

    tcpSock->TraceConnectWithoutContext(
        "SlowStartThreshold",
        MakeBoundCallback(&OnSsthreshChange, ss));

    Simulator::ScheduleNow([tcpSock, ss]()
    {
        tcpSock->SetAttribute("InitialSlowStartThreshold",
                              UintegerValue(ss));
    });
}

// ---------------------------------------------------------------
// Throughput sampler
// ---------------------------------------------------------------
static void Sample(Ptr<PacketSink> sink)
{
    uint64_t rx   = sink->GetTotalRx();
    uint64_t diff = rx - g_lastRx;
    g_lastRx      = rx;

    double now = Simulator::Now().GetSeconds();
    if (now >= T_START + WARMUP_SEC + SAMPLE_SEC)
    {
        g_samples.push_back((diff * 8.0) / (SAMPLE_SEC * 1e6));
    }

    if (now + SAMPLE_SEC < T_START + SIM_SEC)
        Simulator::Schedule(Seconds(SAMPLE_SEC), &Sample, sink);
}

// ---------------------------------------------------------------
// Run one simulation
//   linkMbps : reserved bandwidth [Mbps]
//   delayMs  : one-way delay [ms]   (RTT = 2 * delayMs)
//   useBRTCP : true = BR-TCP, false = New Reno
// Returns average throughput [Mbps]
// ---------------------------------------------------------------
static double RunOne(double linkMbps, double delayMs, bool useBRTCP)
{
    g_lastRx = 0;
    g_samples.clear();

    double   linkBps = linkMbps * 1e6;
    double   rttSec  = 2.0 * delayMs / 1000.0;
    uint32_t bdp     = (uint32_t)std::ceil(linkBps * rttSec / 8.0);
    uint32_t sockBuf = std::max(bdp * 2, (uint32_t)32000);
    uint32_t initSS  = useBRTCP
                       ? BRssthresh(linkBps, rttSec)
                       : (uint32_t)0xFFFFFFFF;

    // Store context for NewConnectionCreated callback
    g_ctx.useBRTCP = useBRTCP;
    g_ctx.brtcpSS  = initSS;

    // ---- TCP global defaults ----
    Config::SetDefault("ns3::TcpL4Protocol::SocketType",
                       TypeIdValue(TcpNewReno::GetTypeId()));
    Config::SetDefault("ns3::TcpSocket::SegmentSize",
                       UintegerValue((uint32_t)PKT_BYTES));
    Config::SetDefault("ns3::TcpSocket::InitialCwnd",
                       UintegerValue(1));
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
    {
        std::ostringstream bw;
        bw << std::fixed << std::setprecision(1) << linkMbps << "Mbps";
        p2p.SetDeviceAttribute("DataRate", StringValue(bw.str()));
    }
    {
        std::ostringstream dl;
        dl << std::fixed << std::setprecision(4) << delayMs << "ms";
        p2p.SetChannelAttribute("Delay", StringValue(dl.str()));
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

    // ---- BulkSend ----
    BulkSendHelper bulkH(
        "ns3::TcpSocketFactory",
        InetSocketAddress(ifaces.GetAddress(1), port));
    bulkH.SetAttribute("MaxBytes", UintegerValue(0));
    bulkH.SetAttribute("SendSize", UintegerValue((uint32_t)PKT_BYTES));

    ApplicationContainer senderApp = bulkH.Install(nodes.Get(0));
    senderApp.Start(Seconds(T_START));
    senderApp.Stop(Seconds(T_START + SIM_SEC));

    // ---- BR-TCP hook ----
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
// Main — bandwidth × RTT sweep
// ---------------------------------------------------------------
int main(int argc, char *argv[])
{
    CommandLine cmd;
    cmd.Parse(argc, argv);

    // Four bandwidth curves (paper Figure 14)
    std::vector<double> bwMbps = {10.0, 20.0, 30.0, 40.0};

    // RTT sweep points
    std::vector<double> rttMs  = {10.0, 20.0, 30.0, 40.0, 50.0, 60.0};

    AsciiTraceHelper ascii;
    Ptr<OutputStreamWrapper> out =
        ascii.CreateFileStream("figure14_improvement_vs_rtt.tr");

    *out->GetStream()
        << "# rtt_ms\tbw_mbps\tnewreno_mbps\tbrtcp_mbps\timprovement_pct\n";

    for (double bw : bwMbps)
    {
        std::cout << "\n============================\n"
                  << "  Bandwidth = " << bw << " Mbps\n"
                  << "============================\n";

        for (double rtt : rttMs)
        {
            double   rttSec = rtt / 1000.0;
            uint32_t bdp    = (uint32_t)(bw * 1e6 * rttSec / 8.0);
            uint32_t buf    = std::max(bdp * 2, (uint32_t)32000);
            double   ss     = (bw * 1e6 / (PKT_BYTES * 8.0)) * rttSec;

            std::cout << "  RTT=" << rtt << "ms"
                      << "  BDP=" << bdp << "B"
                      << "  buf=" << buf << "B"
                      << "  ssthresh=" << ss << " pkts"
                      << std::endl;

            double nr  = RunOne(bw, rtt / 2.0, false);
            double br  = RunOne(bw, rtt / 2.0, true);
            double imp = (nr > 0.0) ? ((br - nr) / nr * 100.0) : 0.0;

            std::cout << "    NR=" << nr << " Mbps"
                      << "  BR=" << br << " Mbps"
                      << "  Improvement=" << imp << "%"
                      << std::endl;

            *out->GetStream()
                << rtt  << "\t"
                << bw   << "\t"
                << nr   << "\t"
                << br   << "\t"
                << imp  << "\n";
        }
    }

    std::cout << "\nAll 48 runs done. Results in figure14_improvement_vs_rtt.tr\n";
    return 0;
}