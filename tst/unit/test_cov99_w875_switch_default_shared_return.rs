//@ compile-flags: -C opt-level=2 -Z dump-mir

fn choose(tag: u8, take: bool, callback: fn() -> u8) -> u8 {
    match tag {
        1 => 7,
        2 => if take { callback() } else { 7 },
        _ => 7,
    }
}

fn answer() -> u8 {
    19
}

fn main() {
    assert_eq!(choose(2, true, answer), 19);
    assert_eq!(choose(1, true, answer), 7);
    assert_eq!(choose(2, false, answer), 7);
    assert_eq!(choose(3, true, answer), 7);
}
