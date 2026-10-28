//@ compile-flags: -Cemit-cpp-only
//@ crate-type: lib
#![allow(dead_code)]

global_asm!(".text\n.global metadata_asm_symbol\nmetadata_asm_symbol:");

#[repr(C)]
pub struct Layout {
    pub a: u8,
    pub b: u64,
}

#[repr(packed)]
pub struct Packed {
    pub a: u8,
    pub b: u16,
}

#[no_mangle]
#[inline(never)]
#[cold]
#[track_caller]
#[target_feature(enable = "avx2")]
#[link_section = ".text.metadata"]
pub extern "C" fn marked_function(value: i32) -> i32 {
    value
}

#[used]
#[link_section = ".data.metadata"]
pub static MARKED_STATIC: Layout = Layout { a: 1, b: 2 };

pub const CONST_LAYOUT: Layout = Layout { a: 3, b: 4 };

pub fn generic_inline<T>(value: T) -> T {
    value
}
