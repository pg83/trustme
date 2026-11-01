pub trait Convert<T> {
    type Output;
    fn convert(&self, value: T) -> Self::Output;
}

pub trait Borrow<'a> {
    fn borrow(&self) -> &'a u16;
}

pub trait Count<const N: usize> {
    fn count(&self) -> usize { N }
}
