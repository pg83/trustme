//@ aux-build: cov99_w593_inferred_rpit_origin.rs

fn main() {
    let mut values = cov99_w593_inferred_rpit_origin::relay::<u8, 3>(47);
    assert_eq!(values.next(), Some(47));
    assert_eq!(values.next(), None);
}
