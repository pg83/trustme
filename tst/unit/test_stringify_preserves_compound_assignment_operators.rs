macro_rules! text {
    ($token:tt) => { stringify!($token) };
}

fn main() {
    assert_eq!(text!(+=), "+=");
    assert_eq!(text!(-=), "-=");
    assert_eq!(text!(*=), "*=");
    assert_eq!(text!(/=), "/=");
    assert_eq!(text!(%=), "%=");
    assert_eq!(text!(&=), "&=");
    assert_eq!(text!(|=), "|=");
    assert_eq!(text!(^=), "^=");
}
