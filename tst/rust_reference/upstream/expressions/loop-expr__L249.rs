// Extracted from src/expressions/loop-expr.md:249
#![allow(unused)]
fn main() {
    'a: loop {
        'a: loop {
            break 'a;
        }
        print!("outer loop");
        break 'a;
    }
}
