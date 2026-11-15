//@ compile-only
//@ compile-flags: --emit=metadata -Cpanic=abort

use std::ops::Deref;

struct Deferred<F>(F);

impl<F, T> Deref for Deferred<F>
where
    F: Fn() -> T,
{
    type Target = T;
    fn deref(&self) -> &T { loop {} }
}

fn main() {
    let deferred = Deferred(|| Vec::new());
    let vector: &Vec<_> = &deferred;
    let _: &[u8] = vector;
    let _ = Some(()).unwrap();
}
