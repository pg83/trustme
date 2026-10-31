include!("aux/drop_state_token.rs");
fn exercise(mode: u8, branch: bool, log: &Cell<u64>) {
    let value;
    match mode {
        0 => {}
        1 => {
            value = Box::new(token(log, 0));
            if branch { drop(value); } else { drop(*value); }
        }
        _ => {
            value = Box::new(token(log, 0));
            if branch { drop(*value); } else { drop(value); }
        }
    }
    assert_eq!(log.get(), if mode == 0 { 0 } else { 1 });
}
fn main() {
    for mode in 0..3 {
        for branch in [false, true] {
            let log = Cell::new(0);
            exercise(std::hint::black_box(mode), std::hint::black_box(branch), &log);
            assert_eq!(log.get(), if mode == 0 { 0 } else { 1 });
        }
    }
}
