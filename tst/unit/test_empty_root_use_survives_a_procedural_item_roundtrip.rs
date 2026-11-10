//@ proc-macro-aux-build: own_empty_use_passthrough.rs

macro_rules! round_trip {
    ($item:item) => {
        own_empty_use_passthrough::preserve!($item);
    };
}

round_trip! {
    use {};
}

fn main() {
    let values = [7u16, 11, 19];
    assert_eq!(values.iter().copied().sum::<u16>(), 37);
}
