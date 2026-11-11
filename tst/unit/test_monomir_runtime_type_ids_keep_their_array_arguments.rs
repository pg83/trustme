//@ crate-type: lib
//@ compile-flags: -Ccodegen-type=monomir -Cemit-cpp-only -Zmir-opt-level=0
//@ output-contains: .mir "type_id"<
#![feature(core_intrinsics)]
#![no_std]

#[no_mangle]
pub fn three_items() -> core::any::TypeId {
    core::intrinsics::type_id::<[u16; 3]>()
}

#[no_mangle]
pub fn four_items() -> core::any::TypeId {
    core::intrinsics::type_id::<[u16; 4]>()
}
