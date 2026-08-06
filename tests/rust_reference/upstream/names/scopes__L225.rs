// Extracted from src/names/scopes.md:225
#![allow(unused)]
fn main() {
    'a: for n in 0..3 {
        if n % 2 == 0 {
            break 'a;
        }
        fn inner() {
            // Using 'a here would be an error.
            // break 'a;
        }
    }
    
    // The label is in scope for the expression of `while` loops.
    'a: while break 'a {}         // Loop does not run.
    'a: while let _ = break 'a {} // Loop does not run.
    
    // The label is not in scope in the defining `for` loop:
    'a: for outer in 0..5 {
        // This will break the outer loop, skipping the inner loop and stopping
        // the outer loop.
        'a: for inner in { break 'a; 0..1 } {
            println!("{}", inner); // This does not run.
        }
        println!("{}", outer); // This does not run, either.
    }
}
