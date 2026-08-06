// Extracted from src/items/functions.md:192
#![allow(unused)]
fn main() {
    // Declares a function with the "C" ABI
    extern "C" fn new_i32() -> i32 { 0 }
    
    // Declares a function with the "stdcall" ABI
    #[cfg(any(windows, target_arch = "x86"))]
    extern "stdcall" fn new_i32_stdcall() -> i32 { 0 }
}
