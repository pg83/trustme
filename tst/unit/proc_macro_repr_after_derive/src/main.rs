// A `#[repr(C, packed)]` written after a procedural-macro derive still shapes the type: the
// derive is handed the attribute but does not consume it (only a derive's declared helper
// attributes are inert).  zerocopy's `Unalign<T>` (`#[derive(Immutable, ..)]` then
// `#[repr(C, packed)]`) came out with its field's alignment, and `read_from_bytes` of a
// `u64` took the `unreachable_unchecked` branch of its alignment check.
use marker_derive::Marker;
use std::mem::{align_of, size_of};

#[derive(Marker)]
#[repr(C, packed)]
struct Unalign<T>(T);

fn align_of_generic<U>() -> usize {
    align_of::<U>()
}

fn main() {
    assert_eq!(align_of::<Unalign<u64>>(), 1);
    assert_eq!(align_of_generic::<Unalign<u64>>(), 1);
    assert_eq!(size_of::<Unalign<u64>>(), 8);
    let bytes = [0u8; 9];
    let p = bytes[1..9].as_ptr() as *const Unalign<u64>;
    let v = unsafe { std::ptr::read(p) };
    let inner = v.0;
    assert_eq!(inner, 0);
}
