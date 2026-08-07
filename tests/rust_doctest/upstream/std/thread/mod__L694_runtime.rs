// Extracted from library/std/src/thread/mod.rs:694
#![allow(unused)]
fn main() {
    use std::thread;

    let computation = thread::spawn(|| {
        // Some expensive computation.
        42
    });

    let result = computation.join().unwrap();
    println!("{result}");
}
