struct Record {
    value: u32,
}

fn main() {
    let record = Record {
        #[ignore]
        value: 99,
    };
    assert_eq!(record.value, 99);
}
