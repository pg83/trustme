#![feature(intrinsics)]
#![allow(internal_features)]

extern "rust-intrinsic" {
    fn trustme_slice_len<T>(value: *const [T]) -> usize;
}

#[inline(never)]
fn length<T>(value: &[T]) -> usize {
    unsafe { trustme_slice_len(value as *const [T]) }
}

fn main() {
    let values = [3u16, 5, 7, 11, 13, 17];
    assert_eq!(length(std::hint::black_box(&values[1..5])), 4);
    assert_eq!(length(std::hint::black_box(&values[..0])), 0);
    assert_eq!(length(std::hint::black_box(&values[..])), 6);
    let empty_items = [(); 7];
    assert_eq!(length(std::hint::black_box(&empty_items[2..])), 5);
}
