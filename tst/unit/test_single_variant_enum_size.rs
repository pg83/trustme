// One variant is not a choice, so nothing is stored to say which it is: such an
// enum is zero-sized and its discriminant is a constant.
use core::mem::size_of;

enum Unit {
    V,
}

#[derive(Copy, Clone)]
enum Big {
    Bar = 0xDEADBEE,
}

#[derive(Copy, Clone)]
enum Neg {
    NegOne = -1,
}

#[repr(u8)]
enum Tagged {
    V,
}

enum Two {
    A,
    B,
}

static X: Big = Big::Bar;
const NEG_I8: i8 = Neg::NegOne as i8;
const NEG_I128: i128 = Neg::NegOne as i128;

fn main() {
    assert_eq!(size_of::<Unit>(), 0);
    assert_eq!(size_of::<Big>(), 0);
    // An explicit representation says how wide the tag is, so it is stored.
    assert_eq!(size_of::<Tagged>(), 1);
    assert_eq!(size_of::<Two>(), 1);

    let u = Unit::V;
    match u {
        Unit::V => (),
    }

    assert_eq!(X as usize, 0xDEADBEE);
    assert_eq!(Big::Bar as usize, 0xDEADBEE);
    assert_eq!(NEG_I8, -1);
    assert_eq!(NEG_I128, -1);
    assert_eq!(Neg::NegOne as i32, -1);
    assert_eq!(Neg::NegOne as i128, -1);
    assert_eq!(Tagged::V as u8, 0);
}
