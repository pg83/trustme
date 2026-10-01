// Upstream leaves out of an enum's layout every variant that is uninhabited
// and has only 1-ZST fields (`absent`, rustc_abi layout.rs): none present is
// the never type, one present is that variant's struct, and the niche of
// the rest numbers only the variants that need a discriminant. We gave
// `Result<u32, Infallible>` a tag and `Option<!>` a byte.
#![feature(never_type)]
#![allow(dead_code)]
use std::convert::Infallible;
use std::mem::{align_of, discriminant, size_of};

enum Void {}

enum UnitOrNever {
    A,
    B(!),
}

enum VoidFirst {
    A(Void),
    B(u64, u8),
}

enum NicheSkip {
    A(&'static u8),
    B(!),
    C,
}

enum NicheMiddle {
    A(!),
    B,
    C(&'static u8),
    D(Void),
    E,
}

enum AllAbsent {
    A(!),
    B(Void),
}

enum PresentUninhabited {
    A(u32),
    B(u16, Void),
}

#[repr(C)]
enum CAbsent {
    A(u32),
    B(Void),
}

enum AlignedAbsent {
    A(u8),
    B([u64; 0], Void),
}

fn unwrap(r: Result<u32, Infallible>) -> u32 {
    match r {
        Ok(v) => v,
        Err(e) => match e {},
    }
}

fn main() {
    assert_eq!((size_of::<Result<u32, Infallible>>(), align_of::<Result<u32, Infallible>>()), (4, 4));
    assert_eq!(size_of::<Option<Result<u32, Infallible>>>(), 8);
    assert_eq!(size_of::<Option<Result<&u8, Infallible>>>(), 8);
    assert_eq!((size_of::<UnitOrNever>(), align_of::<UnitOrNever>()), (0, 1));
    assert_eq!((size_of::<VoidFirst>(), align_of::<VoidFirst>()), (16, 8));
    assert_eq!(size_of::<NicheSkip>(), 8);
    assert_eq!(size_of::<NicheMiddle>(), 16);
    assert_eq!((size_of::<AllAbsent>(), align_of::<AllAbsent>()), (0, 1));
    assert_eq!(size_of::<PresentUninhabited>(), 8);
    assert_eq!(size_of::<CAbsent>(), 8);
    assert_eq!((size_of::<AlignedAbsent>(), align_of::<AlignedAbsent>()), (8, 8));
    assert_eq!((size_of::<Option<!>>(), size_of::<Option<Void>>()), (0, 0));
    assert_eq!(size_of::<Option<(u8, Void)>>(), 2);

    assert_eq!(unwrap(Ok(7)), 7);
    let w = match VoidFirst::B(5, 6) {
        VoidFirst::A(v) => match v {},
        VoidFirst::B(a, b) => a + b as u64,
    };
    assert_eq!(w, 11);
    let mut sum = 0u32;
    for x in &[NicheSkip::C, NicheSkip::A(&3)] {
        sum += match x {
            NicheSkip::A(r) => **r as u32,
            NicheSkip::B(_) => 100,
            NicheSkip::C => 10,
        };
    }
    let m = [NicheMiddle::B, NicheMiddle::C(&4), NicheMiddle::E];
    for x in &m {
        sum += match x {
            NicheMiddle::B => 1000,
            NicheMiddle::C(r) => **r as u32 * 10000,
            NicheMiddle::E => 100000,
            _ => 7,
        };
    }
    assert_eq!(sum, 141013);
    assert!(discriminant(&m[0]) == discriminant(&NicheMiddle::B) && discriminant(&m[1]) != discriminant(&m[2]));
    assert!(matches!(Some(UnitOrNever::A), Some(UnitOrNever::A)));

    const C: Result<u32, Infallible> = Ok(9);
    const D: NicheMiddle = NicheMiddle::E;
    const E: NicheSkip = NicheSkip::C;
    assert_eq!(unwrap(C), 9);
    assert!(matches!(D, NicheMiddle::E));
    assert!(matches!(E, NicheSkip::C));
}
