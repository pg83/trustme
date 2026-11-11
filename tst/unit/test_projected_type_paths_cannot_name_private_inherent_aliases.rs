//@ compile-fail: Failed to find impl with 'Hidden'
#![feature(inherent_associated_types)]

mod vault {
    pub struct Record;

    impl Record {
        type Hidden = [u16; 2];
    }
}

trait Select {
    type Chosen;
}

impl Select for () {
    type Chosen = vault::Record;
}

type Leaked = <<() as Select>::Chosen>::Hidden;

fn main() {}
