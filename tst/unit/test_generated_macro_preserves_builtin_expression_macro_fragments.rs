macro_rules! panic {
    ($message:expr) => {{
        macro_rules! captured_message { () => { $message }; }
        let message: &str = captured_message!();
        std::panic!("{}", message);
    }};
}
macro_rules! message { () => { "unexpected" }; }
fn main() {
    assert!(true, message!());
    assert!(true, message!{});
}
