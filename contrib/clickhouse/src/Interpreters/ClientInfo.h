#pragma once

#include <Core/UUID.h>
#include <Poco/Net/SocketAddress.h>
#include <common/types.h>
#include <Common/OpenTelemetryTraceContext.h>

namespace DB
{

class WriteBuffer;
class ReadBuffer;


/** Information about client for query.
  * Some fields are passed explicitly from client and some are calculated automatically.
  *
  * Contains info about initial query source, for tracing distributed queries
  *  (where one query initiates many other queries).
  */
class ClientInfo
{
public:
    enum class Interface : uint8_t
    {
        TCP = 1,
        HTTP = 2,
        GRPC = 3,
        MYSQL = 4,
        POSTGRESQL = 5,
        LOCAL = 6,
        TCP_INTERSERVER = 7,
    };

    enum class HTTPMethod : uint8_t
    {
        UNKNOWN = 0,
        GET = 1,
        POST = 2,
    };

    enum class QueryKind : uint8_t
    {
        NO_QUERY = 0,            /// Uninitialized object.
        INITIAL_QUERY = 1,
        SECONDARY_QUERY = 2,    /// Query that was initiated by another query for distributed or ON CLUSTER query execution.
    };


    QueryKind query_kind = QueryKind::NO_QUERY;

    /// Current values are not serialized, because it is passed separately.
    String current_user;
    String current_query_id;
    Poco::Net::SocketAddress current_address;

#if 1
    /// This field is only used in foreign "Arcadia" build.
    String current_password;
#endif

    /// When query_kind == INITIAL_QUERY, these values are equal to current.
    String initial_user;
    String initial_query_id;
    Poco::Net::SocketAddress initial_address;
    time_t initial_query_start_time{};
    Decimal64 initial_query_start_time_microseconds{};

    // OpenTelemetry trace context we received from client, or which we are going
    // to send to server.
    OpenTelemetryTraceContext client_trace_context;

    /// All below are parameters related to initial query.

    Interface interface = Interface::TCP;

    /// For tcp
    String os_user;
    String client_hostname;
    String client_name;
    UInt64 client_version_major = 0;
    UInt64 client_version_minor = 0;
    UInt64 client_version_patch = 0;
    unsigned client_tcp_protocol_version = 0;

    /// For http
    HTTPMethod http_method = HTTPMethod::UNKNOWN;
    String http_user_agent;
    String http_referer;

    /// For mysql
    UInt64 connection_id = 0;

    /// Comma separated list of forwarded IP addresses (from X-Forwarded-For for HTTP interface).
    /// It's expected that proxy appends the forwarded address to the end of the list.
    /// The element can be trusted only if you trust the corresponding proxy.
    /// NOTE This field can also be reused in future for TCP interface with PROXY v1/v2 protocols.
    String forwarded_for;

    /// Common
    String quota_key;

    UInt64 distributed_depth = 0;

    bool is_replicated_database_internal = false;

    bool empty() const { return query_kind == QueryKind::NO_QUERY; }

    /** Serialization and deserialization.
      * Only values that are not calculated automatically or passed separately are serialized.
      * Revisions are passed to use format that server will understand or client was used.
      */
    void write(WriteBuffer & out, const UInt64 server_protocol_revision) const;
    void read(ReadBuffer & in, const UInt64 client_protocol_revision);

    /// Initialize parameters on client initiating query.
    void setInitialQuery();

private:
    void fillOSUserHostNameAndVersionInfo();
};

}
