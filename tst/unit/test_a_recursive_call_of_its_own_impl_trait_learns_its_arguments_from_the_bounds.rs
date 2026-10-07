// der-parser 10.0.0's ber_encode_object_content returns `impl SerializeFn<W>` and
// calls itself, `ber_encode_object_content(&obj.content)(out)`. The recursive
// call names the opaque type with a fresh `?W`; rustc learns `?W = W` from
// the opaque's bound `SerializeFn<?W>: Fn(Ctx<?W>)` against `out`. It is no use
// of the hidden type with the function's own arguments until `?W` is known.
pub struct Ctx<W> {
    pub w: W,
    pub n: usize,
}

pub trait SerializeFn<W>: Fn(Ctx<W>) -> Result<Ctx<W>, ()> {}

impl<W, F: Fn(Ctx<W>) -> Result<Ctx<W>, ()>> SerializeFn<W> for F {}

fn add<W>(k: usize) -> impl SerializeFn<W> {
    move |mut out| {
        out.n += k;
        Ok(out)
    }
}

pub enum Content {
    Leaf(usize),
    Optional(Option<Box<Content>>),
}

fn encode<'a, W: Default + 'a>(c: &'a Content) -> impl SerializeFn<W> + 'a {
    move |out| match c {
        Content::Leaf(n) => add(*n)(out),
        Content::Optional(inner) => match inner {
            Some(obj) => encode(obj)(out),
            None => add(0)(out),
        },
    }
}

fn main() {
    let tree = Content::Optional(Some(Box::new(Content::Leaf(5))));
    let out = encode::<u8>(&tree)(Ctx { w: 0u8, n: 1 }).unwrap();
    assert_eq!((out.w, out.n), (0, 6));
}
