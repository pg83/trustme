#[inline(never)]
fn choose(flag: bool) -> u8 {
    if flag && { return 7; } {
        11
    } else {
        13
    }
}

fn main() {
    assert_eq!(choose(true), 7);
    assert_eq!(choose(false), 13);
}
