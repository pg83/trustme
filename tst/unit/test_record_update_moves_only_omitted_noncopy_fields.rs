struct Record {
    tag: u8,
    text: String,
}

fn main() {
    let base = Record { tag: 7, text: String::from("first") };
    let moved = Record { tag: 11, ..base };
    assert_eq!(base.tag, 7);
    assert_eq!(moved.text, "first");

    let copied = Record { text: String::from("replacement"), ..moved };
    assert_eq!(copied.tag, 11);
    assert_eq!(moved.text, "first");
    assert_eq!(copied.text, "replacement");
}
