// Extracted from library/std/src/keyword_docs.rs:74
#![allow(unused)]
fn main() {
    'outer: for i in 1..=5 {
        println!("outer iteration (i): {i}");

        '_inner: for j in 1..=200 {
            println!("    inner iteration (j): {j}");
            if j >= 3 {
                // breaks from inner loop, lets outer loop continue.
                break;
            }
            if i >= 2 {
                // breaks from outer loop, and directly to "Bye".
                break 'outer;
            }
        }
    }
    println!("Bye.");
}
