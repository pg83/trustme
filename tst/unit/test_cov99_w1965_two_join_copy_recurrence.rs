//@ edition: 2021
//@ compile-flags: -O

#[inline(never)]
fn bump(value: i32) -> i32 {
    value * 3 + 1
}

#[inline(never)]
fn recurrence(mut value: i32, mut remaining: u8, reverse: bool) -> i32 {
    loop {
        let joined = if remaining & 1 == 0 {
            bump(value)
        } else {
            value + 2
        };
        value = joined;
        let carried = if reverse {
            value - 1
        } else {
            bump(value)
        };
        value = carried;
        remaining -= 1;
        if remaining == 0 {
            break value;
        }
    }
}

fn main() {
    assert_eq!(recurrence(2, 3, false), 370);
    assert_eq!(recurrence(2, 3, true), 10);
    assert_eq!(recurrence(-3, 4, false), -1655);
    assert_eq!(recurrence(-3, 4, true), -23);
}
