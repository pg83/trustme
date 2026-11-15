//@ aux-build: cov99_w1165_named_variant.rs

fn main() {
    let item = cov99_w1165_named_variant::Payload { count: 37 };
    let cov99_w1165_named_variant::Payload { count } = item;
    assert_eq!(count, 37);
}
