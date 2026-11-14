//@ compile-flags: -Copt-level=2 -Cpanic=abort -Zdump-mir

enum Choice {
    First([u64; 4]),
    Second([u64; 4]),
    Third([u64; 4]),
    Last([u64; 4]),
}

fn choose(value: Choice, left: bool, right: bool) -> u8 {
    match value {
        Choice::First(_) | Choice::Second(_) | Choice::Third(_) => {
            if left { 11 } else { 13 }
        }
        Choice::Last(_) => {
            if right { 17 } else { 19 }
        }
    }
}

const FIRST: Choice = Choice::First([7; 4]);
const LAST: Choice = Choice::Last([9; 4]);

fn main() {
    assert_eq!(choose(FIRST, true, false), 11);
    assert_eq!(choose(LAST, false, false), 19);
}
