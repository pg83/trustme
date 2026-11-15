//@ compile-only
//@ compile-flags: --emit=metadata -Cpanic=abort

struct Left<T>(T);
struct Right<T>(T);
struct Sink { left: Left<u8>, right: Right<u8> }

impl<T: Default> Default for Right<T> {
    fn default() -> Self { Right(T::default()) }
}

impl<T, U> PartialEq<Right<U>> for Left<T>
where
    T: PartialEq<U>,
{
    fn eq(&self, other: &Right<U>) -> bool { self.0.eq(&other.0) }
}

impl<T: PartialEq> PartialEq for Right<T> {
    fn eq(&self, other: &Self) -> bool { self.0.eq(&other.0) }
}

fn exercise(flag: bool) {
    let left = Left(Default::default());
    let right = Default::default();
    let _ = &left == &right;
    let joined = if flag { &right } else { &right };
    let _ = joined == joined;
    let _ = Sink { left, right };
}

fn main() { exercise(true); }
