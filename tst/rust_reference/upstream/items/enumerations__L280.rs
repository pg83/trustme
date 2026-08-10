// Extracted from src/items/enumerations.md:280
#![allow(unused)]
fn main() {
    #[repr(u8)]
    enum Enum {
        Unit,
        Tuple(bool),
        Struct{a: bool},
    }
    
    impl Enum {
        fn discriminant(&self) -> u8 {
            unsafe { *(self as *const Self as *const u8) }
        }
    }
    
    let unit_like = Enum::Unit;
    let tuple_like = Enum::Tuple(true);
    let struct_like = Enum::Struct{a: false};
    
    assert_eq!(0, unit_like.discriminant());
    assert_eq!(1, tuple_like.discriminant());
    assert_eq!(2, struct_like.discriminant());
}
