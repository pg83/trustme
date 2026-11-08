//@ compile-flags: -O

#[inline(never)]
fn advance(seed: u64) -> u64 {
    let mut state = seed;
    state = state;
    for _ in 0..3 {
        state = state.rotate_left(7);
        state = state;
    }
    state
}

fn main() {
    assert_eq!(advance(5), 5 << 21);
    assert_eq!(advance(1 << 63), 1 << 20);
}
