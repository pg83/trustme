#![feature(type_alias_impl_trait)]

type AsyncCallable = impl AsyncFn();

#[define_opaque(AsyncCallable)]
fn make() -> AsyncCallable {
    || async {}
}

fn main() {}
