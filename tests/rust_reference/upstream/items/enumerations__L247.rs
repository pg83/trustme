// Extracted from src/items/enumerations.md:247
#![allow(unused)]
fn main() {
    enum Fieldless {
        Tuple(),
        Struct{},
        Unit,
    }
    
    assert_eq!(0, Fieldless::Tuple() as isize);
    assert_eq!(1, Fieldless::Struct{} as isize);
    assert_eq!(2, Fieldless::Unit as isize);
    
    #[repr(u8)]
    enum FieldlessWithDiscriminants {
        First = 10,
        Tuple(),
        Second = 20,
        Struct{},
        Unit,
    }
    
    assert_eq!(10, FieldlessWithDiscriminants::First as u8);
    assert_eq!(11, FieldlessWithDiscriminants::Tuple() as u8);
    assert_eq!(20, FieldlessWithDiscriminants::Second as u8);
    assert_eq!(21, FieldlessWithDiscriminants::Struct{} as u8);
    assert_eq!(22, FieldlessWithDiscriminants::Unit as u8);
}
