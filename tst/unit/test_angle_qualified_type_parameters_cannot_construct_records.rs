//@ compile-fail: Unexpected token TOK_BRACE_OPEN, expected TOK_DOUBLE_COLON
#![feature(more_qualified_paths)]
fn construct<Shape>() {
    let _ = <Shape> { value: 757u16 };
}
fn main() { construct::<u16>(); }
