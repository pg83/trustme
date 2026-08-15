#![feature(type_alias_impl_trait)]

type OpaqueIterator = impl Iterator;

#[define_opaque(OpaqueIterator)]
fn opaque_iterator() -> OpaqueIterator {
    0..1
}

fn main() {
    let _ = opaque_iterator().map(|value| value);
}
