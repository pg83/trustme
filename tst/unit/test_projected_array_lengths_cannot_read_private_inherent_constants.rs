//@ compile-fail: Failed to find impl with 'WIDTH'

mod vault {
    pub struct Record;

    impl Record {
        const WIDTH: usize = 3;
    }
}

trait Select {
    type Chosen;
}

impl Select for () {
    type Chosen = vault::Record;
}

type Bytes = [u8; <<() as Select>::Chosen>::WIDTH];

fn main() {}
