fn main() {
    let value = "x";

    assert_eq!(format!("<{value    }>"), "<x>");
    assert_eq!(format!("<{value   :>3}>"), "<  x>");
    assert_eq!(format!("<{value:>3   }>"), "<  x>");
}
