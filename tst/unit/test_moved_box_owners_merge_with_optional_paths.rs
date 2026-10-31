include!("aux/drop_state_token.rs");
fn drop_first(mode: u8, branch: bool, log: &Cell<u64>) {
    let value = Box::new(token(log, 0));
    match mode {
        0 => drop(*value),
        _ => if branch { drop(value); },
    }
    assert_eq!(log.get(), if mode == 0 || branch { 1 } else { 0 });
}
fn keep_first(mode: u8, branch: bool, log: &Cell<u64>) {
    let value = Box::new(token(log, 0));
    match mode {
        0 => drop(*value),
        _ => if branch {} else { drop(value); },
    }
    assert_eq!(log.get(), if mode == 0 || !branch { 1 } else { 0 });
}
fn main() {
    for mode in 0..2 {
        for branch in [false, true] {
            let log = Cell::new(0);
            drop_first(std::hint::black_box(mode), std::hint::black_box(branch), &log);
            assert_eq!(log.get(), 1);
            log.set(0);
            keep_first(std::hint::black_box(mode), std::hint::black_box(branch), &log);
            assert_eq!(log.get(), 1);
        }
    }
}
