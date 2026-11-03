#![feature(box_patterns)]
macro_rules! read {
    ($value:expr; $pattern:pat => $body:expr) => {
        match $value { $pattern => $body }
    };
}

fn main() {
    let value = Box::new(41u32);
    assert_eq!(read!(value; box ref selected | box ref selected => *selected), 41);
}
