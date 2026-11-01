//@ compile-variant: -O
static VALUES: [u16; 3] = [7, 11, 13];
fn add(value: u16) -> u16 { value + 17 }
fn multiply(value: u16) -> u16 { value * 3 }
enum Choice { First, Second, Third }
#[inline(never)]
fn select(choice: Choice, alternate: bool) -> u16 {
    match choice {
        Choice::First => if alternate { add(VALUES[0]) } else { multiply(VALUES[0]) },
        Choice::Second => add(VALUES[1]),
        Choice::Third => multiply(VALUES[2]),
    }
}
fn main() {
    assert_eq!(select(Choice::First, true), 24);
    assert_eq!(select(Choice::First, false), 21);
    assert_eq!(select(Choice::Second, false), 28);
    assert_eq!(select(Choice::Third, false), 39);
}
