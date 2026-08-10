//@ check-pass

#![feature(supertrait_item_shadowing)]

trait Base {
    fn selected() -> u8 {
        1
    }

    const VALUE: u8;
}

impl<T> Base for T {
    const VALUE: u8 = 1;
}

trait Child: Base {
    fn selected() -> u8 {
        2
    }

    const VALUE: u8;
}

impl<T> Child for T {
    const VALUE: u8 = 2;
}

fn selected_for<T>() -> u8 {
    T::selected()
}

fn main() {
    assert!(selected_for::<()>() == 2u8);
    assert!(u8::VALUE == 2u8);
}
