//@ crate-type: lib
//@ compile-fail: Type mismatch

trait Project {
    type Output;
}

fn value() -> u32 {
    0
}

fn unconstrained<S, T>() -> T
where
    S: Project<Output = T>,
{
    value()
}
