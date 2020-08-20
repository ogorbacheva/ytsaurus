package ru.yandex.yt.ytclient.proxy;

import java.util.List;
import java.util.Random;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

import org.junit.After;
import org.junit.Before;
import org.junit.Test;

import ru.yandex.yt.ytclient.proxy.internal.HostPort;

import static org.hamcrest.CoreMatchers.containsString;
import static org.hamcrest.CoreMatchers.is;
import static org.junit.Assert.assertThat;
import static ru.yandex.yt.testlib.FutureUtils.getError;
import static ru.yandex.yt.testlib.FutureUtils.waitFuture;

public class ClientPoolTest {
    ExecutorService executorService;
    MockRpcClientFactory mockRpcClientFactory;

    @Before
    public void before() {
        executorService = Executors.newFixedThreadPool(1);
        mockRpcClientFactory = new MockRpcClientFactory();
    }

    @After
    public void after() {
        executorService.shutdownNow();
    }

    @Test
    public void testSimple() {
        ClientPool clientPool = newClientPool();

        CompletableFuture<Void> done = new CompletableFuture<>();
        try {

            var clientFuture1 = clientPool.peekClient(done);
            assertThat(clientFuture1.isDone(), is(false));
            clientPool.updateClients(List.of(HostPort.parse("localhost:1")));

            waitFuture(clientFuture1, 100);
            assertThat(clientFuture1.join().destinationName(), is("localhost:1"));

            var clientFuture2 = clientPool.peekClient(done);
            assertThat(clientFuture2.isDone(), is(true));
            assertThat(clientFuture2.join().destinationName(), is("localhost:1"));
        } finally {
            done.complete(null);
        }
    }

    @Test
    public void testUpdateEmpty() {
        ClientPool clientPool = newClientPool();

        CompletableFuture<Void> done = new CompletableFuture<>();
        try {
            var clientFuture1 = clientPool.peekClient(done);
            assertThat(clientFuture1.isDone(), is(false));
            clientPool.updateClients(List.of());

            waitFuture(clientFuture1, 100);
            assertThat(getError(clientFuture1).getMessage(), containsString("Cannot get rpc proxies"));
        } finally {
            done.complete(null);
        }
    }

    @Test
    public void testLingeringConnection() {
        ClientPool clientPool = newClientPool();

        CompletableFuture<Void> done = new CompletableFuture<>();
        try {
            waitFuture(
                    clientPool.updateClients(List.of(HostPort.parse("localhost:1"))),
                    100);
            var clientFuture1 = clientPool.peekClient(done);
            assertThat(clientFuture1.isDone(), is(true));
            assertThat(clientFuture1.join().destinationName(), is("localhost:1"));
            assertThat(mockRpcClientFactory.isConnectionOpened("localhost:1"), is(true));

            waitFuture(
                    clientPool.updateClients(List.of()),
                    100);

            assertThat(mockRpcClientFactory.isConnectionOpened("localhost:1"), is(true));
        } finally {
            done.complete(null);
        }
        assertThat(mockRpcClientFactory.isConnectionOpened("localhost:1"), is(false));
    }

    @Test
    public void testCanceledConnection() {
        ClientPool clientPool = newClientPool();

        CompletableFuture<Void> done = new CompletableFuture<>();
        try {
            var clientFuture1 = clientPool.peekClient(done);
            assertThat(clientFuture1.isDone(), is(false));
            clientFuture1.cancel(true);

            waitFuture(
                    clientPool.updateClients(List.of(HostPort.parse("localhost:1"))),
                    100);

            assertThat(mockRpcClientFactory.isConnectionOpened("localhost:1"), is(true));

            waitFuture(
                    clientPool.updateClients(List.of()),
                    100);

            assertThat(mockRpcClientFactory.isConnectionOpened("localhost:1"), is(false));
        } finally {
            done.complete(null);
        }
    }

    ClientPool newClientPool() {
        return new ClientPool(
                "testDc",
                5,
                mockRpcClientFactory,
                executorService,
                new Random());
    }
}
