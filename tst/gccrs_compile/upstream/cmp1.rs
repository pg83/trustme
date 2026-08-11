#[derive(Clone, Copy, PartialEq, Eq)]
enum BookFormat { Paperback, Hardback, Ebook }

pub struct Book { isbn: i32, format: BookFormat }

impl PartialEq<BookFormat> for Book { fn eq(&self, other: &BookFormat) -> bool { self.format == *other } }
impl PartialEq<Book> for BookFormat { fn eq(&self, other: &Book) -> bool { *self == other.format } }
impl PartialEq for Book { fn eq(&self, other: &Book) -> bool { self.isbn == other.isbn } }

pub fn main() {
    let b1=Book{isbn:1,format:BookFormat::Paperback}; let b2=Book{isbn:2,format:BookFormat::Paperback};
    let _ = b1 == BookFormat::Paperback; let _ = BookFormat::Paperback == b2; let _ = b1 != b2;
}
