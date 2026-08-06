// Extracted from library/core/src/cell/lazy.rs:21
#![allow(unused)]
fn main() {
    use std::cell::LazyCell;
    
    let lazy: LazyCell<i32> = LazyCell::new(|| {
        println!("initializing");
        92
    });
    println!("ready");
    println!("{}", *lazy);
    println!("{}", *lazy);
    
    // Prints:
    //   ready
    //   initializing
    //   92
    //   92
}
