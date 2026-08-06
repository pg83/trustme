// Extracted from library/core/src/mem/maybe_uninit.rs:141
#![allow(unused)]
fn main() {
    use std::mem::MaybeUninit;
    
    // Create an uninitialized array of `MaybeUninit`.
    let mut data: [MaybeUninit<String>; 1000] = [const { MaybeUninit::uninit() }; 1000];
    // Count the number of elements we have assigned.
    let mut data_len: usize = 0;
    
    for elem in &mut data[0..500] {
        elem.write(String::from("hello"));
        data_len += 1;
    }
    
    // For each item in the array, drop if we allocated it.
    for elem in &mut data[0..data_len] {
        unsafe { elem.assume_init_drop(); }
    }
}
