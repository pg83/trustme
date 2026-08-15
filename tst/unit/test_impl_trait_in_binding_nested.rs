#![feature(impl_trait_in_bindings)]

fn main() {
    let _: impl IntoIterator<Item = impl Sized> = [1u32, 2];
}
