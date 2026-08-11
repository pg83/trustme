macro_rules! introduce_local {
    () => {
        let value = 99;
    };
}

fn main() {
    let value = 7;
    introduce_local!();
    assert_eq!(value, 7);
}
