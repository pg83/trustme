use std::marker::PhantomData;
trait Hash { fn hash<H>(&self, state: &mut H); }
impl<T> Hash for PhantomData<T> { fn hash<H>(&self, _state: &mut H) {} }
