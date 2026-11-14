//@ compile-only
//@ compile-flags: -Zstop-after=expand -Zdump-ast
//@ output-contains: _1_ast.rs .. /*null*/
fn main() {
    (10u8..) += 1u8;
}
