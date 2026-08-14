#![expect(incomplete_features)]
#![feature(explicit_tail_calls)]

const fn count_down(n: u32) -> u32 {
    if n == 0 {
        0
    } else {
        become count_down(n - 1)
    }
}

const RESULT: u32 = count_down(128);

fn main() {
    assert_eq!(RESULT, 0);
}
