macro_rules! packets {
    ($($(one two)+);* end) => {
        881u16
    };
}

fn main() {
    assert_eq!(packets!(end), 881);
    assert_eq!(packets!(one two one two; one two end), 881);
}
