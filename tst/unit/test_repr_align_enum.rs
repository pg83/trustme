// `#[repr(align(N))]` on a fieldless enum raises the alignment without turning
// the enum into a data enum. It used to be lowered as one, which rejected the
// explicit discriminants and then refused to cast a variant to an integer.
//
// Same shape as the upstream tests repr/aligned_enum_cast.rs and
// consts/const-enum-cast.rs.
use std::mem::{align_of, size_of};

#[repr(align(8))]
enum Aligned {
    Zero = 0,
    One = 1,
}

#[repr(align(4))]
enum Plain {
    A,
    B,
}

// The alignment says nothing about the tag, so an explicit discriminant is as
// ordinary here as on any value enum.
const X: u8 = Aligned::One as u8;

#[inline(never)]
fn toU8(a: Aligned) -> u8 {
    // Behind a call so the cast is lowered, not constant-folded.
    a as u8
}

fn main() {
    assert_eq!(align_of::<Aligned>(), 8);
    assert_eq!(size_of::<Aligned>(), 8);
    assert_eq!(align_of::<Plain>(), 4);
    assert_eq!(size_of::<Plain>(), 4);

    assert_eq!(Aligned::Zero as u8, 0);
    assert_eq!(X, 1);
    assert_eq!(toU8(Aligned::Zero), 0);
    assert_eq!(toU8(Aligned::One), 1);

    // The auto-numbered enum still counts from zero.
    assert_eq!(Plain::A as i32, 0);
    assert_eq!(Plain::B as i32, 1);
}
