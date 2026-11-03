fn take_or<T>(value: Option<T>, fallback: fn() -> !) -> T {
    match value { Some(value) => value, None => fallback() }
}
fn unused_fallback() -> ! { panic!("the present value must be selected") }
fn twice<F: FnMut() -> T, T>(mut build: F) -> (T, T) { (build(), build()) }
fn main() {
    assert_eq!(take_or(Some((311u32, ())), unused_fallback), (311, ()));
    let mut seed = 317u32;
    let parts = twice(|| { seed += 1; format!("seed{seed}") });
    assert_eq!(parts, (String::from("seed318"), String::from("seed319")));
}
