macro_rules! stringify_expr {
    ($value:expr) => {
        stringify!($value)
    };
}

struct Point {
    x: u8,
    y: u8,
}

fn f(value: &u8) -> &u8 {
    value
}

fn main() {
    let value = 1u8;
    let mut foo = 0;

    assert_eq!(
        stringify_expr!(Point { x: 42, y: 24 }),
        "Point { x: 42, y: 24 }",
    );
    assert_eq!(stringify_expr!(dbg!(&value)), "dbg!(&value)");
    assert_eq!(stringify_expr!(f(&42)), "f(&42)");
    assert_eq!(
        stringify_expr!({
            foo += 1;
            eprintln!("before");
            7331
        }),
        "{ foo += 1; eprintln!(\"before\"); 7331 }",
    );
    assert_eq!(stringify_expr!(("Yeah",)), "(\"Yeah\",)");
    assert_eq!(stringify_expr!("\r"), "\"\\r\"");
    assert_eq!(stringify_expr!(1u8), "1u8");

    let _ = (Point { x: value, y: value }, f(&value), foo);
}
