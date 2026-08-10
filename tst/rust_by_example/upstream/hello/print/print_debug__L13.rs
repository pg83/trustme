// Extracted from src/hello/print/print_debug.md:13
#![allow(unused)]
fn main() {
    // This structure cannot be printed either with `fmt::Display` or
    // with `fmt::Debug`.
    struct UnPrintable(i32);
    
    // The `derive` attribute automatically creates the implementation
    // required to make this `struct` printable with `fmt::Debug`.
    #[derive(Debug)]
    struct DebugPrintable(i32);
}
