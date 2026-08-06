// Extracted from src/names/scopes.md:255
#![allow(unused)]
fn main() {
    // Loop label shadowing example.
    'a: for outer in 0..5 {
        'a: for inner in 0..5 {
            // This terminates the inner loop, but the outer loop continues to run.
            break 'a;
        }
    }
}
