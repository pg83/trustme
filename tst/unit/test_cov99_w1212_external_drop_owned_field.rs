//@ aux-build: cov99_w1212_external_drop_owned_field.rs
//@ compile-fail: not constrained
//@ compile-flags: -Cpanic=abort

fn main() {
    let _owner = cov99_w1212_external_drop_owned_field::Owner(String::from("owned"));
}
