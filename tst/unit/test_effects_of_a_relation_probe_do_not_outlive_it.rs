// tokio's time_rt test builds `Starve(Box::pin(sleep_until(when)), 0)` with
// `struct Starve<T: Future<Output = ()> + Unpin>`, while futures-util is
// loaded: `?T: Future<Output = ()>` is asked with `?T` still open and meets
// `MapOk`, whose `Output` is `<Map<IntoFuture<Fut>, MapOkFn<F>> as
// Future>::Output`. Relating that projection to `()` is a probe undone
// afterwards; normalizing it answered with inference variables made inside
// the probe, and those answers were kept on the candidate. After the rollback
// they named variables that no longer existed ("type ivar 1385 is not in a
// table of 1341"), or silently other ones. This shape runs the same path; the
// crash itself needs futures-util's whole candidate set.
use std::future::Future;
use std::pin::Pin;
use std::task::{Context, Poll};

pub trait FnOnce1<A> {
    type Output;
    fn call_once(self, arg: A) -> Self::Output;
}

impl<T, A, R> FnOnce1<A> for T
where
    T: FnOnce(A) -> R,
{
    type Output = R;
    fn call_once(self, arg: A) -> R {
        self(arg)
    }
}

pub struct MapOkFn<F>(F);

impl<F, T, E> FnOnce1<Result<T, E>> for MapOkFn<F>
where
    F: FnOnce1<T>,
{
    type Output = Result<F::Output, E>;
    fn call_once(self, arg: Result<T, E>) -> Self::Output {
        arg.map(|v| self.0.call_once(v))
    }
}

pub trait TryFuture: Future {
    type Ok;
    type Error;
}

impl<F, T, E> TryFuture for F
where
    F: ?Sized + Future<Output = Result<T, E>>,
{
    type Ok = T;
    type Error = E;
}

pub struct IntoFut<Fut>(Fut);

impl<Fut: TryFuture> Future for IntoFut<Fut> {
    type Output = Result<Fut::Ok, Fut::Error>;
    fn poll(self: Pin<&mut Self>, _cx: &mut Context<'_>) -> Poll<Self::Output> {
        Poll::Pending
    }
}

pub struct Map<Fut, F>(Fut, Option<F>);

impl<Fut, F, T> Future for Map<Fut, F>
where
    Fut: Future,
    F: FnOnce1<Fut::Output, Output = T>,
{
    type Output = T;
    fn poll(self: Pin<&mut Self>, _cx: &mut Context<'_>) -> Poll<T> {
        Poll::Pending
    }
}

pub struct MapOk<Fut, F>(Map<IntoFut<Fut>, MapOkFn<F>>);

impl<Fut, F> Future for MapOk<Fut, F>
where
    Map<IntoFut<Fut>, MapOkFn<F>>: Future,
{
    type Output = <Map<IntoFut<Fut>, MapOkFn<F>> as Future>::Output;
    fn poll(self: Pin<&mut Self>, _cx: &mut Context<'_>) -> Poll<Self::Output> {
        Poll::Pending
    }
}

pub struct TryFlatten<Fut, Next>(Fut, Option<Next>);

impl<Fut> Future for TryFlatten<Fut, Fut::Ok>
where
    Fut: TryFuture,
    Fut::Ok: TryFuture<Error = Fut::Error>,
{
    type Output = Result<<Fut::Ok as TryFuture>::Ok, Fut::Error>;
    fn poll(self: Pin<&mut Self>, _cx: &mut Context<'_>) -> Poll<Self::Output> {
        Poll::Pending
    }
}

pub struct AndThen<Fut1, Fut2, F>(TryFlatten<MapOk<Fut1, F>, Fut2>);

impl<Fut1, Fut2, F> Future for AndThen<Fut1, Fut2, F>
where
    TryFlatten<MapOk<Fut1, F>, Fut2>: Future,
{
    type Output = <TryFlatten<MapOk<Fut1, F>, Fut2> as Future>::Output;
    fn poll(self: Pin<&mut Self>, _cx: &mut Context<'_>) -> Poll<Self::Output> {
        Poll::Pending
    }
}


struct Sleep;

impl Future for Sleep {
    type Output = ();
    fn poll(self: Pin<&mut Self>, _cx: &mut Context<'_>) -> Poll<()> {
        Poll::Ready(())
    }
}

struct Starve<T: Future<Output = ()> + Unpin>(T, u64);

fn main() {
    let starve = Starve(Box::pin(Sleep), 0);
    assert_eq!(starve.1, 0);
}
