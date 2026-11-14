//@ compile-flags: -O -Zdump-mir -Cpanic=abort

fn dispatch_saved(slot: &mut u32, choose: bool) -> u32 {
    let saved = *slot;
    *slot = 7;
    match saved {
        0 => if choose { 11 } else { 12 },
        1 => 22,
        _ => 33,
    }
}

fn main() {
    let mut slot = 0;
    assert_eq!(dispatch_saved(&mut slot, false), 12);
    assert_eq!(slot, 7);
}
