#include <yt/core/test_framework/framework.h>

#include <yp/server/nodes/porto.h>

namespace NYP {
namespace NServer {
namespace NNodes {
namespace {

using namespace ::NYP::NServer::NObjects;

////////////////////////////////////////////////////////////////////////////////

THashMap<TString, TString> BuildProps(
    const NClient::NApi::NProto::TNodeSpec& nodeSpec,
    const NProto::TPodSpecOther& podSpecOther,
    const NProto::TPodStatusOther& podStatusOther)
{
    const auto buildedProps = BuildPortoProperties(nodeSpec, podSpecOther, podStatusOther);
    THashMap<TString, TString> props;
    props.insert(buildedProps.begin(), buildedProps.end());
    EXPECT_EQ(buildedProps.size(), props.size());
    return props;
}

TEST(BuildPortoProperties, TestTunnelProperties)
{
    NClient::NApi::NProto::TNodeSpec nodeSpec;
    NProto::TPodSpecOther podSpecOther;
    NProto::TPodStatusOther podStatusOther;

    nodeSpec.set_cpu_to_vcpu_factor(1);
    auto* resourceRequest = podSpecOther.mutable_resource_requests();
    resourceRequest->set_vcpu_guarantee(1000);

    auto* alloc = podStatusOther.add_ip6_address_allocations();
    alloc->set_vlan_id("backbone");
    alloc->set_address("5678");

    auto* dns = podStatusOther.mutable_dns();
    dns->set_transient_fqdn("hello_world.yandex.net");

    auto* vsTunnel = podSpecOther.mutable_virtual_service_tunnel();
    auto* vsStatus = podStatusOther.mutable_virtual_service();
    vsTunnel->set_virtual_service_id("VS_ID");
    vsStatus->add_ip6_addresses("1234");
    vsStatus->add_ip4_addresses("1.2.3.4");

    const auto props = BuildProps(nodeSpec, podSpecOther, podStatusOther);

    EXPECT_EQ("1.000c", props.at("cpu_guarantee"));
    EXPECT_EQ("hello_world.yandex.net", props.at("hostname"));
    EXPECT_EQ("L3 veth;ipip6 tun0 2a02:6b8:0:3400::aaaa 5678;MTU tun0 1450;MTU ip6tnl0 1450", props.at("net"));
    EXPECT_EQ("veth 5678;ip6tnl0 1234;tun0 1.2.3.4", props.at("ip"));
    EXPECT_EQ("net.ipv4.conf.all.rp_filter:0;net.ipv4.conf.default.rp_filter:0", props.at("sysctl"));
}

TEST(BuildPortoProperties, Limits)
{
    NClient::NApi::NProto::TNodeSpec nodeSpec;
    NProto::TPodSpecOther podSpecOther;
    NProto::TPodStatusOther podStatusOther;

    nodeSpec.set_cpu_to_vcpu_factor(1);
    auto* resourceRequest = podSpecOther.mutable_resource_requests();
    resourceRequest->set_vcpu_guarantee(1000);
    resourceRequest->set_vcpu_limit(2000);
    resourceRequest->set_memory_guarantee(2048);
    resourceRequest->set_memory_limit(4096);
    resourceRequest->set_anonymous_memory_limit(8192);
    resourceRequest->set_dirty_memory_limit(9999);

    auto dns = podStatusOther.mutable_dns();
    dns->set_transient_fqdn("limits.yandex.net");

    const auto props = BuildProps(nodeSpec, podSpecOther, podStatusOther);

    EXPECT_EQ("1.000c", props.at("cpu_guarantee"));
    EXPECT_EQ("2.000c", props.at("cpu_limit"));
    EXPECT_EQ("2048", props.at("memory_guarantee"));
    EXPECT_EQ("4096", props.at("memory_limit"));
    EXPECT_EQ("8192", props.at("anon_limit"));
    EXPECT_EQ("9999", props.at("dirty_limit"));
    EXPECT_EQ("limits.yandex.net", props.at("hostname"));
    EXPECT_EQ("L3 veth", props.at("net"));
}

////////////////////////////////////////////////////////////////////////////////

} // namespace
} // namespace NObjects
} // namespace NServer
} // namespace NYP
