// Extracted from src/other-reprs.md:108
#![allow(unused)]
fn main() {
    use std::mem::size_of;
    enum MyOption<T> {
        Some(T),
        None,
    }
    
    #[repr(u8)]
    enum MyReprOption<T> {
        Some(T),
        None,
    }
    
    assert_eq!(8, size_of::<MyOption<&u16>>());
    assert_eq!(16, size_of::<MyReprOption<&u16>>());
}
