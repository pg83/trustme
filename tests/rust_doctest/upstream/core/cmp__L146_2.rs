// Extracted from library/core/src/cmp.rs:146
#![allow(unused)]
fn main() {
    // The derive implements <BookFormat> == <BookFormat> comparisons
    #[derive(PartialEq)]
    enum BookFormat {
        Paperback,
        Hardback,
        Ebook,
    }

    struct Book {
        isbn: i32,
        format: BookFormat,
    }

    // Implement <Book> == <BookFormat> comparisons
    impl PartialEq<BookFormat> for Book {
        fn eq(&self, other: &BookFormat) -> bool {
            self.format == *other
        }
    }

    // Implement <BookFormat> == <Book> comparisons
    impl PartialEq<Book> for BookFormat {
        fn eq(&self, other: &Book) -> bool {
            *self == other.format
        }
    }

    let b1 = Book { isbn: 3, format: BookFormat::Paperback };

    assert!(b1 == BookFormat::Paperback);
    assert!(BookFormat::Ebook != b1);
}
