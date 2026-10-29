fn main() {
    assert_eq!(format!("{:界>4}", 7), "界界界7");
    assert_eq!(format!("{:界^5}", "x"), "界界x界界");
    assert_eq!(format!("{:☀<width$}", "ok", width = 4), "ok☀☀");
    assert_eq!(format!("{:🦀>3}", 8), "🦀🦀8");
}
