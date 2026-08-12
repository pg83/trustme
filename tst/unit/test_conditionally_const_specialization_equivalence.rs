#![feature(const_trait_impl)]
#![feature(min_specialization)]
#![feature(rustc_attrs)]

#[const_trait]
#[rustc_specialization_trait]
trait Foo {}

#[const_trait]
trait Bar {
    fn bar();
}

impl<T: [const] Foo> const Bar for (T, ()) {
    fn bar() {}
}

impl<T: Foo, U> Bar for (T, U) {
    default fn bar() {}
}

struct Selected;

impl const Foo for Selected {}

fn main() {
    <(Selected, ())>::bar();
}
