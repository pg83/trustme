fn main() {
    assert_eq!(format!("{:🦀>4}", "x"), "🦀🦀🦀x");
    assert_eq!(format!("{:🦀<3}", 17), "17🦀");
    assert_eq!(format!("{:🦀^5}", "y"), "🦀🦀y🦀🦀");
}
