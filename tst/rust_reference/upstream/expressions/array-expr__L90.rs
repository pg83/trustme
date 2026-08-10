// Extracted from src/expressions/array-expr.md:90
#![allow(unused)]
fn main() {
    // The temporary holding the result of `vec![()]` is extended to
    // live to the end of the block, so `x` may be used in subsequent
    // statements.
    let x = &vec![()][0];
    x;
}
