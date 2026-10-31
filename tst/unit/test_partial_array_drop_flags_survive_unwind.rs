include!("aux/drop_state_token.rs");
fn exercise(mode: u8, log: &Cell<u64>) {
    let values: [_; 32] = std::array::from_fn(|i| token(log, i as u32));
    match mode {
        0 => {}
        1 => { let [first, ..] = values; drop(first); }
        2 => { let [.., last] = values; drop(last); }
        _ => drop(values),
    }
    std::panic::panic_any("after moves");
}
fn main() {
    std::panic::set_hook(Box::new(|_| {}));
    for mode in 0..4 {
        let log = Cell::new(0);
        let result = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
            exercise(std::hint::black_box(mode), &log);
        }));
        let payload = result.unwrap_err();
        assert_eq!(payload.downcast_ref::<&str>(), Some(&"after moves"));
        assert_eq!(log.get(), u32::MAX as u64);
    }
}
