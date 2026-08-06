// Extracted from library/core/src/mem/mod.rs:1344
#![allow(unused)]
fn main() {
    struct Wrapper<T, U>(T, U);
    
    type A = Wrapper<u8, u8>;
    type B = Wrapper<u8, i8>;
    
    // Not necessarily identical even though `u8` and `i8` have the same layout!
    // assert_eq!(mem::offset_of!(A, 1), mem::offset_of!(B, 1));
    
    #[repr(transparent)]
    struct U8(u8);
    
    type C = Wrapper<u8, U8>;
    
    // Not necessarily identical even though `u8` and `U8` have the same layout!
    // assert_eq!(mem::offset_of!(A, 1), mem::offset_of!(C, 1));
    
    struct Empty<T>(core::marker::PhantomData<T>);
    
    // Not necessarily identical even though `PhantomData` always has the same layout!
    // assert_eq!(mem::offset_of!(Empty<u8>, 0), mem::offset_of!(Empty<i8>, 0));
}
