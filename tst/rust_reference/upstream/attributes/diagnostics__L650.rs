// Extracted from src/attributes/diagnostics.md:650
pub trait Expression {
    type SqlType;
}

pub trait AsExpression<ST> {
    type Expression: Expression<SqlType = ST>;
}

pub struct Text;
pub struct Integer;

pub struct Bound<T>(T);
pub struct SelectInt;

impl Expression for SelectInt {
    type SqlType = Integer;
}

impl<T> Expression for Bound<T> {
    type SqlType = T;
}

impl AsExpression<Integer> for i32 {
    type Expression = Bound<Integer>;
}

impl AsExpression<Text> for &'_ str {
    type Expression = Bound<Text>;
}

impl<T> Foo for T where T: Expression {}

// Uncomment this line to change the recommendation.
// #[diagnostic::do_not_recommend]
impl<T, ST> AsExpression<ST> for T
where
    T: Expression<SqlType = ST>,
{
    type Expression = T;
}

trait Foo: Expression + Sized {
    fn check<T>(&self, _: T) -> <T as AsExpression<<Self as Expression>::SqlType>>::Expression
    where
        T: AsExpression<Self::SqlType>,
    {
        todo!()
    }
}

fn main() {
    SelectInt.check("bar");
}
