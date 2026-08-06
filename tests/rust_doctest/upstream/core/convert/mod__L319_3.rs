// Extracted from library/core/src/convert/mod.rs:319
struct Document {
    info: String,
    content: Vec<u8>,
}

impl<T: ?Sized> AsMut<T> for Document
where
    Vec<u8>: AsMut<T>,
{
    fn as_mut(&mut self) -> &mut T {
        self.content.as_mut()
    }
}

fn caesar<T: AsMut<[u8]>>(data: &mut T, key: u8) {
    for byte in data.as_mut() {
        *byte = byte.wrapping_add(key);
    }
}

fn null_terminate<T: AsMut<Vec<u8>>>(data: &mut T) {
    // Using a non-generic inner function, which contains most of the
    // functionality, helps to minimize monomorphization overhead.
    fn doit(data: &mut Vec<u8>) {
        let len = data.len();
        if len == 0 || data[len-1] != 0 {
            data.push(0);
        }
    }
    doit(data.as_mut());
}

fn main() {
    let mut v: Vec<u8> = vec![1, 2, 3];
    caesar(&mut v, 5);
    assert_eq!(v, [6, 7, 8]);
    null_terminate(&mut v);
    assert_eq!(v, [6, 7, 8, 0]);
    let mut doc = Document {
        info: String::from("Example"),
        content: vec![17, 19, 8],
    };
    caesar(&mut doc, 1);
    assert_eq!(doc.content, [18, 20, 9]);
    null_terminate(&mut doc);
    assert_eq!(doc.content, [18, 20, 9, 0]);
}
