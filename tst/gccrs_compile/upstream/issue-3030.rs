
#![feature(negative_impls)]

pub trait Deref {}

pub trait DerefMut: Deref {
    type Target;

    /// Mutably dereferences the value.
    fn deref_mut(&mut self) -> &mut Self::Target;
}

impl<T: ?Sized> !DerefMut for &T {}
