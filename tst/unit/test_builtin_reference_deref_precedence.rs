#![feature(trivial_bounds)]
#![allow(unused)]

trait ImpossibleCopy {
    fn copy_value(&self) -> Self
    where
        Self: Copy;
}

impl ImpossibleCopy for str {
    fn copy_value(&self) -> Self
    where
        Self: Copy,
    {
        *"value"
    }
}

fn main() {}
