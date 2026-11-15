//@ compile-only
//@ compile-flags: --emit=metadata -Cpanic=abort

struct Left<T>(T);
struct Right<T>(T);

impl<T: Default> Default for Right<T> {
    fn default() -> Self { Right(T::default()) }
}

impl<T, U> PartialEq<Right<U>> for Left<T>
where
    T: PartialEq<U>,
{
    fn eq(&self, other: &Right<U>) -> bool { self.0.eq(&other.0) }
}

fn main() {
    let left = Left(Default::default());
    let right = Default::default();
    let _ = &left == &right;
    let _: Left<u8> = left;
    let _: Right<u8> = right;
}
