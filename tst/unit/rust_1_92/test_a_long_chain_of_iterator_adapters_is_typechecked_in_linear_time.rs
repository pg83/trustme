// Forty `.chain(..)` calls on `empty()`, each argument an opaque
// `impl Iterator`, so the receiver of the last call is forty `Chain`s deep.
// rustc's `fudge_inference_if_ok` asks once per probe whether a type
// mentions a variable created inside the snapshot, in one fold over the type.
// Our check walked a path's parameters twice per level, once through the
// type visitor and once by hand, which is 2^depth: exr's
// `Header::all_named_attributes`, under image, never finished typeck.
#[derive(Clone, Debug)]
enum Value {
    I32(i32),
    F32(f32),
}

fn optional<'t, T: Clone>(
    name: &'t str,
    to_value: impl Fn(T) -> Value,
    value: &'t Option<T>,
) -> impl Iterator<Item = (&'t str, Value)> {
    value.as_ref().map(move |value| (name, to_value(value.clone()))).into_iter()
}

const fn expect_is_iter<'s, T: Iterator<Item = (&'s str, Value)>>(val: T) -> T {
    val
}

struct Header {
    a: Option<i32>,
    b: Option<f32>,
}

fn all(h: &Header) -> impl '_ + Iterator<Item = (&str, Value)> {
    use std::iter::empty;
    use Value::*;
    expect_is_iter(empty()
        .chain(optional("a0", I32, &h.a))
        .chain(optional("b1", F32, &h.b))
        .chain(optional("a2", I32, &h.a))
        .chain(optional("b3", F32, &h.b))
        .chain(optional("a4", I32, &h.a))
        .chain(optional("b5", F32, &h.b))
        .chain(optional("a6", I32, &h.a))
        .chain(optional("b7", F32, &h.b))
        .chain(optional("a8", I32, &h.a))
        .chain(optional("b9", F32, &h.b))
        .chain(optional("a10", I32, &h.a))
        .chain(optional("b11", F32, &h.b))
        .chain(optional("a12", I32, &h.a))
        .chain(optional("b13", F32, &h.b))
        .chain(optional("a14", I32, &h.a))
        .chain(optional("b15", F32, &h.b))
        .chain(optional("a16", I32, &h.a))
        .chain(optional("b17", F32, &h.b))
        .chain(optional("a18", I32, &h.a))
        .chain(optional("b19", F32, &h.b))
        .chain(optional("a20", I32, &h.a))
        .chain(optional("b21", F32, &h.b))
        .chain(optional("a22", I32, &h.a))
        .chain(optional("b23", F32, &h.b))
        .chain(optional("a24", I32, &h.a))
        .chain(optional("b25", F32, &h.b))
        .chain(optional("a26", I32, &h.a))
        .chain(optional("b27", F32, &h.b))
        .chain(optional("a28", I32, &h.a))
        .chain(optional("b29", F32, &h.b))
        .chain(optional("a30", I32, &h.a))
        .chain(optional("b31", F32, &h.b))
        .chain(optional("a32", I32, &h.a))
        .chain(optional("b33", F32, &h.b))
        .chain(optional("a34", I32, &h.a))
        .chain(optional("b35", F32, &h.b))
        .chain(optional("a36", I32, &h.a))
        .chain(optional("b37", F32, &h.b))
        .chain(optional("a38", I32, &h.a))
        .chain(optional("b39", F32, &h.b))
    )
}

fn main() {
    let h = Header { a: Some(1), b: None };
    assert_eq!(all(&h).count(), 20);
}
