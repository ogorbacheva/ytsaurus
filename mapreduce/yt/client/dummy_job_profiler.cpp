#include "job_profiler.h"

namespace NYT {

////////////////////////////////////////////////////////////////////////////////

class TDummyJobProfiler
    : public IJobProfiler
{
    void Start() override
    { }

    void Stop() override
    { }
};

////////////////////////////////////////////////////////////////////////////////

std::unique_ptr<IJobProfiler> CreateJobProfiler()
{
    return std::make_unique<TDummyJobProfiler>();
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT
