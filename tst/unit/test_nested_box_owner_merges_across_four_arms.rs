include!("aux/drop_state_token.rs");
fn exercise(mode: u8, branch: bool, log: &Cell<u64>) {
    let value = Box::new(token(log, 0));
    match mode {
        0 => if branch { drop(value); } else { drop(*value); },
        1 => if branch { drop(*value); } else { drop(value); },
        2 => {}
        _ => drop(*value),
    }
    assert_eq!(log.get(), if mode == 2 { 0 } else { 1 });
}
fn main() {
    for mode in 0..4 {
        for branch in [false, true] {
            let log = Cell::new(0);
            exercise(std::hint::black_box(mode), std::hint::black_box(branch), &log);
            assert_eq!(log.get(), 1);
        }
    }
}
