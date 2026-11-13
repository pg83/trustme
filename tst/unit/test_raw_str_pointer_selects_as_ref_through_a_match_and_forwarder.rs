struct Text(&'static str);

impl AsRef<str> for Text {
    fn as_ref(&self) -> &str {
        self.0
    }
}

impl AsRef<[u8]> for Text {
    fn as_ref(&self) -> &[u8] {
        self.0.as_bytes()
    }
}

fn forward<T: ?Sized>(value: &T) -> &T {
    value
}

fn view(text: &Text, first: bool) -> *const str {
    let identity = |value| value;
    let value = match first {
        true => identity(text.as_ref()),
        false => forward(text.as_ref()),
    };
    value
}

fn main() {
    let text = Text("lambda: λ");
    for first in [false, true] {
        let value = unsafe { &*view(&text, first) };
        assert_eq!(value, text.0);
        assert_eq!(value.as_ptr(), text.0.as_ptr());
        assert_eq!(value.len(), text.0.len());
    }
}
