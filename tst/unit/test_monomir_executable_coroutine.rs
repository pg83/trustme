//@ compile-only
//@ compile-flags: -Ccodegen-type=monomir -Zmir-opt-level=0 --env-set TRUSTME_DUMP=ast:hir:mir
//@ output-contains: .mir fn main#(isize, *const *const i8): isize
//@ output-contains: .mir fn panic_impl#(usize): u32
//@ output-contains: .mir LOADFLAG df
//@ output-contains: .mir SAVEFLAG

#![feature(coroutines, coroutine_trait, stmt_expr_attributes)]
use std::ops::{Coroutine, CoroutineState};

struct Owned(String);
impl Drop for Owned {
    fn drop(&mut self) { self.0.clear(); }
}

fn main() {
    let mut coroutine = Box::pin(#[coroutine] |flag: bool| {
        let first = Owned(String::from("first"));
        let second = Owned(String::from("second"));
        if flag { drop(first); }
        let next = yield second.0.len();
        if next { drop(second); }
        yield 0;
        7
    });
    assert_eq!(coroutine.as_mut().resume(true), CoroutineState::Yielded(6));
    assert_eq!(coroutine.as_mut().resume(false), CoroutineState::Yielded(0));
    assert_eq!(coroutine.as_mut().resume(false), CoroutineState::Complete(7));
}
