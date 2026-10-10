// typewit's `TypeNe::with_fn(f)` requires `Invoke<F>: InjTypeFn<LeftArg>`,
// whose blanket impl has a parameter only its bounds decide:
// `impl<F, A, R> InjTypeFn<A> for F where F: TypeFn<A, Output = R>,
// F: RevTypeFn<R, Arg = A>`. Upstream instantiates `R` as an inference
// variable; the first bound makes it `<F as TypeFn<A>>::Output` and the
// second is asked about that. We asked the second bound in the form it had
// before the first was answered, with `R` still unknown, and its pending
// `P<u8>: RevTypeFn<?>` - two impls, `RevTypeFn<X>` and `RevTypeFn<Vec<X>>` -
// was left ambiguous for good.
use std::marker::PhantomData;

pub trait TypeFn<T: ?Sized> {
    type Output: ?Sized;
}

pub trait RevTypeFn<Ret: ?Sized>: TypeFn<Self::Arg, Output = Ret> {
    type Arg: ?Sized;
}

pub trait InjTypeFn<A: ?Sized> {
    type Ret: ?Sized;
}

impl<F, A: ?Sized, R: ?Sized> InjTypeFn<A> for F
where
    F: TypeFn<A, Output = R>,
    F: RevTypeFn<R, Arg = A>,
{
    type Ret = R;
}

pub struct Invoke<F>(PhantomData<fn() -> F>);

impl<F, T: ?Sized> TypeFn<T> for Invoke<F>
where
    F: TypeFn<T>,
{
    type Output = <F as TypeFn<T>>::Output;
}

impl<F, R: ?Sized> RevTypeFn<R> for Invoke<F>
where
    F: RevTypeFn<R>,
{
    type Arg = <F as RevTypeFn<R>>::Arg;
}

pub struct LeftArg;
pub struct RightArg;

pub fn with_fn<F>(_f: F) -> PhantomData<<Invoke<F> as InjTypeFn<LeftArg>>::Ret>
where
    Invoke<F>: InjTypeFn<LeftArg>,
{
    PhantomData
}

pub struct P<X>(PhantomData<X>);

impl<X> TypeFn<LeftArg> for P<X> {
    type Output = X;
}

impl<X> RevTypeFn<X> for P<X> {
    type Arg = LeftArg;
}

impl<X> TypeFn<RightArg> for P<X> {
    type Output = Vec<X>;
}

impl<X> RevTypeFn<Vec<X>> for P<X> {
    type Arg = RightArg;
}

fn main() {
    let made: PhantomData<u8> = with_fn(P(PhantomData));
    let _ = made;
}
