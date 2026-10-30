//@ aux-build: serialized_tail_call.rs
fn main() {
    assert_eq!(serialized_tail_call::relay(17u16), 17);
    assert_eq!(serialized_tail_call::relay(37u64), 37);
    assert!(serialized_tail_call::relay(true));
}
