macro_rules! stringify_expr {
    ($value:expr) => {
        stringify!($value)
    };
}

fn main() {
    assert_eq!(stringify_expr!(1.0).parse::<f64>(), Ok(1.0));
    assert_eq!(stringify_expr!(3e-5).parse::<f64>(), Ok(3e-5));
    assert_eq!(stringify_expr!(12345.).parse::<f64>(), Ok(12345.));
    assert_eq!(stringify_expr!(0.9999999).parse::<f64>(), Ok(0.9999999));
    assert_eq!(
        stringify_expr!(1.448997445238699).parse::<f64>(),
        Ok(1.448997445238699),
    );
}
