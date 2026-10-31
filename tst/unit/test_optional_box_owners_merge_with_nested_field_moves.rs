include!("aux/drop_state_token.rs");
fn exercise(mode: u8, branch: bool, log: &Cell<u64>) {
    let value = Box::new((token(log, 0), token(log, 1)));
    match mode {
        0 => drop(value),
        1 => {}
        _ => if branch { drop(value.0); } else { drop(value); },
    }
    let expected = match mode { 0 => 3, 1 => 0, _ => if branch { 1 } else { 3 } };
    assert_eq!(log.get(), expected);
}
fn main() {
    for mode in 0..3 {
        for branch in [false, true] {
            let log = Cell::new(0);
            exercise(std::hint::black_box(mode), std::hint::black_box(branch), &log);
            assert_eq!(log.get(), 3);
        }
    }
}
