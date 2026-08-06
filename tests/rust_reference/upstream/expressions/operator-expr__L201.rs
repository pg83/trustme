// Extracted from src/expressions/operator-expr.md:201
#![allow(unused)]
fn main() {
    // The temporary holding the result of `String::new()` is dropped at
    // the end of the statement, so it's an error to use `y` after.
    let y = &*std::ops::Deref::deref(&String::new()); // ERROR
    y;
}
