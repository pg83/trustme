#![expect(incomplete_features)]
#![feature(explicit_tail_calls)]

fn count_down(n: u32) -> u32 {
    if n == 0 {
        0
    } else {
        become count_down(n - 1)
    }
}

fn main() {
    assert_eq!(count_down(100_000), 0);
}
