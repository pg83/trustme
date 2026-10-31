trait Marker {}
use std::mem::ManuallyDrop;
#[repr(align(32))]
struct Data([u8; 7]);
impl Marker for Data {}
#[repr(C)]
struct Packet<T: ?Sized> { head: u8, tail: T }
#[repr(C)]
struct Outer<T: ?Sized> { head: u16, tail: Packet<T> }
#[repr(C, packed)]
struct Packed<T: ?Sized> { head: u8, tail: ManuallyDrop<T> }
const OBJECT: &Packet<dyn Marker> = &Packet { head: 17, tail: Data([23; 7]) };
const NESTED: &Outer<dyn Marker> = &Outer { head: 29, tail: Packet { head: 31, tail: Data([37; 7]) } };
const PACKED: &Packed<[u16]> = &Packed { head: 41, tail: ManuallyDrop::new([43, 47, 53]) };
const VALUES: (usize, usize, usize, usize, usize) = (
    std::mem::size_of_val(OBJECT),
    std::mem::align_of_val(OBJECT),
    std::mem::size_of_val(NESTED),
    std::mem::size_of_val(PACKED),
    std::mem::size_of_val(&17u32),
);
fn main() { assert_eq!(VALUES, (64, 32, 96, 7, 4)); }
