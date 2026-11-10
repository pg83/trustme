#![feature(crate_visibility_modifier)]

mod storage {
    crate struct Record {
        pub value: u16,
    }

    crate fn create() -> Record {
        Record { value: 29 }
    }
}

mod client {
    pub fn read() -> u16 {
        crate::storage::create().value
    }
}

fn main() {
    assert_eq!(client::read(), 29);
}
