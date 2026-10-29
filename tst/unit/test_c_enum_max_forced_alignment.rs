//@ crate-type: lib
#[repr(C, align(536870912))]
pub enum Wide { Byte(u8), Word(u16) }
const _: () = assert!(std::mem::size_of::<Wide>() == 536870912);
const _: () = assert!(std::mem::align_of::<Wide>() == 536870912);
pub fn read(value: &Wide) -> u16 {
    match value { Wide::Byte(n) => *n as u16, Wide::Word(n) => *n }
}
