// serde_with_macros writes `let DeriveInput { attrs, .. } = syn::parse(input)?;` and
// then `attrs.iter().map(|Attribute { meta, .. }| meta)`. rustc checks the `let`
// pattern as it is met: the struct pattern names the scrutinee's type, so
// `attrs` is a `Vec<Attribute>` before `.iter()` and the closure's parameter is an
// `&Attribute` its pattern matches through, binding `meta: &Meta`. Deciding the
// closure's pattern in the same fallback round as the `let`'s, before the method
// calls between them had resolved, made the parameter an `Attribute`.
pub struct Meta(u8);
pub struct Attribute {
    pub style: u8,
    pub meta: Meta,
}
pub struct DeriveInput {
    pub attrs: Vec<Attribute>,
    pub ident: u8,
}

pub trait Parse: Sized {
    fn parse(input: &[u8]) -> Result<Self, ()>;
}
impl Parse for DeriveInput {
    fn parse(input: &[u8]) -> Result<Self, ()> {
        Ok(DeriveInput { attrs: input.iter().map(|b| Attribute { style: 0, meta: Meta(*b) }).collect(), ident: 0 })
    }
}
pub fn parse<T: Parse>(input: &[u8]) -> Result<T, ()> {
    T::parse(input)
}

pub(crate) fn schemars_with_attr_if(input: &[u8]) -> Result<u32, ()> {
    fn eval_metas<'a>(metas: impl IntoIterator<Item = &'a Meta>) -> Result<u32, ()> {
        metas.into_iter().map(eval_meta).try_fold(0, |state, result| Ok(state + result?))
    }

    fn eval_meta(meta: &Meta) -> Result<u32, ()> {
        Ok(meta.0 as u32)
    }

    let DeriveInput { attrs, .. } = parse(input)?;
    let metas = attrs.iter().map(|Attribute { meta, .. }| meta);
    eval_metas(metas)
}

fn main() {
    assert_eq!(schemars_with_attr_if(&[2, 3]), Ok(5));
}
