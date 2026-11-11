//@ compile-fail: nested quantification of lifetimes

trait Inspect<'a> {}

fn inspect<T>()
where
    for<'outer> &'outer T: for<'inner> Inspect<'inner>,
{
}

fn main() {}
