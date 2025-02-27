#include <vector>
#include <string>
#include <ctime>
#include <zmq.hpp>

// NS3 
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/mobility-module.h"
#include "ns3/wifi-module.h"
#include "ns3/flow-monitor-helper.h"
#include "ns3/ipv4-flow-probe.h"
#include "ns3/netanim-module.h" 
#include "ns3/log.h"

// custom
#include "readExt.h"
#include "nodeApp.h"

using namespace ns3;
using namespace std;

NS_LOG_COMPONENT_DEFINE("NetworkSim");

NetworkConfig config;

int main(int argc, char *argv[])
{
    std::cout << "1. Program started" << std::endl;


    // 解析命令行參數
    CommandLine cmd(__FILE__);
    
    
    std::string configFile = "/home/nmsl/python_test/syncFromFile/static.json";
    readConfig(configFile, config);

    std::cout << "2. Config read" << std::endl;

    uint32_t round = 1;
    uint32_t timeSlot = 0;
    cmd.AddValue("round", "ns3 round", round);
    cmd.AddValue("timeSlot", "ns3 time_slot", timeSlot);
    cmd.Parse(argc, argv);

    LogComponentEnable("NetworkSim", LOG_LEVEL_ALL);    
    LogComponentEnable("NodeApp", LOG_LEVEL_ALL);
    
    NS_LOG_INFO("Using config: " << config);

    // 設置時間解析度
    Time::SetResolution(Time::NS);

    // 配置 TCP 參數
    Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue(config.segmentSize));
    Config::SetDefault("ns3::TcpSocket::SndBufSize", UintegerValue(config.tcpSendBuffer));
    Config::SetDefault("ns3::TcpSocket::RcvBufSize", UintegerValue(config.tcpRecvBuffer));

    // 啟用數據包打印
    Packet::EnablePrinting();

    // 創建節點
    NodeContainer mobileNodes;
    NodeContainer staticNodes;
    // NodeContainer infrastructureNodes;

    NS_LOG_INFO("Creating Nodes");
    mobileNodes.Create(config.uavsNames.size());
    staticNodes.Create(1);  // 控制節點

    // 創建並安裝網路設備
    NetDeviceContainer mobileDevices;
    NetDeviceContainer staticDevices;
    // NetDeviceContainer infraDevices;

    // 設置 WiFi
    YansWifiChannelHelper channel = YansWifiChannelHelper::Default();
    YansWifiPhyHelper phy;
    phy.SetChannel(channel.Create());

    // 配置 WiFi
    WifiHelper wifi;
    wifi.SetStandard (WIFI_STANDARD_80211g);
    wifi.SetRemoteStationManager("ns3::AarfWifiManager");

    WifiMacHelper mac;
    Ssid ssid = Ssid("ns-3-ssid");
    mac.SetType("ns3::StaWifiMac",
                "Ssid", SsidValue(ssid),
                "ActiveProbing", BooleanValue(false));
    mobileDevices = wifi.Install(phy, mac, mobileNodes);

    mac.SetType("ns3::ApWifiMac",
            "Ssid", SsidValue(ssid));
    staticDevices = wifi.Install(phy, mac, staticNodes);

    // 設置移動模型
    MobilityHelper mobilityMobile;
    MobilityHelper mobilityStatic;

    // 初始化位置分配器
    Ptr<ListPositionAllocator> mobilePos = CreateObject<ListPositionAllocator>();
    Ptr<ListPositionAllocator> staticPos = CreateObject<ListPositionAllocator>();


    // 設置移動節點的初始位置
    for(uint32_t i = 0; i < mobileNodes.GetN(); i++) {
        std::vector<float>& position = config.uavsPositions[i];
        Vector pos(position[0], position[1], position[2]);
        mobilePos->Add(pos);
        NS_LOG_INFO("Added mobility model for node " << config.uavsNames[i]);
    }
    mobilityMobile.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobilityMobile.SetPositionAllocator(mobilePos);
    mobilityMobile.Install(mobileNodes);

    // 設置靜態節點位置
    staticPos->Add(Vector(0, 0, 0));
    mobilityStatic.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobilityStatic.SetPositionAllocator(staticPos);
    mobilityStatic.Install(staticNodes);

    // 安裝網路協議棧
    InternetStackHelper stack;
    stack.Install(mobileNodes);
    stack.Install(staticNodes);

    // 分配 IP 地址
    uint32_t port = 8888;
    Ipv4AddressHelper ipv4;
    ipv4.SetBase("10.1.1.0", "255.255.255.0");
    
    Ipv4InterfaceContainer mobileInterfaces = ipv4.Assign(mobileDevices);
    Ipv4InterfaceContainer staticInterfaces = ipv4.Assign(staticDevices);

    // UAV
    std::unordered_map<std::string, Ptr<NodeApp>> name2nodeApps;
    std::vector<Ptr<NodeApp>> nodeApps;
    for(uint32_t i = 0; i < mobileNodes.GetN(); i++) {
        Ptr<NodeApp> uavApp = CreateObject<NodeApp>();
        mobileNodes.Get(i)->AddApplication(uavApp);

        std::vector<Address> peerAddresses;
        peerAddresses.push_back(InetSocketAddress(staticInterfaces.GetAddress(0), port));

        Ptr<Socket> socket = Socket::CreateSocket(mobileNodes.Get(i), TypeId::LookupByName ("ns3::UdpSocketFactory"));
        Ipv4Address address = mobileInterfaces.GetAddress(i);

        uavApp->Setup(config.uavsNames[i], socket, address, port, peerAddresses, timeSlot, round);
        uavApp->SetStartTime(Seconds(0.2));
        nodeApps.push_back(uavApp);
        name2nodeApps[config.uavsNames[i]] = uavApp;
    }
        
    // GCS
    std::vector<Address> mobileAddress;
    for(uint32_t i = 0; i < mobileNodes.GetN(); i++) { // get all mobile adress in order to connect it
        mobileAddress.push_back(InetSocketAddress(mobileInterfaces.GetAddress(i), port));
    }
    Ptr<NodeApp> gcsApp = CreateObject<NodeApp>();    
    staticNodes.Get(0)->AddApplication(gcsApp);

    Ptr<Socket> socket = Socket::CreateSocket(staticNodes.Get(0), TypeId::LookupByName ("ns3::UdpSocketFactory"));
    Ipv4Address address = staticInterfaces.GetAddress(0);

    gcsApp->Setup(std::string("GCS"), socket, address, port, mobileAddress, timeSlot, round);
    gcsApp->SetStartTime(Seconds(0.1));
    nodeApps.push_back(gcsApp);
    name2nodeApps["gcs"] = gcsApp;
    // gcsApp->SetStopTime(Simulator::GetMaximumSimulationTime());

    std::string MessageTransmitLogFile = "/home/nmsl/python_test/syncFromFile/msg_logs/message_transmit_log"+ std::to_string(timeSlot)  + "_" + std::to_string(round) + ".csv";
    readMessageTransmitLog(MessageTransmitLogFile, name2nodeApps);

    // 設置流量監控
    FlowMonitorHelper flowmon;
    Ptr<FlowMonitor> monitor = flowmon.InstallAll();

    // 將所有應用程序轉換為基類指針
    // std::vector<Ptr<NodeApp>> apps;
    // for(auto& app : nodeApps) {
    //     apps.push_back(DynamicCast<NodeApp>(app));
    // }


    AnimationInterface anim("syncFormFile-animation.xml");

    
    // 運行模擬
    Simulator::Stop(Seconds(100.0));
    Simulator::Run();
    std::cout << "3. Simulation completed " << std::endl;

    // Collect and print flow statistics
    monitor->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier> (flowmon.GetClassifier());
    std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats();

    for (std::map<FlowId, FlowMonitor::FlowStats>::const_iterator i = stats.begin(); i != stats.end(); ++i)
    {
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(i->first);

        
        std::cout << "Flow " << i->first << " (" << t.sourceAddress << " -> " << t.destinationAddress << ")\n";
        std::cout << "  Tx Packets: " << i->second.txPackets << "\n";
        std::cout << "  Tx Bytes:   " << i->second.txBytes << "\n";
        std::cout << "  Rx Packets: " << i->second.rxPackets << "\n";
        std::cout << "  Rx Bytes:   " << i->second.rxBytes << "\n";
        std::cout << "  Throughput: " << i->second.rxBytes * 8.0 / (i->second.timeLastRxPacket.GetSeconds() - i->second.timeFirstTxPacket.GetSeconds()) / 1000000 << " Mbps\n";
        std::cout << "  Mean Delay: " << i->second.delaySum.GetSeconds() / i->second.rxPackets << " s\n";
        std::cout << "  Mean Jitter: " << i->second.jitterSum.GetSeconds() / (i->second.rxPackets - 1) << " s\n";
        std::cout << "  Packet Loss: " << 100.0 * (i->second.txPackets - i->second.rxPackets) / i->second.txPackets << "%\n";
    }

    // 清理
    Simulator::Destroy();
    
    return 0;
}