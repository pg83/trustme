#![feature(coerce_unsized)]

use std::ops::CoerceUnsized;

trait Mirror {
    type Assoc: ?Sized;
}

impl<T: ?Sized> Mirror for T {
    type Assoc = T;
}

trait Any {}

impl<T> Any for T {}

struct Wrapper<T: ?Sized + 'static>(<&'static T as Mirror>::Assoc);

impl CoerceUnsized<Wrapper<dyn Any>> for Wrapper<i32> {}

fn main() {
    let _: Wrapper<dyn Any> = Wrapper(&1_i32);
}
