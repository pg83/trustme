#![feature(never_type)]

use std::mem;

fn main() {
    let values: [!; 0] = [];
    assert_eq!(mem::size_of_val(&values), 0);
    assert_eq!(mem::align_of_val(&values), 1);
    assert_eq!(mem::align_of::<[!; 0]>(), 1);
}
