#![feature(intrinsics)]
#![allow(internal_features)]

extern "rust-intrinsic" {
    fn likely(value: bool) -> bool;
    fn unlikely(value: bool) -> bool;
}

#[inline(never)]
fn hints(left: bool, right: bool) -> (bool, bool) {
    unsafe { (likely(left), unlikely(right)) }
}

fn main() {
    for left in [false, true] {
        for right in [false, true] {
            assert_eq!(
                hints(std::hint::black_box(left), std::hint::black_box(right)),
                (left, right),
            );
        }
    }
}
