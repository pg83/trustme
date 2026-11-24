//@ edition: 2021
#![feature(custom_inner_attributes)]
#![rustfmt::skip]
#![crate_name = "cov99_w1930_deferred_crate"]

fn main() {
    assert_eq!(module_path!(), "cov99_w1930_deferred_crate");
}
