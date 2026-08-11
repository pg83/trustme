//@ check-pass
//@ compile-flags: -Znext-solver

fn consume<T>(_iter: &mut impl Iterator<Item = T>) {}

trait Spec<'a, T: 'a>: Iterator<Item = &'a T>
where
    T: Copy,
{
    fn spec(&mut self);
}

impl<'a, I, T: 'a> Spec<'a, T> for I
where
    I: Iterator<Item = &'a T>,
    T: Copy,
{
    fn spec(&mut self) {
        consume(&mut self.copied());
    }
}

fn main() {}
