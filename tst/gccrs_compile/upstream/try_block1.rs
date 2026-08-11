// { dg-additional-options "-frust-edition=2018" }

#![feature(try_blocks)]

pub fn test() -> Result<i32, ()> {
    let value: Result<i32, ()> = try { 15i32 };
    value
}
