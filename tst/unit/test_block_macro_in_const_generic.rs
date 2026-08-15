//@ edition: 2015
#![allow(non_local_definitions)]

struct Example<const N: usize>;

trait Marker<const N: usize> {}
impl<const N: usize> Marker<N> for Example<N> {}

fn make_marker() -> impl Marker<{
    #[macro_export]
    macro_rules! const_macro { () => {{ 3 }} }
    inline!()
}> {
    Example::<{ const_macro!() }>
}

fn from_marker(_: impl Marker<{
    #[macro_export]
    macro_rules! inline { () => {{ 3 }} }
    inline!()
}>) {}

fn main() {
    let value = Example::<{
        #[macro_export]
        macro_rules! local_const {
            ($name:ident) => {{ let $name = 3; $name }};
        }
        local_const!(inside_const)
    }>;
    let _ = value;
    from_marker(make_marker());
}
