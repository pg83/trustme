//@ crate-type: lib
//@ compile-flags: -Cemit-cpp-only
//@ output-contains: .cpp _trustme_tail[
#[repr(align(536870912))]
pub struct Aligned(pub u8);
#[repr(C)]
pub enum Wide { First(Aligned), Second(u16) }
const _: () = assert!(std::mem::size_of::<Wide>() == 1073741824);
const _: () = assert!(std::mem::align_of::<Wide>() == 536870912);
pub fn read(value: &Wide) -> u16 {
    match value { Wide::First(n) => n.0 as u16, Wide::Second(n) => *n }
}
