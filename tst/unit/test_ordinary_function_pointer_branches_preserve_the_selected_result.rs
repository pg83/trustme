//@ compile-variant: -O

#[inline(never)]
fn left(value: u16) -> u16 { value * 3 }
#[inline(never)]
fn right(value: u16) -> u16 { value * 5 }
#[inline(never)]
fn select(first: bool, a: fn(u16) -> u16, b: fn(u16) -> u16, value: u16) -> u16 {
    let result = if first { a(value) } else { b(value) };
    result + 7
}

fn main() {
    assert_eq!(select(true, left, right, 11), 40);
    assert_eq!(select(false, left, right, 11), 62);
    assert_eq!(select(true, right, left, 3), 22);
    assert_eq!(select(false, right, left, 3), 16);
}
