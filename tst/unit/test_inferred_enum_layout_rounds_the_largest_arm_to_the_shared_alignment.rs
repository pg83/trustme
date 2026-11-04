//@ compile-variant: -O
#[repr(C, align(16))]
struct Aligned(u8);
#[repr(u8)]
enum Choice { Small(Aligned), Large([u8; 36]) }

#[inline(never)]
fn read(value: Choice) -> u16 {
    match value {
        Choice::Small(value) => value.0 as u16,
        Choice::Large(values) => values[0] as u16 + values[35] as u16,
    }
}

fn main() {
    assert_eq!(std::mem::size_of::<Choice>(), 48);
    assert_eq!(std::mem::align_of::<Choice>(), 16);
    assert_eq!(read(std::hint::black_box(Choice::Small(Aligned(11)))), 11);
    assert_eq!(read(std::hint::black_box(Choice::Large([7; 36]))), 14);
}
