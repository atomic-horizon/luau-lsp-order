#include "Platform/RobloxPlatform.hpp"
#include "Luau/BuiltinDefinitions.h"
#include "Luau/ConstraintSolver.h"
#include "Luau/TypeArena.h"
#include "Luau/TypeInfer.h"

LUAU_FASTFLAG(DebugLuauCyclicRequireTypeInference)

#ifdef ORDER_STRING_REQUIRE

// Magic resolver for Order-style `shared("ModuleName")` requires.
//
// A single instance is attached to the ONE `shared` global registered per GlobalTypes
// (see RobloxPlatform::registerOrderSharedGlobal). It deliberately holds no sourcemap
// state: module names are resolved through the platform's name map at call time, and the
// current module is identified from the type checker itself. This keeps the magic function
// valid across sourcemap regenerations, which free all SourceNodes and sourcemap types.
//
// The platform is owned by the same WorkspaceFolder as the Frontend whose global arena
// holds this magic function, and the magic function only runs during type checking, so the
// reference cannot dangle.
struct MagicOrderStringRequire final : Luau::MagicFunction
{
    const RobloxPlatform& platform;

    explicit MagicOrderStringRequire(const RobloxPlatform& platform)
        : platform(platform)
    {
    }

    std::optional<Luau::WithPredicate<Luau::TypePackId>> handleOldSolver(Luau::TypeChecker& typeChecker, const Luau::ScopePtr& scope,
        const Luau::AstExprCall& expr, Luau::WithPredicate<Luau::TypePackId> withPredicate) override;
    bool infer(const Luau::MagicFunctionCallContext& context) override;
};

// `ConstraintSolver::reportError` gained a module-name parameter behind
// DebugLuauCyclicRequireTypeInference; the pre-flag overload is now DEPRECATED_reportError.
// Mirrors the branching that `MagicRequire::infer` does in BuiltinDefinitions.cpp.
static void reportRequireError(const Luau::MagicFunctionCallContext& context, const std::string& moduleName)
{
    if (FFlag::DebugLuauCyclicRequireTypeInference)
        context.solver->reportError(
            Luau::UnknownRequire{moduleName}, context.callSite->args.data[0]->location, *context.constraint->moduleName);
    else
        context.solver->DEPRECATED_reportError(Luau::UnknownRequire{moduleName}, context.callSite->args.data[0]->location);
}

static bool isNilableSharedCall(const Luau::AstExprCall& expr)
{
    if (expr.args.size >= 2)
    {
        if (auto boolArg = expr.args.data[1]->as<Luau::AstExprConstantBool>())
            return boolArg->value;
    }
    return false;
}

std::optional<Luau::WithPredicate<Luau::TypePackId>> MagicOrderStringRequire::handleOldSolver(
    Luau::TypeChecker& typeChecker, const Luau::ScopePtr& scope, const Luau::AstExprCall& expr, Luau::WithPredicate<Luau::TypePackId>)
{
    if (expr.args.size < 1)
    {
        typeChecker.reportError(Luau::TypeError{expr.location, Luau::UnknownRequire{}});
        return std::nullopt;
    }

    auto str = expr.args.data[0]->as<Luau::AstExprConstantString>();
    if (!str)
    {
        typeChecker.reportError(Luau::TypeError{expr.args.data[0]->location, Luau::UnknownRequire{}});
        return std::nullopt;
    }

    auto moduleName = std::string(str->value.data, str->value.size);
    bool nilable = isNilableSharedCall(expr);

    // Use the TypeChecker's own module arena (same as built-in MagicRequire::handleOldSolver).
    Luau::TypeArena& moduleArena = *typeChecker.currentModule->internalTypes;

    auto virtualPath = platform.findOrderStringModule(moduleName);
    if (!virtualPath.has_value())
    {
        // When nilable flag is set, an unknown module resolves to nil instead of an error
        if (nilable)
            return Luau::WithPredicate<Luau::TypePackId>{moduleArena.addTypePack({typeChecker.builtinTypes->nilType})};
        typeChecker.reportError(Luau::TypeError{expr.args.data[0]->location, Luau::UnknownRequire{moduleName}});
        return std::nullopt;
    }

    // Prevent self-requires
    if (*virtualPath == typeChecker.currentModule->name)
    {
        typeChecker.reportError(Luau::TypeError{expr.args.data[0]->location, Luau::UnknownRequire{moduleName}});
        return std::nullopt;
    }

    Luau::ModuleInfo moduleInfo;
    moduleInfo.name = *virtualPath;

    // checkRequire consults requireCycles and yields `any` for cyclic edges, which is what
    // keeps intentional Order service cycles from embedding freeable cross-module TypeIds.
    Luau::TypeId resultTy = typeChecker.checkRequire(scope, moduleInfo, expr.args.data[0]->location);

    // When the nilable flag is set, wrap the return type as T? (union with nil)
    if (nilable)
        resultTy = Luau::makeOption(typeChecker.builtinTypes, moduleArena, resultTy);

    return Luau::WithPredicate<Luau::TypePackId>{moduleArena.addTypePack({resultTy})};
}

bool MagicOrderStringRequire::infer(const Luau::MagicFunctionCallContext& context)
{
    if (context.callSite->args.size < 1)
        return false;

    auto str = context.callSite->args.data[0]->as<Luau::AstExprConstantString>();
    if (!str)
        return false;

    auto moduleName = std::string(str->value.data, str->value.size);
    bool nilable = isNilableSharedCall(*context.callSite);

    auto virtualPath = platform.findOrderStringModule(moduleName);
    if (!virtualPath.has_value())
    {
        // When nilable flag is set, an unknown module resolves to nil instead of an error
        if (nilable)
        {
            asMutable(context.result)->ty.emplace<Luau::BoundTypePack>(context.solver->arena->addTypePack({context.solver->builtinTypes->nilType}));
            return true;
        }
        reportRequireError(context, moduleName);
        return false;
    }

    // Prevent self-requires (mirrors the old-solver path). The current module is identified
    // the same way builtin MagicRequire::infer does.
    const Luau::ModuleName& currentModuleName =
        FFlag::DebugLuauCyclicRequireTypeInference ? *context.constraint->moduleName : context.solver->module->name;
    if (*virtualPath == currentModuleName)
    {
        reportRequireError(context, moduleName);
        return false;
    }

    Luau::ModuleInfo moduleInfo;
    moduleInfo.name = *virtualPath;

    // resolveModule consults requireCycles and yields `any` for cyclic edges, which is what
    // keeps intentional Order service cycles from embedding freeable cross-module TypeIds.
    Luau::TypeId resultTy = FFlag::DebugLuauCyclicRequireTypeInference
                                ? context.solver->resolveModule(moduleInfo, context.callSite->args.data[0]->location, *context.constraint->moduleName)
                                : context.solver->DEPRECATED_resolveModule(moduleInfo, context.callSite->args.data[0]->location);

    // When the nilable flag is set, wrap the return type as T? (union with nil)
    if (nilable)
        resultTy = Luau::makeOption(context.solver->builtinTypes, *context.solver->arena, resultTy);

    asMutable(context.result)->ty.emplace<Luau::BoundTypePack>(context.solver->arena->addTypePack({resultTy}));

    return true;
}

void RobloxPlatform::registerOrderSharedGlobal(Luau::GlobalTypes& globals) const
{
    // `shared` is typed as a plain function `(string, boolean?) -> any` with a magic resolver
    // attached. The runtime is actually a callable table, but a previous attempt to model that
    // as a MetatableType triggered a Luau old-solver crash in `Normalizer::unionNormalWithTy`
    // when many `shared(...)` calls were unified during dependency-cascade type checking
    // (a null was reaching a UnionType's options vector somewhere downstream). Field access
    // on `shared` (e.g. `shared.foo = 5`) types as `any` here, which matches the runtime
    // functionally even though it doesn't structurally model the table.
    //
    // The type is allocated ONCE per GlobalTypes into the global arena, exactly like builtin
    // `require`. It must NOT live in `instanceTypes` (cleared on every sourcemap update while
    // checked modules still reference its contents) and must not hold SourceNode pointers
    // (freed on every sourcemap update); resolution happens by name at call time instead.
    Luau::TypeArena& arena = globals.globalTypes;
    Luau::unfreeze(arena);

    Luau::TypeId optionalBool = Luau::makeOption(globals.builtinTypes, arena, globals.builtinTypes->booleanType);
    Luau::TypePackId argTypes = arena.addTypePack({globals.builtinTypes->stringType, optionalBool});
    Luau::TypePackId retTypes = arena.addTypePack({globals.builtinTypes->anyType}); // Overridden by magic function
    Luau::TypeId fnTy = arena.addType(Luau::FunctionType{argTypes, retTypes});

    Luau::attachMagicFunction(fnTy, std::make_shared<MagicOrderStringRequire>(*this));
    Luau::attachTag(fnTy, "OrderStringRequires");
    Luau::attachTag(fnTy, "require"); // Magic tag for require-like resolution

    // Overrides the `declare shared: any` binding from the Roblox definitions file
    Luau::addGlobalBinding(globals, "shared", Luau::Binding{fnTy});

    Luau::freeze(arena);
}

std::optional<Luau::ModuleName> RobloxPlatform::findOrderStringModule(const std::string& moduleName) const
{
    auto result = this->orderModuleNameToVirtualPath.find(moduleName);
    if (result != this->orderModuleNameToVirtualPath.end())
        return result->second;

    return std::nullopt;
}

#endif
