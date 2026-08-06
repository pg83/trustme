// Extracted from library/core/src/cmp.rs:191
#[derive(PartialEq)]
enum BookFormat {
    Paperback,
    Hardback,
    Ebook,
}

#[derive(PartialEq)]
struct Book {
    isbn: i32,
    format: BookFormat,
}

impl PartialEq<BookFormat> for Book {
    fn eq(&self, other: &BookFormat) -> bool {
        self.format == *other
    }
}

impl PartialEq<Book> for BookFormat {
    fn eq(&self, other: &Book) -> bool {
        *self == other.format
    }
}

fn main() {
    let b1 = Book { isbn: 1, format: BookFormat::Paperback };
    let b2 = Book { isbn: 2, format: BookFormat::Paperback };

    assert!(b1 == BookFormat::Paperback);
    assert!(BookFormat::Paperback == b2);

    // The following should hold by transitivity but doesn't.
    assert!(b1 == b2); // <-- PANICS
}
