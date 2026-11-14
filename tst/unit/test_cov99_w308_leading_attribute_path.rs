//@ proc-macro-aux-build: cov99_w308_leading_attribute_observer.rs

#[cov99_w308_leading_attribute_observer::inspect]
#[::w308_unused::marker]
struct Discarded;

fn main() {
    assert_eq!(leading_attribute_seen(), 31);
}
