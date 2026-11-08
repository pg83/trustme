//@ compile-flags: -O

#[inline(never)]
fn pack(seed: u64) -> ((u64, u64), (u64, u64)) {
    ((seed + 1, seed + 2), (seed + 5, seed + 9))
}

#[inline(never)]
fn weighted(seed: u64) -> u64 {
    let source = pack(seed);
    let first = source.0;
    let second = source.1;
    let rebuilt = (first.1, second.0, first.0, second.1);
    rebuilt.2 * 2 + rebuilt.0 * 3 + rebuilt.1 * 5 + rebuilt.3 * 7
}

fn main() {
    assert_eq!(weighted(0), 96);
    assert_eq!(weighted(2), 130);
    assert_eq!(weighted(10), 266);
}
