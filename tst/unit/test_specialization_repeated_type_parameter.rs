#![feature(specialization)]

trait Kind {
    fn kind(&self) -> &'static str;
}

impl<T: Clone, U: Clone> Kind for (T, U) {
    default fn kind(&self) -> &'static str {
        "pair"
    }
}

impl<T: Clone> Kind for (T, T) {
    default fn kind(&self) -> &'static str {
        "uniform"
    }
}

fn main() {
    assert_eq!(((), ()).kind(), "uniform");
}
