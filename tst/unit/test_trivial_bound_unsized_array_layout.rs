#![feature(trivial_bounds)]
#![allow(unused)]

fn build_vec()
where
    str: Copy,
{
    let _: Vec<str> = vec![*"value"];
}

fn main() {}
