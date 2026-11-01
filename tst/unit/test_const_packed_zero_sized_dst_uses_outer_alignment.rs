#[repr(C, packed)]
struct Packed { marker: (), tail: [u16] }
const LAYOUT: (usize, usize) = unsafe {
    let pointer = std::ptr::slice_from_raw_parts(1usize as *const u16, 0) as *const Packed;
    let reference = &*pointer;
    (std::mem::size_of_val(reference), std::mem::align_of_val(reference))
};
fn main() { assert_eq!(LAYOUT, (0, 1)); }
