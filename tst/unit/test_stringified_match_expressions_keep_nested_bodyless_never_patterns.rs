#![feature(never_patterns)]
enum Empty {}
macro_rules! inspect {
    ($body:expr) => {{
        let spelling = stringify!($body);
        assert!(spelling.contains('!'));
        $body
    }};
}
fn main() {
    let value: Option<Empty> = None;
    let answer = inspect!(match value { Some(!), None => 317u16 });
    assert_eq!(answer, 317);
}
