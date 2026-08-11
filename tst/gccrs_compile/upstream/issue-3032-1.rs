#![feature(negative_impls)]

trait LocalDeref {
    type Target: ?Sized;
    fn deref(&self) -> &Self::Target;
}

trait LocalDerefMut: LocalDeref {
    fn deref_mut(&mut self) -> &mut Self::Target;
}

struct Shared<T: ?Sized>(*const T);

impl<T: ?Sized> LocalDeref for Shared<T> {
    type Target = T;
    fn deref(&self) -> &T { unsafe { &*self.0 } }
}

impl<T: ?Sized> !LocalDerefMut for Shared<T> {}
