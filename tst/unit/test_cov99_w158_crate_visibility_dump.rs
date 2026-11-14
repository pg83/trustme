//@ compile-flags: -Z dump-ast
//@ output-contains: _1_ast.rs crate fn crate_visible

crate fn crate_visible() -> u32 {
    158
}

fn main() {
    assert_eq!(crate_visible(), 158);
}
