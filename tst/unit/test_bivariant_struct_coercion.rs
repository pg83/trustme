// A generic parameter absent from every field is bivariant.  A coercion may
// therefore change it while the represented field type is coerced normally.
#![allow(coherence_leak_check)]

trait Trait {
    type Assoc;
}

struct Wrapper<T, U>(T)
where
    T: Trait<Assoc = U>;

impl Trait for for<'a> fn(&'a ()) {
    type Assoc = u32;
}

impl Trait for fn(&'static ()) {
    type Assoc = String;
}

fn coerce(
    value: Wrapper<for<'a> fn(&'a ()), u32>,
) -> Wrapper<fn(&'static ()), String> {
    value
}

fn main() {}
