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

    // Prevent self-requires
    if (node->name == moduleName)
    {
        typeChecker.reportError(Luau::TypeError{expr.args.data[0]->location, Luau::UnknownRequire{moduleName}});
        return std::nullopt;
    }

    auto module = platform.findOrderStringModule(moduleName);
    if (!module.has_value())
    {
        typeChecker.reportError(Luau::TypeError{expr.args.data[0]->location, Luau::UnknownRequire{moduleName}});
        return std::nullopt;
    }

    Luau::ModuleInfo moduleInfo;
    moduleInfo.name = module.value()->virtualPath;

    return Luau::WithPredicate<Luau::TypePackId>{arena.addTypePack({typeChecker.checkRequire(scope, moduleInfo, expr.args.data[0]->location)})};
}

bool MagicOrderStringRequire::infer(const Luau::MagicFunctionCallContext& context)
{
    if (context.callSite->args.size < 1)
        return false;

    auto str = context.callSite->args.data[0]->as<Luau::AstExprConstantString>();
    if (!str)
        return false;

    auto moduleName = std::string(str->value.data, str->value.size);
    auto module = platform.findOrderStringModule(moduleName);
    if (!module.has_value())
    {
        context.solver->reportError(Luau::UnknownRequire{moduleName}, context.callSite->args.data[0]->location);
        return false;
    }

    Luau::ModuleInfo moduleInfo;
    moduleInfo.name = module.value()->virtualPath;

    asMutable(context.result)
        ->ty.emplace<Luau::BoundTypePack>(
            context.solver->arena->addTypePack({context.solver->resolveModule(moduleInfo, context.callSite->args.data[0]->location)}));

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
    if (node->orderStringRequireTypes.find(&globals) != node->orderStringRequireTypes.end())
        return node->orderStringRequireTypes.at(&globals);

    Luau::LazyType lazyTypeValue(
        [&globals, this, &arena, node](Luau::LazyType& lazyTypeValue) -> void
        {
            // Check if the lazy type value already has an unwrapped type
            if (lazyTypeValue.unwrapped.load())
                return;

            // Handle if the node is no longer valid
            if (!node)
            {
                lazyTypeValue.unwrapped = globals.builtinTypes->anyType;
                return;
            }

            // Create a function type: (string) -> any, with magic resolution
            Luau::TypePackId argTypes = arena.addTypePack({globals.builtinTypes->stringType});
            Luau::TypePackId retTypes = arena.addTypePack({globals.builtinTypes->anyType}); // Overridden by magic function
            Luau::FunctionType functionCtv(argTypes, retTypes);

            auto typeId = arena.addType(std::move(functionCtv));
            attachMagicOrderStringRequireFunction(globals, *this, arena, node, typeId);

            lazyTypeValue.unwrapped = typeId;
            return;
        });

    auto ty = arena.addType(std::move(lazyTypeValue));
    node->orderStringRequireTypes.insert_or_assign(&globals, ty);

    return ty;
}

std::optional<const SourceNode*> RobloxPlatform::findOrderStringModule(const std::string& moduleName) const
{
    auto result = this->orderModuleNameToSourceNode.find(moduleName);
    if (result != this->orderModuleNameToSourceNode.end())
        return result->second;

    return std::nullopt;
}

#endif
