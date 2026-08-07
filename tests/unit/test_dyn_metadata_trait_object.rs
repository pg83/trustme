#![feature(ptr_metadata)]

use std::fmt::Debug;
use std::ptr;

fn main() {
    let value = 7u32;
    let object: &dyn Debug = &value;
    let metadata = ptr::metadata(object);
    let rebuilt = ptr::from_raw_parts::<dyn Debug>(&value as *const u32 as *const (), metadata);

    assert_eq!(unsafe { std::mem::size_of_val(&*rebuilt) }, std::mem::size_of::<u32>());
}
