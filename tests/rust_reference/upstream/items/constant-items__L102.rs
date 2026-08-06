// Extracted from src/items/constant-items.md:102
#![allow(unused)]
fn main() {
    // Compile-time panic
    const PANIC: () = std::unimplemented!();
    
    fn unused_generic_function<T>() {
        // A failing compile-time assertion
        const _: () = assert!(usize::BITS == 0);
    }
}
