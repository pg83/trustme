//@ compile-variant: -O

#[derive(Clone, Copy)]
enum Choice { Left, Middle, Right }
const fn enum_code(value: Choice) -> u16 {
    match value { Choice::Left => 3, Choice::Middle => 5, Choice::Right => 7 }
}
const fn number_code(value: u8) -> u16 {
    match value { 11 => 13, 17 => 19, _ => 23 }
}
const fn combine(value: u8) -> u16 { number_code(value) + enum_code(Choice::Middle) }
const EXPECTED: [u16; 3] = [combine(11), combine(17), combine(0)];

#[inline(never)]
fn fixed_enum_calls() -> u16 { enum_code(Choice::Left) + enum_code(Choice::Right) }
#[inline(never)]
fn fixed_number_calls() -> u16 { number_code(11) + number_code(17) + number_code(0) }

fn main() {
    assert_eq!(EXPECTED, [18, 24, 28]);
    assert_eq!(fixed_enum_calls(), 10);
    assert_eq!(fixed_number_calls(), 55);
    for (value, expected) in [(11, 18), (17, 24), (0, 28)] {
        assert_eq!(combine(value), expected);
    }
}
