//@ edition: 2021
// arti-client (arti) collects tor-circmgr's `get_or_launch` futures with
// `.map(MaybeDone::Future)`; each awaits a `tracing::Instrumented<..>`, whose
// pin-project `Drop` pins itself with `Pin::new_unchecked(self)`. Both calls are
// `<Pin<&mut Instrumented<..>>>::new_unchecked` of `impl<Ptr: Deref> Pin<Ptr>`.
// The `.await` lowering named it without the impl's arguments, the checked call
// with them. Code generation met the awaited spelling first; the constructor,
// passed as a function, re-enumerated the future's types after monomorphisation
// and reached the `Drop` spelling, and it stopped at "Distinct function paths
// have the same mangled name".
use std::future::Future;
use std::pin::{pin, Pin};
use std::task::{Context, Poll, Waker};

struct Instrumented<T> {
    inner: T,
}

impl<T: Future> Future for Instrumented<T> {
    type Output = T::Output;
    fn poll(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<T::Output> {
        unsafe { self.map_unchecked_mut(|s| &mut s.inner) }.poll(cx)
    }
}

impl<T> Drop for Instrumented<T> {
    fn drop(&mut self) {
        let pinned: Pin<&mut Self> = unsafe { Pin::new_unchecked(self) };
        let _ = pinned;
    }
}

async fn get_or_launch(value: u32) -> u32 {
    Instrumented { inner: async move { value } }.await + 1
}

fn main() {
    let futures: Vec<Option<_>> = [get_or_launch(3)].into_iter().map(Some).collect();
    for future in futures {
        let mut future = pin!(future.unwrap());
        assert_eq!(future.as_mut().poll(&mut Context::from_waker(Waker::noop())), Poll::Ready(4));
    }
}
