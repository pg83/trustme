// Extracted from library/core/src/ptr/mut_ptr.rs:360
#![allow(unused)]
#![feature(ptr_as_uninit)]
fn main() {

    let ptr: *mut u8 = &mut 10u8 as *mut u8;

    unsafe {
        if let Some(val_back) = ptr.as_uninit_ref() {
            println!("We got back the value: {}!", val_back.assume_init());
        }
    }
}
