//@ edition: 2021
// tokio-util's `ReusableBoxFuture::try_set`: `this.boxed = Pin::from(boxed)`
// with `boxed: Box<F>` and the field `Pin<Box<dyn Future + Send>>`. Upstream
// checks the right-hand side first - the argument makes `From`'s parameter
// `Box<F>` - and only then coerces the value; `coerce` begins with
// `resolve_vars_with_obligations`, which selects `Pin<?P>: From<Box<F>>` and so
// `?P = Box<F>`, and `Pin<Box<F>>` unsizes into the field. Coercing first
// made `?P` the field's `Box<dyn ..>` and no `From` impl matched.
use std::future::Future;
use std::pin::Pin;

struct R<'a, T> {
    boxed: Pin<Box<dyn Future<Output = T> + Send + 'a>>,
}

fn reuse<U, O, C: FnOnce(Box<U>) -> O>(new_value: U, callback: C) -> O {
    callback(Box::new(new_value))
}

fn set<'a, F: Future + Send + 'a>(this: &mut R<'a, F::Output>, future: F) {
    reuse(future, |boxed| this.boxed = Pin::from(boxed))
}

fn set2<'a, F: Future + Send + 'a>(this: &mut R<'a, F::Output>, future: F) {
    this.boxed = Pin::from(Box::new(future));
}

fn main() {
    let mut r = R { boxed: Box::pin(async { 1 }) };
    set(&mut r, async { 2 });
    set2(&mut r, async { 3 });
}
