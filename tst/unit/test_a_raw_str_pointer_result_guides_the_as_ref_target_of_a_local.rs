fn string_pointer(text: &String) -> *const str {
    let view = text.as_ref();
    view
}

fn main() {
    let text = String::from("coast");
    let pointer = string_pointer(&text);
    unsafe {
        assert_eq!(&*pointer, "coast");
    }
}
