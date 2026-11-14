pub trait Byte {}

impl Byte for u8 {}

const OBJECT: *const dyn Byte = &b"Q"[0] as *const u8 as *const dyn Byte;

pub const fn object<T>() -> *const dyn Byte {
    OBJECT
}
