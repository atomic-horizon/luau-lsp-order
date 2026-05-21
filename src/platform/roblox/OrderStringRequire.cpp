#include "Platform/RobloxPlatform.hpp"
#include "LSP/JsonTomlSyntaxParser.hpp"
#include "Luau/BuiltinDefinitions.h"
#include "Luau/ConstraintSolver.h"
#include "Luau/TypeInfer.h"

#ifdef ORDER_STRING_REQUIRE

struct MagicOrderStringRequire final : Luau::MagicFunction
{
    const Luau::GlobalTypes& globals;
    const RobloxPlatform& platform;
    Luau::TypeArena& arena;
    const SourceNode* node;

    MagicOrderStringRequire(const Luau::GlobalTypes& globals, const RobloxPlatform& platform, Luau::TypeArena& arena, const SourceNode* node)
        : globals(globals)
        , platform(platform)
        , arena(arena)
        , node(std::move(node))
    {
    }

    std::optional<Luau::WithPredicate<Luau::TypePackId>> handleOldSolver(Luau::TypeChecker& typeChecker, const Luau::ScopePtr& scope,
        const Luau::AstExprCall& expr, Luau::WithPredicate<Luau::TypePackId> withPredicate) override;
    bool infer(const Luau::MagicFunctionCallContext& context) override;
};

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

    // Prevent self-requires
    if (node->name == moduleName)
    {
        typeChecker.reportError(Luau::TypeError{expr.args.data[0]->location, Luau::UnknownRequire{moduleName}});
        return std::nullopt;
    }

    auto module = platform.findOrderStringModule(moduleName);
    if (!module.has_value())
    {
        // When nilable flag is set, an unknown module resolves to nil instead of an error
        if (nilable)
        {
            Luau::TypeArena& moduleArena = typeChecker.currentModule->internalTypes;
            return Luau::WithPredicate<Luau::TypePackId>{moduleArena.addTypePack({globals.builtinTypes->nilType})};
        }
        typeChecker.reportError(Luau::TypeError{expr.args.data[0]->location, Luau::UnknownRequire{moduleName}});
        return std::nullopt;
    }

    Luau::ModuleInfo moduleInfo;
    moduleInfo.name = module.value()->virtualPath;

    // Use the TypeChecker's own module arena (same as built-in MagicRequire::handleOldSolver),
    // NOT instanceTypes. instanceTypes can be cleared/reallocated across sourcemap updates, making
    // any TypePackIds allocated in it potentially stale during subsequent type checks.
    Luau::TypeArena& moduleArena = typeChecker.currentModule->internalTypes;
    Luau::TypeId resultTy = typeChecker.checkRequire(scope, moduleInfo, expr.args.data[0]->location);

    // When the nilable flag is set, wrap the return type as T? (union with nil)
    if (nilable)
        resultTy = Luau::makeOption(globals.builtinTypes, moduleArena, resultTy);

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

    // Prevent self-requires (mirrors the old-solver path).
    if (node->name == moduleName)
    {
        context.solver->reportError(Luau::UnknownRequire{moduleName}, context.callSite->args.data[0]->location);
        return false;
    }

    auto module = platform.findOrderStringModule(moduleName);
    if (!module.has_value())
    {
        // When nilable flag is set, an unknown module resolves to nil instead of an error
        if (nilable)
        {
            asMutable(context.result)->ty.emplace<Luau::BoundTypePack>(context.solver->arena->addTypePack({globals.builtinTypes->nilType}));
            return true;
        }
        context.solver->reportError(Luau::UnknownRequire{moduleName}, context.callSite->args.data[0]->location);
        return false;
    }

    Luau::ModuleInfo moduleInfo;
    moduleInfo.name = module.value()->virtualPath;

    Luau::TypeId resultTy = context.solver->resolveModule(moduleInfo, context.callSite->args.data[0]->location);

    // When the nilable flag is set, wrap the return type as T? (union with nil)
    if (nilable)
        resultTy = Luau::makeOption(context.solver->builtinTypes, *context.solver->arena, resultTy);

    asMutable(context.result)->ty.emplace<Luau::BoundTypePack>(context.solver->arena->addTypePack({resultTy}));

    return true;
}

static void attachMagicOrderStringRequireFunction(
    const Luau::GlobalTypes& globals, const RobloxPlatform& platform, Luau::TypeArena& arena, const SourceNode* node, Luau::TypeId lookupFuncTy)
{
    Luau::attachMagicFunction(lookupFuncTy, std::make_shared<MagicOrderStringRequire>(globals, platform, arena, node));
    Luau::attachTag(lookupFuncTy, kSourcemapGeneratedTag);
    Luau::attachTag(lookupFuncTy, "OrderStringRequires");
    Luau::attachTag(lookupFuncTy, "require"); // Magic tag for require-like resolution
}

Luau::TypeId RobloxPlatform::getOrderStringRequireType(const Luau::GlobalTypes& globals, Luau::TypeArena& arena, const SourceNode* node) const
{
    // Gets the type corresponding to the sourcemap node if it exists
    // Make sure to use the correct ty version (base typeChecker vs autocomplete typeChecker)
    if (auto it = node->orderStringRequireTypes.find(&globals); it != node->orderStringRequireTypes.end())
        return it->second;

    // Model `shared` as a callable table: a table with an `[any]: any` indexer wrapped by a
    // metatable supplying `__call`. The `__call` function carries the magic resolver and tags,
    // matching the runtime where `shared` is a table whose metatable makes it callable.
    // `self` is `any` to avoid a recursive type construction; the magic function reads args
    // off the AST and never inspects self.
    Luau::TypeId optionalBool = Luau::makeOption(globals.builtinTypes, arena, globals.builtinTypes->booleanType);
    Luau::TypePackId callArgs = arena.addTypePack({globals.builtinTypes->anyType, globals.builtinTypes->stringType, optionalBool});
    Luau::TypePackId callRets = arena.addTypePack({globals.builtinTypes->anyType}); // Overridden by magic function
    Luau::TypeId callFnTy = arena.addType(Luau::FunctionType{callArgs, callRets});

    // Magic + tags live on the __call function so that solver/typecheck/autocomplete sites
    // that unwrap __call via findMetatableEntry pick them up.
    attachMagicOrderStringRequireFunction(globals, *this, arena, node, callFnTy);

    // Metatable: { __call = callFnTy }
    Luau::TypeId metatableTy = arena.addType(Luau::TableType{{}, std::nullopt, Luau::TypeLevel{}, Luau::TableState::Sealed});
    Luau::getMutable<Luau::TableType>(metatableTy)->props["__call"] = Luau::Property{callFnTy};

    // Inner table: sealed, with a `[any]: any` indexer so arbitrary keys can be read/written.
    Luau::TableIndexer indexer{globals.builtinTypes->anyType, globals.builtinTypes->anyType};
    Luau::TypeId innerTableTy = arena.addType(Luau::TableType{{}, indexer, Luau::TypeLevel{}, Luau::TableState::Sealed});

    Luau::TypeId sharedTy = arena.addType(Luau::MetatableType{innerTableTy, metatableTy});

    node->orderStringRequireTypes.insert_or_assign(&globals, sharedTy);

    return sharedTy;
}

std::optional<const SourceNode*> RobloxPlatform::findOrderStringModule(const std::string& moduleName) const
{
    auto result = this->orderModuleNameToSourceNode.find(moduleName);
    if (result != this->orderModuleNameToSourceNode.end())
        return result->second;

    return std::nullopt;
}

#endif
