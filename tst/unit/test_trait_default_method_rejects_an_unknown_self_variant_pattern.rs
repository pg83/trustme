//@ compile-fail: outside of an impl block
#![feature(more_qualified_paths)]

trait Decompose {
    fn decompose(self)
    where
        Self: Sized,
    {
        match self {
            <Self>::Missing { .. } => {}
        }
    }
}

fn main() {}
