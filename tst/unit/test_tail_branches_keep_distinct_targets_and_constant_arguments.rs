//@ compile-variant: -O
#![feature(explicit_tail_calls)]

#[inline(never)]
fn left(_: bool, value: u16) -> u16 { value + 3 }
#[inline(never)]
fn right(_: bool, value: u16) -> u16 { value + 5 }
#[inline(never)]
fn distinct_target(first: bool, value: u16) -> u16 {
    if first { become left(first, value) } else { become right(first, value) }
}
#[inline(never)]
fn distinct_argument(first: bool, _: u16) -> u16 {
    if first { become left(first, 11) } else { become left(first, 17) }
}

fn main() {
    assert_eq!(distinct_target(true, 7), 10);
    assert_eq!(distinct_target(false, 7), 12);
    assert_eq!(distinct_argument(true, 0), 14);
    assert_eq!(distinct_argument(false, 0), 20);
}
