// time's format-description parser: `ParseTarget<'a, const VERSION: u8, ..>`
// bounds `type Component: TryFrom<AstComponent, Error: Into<Error>>`, and a
// function generic over VERSION calls `error.into()` on the conversion
// error. The alias bound mentions the const parameter, so it is not a global
// where-clause and wins over the blanket `impl<T, U: From<T>> Into<U> for T`
// (rustc's `is_global` counts const parameters as well as type parameters).
// Our globality check looked at type parameters only, took the blanket impl
// and failed on `Error: From<<.. as TryFrom<Ast>>::Error>`.
use std::convert::TryFrom;

#[derive(Debug, PartialEq)]
struct Error(u8);
struct Ast(u8);

trait Target<const VERSION: u8> {
    type Component: TryFrom<Ast, Error: Into<Error>>;
    fn component(component: Self::Component) -> u8;
}

struct Narrow(u8);
struct TooWide(u8);

impl From<TooWide> for Error {
    fn from(e: TooWide) -> Error {
        Error(e.0)
    }
}

impl TryFrom<Ast> for Narrow {
    type Error = TooWide;
    fn try_from(ast: Ast) -> Result<Narrow, TooWide> {
        if ast.0 < 10 { Ok(Narrow(ast.0)) } else { Err(TooWide(ast.0)) }
    }
}

impl Target<1> for () {
    type Component = Narrow;
    fn component(component: Narrow) -> u8 {
        component.0
    }
}

fn parse<const VERSION: u8>(ast: Ast) -> Result<u8, Error>
where
    (): Target<VERSION>,
{
    let component: <() as Target<VERSION>>::Component = match ast.try_into() {
        Ok(value) => value,
        Err(error) => return Err(error.into()),
    };
    Ok(<() as Target<VERSION>>::component(component))
}

fn main() {
    assert_eq!(parse::<1>(Ast(3)), Ok(3));
    assert_eq!(parse::<1>(Ast(30)), Err(Error(30)));
}
