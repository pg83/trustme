//@ compile-variant: -O

static INPUT: u8 = 31;
fn universal(value: &u8) -> u8 { *value + 19 }

#[inline(never)]
fn choose(first: bool) -> fn(&'static u8) -> u8 {
    let callback = universal;
    if first {
        callback as for<'a> fn(&'a u8) -> u8
    } else {
        callback as fn(&'static u8) -> u8
    }
}

fn main() {
    assert_eq!(choose(true)(&INPUT), 50);
    assert_eq!(choose(false)(&INPUT), 50);
}
