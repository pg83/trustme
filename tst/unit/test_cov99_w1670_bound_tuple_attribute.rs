//@ proc-macro-aux-build: cov99_w1670_binding_identity.rs

#[cov99_w1670_binding_identity::keep]
fn add_pair(pair @ (_, _): (u32, u32)) -> u32 {
    pair.0 + pair.1
}

fn main() {
    assert_eq!(add_pair((17, 25)), 42);
}
