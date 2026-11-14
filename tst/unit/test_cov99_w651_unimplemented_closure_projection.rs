//@ compile-fail: MissingResult
//@ compile-flags: --emit=metadata

trait MissingResult {
    type Output;
}

fn invoke<F>(f: F) -> <F as MissingResult>::Output
where
    F: FnOnce() -> <F as MissingResult>::Output + MissingResult,
{
    f()
}

fn main() {
    let _ = invoke(|| loop {});
}
