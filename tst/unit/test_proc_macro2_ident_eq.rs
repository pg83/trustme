#[derive(Clone)]
struct InnerIdent(String);

impl PartialEq for InnerIdent {
    fn eq(&self, other: &InnerIdent) -> bool {
        self.0 == other.0
    }
}

impl<T> PartialEq<T> for InnerIdent
where
    T: ?Sized + AsRef<str>,
{
    fn eq(&self, other: &T) -> bool {
        self.0 == other.as_ref()
    }
}

struct Ident {
    inner: InnerIdent,
}

impl PartialEq for Ident {
    fn eq(&self, other: &Ident) -> bool {
        self.inner == other.inner
    }
}

impl<T> PartialEq<T> for Ident
where
    T: ?Sized + AsRef<str>,
{
    fn eq(&self, other: &T) -> bool {
        self.inner == other
    }
}

fn main() {
    let ident = Ident {
        inner: InnerIdent(String::from("name")),
    };
    assert!(ident == "name");
}
