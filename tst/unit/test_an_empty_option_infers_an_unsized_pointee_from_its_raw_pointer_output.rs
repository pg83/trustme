fn main() {
    let text: Option<&_> = None;
    let text_pointer: Option<*const str> = text.map(|value| value);
    assert!(text_pointer.is_none());

    let bytes: Option<&_> = None;
    let bytes_pointer: Option<*const [u8]> = bytes.map(|value| value);
    assert!(bytes_pointer.is_none());
}
