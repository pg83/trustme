struct Position { x: u8, y: u8 }
fn main() {
    let panic = std::panic::catch_unwind(|| assert!({
        let base = Position { x: 3, y: 5 };
        let moved = Position { x: 7, ..base };
        let same = Position { ..moved };
        same.x == same.y
    })).unwrap_err();
    let text = panic.downcast_ref::<String>().map(String::as_str)
        .or_else(|| panic.downcast_ref::<&str>().copied()).unwrap();
    let compact: String = text.chars().filter(|c| !c.is_whitespace()).collect();
    assert!(compact.contains("Position{x:7,..base}"));
    assert!(compact.contains("Position{..moved}"));
}
