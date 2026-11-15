#[repr(align(32))]
struct Aligned(u8);

enum Choice {
    Left(Aligned),
    Right(Aligned),
}

#[inline(never)]
fn payload(choice: Choice) -> u8 {
    match choice {
        Choice::Left(value) => value.0,
        Choice::Right(value) => value.0 + 1,
    }
}

fn main() {
    assert_eq!(std::mem::align_of::<Aligned>(), 32);
    assert_eq!(std::mem::align_of::<Choice>(), 32);
    assert_eq!(payload(Choice::Left(Aligned(19))), 19);
    assert_eq!(payload(Choice::Right(Aligned(37))), 38);
}
