#![feature(fn_delegation)]
#![allow(incomplete_features)]

fn next(value: u32) -> u32 {
    value + 1
}

reuse next as through_module { self + 1 }

trait Read {
    fn read(&self) -> u32;

    reuse Read::read as through_trait { self }
}

struct Reading(u32);

impl Read for Reading {
    fn read(&self) -> u32 {
        self.0
    }
}

fn main() {
    assert_eq!(through_module(40), 42);
    assert_eq!(Reading(7).through_trait(), 7);
}
