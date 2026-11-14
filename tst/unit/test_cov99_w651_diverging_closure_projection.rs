//@ compile-only
//@ compile-flags: --emit=metadata

trait ResultType {
    type Output;
}

impl<F, R> ResultType for F
where
    F: FnOnce() -> R,
{
    type Output = R;
}

fn invoke<F>(f: F) -> <F as ResultType>::Output
where
    F: ResultType + FnOnce() -> <F as ResultType>::Output,
{
    f()
}

fn main() {
    let _ = invoke(|| loop {});
}
