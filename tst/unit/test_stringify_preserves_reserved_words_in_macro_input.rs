macro_rules! text {
    ($token:tt) => { stringify!($token) };
}

fn main() {
    assert_eq!(text!(abstract), "abstract");
    assert_eq!(text!(final), "final");
    assert_eq!(text!(override), "override");
    assert_eq!(text!(virtual), "virtual");
    assert_eq!(text!(typeof), "typeof");
    assert_eq!(text!(unsized), "unsized");
    assert_eq!(text!(macro), "macro");
}
