//@ run-pass
// schemars' derive writes `<MaybeJsonSchemaWrapper<T>>::maybe_schema_id()` for
// each type parameter: the inherent `maybe_schema_id` exists for
// `T: JsonSchema`, and a blanket `NoJsonSchema` trait in scope supplies one
// for every other `T`. rustc's probe relates the inherent impl's header and
// then registers its where-clauses; one that cannot hold makes the candidate
// `NoMatch` (`consider_probe`), and the trait candidate is taken. The impl
// was taken on its header alone, and `T: JsonSchema` failed afterwards.
use std::borrow::Cow;

pub trait JsonSchema {
    fn schema_id() -> Cow<'static, str>;
}

pub struct MaybeJsonSchemaWrapper<T: ?Sized>(core::marker::PhantomData<T>);

pub trait NoJsonSchema {
    fn maybe_schema_id() -> Cow<'static, str> {
        Cow::Borrowed("none")
    }
}

impl<T: ?Sized> NoJsonSchema for T {}

impl<T: JsonSchema + ?Sized> MaybeJsonSchemaWrapper<T> {
    pub fn maybe_schema_id() -> Cow<'static, str> {
        T::schema_id()
    }
}

impl JsonSchema for u32 {
    fn schema_id() -> Cow<'static, str> {
        Cow::Borrowed("u32")
    }
}

fn id_of<T>() -> Cow<'static, str> {
    #[allow(unused_imports)]
    use NoJsonSchema as _;
    <MaybeJsonSchemaWrapper<T>>::maybe_schema_id()
}

fn id_of_bounded<T: JsonSchema>() -> Cow<'static, str> {
    #[allow(unused_imports)]
    use NoJsonSchema as _;
    <MaybeJsonSchemaWrapper<T>>::maybe_schema_id()
}

fn main() {
    #[allow(unused_imports)]
    use NoJsonSchema as _;
    assert_eq!(id_of::<u32>(), "none");
    assert_eq!(id_of_bounded::<u32>(), "u32");
    assert_eq!(<MaybeJsonSchemaWrapper<u32>>::maybe_schema_id(), "u32");
    assert_eq!(<MaybeJsonSchemaWrapper<String>>::maybe_schema_id(), "none");
}
