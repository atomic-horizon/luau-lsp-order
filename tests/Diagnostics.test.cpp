#include "doctest.h"
#include "Fixture.h"
#include "Platform/RobloxPlatform.hpp"

TEST_SUITE_BEGIN("Diagnostics");

TEST_CASE_FIXTURE(Fixture, "document_diagnostics_sends_information_for_required_modules")
{
    client->capabilities.textDocument = lsp::TextDocumentClientCapabilities{};
    client->capabilities.textDocument->diagnostic = lsp::DiagnosticClientCapabilities{};
    client->capabilities.textDocument->diagnostic->relatedDocumentSupport = true;

    // Don't show diagnostic for game indexing
    loadDefinition("@extra", "declare game: any");

    registerDocumentForVirtualPath(newDocument("required.luau", R"(
        --!strict
        local x: string = 1
        return {}
    )"),
        "game/Testing/Required");
    auto document = newDocument("main.luau", R"(
        --!strict
        require(game.Testing.Required)
    )");

    auto diagnostics = workspace.documentDiagnostics(lsp::DocumentDiagnosticParams{{document}}, nullptr);
    CHECK_EQ(diagnostics.items.size(), 0);
    CHECK_EQ(diagnostics.relatedDocuments.size(), 1);
}

TEST_CASE_FIXTURE(Fixture, "document_diagnostics_does_not_send_information_for_required_modules_if_related_document_support_is_disabled")
{
    client->capabilities.textDocument = lsp::TextDocumentClientCapabilities{};
    client->capabilities.textDocument->diagnostic = lsp::DiagnosticClientCapabilities{};
    client->capabilities.textDocument->diagnostic->relatedDocumentSupport = false;

    // Don't show diagnostic for game indexing
    loadDefinition("@extra", "declare game: any");

    registerDocumentForVirtualPath(newDocument("required.luau", R"(
        --!strict
        local x: string = 1
        return {}
    )"),
        "game/Testing/Required");
    auto document = newDocument("main.luau", R"(
        --!strict
        require(game.Testing.Required)
    )");

    auto diagnostics = workspace.documentDiagnostics(lsp::DocumentDiagnosticParams{{document}}, nullptr);
    CHECK_EQ(diagnostics.items.size(), 0);
    CHECK_EQ(diagnostics.relatedDocuments.size(), 0);
}

TEST_CASE_FIXTURE(Fixture, "text_document_update_marks_dependent_files_as_dirty")
{
    auto firstDocument = newDocument("a.luau", R"(
        --!strict
        return { hello = true }
    )");
    auto secondDocument = newDocument("b.luau", R"(
        --!strict
        local a = require("./a.luau")
        print(a.hello)
    )");

    auto diagnosticsA = workspace.documentDiagnostics(lsp::DocumentDiagnosticParams{{firstDocument}}, nullptr);
    CHECK_EQ(diagnosticsA.items.size(), 0);

    auto diagnosticsB = workspace.documentDiagnostics(lsp::DocumentDiagnosticParams{{secondDocument}}, nullptr);
    CHECK_EQ(diagnosticsB.items.size(), 0);

    // We should see diagnostics in the dependent file after the update request
    updateDocument(firstDocument, R"(
        --!strict
        return { hello2 = true }
    )");

    diagnosticsA = workspace.documentDiagnostics(lsp::DocumentDiagnosticParams{{firstDocument}}, nullptr);
    CHECK_EQ(diagnosticsA.items.size(), 0);

    diagnosticsB = workspace.documentDiagnostics(lsp::DocumentDiagnosticParams{{secondDocument}}, nullptr);
    CHECK_EQ(diagnosticsB.items.size(), 1);
    CHECK_EQ(diagnosticsB.items[0].message, "TypeError: Key 'hello' not found in table '{ hello2: boolean }'");
}

TEST_CASE_FIXTURE(Fixture, "text_document_update_triggers_dependent_diagnostics_in_push_based_diagnostics")
{
    client->globalConfig.diagnostics.includeDependents = true;

    auto firstDocument = newDocument("a.luau", R"(
        --!strict
        return { hello = true }
    )");
    auto secondDocument = newDocument("b.luau", R"(
        --!strict
        local a = require("./a.luau")
        print(a.hello)
    )");

    // Assumption: documents were already checked
    workspace.documentDiagnostics(lsp::DocumentDiagnosticParams{{firstDocument}}, nullptr);
    workspace.documentDiagnostics(lsp::DocumentDiagnosticParams{{secondDocument}}, nullptr);

    updateDocument(firstDocument, R"(
        --!strict
        return { hello2 = true }
    )");

    REQUIRE(client->notificationQueue.size() > 2);
    auto secondNotification = *client->notificationQueue.rbegin();
    auto firstNotification = *(++client->notificationQueue.rbegin());

    REQUIRE_EQ(firstNotification.first, "textDocument/publishDiagnostics");
    REQUIRE(firstNotification.second);
    lsp::PublishDiagnosticsParams pushedDiagnostics = firstNotification.second.value();
    CHECK_EQ(pushedDiagnostics.uri, firstDocument);
    CHECK_EQ(pushedDiagnostics.diagnostics.size(), 0);

    REQUIRE_EQ(secondNotification.first, "textDocument/publishDiagnostics");
    REQUIRE(secondNotification.second);
    pushedDiagnostics = secondNotification.second.value();
    CHECK_EQ(pushedDiagnostics.uri, secondDocument);
    CHECK_EQ(pushedDiagnostics.diagnostics.size(), 1);
    CHECK_EQ(pushedDiagnostics.diagnostics[0].message, "TypeError: Key 'hello' not found in table '{ hello2: boolean }'");
}

TEST_CASE_FIXTURE(Fixture, "text_document_update_does_not_update_workspace_diagnostics")
{
    client->globalConfig.diagnostics.workspace = true;

    auto firstDocument = newDocument("a.luau", R"(
        --!strict
        return { hello = true }
    )");
    auto secondDocument = newDocument("b.luau", R"(
        --!strict
        local a = require("./a.luau")
        print(a.hello)
    )");

    // Assumption: initial workspace diagnostics was triggered
    // We are using documentDiagnostics to replicate workspace diagnostics checking the file (and making it non-dirty)
    workspace.documentDiagnostics(lsp::DocumentDiagnosticParams{{firstDocument}}, nullptr);
    workspace.documentDiagnostics(lsp::DocumentDiagnosticParams{{secondDocument}}, nullptr);
    client->workspaceDiagnosticsToken = "WORKSPACE-DIAGNOSTICS-PROGRESS-TOKEN";

    updateDocument(firstDocument, R"(
        --!strict
        return { hello2 = true }
    )");

    // Check no workspace diagnostics progress on queue
    for (const auto& notification : client->notificationQueue)
        CHECK_NE(notification.first, "$/progress");
}

TEST_CASE_FIXTURE(Fixture, "text_document_save_auto_updates_workspace_diagnostics_of_dependent_files")
{
    client->globalConfig.diagnostics.workspace = true;

    auto firstDocument = newDocument("a.luau", R"(
        --!strict
        return { hello = true }
    )");
    auto secondDocument = newDocument("b.luau", R"(
        --!strict
        local a = require("./a.luau")
        print(a.hello)
    )");

    // Assumption: initial workspace diagnostics was triggered
    // We are using documentDiagnostics to replicate workspace diagnostics checking the file (and making it non-dirty)
    workspace.documentDiagnostics(lsp::DocumentDiagnosticParams{{firstDocument}}, nullptr);
    workspace.documentDiagnostics(lsp::DocumentDiagnosticParams{{secondDocument}}, nullptr);
    client->workspaceDiagnosticsToken = "WORKSPACE-DIAGNOSTICS-PROGRESS-TOKEN";

    updateDocument(firstDocument, R"(
        --!strict
        return { hello2 = true }
    )");
    workspace.onDidSaveTextDocument(firstDocument, lsp::DidSaveTextDocumentParams{{firstDocument}});

    REQUIRE(!client->notificationQueue.empty());
    auto notification = client->notificationQueue.back();
    REQUIRE_EQ(notification.first, "$/progress");
    REQUIRE(notification.second);

    lsp::ProgressParams progressData = notification.second.value();
    REQUIRE_EQ(progressData.token, client->workspaceDiagnosticsToken.value());

    lsp::WorkspaceDiagnosticReportPartialResult diagnostics = progressData.value;
    REQUIRE_EQ(diagnostics.items.size(), 2);

    auto mainDiagnostics = diagnostics.items[0];
    CHECK_EQ(mainDiagnostics.uri, firstDocument);
    CHECK_EQ(mainDiagnostics.items.size(), 0);

    auto dependentDiagnostics = diagnostics.items[1];
    CHECK_EQ(dependentDiagnostics.uri, secondDocument);
    CHECK_EQ(dependentDiagnostics.items.size(), 1);
    CHECK_EQ(dependentDiagnostics.items[0].message, "TypeError: Key 'hello' not found in table '{ hello2: boolean }'");
}

TEST_CASE_FIXTURE(Fixture, "text_document_save_does_not_update_workspace_diagnostics_if_setting_is_disabled")
{
    client->globalConfig.diagnostics.workspace = false;

    auto firstDocument = newDocument("a.luau", R"(
        --!strict
        return { hello = true }
    )");
    auto secondDocument = newDocument("b.luau", R"(
        --!strict
        local a = require("./a.luau")
        print(a.hello)
    )");

    // Assumption: initial workspace diagnostics was triggered
    // We are using documentDiagnostics to replicate workspace diagnostics checking the file (and making it non-dirty)
    workspace.documentDiagnostics(lsp::DocumentDiagnosticParams{{firstDocument}}, nullptr);
    workspace.documentDiagnostics(lsp::DocumentDiagnosticParams{{secondDocument}}, nullptr);
    client->workspaceDiagnosticsToken = "WORKSPACE-DIAGNOSTICS-PROGRESS-TOKEN";

    updateDocument(firstDocument, R"(
        --!strict
        return { hello2 = true }
    )");
    workspace.onDidSaveTextDocument(firstDocument, lsp::DidSaveTextDocumentParams{{firstDocument}});

    // Check no workspace diagnostics progress on queue
    for (const auto& notification : client->notificationQueue)
        CHECK_NE(notification.first, "$/progress");
}

TEST_CASE_FIXTURE(Fixture, "document_diagnostics_respects_cancellation")
{
    auto cancellationToken = std::make_shared<Luau::FrontendCancellationToken>();
    cancellationToken->cancel();

    auto document = newDocument("a.luau", "local x = 1");
    CHECK_THROWS_AS(workspace.documentDiagnostics(lsp::DocumentDiagnosticParams{{document}}, cancellationToken), RequestCancelledException);
}

#ifdef ORDER_STRING_REQUIRE
TEST_CASE_FIXTURE(Fixture, "shared_call_with_unknown_module_reports_unknown_require")
{
    loadSourcemap(R"({
        "name": "Game",
        "className": "DataModel",
        "children": [
            {
                "name": "ServerScriptService",
                "className": "ServerScriptService",
                "children": [{ "name": "Main", "className": "Script", "filePaths": ["main.luau"] }]
            }
        ]
    })");

    auto document = newDocument("main.luau", R"(
        local _ = shared("MissingModule")
    )");

    auto diagnostics = workspace.documentDiagnostics(lsp::DocumentDiagnosticParams{{document}}, nullptr);
    REQUIRE_EQ(diagnostics.items.size(), 1);
    CHECK_EQ(diagnostics.items[0].message, "TypeError: Unknown require: MissingModule");
}

TEST_CASE_FIXTURE(Fixture, "shared_call_self_require_reports_unknown_require")
{
    loadSourcemap(R"({
        "name": "Game",
        "className": "DataModel",
        "children": [
            {
                "name": "ServerStorage",
                "className": "ServerStorage",
                "children": [{ "name": "Main", "className": "ModuleScript", "filePaths": ["main.luau"] }]
            }
        ]
    })");

    auto document = newDocument("main.luau", R"(
        local _ = shared("Main")
        return {}
    )");

    auto diagnostics = workspace.documentDiagnostics(lsp::DocumentDiagnosticParams{{document}}, nullptr);
    REQUIRE_EQ(diagnostics.items.size(), 1);
    CHECK_EQ(diagnostics.items[0].message, "TypeError: Unknown require: Main");
}

TEST_CASE_FIXTURE(Fixture, "cyclic_shared_requires_are_allowed_and_survive_rechecks")
{
    loadSourcemap(R"({
        "name": "Game",
        "className": "DataModel",
        "children": [
            {
                "name": "ReplicatedStorage",
                "className": "ReplicatedStorage",
                "children": [
                    { "name": "ServiceA", "className": "ModuleScript", "filePaths": ["a.luau"] },
                    { "name": "ServiceB", "className": "ModuleScript", "filePaths": ["b.luau"] }
                ]
            }
        ]
    })");

    auto documentA = newDocument("a.luau", R"(
        local ServiceB = shared("ServiceB")
        return { other = ServiceB }
    )");
    auto documentB = newDocument("b.luau", R"(
        local ServiceA = shared("ServiceA")
        return { other = ServiceA }
    )");

    // Intentional Order service cycles must not surface ModuleHasCyclicDependency (the
    // cyclic edges resolve to `any` instead)
    auto diagnosticsA = workspace.documentDiagnostics(lsp::DocumentDiagnosticParams{{documentA}}, nullptr);
    CHECK_EQ(diagnosticsA.items.size(), 0);
    auto diagnosticsB = workspace.documentDiagnostics(lsp::DocumentDiagnosticParams{{documentB}}, nullptr);
    CHECK_EQ(diagnosticsB.items.size(), 0);

    // Regression test: shared() cycles used to be hidden from getRequireCycles(), which
    // bypassed the requireCycles -> `any` substitution. The first member of the cycle to be
    // rechecked would then embed TypeIds from the stale module of the other member, and once
    // that module was itself rechecked (freeing the old arena) the next read of the fresh
    // graph crashed with an access violation.
    updateDocument(documentA, R"(
        local ServiceB = shared("ServiceB")
        local extra = 1
        return { other = ServiceB, extra = extra }
    )");

    diagnosticsA = workspace.documentDiagnostics(lsp::DocumentDiagnosticParams{{documentA}}, nullptr);
    CHECK_EQ(diagnosticsA.items.size(), 0);
    diagnosticsB = workspace.documentDiagnostics(lsp::DocumentDiagnosticParams{{documentB}}, nullptr);
    CHECK_EQ(diagnosticsB.items.size(), 0);
}

TEST_CASE_FIXTURE(Fixture, "shared_requires_survive_sourcemap_reload")
{
    const std::string sourcemap = R"({
        "name": "Game",
        "className": "DataModel",
        "children": [
            {
                "name": "ReplicatedStorage",
                "className": "ReplicatedStorage",
                "children": [
                    { "name": "TestModule", "className": "ModuleScript", "filePaths": ["mod.luau"] },
                    { "name": "Consumer", "className": "ModuleScript", "filePaths": ["main.luau"] }
                ]
            }
        ]
    })";

    loadSourcemap(sourcemap);

    newDocument("mod.luau", "return { value = 42 }");
    auto document = newDocument("main.luau", R"(
        local TestModule = shared("TestModule")
        return { value = TestModule.value }
    )");

    auto diagnostics = workspace.documentDiagnostics(lsp::DocumentDiagnosticParams{{document}}, nullptr);
    CHECK_EQ(diagnostics.items.size(), 0);

    // Regression test: reloading the sourcemap frees every SourceNode and clears the
    // sourcemap type arena. `shared` used to be re-injected per module scope with its type
    // allocated in that arena and a raw SourceNode pointer captured in its magic function,
    // so requests after a reload could read freed memory.
    loadSourcemap(sourcemap);

    diagnostics = workspace.documentDiagnostics(lsp::DocumentDiagnosticParams{{document}}, nullptr);
    CHECK_EQ(diagnostics.items.size(), 0);
}

TEST_CASE_FIXTURE(Fixture, "shared_place_type_is_string")
{
    auto document = newDocument("main.luau", R"(
        --!strict
        local pt: string = shared.PlaceType
        local bad: number = shared.PlaceType
        print(pt, bad)
    )");

    // Only the string -> number assignment should error, proving PlaceType is `string`, not `any`
    auto diagnostics = workspace.documentDiagnostics(lsp::DocumentDiagnosticParams{{document}}, nullptr);
    CHECK_EQ(diagnostics.items.size(), 1);
}

TEST_CASE_FIXTURE(Fixture, "shared_assets_is_an_instance_type")
{
    // The full Roblox definitions type `shared.Assets` as Folder; the minimal test
    // definitions do not declare Folder, so registration falls back to Instance
    auto document = newDocument("main.luau", R"(
        --!strict
        local assets: Instance = shared.Assets
        print(assets)
    )");

    auto diagnostics = workspace.documentDiagnostics(lsp::DocumentDiagnosticParams{{document}}, nullptr);
    CHECK_EQ(diagnostics.items.size(), 0);
}

TEST_CASE_FIXTURE(Fixture, "shared_framework_fields_are_typed")
{
    auto document = newDocument("main.luau", R"(
        --!strict
        local initialized: boolean? = shared._OrderInitialized
        local codeGroups: { [string]: true } = shared.CodeGroups
        print(initialized, codeGroups)
    )");

    auto diagnostics = workspace.documentDiagnostics(lsp::DocumentDiagnosticParams{{document}}, nullptr);
    CHECK_EQ(diagnostics.items.size(), 0);

    // Mismatched annotations prove the fields are precisely typed rather than `any`
    auto badDocument = newDocument("bad.luau", R"(
        --!strict
        local initialized: number = shared._OrderInitialized
        local codeGroups: string = shared.CodeGroups
        print(initialized, codeGroups)
    )");

    diagnostics = workspace.documentDiagnostics(lsp::DocumentDiagnosticParams{{badDocument}}, nullptr);
    CHECK_EQ(diagnostics.items.size(), 2);
}

TEST_CASE_FIXTURE(Fixture, "shared_unknown_field_falls_back_to_any")
{
    auto document = newDocument("main.luau", R"(
        --!strict
        local x: number = shared.DefinitelyNotAField
        shared.SomeOtherField = { x }
        print(x)
    )");

    // Unknown fields hit the `[string]: any` indexer: reads and writes are permitted
    auto diagnostics = workspace.documentDiagnostics(lsp::DocumentDiagnosticParams{{document}}, nullptr);
    CHECK_EQ(diagnostics.items.size(), 0);
}

TEST_CASE_FIXTURE(Fixture, "shared_globals_survive_dependency_cascade_rechecks")
{
    // Regression guard: an earlier callable-table model of `shared` crashed the old solver
    // during dependency-cascade unification (many modules re-checked after a common
    // dependency changed). Recreate that shape: two consumers calling shared() and reading
    // shared fields, then edit the common module to force the cascade.
    loadSourcemap(R"({
        "name": "Game",
        "className": "DataModel",
        "children": [
            {
                "name": "ReplicatedStorage",
                "className": "ReplicatedStorage",
                "children": [
                    { "name": "Common", "className": "ModuleScript", "filePaths": ["common.luau"] },
                    { "name": "ConsumerA", "className": "ModuleScript", "filePaths": ["a.luau"] },
                    { "name": "ConsumerB", "className": "ModuleScript", "filePaths": ["b.luau"] }
                ]
            }
        ]
    })");

    auto commonDocument = newDocument("common.luau", "return { value = 1 }");
    auto documentA = newDocument("a.luau", R"(
        local Common = shared("Common")
        return { value = Common.value, place = shared.PlaceType }
    )");
    auto documentB = newDocument("b.luau", R"(
        local Common = shared("Common")
        return { value = Common.value, assets = shared.Assets }
    )");

    CHECK_EQ(workspace.documentDiagnostics(lsp::DocumentDiagnosticParams{{documentA}}, nullptr).items.size(), 0);
    CHECK_EQ(workspace.documentDiagnostics(lsp::DocumentDiagnosticParams{{documentB}}, nullptr).items.size(), 0);

    updateDocument(commonDocument, "return { value = 2, extra = true }");

    CHECK_EQ(workspace.documentDiagnostics(lsp::DocumentDiagnosticParams{{commonDocument}}, nullptr).items.size(), 0);
    CHECK_EQ(workspace.documentDiagnostics(lsp::DocumentDiagnosticParams{{documentA}}, nullptr).items.size(), 0);
    CHECK_EQ(workspace.documentDiagnostics(lsp::DocumentDiagnosticParams{{documentB}}, nullptr).items.size(), 0);
}
#endif

TEST_SUITE_END();
