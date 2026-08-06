// Extracted from library/core/src/mem/mod.rs:280
#![allow(unused)]
fn main() {
    #[repr(C)]
    struct FieldStruct {
        first: u8,
        second: u16,
        third: u8
    }
    
    // The size of the first field is 1, so add 1 to the size. Size is 1.
    // The alignment of the second field is 2, so add 1 to the size for padding. Size is 2.
    // The size of the second field is 2, so add 2 to the size. Size is 4.
    // The alignment of the third field is 1, so add 0 to the size for padding. Size is 4.
    // The size of the third field is 1, so add 1 to the size. Size is 5.
    // Finally, the alignment of the struct is 2 (because the largest alignment amongst its
    // fields is 2), so add 1 to the size for padding. Size is 6.
    assert_eq!(6, size_of::<FieldStruct>());
    
    #[repr(C)]
    struct TupleStruct(u8, u16, u8);
    
    // Tuple structs follow the same rules.
    assert_eq!(6, size_of::<TupleStruct>());
    
    // Note that reordering the fields can lower the size. We can remove both padding bytes
    // by putting `third` before `second`.
    #[repr(C)]
    struct FieldStructOptimized {
        first: u8,
        third: u8,
        second: u16
    }
    
    assert_eq!(4, size_of::<FieldStructOptimized>());
    
    // Union size is the size of the largest field.
    #[repr(C)]
    union ExampleUnion {
        smaller: u8,
        larger: u16
    }
    
    assert_eq!(2, size_of::<ExampleUnion>());
}
