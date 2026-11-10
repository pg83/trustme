//@ edition: 2024
#![feature(gen_blocks)]

mod model {
    pub struct Record {
        pub value: u8,
    }
}

fn main() {
    let mut slot = 0;
    model::Record { value: slot, .. } = model::Record { value: 37 };
    assert_eq!(slot, 37);

    let mut generated = {
        gen move { yield slot; }
    };
    assert_eq!(generated.next(), Some(37));
    assert_eq!(generated.next(), None);
}
