// Extracted from src/expressions/operator-expr.md:193
#![allow(unused)]
fn main() {
    // The temporary holding the result of `String::new()` is extended
    // to live to the end of the block, so `x` may be used in subsequent
    // statements.
    let x = &*String::new();
    x;
}
