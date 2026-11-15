#![feature(extern_types)]

pub struct Token<const N: usize>;

extern "C" {
    pub type Opaque;
}

pub fn identity<const N: usize>(pointer: *const Opaque, _token: Token<N>) -> *const Opaque {
    pointer
}
