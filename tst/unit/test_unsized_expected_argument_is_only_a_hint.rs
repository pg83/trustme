/* syn 3's `Punctuated::iter` / `empty_punctuated_iter`: `Box::new(NoDrop::new(x))`
   where the field wants `Box<NoDrop<dyn IterTrait<'a, T> + 'a>>`. */
use std::iter;
use std::mem::ManuallyDrop;
use std::ops::{Deref, DerefMut};
use std::option;
use std::slice;

#[repr(transparent)]
struct NoDrop<T: ?Sized>(ManuallyDrop<T>);

impl<T> NoDrop<T> {
    fn new(value: T) -> Self
    where
        T: TrivialDrop,
    {
        NoDrop(ManuallyDrop::new(value))
    }
}

impl<T: ?Sized> Deref for NoDrop<T> {
    type Target = T;
    fn deref(&self) -> &Self::Target {
        &self.0
    }
}

impl<T: ?Sized> DerefMut for NoDrop<T> {
    fn deref_mut(&mut self) -> &mut Self::Target {
        &mut self.0
    }
}

trait TrivialDrop {}

impl<T> TrivialDrop for iter::Empty<T> {}
impl<T> TrivialDrop for slice::Iter<'_, T> {}
impl<T> TrivialDrop for option::IntoIter<&T> {}

pub struct Iter<'a, T: 'a> {
    inner: Box<NoDrop<dyn IterTrait<'a, T> + 'a>>,
}

trait IterTrait<'a, T: 'a>: Iterator<Item = &'a T> + DoubleEndedIterator + ExactSizeIterator {
    fn clone_box(&self) -> Box<NoDrop<dyn IterTrait<'a, T> + 'a>>;
}

struct PrivateIter<'a, T: 'a, P: 'a> {
    inner: slice::Iter<'a, (T, P)>,
    last: option::IntoIter<&'a T>,
}

impl<'a, T, P> TrivialDrop for PrivateIter<'a, T, P>
where
    slice::Iter<'a, (T, P)>: TrivialDrop,
    option::IntoIter<&'a T>: TrivialDrop,
{
}

pub(crate) fn empty_punctuated_iter<'a, T>() -> Iter<'a, T> {
    Iter {
        inner: Box::new(NoDrop::new(iter::empty())),
    }
}

impl<'a, T> Clone for Iter<'a, T> {
    fn clone(&self) -> Self {
        Iter {
            inner: self.inner.clone_box(),
        }
    }
}

impl<'a, T> Iterator for Iter<'a, T> {
    type Item = &'a T;
    fn next(&mut self) -> Option<Self::Item> {
        self.inner.next()
    }
    fn size_hint(&self) -> (usize, Option<usize>) {
        (self.len(), Some(self.len()))
    }
}

impl<'a, T> DoubleEndedIterator for Iter<'a, T> {
    fn next_back(&mut self) -> Option<Self::Item> {
        self.inner.next_back()
    }
}

impl<'a, T> ExactSizeIterator for Iter<'a, T> {
    fn len(&self) -> usize {
        self.inner.len()
    }
}

impl<'a, T, P> Iterator for PrivateIter<'a, T, P> {
    type Item = &'a T;
    fn next(&mut self) -> Option<Self::Item> {
        self.inner.next().map(|pair| &pair.0).or_else(|| self.last.next())
    }
    fn size_hint(&self) -> (usize, Option<usize>) {
        (self.len(), Some(self.len()))
    }
}

impl<'a, T, P> DoubleEndedIterator for PrivateIter<'a, T, P> {
    fn next_back(&mut self) -> Option<Self::Item> {
        self.last.next().or_else(|| self.inner.next_back().map(|pair| &pair.0))
    }
}

impl<'a, T, P> ExactSizeIterator for PrivateIter<'a, T, P> {
    fn len(&self) -> usize {
        self.inner.len() + self.last.len()
    }
}

impl<'a, T, P> Clone for PrivateIter<'a, T, P> {
    fn clone(&self) -> Self {
        PrivateIter { inner: self.inner.clone(), last: self.last.clone() }
    }
}

impl<'a, T, I> IterTrait<'a, T> for I
where
    T: 'a,
    I: DoubleEndedIterator<Item = &'a T> + ExactSizeIterator<Item = &'a T> + Clone + TrivialDrop + 'a,
{
    fn clone_box(&self) -> Box<NoDrop<dyn IterTrait<'a, T> + 'a>> {
        Box::new(NoDrop::new(self.clone()))
    }
}

fn main() {
    let it = empty_punctuated_iter::<u8>();
    assert_eq!(it.clone().len(), 0);
}
