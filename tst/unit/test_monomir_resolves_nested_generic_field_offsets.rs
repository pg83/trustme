//@ crate-type: lib
//@ compile-flags: -Ccodegen-type=monomir -Cemit-cpp-only -Zmir-opt-level=0

#[repr(C)]
pub struct Record<T> { pub tag: u8, pub payload: (u16, T) }

pub fn integer_offset() -> usize {
    core::mem::offset_of!(Record<u64>, payload.1)
}

pub fn array_offset() -> usize {
    core::mem::offset_of!(Record<[u32; 3]>, payload.1)
}

pub fn tuple_offset() -> usize {
    core::mem::offset_of!((u8, Record<u32>), 1.payload.0)
}
