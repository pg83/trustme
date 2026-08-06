// Extracted from library/core/src/mem/mod.rs:1378
#![allow(unused)]
fn main() {
    use std::mem;
    #[repr(C)]
    struct FieldStruct {
        first: u8,
        second: u16,
        third: u8
    }
    
    assert_eq!(mem::offset_of!(FieldStruct, first), 0);
    assert_eq!(mem::offset_of!(FieldStruct, second), 2);
    assert_eq!(mem::offset_of!(FieldStruct, third), 4);
    
    #[repr(C)]
    struct NestedA {
        b: NestedB
    }
    
    #[repr(C)]
    struct NestedB(u8);
    
    assert_eq!(mem::offset_of!(NestedA, b.0), 0);
}
