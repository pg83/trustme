//@ compile-flags: -Z mir-opt-level=0 -Z dump-mir

fn same(flag: bool) -> u32 {
    if flag {
        return 91;
    } else {
        return 91;
    }
}

fn main() {
    assert_eq!(same(true), 91);
    assert_eq!(same(false), 91);
}
