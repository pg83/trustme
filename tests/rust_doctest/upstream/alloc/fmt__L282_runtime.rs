// Extracted from library/alloc/src/fmt.rs:282
#![allow(unused)]
extern crate alloc;
fn main() {
    println!("{}, `{name:.*}` has 3 fractional digits", "Hello", 3, name=1234.56);
    println!("{}, `{name:.*}` has 3 characters", "Hello", 3, name="1234.56");
    println!("{}, `{name:>8.*}` has 3 right-aligned characters", "Hello", 3, name="1234.56");
}
