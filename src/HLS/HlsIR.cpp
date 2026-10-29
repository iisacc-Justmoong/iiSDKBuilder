#include "HlsIR.h"
#include <algorithm>
#include <bit>
#include <cctype>
#include <charconv>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>

namespace ii::builder::detail::hls {
namespace {
std::string text(const Json &j, std::string_view key) { return j.has(key) ? j.at(key).string() : ""; }
bool flag(const Json &j, std::string_view key) { return j.has(key) && std::get<bool>(j.at(key).value); }
const Json::Array &children(const Json &j) {
    static const Json::Array empty;
    return j.has("inner") ? j.at("inner").array() : empty;
}
std::uint64_t number(std::string_view value) {
    std::uint64_t result = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
        throw std::runtime_error("HLS: integer outside 64-bit contract");
    return result;
}
std::uint64_t mask(Type t) { return t.bits == 64 ? UINT64_MAX : (std::uint64_t{1} << t.bits) - 1; }
std::int64_t signed_value(std::uint64_t v, Type t) {
    if (t.sign && t.bits < 64 && (v & (std::uint64_t{1} << (t.bits - 1))))
        v |= ~mask(t);
    return std::bit_cast<std::int64_t>(v);
}
std::string trim(std::string s) {
    const auto start = s.find_first_not_of(' ');
    if (start == s.npos)
        return {};
    const auto end = s.find_last_not_of(' ');
    return s.substr(start, end - start + 1);
}
struct Shape final {
    Type type;
    unsigned count = 1;
    bool array = false, reference = false, constant = false;
};
struct Value final {
    Type type;
    std::vector<unsigned> parts;
    bool array = false;
};
struct Context final {
    std::map<std::string, Value> locals;
    unsigned active = 0, result = 0;
    Type result_type;
};
struct Function final {
    const Json *ast;
    std::string name;
};
class Lowerer final {
    const HlsRequest &request_;
    std::string_view source_;
    std::stop_token stop_;
    core::Deadline deadline_;
    std::set<std::string> reading_globals_;
    Circuit circuit_;
    std::map<std::string, Function> functions_;
    std::map<std::string, const Json *> globals_;
    std::map<std::string, std::string> aliases_;
    std::map<std::tuple<unsigned, bool, bool, std::string, std::vector<unsigned>, std::string>, unsigned> intern_;
    std::vector<std::string> calls_;
    unsigned visited_ = 0;
    [[noreturn]] void fail(const Json &j, const std::string &message) const {
        unsigned line = 0;
        const Json *loc = j.has("loc") ? &j.at("loc") : j.has("range") ? &j.at("range").at("begin") : nullptr;
        if (loc) {
            if (loc->has("line"))
                line = loc->at("line").integer();
            else if (loc->has("offset")) {
                auto offset = std::min<std::size_t>(loc->at("offset").integer(), source_.size());
                line = 1 + unsigned(std::count(source_.begin(), source_.begin() + offset, '\n'));
            }
        }
        throw std::runtime_error(path_string(request_.source) + ":" + std::to_string(line) + ": HLS: " + message);
    }
    void checkpoint() {
        if (stop_.stop_requested())
            throw std::runtime_error("HLS synthesis cancelled");
        if (deadline_.expired())
            throw std::runtime_error("HLS synthesis exceeded deadline");
        if (++visited_ > 1000000)
            throw std::runtime_error("HLS analysis budget exceeded");
    }
    void index(const Json &j, std::string scope = {}) {
        checkpoint();
        const auto kind = text(j, "kind");
        if (kind == "NamespaceDecl" && !text(j, "name").empty())
            scope += text(j, "name") + "::";
        if ((kind == "TypedefDecl" || kind == "TypeAliasDecl") && j.has("type")) {
            const auto &t = j.at("type");
            auto type = text(t, "desugaredQualType");
            if (type.empty())
                type = text(t, "qualType");
            aliases_[scope + text(j, "name")] = type;
        }
        if (kind == "FunctionDecl")
            for (const auto &c : children(j))
                if (text(c, "kind") == "CompoundStmt")
                    functions_.insert_or_assign(text(j, "id"), Function{&j, scope + text(j, "name")});
        if (kind == "VarDecl")
            globals_[text(j, "id")] = &j;
        for (const auto &child : children(j))
            index(child, scope);
    }
    Shape shape(std::string value, const Json &site) const {
        Shape s;
        s.constant = value.find("const ") != value.npos;
        s.reference = value.find('&') != value.npos;
        if (value.find("volatile") != value.npos || value.find('*') != value.npos)
            fail(site, "volatile and pointer types are unsupported: " + value);
        auto bracket = value.find('[');
        if (bracket != value.npos) {
            auto close = value.find(']', bracket);
            if (close == value.npos || value.find('[', close) != value.npos)
                fail(site, "only one fixed array dimension is supported");
            const auto count = number(trim(value.substr(bracket + 1, close - bracket - 1)));
            if (!count || count > 256)
                fail(site, "array bound must be 1..256");
            s.count = unsigned(count);
            s.array = true;
            value = value.substr(0, bracket);
        }
        for (char c : std::string("()&"))
            std::erase(value, c);
        for (auto qualifier : {"const ", " const", "constexpr "}) {
            std::size_t at;
            while ((at = value.find(qualifier)) != value.npos)
                value.erase(at, std::string_view(qualifier).size());
        }
        value = trim(value);
        for (unsigned i = 0; i < 16; ++i) {
            auto alias = aliases_.find(value);
            if (alias == aliases_.end() || alias->second == value)
                break;
            value = trim(alias->second);
            if (value.starts_with("const "))
                value.erase(0, 6);
        }
        if (value == "bool")
            s.type = {1, false, true};
        else if (value == "char" || value == "signed char")
            s.type = {8, true, false};
        else if (value == "unsigned char")
            s.type = {8, false, false};
        else if (value == "short" || value == "short int")
            s.type = {16, true, false};
        else if (value == "unsigned short" || value == "unsigned short int")
            s.type = {16, false, false};
        else if (value == "int" || value == "signed int")
            s.type = {32, true, false};
        else if (value == "unsigned int" || value == "unsigned")
            s.type = {32, false, false};
        else if (value == "long" || value == "long int" || value == "long long" || value == "long long int")
            s.type = {64, true, false};
        else if (value == "unsigned long" || value == "unsigned long int" || value == "unsigned long long" ||
                 value == "unsigned long long int")
            s.type = {64, false, false};
        else
            fail(site, "expected a bool or 8/16/32/64-bit integer, got " + value);
        return s;
    }
    Shape shape(const Json &node) const {
        const auto &t = node.at("type");
        const auto canonical = text(t, "desugaredQualType");
        return shape(canonical.empty() ? text(t, "qualType") : canonical, node);
    }
    Type type(const Json &node) const {
        auto s = shape(node);
        if (s.array || s.reference)
            fail(node, "expected scalar value");
        return s.type;
    }
    unsigned add(Type t, std::string op, std::vector<unsigned> args = {}, std::optional<std::uint64_t> constant = {},
                 std::string name = {}) {
        if (constant) {
            *constant &= mask(t);
            op = "constant";
            args.clear();
            name = std::to_string(*constant);
        }
        const auto key = std::tuple{t.bits, t.sign, t.boolean, op, args, name};
        if (auto it = intern_.find(key); it != intern_.end())
            return it->second;
        if (circuit_.nodes.size() >= request_.max_nodes)
            throw std::runtime_error("HLS node budget exceeded");
        const auto id = unsigned(circuit_.nodes.size());
        circuit_.nodes.push_back({t, std::move(op), std::move(name), std::move(args), constant});
        intern_.emplace(key, id);
        return id;
    }
    unsigned constant(Type t, std::uint64_t v) { return add(t, "constant", {}, v); }
    std::optional<std::uint64_t> known(unsigned a) const { return circuit_.nodes.at(a).constant; }
    Type node_type(unsigned a) const { return circuit_.nodes.at(a).type; }
    unsigned cast(unsigned a, Type dest) {
        const auto from = node_type(a);
        if (from == dest)
            return a;
        if (auto value = known(a)) {
            auto v = *value;
            if (dest.boolean)
                v = v != 0;
            else if (from.sign && dest.bits > from.bits)
                v = std::bit_cast<std::uint64_t>(signed_value(v, from));
            return constant(dest, v);
        }
        return add(dest, dest.boolean ? "bool" : "cast", {a});
    }
    unsigned boolean(unsigned a) { return cast(a, {1, false, true}); }
    unsigned unary(const std::string &op, unsigned a, Type t, const Json &site) {
        if (op == "+")
            return cast(a, t);
        if (auto v = known(a)) {
            if (op == "~")
                return constant(t, ~*v);
            if (op == "-")
                return constant(t, 0 - *v);
            if (op == "!")
                return constant(t, !*v);
        }
        if (op != "~" && op != "-" && op != "!")
            fail(site, "unsupported unary operator " + op);
        return add(t, "unary" + op, {a});
    }
    unsigned binary(std::string op, unsigned a, unsigned b, Type t, const Json &site) {
        auto av = known(a), bv = known(b);
        const auto at = node_type(a);
        if ((op == "<<" || op == ">>") && (!bv || *bv >= at.bits))
            fail(site, "shifts require a constant count smaller than the promoted operand width");
        if ((op == "/" || op == "%") && (!bv || !*bv))
            fail(site, "division requires a nonzero constant denominator");
        if (av && bv) {
            const auto x = *av, y = *bv;
            const auto sx = signed_value(x, at), sy = signed_value(y, node_type(b));
            std::uint64_t v = 0;
            if (op == "+")
                v = x + y;
            else if (op == "-")
                v = x - y;
            else if (op == "*")
                v = x * y;
            else if (op == "&")
                v = x & y;
            else if (op == "|")
                v = x | y;
            else if (op == "^")
                v = x ^ y;
            else if (op == "<<")
                v = x << y;
            else if (op == ">>")
                v = at.sign ? std::bit_cast<std::uint64_t>(sx >> y) : x >> y;
            else if (op == "==")
                v = x == y;
            else if (op == "!=")
                v = x != y;
            else if (op == "<")
                v = at.sign ? sx < sy : x < y;
            else if (op == "<=")
                v = at.sign ? sx <= sy : x <= y;
            else if (op == ">")
                v = at.sign ? sx > sy : x > y;
            else if (op == ">=")
                v = at.sign ? sx >= sy : x >= y;
            else if (op == "&&")
                v = x && y;
            else if (op == "||")
                v = x || y;
            else if (op == "/" || op == "%") {
                const auto minimum =
                    at.bits == 64 ? std::numeric_limits<std::int64_t>::min() : -(std::int64_t{1} << (at.bits - 1));
                if (at.sign && sx == minimum && sy == -1)
                    fail(site, "undefined signed division overflow");
                v = at.sign ? std::bit_cast<std::uint64_t>(op == "/" ? sx / sy : sx % sy) : (op == "/" ? x / y : x % y);
            } else
                fail(site, "unsupported binary operator " + op);
            return constant(t, v);
        }
        if (op == "&&" || op == "||") {
            a = boolean(a);
            b = boolean(b);
            if (a == b)
                return a;
            const auto complementary = [&](unsigned x, unsigned y) {
                const auto &node = circuit_.nodes.at(x);
                return node.op == "unary!" && node.inputs.at(0) == y;
            };
            if (complementary(a, b) || complementary(b, a))
                return constant({1, false, true}, op == "||");
            av = known(a);
            bv = known(b);
            if (av) {
                if (op == "&&")
                    return *av ? b : a;
                return *av ? a : b;
            }
            if (bv) {
                if (op == "&&")
                    return *bv ? a : b;
                return *bv ? b : a;
            }
        }
        static constexpr std::string_view supported[]{
            "+", "-", "*", "&", "|", "^", "<<", ">>", "==", "!=", "<", "<=", ">", ">=", "&&", "||", "/", "%"};
        if (std::ranges::find(supported, op) == std::end(supported))
            fail(site, "unsupported binary operator " + op);
        return add(t, std::move(op), {a, b});
    }
    unsigned mux(unsigned condition, unsigned a, unsigned b) {
        condition = boolean(condition);
        if (a == b)
            return a;
        if (auto c = known(condition))
            return *c ? a : b;
        if (node_type(a) != node_type(b))
            throw std::runtime_error("HLS internal mux type mismatch");
        return add(node_type(a), "mux", {condition, a, b});
    }
    unsigned scalar(const Value &v, const Json &site) const {
        if (v.array || v.parts.size() != 1)
            fail(site, "array must be subscripted");
        return v.parts[0];
    }
    Value value(unsigned n) const { return {node_type(n), {n}, false}; }
    Value lookup(const Json &ref, Context &ctx) {
        const auto id = text(ref.at("referencedDecl"), "id");
        if (auto it = ctx.locals.find(id); it != ctx.locals.end())
            return it->second;
        const auto it = globals_.find(id);
        if (it == globals_.end())
            fail(ref, "unbound value " + text(ref.at("referencedDecl"), "name"));
        const auto &g = *it->second;
        const auto s = shape(g);
        if (!s.constant && !flag(g, "constexpr"))
            fail(ref, "mutable global state is unsupported");
        if (children(g).empty())
            fail(ref, "global constant has no initializer");
        if (!reading_globals_.insert(id).second)
            fail(ref, "cyclic global constant initializer");
        auto v = eval(children(g).back(), ctx);
        reading_globals_.erase(id);
        for (auto n : v.parts)
            if (!known(n))
                fail(ref, "global initializer must be compile-time constant");
        return v;
    }
    std::pair<Value *, unsigned> lvalue(const Json &node, Context &ctx) {
        const auto k = text(node, "kind");
        if (k == "ParenExpr" || k == "ImplicitCastExpr")
            return lvalue(children(node).at(0), ctx);
        if (k == "DeclRefExpr") {
            auto it = ctx.locals.find(text(node.at("referencedDecl"), "id"));
            if (it == ctx.locals.end())
                fail(node, "writes to globals are unsupported");
            if (it->second.array)
                fail(node, "whole array assignment unsupported");
            return {&it->second, 0};
        }
        if (k == "ArraySubscriptExpr") {
            auto base = eval(children(node).at(0), ctx);
            (void)base;
            auto index = known(scalar(eval(children(node).at(1), ctx), node));
            if (!index)
                fail(node, "array writes require a constant index");
            const Json *decl = &children(node).at(0);
            while (text(*decl, "kind") == "ImplicitCastExpr" || text(*decl, "kind") == "ParenExpr")
                decl = &children(*decl).at(0);
            if (text(*decl, "kind") != "DeclRefExpr")
                fail(node, "unsupported array storage");
            auto it = ctx.locals.find(text(decl->at("referencedDecl"), "id"));
            if (it == ctx.locals.end() || *index >= it->second.parts.size())
                fail(node, "array write outside fixed extent");
            return {&it->second, unsigned(*index)};
        }
        fail(node, "unsupported assignment destination");
    }
    void merge(Context &ctx, unsigned condition, const Context &a, const Context &b) {
        for (auto &[id, v] : ctx.locals) {
            const auto &x = a.locals.at(id);
            const auto &y = b.locals.at(id);
            for (unsigned i = 0; i < v.parts.size(); ++i)
                v.parts[i] = mux(condition, x.parts.at(i), y.parts.at(i));
        }
        ctx.result = mux(condition, a.result, b.result);
        ctx.active = binary("||", a.active, b.active, {1, false, true}, Json{});
    }
    Value conditional(const Json &condition, const Json &yes, const Json &no, Context &ctx) {
        const auto test = boolean(scalar(eval(condition, ctx), condition));
        if (auto c = known(test))
            return eval(*c ? yes : no, ctx);
        auto a = ctx, b = ctx;
        auto x = eval(yes, a), y = eval(no, b);
        if (x.array || y.array || x.type != y.type)
            fail(condition, "conditional result types differ");
        merge(ctx, test, a, b);
        return value(mux(test, scalar(x, yes), scalar(y, no)));
    }
    const Function &called(const Json &node) const {
        if (text(node, "kind") == "DeclRefExpr") {
            auto id = text(node.at("referencedDecl"), "id");
            if (auto it = functions_.find(id); it != functions_.end())
                return it->second;
            const auto name = text(node.at("referencedDecl"), "name");
            const Function *result = nullptr;
            for (const auto &[key, f] : functions_)
                if (text(*f.ast, "name") == name) {
                    (void)key;
                    if (result)
                        fail(node, "ambiguous helper function");
                    result = &f;
                }
            if (result)
                return *result;
            fail(node, "helper must have a synthesizable definition");
        }
        if (children(node).size() == 1)
            return called(children(node).at(0));
        fail(node, "indirect function calls are unsupported");
    }
    Value eval(const Json &node, Context &ctx) {
        checkpoint();
        const auto k = text(node, "kind");
        const auto &c = children(node);
        if (k == "DeclRefExpr")
            return lookup(node, ctx);
        if (k == "IntegerLiteral" || k == "CharacterLiteral") {
            auto v =
                k == "CharacterLiteral" ? std::uint64_t(node.at("value").integer()) : number(node.at("value").string());
            return value(constant(type(node), v));
        }
        if (k == "CXXBoolLiteralExpr")
            return value(constant({1, false, true}, flag(node, "value")));
        if (k == "ImplicitValueInitExpr")
            return value(constant(type(node), 0));
        if (k == "ParenExpr" || k == "ConstantExpr" || k == "ExprWithCleanups") {
            if (c.empty())
                fail(node, "missing expression");
            return eval(c.back(), ctx);
        }
        if (k == "ImplicitCastExpr" || k == "CStyleCastExpr" || k == "CXXStaticCastExpr" ||
            k == "CXXFunctionalCastExpr") {
            const auto cast_kind = text(node, "castKind");
            if (cast_kind != "IntegralCast" && cast_kind != "LValueToRValue" && cast_kind != "NoOp" &&
                cast_kind != "IntegralToBoolean" && cast_kind != "ArrayToPointerDecay")
                fail(node, "unsupported cast " + cast_kind);
            auto v = eval(c.at(0), ctx);
            if (v.array) {
                if (cast_kind == "ArrayToPointerDecay" || cast_kind == "NoOp")
                    return v;
                fail(node, "array cast unsupported");
            }
            return value(cast(scalar(v, node), type(node)));
        }
        if (k == "ArraySubscriptExpr") {
            auto array = eval(c.at(0), ctx);
            if (!array.array)
                fail(node, "expected fixed array");
            auto index = scalar(eval(c.at(1), ctx), node);
            if (auto n = known(index)) {
                if (*n >= array.parts.size())
                    fail(node, "array read outside fixed extent");
                return value(array.parts[*n]);
            }
            unsigned result = constant(array.type, 0);
            for (unsigned i = 0; i < array.parts.size(); ++i) {
                auto match = binary("==", index, constant(node_type(index), i), {1, false, true}, node);
                result = mux(match, array.parts[i], result);
            }
            return value(result);
        }
        if (k == "ConditionalOperator")
            return conditional(c.at(0), c.at(1), c.at(2), ctx);
        if (k == "UnaryOperator") {
            const auto op = text(node, "opcode");
            if (op == "++" || op == "--") {
                auto [v, i] = lvalue(c.at(0), ctx);
                auto old = v->parts[i];
                auto next = binary(op == "++" ? "+" : "-", old, constant(v->type, 1), v->type, node);
                v->parts[i] = next;
                return value(flag(node, "isPostfix") ? old : next);
            }
            return value(unary(op, scalar(eval(c.at(0), ctx), node), type(node), node));
        }
        if (k == "BinaryOperator" || k == "CompoundAssignOperator") {
            const auto op = text(node, "opcode");
            if (op == "=") {
                auto rhs = scalar(eval(c.at(1), ctx), node);
                auto [v, i] = lvalue(c.at(0), ctx);
                v->parts[i] = cast(rhs, v->type);
                return value(v->parts[i]);
            }
            if (k == "CompoundAssignOperator") {
                // C++17 and later sequence the RHS before evaluation/read of the LHS.
                const auto right = scalar(eval(c.at(1), ctx), node);
                auto [v, i] = lvalue(c.at(0), ctx);
                auto destination_type = v->type;
                const auto left = cast(v->parts[i], shape(text(node.at("computeLHSType"), "qualType"), node).type);
                auto result_type = shape(text(node.at("computeResultType"), "qualType"), node).type;
                auto result = binary(op.substr(0, op.size() - 1), left, right, result_type, node);
                v->parts[i] = cast(result, destination_type);
                return value(v->parts[i]);
            }
            auto left = scalar(eval(c.at(0), ctx), node);
            if (op == "&&" || op == "||") {
                auto test = boolean(left);
                if (auto n = known(test)) {
                    if ((op == "&&" && !*n) || (op == "||" && *n))
                        return value(test);
                    return value(boolean(scalar(eval(c.at(1), ctx), node)));
                }
                auto changed = ctx, unchanged = ctx;
                auto right = boolean(scalar(eval(c.at(1), changed), node));
                if (op == "&&")
                    merge(ctx, test, changed, unchanged);
                else
                    merge(ctx, test, unchanged, changed);
                return value(binary(op, test, right, {1, false, true}, node));
            }
            auto right = scalar(eval(c.at(1), ctx), node);
            if (op == ",")
                return value(right);
            return value(binary(op, left, right, type(node), node));
        }
        if (k == "CallExpr") {
            const auto &f = called(c.at(0));
            std::vector<Value> args;
            for (unsigned i = 1; i < c.size(); ++i)
                args.push_back(eval(c[i], ctx));
            return value(invoke(f, args));
        }
        if (k == "UnaryExprOrTypeTraitExpr" && text(node, "name") == "sizeof") {
            Shape s;
            if (node.has("argType")) {
                const auto &t = node.at("argType");
                auto canon = text(t, "desugaredQualType");
                s = shape(canon.empty() ? text(t, "qualType") : canon, node);
            } else
                s = shape(c.at(0));
            return value(constant(type(node), std::uint64_t((s.type.bits + 7) / 8) * s.count));
        }
        fail(node, "unsupported expression " + k);
    }
    void declare(const Json &node, Context &ctx) {
        if (text(node, "storageClass") == "static" || text(node, "storageClass") == "extern")
            fail(node, "static/local external state is unsupported");
        auto s = shape(node);
        if (s.reference)
            fail(node, "local reference aliases are unsupported");
        const auto &c = children(node);
        if (c.empty())
            fail(node, "local variables require an initializer");
        Value v{s.type, {}, s.array};
        if (s.array) {
            const auto &init = c.back();
            if (text(init, "kind") != "InitListExpr")
                fail(node, "fixed arrays need an initializer list");
            const auto *values = &children(init);
            unsigned skip = 0;
            auto filler = constant(s.type, 0);
            if (init.has("array_filler")) {
                values = &init.at("array_filler").array();
                if (values->empty())
                    fail(node, "malformed array filler");
                filler = cast(scalar(eval(values->front(), ctx), node), s.type);
                skip = 1;
            }
            if (values->size() - skip > s.count)
                fail(node, "array initializer too long");
            for (unsigned i = 0; i < s.count; ++i)
                v.parts.push_back(i + skip < values->size() ? cast(scalar(eval((*values)[i + skip], ctx), node), s.type)
                                                            : filler);
        } else
            v.parts = {cast(scalar(eval(c.back(), ctx), node), s.type)};
        ctx.locals.insert_or_assign(text(node, "id"), std::move(v));
    }
    void statement(const Json &node, Context &ctx) {
        checkpoint();
        if (auto active = known(ctx.active); active && !*active)
            return;
        const auto k = text(node, "kind");
        const auto &c = children(node);
        if (k.empty() || k == "NullStmt" || k == "StaticAssertDecl")
            return;
        if (k == "CompoundStmt" || k == "DeclStmt") {
            for (const auto &child : c)
                statement(child, ctx);
            return;
        }
        if (k == "VarDecl") {
            declare(node, ctx);
            return;
        }
        if (k == "ReturnStmt") {
            if (c.size() != 1)
                fail(node, "kernel/helper must return an integer");
            auto result = cast(scalar(eval(c[0], ctx), node), ctx.result_type);
            ctx.result = mux(ctx.active, result, ctx.result);
            ctx.active = constant({1, false, true}, 0);
            return;
        }
        if (k == "IfStmt") {
            if (flag(node, "hasInit") || flag(node, "hasVar") || c.size() < 2 || c.size() > 3)
                fail(node, "if initializers/condition declarations unsupported");
            auto condition = boolean(scalar(eval(c[0], ctx), node));
            if (auto v = known(condition)) {
                if (*v)
                    statement(c[1], ctx);
                else if (c.size() == 3)
                    statement(c[2], ctx);
                return;
            }
            auto a = ctx, b = ctx;
            a.active = binary("&&", ctx.active, condition, {1, false, true}, node);
            b.active = binary("&&", ctx.active, unary("!", condition, {1, false, true}, node), {1, false, true}, node);
            statement(c[1], a);
            if (c.size() == 3)
                statement(c[2], b);
            merge(ctx, condition, a, b);
            return;
        }
        if (k == "ForStmt") {
            if (c.size() != 5 || !text(c[1], "kind").empty() || text(c[2], "kind").empty() ||
                text(c[3], "kind").empty())
                fail(node, "for requires an explicit static induction and condition");
            statement(c[0], ctx);
            while (true) {
                auto condition = known(boolean(scalar(eval(c[2], ctx), node)));
                if (!condition)
                    fail(node, "loop condition must be compile-time bounded");
                if (!*condition)
                    break;
                if (++circuit_.iterations > request_.max_unroll)
                    fail(node, "loop unroll budget exceeded");
                statement(c[4], ctx);
                if (auto active = known(ctx.active); active && !*active)
                    break;
                (void)eval(c[3], ctx);
            }
            return;
        }
        if (k == "AttributedStmt")
            fail(node, "statement synthesis attributes require a supported backend");
        (void)eval(node, ctx);
    }
    Type return_type(const Function &function) const {
        auto signature = text(function.ast->at("type"), "qualType");
        auto at = signature.find("->");
        if (at != signature.npos)
            return shape(trim(signature.substr(at + 2)), *function.ast).type;
        at = signature.find('(');
        if (at == signature.npos)
            fail(*function.ast, "invalid function type");
        return shape(trim(signature.substr(0, at)), *function.ast).type;
    }
    unsigned invoke(const Function &f, const std::vector<Value> &args) {
        if (calls_.size() >= 16 || std::ranges::find(calls_, f.name) != calls_.end())
            fail(*f.ast, "recursion or helper call depth exceeded");
        calls_.push_back(f.name);
        Context ctx;
        ctx.result_type = return_type(f);
        ctx.active = constant({1, false, true}, 1);
        ctx.result = constant(ctx.result_type, 0);
        const Json *body = nullptr;
        unsigned index = 0;
        for (const auto &c : children(*f.ast)) {
            if (text(c, "kind") == "ParmVarDecl") {
                if (index >= args.size())
                    fail(c, "helper argument count mismatch");
                auto s = shape(c);
                if (s.reference && !s.constant)
                    fail(c, "mutable reference parameters unsupported");
                const auto &arg = args[index++];
                if (s.array != arg.array || arg.parts.size() != s.count)
                    fail(c, "helper array shape mismatch");
                Value v{s.type, {}, s.array};
                for (auto n : arg.parts)
                    v.parts.push_back(cast(n, s.type));
                ctx.locals[text(c, "id")] = std::move(v);
            } else if (text(c, "kind") == "CompoundStmt")
                body = &c;
        }
        if (index != args.size() || !body)
            fail(*f.ast, "missing function body/arguments");
        statement(*body, ctx);
        if (auto active = known(ctx.active); !active || *active)
            fail(*f.ast, "not every path returns a value");
        calls_.pop_back();
        return ctx.result;
    }

  public:
    Lowerer(const Json &ast, const HlsRequest &request, std::string_view source, std::stop_token stop,
            core::Deadline deadline)
        : request_(request), source_(source), stop_(stop), deadline_(deadline) {
        index(ast);
    }
    Circuit run() {
        const Function *entry = nullptr;
        for (const auto &[id, f] : functions_) {
            (void)id;
            if (f.name == request_.entry ||
                (request_.entry.find("::") == request_.entry.npos && text(*f.ast, "name") == request_.entry)) {
                if (entry)
                    fail(*f.ast, "ambiguous/overloaded synthesis entry");
                entry = &f;
            }
        }
        if (!entry)
            throw std::runtime_error("HLS entry definition not found: " + request_.entry);
        circuit_.result_type = return_type(*entry);
        std::vector<Value> args;
        for (const auto &p : children(*entry->ast))
            if (text(p, "kind") == "ParmVarDecl") {
                auto s = shape(p);
                if (s.reference && !s.constant)
                    fail(p, "input reference must be const");
                auto name = text(p, "name");
                if (!identifier(name))
                    fail(p, "input needs a portable named parameter");
                HlsPort port{"arg_" + name, name, s.type.bits, s.count, s.type.sign};
                circuit_.ports.push_back(port);
                Value v{s.type, {}, s.array};
                for (unsigned i = 0; i < s.count; ++i) {
                    auto signal = port.name;
                    if (s.array)
                        signal += "[" + std::to_string(i * s.type.bits) + " +: " + std::to_string(s.type.bits) + "]";
                    v.parts.push_back(add(s.type, "input", {}, std::nullopt, signal));
                }
                circuit_.parameters.push_back(v.parts);
                args.push_back(std::move(v));
            }
        circuit_.result = invoke(*entry, args);
        return std::move(circuit_);
    }
};
} // namespace
bool identifier(std::string_view s) {
    return !s.empty() && (std::isalpha(static_cast<unsigned char>(s[0])) || s[0] == '_') &&
           std::ranges::all_of(s, [](unsigned char c) { return std::isalnum(c) || c == '_'; });
}
Circuit lower(const Json &ast, const HlsRequest &request, std::string_view source, std::stop_token stop,
              core::Deadline deadline) {
    if (text(ast, "kind") != "TranslationUnitDecl")
        throw std::runtime_error("HLS frontend did not return a Clang translation unit");
    return Lowerer(ast, request, source, stop, deadline).run();
}
Json circuit_json(const Circuit &c) {
    Json::Array nodes;
    for (const auto &n : c.nodes) {
        Json::Array inputs;
        for (auto i : n.inputs)
            inputs.emplace_back(i);
        nodes.emplace_back(Json::Object{{"bits", n.type.bits},
                                        {"signed", n.type.sign},
                                        {"boolean", n.type.boolean},
                                        {"operation", n.op},
                                        {"inputs", std::move(inputs)},
                                        {"name", n.name}});
    }
    return Json::Object{{"nodes", std::move(nodes)}, {"result", c.result}, {"unrolledIterations", c.iterations}};
}
} // namespace ii::builder::detail::hls
