macro_rules! printed {
    ($value:expr) => { stringify!($value) };
}

fn main() {
    assert_eq!(printed!(loop { break }), "loop { break }");
    assert_eq!(printed!(while false { break; }), "while false { break; }");

    let block = printed!({
        #[allow(unused)]
        let value = 37;
        value
    });
    assert!(block.contains("#[allow(unused)]"));
    assert!(block.ends_with('}'));

    let invocation = printed!(unexpanded! { outer { inner } });
    assert!(invocation.starts_with("unexpanded!"));
    assert!(invocation.contains("inner"));
    assert!(invocation.ends_with('}'));

    let matched = printed!(match 0 {
        #[allow(unused)]
        0 => { 17 },
        _ => 31,
    });
    assert!(matched.contains("#[allow(unused)]"));
    assert!(matched.ends_with('}'));
}
