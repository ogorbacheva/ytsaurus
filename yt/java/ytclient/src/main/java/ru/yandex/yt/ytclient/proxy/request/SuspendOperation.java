package ru.yandex.yt.ytclient.proxy.request;

import ru.yandex.inside.yt.kosher.common.GUID;
import ru.yandex.inside.yt.kosher.impl.ytree.builder.YTreeBuilder;
import ru.yandex.lang.NonNullApi;
import ru.yandex.lang.NonNullFields;
import ru.yandex.yt.rpcproxy.TReqSuspendOperation;
import ru.yandex.yt.ytclient.rpc.RpcClientRequestBuilder;
import ru.yandex.yt.ytclient.rpc.RpcUtil;

/**
 * Request for suspending operation
 *
 * @see <a href="https://docs.yandex-team.ru/yt/api/commands#suspend_operation">
 *     suspend_operation documentation
 *     </a>
 * @see ResumeOperation
 */
@NonNullApi
@NonNullFields
public class SuspendOperation extends OperationReq<SuspendOperation>
        implements HighLevelRequest<TReqSuspendOperation.Builder> {

    private boolean abortRunningJobs;

    /**
     * Construct request from operation id.
     */
    public SuspendOperation(GUID operationId) {
        super(operationId, null);
    }

    SuspendOperation(String operationAlias) {
        super(null, operationAlias);
    }

    /**
     * Construct request from operation alias
     */
    public static SuspendOperation fromAlias(String operationAlias) {
        return new SuspendOperation(operationAlias);
    }

    /**
     * Abort running jobs
     */
    public SuspendOperation setAbortRunningJobs(boolean abortRunningJobs) {
        this.abortRunningJobs = abortRunningJobs;
        return this;
    }

    @Override
    public void writeTo(RpcClientRequestBuilder<TReqSuspendOperation.Builder, ?> builder) {
        TReqSuspendOperation.Builder messageBuilder = builder.body();
        if (operationId != null) {
            messageBuilder.setOperationId(RpcUtil.toProto(operationId));
        } else {
            assert operationAlias != null;
            messageBuilder.setOperationAlias(operationAlias);
        }
        if (abortRunningJobs) {
            messageBuilder.setAbortRunningJobs(abortRunningJobs);
        }
    }

    public YTreeBuilder toTree(YTreeBuilder builder) {
        super.toTree(builder);
        if (abortRunningJobs) {
            builder.key("abort_running_jobs").value(abortRunningJobs);
        }
        return builder;
    }

    @Override
    protected SuspendOperation self() {
        return this;
    }

    @Override
    public SuspendOperation build() {
        throw new RuntimeException("unimplemented build() method");
    }
}
