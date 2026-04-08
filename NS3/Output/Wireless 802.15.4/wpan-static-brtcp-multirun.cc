/* =============================================================
 * wpan-static-brtcp-multirun.cc
 * NS-3 BR-TCP simulation — IEEE 802.15.4 LR-WPAN + 6LoWPAN
 * =============================================================*/

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/internet-module.h"
#include "ns3/lr-wpan-module.h"
#include "ns3/lr-wpan-net-device.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/ripng-helper.h"
#include "ns3/sixlowpan-module.h"
#include "ns3/tcp-congestion-ops.h"
#include "ns3/spectrum-module.h"          // ← SingleModelSpectrumChannel
#include "ns3/propagation-module.h"       // ← RangePropagationLossModel
                                          //   ConstantSpeedPropagationDelayModel

#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("WpanStaticBrTcpMulti");

/* ─────────────── BR-TCP Congestion Control ─────────────────── */
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
                              "Reserved bandwidth r [bit/s] — 802.15.4 rate.",
                              DataRateValue(DataRate("250kbps")),
                              MakeDataRateAccessor(&TcpBr::m_reservedBw),
                              MakeDataRateChecker());
        return tid;
    }

    TcpBr() : TcpNewReno(), m_reservedBw("250kbps") {}
    TcpBr(const TcpBr& s) : TcpNewReno(s), m_reservedBw(s.m_reservedBw) {}
    ~TcpBr() override = default;

    std::string GetName() const override { return "TcpBr"; }

    uint32_t GetSsThresh(Ptr<const TcpSocketState> tcb,
                         uint32_t bytesInFlight) override
    {
        double   rtt = tcb->m_lastRtt.Get().GetSeconds();
        double   r   = static_cast<double>(m_reservedBw.GetBitRate());
        uint32_t seg = tcb->m_segmentSize;
        if (rtt > 0.0 && r > 0.0 && seg > 0)
            return std::max(static_cast<uint32_t>((r / 8.0) * rtt), 2 * seg);
        return TcpNewReno::GetSsThresh(tcb, bytesInFlight);
    }

    Ptr<TcpCongestionOps> Fork() override { return CopyObject<TcpBr>(this); }

  private:
    DataRate m_reservedBw;
};

NS_OBJECT_ENSURE_REGISTERED(TcpBr);

/* ───────────────────────────── main ────────────────────────── */
int main(int argc, char* argv[])
{
    CommandLine cmd(__FILE__);
    cmd.Parse(argc, argv);

    const double   txRange   = 30.0;
    const double   simTime   = 100.0;
    const double   startTime = 10.0;
    const uint32_t pktSize   = 60;

    uint32_t nodesArr[]     = {20, 40, 60, 80, 100};
    uint32_t flowsArr[]     = {10, 20, 30, 40, 50};
    uint32_t pktPerSecArr[] = {10, 20, 30, 40, 50};
    uint32_t covMultArr[]   = {1, 2, 3, 4, 5};

    Config::SetDefault("ns3::TcpL4Protocol::SocketType",  StringValue("ns3::TcpBr"));
    Config::SetDefault("ns3::TcpBr::ReservedBandwidth",   DataRateValue(DataRate("250kbps")));
    Config::SetDefault("ns3::TcpSocket::SegmentSize",     UintegerValue(60));
    Config::SetDefault("ns3::TcpSocket::SndBufSize",      UintegerValue(1 << 16));
    Config::SetDefault("ns3::TcpSocket::RcvBufSize",      UintegerValue(1 << 16));

    for (uint32_t nNodes : nodesArr)
    {
        for (uint32_t nFlows : flowsArr)
        {
            for (uint32_t pktPerSec : pktPerSecArr)
            {
                for (uint32_t covMult : covMultArr)
                {
                    double areaSide = covMult * txRange;

                    std::cout << "\n=== IEEE 802.15.4 | "
                              << nNodes    << " nodes | "
                              << nFlows    << " flows | "
                              << pktPerSec << " pkt/s | "
                              << covMult   << "Tx ===\n";

                    /* ── 1. Nodes ── */
                    NodeContainer nodes;
                    nodes.Create(nNodes);

                    /* ── 2. LR-WPAN PHY/MAC + range propagation loss ──
                     * BUG FIX 3: original code never applied txRange to the
                     * LR-WPAN channel — range was unlimited. Fix: create the
                     * channel manually and add RangePropagationLossModel.    */
                    LrWpanHelper lrWpan;

                    // Build a channel with range limiting
                    Ptr<SingleModelSpectrumChannel> channel =
                        CreateObject<SingleModelSpectrumChannel>();
                    Ptr<RangePropagationLossModel> propLoss =
                        CreateObject<RangePropagationLossModel>();
                    propLoss->SetAttribute("MaxRange", DoubleValue(txRange));
                    channel->AddPropagationLossModel(propLoss);
                    Ptr<ConstantSpeedPropagationDelayModel> propDelay =
                        CreateObject<ConstantSpeedPropagationDelayModel>();
                    channel->SetPropagationDelayModel(propDelay);
                    lrWpan.SetChannel(channel);

                    NetDeviceContainer lrDevices = lrWpan.Install(nodes);

                    // Manual PAN + short-address assignment
                    for (uint32_t i = 0; i < lrDevices.GetN(); i++)
                    {
                        Ptr<LrWpanNetDevice> dev =
                            DynamicCast<LrWpanNetDevice>(lrDevices.Get(i));
                        dev->GetMac()->SetPanId(0);
                        uint8_t addrBuf[2];
                        addrBuf[0] = (uint8_t)((i + 1) >> 8);
                        addrBuf[1] = (uint8_t)((i + 1) & 0xFF);
                        Mac16Address shortAddr;
                        shortAddr.CopyFrom(addrBuf);
                        dev->GetMac()->SetShortAddress(shortAddr);
                    }

                    /* ── 3. 6LoWPAN ── */
                    SixLowPanHelper sixLowPan;
                    sixLowPan.SetDeviceAttribute("ForceEtherType", BooleanValue(true));
                    NetDeviceContainer sixDevices = sixLowPan.Install(lrDevices);

                    /* ── 4. Mobility ── */
                    MobilityHelper mobility;
                    mobility.SetPositionAllocator(
                        "ns3::RandomRectanglePositionAllocator",
                        "X", StringValue("ns3::UniformRandomVariable[Min=0.0|Max=" +
                                         std::to_string(areaSide) + "]"),
                        "Y", StringValue("ns3::UniformRandomVariable[Min=0.0|Max=" +
                                         std::to_string(areaSide) + "]"));
                    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
                    mobility.Install(nodes);

                    /* ── 5. Internet stack + RIPng ──
                     * BUG FIX 1: original had RipNgHelper / InternetStackHelper
                     * declared TWICE → compile error "redefinition of ripNg/listRH/
                     * internet". Removed all duplicates; single clean block below. */
                    RipNgHelper ripNg;
                    Ipv6ListRoutingHelper listRH;
                    listRH.Add(ripNg, 0);

                    InternetStackHelper internet;
                    internet.SetRoutingHelper(listRH);
                    internet.Install(nodes);

                    /* ── 6. IPv6 addressing ── */
                    Ipv6AddressHelper ipv6;
                    ipv6.SetBase(Ipv6Address("2001:1::"), Ipv6Prefix(64));
                    Ipv6InterfaceContainer ifaces = ipv6.Assign(sixDevices);

                    for (uint32_t i = 0; i < ifaces.GetN(); i++)
                        ifaces.SetForwarding(i, true);

                    /* ── 7. Applications ── */
                    ApplicationContainer sinkApps, srcApps;
                    const std::string dataRate =
                        std::to_string(pktSize * 8 * pktPerSec) + "bps";

                    for (uint32_t i = 0; i < nFlows; i++)
                    {
                        uint32_t src  = i % nNodes;
                        uint32_t dst  = (i + nNodes / 2) % nNodes;
                        if (src == dst) dst = (dst + 1) % nNodes;
                        uint16_t port = 9000 + i;

                        PacketSinkHelper sink(
                            "ns3::TcpSocketFactory",
                            Inet6SocketAddress(Ipv6Address::GetAny(), port));
                        sinkApps.Add(sink.Install(nodes.Get(dst)));

                        OnOffHelper onoff(
                            "ns3::TcpSocketFactory",
                            Inet6SocketAddress(ifaces.GetAddress(dst, 1), port));
                        onoff.SetAttribute("DataRate",   StringValue(dataRate));
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

                    /* ── 8. Flow Monitor ── */
                    FlowMonitorHelper fmHelper;
                    Ptr<FlowMonitor> flowMon = fmHelper.InstallAll();

                    Simulator::Stop(Seconds(simTime));
                    Simulator::Run();

                    /* ── 9. Metrics ──
                     * BUG FIX 2: original used fmHelper.GetClassifier() which
                     * returns Ipv4FlowClassifier — DynamicCast to Ipv6 returns
                     * nullptr → crash at classifier->FindFlow().
                     * Fix: use GetClassifier6() for the IPv6 classifier.        */
                    flowMon->CheckForLostPackets();

                    Ptr<Ipv6FlowClassifier> classifier =
                        DynamicCast<Ipv6FlowClassifier>(fmHelper.GetClassifier6());

                    auto   stats    = flowMon->GetFlowStats();
                    double duration = simTime - 1.0 - startTime;

                    std::string csvName =
                        "node"     + std::to_string(nNodes)    +
                        "_flow"    + std::to_string(nFlows)    +
                        "_packet_" + std::to_string(pktPerSec) +
                        "_"        + std::to_string(covMult)   + "Tx.csv";

                    std::ofstream csv(csvName);
                    csv << "FlowID,Src,Dst,Throughput_Mbps,Delay_ms,PDR,DropRatio\n";

                    for (auto& [fid, st] : stats)
                    {
                        if (st.txPackets == 0) continue;
                        Ipv6FlowClassifier::FiveTuple t = classifier->FindFlow(fid);

                        double thput = st.rxBytes * 8.0 / duration / 1e6;
                        double delay = (st.rxPackets > 0)
                                       ? st.delaySum.GetSeconds() / st.rxPackets * 1e3
                                       : 0.0;
                        double pdr  = (double)st.rxPackets / st.txPackets;
                        double drop = (double)(st.txPackets - st.rxPackets) / st.txPackets;

                        csv << fid << ","
                            << t.sourceAddress << ","
                            << t.destinationAddress << ","
                            << std::fixed << std::setprecision(4)
                            << thput << "," << delay << ","
                            << pdr   << "," << drop  << "\n";
                    }
                    csv.close();
                    std::cout << "Saved: " << csvName << "\n";

                    Simulator::Destroy();
                }
            }
        }
    }
    return 0;
}