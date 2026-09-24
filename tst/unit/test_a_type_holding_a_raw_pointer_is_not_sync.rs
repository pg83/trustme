// futures' auto_traits tests are `static_assertions::assert_not_impl!`: an
// ambiguous impl selection unless the type lacks the auto trait. With
// `PhantomData<T>` counted as `Sync` whatever `T`, the selection stayed
// ambiguous for types that are not.
use std::marker::PhantomData;
macro_rules! assert_not_impl {
    ($x:ty: $($t:path),+ $(,)?) => {
        const _: fn() = || {
            trait AmbiguousIfImpl<A> { fn some_item() {} }
            impl<T: ?Sized> AmbiguousIfImpl<()> for T {}
            #[allow(dead_code)]
            struct Invalid;
            impl<T: ?Sized $(+ $t)+> AmbiguousIfImpl<Invalid> for T {}
            let _ = <$x as AmbiguousIfImpl<_>>::some_item;
        };
    };
}
struct Local(PhantomData<*const ()>);
struct Wrap<A, B>(A, B);
assert_not_impl!(Local: Send);
assert_not_impl!(Wrap<u8, Local>: Send);
assert_not_impl!(Wrap<Local, u8>: Sync);
fn main() {}
