package ru.yandex.yt.ytclient.proxy;

import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Random;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;

import com.google.protobuf.MessageLite;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import ru.yandex.bolts.collection.Cf;
import ru.yandex.yt.ytclient.bus.BusConnector;
import ru.yandex.yt.ytclient.rpc.BalancingRpcClient;
import ru.yandex.yt.ytclient.rpc.RpcClient;
import ru.yandex.yt.ytclient.rpc.RpcClientRequestBuilder;
import ru.yandex.yt.ytclient.rpc.RpcCredentials;
import ru.yandex.yt.ytclient.rpc.RpcOptions;
import ru.yandex.yt.ytclient.rpc.internal.BalancingDestination;
import ru.yandex.yt.ytclient.rpc.internal.DataCenter;

public class YtClient extends ApiServiceClient implements AutoCloseable {
    private static final Logger logger = LoggerFactory.getLogger(ApiServiceClient.class);

    private final List<PeriodicDiscovery> discovery; // TODO: stop
    private final Random rnd = new Random();

    final private DataCenter[] dataCenters;
    final private ScheduledExecutorService executorService;
    final private RpcOptions options;
    private DataCenter localDataCenter = null;

    public YtClient(
           BusConnector connector,
           Map<String, List<String>> initialAddresses,
           String localDataCenterName,
           RpcCredentials credentials,
           RpcOptions options)
    {
        super(options);
        discovery = new ArrayList<>();

        this.dataCenters = new DataCenter[initialAddresses.size()];
        this.executorService = connector.executorService();
        this.options = options;

        int dataCenterIndex = 0;

        for (Map.Entry<String, List<String>> entry : initialAddresses.entrySet()) {
            final String dataCenterName = entry.getKey();

            DataCenter dc = new DataCenter(
                    dataCenterName,
                    new BalancingDestination[0],
                    -1.0,
                    options);

            dataCenters[dataCenterIndex++] = dc;

            if (dataCenterName.equals(localDataCenterName)) {
                localDataCenter = dc;
            }

            final PeriodicDiscoveryListener listener = new PeriodicDiscoveryListener() {
                @Override
                public void onProxiesAdded(List<RpcClient> proxies) {
                    dc.addProxies(proxies);
                }

                @Override
                public void onProxiesRemoved(List<RpcClient> proxies) {
                    dc.removeProxies(proxies);
                }
            };

            discovery.add(
                    new PeriodicDiscovery(
                            entry.getValue(),
                            connector,
                            options,
                            credentials,
                            listener));
        }

        pingDataCenters();
    }

    public YtClient(
            BusConnector connector,
            List<String> addresses,
            RpcCredentials credentials,
            RpcOptions options)
    {
        this(connector, Cf.map("unknown", addresses), "unknown", credentials, options);
    }

    @Override
    public void close() {
        for (PeriodicDiscovery disco : discovery) {
            disco.close();
        }

        for (DataCenter dc : dataCenters) {
            dc.close();
        }
    }

    private void schedulePing() {
        executorService.schedule(
                this::pingDataCenters,
                options.getPingTimeout().toMillis(),
                TimeUnit.MILLISECONDS);
    }

    private void pingDataCenters() {
        logger.debug("ping");

        CompletableFuture<Void> futures[] = new CompletableFuture[dataCenters.length];
        int i = 0;
        for (DataCenter entry : dataCenters) {
            futures[i++] = entry.ping(executorService, options.getPingTimeout());
        }

        schedulePing();
    }

    private List<RpcClient> selectDestinations() {
        return BalancingRpcClient.selectDestinations(
                dataCenters, 3,
                localDataCenter != null,
                rnd,
                ! options.getFailoverPolicy().randomizeDcs());
    }

    @Override
    protected <RequestType extends MessageLite.Builder, ResponseType> CompletableFuture<ResponseType> invoke(
            RpcClientRequestBuilder<RequestType, ResponseType> builder)
    {
        return builder.invokeVia(selectDestinations());
    }
}
