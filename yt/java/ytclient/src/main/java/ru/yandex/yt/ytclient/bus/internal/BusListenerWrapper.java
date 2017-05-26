package ru.yandex.yt.ytclient.bus.internal;

import java.util.List;
import java.util.Objects;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import ru.yandex.yt.ytclient.bus.Bus;
import ru.yandex.yt.ytclient.bus.BusListener;

/**
 * Обёртка над BusListener, подавляющая и логирующая исключения
 */
public class BusListenerWrapper implements BusListener {
    private static final Logger logger = LoggerFactory.getLogger(BusListenerWrapper.class);

    private final BusListener listener;

    public BusListenerWrapper(BusListener listener) {
        this.listener = Objects.requireNonNull(listener);
    }

    public BusListener getListener() {
        return listener;
    }

    @Override
    public void onMessage(Bus bus, List<byte[]> message) {
        try {
            listener.onMessage(bus, message);
        } catch (RuntimeException e) {
            logger.error("Unhandled exception in a listener", e);
        }
    }

    @Override
    public void onConnect(Bus bus) {
        try {
            listener.onConnect(bus);
        } catch (RuntimeException e) {
            logger.error("Unhandled exception in a listener", e);
        }
    }

    @Override
    public void onDisconnect(Bus bus) {
        try {
            listener.onDisconnect(bus);
        } catch (RuntimeException e) {
            logger.error("Unhandled exception in a listener", e);
        }
    }

    @Override
    public void onException(Bus bus, Throwable cause) {
        try {
            listener.onException(bus, cause);
        } catch (RuntimeException e) {
            logger.error("Unhandled exception in a listener", e);
        }
    }
}
