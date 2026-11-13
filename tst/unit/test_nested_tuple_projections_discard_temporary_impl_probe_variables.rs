trait Invoke<A> {
    type Output;
}

impl<F, A, R> Invoke<A> for F
where
    F: FnOnce(A) -> R,
{
    type Output = R;
}

trait Gate<T> {}
impl Gate<u8> for () {}

struct First<F>(F);

impl<F, A, B> Invoke<(A, B)> for First<F>
where
    F: Invoke<A>,
    (): Gate<A>,
{
    type Output = (F::Output, B);
}

trait Produce {
    type Output;
}

struct Source;
impl Produce for Source {
    type Output = (u8, u8);
}

struct Owned<T>(T);
impl<T: Produce> Produce for Owned<T> {
    type Output = T::Output;
}

struct Mapped<S, F>(S, F);
impl<S: Produce, F: Invoke<S::Output>> Produce for Mapped<S, F> {
    type Output = F::Output;
}

struct Buffered<S, F>(Mapped<S, First<F>>);
impl<S, F> Produce for Buffered<S, F>
where
    Mapped<S, First<F>>: Produce,
{
    type Output = <Mapped<S, First<F>> as Produce>::Output;
}

fn own<T>(value: T) -> Owned<T> {
    Owned(value)
}

struct Require<T: Produce<Output = (u8, u8)>>(T, u8);

fn main() {
    let value = Require(own(Source), 23);
    assert_eq!(value.1, 23);
}
