//@ compile-fail: outside of an impl block

trait Decompose {
    fn decompose(self)
    where
        Self: Sized,
    {
        let Self { .. } = self;
    }
}

fn main() {}
