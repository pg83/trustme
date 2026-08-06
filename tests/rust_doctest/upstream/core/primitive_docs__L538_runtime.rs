// Extracted from library/core/src/primitive_docs.rs:538
#![allow(unused)]
fn main() {
    let my_num: Box<i32> = Box::new(10);
    let my_num_ptr: *const i32 = &*my_num;
    let mut my_speed: Box<i32> = Box::new(88);
    let my_speed_ptr: *mut i32 = &mut *my_speed;
}
