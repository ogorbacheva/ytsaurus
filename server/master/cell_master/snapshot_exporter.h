#pragma once

#include "bootstrap.h"

namespace NYT::NCellMaster {

/////////////////////////////////////////////////////////////////////////////

void ExportSnapshot(TBootstrap* bootstrap, const TString& snapshotPath, const TString& configPath);

/////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NCellMaster
