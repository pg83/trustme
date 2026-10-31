include!("aux/drop_state_token.rs");
fn exercise(mode: u8, owner: bool, log: &Cell<u64>) {
    let value = (token(log, 0), token(log, 1));
    match mode {
        0 => drop(value),
        1 => {}
        2 => drop(value.0),
        _ => if owner { drop(value); } else { drop(value.1); },
    }
    let expected = match mode { 0 => 3, 1 => 0, 2 => 1, _ => if owner { 3 } else { 2 } };
    assert_eq!(log.get(), expected);
}
fn main() {
    for mode in 0..4 {
        for owner in [false, true] {
            let log = Cell::new(0);
            exercise(std::hint::black_box(mode), std::hint::black_box(owner), &log);
            assert_eq!(log.get(), 3);
        }
    }
}
