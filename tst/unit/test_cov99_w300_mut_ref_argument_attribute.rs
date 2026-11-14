//@ proc-macro-aux-build: cov99_w300_identity_attribute.rs

#[cov99_w300_identity_attribute::identity]
fn copied(&mut value: &mut u8) -> u8 {
    value
}

fn main() {
    let mut value = 37;
    assert_eq!(copied(&mut value), 37);
    assert_eq!(value, 37);
}
