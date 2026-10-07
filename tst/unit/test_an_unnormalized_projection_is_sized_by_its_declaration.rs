// tor-chanmgr (arti) writes `sleep.then(move |_| async move { .. }
// .map_err(move |e: ConnectError| (e, a)))` inside an iterator's `map`
// closure. futures' `MapErr<Fut, F>` is a `Future` through
// `MapErrFn<F>: FnOnce1<Result<Fut::Ok, Fut::Error>>`, whose impl
// `impl<F, T, E> FnOnce1<Result<T, E>> for MapErrFn<F>` needs `T: Sized` for
// `T = <async as TryFuture>::Ok` while the async block's output is still being
// inferred. The projection cannot be normalized yet, and it is `Sized` all the
// same: upstream proves it from the associated type's own (implicit) `Sized`
// bound. We took that only from a projection already known to be rigid, so
// the impl had "no solution" and `then` failed to find `MapErr<..>: Future`.
// The same shape here, with futures' types written out.
use std::future::Future;
use std::pin::{pin, Pin};
use std::task::{Context, Poll, Waker};

trait TryFuture: Future {
    type Ok;
    type Error;
    fn try_poll(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Result<Self::Ok, Self::Error>>;
}

impl<F: ?Sized, T, E> TryFuture for F
where
    F: Future<Output = Result<T, E>>,
{
    type Ok = T;
    type Error = E;
    fn try_poll(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<F::Output> {
        self.poll(cx)
    }
}

trait FnOnce1<A> {
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

struct MapErrFn<F>(F);

impl<F, T, E> FnOnce1<Result<T, E>> for MapErrFn<F>
where
    F: FnOnce1<E>,
{
    type Output = Result<T, F::Output>;
    fn call_once(self, arg: Result<T, E>) -> Self::Output {
        arg.map_err(|x| self.0.call_once(x))
    }
}

struct IntoFuture<Fut>(Fut);

impl<Fut: TryFuture> Future for IntoFuture<Fut> {
    type Output = Result<Fut::Ok, Fut::Error>;
    fn poll(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        unsafe { self.map_unchecked_mut(|s| &mut s.0) }.try_poll(cx)
    }
}

struct Map<Fut, F> {
    fut: Fut,
    f: Option<F>,
}

impl<Fut, F, T> Future for Map<Fut, F>
where
    Fut: Future,
    F: FnOnce1<Fut::Output, Output = T>,
{
    type Output = T;
    fn poll(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<T> {
        let this = unsafe { self.get_unchecked_mut() };
        match unsafe { Pin::new_unchecked(&mut this.fut) }.poll(cx) {
            Poll::Ready(v) => Poll::Ready(this.f.take().unwrap().call_once(v)),
            Poll::Pending => Poll::Pending,
        }
    }
}

struct MapErr<Fut, F>(Map<IntoFuture<Fut>, MapErrFn<F>>);

impl<Fut, F> Future for MapErr<Fut, F>
where
    Map<IntoFuture<Fut>, MapErrFn<F>>: Future,
{
    type Output = <Map<IntoFuture<Fut>, MapErrFn<F>> as Future>::Output;
    fn poll(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        unsafe { self.map_unchecked_mut(|s| &mut s.0) }.poll(cx)
    }
}

trait TryFutureExt: TryFuture {
    fn map_err<E, F>(self, f: F) -> MapErr<Self, F>
    where
        F: FnOnce(Self::Error) -> E,
        Self: Sized,
    {
        MapErr(Map { fut: IntoFuture(self), f: Some(MapErrFn(f)) })
    }
}

impl<Fut: ?Sized + TryFuture> TryFutureExt for Fut {}

struct Then<Fut1, Fut2, F> {
    first: Option<(Fut1, F)>,
    second: Option<Fut2>,
}

impl<Fut1, Fut2, F> Future for Then<Fut1, Fut2, F>
where
    Fut1: Future,
    Fut2: Future,
    F: FnOnce(Fut1::Output) -> Fut2,
{
    type Output = Fut2::Output;
    fn poll(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Fut2::Output> {
        let this = unsafe { self.get_unchecked_mut() };
        if let Some((mut fut, f)) = this.first.take() {
            match unsafe { Pin::new_unchecked(&mut fut) }.poll(cx) {
                Poll::Ready(v) => this.second = Some(f(v)),
                Poll::Pending => {
                    this.first = Some((fut, f));
                    return Poll::Pending;
                }
            }
        }
        unsafe { Pin::new_unchecked(this.second.as_mut().unwrap()) }.poll(cx)
    }
}

trait FutureExt: Future {
    fn then<Fut, F>(self, f: F) -> Then<Self, Fut, F>
    where
        F: FnOnce(Self::Output) -> Fut,
        Fut: Future,
        Self: Sized,
    {
        Then { first: Some((self, f)), second: None }
    }
}

impl<T: ?Sized + Future> FutureExt for T {}

async fn sleep() {}

#[derive(Debug, PartialEq)]
struct Small(u32);

#[derive(Debug, PartialEq)]
struct ConnectError(u32);

impl From<Small> for ConnectError {
    fn from(s: Small) -> Self {
        ConnectError(s.0)
    }
}

fn check(a: u32) -> Result<u32, Small> {
    if a > 5 { Err(Small(a)) } else { Ok(a * 2) }
}

fn run(a: u32) -> Result<(u32, u32), (ConnectError, u32)> {
    let addrs = [a];
    let mut futures = addrs
        .iter()
        .map(|a| {
            sleep().then(move |_| {
                let a = *a;
                async move {
                    let doubled = check(a)?;
                    Ok((doubled, a))
                }
                .map_err(move |e: ConnectError| (e, a))
            })
        })
        .collect::<Vec<_>>();
    let mut fut = pin!(futures.pop().unwrap());
    match fut.as_mut().poll(&mut Context::from_waker(Waker::noop())) {
        Poll::Ready(v) => v,
        Poll::Pending => unreachable!(),
    }
}

fn main() {
    assert_eq!(run(3), Ok((6, 3)));
    assert_eq!(run(7), Err((ConnectError(7), 7)));
}
