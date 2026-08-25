//@ crate-type: lib
//@ compile-flags: -Znext-solver=globally

// `Output` on an `FnMut` goal is declared on `FnOnce`: the requirement
// must project through the declaring trait so it folds through the
// elaborated ParamEnv equality.  Mirrors liballoc resize_with/repeat_with.

struct RW<F> {
    f: F,
}

fn repeat_with2<A, F: FnMut() -> A>(repeater: F) -> RW<F> {
    RW { f: repeater }
}

fn resize_with2<T>(generator: impl FnMut() -> T) -> RW<impl FnMut() -> T> {
    repeat_with2(generator)
}
