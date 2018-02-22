package ru.yandex.yt.ytclient.rpc;

import java.io.IOException;
import java.io.UncheckedIOException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.time.Duration;
import java.time.Instant;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.Executor;
import java.util.concurrent.Future;
import java.util.function.Function;

import com.google.protobuf.CodedInputStream;
import com.google.protobuf.CodedOutputStream;
import com.google.protobuf.MessageLite;
import io.netty.buffer.ByteBuf;

import ru.yandex.inside.yt.kosher.common.GUID;
import ru.yandex.yt.TGuid;
import ru.yandex.yt.TGuidOrBuilder;
import ru.yandex.yt.TSerializedMessageEnvelope;
import ru.yandex.yt.rpc.TRequestCancelationHeader;
import ru.yandex.yt.rpc.TRequestHeader;
import ru.yandex.yt.rpc.TResponseHeader;

public class RpcUtil {
    public static final long MICROS_PER_SECOND = 1_000_000L;
    public static final long NANOS_PER_MICROSECOND = 1_000L;

    public static byte[] createMessageHeader(RpcMessageType type, MessageLite header) {
        int size = header.getSerializedSize();
        byte[] data = new byte[4 + size];
        ByteBuffer.wrap(data).order(ByteOrder.LITTLE_ENDIAN).putInt(type.getValue());
        try {
            CodedOutputStream output = CodedOutputStream.newInstance(data, 4, size);
            header.writeTo(output);
            output.checkNoSpaceLeft();
        } catch (IOException e) {
            throw new UncheckedIOException(e);
        }
        return data;
    }

    public static byte[] createMessageBodyWithEnvelope(MessageLite body, int codecId) {
        if (codecId != 0) {
            throw new IllegalArgumentException("Compression codecs are not supported");
        }
        TSerializedMessageEnvelope header = TSerializedMessageEnvelope.getDefaultInstance();
        int headerSize = header.getSerializedSize();
        int bodySize = body.getSerializedSize();
        byte[] data = new byte[8 + headerSize + bodySize];
        ByteBuffer.wrap(data).order(ByteOrder.LITTLE_ENDIAN).putInt(headerSize).putInt(bodySize);
        try {
            CodedOutputStream output = CodedOutputStream.newInstance(data, 8, data.length - 8);
            header.writeTo(output);
            body.writeTo(output);
            output.checkNoSpaceLeft();
        } catch (IOException e) {
            throw new UncheckedIOException(e);
        }
        return data;
    }

    public static <T> T parseMessageBodyWithEnvelope(byte[] data, RpcMessageParser<T> parser) {
        if (data == null || data.length < 8) {
            throw new IllegalStateException("Missing fixed envelope header");
        }
        ByteBuffer buffer = ByteBuffer.wrap(data, 0, 8).order(ByteOrder.LITTLE_ENDIAN);
        int headerSize = buffer.getInt();
        int bodySize = buffer.getInt();
        if (headerSize < 0 || bodySize < 0 || 8 + headerSize + bodySize > data.length) {
            throw new IllegalStateException("Corrupted fixed envelope header");
        }
        try {
            CodedInputStream input = CodedInputStream.newInstance(data, 8, headerSize);
            TSerializedMessageEnvelope header = TSerializedMessageEnvelope.parseFrom(input);
            if (header.getCodec() != 0) {
                throw new IllegalStateException(
                        "Compression codecs are not supported: message body has codec=" + header.getCodec());
            }
            input = CodedInputStream.newInstance(data, 8 + headerSize, bodySize);
            return parser.parse(input);
        } catch (IOException e) {
            throw new UncheckedIOException(e);
        }
    }

    public static List<byte[]> createRequestMessage(TRequestHeader header, MessageLite body,
            List<byte[]> attachments)
    {
        List<byte[]> message = new ArrayList<>(2 + attachments.size());
        message.add(createMessageHeader(RpcMessageType.REQUEST, header));
        message.add(createMessageBodyWithEnvelope(body, 0));
        message.addAll(attachments);
        return message;
    }

    public static List<byte[]> createCancelMessage(TRequestCancelationHeader header) {
        return Collections.singletonList(createMessageHeader(RpcMessageType.CANCEL, header));
    }

    public static List<ByteBuf> createResponseMessage(TResponseHeader header, MessageLite body,
            List<byte[]> attachments)
    {
        throw new UnsupportedOperationException("There is no server support");
    }

    /**
     * Возвращает CompletableFuture, завершённое с ошибкой ex
     */
    public static <T> CompletableFuture<T> failedFuture(Throwable ex) {
        CompletableFuture<T> future = new CompletableFuture<>();
        future.completeExceptionally(ex);
        return future;
    }

    /**
     * Транслирует результат src в соответствующий результат dst
     */
    public static <T> void relay(CompletableFuture<T> src, CompletableFuture<? super T> dst) {
        if (src.isDone()) {
            // Экономим стек и транслируем результат напрямую
            if (!dst.isDone()) {
                try {
                    dst.complete(src.get());
                } catch (Throwable e) {
                    dst.completeExceptionally(e);
                }
            }
        } else {
            src.whenComplete((result, failure) -> {
                if (!dst.isDone()) {
                    if (failure != null) {
                        dst.completeExceptionally(failure);
                    } else {
                        dst.complete(result);
                    }
                }
            });
        }
    }

    /**
     * Добавляет вызов dst.cancel(false) в случае завершения src
     */
    public static <T> void relayCancel(CompletableFuture<T> src, Future<?> dst) {
        if (src.isDone()) {
            if (!dst.isDone()) {
                dst.cancel(false);
            }
        } else {
            src.whenComplete((ignoredResult, ignoredFailure) -> {
                if (!dst.isDone()) {
                    dst.cancel(false);
                }
            });
        }
    }

    /**
     * Транслирует результат применения fn.apply(value) в результат для f
     */
    public static <T, U> void relayApply(CompletableFuture<U> f, T value, Throwable exception,
            Function<? super T, ? extends U> fn)
    {
        if (!f.isDone()) {
            if (exception != null) {
                f.completeExceptionally(exception);
            } else {
                try {
                    f.complete(fn.apply(value));
                } catch (Throwable e) {
                    f.completeExceptionally(e);
                }
            }
        }
    }

    /**
     * Аналогично f.thenApply(fn), но с дополнениями:
     * <p>
     * - Автоматически вызывается cancel(false) на f
     */
    public static <T, U> CompletableFuture<U> apply(CompletableFuture<T> f, Function<? super T, ? extends U> fn) {
        CompletableFuture<U> result = new CompletableFuture<>();
        if (f.isDone()) {
            // Экономим стек и вызываем функцию напрямую
            try {
                result.complete(fn.apply(f.get()));
            } catch (Throwable e) {
                result.completeExceptionally(e);
            }
        } else {
            f.whenComplete((r, e) -> relayApply(result, r, e, fn));
        }
        relayCancel(result, f);
        return result;
    }

    /**
     * Аналогично f.thenApplyAsync(fn, executor), но с дополнениями:
     * <p>
     * - Автоматически вызывается cancel(false) на f
     */
    public static <T, U> CompletableFuture<U> applyAsync(CompletableFuture<T> f, Function<? super T, ? extends U> fn,
            Executor executor)
    {
        CompletableFuture<U> result = new CompletableFuture<>();
        f.whenCompleteAsync((r, e) -> relayApply(result, r, e, fn), executor);
        relayCancel(result, f);
        return result;
    }

    /**
     * Аналогично f.thenCompose(fn), но с дополнениями:
     * <p>
     * - Автоматически вызывается cancel(false) на результате fn
     * - Если cancelF == true, то автоматически вызывается cancel(false) на f
     */
    public static <T, U> CompletableFuture<U> compose(CompletableFuture<T> f,
            Function<? super T, ? extends CompletionStage<U>> fn, boolean cancelF)
    {
        CompletableFuture<U> result = new CompletableFuture<>();
        if (f.isDone()) {
            // Экономим стек и вызываем функцию напрямую
            try {
                CompletableFuture<U> intermediate = fn.apply(f.get()).toCompletableFuture();
                relay(intermediate, result);
                relayCancel(result, intermediate);
            } catch (Throwable e) {
                result.completeExceptionally(e);
            }
        } else {
            f.whenComplete((r, e) -> {
                if (!result.isDone()) {
                    if (e != null) {
                        result.completeExceptionally(e);
                    } else {
                        try {
                            CompletableFuture<U> intermediate = fn.apply(r).toCompletableFuture();
                            relay(intermediate, result);
                            relayCancel(result, intermediate);
                        } catch (Throwable e2) {
                            result.completeExceptionally(e2);
                        }
                    }
                }
            });
            if (cancelF) {
                relayCancel(result, f);
            }
        }
        return result;
    }

    /**
     * Аналогично f.thenCompose(fn), но f и результат fn отменяются в случае отмены всей операции
     */
    public static <T, U> CompletableFuture<U> compose(CompletableFuture<T> f,
            Function<? super T, ? extends CompletionStage<U>> fn)
    {
        return compose(f, fn, true);
    }

    /**
     * Аналогично f.thenCompose(fn), но результат fn отменяется в случае отмены всей операции
     */
    public static <T, U> CompletableFuture<U> composeWithInnerCancel(CompletableFuture<T> f,
            Function<? super T, ? extends CompletionStage<U>> fn)
    {
        return compose(f, fn, false);
    }

    /**
     * Конвертирует Duration в микросекунды для yt
     */
    public static long durationToMicros(Duration duration) {
        long micros = Math.multiplyExact(duration.getSeconds(), MICROS_PER_SECOND);
        micros = Math.addExact(micros, duration.getNano() / NANOS_PER_MICROSECOND);
        return micros;
    }

    /**
     * Конвертирует микросекунды yt в Duration
     */
    public static Duration durationFromMicros(long micros) {
        long seconds = micros / MICROS_PER_SECOND;
        long nanos = (micros % MICROS_PER_SECOND) * NANOS_PER_MICROSECOND;
        return Duration.ofSeconds(seconds, nanos);
    }

    /**
     * Конвертирует Instant в микросекунды для yt
     */
    public static long instantToMicros(Instant instant) {
        long micros = Math.multiplyExact(instant.getEpochSecond(), MICROS_PER_SECOND);
        micros = Math.addExact(micros, instant.getNano() / NANOS_PER_MICROSECOND);
        return micros;
    }

    /**
     * Конвертирует микросекунды yt в Instant
     */
    public static Instant instantFromMicros(long micros) {
        long seconds = micros / MICROS_PER_SECOND;
        long nanos = (micros % MICROS_PER_SECOND) * NANOS_PER_MICROSECOND;
        return Instant.ofEpochSecond(seconds, nanos);
    }

    public static TGuid toProto(GUID guid) {
        return TGuid.newBuilder().setFirst(guid.getFirst()).setSecond(guid.getSecond()).build();
    }

    public static GUID fromProto(TGuidOrBuilder guid) {
        return new GUID(guid.getFirst(), guid.getSecond());
    }
}
