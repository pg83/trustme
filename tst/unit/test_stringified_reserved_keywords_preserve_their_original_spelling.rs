macro_rules! text { ($token:tt) => { stringify!($token) }; }

fn main() {
    assert_eq!(text!(priv), "priv");
    assert_eq!(text!(continue), "continue");
    assert_eq!(text!(try), "try");
}
