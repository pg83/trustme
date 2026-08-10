// Extracted from library/std/src/alloc.rs:81
use std::alloc::System;

#[global_allocator]
static A: System = System;

fn main() {
    let a = Box::new(4); // Allocates from the system allocator.
    println!("{a}");
}
