// autocfg probes `pub trait Probe: std::ops::Add + Sized {}`. `Add`'s
// parameter defaults to `Self`, so `<T as Probe>::Output` is
// `<T as Add<T>>::Output`: rustc elaborates the supertrait with the
// projection's own self type (`instantiate_supertrait`). The lookup of the
// declaring trait substituted the subtrait's parameters but left `Self` with
// nothing to stand for.
pub trait Probe: std::ops::Add + Sized {}

impl Probe for u32 {}

fn sum<T: Probe>(a: T, b: T) -> T::Output {
    a + b
}

fn main() {
    assert_eq!(sum(2u32, 3), 5);
}
