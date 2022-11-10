package ru.yandex.yt.ytclient.request;

import java.util.Objects;

import ru.yandex.yt.rpcproxy.TMasterReadOptions;
import ru.yandex.yt.rpcproxy.TPrerequisiteOptions;
import ru.yandex.yt.rpcproxy.TReqExistsNode;
import ru.yandex.yt.rpcproxy.TSuppressableAccessTrackingOptions;
import ru.yandex.yt.rpcproxy.TTransactionalOptions;
import ru.yandex.yt.ytclient.proxy.request.MasterReadOptions;
import ru.yandex.yt.ytclient.proxy.request.PrerequisiteOptions;
import ru.yandex.yt.ytclient.proxy.request.SuppressableAccessTrackingOptions;
import ru.yandex.yt.ytclient.proxy.request.TransactionalOptions;
import ru.yandex.yt.ytclient.rpc.RpcClientRequestBuilder;

public class ExistsNode
        extends GetLikeReq<ExistsNode.Builder, ExistsNode>
        implements HighLevelRequest<TReqExistsNode.Builder> {
    public ExistsNode(BuilderBase<?> builder) {
        super(builder);
    }

    public static Builder builder() {
        return new Builder();
    }

    /**
     * Internal method: prepare request to send over network.
     */
    @Override
    public void writeTo(RpcClientRequestBuilder<TReqExistsNode.Builder, ?> builder) {
        builder.body().setPath(path.toString());
        if (transactionalOptions != null) {
            builder.body().setTransactionalOptions(transactionalOptions.writeTo(TTransactionalOptions.newBuilder()));
        }
        if (prerequisiteOptions != null) {
            builder.body().setPrerequisiteOptions(prerequisiteOptions.writeTo(TPrerequisiteOptions.newBuilder()));
        }
        if (masterReadOptions != null) {
            builder.body().setMasterReadOptions(masterReadOptions.writeTo(TMasterReadOptions.newBuilder()));
        }
        if (suppressableAccessTrackingOptions != null) {
            builder.body().setSuppressableAccessTrackingOptions(
                    suppressableAccessTrackingOptions.writeTo(TSuppressableAccessTrackingOptions.newBuilder())
            );
        }
        if (additionalData != null) {
            builder.body().mergeFrom(additionalData);
        }
    }

    @Override
    public Builder toBuilder() {
        return builder()
                .setPath(path)
                .setAttributes(attributes)
                .setMaxSize(maxSize)
                .setMasterReadOptions(masterReadOptions != null
                        ? new MasterReadOptions(masterReadOptions)
                        : null)
                .setSuppressableAccessTrackingOptions(suppressableAccessTrackingOptions != null
                        ? new SuppressableAccessTrackingOptions(suppressableAccessTrackingOptions)
                        : null)
                .setTransactionalOptions(transactionalOptions != null
                        ? new TransactionalOptions(transactionalOptions)
                        : null)
                .setPrerequisiteOptions(prerequisiteOptions != null
                        ? new PrerequisiteOptions(prerequisiteOptions)
                        : null)
                .setTimeout(timeout)
                .setRequestId(requestId)
                .setUserAgent(userAgent)
                .setTraceId(traceId, traceSampled)
                .setAdditionalData(additionalData);
    }

    public static class Builder extends BuilderBase<Builder> {
        @Override
        protected Builder self() {
            return this;
        }
    }

    public abstract static class BuilderBase<
            TBuilder extends BuilderBase<TBuilder>>
            extends GetLikeReq.Builder<TBuilder, ExistsNode>
            implements HighLevelRequest<TReqExistsNode.Builder> {
        public BuilderBase() {
        }

        public BuilderBase(BuilderBase<?> builder) {
            super(builder);
        }

        /**
         * Internal method: prepare request to send over network.
         */
        @Override
        public void writeTo(RpcClientRequestBuilder<TReqExistsNode.Builder, ?> builder) {
            builder.body().setPath(Objects.requireNonNull(path).toString());
            if (transactionalOptions != null) {
                builder.body().setTransactionalOptions(
                        transactionalOptions.writeTo(TTransactionalOptions.newBuilder()));
            }
            if (prerequisiteOptions != null) {
                builder.body().setPrerequisiteOptions(prerequisiteOptions.writeTo(TPrerequisiteOptions.newBuilder()));
            }
            if (masterReadOptions != null) {
                builder.body().setMasterReadOptions(masterReadOptions.writeTo(TMasterReadOptions.newBuilder()));
            }
            if (suppressableAccessTrackingOptions != null) {
                builder.body().setSuppressableAccessTrackingOptions(
                        suppressableAccessTrackingOptions.writeTo(TSuppressableAccessTrackingOptions.newBuilder())
                );
            }
            if (additionalData != null) {
                builder.body().mergeFrom(additionalData);
            }
        }

        @Override
        public ExistsNode build() {
            return new ExistsNode(this);
        }
    }
}
