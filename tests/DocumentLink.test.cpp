#include "doctest.h"
#include "Fixture.h"

#include <algorithm>

TEST_SUITE_BEGIN("DocumentLink");

TEST_CASE_FIXTURE(Fixture, "document_link_for_roblox_require_path")
{
    loadSourcemap(R"({
        "name": "Game",
        "className": "DataModel",
        "children": [
            {
                "name": "ReplicatedStorage",
                "className": "ReplicatedStorage",
                "children": [{ "name": "Test", "className": "ModuleScript", "filePaths": ["source.luau"] }]
            }
        ]
    })");

    auto document = newDocument("main.luau", R"(
        local X = require(game.ReplicatedStorage.Test)
    )");

    auto params = lsp::DocumentLinkParams{};
    params.textDocument = lsp::TextDocumentIdentifier{document};

    auto result = workspace.documentLink(params);
    REQUIRE_EQ(result.size(), 1);
    CHECK_EQ(result[0].target, workspace.rootUri.resolvePath("source.luau"));
}

#ifdef ORDER_STRING_REQUIRE
TEST_CASE_FIXTURE(Fixture, "document_link_for_order_shared_call")
{
    loadSourcemap(R"({
        "name": "Game",
        "className": "DataModel",
        "children": [
            {
                "name": "ServerStorage",
                "className": "ServerStorage",
                "children": [{ "name": "TestModule", "className": "ModuleScript", "filePaths": ["testmodule.luau"] }]
            }
        ]
    })");

    auto document = newDocument("main.luau", R"(
        local X = shared("TestModule")
    )");

    auto params = lsp::DocumentLinkParams{};
    params.textDocument = lsp::TextDocumentIdentifier{document};

    auto result = workspace.documentLink(params);
    REQUIRE_EQ(result.size(), 1);
    CHECK_EQ(result[0].target, workspace.rootUri.resolvePath("testmodule.luau"));
}

TEST_CASE_FIXTURE(Fixture, "document_link_multiple_calls_require_and_shared")
{
    loadSourcemap(R"({
        "name": "Game",
        "className": "DataModel",
        "children": [
            {
                "name": "ReplicatedStorage",
                "className": "ReplicatedStorage",
                "children": [
                    { "name": "ModuleA", "className": "ModuleScript", "filePaths": ["modulea.luau"] },
                    { "name": "ModuleB", "className": "ModuleScript", "filePaths": ["moduleb.luau"] }
                ]
            }
        ]
    })");

    auto document = newDocument("main.luau", R"(
        local A = require(game.ReplicatedStorage.ModuleA)
        local B = shared("ModuleB")
    )");

    auto params = lsp::DocumentLinkParams{};
    params.textDocument = lsp::TextDocumentIdentifier{document};

    auto result = workspace.documentLink(params);
    REQUIRE_EQ(result.size(), 2);

    // Collect targets (order may vary)
    std::vector<lsp::DocumentUri> targets;
    for (auto& link : result)
        targets.push_back(link.target);

    CHECK(std::find(targets.begin(), targets.end(), workspace.rootUri.resolvePath("modulea.luau")) != targets.end());
    CHECK(std::find(targets.begin(), targets.end(), workspace.rootUri.resolvePath("moduleb.luau")) != targets.end());
}
#endif

TEST_SUITE_END();
