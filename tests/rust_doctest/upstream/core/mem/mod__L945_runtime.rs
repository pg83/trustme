// Extracted from library/core/src/mem/mod.rs:945
#![allow(unused)]
#![allow(dropping_copy_types)]
fn main() {
    #[derive(Copy, Clone)]
    struct Foo(u8);
    
    let x = 1;
    let y = Foo(2);
    drop(x); // a copy of `x` is moved and dropped
    drop(y); // a copy of `y` is moved and dropped
    
    println!("x: {}, y: {}", x, y.0); // still available
}
