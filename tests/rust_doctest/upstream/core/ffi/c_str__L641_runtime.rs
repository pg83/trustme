// Extracted from library/core/src/ffi/c_str.rs:641
#![allow(unused)]
#![feature(cstr_display)]
fn main() {
    
    let cstr = c"Hello, world!";
    println!("{}", cstr.display());
}
