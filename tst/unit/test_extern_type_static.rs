// An `extern type` has no size, and a value of one cannot be read: only its
// address is ever taken, so the symbol stands on its own.
//@ crate-type: lib
#![feature(extern_types)]

pub mod a {
    unsafe extern "C" {
        pub type StartFn;
        pub static start: StartFn;
    }
}

pub mod b {
    #[repr(transparent)]
    pub struct TransparentType(crate::a::StartFn);
    unsafe extern "C" {
        pub static start: TransparentType;
    }
}

pub mod c {
    #[repr(C)]
    pub struct CType(u32, crate::b::TransparentType);
    unsafe extern "C" {
        pub static start: CType;
    }
}

pub fn address() -> *const () {
    &raw const a::start as *const ()
}
