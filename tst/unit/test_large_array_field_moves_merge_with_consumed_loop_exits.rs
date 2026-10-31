include!("aux/drop_state_token.rs");
fn exercise(branch: bool, log: &Cell<u64>) {
    let values: [_; 32] = std::array::from_fn(|i| token(log, i as u32));
    loop {
        if branch { let [first, ..] = values; drop(first); break; }
        drop(values);
        break;
    }
    assert_eq!(log.get(), if branch { 1 } else { u32::MAX as u64 });
}
fn main() {
    for branch in [false, true] {
        let log = Cell::new(0);
        exercise(std::hint::black_box(branch), &log);
        assert_eq!(log.get(), u32::MAX as u64);
    }
}
