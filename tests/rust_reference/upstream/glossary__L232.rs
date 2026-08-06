// Extracted from src/glossary.md:232
#![allow(unused)]
fn main() {
    use core::mem::{size_of, size_of_val};
    fn f() {}
    struct S(u8);
    enum E { V(u8) }
    struct UnitLike;
    struct NoFields {}
    struct OnlyZST {
        f1: (),
        f2: [(); 10],
        f3: [u8; 0],
    }
    #[repr(C)]
    struct C1 {}
    #[repr(C)]
    struct C2 {
        f1: (),
        f2: [(); 10],
        f3: [u8; 0],
        f4: C1,
    }
    #[repr(transparent)]
    struct T1 {}
    #[repr(transparent)]
    struct T2 {
        f1: (),
        f2: [(); 10],
        f3: [u8; 0],
    }
    union U {
        f1: (),
        f2: [(); 10],
        f3: [u8; 0],
    }
    /// An enum with a single field-struct-like variant with all fields
    /// being ZSTs.
    enum E2 {
        V1 { f1: (), f2: [(); 10] },
    }
    /// An enum with a single field-struct-like variant with no fields.
    enum E3 {
        V1 {},
    }
    /// An enum with a single unit-struct-like variant.
    enum E4 {
        V1,
    }
    /// An enum with a single tuple-struct-like variant with all fields
    /// being ZSTs.
    enum E5 {
        V1 ((), [(); 10]),
    }
    /// An enum with a single tuple-struct-like variant with no fields.
    enum E6 {
        V1 (),
    }
    
    assert_eq!(0, size_of::<()>());
    assert_eq!(0, size_of_val(&f));
    assert_eq!(0, size_of_val(&S));
    assert_eq!(0, size_of_val(&E::V));
    assert_eq!(0, size_of::<UnitLike>());
    assert_eq!(0, size_of::<NoFields>());
    assert_eq!(0, size_of::<OnlyZST>());
    assert_eq!(0, size_of::<C1>());
    assert_eq!(0, size_of::<C2>());
    assert_eq!(0, size_of::<T1>());
    assert_eq!(0, size_of::<T2>());
    assert_eq!(0, size_of::<[(); 10]>());
    assert_eq!(0, size_of::<[u8; 0]>());
    assert_eq!(0, size_of::<U>());
    assert_eq!(0, size_of::<E2>());
    assert_eq!(0, size_of::<E3>());
    assert_eq!(0, size_of::<E4>());
    assert_eq!(0, size_of::<E5>());
    assert_eq!(0, size_of::<E6>());
}
