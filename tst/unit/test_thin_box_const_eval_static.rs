#![feature(thin_box)]

use std::boxed::ThinBox;

fn main() {
    let value = ThinBox::<[u8]>::new_unsize([5u8]);
    assert_eq!(&*value, &[5]);
}
