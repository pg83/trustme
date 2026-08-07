// Extracted from library/core/src/ptr/mut_ptr.rs:285
#![allow(unused)]
fn main() {
    let ptr: *mut u8 = &mut 10u8 as *mut u8;

    unsafe {
        let val_back = &*ptr;
        println!("We got back the value: {val_back}!");
    }
}
