#![feature(no_core)]
#![no_core]
#![no_main]
extern crate own_dynamic_adapter;
#[no_mangle]
extern "C" fn main() -> i32 {
    match own_dynamic_adapter::through(173) { 173 => 0, _ => 1 }
}
