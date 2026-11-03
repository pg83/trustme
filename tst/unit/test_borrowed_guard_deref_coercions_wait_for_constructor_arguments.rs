use std::panic::AssertUnwindSafe;
fn add(value: &mut u16) { *value += 5; }
fn main() {
    let run = |mut value, operation: fn(&mut u16)| {
        let mut guard = AssertUnwindSafe(&mut value);
        operation(&mut guard);
        value
    };
    assert_eq!(run(17u16, add), 22);
    assert_eq!(run(29u16, add), 34);
}
