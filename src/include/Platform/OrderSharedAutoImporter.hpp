#pragma once

#ifdef ORDER_STRING_REQUIRE

#include "Luau/Frontend.h"
#include "Platform/AutoImports.hpp"
#include "Platform/InstanceRequireAutoImporter.hpp" // for RobloxFindImportsVisitor
#include "LSP/Workspace.hpp"

#include <functional>
#include <optional>
#include <string>
#include <vector>

class RobloxPlatform;

namespace Luau::LanguageServer::AutoImports
{

struct OrderSharedAutoImporterContext
{
    Luau::ModuleName from;
    Luau::NotNull<const TextDocument> textDocument;

    Luau::NotNull<const WorkspaceFolder> workspaceFolder;
    Luau::NotNull<const ClientCompletionImportsConfiguration> config;

    size_t hotCommentsLineNumber = 0;
    Luau::NotNull<const RobloxFindImportsVisitor> importsVisitor;

    Luau::NotNull<const RobloxPlatform> platform;

    std::optional<std::function<bool(const std::string&)>> moduleFilter;
};

/// Result of computing a `shared("ModuleName")` auto-import.
struct OrderSharedRequireResult
{
    std::string variableName;    // local-binding name, e.g. "Util"
    Luau::ModuleName moduleName; // virtual path of the target module
    std::string sharedName;      // string inside shared("...")
    lsp::TextEdit edit;          // the actual TextEdit
};

std::vector<OrderSharedRequireResult> computeAllSharedRequires(const OrderSharedAutoImporterContext& ctx);
void suggestSharedRequires(const OrderSharedAutoImporterContext& ctx, std::vector<lsp::CompletionItem>& items);

} // namespace Luau::LanguageServer::AutoImports

#endif
