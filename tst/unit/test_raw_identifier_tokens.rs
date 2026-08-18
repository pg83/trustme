// A raw identifier prints back the way it was written, and a macro matcher
// tells `r#a` apart from `a`.
macro_rules! pick {
    (a) => { 6 };
    (r#a) => { 7 };
}

macro_rules! r#struct {
    ($r#struct:expr) => { $r#struct };
}

fn main() {
    let r#match = 5;
    assert_eq!(r#match, 5);
    assert_eq!(r#struct!(2), 2);
    assert_eq!(stringify!(r#struct), "r#struct");
    assert_eq!(stringify!(struct), "struct");
    assert_eq!(pick!(a), 6);
    assert_eq!(pick!(r#a), 7);
}
