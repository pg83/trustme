//@ compile-flags: -O -Z dump-mir

const EMPTY: &mut [u16; 0] = {
    let value = &mut [];
    value
};

#[inline(never)]
fn choose(flag: bool, local: &mut [u16; 0]) -> &mut [u16; 0] {
    if flag { EMPTY } else { &mut *local }
}

fn main() {
    let mut local = [];
    assert_eq!(choose(std::hint::black_box(true), &mut local).len(), 0);
    assert_eq!(choose(std::hint::black_box(false), &mut local).len(), 0);
}
