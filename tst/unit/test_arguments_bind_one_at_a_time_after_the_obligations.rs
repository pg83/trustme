/* rustc `check_argument_types`: each argument is coerced after
   `resolve_vars_with_obligations`, so what the earlier arguments bound has let the
   obligations act.  In the impl below, `MyEq::eq(&**self, other)` binds `Self = [A]`
   from its receiver, the obligation `[A]: MyEq<?U>` then selects the one slice impl
   and fixes `?U = [?B]`, and only then is `other: &[B; 0]` coerced - by unsizing -
   into `&[?B]`.  Binding `?U = [B; 0]` from the argument first leaves no impl. */
use std::marker::PhantomData;
use std::ops::Deref;

pub trait MyEq<U: ?Sized = Self> {
    fn eq(&self, u: &U) -> bool;
}

impl<A, B> MyEq<[B]> for [A]
where
    A: MyEq<B>,
{
    fn eq(&self, other: &[B]) -> bool {
        self.len() == other.len() && self.iter().zip(other).all(|(a, b)| MyEq::eq(a, b))
    }
}

impl<'a, A, B, Lhs> MyEq<[B; 0]> for Lhs
where
    A: MyEq<B>,
    Lhs: Deref<Target = [A]>,
{
    fn eq(&self, other: &[B; 0]) -> bool {
        MyEq::eq(&**self, other)
    }
}

struct DerefWithHelper<H, T> {
    pub helper: H,
    pub marker: PhantomData<T>,
}

trait Helper<T> {
    fn helper_borrow(&self) -> &T;
}

impl<T> Helper<T> for Option<T> {
    fn helper_borrow(&self) -> &T {
        self.as_ref().unwrap()
    }
}

impl<T, H: Helper<T>> Deref for DerefWithHelper<H, T> {
    type Target = T;
    fn deref(&self) -> &T {
        self.helper.helper_borrow()
    }
}

pub fn check<T: MyEq>(x: T, y: T) -> bool {
    let d: DerefWithHelper<Option<T>, T> = DerefWithHelper { helper: Some(x), marker: PhantomData };
    d.eq(&y)
}

impl MyEq for u8 {
    fn eq(&self, u: &u8) -> bool {
        self == u
    }
}

fn main() {
    assert!(check(3u8, 3u8));
    assert!(!check(3u8, 4u8));
}
