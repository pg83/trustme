//@ input: aux/valid_utf8_including_nul.txt
//@ input: aux/valid_utf8_format_fill.txt

fn main() {
    let text = include_str!("aux/valid_utf8_including_nul.txt");
    assert_eq!(text.as_bytes(), include_bytes!("aux/valid_utf8_including_nul.txt"));
    assert_eq!(text.chars().count(), 6);
    assert_eq!(format!(include_str!("aux/valid_utf8_format_fill.txt"), 7), "ééé7");
}
