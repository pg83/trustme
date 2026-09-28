// derive-where's `#[repr(C)] enum Test { A = -0x8000_0000_0000_0000_isize, B,
// C }`: the C backend switched over a `u32` tag with cases near 2^63, which
// clang rejects. Upstream picks a fieldless enum's tag as the smallest integer
// holding every discriminant - signed when one is negative - but no smaller
// than a C `int` under `repr(C)` (`repr_discr`, rustc_abi's layout): `-1`
// makes an `i32`, `isize::MIN` an `i64`, `isize::MAX - 2` a `u64`. `repr(C)`
// was always a `u32` here.
use std::mem::size_of;

#[repr(C)]
#[derive(Clone, Copy, PartialEq, PartialOrd, Debug)]
enum Negative {
    A = -0x8000_0000_0000_0000_isize,
    B,
    C,
}

#[repr(C)]
#[derive(Clone, Copy, PartialEq, PartialOrd, Debug)]
enum Large {
    A = isize::MAX - 2,
    B,
    C,
}

#[repr(C)]
#[derive(Clone, Copy, PartialEq, Debug)]
enum SmallNegative {
    A = -1,
    B,
}

#[repr(C)]
#[allow(dead_code)]
enum Small {
    A = 3,
    B,
}

fn main() {
    assert_eq!(size_of::<Negative>(), 8);
    assert_eq!(size_of::<Large>(), 8);
    assert_eq!(size_of::<SmallNegative>(), 4);
    assert_eq!(size_of::<Small>(), 4);

    assert!(Negative::A < Negative::B);
    assert_eq!(Negative::C as isize, isize::MIN + 2);
    assert!(Large::C > Large::A);
    assert_eq!(Large::C as isize, isize::MAX);

    let tag = unsafe { *(&SmallNegative::A as *const SmallNegative as *const i32) };
    assert_eq!(tag, -1);
    assert_eq!(SmallNegative::B as i32, 0);
    match Large::B {
        Large::A => panic!(),
        Large::B => {}
        Large::C => panic!(),
    }
    assert_eq!(Small::B as u32, 4);
}
