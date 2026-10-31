trait Marker {}
impl Marker for u32 {}
#[repr(C)]
struct Header<T: ?Sized> { prefix: u8, value: T }
const DIRECT: &dyn Marker = &13_u32;
const TAIL: &Header<dyn Marker> = &Header { prefix: 7, value: 21_u32 };
const DIRECT_LAYOUT: (usize, usize) = (core::mem::size_of_val(DIRECT), core::mem::align_of_val(DIRECT));
const TAIL_LAYOUT: (usize, usize) = (core::mem::size_of_val(TAIL), core::mem::align_of_val(TAIL));
fn main() {
    assert_eq!(DIRECT_LAYOUT, (4, 4));
    assert_eq!(TAIL_LAYOUT, (8, 4));
    assert_eq!(TAIL.prefix, 7);
}
