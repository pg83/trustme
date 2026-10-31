include!("aux/drop_state_token.rs");
fn exercise(mode: u8, owner: bool, log: &Cell<u64>) {
    let values: [_; 32] = std::array::from_fn(|i| token(log, i as u32));
    match mode {
        0 => {}
        1 => drop(values),
        2 => { let [first, ..] = values; drop(first); }
        _ => if owner { drop(values); } else { let [.., last] = values; drop(last); },
    }
    let expected = match mode { 0 => 0, 1 => u32::MAX as u64, 2 => 1, _ => if owner { u32::MAX as u64 } else { 1 << 31 } };
    assert_eq!(log.get(), expected);
}
fn main() {
    for mode in 0..4 {
        for owner in [false, true] {
            let log = Cell::new(0);
            exercise(std::hint::black_box(mode), std::hint::black_box(owner), &log);
            assert_eq!(log.get(), u32::MAX as u64);
        }
    }
}
