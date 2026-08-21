#![feature(type_alias_impl_trait)]

type Alias<T: Clone> = impl Clone;

#[define_opaque(Alias)]
fn pair<T: Clone>(value: T) -> (Alias<T>, Alias<T>) {
    (value.clone(), value)
}

fn main() {
    let value = pair(7u8).1;
    let _ = <Alias<_> as Clone>::clone(&value);
}
