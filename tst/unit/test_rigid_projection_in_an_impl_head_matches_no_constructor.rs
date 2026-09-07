/* zerocopy `Ptr::as_bytes` from `is_zeroed`: `[u8]: TransmuteFromPtr<T, I::Aliasing, .., R>`
   through the blanket impl and `TryTransmuteFromPtr`, whose impls fix `R`. */
use std::marker::PhantomData;

trait Aliasing {}
struct Shared;
struct Exclusive;
impl Aliasing for Shared {}
impl Aliasing for Exclusive {}
trait Reference: Aliasing {}
impl Reference for Shared {}
impl Reference for Exclusive {}

trait Invariants {
    type Aliasing: Aliasing;
}
struct Inv<A>(PhantomData<A>);
impl<A: Aliasing> Invariants for Inv<A> {
    type Aliasing = A;
}

trait Immutable {}
impl Immutable for u8 {}
impl Immutable for [u8] {}

trait Read<A: Aliasing, R> {}
enum BecauseExclusive {}
enum BecauseImmutable {}
impl<A: Aliasing, T: ?Sized + Immutable> Read<A, BecauseImmutable> for T {}
impl<T: ?Sized> Read<Exclusive, BecauseExclusive> for T {}

trait MutationCompatible<Src: ?Sized, A: Aliasing, R> {}
enum BecauseRead {}
impl<Src: ?Sized, Dst: ?Sized, A: Aliasing, R> MutationCompatible<Src, A, (BecauseRead, R)> for Dst
where
    Src: Read<A, R>,
    Dst: Read<A, R>,
{
}

trait TryTransmuteFromPtr<Src: ?Sized, A: Aliasing, R> {}
enum BecauseMutationCompatible {}
impl<Src: ?Sized, Dst: ?Sized, A: Aliasing, R> TryTransmuteFromPtr<Src, A, (BecauseMutationCompatible, R)> for Dst where Dst: MutationCompatible<Src, A, R> {}
impl<Src: ?Sized + Immutable, Dst: ?Sized + Immutable> TryTransmuteFromPtr<Src, Shared, BecauseImmutable> for Dst {}

trait TransmuteFromPtr<Src: ?Sized, A: Aliasing, R>: TryTransmuteFromPtr<Src, A, R> {}
impl<Src: ?Sized, Dst: ?Sized, A: Aliasing, R> TransmuteFromPtr<Src, A, R> for Dst where Dst: TryTransmuteFromPtr<Src, A, R> {}

struct Ptr<'a, T: ?Sized, I> {
    ptr: &'a T,
    _i: PhantomData<I>,
}

impl<'a, T: ?Sized, I: Invariants> Ptr<'a, T, I> {
    fn as_bytes<R>(self) -> Ptr<'a, [u8], I>
    where
        [u8]: TransmuteFromPtr<T, I::Aliasing, R>,
    {
        let bytes = unsafe { std::slice::from_raw_parts(self.ptr as *const T as *const u8, std::mem::size_of_val(self.ptr)) };
        Ptr { ptr: bytes, _i: PhantomData }
    }
}

fn is_zeroed<T, I>(ptr: Ptr<'_, T, I>) -> bool
where
    T: Immutable,
    I: Invariants,
    I::Aliasing: Reference,
{
    ptr.as_bytes().ptr.iter().all(|&byte| byte == 0)
}

fn main() {
    let x = 0u8;
    assert!(is_zeroed(Ptr::<u8, Inv<Shared>> { ptr: &x, _i: PhantomData }));
}
