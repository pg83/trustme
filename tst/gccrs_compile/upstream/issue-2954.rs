pub trait LocalReceiver {}
impl<T: ?Sized> LocalReceiver for &T {}
impl<T: ?Sized> LocalReceiver for &mut T {}
