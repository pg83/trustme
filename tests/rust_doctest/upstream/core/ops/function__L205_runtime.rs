// Extracted from library/core/src/ops/function.rs:205
#![allow(unused)]
fn main() {
    fn consume_with_relish<F>(func: F)
        where F: FnOnce() -> String
    {
        // `func` consumes its captured variables, so it cannot be run more
        // than once.
        println!("Consumed: {}", func());
    
        println!("Delicious!");
    
        // Attempting to invoke `func()` again will throw a `use of moved
        // value` error for `func`.
    }
    
    let x = String::from("x");
    let consume_and_return_x = move || x;
    consume_with_relish(consume_and_return_x);
    
    // `consume_and_return_x` can no longer be invoked at this point
}
