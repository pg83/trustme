/* zerocopy's `try_transmute_mut` (`ptr.try_with_unchecked(|ptr| ..)` on a `Ptr<Src, (Exclusive,
   Aligned, Valid)>`): the method's `J: Invariants<Aliasing = I::Aliasing>` is decided by its one
   impl only in part - `(Exclusive, ?, ?)` - before the closure is checked; the rest comes from
   the closure's return, and the probe must not hand the caller its own variables for it. */
use std::marker::PhantomData;

trait Aliasing {}
trait Alignment {}
trait Validity {}
struct Exclusive;
struct Aligned;
struct Unaligned;
struct Valid;
struct Initialized;
impl Aliasing for Exclusive {}
impl Alignment for Aligned {}
impl Alignment for Unaligned {}
impl Validity for Valid {}
impl Validity for Initialized {}

trait Invariants {
    type Aliasing: Aliasing;
    type Alignment: Alignment;
    type Validity: Validity;
}
impl<A: Aliasing, AA: Alignment, V: Validity> Invariants for (A, AA, V) {
    type Aliasing = A;
    type Alignment = AA;
    type Validity = V;
}

struct Ptr<T: ?Sized, I: Invariants> {
    ptr: *mut T,
    _i: PhantomData<I>,
}

trait TryWithError<P> {
    type Mapped;
    fn map(self, p: P) -> Self::Mapped;
}

struct Failure<S>(S);
impl<T, I: Invariants> TryWithError<Ptr<T, I>> for Failure<()> {
    type Mapped = Failure<Ptr<T, I>>;
    fn map(self, p: Ptr<T, I>) -> Failure<Ptr<T, I>> {
        Failure(p)
    }
}

impl<T: ?Sized, I: Invariants> Ptr<T, I> {
    fn from_mut(t: &mut T) -> Ptr<T, (Exclusive, Aligned, Valid)> {
        Ptr { ptr: t, _i: PhantomData }
    }

    fn try_with<U: ?Sized, J, E, F>(self, f: F) -> Result<Ptr<U, J>, E::Mapped>
    where
        J: Invariants<Aliasing = I::Aliasing>,
        E: TryWithError<Self>,
        F: FnOnce(Ptr<T, I>) -> Result<Ptr<U, J>, E>,
    {
        let old = self.ptr;
        f(self).map_err(|err| err.map(Ptr { ptr: old, _i: PhantomData }))
    }
}

fn try_transmute_mut<Src, Dst>(src: &mut Src, ok: bool) -> Result<&mut Dst, Failure<Ptr<Src, (Exclusive, Aligned, Valid)>>> {
    let ptr = Ptr::<Src, (Exclusive, Aligned, Valid)>::from_mut(src);
    match ptr.try_with(|ptr| {
        if ok {
            Ok(Ptr::<Dst, (Exclusive, Unaligned, Initialized)> { ptr: ptr.ptr as *mut Dst, _i: PhantomData })
        } else {
            Err(Failure(()))
        }
    }) {
        Ok(ptr) => Ok(unsafe { &mut *ptr.ptr }),
        Err(err) => Err(err),
    }
}

fn main() {
    let mut x = 7u32;
    let y: &mut u32 = try_transmute_mut::<u32, u32>(&mut x, true).ok().unwrap();
    *y += 1;
    assert_eq!(x, 8);
    assert!(try_transmute_mut::<u32, u32>(&mut x, false).is_err());
}
