//@ compile-variant: -O

struct Wrapped<T>(T);
enum Choice<T> { One(T) }
union Slot<T: Copy> { value: T }

static INPUT: u8 = 23;
fn universal(value: &u8) -> u8 { *value + 7 }
fn static_input(value: &'static u8) -> u8 { *value + 11 }

#[inline(never)]
fn wrapped(first: bool, left: for<'a> fn(&'a u8) -> u8,
           right: fn(&'static u8) -> u8) -> Wrapped<fn(&'static u8) -> u8> {
    if first {
        Wrapped::<for<'a> fn(&'a u8) -> u8>(left)
    } else {
        Wrapped::<fn(&'static u8) -> u8>(right)
    }
}

#[inline(never)]
fn choice(first: bool, left: for<'a> fn(&'a u8) -> u8,
          right: fn(&'static u8) -> u8) -> Choice<fn(&'static u8) -> u8> {
    if first {
        Choice::<for<'a> fn(&'a u8) -> u8>::One(left)
    } else {
        Choice::<fn(&'static u8) -> u8>::One(right)
    }
}

#[inline(never)]
fn slot(first: bool, left: for<'a> fn(&'a u8) -> u8,
        right: fn(&'static u8) -> u8) -> Slot<fn(&'static u8) -> u8> {
    if first {
        Slot::<for<'a> fn(&'a u8) -> u8> { value: left }
    } else {
        Slot::<fn(&'static u8) -> u8> { value: right }
    }
}

fn main() {
    for first in [true, false] {
        let expected = if first { 30 } else { 34 };
        assert_eq!(wrapped(first, universal, static_input).0(&INPUT), expected);
        let Choice::One(callback) = choice(first, universal, static_input);
        assert_eq!(callback(&INPUT), expected);
        assert_eq!(unsafe { (slot(first, universal, static_input).value)(&INPUT) }, expected);
    }
}
