// derive_more's `let attrs: Vec<_> = if .. { a.iter().map(..).collect() }
// else { b.iter().map(..).collect() };` and then `attrs.iter().map(|attrs|
// get_meta_info(.., attrs, ..))` with a `&[Attribute]` parameter. Upstream's
// `if`/`match` coerces its arms into `coercion_target_type` - the expected
// type itself when that is not a bare variable - so each `collect()` is
// checked against `Vec<_>`, its `FromIterator` obligation fixes the element,
// and `attrs` is `&&Vec<Attribute>` by the time it is coerced into
// `&[Attribute]`. We coerced the arms into a fresh variable and the argument
// read `[Attribute]` into the element instead.
struct Attr(u8);

struct Field {
    attrs: Vec<Attr>,
}

fn info(attrs: &[Attr]) -> usize {
    attrs.len()
}

fn run(c: bool, fields: Vec<&Field>) -> usize {
    let attrs: Vec<_> = if c {
        fields.iter().map(|f| &f.attrs).collect()
    } else {
        fields.iter().rev().map(|f| &f.attrs).collect()
    };
    attrs.iter().map(|a| info(a)).sum()
}

fn main() {
    let f = Field { attrs: vec![Attr(1), Attr(2)] };
    let g = Field { attrs: vec![Attr(3)] };
    assert_eq!(run(true, vec![&f, &g]), 3);
    assert_eq!(run(false, vec![&f]), 2);
    let _ = Attr(0).0;
}
