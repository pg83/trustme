pub trait Operations {
    fn choose<T>(&self, value: T, truth: bool) -> (T, bool);
}

#[derive(Clone, Copy)]
pub struct Source;

impl Operations for Source {
    fn choose<T>(&self, value: T, truth: bool) -> (T, bool) { (value, !truth) }
}
