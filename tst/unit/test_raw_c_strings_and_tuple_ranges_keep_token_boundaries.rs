macro_rules! select_range {
    ($value:ident . $field:tt ... $end:literal) => {
        ($value.$field, $end)
    };
}

fn main() {
    let branded = (953u16, 967u16);
    let read = select_range!(branded.0...971u16);
    assert_eq!(read, (953, 971));
    assert_eq!(select_range!(branded.1...977u16), (967, 977));
    let quoted = cr###"raw "C" body"###;
    assert_eq!(quoted.to_bytes(), b"raw \"C\" body");
}
