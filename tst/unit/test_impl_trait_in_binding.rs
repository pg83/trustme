#![feature(impl_trait_in_bindings)]

trait Value {}

impl Value for u32 {}

fn main() {
    let _: impl Value = 0u32;
}
