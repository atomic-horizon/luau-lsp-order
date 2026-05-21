#include "Platform/OrderSharedAutoImporter.hpp"

#ifdef ORDER_STRING_REQUIRE

#include "Platform/RobloxPlatform.hpp"
#include "LSP/Completion.hpp"

namespace Luau::LanguageServer::AutoImports
{

std::vector<OrderSharedRequireResult> computeAllSharedRequires(const OrderSharedAutoImporterContext& ctx)
{
    std::vector<OrderSharedRequireResult> results;
    size_t minimumLineNumber = computeMinimumLineNumberForRequire(*ctx.importsVisitor, ctx.hotCommentsLineNumber);

    ScriptContext callerContext = ScriptContext::Shared;
    if (auto it = ctx.platform->virtualPathsToSourceNodes.find(ctx.from); it != ctx.platform->virtualPathsToSourceNodes.end())
        callerContext = it->second->scriptContext;

    for (auto& [path, node] : ctx.platform->virtualPathsToSourceNodes)
    {
        auto variableName = AutoImports::makeValidVariableName(node->name);

        if (ctx.moduleFilter && !(*ctx.moduleFilter)(variableName))
            continue;

        if (path == ctx.from || node->className != "ModuleScript" || ctx.importsVisitor->containsRequire(variableName))
            continue;

        if (auto scriptFilePath = ctx.platform->getRealPathFromSourceNode(node);
            scriptFilePath && ctx.workspaceFolder->isIgnoredFileForAutoImports(*scriptFilePath))
            continue;

        if (!isScriptContextCompatible(callerContext, node->scriptContext))
            continue;

        const std::string& sharedName = node->name;

        size_t lineNumber = computeBestLineForRequire(*ctx.importsVisitor, *ctx.textDocument, sharedName, minimumLineNumber);
        bool prependNewline = ctx.config->separateGroupsWithLine && ctx.importsVisitor->shouldPrependNewline(lineNumber);

        results.emplace_back(OrderSharedRequireResult{
            variableName,
            path,
            sharedName,
            createSharedRequireTextEdit(variableName, sharedName, lineNumber, prependNewline, ctx.config->useConst),
        });
    }

    return results;
}

void suggestSharedRequires(const OrderSharedAutoImporterContext& ctx, std::vector<lsp::CompletionItem>& items)
{
    for (const auto& result : computeAllSharedRequires(ctx))
    {
        std::vector<lsp::TextEdit> edits{result.edit};
        std::string displayRequire = "shared(\"" + result.sharedName + "\")";
        items.emplace_back(
            createSuggestRequire(result.variableName, edits, SortText::AutoImports, result.moduleName, displayRequire));
    }
}

} // namespace Luau::LanguageServer::AutoImports

#endif
