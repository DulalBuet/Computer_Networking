
#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/olsr-module.h"
#include "ns3/tcp-congestion-ops.h"
#include "ns3/wifi-module.h"

#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("WifiStaticTraces");


class TcpBr : public TcpNewReno
{
public:
    static TypeId GetTypeId()
    {
        static TypeId tid =
            TypeId("ns3::TcpBr")
                .SetParent<TcpNewReno>()
                .SetGroupName("Internet")
                .AddConstructor<TcpBr>()
                .AddAttribute(
                    "ReservedBandwidth",
                    "Reserved bandwidth r [bit/s] for ssthresh = r*RTT/8",
                    DataRateValue(DataRate("10Mbps")),
                    MakeDataRateAccessor(&TcpBr::m_reservedBw),
                    MakeDataRateChecker());
        return tid;
    }

    TcpBr() : TcpNewReno(), m_reservedBw("10Mbps") {}
    TcpBr(const TcpBr& s) : TcpNewReno(s), m_reservedBw(s.m_reservedBw) {}
    ~TcpBr() override = default;

    std::string GetName() const override { return "TcpBr"; }

    uint32_t GetSsThresh(Ptr<const TcpSocketState> tcb,
                         uint32_t bytesInFlight) override
    {
       
        double rtt = tcb->m_lastRtt.Get().GetSeconds();
        double r   = static_cast<double>(m_reservedBw.GetBitRate());
        uint32_t seg = tcb->m_segmentSize;

        if (rtt > 0.0 && r > 0.0 && seg > 0)
        {
            // BR-TCP formula: ssthresh = (r / 8) * RTT  [bytes]
            uint32_t ss = static_cast<uint32_t>((r / 8.0) * rtt);
            return std::max(ss, 2 * seg);
        }
        // Fallback to New Reno if RTT not yet measured
        return TcpNewReno::GetSsThresh(tcb, bytesInFlight);
    }

    Ptr<TcpCongestionOps> Fork() override
    {
        return CopyObject<TcpBr>(this);
    }

private:
    DataRate m_reservedBw;
};

NS_OBJECT_ENSURE_REGISTERED(TcpBr);

/* =====================================================================
 * Trace Callbacks
 * ===================================================================== */

static void CwndChange(Ptr<OutputStreamWrapper> stream,
                       uint32_t /*oldCwnd*/, uint32_t newCwnd)
{
    *stream->GetStream()
        << Simulator::Now().GetSeconds()
        << "\t" << newCwnd / 1472.0   // segment size 1472
        << "\n";
}

static void SsthreshChange(Ptr<OutputStreamWrapper> stream,
                           uint32_t /*oldVal*/, uint32_t newVal)
{
    // 0xFFFFFFFF = initial unlimited ssthresh — skip করা
    if (newVal == 0xFFFFFFFF || newVal > 100000 * 1472) return;

    *stream->GetStream()
        << Simulator::Now().GetSeconds()
        << "\t" << newVal / 1472.0
        << "\n";
}

static void RunOneConfig(uint32_t nNodes,
                         uint32_t nFlows,
                         uint32_t pktPerSec,
                         double   txRange,
                         double   simTime)
{
    std::cout << "\n>>> Running: " << nNodes << " nodes, "
              << nFlows << " flows, "
              << pktPerSec << " pkt/s <<<" << std::endl;

    const uint32_t pktSize    = 1472;   // bytes (WiFi-friendly)
    const double   startTime  = 5.0;    // OLSR convergence time
    const double   areaSide   = txRange; // 1× coverage

    // ----------------------------------------------------------------
    // Nodes
    // ----------------------------------------------------------------
    NodeContainer nodes;
    nodes.Create(nNodes);

    // ----------------------------------------------------------------
    // WiFi — 802.11g, Ad-hoc mode
    // ----------------------------------------------------------------
    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211g);
    wifi.SetRemoteStationManager(
        "ns3::ConstantRateWifiManager",
        "DataMode",    StringValue("ErpOfdmRate54Mbps"),
        "ControlMode", StringValue("ErpOfdmRate6Mbps"));

    YansWifiChannelHelper channel = YansWifiChannelHelper::Default();
    channel.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
    channel.AddPropagationLoss(
        "ns3::RangePropagationLossModel",
        "MaxRange", DoubleValue(txRange));

    YansWifiPhyHelper phy;
    phy.SetChannel(channel.Create());

    WifiMacHelper mac;
    mac.SetType("ns3::AdhocWifiMac");

    NetDeviceContainer devices = wifi.Install(phy, mac, nodes);

    // ----------------------------------------------------------------
    // Mobility — random static positions within areaSide × areaSide
    // ----------------------------------------------------------------
    MobilityHelper mobility;
    mobility.SetPositionAllocator(
        "ns3::RandomRectanglePositionAllocator",
        "X", StringValue("ns3::UniformRandomVariable[Min=0.0|Max="
                          + std::to_string(areaSide) + "]"),
        "Y", StringValue("ns3::UniformRandomVariable[Min=0.0|Max="
                          + std::to_string(areaSide) + "]"));
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(nodes);

    // ----------------------------------------------------------------
    // Internet stack + OLSR routing
    // ----------------------------------------------------------------
    OlsrHelper olsr;
    Ipv4ListRoutingHelper list;
    list.Add(olsr, 10);

    InternetStackHelper internet;
    internet.SetRoutingHelper(list);
    internet.Install(nodes);

    Ipv4AddressHelper addr;
    addr.SetBase("10.0.0.0", "255.255.0.0");
    Ipv4InterfaceContainer ifaces = addr.Assign(devices);

    // ----------------------------------------------------------------
    // TCP Applications (OnOff → PacketSink)
    // ----------------------------------------------------------------
    const std::string dataRate =
        std::to_string(pktSize * 8 * pktPerSec) + "bps";

    ApplicationContainer sinkApps;
    std::vector<Ptr<Socket>> traceSockets;

    for (uint32_t i = 0; i < nFlows; i++)
    {
        uint32_t src = i % nNodes;
        uint32_t dst = (i + nNodes / 2) % nNodes;
        if (src == dst) dst = (dst + 1) % nNodes;
        uint16_t port = 9000 + i;

        // Sink
        PacketSinkHelper sink(
            "ns3::TcpSocketFactory",
            InetSocketAddress(Ipv4Address::GetAny(), port));
        sinkApps.Add(sink.Install(nodes.Get(dst)));

        // Source
        OnOffHelper onoff(
            "ns3::TcpSocketFactory",
            InetSocketAddress(ifaces.GetAddress(dst), port));
        onoff.SetAttribute("DataRate",   StringValue(dataRate));
        onoff.SetAttribute("PacketSize", UintegerValue(pktSize));
        onoff.SetAttribute("OnTime",
            StringValue("ns3::ConstantRandomVariable[Constant=1]"));
        onoff.SetAttribute("OffTime",
            StringValue("ns3::ConstantRandomVariable[Constant=0]"));

        ApplicationContainer srcApp = onoff.Install(nodes.Get(src));
        srcApp.Start(Seconds(startTime));
        srcApp.Stop(Seconds(simTime - 1.0));
    }

    sinkApps.Start(Seconds(0.0));
    sinkApps.Stop(Seconds(simTime));

    std::string tag = "wifi_n" + std::to_string(nNodes);

    AsciiTraceHelper ascii;
    Ptr<OutputStreamWrapper> cwndStream =
        ascii.CreateFileStream(tag + "_cwnd.tr");
    Ptr<OutputStreamWrapper> ssStream =
        ascii.CreateFileStream(tag + "_ssthresh.tr");

    Simulator::Schedule(
        Seconds(startTime + 0.1),
        [&nodes, cwndStream, ssStream, nNodes]()
        {
            for (uint32_t i = 0; i < nNodes; i++)
            {
                Ptr<Node> node = nodes.Get(i);
                Ptr<TcpL4Protocol> tcp = node->GetObject<TcpL4Protocol>();
                if (!tcp) continue;

                ObjectVectorValue sockets;
                tcp->GetAttribute("SocketList", sockets);

                for (std::size_t s = 0; s < sockets.GetN(); s++)
                {
                    Ptr<Object> sockObj = sockets.Get(s);
                    Ptr<TcpSocketBase> tcpSock =
                        DynamicCast<TcpSocketBase>(sockObj);
                    if (!tcpSock) continue;

                    tcpSock->TraceConnectWithoutContext(
                        "CongestionWindow",
                        MakeBoundCallback(&CwndChange, cwndStream));
                    tcpSock->TraceConnectWithoutContext(
                        "SlowStartThreshold",
                        MakeBoundCallback(&SsthreshChange, ssStream));

                    break; 
                }
            }
        });

    // ----------------------------------------------------------------
    // Run
    // ----------------------------------------------------------------
    Simulator::Stop(Seconds(simTime));
    Simulator::Run();
    Simulator::Destroy();

    std::cout << "    Saved: " << tag << "_cwnd.tr" << std::endl;
    std::cout << "    Saved: " << tag << "_ssthresh.tr" << std::endl;
}

/* =====================================================================
 * Main
 * ===================================================================== */
int main(int argc, char* argv[])
{
    // ----------------------------------------------------------------
    // Global TCP settings
    // ----------------------------------------------------------------
    Config::SetDefault("ns3::TcpL4Protocol::SocketType",
                       StringValue("ns3::TcpBr"));
    Config::SetDefault("ns3::TcpBr::ReservedBandwidth",
                       DataRateValue(DataRate("10Mbps")));
    Config::SetDefault("ns3::TcpSocket::SegmentSize",
                       UintegerValue(1472));
    Config::SetDefault("ns3::TcpSocket::SndBufSize",
                       UintegerValue(1 << 20));
    Config::SetDefault("ns3::TcpSocket::RcvBufSize",
                       UintegerValue(1 << 20));
    Config::SetDefault("ns3::TcpSocket::InitialCwnd",
                       UintegerValue(1));

    CommandLine cmd(__FILE__);
    cmd.Parse(argc, argv);

    // ----------------------------------------------------------------
    // Fixed parameters for Figure 4 style plot
    // ----------------------------------------------------------------
    const double   txRange   = 100.0;  // meters
    const double   simTime   = 50.0;   // seconds
    const uint32_t nFlows    = 10;     // flows
    const uint32_t pktPerSec = 100;    // packets per second

    // 5 node configurations → 10 .tr files (cwnd + ssthresh each)
    uint32_t nodeConfigs[] = {20, 40, 60, 80, 100};

    std::cout << "==================================================" << std::endl;
    std::cout << " WiFi Static BR-TCP — Generating 10 trace files" << std::endl;
    std::cout << "==================================================" << std::endl;
    std::cout << " Flows    : " << nFlows    << std::endl;
    std::cout << " Pkt/sec  : " << pktPerSec << std::endl;
    std::cout << " TxRange  : " << txRange   << " m" << std::endl;
    std::cout << " SimTime  : " << simTime   << " s" << std::endl;
    std::cout << "==================================================" << std::endl;

    for (uint32_t nNodes : nodeConfigs)
    {
        RunOneConfig(nNodes, nFlows, pktPerSec, txRange, simTime);
    }

    std::cout << "\n=================================================" << std::endl;
    std::cout << " All done! 10 trace files generated:" << std::endl;
    for (uint32_t n : nodeConfigs)
    {
        std::cout << "   wifi_n" << n << "_cwnd.tr" << std::endl;
        std::cout << "   wifi_n" << n << "_ssthresh.tr" << std::endl;
    }
    std::cout << "=================================================" << std::endl;

    return 0;
}