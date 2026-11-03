//@ compile-fail: Unable to find
fn main() {
    let local_reading = 673u16;
    use local_reading as SAVED;
    assert_eq!(SAVED, local_reading);
}
