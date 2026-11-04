#![feature(no_core)]
#![no_core]
#![no_main]
extern crate own_dynamic_base;
extern crate own_dynamic_adapter;
#[no_mangle]
extern "C" fn main() -> i32 {
    match (own_dynamic_adapter::through(173), own_dynamic_base::keep(179)) {
        (173, 179) => 0,
        _ => 1,
    }
}
