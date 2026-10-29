//@ proc-macro-aux-build: proc_macro_item_passthrough.rs
macro_rules! relay {
    ($item:item) => { proc_macro_item_passthrough::echo! { $item } };
}
relay! {
    fn from_block(value: Option<u16>) -> u16 {
        let Some(value) = ({ value }) else { return 31; };
        value
    }
}
relay! {
    fn from_if(choose: bool) -> u16 {
        let Some(value) = (if choose { Some(7) } else { None }) else { return 37; };
        value
    }
}
relay! {
    fn from_call(value: Option<u16>) -> u16 {
        let Some(value) = std::hint::black_box(value) else { return 41; };
        value
    }
}
fn main() {
    assert_eq!(from_block(Some(5)), 5);
    assert_eq!(from_block(None), 31);
    assert_eq!(from_if(true), 7);
    assert_eq!(from_if(false), 37);
    assert_eq!(from_call(Some(11)), 11);
    assert_eq!(from_call(None), 41);
}
