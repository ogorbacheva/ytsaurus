#include "format_manager.h"

#include "config.h"

#include <yt/client/api/public.h>

#include <yt/client/security_client/public.h>

#include <yt/core/ytree/helpers.h>
#include <yt/core/ytree/fluent.h>

namespace NYT {

using namespace NFormats;
using namespace NScheduler;
using namespace NYson;
using namespace NYTree;

////////////////////////////////////////////////////////////////////////////////

TFormatManager::TFormatManager(
    THashMap<EFormatType, TFormatConfigPtr> formatConfigs,
    TString authenticatedUser)
    : FormatConfigs_(std::move(formatConfigs))
    , AuthenticatedUser_(std::move(authenticatedUser))
{ }

void TFormatManager::ValidateAndPatchOperationSpec(
    const INodePtr& specNode,
    EOperationType operationType) const
{
    if (!specNode || specNode->GetType() != ENodeType::Map) {
        return;
    }

    auto spec = specNode->AsMap();

    auto processFormatByPath = [&] (const TYPath& taskPath, const IMapNodePtr& taskSpecMap, const TYPath& path) {
        auto formatNode = FindNodeByYPath(taskSpecMap, path);
        if (!formatNode || formatNode->GetType() != ENodeType::String) {
            return;
        }
        auto origin = Format("%v%v in %Qlv operation spec", taskPath, path, operationType);
        ValidateAndPatchFormatNode(formatNode, origin);
    };

    auto processTask = [&] (const TYPath& taskPath) {
        auto taskSpec = FindNodeByYPath(spec, taskPath);
        if (!taskSpec || taskSpec->GetType() != ENodeType::Map) {
            return;
        }
        auto taskSpecMap = taskSpec->AsMap();

        processFormatByPath(taskPath, taskSpecMap, "/format");
        processFormatByPath(taskPath, taskSpecMap, "/input_format");
        processFormatByPath(taskPath, taskSpecMap, "/output_format");

        auto filePathsNode = taskSpecMap->FindChild("file_paths");
        if (!filePathsNode || filePathsNode->GetType() != ENodeType::List) {
            return;
        }
        const auto& filePathNodes = filePathsNode->AsList()->GetChildren();
        for (int i = 0; i < static_cast<int>(filePathNodes.size()); ++i) {
            const auto& filePathNode = filePathNodes[i];
            auto formatNode = filePathNode->MutableAttributes()->Find<INodePtr>("format");
            if (!formatNode) {
                continue;
            }
            auto origin = Format("%v/file_paths/%d/@format in %Qlv operation spec", taskPath, i, operationType);
            ValidateAndPatchFormatNode(formatNode, origin);
            filePathNode->MutableAttributes()->Set("format", std::move(formatNode));
        }
    };

    switch (operationType) {
        case EOperationType::Map:
            processTask("/mapper");
            return;
        case EOperationType::Reduce:
        case EOperationType::JoinReduce:
            processTask("/reducer");
            return;
        case EOperationType::MapReduce:
            processTask("/mapper");
            processTask("/reducer");
            processTask("/reduce_combiner");
            return;
        case EOperationType::Vanilla: {
            auto tasks = spec->FindChild("tasks");
            if (!tasks || tasks->GetType() != ENodeType::Map) {
                return;
            }
            for (const auto& [name, taskSpec]: tasks->AsMap()->GetChildren()) {
                processTask("/tasks/" + name);
            }
            return;
        }
        default:
            return; // Do nothing.
    }
}

void TFormatManager::ValidateAndPatchFormatNode(const INodePtr& formatNode, const TString& origin) const
{
    EFormatType formatType;
    try {
        formatType = ConvertTo<EFormatType>(formatNode);
    } catch (const TErrorException& error) {
        return;
    }

    auto it = FormatConfigs_.find(formatType);
    if (it == FormatConfigs_.end()) {
        return;
    }
    const auto& rootConfig = it->second;
    auto formatConfig = static_cast<TFormatConfigBasePtr>(rootConfig);
    auto userOverride = false;
    auto userIt = rootConfig->UserOverrides.find(AuthenticatedUser_);
    if (userIt != rootConfig->UserOverrides.end()) {
        formatConfig = userIt->second;
        userOverride = true;
    }

    if (AuthenticatedUser_ != NSecurityClient::RootUserName && !formatConfig->Enable) {
        TString errorMessage;
        if (userOverride) {
            errorMessage = Format("Format %Qv is disabled for user %v", formatType, AuthenticatedUser_);
        } else {
            errorMessage = Format("Format %Qv is disabled", formatType);
        }
        THROW_ERROR_EXCEPTION(NApi::EErrorCode::FormatDisabled, errorMessage)
            << TErrorAttribute("origin", origin);
    }

    const auto& defaultAttributes = formatConfig->DefaultAttributes;
    auto* attributes = formatNode->MutableAttributes();
    for (const auto& [key, defaultValue] : defaultAttributes->GetChildren()) {
        auto value = attributes->FindYson(key);
        if (!value) {
            attributes->SetYson(key, ConvertToYsonString(defaultValue));
            continue;
        }
        auto node = ConvertToNode(value);
        node = PatchNode(defaultValue, node);
        attributes->SetYson(key, ConvertToYsonString(node));
    }
}

TFormat TFormatManager::ConvertToFormat(const INodePtr& formatNode, const TString& origin) const
{
    ValidateAndPatchFormatNode(formatNode, origin);
    try {
        return ConvertTo<TFormat>(formatNode);
    } catch (const TErrorException& error) {
        THROW_ERROR_EXCEPTION("Failed to parse format")
            << error
            << TErrorAttribute("origin", origin);
    }
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT

