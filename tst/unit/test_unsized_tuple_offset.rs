#![feature(offset_of_slice)]

use std::mem::offset_of;

type Tail = (i16, [i32]);

fn main() {
    assert_eq!(offset_of!(Tail, 1), 4);
}
