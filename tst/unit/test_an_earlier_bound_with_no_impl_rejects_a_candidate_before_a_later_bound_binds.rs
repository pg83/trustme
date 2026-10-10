// diesel's `IntoUpdateTarget`: the blanket impl for `T: Identifiable<Table =
// Tab>` also has `Find<Tab, T::Id>: IntoUpdateTarget<WhereClause = V>`, and
// checking `SelectStatement`'s own impl normalizes `<Select<F, W> as
// IntoUpdateTarget>::WhereClause` with the blanket impl a candidate too.
// Upstream takes a candidate's nested goals in order and drops it at the first
// with no solution - `Select<F, W>: Identifiable` has none, generic `F` and `W`
// or not - so the later bound, whose `T` is one `find` deeper every time it is
// read, is never reached.
pub trait HasTable {
    type Table;
    fn table(&self) -> Self::Table;
}

pub trait Identifiable: HasTable {
    type Id;
    fn id(self) -> Self::Id;
}

pub trait Table: Copy {}

pub trait FindDsl<PK> {
    type Output;
    fn find(self, id: PK) -> Self::Output;
}

pub type Find<S, PK> = <S as FindDsl<PK>>::Output;

pub trait IntoUpdateTarget: HasTable {
    type WhereClause;
    fn into_update_target(self) -> (Self::Table, Self::WhereClause);
}

impl<T, Tab, V> IntoUpdateTarget for T
where
    T: Identifiable<Table = Tab>,
    Tab: Table + FindDsl<T::Id>,
    Find<Tab, T::Id>: IntoUpdateTarget<Table = Tab, WhereClause = V>,
{
    type WhereClause = V;
    fn into_update_target(self) -> (Self::Table, Self::WhereClause) {
        let table = self.table();
        table.find(self.id()).into_update_target()
    }
}

pub struct Select<F, W>(F, W);

impl<F: Table, W> HasTable for Select<F, W> {
    type Table = F;
    fn table(&self) -> F {
        self.0
    }
}

impl<F: Table, W> IntoUpdateTarget for Select<F, W>
where
    Self: HasTable<Table = F>,
{
    type WhereClause = W;
    fn into_update_target(self) -> (Self::Table, Self::WhereClause) {
        (self.0, self.1)
    }
}

impl<T: Table, PK> FindDsl<PK> for T {
    type Output = Select<T, PK>;
    fn find(self, id: PK) -> Select<T, PK> {
        Select(self, id)
    }
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Users;

impl Table for Users {}

fn main() {
    let (t, w) = Users.find(7u32).into_update_target();
    assert_eq!(t, Users);
    assert_eq!(w, 7);
}
