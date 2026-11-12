#![no_std]

pub trait Package {
    type Wrapped;
}

impl<T> Package for T {
    type Wrapped = Option<T>;
}

pub trait Source {
    fn iter() -> impl Iterator<Item = u16>;
}

pub struct Values;

impl Source for Values {
    fn iter() -> impl Iterator<Item = u16> {
        [41u16, 59, 71].into_iter()
    }
}
