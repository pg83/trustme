//@ compile-fail: Attempting to move out of invalid slot
fn main() {
    let later;
    let invoke = || later();
    let payload = Box::new(17u8);
    later = move || drop(payload);
    invoke();
}
