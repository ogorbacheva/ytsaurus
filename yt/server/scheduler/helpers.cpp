#include "stdafx.h"
#include "helpers.h"
#include "operation.h"
#include "job.h"
#include "exec_node.h"
#include "operation_controller.h"
#include "job_resources.h"

#include <ytlib/ytree/fluent.h>

namespace NYT {
namespace NScheduler {

using namespace NYTree;
using namespace NTransactionClient;

////////////////////////////////////////////////////////////////////

void BuildOperationAttributes(TOperationPtr operation, NYson::IYsonConsumer* consumer)
{
    auto userTransaction = operation->GetUserTransaction();
    auto syncTransaction = operation->GetSyncSchedulerTransaction();
    auto asyncTransaction = operation->GetAsyncSchedulerTransaction();
    auto inputTransaction = operation->GetInputTransaction();
    auto outputTransaction = operation->GetOutputTransaction();
    BuildYsonMapFluently(consumer)
        .Item("operation_type").Value(operation->GetType())
        .Item("user_transaction_id").Value(userTransaction ? userTransaction->GetId() : NullTransactionId)
        .Item("sync_scheduler_transaction_id").Value(syncTransaction ? syncTransaction->GetId() : NullTransactionId)
        .Item("async_scheduler_transaction_id").Value(asyncTransaction ? asyncTransaction->GetId() : NullTransactionId)
        .Item("input_transaction_id").Value(inputTransaction ? inputTransaction->GetId() : NullTransactionId)
        .Item("output_transaction_id").Value(outputTransaction ? outputTransaction->GetId() : NullTransactionId)
        .Item("state").Value(FormatEnum(operation->GetState()))
        .Item("start_time").Value(operation->GetStartTime())
        .Item("spec").Node(operation->GetSpec());
}

void BuildJobAttributes(TJobPtr job, NYson::IYsonConsumer* consumer)
{
    auto state = job->GetState();
    BuildYsonMapFluently(consumer)
        .Item("job_type").Value(FormatEnum(job->GetType()))
        .Item("state").Value(FormatEnum(state))
        .Item("address").Value(job->GetNode()->GetAddress())
        .Item("start_time").Value(job->GetStartTime())
        .DoIf(job->GetFinishTime(), [=] (TFluentMap fluent) {
            fluent.Item("finish_time").Value(job->GetFinishTime().Get());
        })
        .DoIf(state == EJobState::Failed, [=] (TFluentMap fluent) {
            auto error = FromProto(job->Result().error());
            fluent.Item("error").Value(error);
        });
}

void BuildExecNodeAttributes(TExecNodePtr node, NYson::IYsonConsumer* consumer)
{
    BuildYsonMapFluently(consumer)
        .Item("resource_usage").Value(node->ResourceUsage())
        .Item("resource_limits").Value(node->ResourceLimits());
}

////////////////////////////////////////////////////////////////////////////////

i64 Clamp(i64 value, i64 minValue, i64 maxValue)
{
    value = std::min(value, maxValue);
    value = std::max(value, minValue);
    return value;
}

////////////////////////////////////////////////////////////////////

} // namespace NScheduler
} // namespace NYT

