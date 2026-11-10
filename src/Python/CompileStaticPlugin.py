"""Compile a bounded, pure integer Python function into a native Core plugin.

This is a host build tool. The emitted C++ has no Python interpreter dependency.
Unsupported syntax fails rather than falling back to runtime interpretation.
"""
import argparse
import ast
from pathlib import Path
import re
import sys


class Unsupported(ValueError):
    pass


def compile_module(path):
    identifier = path.stem
    if not re.fullmatch(r"[A-Za-z][A-Za-z0-9_]{0,255}", identifier):
        raise Unsupported("plugin filename must be an ASCII identifier")
    tree = ast.parse(path.read_text(encoding="utf-8"), filename=str(path))
    if len(tree.body) != 1 or not isinstance(tree.body[0], ast.FunctionDef):
        raise Unsupported("expected exactly one top-level function and no imports or top-level statements")
    function = tree.body[0]
    args = function.args
    positional = [*args.posonlyargs, *args.args]
    if (len(positional) != 1 or args.vararg or args.kwarg or args.kwonlyargs or args.defaults
            or function.decorator_list or getattr(function, "type_params", ())
            or not isinstance(positional[0].annotation, ast.Name) or positional[0].annotation.id != "int"
            or not isinstance(function.returns, ast.Name) or function.returns.id != "int"):
        raise Unsupported("expected one undecorated function with one int argument and an int result")
    if len(function.body) != 1 or not isinstance(function.body[0], ast.Return) or function.body[0].value is None:
        raise Unsupported("function body must be a single return expression")
    argument = positional[0].arg

    def integer(node):
        if isinstance(node, ast.Name) and node.id == argument:
            return "value"
        if isinstance(node, ast.Constant) and type(node.value) is int:
            if not -(1 << 63) <= node.value < (1 << 63):
                raise Unsupported("integer literal exceeds the signed 64-bit ABI")
            return f"std::int64_t{{{node.value}}}"
        if isinstance(node, ast.UnaryOp) and isinstance(node.op, ast.USub):
            if isinstance(node.operand, ast.Constant) and type(node.operand.value) is int and node.operand.value == 1 << 63:
                return "std::numeric_limits<std::int64_t>::min()"
            return f"neg({integer(node.operand)})"
        if isinstance(node, ast.UnaryOp) and isinstance(node.op, ast.UAdd):
            return integer(node.operand)
        if isinstance(node, ast.BinOp):
            operations = {ast.Add: "add", ast.Sub: "sub", ast.Mult: "mul", ast.FloorDiv: "floor_div", ast.Mod: "floor_mod"}
            name = operations.get(type(node.op))
            if name:
                return f"{name}({integer(node.left)}, {integer(node.right)})"
        if isinstance(node, ast.IfExp):
            return f"({condition(node.test)} ? {integer(node.body)} : {integer(node.orelse)})"
        raise Unsupported(f"unsupported integer expression at line {getattr(node, 'lineno', '?')}: {type(node).__name__}")

    def condition(node):
        if isinstance(node, ast.Compare) and len(node.ops) == len(node.comparators) == 1:
            operations = {ast.Eq: "==", ast.NotEq: "!=", ast.Lt: "<", ast.LtE: "<=", ast.Gt: ">", ast.GtE: ">="}
            op = operations.get(type(node.ops[0]))
            if op:
                return f"({integer(node.left)} {op} {integer(node.comparators[0])})"
        raise Unsupported(f"unsupported condition at line {getattr(node, 'lineno', '?')}")

    expression = integer(function.body[0].value)
    return TEMPLATE.replace("@IDENTIFIER@", identifier).replace("@SIZE@", str(len(identifier))).replace("@EXPRESSION@", expression)


TEMPLATE = r'''// Generated native Python subset; no target Python runtime.
#include <iiSDKCore/Plugin/Plugin.h>
#include <bit>
#include <cstdint>
#include <limits>
#include <stdexcept>
namespace {
using I = std::int64_t;
constexpr I lo=std::numeric_limits<I>::min(), hi=std::numeric_limits<I>::max();
[[maybe_unused]] I add(I a,I b){if((b>0&&a>hi-b)||(b<0&&a<lo-b))throw std::overflow_error("add");return a+b;}
[[maybe_unused]] I sub(I a,I b){if((b<0&&a>hi+b)||(b>0&&a<lo+b))throw std::overflow_error("sub");return a-b;}
[[maybe_unused]] I neg(I a){if(a==lo)throw std::overflow_error("neg");return -a;}
[[maybe_unused]] I mul(I a,I b){
    if((a>0&&b>0&&a>hi/b)||(a>0&&b<0&&b<lo/a)||(a<0&&b>0&&a<lo/b)||(a<0&&b<0&&a<hi/b))
        throw std::overflow_error("mul");
    return a*b;
}
[[maybe_unused]] I floor_div(I a,I b){
    if(!b||(a==lo&&b==-1))throw std::overflow_error("div");
    I q=a/b,r=a%b;if(r&&((r<0)!=(b<0)))--q;return q;
}
[[maybe_unused]] I floor_mod(I a,I b){
    if(!b)throw std::overflow_error("mod");
    if(a==lo&&b==-1)return 0;
    I r=a%b;if(r&&((r<0)!=(b<0)))r+=b;return r;
}
void* create() noexcept {static int value=1;return &value;}
void destroy(void*) noexcept {}
std::int32_t invoke(void*,std::uint32_t operation,const std::uint8_t* input,std::uint64_t size,
                    std::uint8_t* output,std::uint64_t capacity,std::uint64_t* written) noexcept {
    if(operation!=0||size!=8||capacity<8||!input||!output||!written)return 1;
    try {
        std::uint64_t bits=0;for(unsigned i=0;i<8;++i)bits|=std::uint64_t(input[i])<<(8*i);
        const I value=std::bit_cast<I>(bits);
        const I answer=@EXPRESSION@;
        bits=std::bit_cast<std::uint64_t>(answer);
        for(unsigned i=0;i<8;++i)output[i]=static_cast<std::uint8_t>(bits>>(8*i));
        *written=8;return 0;
    } catch (...) {return 2;}
}
const ii::plugin::Descriptor descriptor{sizeof(ii::plugin::Descriptor),ii::plugin::abi_version,
                                         "@IDENTIFIER@",@SIZE@,create,destroy,invoke};
}
extern "C" const ii::plugin::Descriptor* ii_sdk_plugin_v1() noexcept {return &descriptor;}
'''


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    args = parser.parse_args()
    try:
        print(compile_module(args.source), end="")
    except (OSError, UnicodeError, SyntaxError, Unsupported) as error:
        print(f"native Python AOT: {args.source}: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
