#![feature(no_core)]
#![no_core]
#![no_main]
extern crate own_hashed_dylib_value;
#[no_mangle]
extern "C" fn main() -> i32 {
    match own_hashed_dylib_value::answer() { 431 => 0, _ => 1 }
}
