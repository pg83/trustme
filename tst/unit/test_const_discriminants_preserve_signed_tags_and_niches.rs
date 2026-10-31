#![feature(core_intrinsics, never_type)]
#[repr(i8)]
enum Signed { Low = -37, High = 23 }
enum Single { Impossible(!), Live(u16) }
enum Niche { First, Second, Payload(bool) }
const SIGNED: (i128, i128) = (
    std::intrinsics::discriminant_value(&Signed::Low) as i128,
    std::intrinsics::discriminant_value(&Signed::High) as i128,
);
const SINGLE: isize = std::intrinsics::discriminant_value(&Single::Live(17));
const NICHES: (isize, isize, isize) = (
    std::intrinsics::discriminant_value(&Niche::First),
    std::intrinsics::discriminant_value(&Niche::Second),
    std::intrinsics::discriminant_value(&Niche::Payload(true)),
);
fn main() {
    assert_eq!(SIGNED, (-37, 23));
    assert_eq!(SINGLE, 1);
    assert_eq!(NICHES, (0, 1, 2));
}
