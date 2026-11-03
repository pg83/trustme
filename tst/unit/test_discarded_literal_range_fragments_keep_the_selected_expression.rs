macro_rules! select {
    ($pattern:pat => $result:expr) => { $result };
}

fn main() {
    assert_eq!(select!(b"alpha"..=b"omega" => 13), 13);
    assert_eq!(select!("alpha"..="omega" => 17), 17);
    assert_eq!(select!(| ..=b"omega" => 19), 19);
    assert_eq!(select!(| ..="omega" => 23), 23);
}
