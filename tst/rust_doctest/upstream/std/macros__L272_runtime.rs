// Extracted from library/std/src/macros.rs:272
#![allow(unused)]
fn main() {
    fn foo(n: usize) {
        if let Some(_) = dbg!(n.checked_sub(4)) {
            // ...
        }
    }

    foo(3)
}
