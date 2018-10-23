#pragma once

#include <mapreduce/yt/interface/node.h>
#include <mapreduce/yt/interface/common.h>

#include <util/generic/string.h>
#include <util/datetime/base.h>

namespace NYT {

enum EEncoding : int
{
    E_IDENTITY  /* "identity" */,
    E_GZIP      /* "gzip" */,
    E_BROTLI    /* "br" */,
    E_Z_LZ4     /* "z-lz4" */,
};

enum class ENodeReaderFormat : int
{
    Yson,  // Always use YSON format,
    Skiff, // Always use Skiff format, throw exception if it's not possible (non-strict schema, dynamic table etc.)
    Auto,  // Use Skiff format if it's possible, YSON otherwise
};

////////////////////////////////////////////////////////////////////////////////

struct TConfig
{
    TString Hosts;
    TString Pool;
    TString Token;
    TString Prefix;
    TString ApiVersion;
    TString LogLevel;

    // Compression for data that is sent to YT cluster.
    EEncoding ContentEncoding;

    // Compression for data that is read from YT cluster.
    EEncoding AcceptEncoding;

    TString GlobalTxId;

    bool ForceIpV4;
    bool ForceIpV6;
    bool UseHosts;

    TNode Spec;
    TNode TableWriter;

    TDuration ConnectTimeout;
    TDuration SocketTimeout;
    TDuration TxTimeout;
    TDuration PingTimeout;
    TDuration PingInterval;

    // How often should we poll for lock state
    TDuration WaitLockPollInterval;

    TDuration RetryInterval;
    TDuration ChunkErrorsRetryInterval;

    TDuration RateLimitExceededRetryInterval;
    TDuration StartOperationRetryInterval;

    int RetryCount;
    int ReadRetryCount;
    int StartOperationRetryCount;

    TString RemoteTempFilesDirectory;
    TString RemoteTempTablesDirectory;


    bool UseClientProtobuf;
    ENodeReaderFormat NodeReaderFormat;

    int ConnectionPoolSize;

    bool MountSandboxInTmpfs;

    // Testing options, should never be used in user programs.
    bool UseAbortableResponse = false;
    bool EnableDebugMetrics = false;

    //
    // There is optimization used with local YT that enables to skip binary upload and use real binary path.
    // When EnableLocalModeOptimization is set to false this optimization is completely disabled.
    bool EnableLocalModeOptimization = true;

    static bool GetBool(const char* var, bool defaultValue = false);
    static int GetInt(const char* var, int defaultValue);
    static TDuration GetDuration(const char* var, TDuration defaultValue);
    static EEncoding GetEncoding(const char* var);

    static void ValidateToken(const TString& token);
    static TString LoadTokenFromFile(const TString& tokenPath);

    static TNode LoadJsonSpec(const TString& strSpec);

    void LoadToken();
    void LoadSpec();
    void LoadTimings();
    TJobBinaryConfig GetJobBinary() const;

    TConfig();

    static TConfig* Get();
};

////////////////////////////////////////////////////////////////////////////////

struct TProcessState
{
    TString HostName;
    TString UserName;
    TVector<TString> CommandLine;
    int Pid;
    TString ClientVersion;

    TProcessState();

    void SetCommandLine(int argc, const char* argv[]);

    static TProcessState* Get();
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT

