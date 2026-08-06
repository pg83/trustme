// Extracted from src/expressions/array-expr.md:98
#![allow(unused)]
fn main() {
    // The temporary holding the result of `vec![()]` is dropped at the
    // end of the statement, so it's an error to use `y` after.
    let y = &*std::ops::Index::index(&vec![()], 0); // ERROR
    y;
}
