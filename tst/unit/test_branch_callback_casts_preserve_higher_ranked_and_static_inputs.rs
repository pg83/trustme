//@ compile-variant: -O

static INPUT: u8 = 17;

fn universal(value: &u8) -> u8 { *value + 3 }
fn static_input(value: &'static u8) -> u8 { *value + 5 }

#[inline(never)]
fn choose(first: bool) -> fn(&'static u8) -> u8 {
    if first {
        universal as for<'a> fn(&'a u8) -> u8
    } else {
        static_input as fn(&'static u8) -> u8
    }
}

fn main() {
    assert_eq!(choose(true)(&INPUT), 20);
    assert_eq!(choose(false)(&INPUT), 22);
}
