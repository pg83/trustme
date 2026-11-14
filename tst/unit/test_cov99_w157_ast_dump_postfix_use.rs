//@ compile-flags: -Zdump-ast
//@ output-contains: _1_ast.rs .use
#![feature(ergonomic_clones)]

fn retained_pair(value: u32) -> (u32, u32) {
    (value.use, value)
}

fn main() {
    assert_eq!(retained_pair(157), (157, 157));
}
