#![feature(core_intrinsics)]
#![allow(internal_features)]
#[repr(C)]
struct Value(u8, u32);
trait Marker {}
impl Marker for Value {}
impl Marker for () {}
#[repr(align(32))]
struct Aligned(u8);
impl Marker for Aligned {}
const VALUE: Value = Value(3, 9);
const ALIGNED: Aligned = Aligned(7);
const DYNAMIC: &dyn Marker = &VALUE;
const SIZE: usize = std::mem::size_of_val(DYNAMIC);
const ALIGN: usize = std::mem::align_of_val(DYNAMIC);
const ZST_SIZE: usize = std::mem::size_of_val(&() as &dyn Marker);
const ZST_ALIGN: usize = std::mem::align_of_val(&() as &dyn Marker);
const LARGE_ALIGN: usize = std::mem::align_of_val(&ALIGNED as &dyn Marker);
enum Only { Item = 27 }
enum Payload { Item(u16) }
const ONLY: isize = std::intrinsics::discriminant_value(&Only::Item);
const PAYLOAD: isize = std::intrinsics::discriminant_value(&Payload::Item(12));
fn main() {
 assert_eq!((SIZE, ALIGN), (std::mem::size_of::<Value>(), std::mem::align_of::<Value>()));
 assert_eq!((ZST_SIZE, ZST_ALIGN), (0, 1));
 assert_eq!(LARGE_ALIGN, 32);
 assert_eq!(ONLY, 27);
 assert_eq!(PAYLOAD, 0);
 assert_eq!(ALIGNED.0, 7);
 let Payload::Item(value) = Payload::Item(12);
 assert_eq!(value, 12);
}
