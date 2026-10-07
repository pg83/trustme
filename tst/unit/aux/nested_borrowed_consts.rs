// What time's `format_description!(version = 2, "[hour]:[minute][optional [.[second]]]")`
// expands to inside rusqlite 0.39: a const block borrowing an array whose
// elements call a const fn and borrow string literals and further items.
#[derive(Debug, PartialEq)]
pub struct Padding(pub u8);

impl Padding {
    pub const fn default() -> Self {
        Padding(2)
    }
}

#[derive(Debug, PartialEq)]
pub enum Item<'a> {
    Component(Padding),
    StringLiteral(&'a str),
    Compound(&'a [Item<'a>]),
    Optional(&'a Item<'a>),
}

const TIME_FORMAT: &[Item<'_>] = const {
    (&[
        Item::Component(<Padding>::default()),
        Item::StringLiteral(":"),
        Item::Optional(&Item::Compound(&[Item::StringLiteral("."), Item::Component(<Padding>::default())])),
    ]) as &'static [Item<'static>]
};

pub fn time_format() -> &'static [Item<'static>] {
    TIME_FORMAT
}
