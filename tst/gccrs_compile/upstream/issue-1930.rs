// { dg-options "-w" }

#![feature(lang_items)]
fn test<T>(x: *mut T) {
    let x = x as *mut u8;
}
