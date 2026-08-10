// Extracted from library/core/src/ptr/mut_ptr.rs:318
#![allow(unused)]
#![feature(ptr_as_ref_unchecked)]
fn main() {
    let ptr: *mut u8 = &mut 10u8 as *mut u8;

    unsafe {
        println!("We got back the value: {}!", ptr.as_ref_unchecked());
    }
}
