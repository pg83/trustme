// Extracted from library/core/src/cmp.rs:115
#![allow(unused)]
fn main() {
    enum BookFormat {
        Paperback,
        Hardback,
        Ebook,
    }

    struct Book {
        isbn: i32,
        format: BookFormat,
    }

    impl PartialEq for Book {
        fn eq(&self, other: &Self) -> bool {
            self.isbn == other.isbn
        }
    }

    let b1 = Book { isbn: 3, format: BookFormat::Paperback };
    let b2 = Book { isbn: 3, format: BookFormat::Ebook };
    let b3 = Book { isbn: 10, format: BookFormat::Paperback };

    assert!(b1 == b2);
    assert!(b1 != b3);
}
