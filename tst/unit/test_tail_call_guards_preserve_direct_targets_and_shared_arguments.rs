//@ compile-variant: -O
#![feature(explicit_tail_calls)]
fn left(mode: u8, value: u32) -> u32 { value.wrapping_mul(3).wrapping_add(mode as u32) }
fn right(mode: u8, value: u32) -> u32 { value.wrapping_mul(5).wrapping_sub(mode as u32) }
#[inline(never)]
fn route(mode: u8, value: u32) -> u32 {
    match mode {
        0 => become left(mode, value),
        1 => become right(mode, value),
        _ => become left(mode, value),
    }
}
fn finish(pair: (u8, u8), gate: bool, value: u32) -> u32 {
    value + pair.0 as u32 + pair.1 as u32 + gate as u32
}
#[inline(never)]
fn guarded(pair: (u8, u8), gate: bool, value: u32) -> u32 {
    match pair {
        (0, _) | (_, 0) if {
            if gate { become finish(pair, gate, value); }
            true
        } => value + 10,
        _ => value + 20,
    }
}
fn main() {
    for mode in 0..4 {
        assert_eq!(route(mode, 13), if mode == 1 { 65 - mode as u32 } else { 39 + mode as u32 });
    }
    assert_eq!(guarded((0, 7), true, 13), 21);
    assert_eq!(guarded((7, 0), true, 13), 21);
    assert_eq!(guarded((0, 7), false, 13), 23);
    assert_eq!(guarded((7, 0), false, 13), 23);
    assert_eq!(guarded((4, 7), true, 13), 33);
}
