//@ compile-flags: -Zmir-opt-level=2

#[inline(never)]
fn select(left: (u32, u32), right: (u32, u32), condition: bool) -> u64 {
    let pair = (left, right);
    let answer = if condition { pair.0.1 as u64 } else { pair.1.0 as u64 };
    std::hint::black_box(&pair);
    answer
}

#[inline(never)]
fn changed(mut left: (u32, u32), right: (u32, u32)) -> u64 {
    let pair = (left, right);
    left.1 = 89;
    let answer = pair.0.1 as u64 + pair.1.0 as u64;
    std::hint::black_box((&pair, left));
    answer
}

fn main() {
    let left = std::hint::black_box((3, u32::MAX));
    let right = std::hint::black_box((7, 11));
    assert_eq!(select(left, right, true), u32::MAX as u64);
    assert_eq!(select(left, right, false), 7);
    assert_eq!(changed(left, right), u32::MAX as u64 + 7);
}
