use std::marker::PhantomData;

struct Struct<I, T>(PhantomData<fn() -> <Self as It>::Item>)
where
    Self: It;

impl<I: It> It for Struct<I, I::Item> {
    type Item = ();
}

trait It {
    type Item;
}

struct Empty<T>(PhantomData<fn() -> T>);

impl<T> It for Empty<T> {
    type Item = T;
}

fn main() {
    let _x = Struct::<Empty<&'static ()>, _>(PhantomData);
}
