// axum's `Layered::call` writes `let future: Map<_, fn(Result<..>) -> _> =
// svc.oneshot(req).map(|result| ..)` and hands `future` to
// `LayeredFuture::new` afterwards. Upstream's `check_argument_types` coerces
// each argument into the input its expected result implies
// (`expected_inputs_for_expected_output`, the probe's bindings of outer
// variables undone but the inputs it read kept), then equates the declared
// parameter with it: the closure becomes a `fn` pointer and `F` is that
// pointer. The method probe dropped the expected result whenever relating it
// bound a variable of the caller - here the `_` of the annotation - and tied
// the closure to `F` directly, so `Map<_, closure>` met `Map<_, fn(..)>`.
use std::convert::Infallible;
use std::future::Future;
use std::pin::Pin;
use std::task::{Context, Poll};

pub struct Map<Fut, F> {
    fut: Fut,
    f: Option<F>,
}

impl<Fut: Future + Unpin, F: FnOnce(Fut::Output) -> T + Unpin, T> Future for Map<Fut, F> {
    type Output = T;
    fn poll(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<T> {
        match Pin::new(&mut self.fut).poll(cx) {
            Poll::Ready(v) => Poll::Ready((self.f.take().unwrap())(v)),
            Poll::Pending => Poll::Pending,
        }
    }
}

pub trait FutureExt: Future + Sized {
    fn map<U, F: FnOnce(Self::Output) -> U>(self, f: F) -> Map<Self, F> {
        Map { fut: self, f: Some(f) }
    }
}
impl<T: Future> FutureExt for T {}

pub trait Service<R> {
    type Response;
    type Error;
    type Future: Future<Output = Result<Self::Response, Self::Error>>;
    fn call(&mut self, req: R) -> Self::Future;
}

pub struct Add(u32);
impl Service<u32> for Add {
    type Response = u32;
    type Error = Infallible;
    type Future = std::future::Ready<Result<u32, Infallible>>;
    fn call(&mut self, req: u32) -> Self::Future {
        std::future::ready(Ok(req + self.0))
    }
}

pub struct Oneshot<S: Service<R>, R> {
    fut: S::Future,
}

impl<S: Service<R>, R> Future for Oneshot<S, R>
where
    S::Future: Unpin,
{
    type Output = Result<S::Response, S::Error>;
    fn poll(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        Pin::new(&mut self.fut).poll(cx)
    }
}

pub trait ServiceExt<R>: Service<R> + Sized {
    fn oneshot(mut self, req: R) -> Oneshot<Self, R> {
        Oneshot { fut: self.call(req) }
    }
}
impl<S: Service<R>, R> ServiceExt<R> for S {}

pub struct LayeredFuture<S: Service<u32>> {
    inner: Map<Oneshot<S, u32>, fn(Result<S::Response, S::Error>) -> S::Response>,
}

impl<S: Service<u32>> LayeredFuture<S> {
    fn new(inner: Map<Oneshot<S, u32>, fn(Result<S::Response, S::Error>) -> S::Response>) -> Self {
        Self { inner }
    }
}

fn layered<S: Service<u32, Error = Infallible>>(svc: S, req: u32) -> LayeredFuture<S>
where
    S::Future: Unpin,
{
    let future: Map<_, fn(Result<<S as Service<u32>>::Response, <S as Service<u32>>::Error>) -> _> = svc.oneshot(req).map(|result| match result {
        Ok(res) => res,
        Err(err) => match err {},
    });
    LayeredFuture::new(future)
}

fn main() {
    let mut fut = layered(Add(1), 41).inner;
    let waker = std::task::Waker::noop();
    let mut cx = Context::from_waker(&waker);
    assert_eq!(Pin::new(&mut fut).poll(&mut cx), Poll::Ready(42));
}
