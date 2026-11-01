//@ compile-variant: -O
#[derive(Clone, Copy)]
enum Choice { First(()), Second(()), Third(()) }
const SECOND: Choice = Choice::Second(());
#[inline(always)]
fn from_enum(value: Choice) -> u16 {
    match value { Choice::First(()) => 13, Choice::Second(()) => 17, Choice::Third(()) => 19 }
}
#[inline(always)]
fn from_integer(value: u16) -> u16 {
    match value { 3 => 13, 7 => 17, 11 => 19, _ => 23 }
}
fn main() {
    assert_eq!(from_enum(Choice::First(())), 13);
    assert_eq!(from_enum(SECOND), 17);
    assert_eq!(from_enum(std::hint::black_box(Choice::Third(()))), 19);
    assert_eq!(from_integer(3), 13);
    assert_eq!(from_integer(7), 17);
    assert_eq!(from_integer(11), 19);
    assert_eq!(from_integer(0), 23);
    assert_eq!(from_integer(std::hint::black_box(7)), 17);
}
