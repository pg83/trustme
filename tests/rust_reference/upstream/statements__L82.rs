// Extracted from src/statements.md:82
#![allow(unused)]
fn main() {
    let (mut v, w) = (vec![1, 2, 3], 42); // The bindings may be mut or const
    let Some(t) = v.pop() else { // Refutable patterns require an else block
        panic!(); // The else block must diverge
    };
    let [u, v] = [v[0], v[1]] else { // This pattern is irrefutable, so the compiler
                                     // will lint as the else block is redundant.
        panic!();
    };
}
