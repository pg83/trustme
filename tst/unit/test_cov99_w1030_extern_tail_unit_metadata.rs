//@ crate-type: lib
//@ compile-flags: -Ccodegen-type=monomir
//@ output-contains: .mir DSTMETA ();
#![feature(extern_types)]

extern "C" {
    pub type Foreign;
}

#[repr(C)]
pub struct ForeignTail {
    pub head: u8,
    pub tail: Foreign,
}

#[no_mangle]
pub unsafe extern "C" fn foreign_head(value: *const ForeignTail) -> u8 {
    (*value).head
}
