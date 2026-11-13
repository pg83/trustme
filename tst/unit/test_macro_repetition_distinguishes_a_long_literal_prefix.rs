macro_rules! packet {
    ($(alpha beta gamma delta)* alpha beta gamma end) => {
        853u16
    };
}

fn main() {
    assert_eq!(packet!(alpha beta gamma end), 853);
    assert_eq!(packet!(alpha beta gamma delta alpha beta gamma end), 853);
}
