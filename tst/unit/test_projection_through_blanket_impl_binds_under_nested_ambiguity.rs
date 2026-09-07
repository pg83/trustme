// `<Ready<Option<_>> as IntoFuture2>::IntoFuture2` has one candidate, the blanket
// `impl<F: Future2> IntoFuture2 for F`, whose nested `Ready<Option<_>>: Future2`
// is still ambiguous (`Option<_>: Copy` waits on `_`).  Upstream keeps such a
// goal pending with its constraints applied - a `Maybe` response's inference
// constraints are instantiated like a `Yes` one, and only a self type that *is*
// an inference variable forces a constraint-free ambiguity
// (`assemble_self_ty_infer_ambiguity_response`).  So the projection is
// `Ready<Option<_>>`, its `Output` is `Option<_>`, and the annotation on the
// result fills in `()`, which then proves the nested bound.
//
// Reduced from the upstream test async-await/issue-105501.rs.
trait Future2 {
    type Output;
    fn get(self) -> Self::Output;
}

trait IntoFuture2 {
    type Output;
    type IntoFuture2: Future2<Output = Self::Output>;
    fn into_future2(self) -> Self::IntoFuture2;
}

impl<F: Future2> IntoFuture2 for F {
    type Output = F::Output;
    type IntoFuture2 = F;
    fn into_future2(self) -> F {
        self
    }
}

struct Ready<T>(T);

impl<T: Copy> Future2 for Ready<T> {
    type Output = T;
    fn get(self) -> T {
        self.0
    }
}

fn output<I: IntoFuture2>(i: I) -> <I::IntoFuture2 as Future2>::Output {
    i.into_future2().get()
}

fn main() {
    let fut = Ready(None);
    let r: Option<()> = output(fut);
    assert!(r.is_none());
}
