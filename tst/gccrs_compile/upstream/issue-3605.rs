
enum Foo<'a> {
    Marker(std::marker::PhantomData<&'a ()>),
}

enum Bar<'a> {
    in_band_def_explicit_impl(Foo<'a>),
}
