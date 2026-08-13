#![allow(dead_code)]

trait Project {
    type Output;
}

trait Conditional {
    type Input;

    fn check(&self)
    where
        Self::Input: Project;
}

impl<T> Conditional for T {
    type Input = ();

    fn check(&self)
    where
        Self::Input: Project,
    {
        let _: Option<<Self::Input as Project>::Output> = None;
    }
}

fn main() {}
