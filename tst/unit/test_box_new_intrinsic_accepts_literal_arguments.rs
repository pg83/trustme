#![feature(intrinsics)]
#![allow(internal_features)]

#[rustc_intrinsic]
fn box_new<T>(value: T) -> Box<T>;

fn main() {
    let number = box_new(37u16);
    let truth = box_new(true);
    let letter = box_new('q');
    assert_eq!(*number, 37);
    assert!(*truth);
    assert_eq!(*letter, 'q');
}
