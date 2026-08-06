// Extracted from library/core/src/mem/mod.rs:1155
#![allow(unused)]
fn main() {
    #[repr(u8)]
    enum Enum {
        Unit,
        Tuple(bool),
        Struct { a: bool },
    }
    
    impl Enum {
        fn discriminant(&self) -> u8 {
            // SAFETY: Because `Self` is marked `repr(u8)`, its layout is a `repr(C)` `union`
            // between `repr(C)` structs, each of which has the `u8` discriminant as its first
            // field, so we can read the discriminant without offsetting the pointer.
            unsafe { *<*const _>::from(self).cast::<u8>() }
        }
    }
    
    let unit_like = Enum::Unit;
    let tuple_like = Enum::Tuple(true);
    let struct_like = Enum::Struct { a: false };
    assert_eq!(0, unit_like.discriminant());
    assert_eq!(1, tuple_like.discriminant());
    assert_eq!(2, struct_like.discriminant());
    
    // ⚠️ This is undefined behavior. Don't do this. ⚠️
    // assert_eq!(0, unsafe { std::mem::transmute::<_, u8>(std::mem::discriminant(&unit_like)) });
}
