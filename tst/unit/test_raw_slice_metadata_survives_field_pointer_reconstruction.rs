//@ compile-variant: -O
#![feature(ptr_metadata)]

struct Holder { values: [u16; 4] }
#[inline(never)]
fn inspect(value: &Holder) -> (usize, u16) {
    let slice: *const [u16] = &value.values;
    let length = std::ptr::metadata(slice);
    let data = slice as *const u16;
    let reconstructed = std::ptr::from_raw_parts::<[u16]>(data.cast::<()>(), length);
    (length, unsafe { (&*reconstructed)[length - 1] })
}
fn main() {
    assert_eq!(inspect(&Holder { values: [3, 5, 7, 11] }), (4, 11));
    assert_eq!(inspect(&Holder { values: [13, 17, 19, 23] }), (4, 23));
}
