// Extracted from src/expressions/block-expr.md:226
#![allow(unused)]
fn main() {
    loop {
        async move {
            break; // error[E0267]: `break` inside of an `async` block
        }
    }
}
