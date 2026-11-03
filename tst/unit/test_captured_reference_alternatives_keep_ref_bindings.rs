macro_rules! read {
    ($value:expr; $pattern:pat => $body:expr) => {
        match $value { $pattern => $body }
    };
}

fn main() {
    let value = 37u32;
    assert_eq!(read!(&value; &ref selected | &ref selected => *selected), 37);
    assert_eq!(read!(&&value; &&ref selected | &&ref selected => *selected), 37);
}
