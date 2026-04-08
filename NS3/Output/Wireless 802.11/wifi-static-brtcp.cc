/* =============================================================
 * wifi-static-brtcp-multirun.cc
 * -------------------------------------------------------------
 * NS-3 BR-TCP simulation for multiple configurations
 *
 *  Nodes: 20,40,60,80,100
 *  Flows: 10,20,30,40,50
 *  Packets/sec: 100,200,300,400,500
 *  Coverage: 1×Tx → 5×Tx
 *
 * Output: per-flow CSV per configuration
 * =============================================================*/

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
#include <string>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("WifiStaticBrTcpMulti");

/* --------------------- BR-TCP congestion control ---------------------- */
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
                .AddAttribute("ReservedBandwidth",
                              "Reserved bandwidth r [bit/s] for ssthresh formula.",
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
        double r = static_cast<double>(m_reservedBw.GetBitRate());
        uint32_t seg = tcb->m_segmentSize;
        if (rtt > 0.0 && r > 0.0 && seg > 0)
        {
            uint32_t ss = static_cast<uint32_t>((r / 8.0) * rtt);
            return std::max(ss, 2 * seg);
        }
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

/* ---------------------- Main simulation function ---------------------- */
int main(int argc, char* argv[])
{
    CommandLine cmd(__FILE__);
    cmd.Parse(argc, argv);

    // Fixed parameters
    const double txRange = 100.0;
    const double simTime = 50.0;
    const double startTime = 5.0;
    const uint32_t pktSize = 1024;

    // Arrays of parameters
    uint32_t nodesArr[] = {20, 40, 60, 80, 100};
    uint32_t flowsArr[] = {10, 20, 30, 40, 50};
    uint32_t pktPerSecArr[] = {100, 200, 300, 400, 500};
    uint32_t covMultArr[] = {1, 2, 3, 4, 5};

    // Set BR-TCP globally
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

    // Nested loops
    for (uint32_t nNodes : nodesArr)
    {
        for (uint32_t nFlows : flowsArr)
        {
            for (uint32_t pktPerSec : pktPerSecArr)
            {
                for (uint32_t covMult : covMultArr)
                {
                    double areaSide = covMult * txRange;
                    std::cout << "\n=== Running: "
                              << nNodes << " nodes, "
                              << nFlows << " flows, "
                              << pktPerSec << " pkt/s, "
                              << covMult << "Tx coverage ===\n";

                    // ------------------- Nodes -------------------
                    NodeContainer nodes;
                    nodes.Create(nNodes);

                    // ------------------- WiFi -------------------
                    WifiHelper wifi;
                    wifi.SetStandard(WIFI_STANDARD_80211g);
                    wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                                                 "DataMode",
                                                 StringValue("ErpOfdmRate54Mbps"),
                                                 "ControlMode",
                                                 StringValue("ErpOfdmRate6Mbps"));

                    YansWifiChannelHelper channel = YansWifiChannelHelper::Default();
                    channel.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
                    channel.AddPropagationLoss("ns3::RangePropagationLossModel",
                                               "MaxRange",
                                               DoubleValue(txRange));

                    YansWifiPhyHelper phy;
                    phy.SetChannel(channel.Create());

                    WifiMacHelper mac;
                    mac.SetType("ns3::AdhocWifiMac");

                    NetDeviceContainer devices = wifi.Install(phy, mac, nodes);

                    // ------------------- Mobility -------------------
                    MobilityHelper mobility;
                    mobility.SetPositionAllocator(
                        "ns3::RandomRectanglePositionAllocator",
                        "X",
                        StringValue("ns3::UniformRandomVariable[Min=0.0|Max=" +
                                    std::to_string(areaSide) + "]"),
                        "Y",
                        StringValue("ns3::UniformRandomVariable[Min=0.0|Max=" +
                                    std::to_string(areaSide) + "]"));
                    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
                    mobility.Install(nodes);

                    // ------------------- Internet + OLSR -------------------
                    OlsrHelper olsr;
                    Ipv4ListRoutingHelper list;
                    list.Add(olsr, 10);
                    InternetStackHelper internet;
                    internet.SetRoutingHelper(list);
                    internet.Install(nodes);

                    Ipv4AddressHelper addr;
                    addr.SetBase("10.0.0.0", "255.255.0.0");
                    Ipv4InterfaceContainer ifaces = addr.Assign(devices);

                    // ------------------- Applications -------------------
                    ApplicationContainer sinkApps, srcApps;
                    const std::string dataRate =
                        std::to_string(pktSize * 8 * pktPerSec) + "bps";

                    for (uint32_t i = 0; i < nFlows; i++)
                    {
                        uint32_t src = i % nNodes;
                        uint32_t dst = (i + nNodes / 2) % nNodes;
                        if (src == dst) dst = (dst + 1) % nNodes;
                        uint16_t port = 9000 + i;

                        PacketSinkHelper sink("ns3::TcpSocketFactory",
                                              InetSocketAddress(Ipv4Address::GetAny(), port));
                        sinkApps.Add(sink.Install(nodes.Get(dst)));

                        OnOffHelper onoff("ns3::TcpSocketFactory",
                                          InetSocketAddress(ifaces.GetAddress(dst), port));
                        onoff.SetAttribute("DataRate", StringValue(dataRate));
                        onoff.SetAttribute("PacketSize", UintegerValue(pktSize));
                        onoff.SetAttribute("OnTime",
                                          StringValue("ns3::ConstantRandomVariable[Constant=1]"));
                        onoff.SetAttribute("OffTime",
                                          StringValue("ns3::ConstantRandomVariable[Constant=0]"));
                        srcApps.Add(onoff.Install(nodes.Get(src)));
                    }

                    sinkApps.Start(Seconds(0.0));
                    sinkApps.Stop(Seconds(simTime));
                    srcApps.Start(Seconds(startTime));
                    srcApps.Stop(Seconds(simTime - 1.0));

                    // ------------------- Flow monitor -------------------
                    FlowMonitorHelper fmHelper;
                    Ptr<FlowMonitor> flowMon = fmHelper.InstallAll();

                    Simulator::Stop(Seconds(simTime));
                    Simulator::Run();

                    // ------------------- Collect metrics -------------------
                    auto classifier = DynamicCast<Ipv4FlowClassifier>(fmHelper.GetClassifier());
                    auto stats = flowMon->GetFlowStats();
                    double duration = simTime - 1.0 - startTime;

                    // Dynamic CSV filename
                    std::string csvName = "node" + std::to_string(nNodes) +
                                          "_flow_" + std::to_string(nFlows) +
                                          "_packet_" + std::to_string(pktPerSec) +
                                          "_" + std::to_string(covMult) + "Tx.csv";

                    std::ofstream csv(csvName);
                    csv << "FlowID,Src,Dst,Throughput_Mbps,Delay_ms,PDR,DropRatio\n";

                    for (auto& [fid, st] : stats)
                    {
                        if (st.txPackets == 0) continue;
                        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(fid);

                        double thput = st.rxBytes * 8.0 / duration / 1e6;
                        double delay = (st.rxPackets > 0)
                                       ? st.delaySum.GetSeconds() / st.rxPackets * 1e3
                                       : 0.0;
                        double pdr   = (double)st.rxPackets / st.txPackets;
                        double drop  = (double)(st.txPackets - st.rxPackets) / st.txPackets;

                        csv << fid << ","
                            << t.sourceAddress << ","
                            << t.destinationAddress << ","
                            << std::fixed << std::setprecision(4)
                            << thput << "," << delay << ","
                            << pdr << "," << drop << "\n";
                    }
                    csv.close();
                    std::cout << "Saved CSV: " << csvName << "\n";

                    Simulator::Destroy();
                }
            }
        }
    }

    return 0;
}