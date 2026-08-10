// Extracted from src/expressions/loop-expr.md:289
#![allow(unused)]
fn main() {
    'outer: loop {
        while true {
            break 'outer;
        }
    }
}
