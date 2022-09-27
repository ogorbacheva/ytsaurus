package ru.yandex.yt.ytclient.proxy.request;

import ru.yandex.inside.yt.kosher.common.GUID;
import ru.yandex.inside.yt.kosher.impl.ytree.builder.YTreeBuilder;
import ru.yandex.lang.NonNullApi;
import ru.yandex.lang.NonNullFields;
import ru.yandex.yt.rpcproxy.TReqResumeOperation;
import ru.yandex.yt.ytclient.rpc.RpcClientRequestBuilder;

/**
 * Request for resuming suspended operation
 *
 * @see <a href="https://docs.yandex-team.ru/yt/api/commands#suspend_operation">
 *     resume_operation documentation
 *     </a>
 * @see SuspendOperation
 */
@NonNullApi
@NonNullFields
public class ResumeOperation extends OperationReq<ResumeOperation>
        implements HighLevelRequest<TReqResumeOperation.Builder> {

    public ResumeOperation(GUID operationId) {
        super(operationId, null);
    }

    ResumeOperation(String operationAlias) {
        super(null, operationAlias);
    }

    public static ResumeOperation fromAlias(String alias) {
        return new ResumeOperation(alias);
    }

    public YTreeBuilder toTree(YTreeBuilder builder) {
        return super.toTree(builder);
    }

    @Override
    public void writeTo(RpcClientRequestBuilder<TReqResumeOperation.Builder, ?> builder) {
        TReqResumeOperation.Builder messageBuilder = builder.body();
        writeOperationDescriptionToProto(messageBuilder::setOperationId, messageBuilder::setOperationAlias);
    }

    @Override
    protected ResumeOperation self() {
        return this;
    }

    @Override
    public ResumeOperation build() {
        throw new RuntimeException("unimplemented build() method");
    }
}
