#![feature(specialization)]
#![allow(incomplete_features)]

trait Hidden {}

impl<T> Hidden for T {}

fn hide<T: Hidden>(value: T) -> impl Hidden {
    value
}

trait Select: Sized {
    type Output: Default;

    fn select(self) -> Self::Output {
        Default::default()
    }
}

impl<T> Select for T {
    default type Output = ();
}

impl<T: Send> Select for T {
    type Output = bool;
}

fn main() {
    let selected: bool = Select::select(hide(0_i32));
    assert!(!selected);
}
