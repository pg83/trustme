//@ edition: 2015
macro_rules! panic {
    ($format:expr,$character:expr,$expected:expr) => {{
        macro_rules! captured_character { () => { stringify!($character) }; }
        assert_eq!(captured_character!(),$expected);
    }};
}
fn main() {
    assert!(false,"{}",'A',"'A'");
    assert!(false,"{}",'\'',r"'\''");
    assert!(false,"{}",'\\',r"'\\'");
    assert!(false,"{}",'\u{a}',r"'\u{a}'");
    assert!(false,"{}",'\u{e9}',r"'\u{e9}'");
}
