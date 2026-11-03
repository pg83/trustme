macro_rules! call {
    ($callable:expr) => { assert!($callable()); };
}
fn main() {
    let mut calls = 0u16;
    call!(|| { calls += 1; calls == 1 });
    call!(|| -> bool { calls += 1; calls == 2 });
    let captured = 337u16;
    call!(move || captured == 337);
    assert_eq!(calls, 2);
}
