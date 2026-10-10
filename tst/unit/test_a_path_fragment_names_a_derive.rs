// alloy-primitives' `wrap_fixed_bytes!` takes `extra_derives: [$($extra_derives:path),*]`
// and writes `#[derive(.., $($extra_derives,)*)]`, with
// `$crate::private::derive_more::Display` among them. Upstream parses each item
// of a derive list as a path, and a `path` fragment is one. We took a type or
// a meta fragment there but stopped at a path fragment ("expected TOK_IDENT").
macro_rules! with_derives {
    ($($d:path),* $(,)?) => {
        #[derive($($d,)*)]
        struct Wrapped(u8);
    };
}

with_derives!(::core::fmt::Debug, core::clone::Clone, PartialEq);

fn main() {
    let w = Wrapped(1);
    assert!(w.clone() == Wrapped(1));
    assert_eq!(format!("{:?}", w), "Wrapped(1)");
}
